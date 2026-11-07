#!/usr/bin/env python3
"""Pinned, fail-closed NativeUfs transformation for the CI194-only build.

No file mutation at import or by these functions. The caller writes returned
sources and links the original VerifiedRoot implementation and crypto adapter.
This build permanently refuses all store/log/export/panic writes and runtime
queue entry; the only active transport is locked direct read-only commands.
"""
from pathlib import Path
import re

HERE = Path(__file__).resolve().parent


def _once(text, old, new):
    if text.count(old) != 1:
        raise ValueError('CI194 requires exactly one pinned anchor: ' + repr(old[:110]))
    return text.replace(old, new, 1)


def _span(text, signature):
    if text.count(signature) != 1:
        raise ValueError('CI194 function anchor mismatch: ' + signature)
    first = text.index(signature)
    opening = text.index('{', first + len(signature))
    depth, at, state = 1, opening + 1, 'code'
    while at < len(text):
        a = text[at]
        b = text[at:at+2]
        if state == 'line':
            if a == '\n': state = 'code'
        elif state == 'comment':
            if b == '*/': state = 'code'; at += 1
        elif state in ('"', "'"):
            if a == '\\': at += 1
            elif a == state: state = 'code'
        elif b == '//': state = 'line'; at += 1
        elif b == '/*': state = 'comment'; at += 1
        elif a in ('"', "'"): state = a
        elif a == '{': depth += 1
        elif a == '}':
            depth -= 1
            if not depth: return first, opening, at + 1
        at += 1
    raise ValueError('CI194 unbalanced function: ' + signature)


def _body(text, signature, body):
    _, opening, end = _span(text, signature)
    return text[:opening] + '{\n' + body + '\n}' + text[end:]


def _prepend(text, signature, code):
    _, opening, _ = _span(text, signature)
    return text[:opening+1] + '\n' + code + text[opening+1:]


def transform_header(name: str, text: str) -> str:
    name = name.replace('\\', '/').rsplit('/', 1)[-1]
    if 'CI194_PERMANENT_READONLY' in text:
        raise ValueError('CI194 header transformed twice: ' + name)
    if name == 'RootCommands.h':
        text = _prepend(text, 'static IOReturn hostSubmit (',
                        '  // CI194_PERMANENT_READONLY: reject before touching DMA or MMIO.\n'
                        '  if (direction == 1) { *ocs = 15; return kIOReturnNotPermitted; }\n')
        text = _prepend(text, 'static IOReturn hostScsiDir (',
                        '  // CI194 only allows discovery/READ10 and the reviewed device wake.\n'
                        '  if (!cdb || !scsiStatus) return kIOReturnBadArgument;\n'
                        '  const bool read = cdb[0] == 0x00 || cdb[0] == 0x12 || cdb[0] == 0x25 || cdb[0] == 0x28;\n'
                        '  const bool wake = lun == 0xd0 && cdbLength == 6 && cdb[0] == 0x1b &&\n'
                        '    cdb[1] == 0 && cdb[2] == 0 && cdb[3] == 0 && cdb[4] == 0x10 && cdb[5] == 0;\n'
                        '  if (direction == 1 || (!read && !wake)) { *scsiStatus = 0xffff; return kIOReturnNotPermitted; }\n')
        for signature in ('static bool hostStoreOpen (', 'static bool hostLogOpen ('):
            text = _body(text, signature, '  return false; // CI194 never opens a write extent.')
        for signature in ('static IOReturn hostWrite4k (', 'static IOReturn hostSyncCache ('):
            text = _body(text, signature, '  return kIOReturnNotPermitted; // CI194 refuses every write/flush path.')
    elif name == 'RuntimeQueue.h':
        text = _body(text, 'static bool runtimeQueueStart (',
                     '  *why = "CI194 prohibits runtime queue installation";\n  return false; // CI194_PERMANENT_READONLY')
        text = _body(text, 'static IOReturn runtimeCommand (',
                     '  return kIOReturnNotPermitted; // CI194 has no runtime/power/recovery command path.')
        text = _body(text, 'static bool runtimePanicWrite (',
                     '  return false; // CI194 permanently refuses the separate panic slot.')
    return text


