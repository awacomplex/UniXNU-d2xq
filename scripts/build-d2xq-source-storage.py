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


def compile_command(record, source: Path, obj: Path):
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
    command += ['-I', str(HEADERS), '-I', str(ROOT / '.cache/xnu-work/osfmk'), '-Wno-error']
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

    record = load_record()
    objects = []
    for source, symbol in ((base_source, 'IOBlockStorageDevice.cpp'),
                           (native_ufs, 'NativeUfs.cpp'),
                           (native_ram, 'NativeRamBlock.cpp'),
                           (source_storage, 'SourceStorage.cpp')):
        obj = OUT / (source.stem + '.o')
        command = compile_command(record, source, obj)
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
    for catalog in ('build/sheng-ufs/libShengUfs.a', 'build/ram-block/libRamBlock.a'):
        target = ROOT / catalog
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ARCHIVE, target)
    print(f'PASS: {len(objects)} late-registration objects archived -> {ARCHIVE}')


if __name__ == '__main__':
    main()
