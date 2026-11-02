#!/usr/bin/env python3
"""Compile the public IOBlockStorageDevice implementation into the kernel.

The d2xq kernel collection does not carry IOStorageFamily, so NativeUfs (and the
other block drivers) have no base class to link against. This builder compiles
the public Darwin IOStorageFamily base class with the same flags the XNU build
used for IOKit sources, and emits an archive the kernel link force-loads.

Usage (from [kernel] archives):
    build-d2xq-source-storage.py
"""
import json
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'build/d2xq-source-storage'
ARCHIVE = OUT / 'libSourceStorageBase.a'
STORAGE = ROOT / 'refer/IOStorageFamily'
HEADERS = ROOT / 'build/storage-abi/include'
RECORD = ROOT / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'

# Files from the public source that the block drivers reference directly.
SOURCES = [
    'IOBlockStorageDevice.cpp',
]


def load_record():
    text = RECORD.read_text().strip().rstrip(',')
    return json.loads(text)


def compile_command(record, source: Path, obj: Path, extra_includes):
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
        elif value.startswith('-W') and value not in ('-Wno-error',):
            pass
        elif value not in ('-MD', '-MP', '-c'):
            command.append(value)
        index += 1
    if '-c' not in command:
        command.append('-c')
    for include in extra_includes:
        command += ['-I', str(include)]
    return command


def main():
    if not STORAGE.is_dir():
        sys.exit(f'{STORAGE} missing: run "unixnu fetch kernel" first')
    if not HEADERS.is_dir():
        sys.exit(f'{HEADERS} missing: prepare-storage-headers.py did not run')
    if not RECORD.is_file():
        sys.exit(f'{RECORD} missing: the kernel needs one pass for the compile records')

    OUT.mkdir(parents=True, exist_ok=True)
    record = load_record()
    extra_includes = [HEADERS, ROOT / '.cache/xnu-work/osfmk']
    objects = []
    for name in SOURCES:
        source = STORAGE / name
        if not source.is_file():
            sys.exit(f'{source} missing')
        obj = OUT / (Path(name).stem + '.o')
        command = compile_command(record, source, obj, extra_includes)
        log = OUT / (Path(name).stem + '.log')
        result = subprocess.run(command, cwd=record['directory'], capture_output=True, text=True)
        log.write_text(result.stdout + result.stderr)
        if result.returncode:
            sys.exit((result.stdout + result.stderr)[-3000:])
        objects.append(obj)

    ar = shutil.which('llvm-ar') or shutil.which('ar')
    if not ar:
        sys.exit('llvm-ar not found')
    if ARCHIVE.exists():
        ARCHIVE.unlink()
    subprocess.run([ar, 'rcs', str(ARCHIVE), *map(str, objects)], check=True)
    print(f'PASS: {len(objects)} public IOStorageFamily source files archived -> {ARCHIVE}')


if __name__ == '__main__':
    main()