def transform_ufs(text: str) -> str:
    if 'CI194_EXTERNAL_LOADER' in text or 'CI193 POLICY BEGIN' in text:
        raise ValueError('CI194 requires one untransformed NativeUfs without CI193 writer injection')
    text = _once(text, '#include "VerifiedBlockPolicy.h"',
                 '#include "VerifiedBlockPolicy.h"\n#include "CI194Policy.h"\n#include <libkern/OSKextLib.h>\n// CI194_EXTERNAL_LOADER')
    # CI194 uses the original locked command path. No runtime queue is started,
    # so runtime PM and its recovery/write paths cannot acquire the controller.
    queue = re.compile(r'      if \((?:false && )?!runtimeQueueStart \(why\)\) \{\n        gHost.ready = false;\n        return false;\n      \}')
    text, count = queue.subn('      if (gQueue.installed) {\n        *why = "CI194 unexpected runtime queue";\n        gHost.ready = false;\n        return false;\n      }', text)
    if count != 1: raise ValueError('CI194 pinned runtimeQueueStart call changed')
    # Exported write APIs are refused even if somebody passes altered bootargs.
    for name in ('native_ufs_store_open', 'native_ufs_log_open'):
        text = _body(text, 'extern "C" int ' + name + ' (',
                     '  if (why) *why = "CI194 read-only mode";\n  return 0;')
    for name in ('native_ufs_store_read', 'native_ufs_store_write', 'native_ufs_store_sync',
                 'native_ufs_log_write', 'native_ufs_export_write', 'native_ufs_log_sync', 'native_ufs_log_panic_write'):
        text = _body(text, 'extern "C" int ' + name + ' (', '  return 0; // CI194 refuses persistent store/log/export.')
    text = _body(text, 'extern "C" uint64_t native_ufs_export_blocks (', '  return 0;')
    text = _once(text, '// One read-only window of the root image as a block device:',
                 (HERE / 'ci194-policy-state.inc').read_text() + '\n// One read-only window of the root image as a block device:')
    text = _once(text, '  bool signedVolumeUuid (char out[37]) const;',
                 '  bool signedVolumeUuid (char out[37]) const;\n  bool ci194SelfTest ();')
    first, _, end = _span(text, 'bool NativeUfsBlock::configureVerification ()')
    text = text[:first] + (HERE / 'ci194-policy-loader.inc').read_text() + text[end:]
    # Count only successful checks in the real doAsyncReadWrite wrapper.
    text = _once(text, '      __atomic_fetch_add (&native_ufs_verified_blocks, 1, __ATOMIC_RELAXED);',
                 '      ci194CountVerified (view);\n      __atomic_fetch_add (&native_ufs_verified_blocks, 1, __ATOMIC_RELAXED);')
    text = _once(text, '  if (verifiedStorage) {\n    kmem_free (kernel_map, verifiedStorage, verifiedStorageBytes);\n    verifiedStorage = 0;\n  }',
                 '  online = false;\n  verified = {};\n'
                 '  for (unsigned p = 0; p < 2; ++p) if (gCi194Windows[p] == this) {\n'
                 '    gCi194Windows[p] = nullptr; gCi194HashPointers[p] = nullptr; gCi194PolicyLoaded[p] = false;\n'
                 '    bzero (gCi194Envelope384[p], 48);\n  }\n'
                 '  if (verifiedStorage) kmem_free (kernel_map, verifiedStorage, verifiedStorageBytes);\n'
                 '  verifiedStorage = 0; verifiedStorageBytes = 0;')
    text = _once(text, '      IOLog ("NativeUfs: published %s\\n", device->windowLabel ());',
                 '      IOLog ("NativeUfs: published %s\\n", device->windowLabel ());\n'
                 '      ci194Print ("CI194 PUBWIN ");')
    text = _once(text, '      bool ready = false;',
                 '      d2xq_console_text ("CI194 MEDIA ");\n      bool ready = false;')
    text = _once(text, '        IOSleep (100);', '        if ((tick % 100) == 0) ci194Print (\"m\");\n        ci194Wait (100);')
    text = _once(text, '        ready = mediaWithUuidExists (uuid, device);',
                 '        ci194Print ("CI194 MEDIACHK ");\n        ready = true;')
    text = _once(text, '  gPendingProvider->release ();  // Balanced with successful thread dispatch.',
                 '  if (result.phase != UfsRootPhase::Complete) ci194Fail ("publisher failed");\n'
                 '  __atomic_store_n (&gCi194Publisher, 2U, __ATOMIC_RELEASE);\n'
                 '  if (ci194DoneSem) semaphore_signal (ci194DoneSem);\n'
                 '  gPendingProvider->release ();  // Balanced with successful thread dispatch.')
    finish_anchor = '// Memory snapshot (native-ufs-probe=13):'
    text = _once(text, finish_anchor, '#if defined(NATIVE_VERIFIED_BLOCK) && NATIVE_VERIFIED_BLOCK\n' +
                 (HERE / 'ci194-policy-finish.inc').read_text() + '\n#endif\n\n' + finish_anchor)
    text = _prepend(text, 'bool NativeUfs::start (IOService *provider)',
                    '  unsigned ci194 = 0, ci194Root = 0, ci194Probe = 0, ci194Discover = 0;\n'
                    '  PE_parse_boot_argn ("native-ufs-ci194", &ci194, sizeof (ci194));\n'
                    '  PE_parse_boot_argn ("native-ufs-root", &ci194Root, sizeof (ci194Root));\n'
                    '  PE_parse_boot_argn ("native-ufs-probe", &ci194Probe, sizeof (ci194Probe));\n'
                    '  PE_parse_boot_argn ("native-ufs-discover", &ci194Discover, sizeof (ci194Discover));\n'
                    '  if (ci194 != 1 || ci194Root != 1 || ci194Probe || ci194Discover ||\n'
                    '      gCi194Entered || storeMode () || logMode () || gQueue.installed || gStoreOpen || gLogOpen)\n'
                    '    ci194Fail ("mode, repeated entry, or writable state");\n'
                    '  gCi194Entered = true; d2xq_ufs_stage = -1;\n')
    text = _once(text, '  gRootDiagnosticEnabled = diagnostic == 1;',
                 '  gRootDiagnosticEnabled = true; // CI194 failures are terminal.')
    old = '''  if (!base || base != &IOBlockStorageDevice::gMetaClass ||
      base->getClassSize () != sizeof (IOBlockStorageDevice) ||
      base->getSuperClass () != &IOService::gMetaClass ||
      IOService::gMetaClass.getClassSize () != sizeof (IOService)) {'''
    new = '''  const OSMetaClass *local = &IOBlockStorageDevice::gMetaClass;
  if (!base || base == local || base->getClassSize () != sizeof (IOBlockStorageDevice) ||
      base->getSuperClass () != &IOService::gMetaClass ||
      IOService::gMetaClass.getClassSize () != sizeof (IOService) ||
      strcmp (local->getClassName (), "UniXNUBlockStorageDevice") ||
      local->getSuperClass () != base || local->getClassSize () != sizeof (IOBlockStorageDevice) ||
      NativeUfsBlock::gMetaClass.getSuperClass () != local) {'''
    text = _once(text, old, new)
    text = _once(text, '  gStoreGeometry = { true, layout->userdataFirst, layout->userdataLast, layout->imageBytes, layout->gptEntriesLba,\n                     layout->gptHeaderCrc, layout->gptEntriesCrc, layout->gptEntryCount };',
                 '  ci194Begin (*layout);\n  gStoreGeometry = {}; // No CI194 store window exists.')
    text = _once(text, '      if (!device || !device->init (static_cast<OSDictionary *> (nullptr)) ||',
                 '      if (!device || device->getMetaClass () != &NativeUfsBlock::gMetaClass ||\n'
                 '          !device->metaCast ("IOBlockStorageDevice") || !device->metaCast ("UniXNUBlockStorageDevice") ||\n'
                 '          !device->init (static_cast<OSDictionary *> (nullptr)) ||')
    text = _once(text, '      owner->retain ();\n      if (kernel_thread_start (publishThread, nullptr, &thread) != KERN_SUCCESS) {',
                 '      ci194SemCreate ();\n'
      '      if (__atomic_load_n (&gCi194Publisher, __ATOMIC_ACQUIRE)) return false;\n'
                 '      __atomic_store_n (&gCi194Publisher, 1U, __ATOMIC_RELEASE);\n'
                 '      owner->retain ();\n      if (kernel_thread_start (publishThread, nullptr, &thread) != KERN_SUCCESS) {')
    if 'verified_root_policies[index]' in text or 'CI193 POLICY BEGIN' in text:
        raise ValueError('CI194 obsolete embedded policy or writer path survived')
    return text
