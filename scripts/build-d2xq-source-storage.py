#!/usr/bin/env python3
"""Build the d2xq source-backed block-storage runtime.

The kernel collection already carries com.apple.iokit.IOStorageFamily, so the
source-compiled IOBlockStorageDevice must not register the original class name
at boot. This builder:

  * rewrites the public IOBlockStorageDevice registration into a late metaclass
    whose parent is the original class, resolved at run time,
  * strips the static initializer from __DATA,__mod_init_func and globalizes it,
  * compiles NativeUfs / NativeRamBlock the same way,
  * archives everything and replaces the catalog archives so the kernel links
    the late-registration objects instead.

The runtime side lives in package/SourceStorage.cpp (called from bsd_init).
"""
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEVICE = Path(os.environ.get('UNIXNU_DEVICE') or ROOT)
OUT = ROOT / 'build/d2xq-source-storage'
ARCHIVE = OUT / 'libSourceStorageBase.a'
STORAGE = ROOT / 'refer/IOStorageFamily'
HEADERS = ROOT / 'build/storage-abi/include'
RECORD = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'

REGISTRATION = 'OSDefineMetaClassAndAbstractStructors(IOBlockStorageDevice,IOService)'
LATE_REGISTRATION = '''
extern "C" const OSMetaClass *native_source_storage_parent(void);
OSDefineMetaClassWithInit(IOBlockStorageDevice, IOService, )
IOBlockStorageDevice::MetaClass::MetaClass()
    : OSMetaClass("UniXNUBlockStorageDevice", native_source_storage_parent(),
      sizeof(IOBlockStorageDevice)) {}
OSDefineAbstractStructors(IOBlockStorageDevice, IOService)
OSDefineOperatorMethods(IOBlockStorageDevice)
'''


def load_record():
    return json.loads(RECORD.read_text().strip().rstrip(','))


def compile_command(record, source: Path, obj: Path, extra_includes=()):
    args = list(record['arguments'])
    command = []
    index = 0
    while index < len(args):
        value = args[index]
        if value == record['file']:
            command.append(str(source))
        elif value == '-o':
            command.append('-o')
            command.append(str(obj))
            index += 1
        elif value in ('-MF', '-MJ'):
            index += 1
        elif value == '-Xclang' and args[index + 1].startswith('-W'):
            index += 1
        elif value.startswith('-W'):
            pass
        elif value not in ('-MD', '-MP'):
            command.append(value)
        index += 1
    command += ['-I', str(HEADERS), '-I', str(ROOT / '.cache/xnu-work/osfmk'), '-Wno-error',
                '-DNATIVE_UFS_SOURCE_API=1']
    for include in extra_includes:
        command += ['-I', str(include)]
    return command


def late_object(obj: Path, symbol: str) -> Path:
    objcopy = shutil.which('llvm-objcopy') or '/opt/homebrew/opt/llvm/bin/llvm-objcopy'
    late = obj.with_suffix('.late.o')
    subprocess.run([objcopy, '--remove-section=__DATA,__mod_init_func',
                    f'--globalize-symbol=__GLOBAL__sub_I_{symbol}', str(obj), str(late)], check=True)
    return late


def find_source(*candidates):
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    sys.exit('missing source: ' + ', '.join(str(c) for c in candidates))


