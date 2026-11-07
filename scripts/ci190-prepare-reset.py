#!/usr/bin/env python3
"""Prepare the exact one-use CI190 RECOVERY arming record; never access a device."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, help='create a new 4096-byte record file')
args = parser.parse_args()
config = json.loads(Path(__file__).with_name('ci190-reset-arm.json').read_text())
expected = (1, 151315, 2225152, 62458874, 7, 2)
actual = tuple(config[key] for key in ('version', 'recovery_first', 'userdata_first',
                                     'userdata_last', 'counter', 'next_repair_slot'))
if actual != expected:
    raise RuntimeError('CI190 fixed geometry/progress changed')
image = bytes.fromhex(config['image_sha256'])
nonce = bytes.fromhex(config['nonce'])
if len(image) != 32 or len(nonce) != 32 or not any(nonce):
    raise RuntimeError('Invalid image digest or nonce')
header = struct.pack('<8sIIQIIQQQ32s32s', b'D2R190A\0', 1, 160, 7, 2,
                     0xfffffffd, 151315, 2225152, 62458874, nonce, image)
assert len(header) == 120
header += bytes(8)
record = header + hashlib.sha256(header).digest() + bytes(3936)
assert len(record) == 4096
digest = hashlib.sha256(record).hexdigest()
if digest != config['arm_sha256']:
    raise RuntimeError('Arming record SHA256 mismatch')
probe = Path(__file__).with_name('ci190-reset-probe.inc').read_text()
begin = probe.index('static const uint8_t ci190ArmSha[32] = {')
end = probe.index('};', begin)
import re
compiled = bytes(int(value, 16) for value in re.findall(r'0x([0-9a-fA-F]{2})\b', probe[begin:end]))
if compiled != bytes.fromhex(digest):
    raise RuntimeError('Kernel arming digest differs from prepared record')
if args.output:
    with args.output.open('xb') as output:
        output.write(record)
print(json.dumps({'mode': 'reset-once-arm', 'bytes': len(record), 'sha256': digest,
                  'output': str(args.output) if args.output else None,
                  'recovery_lba': 151315, 'counter': 7, 'next_repair_slot': 2}, indent=2))
