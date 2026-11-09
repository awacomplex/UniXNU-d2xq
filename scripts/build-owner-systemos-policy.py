#!/usr/bin/env python3
"""Sign and fully verify original OS virtual media without expanding the UDIF."""
import ctypes
import hashlib
import importlib.util
import json
from pathlib import Path
import plistlib
import shutil
import stat
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[1]
source = ROOT / '.cache/firmware/target-volumes/044-76034-038.dmg'
volume = ROOT / '.cache/firmware/target-volumes/systemos-mount'
out = ROOT / 'build/verified-root-systemos'
expected_uuid = uuid.UUID('8846FE78-2910-4297-AF97-D59BB6F8FC16')
if not source.is_file():
    raise SystemExit('Require the existing exact original SystemOS input')
digest = hashlib.sha256()
with source.open('rb') as stream:
    for chunk in iter(lambda: stream.read(1024 * 1024), b''):
        digest.update(chunk)
if digest.hexdigest() != 'b4908cde41d23e6bc05e6af51a2c9eeb00cf0f863a6a18b1600c96a3fd70414f':
    raise SystemExit('The image must be the exact supplied iPad7,1 Cryptex1,SystemOS member')
images = plistlib.loads(subprocess.check_output(['hdiutil', 'info', '-plist']))['images']
found = [i for i in images if Path(i['image-path']).resolve() == source.resolve() and i.get('writeable') is False]
if len(found) != 1:
    raise SystemExit('Require one existing exact read-only image attachment')
entities = found[0]['system-entities']
whole = [e['dev-entry'] for e in entities if e.get('content-hint') == 'GUID_partition_scheme']
if len(whole) != 1:
    # Cryptex1,SystemOS is a bare APFS container without a partition scheme.
    whole = [entities[0]['dev-entry']] if entities else []
if len(whole) != 1:
    raise SystemExit('Require one actual whole virtual media mapping')
info = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', whole[0]]))
volume_info = plistlib.loads(subprocess.check_output(['diskutil', 'info', '-plist', volume]))
if (info.get('BusProtocol') != 'Disk Image' or info.get('Writable') is not False or
        info.get('DeviceNode') != whole[0] or info.get('TotalSize') != 3303014400 or
        volume_info.get('VolumeUUID') != str(expected_uuid).upper() or volume_info.get('Writable') is not False or
        volume_info.get('DeviceNode') not in [e['dev-entry'] for e in entities]):
    raise SystemExit('Only the actual matching read-only Disk Image/SystemOS volume is accepted')
disk = Path('/dev/r' + Path(whole[0]).name)
if not stat.S_ISCHR(disk.stat().st_mode):
    raise SystemExit('Require the verified virtual-media raw character device')
spec = importlib.util.spec_from_file_location('verified_manifest', ROOT / 'kits/VerifiedRoot/tools/manifest.py')
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)
keys = ROOT / 'build/verified-root-host/keys'
with disk.open('rb', buffering=1024 * 1024) as stream:
    record = manifest.make(stream, info['TotalSize'], keys / 'owner.pem', keys / 'owner-public.pem', expected_uuid, 1, out)


class View(ctypes.Structure):
    _fields_ = [('hashes', ctypes.c_void_p), ('image_bytes', ctypes.c_uint64), ('block_count', ctypes.c_uint64),
                ('generation', ctypes.c_uint64), ('volume_uuid', ctypes.c_uint8 * 16), ('image_sha256', ctypes.c_uint8 * 32)]


native = ctypes.CDLL(str(ROOT / 'build/verified-root-host/verified-root.dylib'))
native.vr_host_open.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_uint64, ctypes.POINTER(View)]
native.vr_host_block.argtypes = [ctypes.POINTER(View), ctypes.c_uint64, ctypes.c_void_p, ctypes.c_size_t]
encoded = ctypes.create_string_buffer((out / 'manifest.bin').read_bytes())
anchor = ctypes.create_string_buffer((out / 'trusted-key.bin').read_bytes())
view = View()
if native.vr_host_open(encoded, len(encoded) - 1, anchor, 1, ctypes.byref(view)) or bytes(view.volume_uuid) != expected_uuid.bytes:
    raise SystemExit('Real C signature/volume authentication failed')
complete = hashlib.sha256()
indices = [0, 1, view.block_count // 2, view.block_count - 1]
samples, lengths = [], []
with disk.open('rb', buffering=1024 * 1024) as stream:
    for index in range(view.block_count):
        block = stream.read(min(4096, view.image_bytes - index * 4096))
        if native.vr_host_block(ctypes.byref(view), index, block, len(block)) != 0:
            raise SystemExit('Actual C block verification failed at ' + str(index))
        complete.update(block)
        if index in indices:
            samples.append(block.ljust(4096, b'\0'))
            lengths.append(len(block))
    if stream.read(1):
        raise SystemExit('Virtual media exceeded its authenticated exact bounds')
if complete.digest() != bytes(view.image_sha256) or complete.hexdigest() != record['image_sha256']:
    raise SystemExit('Actual complete original SystemOS digest differs')
(out / 'original-samples.bin').write_bytes(b''.join(samples))
report = dict(image_bytes=view.image_bytes, blocks_verified=view.block_count, image_sha256=complete.hexdigest(),
              volume_uuid=str(expected_uuid).upper(), generation=view.generation,
              source_compressed_sha256=digest.hexdigest(), source_format='Raw',
              host_bus_protocol='Disk Image', host_media_writable=False,
              trusted_key_sha256=record['trusted_key_sha256'], actual_sample_indices=indices,
              actual_sample_lengths=lengths, real_c_signature_and_block_verification=True,
              original_image_modified=False, host_only=True, boot_root_authenticated=False,
              qemu_tested=False, hardware_tested=False)
(out / 'verification.json').write_text(json.dumps(report, indent=2) + '\n')
print('PASS: original SystemOS signature and every', view.block_count, 'block verified by real C/RSA; no expanded image')