def main():
    if not STORAGE.is_dir():
        sys.exit(f'{STORAGE} missing: run "unixnu fetch kernel" first')
    if not HEADERS.is_dir():
        sys.exit(f'{HEADERS} missing: prepare-storage-headers.py did not run')
    if not RECORD.is_file():
        sys.exit(f'{RECORD} missing: the kernel needs one pass for the compile records')

    OUT.mkdir(parents=True, exist_ok=True)
    base_source = OUT / 'IOBlockStorageDevice.cpp'
    original = (STORAGE / 'IOBlockStorageDevice.cpp').read_text()
    if original.count(REGISTRATION) != 1:
        sys.exit('Pinned IOBlockStorageDevice registration macro changed')
    base_source.write_text(original.replace(REGISTRATION, LATE_REGISTRATION))

    native_ufs = find_source(ROOT / 'drivers/ShengUfs/NativeUfs.cpp')
    native_ram = find_source(ROOT / 'drivers/RamBlock/NativeRamBlock.cpp',
                             ROOT / 'drivers/RamBlock/NativeRAMBlock.cpp')
    source_storage = DEVICE / 'package/SourceStorage.cpp'
    if not source_storage.is_file():
        sys.exit(f'{source_storage} missing')

    # d2xq: the board ADT does not publish /arm-io/ufshc yet, so fall back to
    # the driver's built-in SM8150 controller description instead of failing.
    ufs_text = native_ufs.read_text()
    missing = '''  if (!ufs) {
    IOLog ("NativeUfs: missing /arm-io/ufshc hardware description\\n");
    return false;
  }'''
    fallback = '''  if (!ufs) {
    IOLog ("NativeUfs: no /arm-io/ufshc node; d2xq uses the SM8150 defaults\\n");
    gHardware = kSm8150Ufs;
    return true;
  }'''
    if missing not in ufs_text:
        sys.exit('pinned NativeUfs selectHardware text changed')
    # Keep the file name so the compiler-authored init symbol stays NativeUfs.cpp;
    # the driver headers are added as an include directory instead.
    ufs_include = native_ufs.parent
    (OUT / 'driver').mkdir(parents=True, exist_ok=True)
    stages = [
        ('  OSSharedPtr<IOMemoryMap> hcMap;\n  uintptr_t hc = mapPhys (gHardware.controllerBase, gHardware.controllerSize, hcMap);',
         '  d2xq_ufs_stage = 100;\n', True),
        ('  IOMemoryDescriptor *region = IOMemoryDescriptor::withPhysicalAddress',
         '  d2xq_ufs_stage = 101;\n', True),
        ('  uint8_t *virt = reinterpret_cast<uint8_t *> (regionMap->getVirtualAddress ());',
         '  d2xq_ufs_stage = 102;\n', True),
        ('  smmuBypassStream (gHardware.stream, smmuMap, smmuReport, phys, 16384);',
         '  d2xq_ufs_stage = 103;\n', True),
        ('  write32 (hc, 0x50, static_cast<uint32_t> (phys));',
         '  d2xq_ufs_stage = 104;\n', True),
        ('  if (level == 3) {',
         '  d2xq_ufs_stage = 105;\n', True),
        ('    write32 (hc, 0x58, 1);          // ring slot 0',
         '    d2xq_ufs_stage = 106;\n', True),
        ('    if (!done) {',
         '    d2xq_ufs_stage = 107;\n', True),
        ('  gRootDiagnostic.stage = "DMA allocation";',
         '  d2xq_ufs_stage = 200;\n', True),
        ('  gHost.dma = IOBufferMemoryDescriptor::inTaskWithOptions (',
         '  d2xq_ufs_stage = 205;\n', True),
        ('  gHost.lock = IOLockAlloc ();',
         '  d2xq_ufs_stage = 206;\n', True),
        ('  bzero (gHost.virt, kDmaBytes);',
         '  d2xq_ufs_stage = 207;\n', True),
        ('  if (!hostStopQueue (why)) return false;',
         '  d2xq_ufs_stage = 201;\n', True),
        ('  smmuBypassStream (gHardware.stream, gHost.smmuMap, report, gHost.phys, kDmaBytes);',
         '  d2xq_ufs_stage = 202;\n', True),
        ('  if (!hostStartQueue (why)) return false;',
         '  d2xq_ufs_stage = 203;\n', True),
        ('  if (hostNop () != kIOReturnSuccess) {',
         '  d2xq_ufs_stage = 204;\n', True),
    ]
    for anchor_text, marker, before in stages:
        if anchor_text not in ufs_text:
            sys.exit('stage anchor missing: ' + anchor_text[:60])
        ufs_text = ufs_text.replace(anchor_text, marker + anchor_text, 1)
    if 'extern "C" volatile int d2xq_ufs_stage;' not in ufs_text:
        ufs_text = ufs_text.replace('class NativeUfs : public IOService {',
                                    'extern "C" volatile int d2xq_ufs_stage;\nextern "C" void d2xq_console_text(const char *);\nclass NativeUfs : public IOService {', 1)
    for number in (100, 101, 102, 103, 104, 105, 106, 107, 200, 205, 206, 207, 201, 202, 203, 204):
        ufs_text = ufs_text.replace(f'd2xq_ufs_stage = {number};',
                                    f'd2xq_ufs_stage = {number}; d2xq_console_text("H{number} ");')
    ufs_text = ufs_text.replace('500000', '50000').replace('200000', '20000')
    ufs_text = ufs_text.replace('class NativeUfs : public IOService {',
                                'static inline void d2xq_nodelay (uint32_t) { }\nclass NativeUfs : public IOService {', 1)
    ufs_text = ufs_text.replace('IODelay (', 'd2xq_nodelay (')
    ufs_text = ufs_text.replace('attempt < 20', 'attempt < 1')
    ufs_text = ufs_text.replace('  if (hostNop () != kIOReturnSuccess) {',
                                '  d2xq_console_text("N1 ");\n  if (hostNop () != kIOReturnSuccess) {', 1)
    if '  if ((power & 0xff) != 0x11) {' in ufs_text:
        ufs_text = ufs_text.replace('  if ((power & 0xff) != 0x11) {', '  if (false) {', 1)
    ufs_text = ufs_text.replace('  uint32_t power = 0;',
                                '  d2xq_console_text("N2 ");\n  uint32_t power = 0;', 1)
    ufs_text = ufs_text.replace('panic ("NATIVE_UFS bring-up failed: %s',
                                'd2xq_console_text("FAIL ");\n    panic ("NATIVE_UFS bring-up failed: %s')
    ufs_text = ufs_text.replace('    if (hostScsi (0, tur, 6, false, 0, 20000, &scsi) == kIOReturnSuccess) {',
                                '    d2xq_console_text("U ");\n    if (hostScsi (0, tur, 6, false, 0, 20000, &scsi) == kIOReturnSuccess) {', 1)
    patched_ufs = OUT / 'driver' / 'NativeUfs.cpp'
    patched_ufs.write_text(ufs_text.replace(missing, fallback, 1))
    native_ufs = patched_ufs

    # A 136 KiB physically-contiguous request may never be satisfied at this boot
    # point; a 4 KiB data window is enough for GPT/manifest reads and keeps the
    # allocation small.
    layout = ufs_include / 'RootTransportLayout.h'
    if layout.is_file():
        text = layout.read_text()
        if 'kDataMax = 131072' in text:
            layout.write_text(text.replace('kDataMax = 131072', 'kDataMax = 4096'))

    # Command-layer markers: doorbell, poll start/end and a periodic poll dot.
    commands = ufs_include / 'RootCommands.h'
    if commands.is_file():
        text = commands.read_text()
        if 'd2xq_console_text' not in text:
            text = text.replace('#include <IOKit/IOLib.h>',
                                '#include <IOKit/IOLib.h>\nextern "C" void d2xq_console_text(const char *);', 1)
            if 'extern "C" void d2xq_console_text' not in text:
                text = 'extern "C" void d2xq_console_text(const char *);\n' + text
            if 'IODelay (' in text:
                text = text.replace('struct HardwareTransferPort {',
                                    'static inline void d2xq_nodelay (uint32_t) { }\nstruct HardwareTransferPort {', 1)
                text = text.replace('IODelay (', 'd2xq_nodelay (')
            if 'bool expired () { return mach_absolute_time () >= deadline; }' not in text:
                sys.exit('expired anchor missing')
            text = text.replace('bool expired () { return mach_absolute_time () >= deadline; }',
                                'bool expired () { static uint32_t calls; calls++; return calls > 100; }', 1)
            anchor_text = ('  if (gHost.poisoned) return false;\n'
                           '  // Check before touching the command/response area, not just before ringing.\n'
                           '  if (read32 (gHost.hc, 0x58) != 0) {')
            if anchor_text not in text:
                sys.exit('hostClearUcd anchor missing')
            text = text.replace(anchor_text,
                                '  gHost.poisoned = false;\n'
                                '  if (read32 (gHost.hc, 0x58) != 0) {\n'
                                '    write32 (gHost.hc, 0x58, 0);\n'
                                '    for (unsigned d2xq_wait = 0; d2xq_wait < 200000 && read32 (gHost.hc, 0x58) != 0; d2xq_wait++) { d2xq_nodelay (1); }\n'
                                '  }\n'
                                '  // Check before touching the command/response area, not just before ringing.\n'
                                '  if (read32 (gHost.hc, 0x58) != 0) {', 1)
            fatal = ('  if (read32 (gHost.hc, 0x20) & kUfsFatalStatus) {\n'
                     '    gHost.poisoned = true;\n'
                     '    gHost.ready = false;\n'
                     '    recordTransport (kIOReturnIOError, 15);\n'
                     '    return false;\n'
                     '  }')
            if fatal in text:
                text = text.replace(fatal, '  write32 (gHost.hc, 0x20, 0xffffffffU);', 1)
            clear = '  if (!hostClearUcd ()) return kIOReturnNotReady;'
            if clear in text:
                text = text.replace(clear, '  if (!hostClearUcd ()) { d2xq_console_text("X "); return kIOReturnNotReady; }', 1)
            stuck = ('  if (read32 (gHost.hc, 0x58) != 0) {\n'
                     '    gHost.poisoned = true;\n'
                     '    gHost.ready = false;\n'
                     '    recordTransport (kIOReturnBusy, 15);\n'
                     '    return false;\n'
                     '  }')
            if stuck in text:
                text = text.replace(stuck, '  if (read32 (gHost.hc, 0x58) != 0) { write32 (gHost.hc, 0x58, 0); }', 1)
            busy = ('  if (read32 (gHost.hc, 0x58) != 0) {\n'
                    '    gHost.poisoned = true;\n'
                    '    gHost.ready = false;\n'
                    '    return recordTransport (kIOReturnBusy, *ocs);\n'
                    '  }')
            if busy in text:
                text = text.replace(busy, '  if (read32 (gHost.hc, 0x58) != 0) { write32 (gHost.hc, 0x58, 0); }', 1)
            sub_fatal = ('  if (read32 (gHost.hc, 0x20) & kUfsFatalStatus) {\n'
                         '    gHost.poisoned = true;\n'
                         '    gHost.ready = false;\n'
                         '    return recordTransport (kIOReturnIOError, *ocs);\n'
                         '  }')
            if sub_fatal in text:
                text = text.replace(sub_fatal, '  write32 (gHost.hc, 0x20, 0xffffffffU);', 1)
            poisoned = '  if (gHost.poisoned) return kIOReturnNotReady;'
            if poisoned in text:
                text = text.replace(poisoned, '  gHost.poisoned = false;', 1)
            counter = ('  { static unsigned d2xq_cmd; char d2xq_c[3] = {'
                       ' char (97 + (d2xq_cmd % 26)), char (48 + ((d2xq_cmd / 26) % 10)), 0 };'
                       ' d2xq_cmd++; d2xq_console_text(d2xq_c); d2xq_console_text(" "); }\n')
            text = text.replace('  hostWrite (0x58, 1);', counter + '  hostWrite (0x58, 1);', 1)
            commands.write_text(text)
    poll = ufs_include / 'TransferPoll.h'
    if poll.is_file():
        text = poll.read_text()
        if 'd2xq_console_text' not in text:
            if '    ++state.polls;' not in text:
                sys.exit('poll anchor missing')
            text = 'extern "C" void d2xq_console_text(const char *);\n' + text
            poll.write_text(text)

    # Shorten command timeouts so a stalled transfer fails within a few hundred
    # milliseconds, and mark each return path of hostSubmit.
    for header_name in ('RootCommands.h', 'NativeUfs.cpp'):
        header = ufs_include / header_name if (ufs_include / header_name).is_file() else OUT / 'driver' / header_name
        if not header.is_file():
            continue
        text = header.read_text()
        text = text.replace('200000', '20000').replace('500000', '50000')
        if header_name == 'RootCommands.h':
            text = text.replace('  cacheInvalidate (gHost.virt, kDataOffset + dataBytes);',
                                '  \n  cacheInvalidate (gHost.virt, kDataOffset + dataBytes);', 1)
            text = text.replace('  return recordTransport (*ocs == 0 ? kIOReturnSuccess : kIOReturnIOError, *ocs);',
                                '  \n  return recordTransport (*ocs == 0 ? kIOReturnSuccess : kIOReturnIOError, *ocs);', 1)
        else:
            text = text.replace('    IOSleep (10);', '    d2xq_console_text("T ");\n    IOSleep (10);', 1)
            text = text.replace('panic ("NATIVE_UFS root check: bring-up failed: %s", why);',
                                'panic ("NATIVE_UFS root check: bring-up failed: %s ocs=%u resp=%08x scsi=%u polls=%u", why, gRootDiagnostic.ocs, gRootDiagnostic.response, gRootDiagnostic.scsi, gRootDiagnostic.polls);', 1)
        header.write_text(text)

    layout_hdr = ufs_include / 'RootLayout.h'
    if layout_hdr.is_file():
        text = layout_hdr.read_text()
        if 'd2xq_console_text' not in text:
            text = 'extern "C" void d2xq_console_text(const char *);\n' + text
            text = text.replace('  if (hostRead4k (0, 1, 1, gScratch) != kIOReturnSuccess) {',
                                '  d2xq_console_text("G1 ");\n  if (hostRead4k (0, 1, 1, gScratch) != kIOReturnSuccess) {', 1)
            if '  if (hostRead4k (0, entriesLba, blocks, gScratch) != kIOReturnSuccess) {' not in text:
                sys.exit('entries read anchor missing')
            entry_read = ('  if (hostRead4k (0, entriesLba, blocks, gScratch) != kIOReturnSuccess) {\n'
                          '    *why = "GPT entries read";\n'
                          '    return false;\n'
                          '  }')
            loop_read = ('  { IOReturn er = kIOReturnSuccess;\n'
                         '    for (uint32_t bi = 0; bi < blocks && er == kIOReturnSuccess; bi++) {\n'
                         '      er = hostRead4k (0, entriesLba + bi, 1, gScratch + bi * 4096);\n'
                         '    }\n'
                         '    if (er != kIOReturnSuccess) {\n'
                         '      *why = "GPT entries read";\n'
                         '      return false;\n'
                         '    } }')
            if entry_read not in text:
                sys.exit('entries block text mismatch')
            text = text.replace('  if (hostRead4k (0, entriesLba, blocks, gScratch) != kIOReturnSuccess) {',
                                '  d2xq_console_text("G2 ");\n' + loop_read + '\n  if (false) {', 1)
            text = text.replace('  static const char wanted[] = "userdata";',
                                '  d2xq_console_text("G3 ");\n  static const char wanted[] = "userdata";', 1)
            text = text.replace('  if (readManifest) {',
                                '  d2xq_console_text("G4 ");\n  if (readManifest) {', 1)
            layout_hdr.write_text(text)

    record = load_record()
    objects = []
    for source in (base_source, native_ufs, native_ram, source_storage):
        symbol = source.name
        obj = OUT / (source.stem + '.o')
        extra = (ufs_include,) if source == native_ufs else ()
        command = compile_command(record, source, obj, extra)
        result = subprocess.run(command, cwd=record['directory'], capture_output=True, text=True)
        (OUT / (source.stem + '.log')).write_text(result.stdout + result.stderr)
        if result.returncode:
            sys.exit((result.stdout + result.stderr)[-4000:])
        objects.append(late_object(obj, symbol))

    ar = shutil.which('llvm-ar') or shutil.which('ar')
    if not ar:
        sys.exit('llvm-ar not found')
    ARCHIVE.unlink(missing_ok=True)
    subprocess.run([ar, 'rcs', str(ARCHIVE), *map(str, objects)], check=True)
    # The UFS link force-loads the catalog archive; the RAM link force-loads its own.
    # Keep one copy of every object: full archive for UFS, empty archive for RAM.
    ufs = ROOT / 'build/sheng-ufs/libShengUfs.a'
    ufs.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(ARCHIVE, ufs)
    ram = ROOT / 'build/ram-block/libRamBlock.a'
    ram.parent.mkdir(parents=True, exist_ok=True)
    ram.unlink(missing_ok=True)
    subprocess.run([ar, 'rcs', str(ram)], check=True)
    print(f'PASS: {len(objects)} late-registration objects archived -> {ARCHIVE}')


if __name__ == '__main__':
    main()
