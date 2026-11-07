#!/usr/bin/env python3
"""Emit only the immutable CI194 public anchor, descriptors and signed KATs."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import uuid


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def values(data):
    return ','.join('0x%02x' % b for b in data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--policies', type=Path, required=True)
    parser.add_argument('--bindings', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    folder = args.policies.resolve()
    binding_raw = args.bindings.read_bytes()
    binding = json.loads(binding_raw)
    verified_raw = (folder/'independent-C-verification.json').read_bytes()
    verified = json.loads(verified_raw)
    require(verified['status'] == 'PASS' and verified['total_blocks_verified'] == 2290185 and
            verified['total_negative_controls'] == 35 and
            sha(verified_raw) == binding['independent_verification_sha256'], 'Unverified policy source')
    anchor = (folder/'trusted-key.bin').read_bytes()
    require(len(anchor) == 395 and sha(anchor) == binding['trusted_key_sha256'] and
            sha(anchor) == verified['trusted_key_sha256'], 'Owner anchor changed')
    require(binding['mode'] == 'policy-write' and binding['expected_counter'] == 140 and
            binding['target_first_lba'] == 0x44f800 and binding['target_end_lba'] == 0x453de6,
            'CI193 policy layout changed')
    require(len(binding['policies']) == len(verified['policies']) == 2,
            'Exactly two independently verified policies are required')
    lines = ['// Generated from CI193 verified actual envelopes; contains public material only.',
             '#include "CI194Policy.h"', '#include "VerifiedBlockPolicy.h"',
             '#include <stddef.h>',
             '#define CI194_CONST __attribute__((section("__DATA_CONST,__const"),used))',
             'static_assert(sizeof(NativeExternalPolicyV1)==136, "external policy ABI");',
             'static_assert(alignof(NativeExternalPolicyV1)==8, "external policy alignment");',
             'static_assert(offsetof(NativeExternalPolicyV1,manifestSha256)==40, "manifest SHA ABI");',
             'static_assert(offsetof(NativeExternalPolicyV1,imageSha256)==72, "image SHA ABI");',
             'static_assert(offsetof(NativeExternalPolicyV1,volumeUuid)==104, "UUID ABI");',
             'static_assert(offsetof(NativeExternalPolicyV1,label)==120, "label ABI");',
             'static_assert(sizeof(NativeVerifiedBlockPolicy)==16, "legacy policy ABI");',
             'extern "C" {',
             'extern CI194_CONST const uint8_t verified_root_key[395] = {'+values(anchor)+'};',
             'extern CI194_CONST const uint64_t native_ufs_external_policy_version = 1;',
             'extern CI194_CONST const uint64_t native_ufs_external_policy_count = 2;',
             'extern CI194_CONST const NativeVerifiedBlockPolicy verified_root_policies[1] = {{nullptr,0}};',
             'extern CI194_CONST const uint64_t verified_root_policy_count = 0;',
             'extern CI194_CONST const NativeExternalPolicyV1 native_ufs_external_policies[2] = {']
    headers, signatures = [], []
    expected = [('OS',0x44f800,0x452549,1048576,6077580800,47481664),
                ('SystemOS',0x452549,0x453de6,6079643648,3303014400,25805344)]
    for item, check, fields in zip(binding['policies'], verified['policies'], expected):
        name, first, end, offset, size, envelope_size = fields
        require((item['component'],item['target_first_lba'],item['target_end_lba'],
                 item['source_offset'],item['image_bytes'],item['manifest_bytes']) == fields,
                'Fixed policy identity changed')
        path = (folder/name.lower()/'manifest.bin').resolve()
        require(path.is_relative_to(folder), 'Escaped policy path')
        envelope = path.read_bytes()
        require(len(envelope) == envelope_size and
                sha(envelope) == item['manifest_sha256'] == check['manifest_sha256'] and
                envelope[:8] == b'VROOT01\0' and
                struct.unpack_from('<QQ',envelope,24) == (size,(size+4095)//4096) and
                struct.unpack_from('<Q',envelope,152)[0] == 1 and
                envelope[40:56] == uuid.UUID(item['volume_uuid']).bytes and
                envelope[56:88] == bytes.fromhex(item['image_sha256']) and
                envelope[120:152] == hashlib.sha256(anchor).digest(), 'Signed envelope changed')
        lines.append('  {%dULL,%dULL,%dULL,%dULL,1ULL,{%s},{%s},{%s},"%s"},' %
                     (first,envelope_size,offset,size,values(bytes.fromhex(item['manifest_sha256'])),
                      values(bytes.fromhex(item['image_sha256'])),values(envelope[40:56]),name))
        headers.append(envelope[:160])
        signatures.append(envelope[160:544])
    lines.append('};')
    for symbol, content in [('native_ufs_external_key_sha256',hashlib.sha256(anchor).digest()),
                            ('native_ufs_external_root_manifest_sha256',bytes.fromhex(binding['root_manifest_sha256'])),
                            ('native_ufs_external_image_sha256',bytes.fromhex(binding['image_sha256']))]:
        lines.append('extern CI194_CONST const uint8_t '+symbol+'[32] = {'+values(content)+'};')
    for symbol, content, count in [('native_ufs_external_signed_headers',headers,160),
                                   ('native_ufs_external_signatures',signatures,384)]:
        lines.append('extern CI194_CONST const uint8_t %s[2][%d] = {' % (symbol,count))
        lines.extend('  {'+values(row)+'},' for row in content)
        lines.append('};')
    lines.extend(['}', ''])
    output = '\n'.join(lines).encode()
    require(not args.output.exists(), 'Generated policy-data output exists; inspect before replacement')
    args.output.write_bytes(output)
    record = dict(version=1,mode='authenticated-windows-readonly',
                  ci193_binding_sha256=sha(binding_raw),
                  independent_C_report_sha256=sha(verified_raw),
                  policy_data_sha256=sha(output),trusted_key_sha256=sha(anchor),
                  external_descriptor_bytes=136,legacy_descriptor_bytes=16,
                  real_policy_count=2,legacy_disabled_policy_count=0,
                  source_policy_sha256=[p['manifest_sha256'] for p in binding['policies']],
                  crypto_known_signature_tests=2,private_key_included=False)
    args.output.with_suffix('.json').write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))


if __name__ == '__main__':
    main()
