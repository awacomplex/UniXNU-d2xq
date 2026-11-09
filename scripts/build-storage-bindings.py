#!/usr/bin/env python3
"""Bind a storage driver's original IOStorageFamily imports to their absolute kernelcache addresses.

IOStorageFamily is prelinked and exports nothing, so every undefined IOBlockStorageDevice/IOMedia/IOStorage name of the
compiled driver object is set to its address in the unchanged 21H461 kernelcache. The table is
data/original-storage-bindings.S, the binding set the earlier S7plus-iPadOS route derived for its own
IOBlockStorageDevice subclass and linked against the same kernelcache. A name the table lacks stops the build: no
address is guessed. Usage: --object build/sheng-ufs/NativeUfs.o --archive libShengUfs.a
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--object', type=Path, required=True)
parser.add_argument('--archive', required=True)
parser.add_argument('--provided-by', type=Path, help='bindings.json of an archive linked into the same kernel: its names are not defined again')
args = parser.parse_args()
obj = (ROOT / args.object).resolve()
out = obj.parent
table = {}
for line in (Path(__file__).resolve().parent / 'data/original-storage-bindings.S').read_text().splitlines():
    match = re.match(r'\.set (\S+), (0x[0-9a-f]+)$', line)
    if match:
        table[match.group(1)] = int(match.group(2), 16)
names = subprocess.run(['llvm-nm', '-u', str(obj)], capture_output=True, text=True, check=True).stdout.split()
wanted = sorted(n for n in names if re.search(r'IOBlockStorageDevice|IOMedia|IOStorage', n))
missing = [n for n in wanted if n not in table]
if missing:
    raise SystemExit('No recorded original address for: ' + ', '.join(missing))
bindings = {n: table[n] for n in wanted}
shared = {}
if args.provided_by:
    shared = json.loads((ROOT / args.provided_by).read_text())['bindings']
    for name in list(bindings):
        if name in shared:
            if int(shared[name], 16) != bindings[name]:
                raise SystemExit('Shared original binding differs: ' + name)
            del bindings[name]
assembly = out / 'original-bindings.S'
assembly.write_text(''.join(f'.globl {n}\n.set {n}, {v:#x}\n' for n, v in bindings.items()))
objects = [str(obj)]
if bindings:
    subprocess.run(['clang', '-target', 'arm64-apple-ios17.0', '-c', str(assembly), '-o', str(out / 'original-bindings.o')], check=True)
    objects.append(str(out / 'original-bindings.o'))
archive = out / args.archive
archive.unlink(missing_ok=True)
subprocess.run(['xcrun', 'ar', 'rcs', str(archive), *objects], check=True)
(out / 'bindings.json').write_text(json.dumps({
    'compiled_object_sha256': hashlib.sha256(obj.read_bytes()).hexdigest(),
    'bindings': {n: hex(v) for n, v in bindings.items()},
    'shared_from': str(args.provided_by) if args.provided_by else None,
    'shared': sorted(n for n in wanted if n not in bindings)}, indent=2) + '\n')
print(f'{archive.name}: {len(bindings)} original storage imports bound, {len(wanted) - len(bindings)} shared')
