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
    for number in (100, 101, 102, 103, 104, 105, 106, 107, 200, 201, 202, 203, 204):
        ufs_text = ufs_text.replace(f'd2xq_ufs_stage = {number};',
                                    f'd2xq_ufs_stage = {number}; d2xq_console_text("H{number} ");')
    patched_ufs = OUT / 'driver' / 'NativeUfs.cpp'
    patched_ufs.write_text(ufs_text.replace(missing, fallback, 1))
    native_ufs = patched_ufs

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
