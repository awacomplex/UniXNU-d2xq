#!/usr/bin/env python3
"""Build CI194's original ApplePMGR provider/nub; never install power callbacks.

The normal archive entry needs no arguments. It resolves the same board profile
as unixnu, prepares the original KernelCache through unixnu's normal IPSW path,
then checks the exact cache, module, class and instruction identities before
emitting any absolute bindings. --audit-only can inspect a supplied cache/IPSW
without a compiler, SDK, network access or device operation.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
CACHE_SHA = 'f18b4df865405f2a287b84977d7aca745e056d656d9fc9df1662970ce2969de7'
SLIDE = -0x1000000
MODULE = 'com.apple.driver.ApplePMGR'
ORIGINAL = {
    '__ZN9ApplePMGRC2EPK11OSMetaClass': 0xfffffff0059f003c,
    '__ZN9ApplePMGRD2Ev': 0xfffffff0059f019c,
    '__ZN9ApplePMGR10gMetaClassE': 0xfffffff007adf490,
    '__ZN9ApplePMGR7readAonEv': 0xfffffff005a20fbc,
    '__ZN9ApplePMGR8writeAonEy': 0xfffffff005a20fc4,
    '_d2xq_original_pmgr_nub_create': 0xfffffff005a22568,
    '_d2xq_original_pmgr_nub_metaclass': 0xfffffff007adf828,
    '_d2xq_original_pmgr_nub_read_aon': 0xfffffff005a24adc,
    '_d2xq_original_pmgr_nub_write_aon': 0xfffffff005a230ec,
}


def require(ok, message):
    if not ok:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def run(command, output, name, cwd=None):
    command = list(map(str, command))
    (output / (name + '.command.json')).write_text(json.dumps(command, indent=2) + '\n')
    with (output / (name + '.log')).open('w') as stream:
        result = subprocess.run(command, cwd=cwd, stdout=stream, stderr=subprocess.STDOUT)
    require(result.returncode == 0, (output / (name + '.log')).read_text()[-6000:])


def read_ipsw(path):
    # Explicit audit input only; no network fallback or whole-IPSW download.
    import lzfse
    with zipfile.ZipFile(path) as archive:
        data = archive.read('kernelcache.release.ipad7')

    def der(at):
        require(at + 2 <= len(data), 'Truncated IM4P')
        tag, size = data[at:at + 2]
        at += 2
        if size & 0x80:
            count = size & 0x7f
            require(0 < count <= 8 and at + count <= len(data), 'Invalid DER length')
            size = int.from_bytes(data[at:at + count], 'big')
            at += count
        require(at + size <= len(data), 'DER value outside IM4P')
        return tag, at, size

    tag, at, size = der(0)
    require(tag == 0x30 and at + size == len(data), 'Expected exact IM4P sequence')
    end, items = at + size, []
    while at < end:
        _, body, extent = der(at)
        items.append(data[body:body + extent])
        at = body + extent
    require(len(items) >= 4 and items[0] == b'IM4P', 'Invalid kernel IM4P')
    data = lzfse.decompress(items[3])
    if data[:4] == b'\xca\xfe\xba\xbe':
        count, = struct.unpack_from('>I', data, 4)
        require(0 < count <= 16 and 8 + 20 * count <= len(data), 'Invalid fat header')
        slices = [struct.unpack_from('>5I', data, 8 + 20 * i) for i in range(count)]
        arm64 = [entry for entry in slices if entry[0] == 0x0100000c and entry[1] == 0]
        require(len(arm64) == 1, 'Expected one ordinary arm64 kernelcache slice')
        _, _, offset, size, _ = arm64[0]
        require(offset + size <= len(data), 'Fat slice outside payload')
        data = data[offset:offset + size]
    return data


def resolve_inputs(args, tree, device, output):
    explicit_cache = args.kernelcache or os.environ.get('UNIXNU_KERNELCACHE')
    if args.ipsw:
        require(args.audit_only, '--ipsw is restricted to an explicit offline audit')
        require(args.slide == SLIDE, 'Offline audit must supply the reviewed --slide=-0x1000000')
        return read_ipsw(args.ipsw), SLIDE, {'ipsw': str(args.ipsw.resolve()), 'offline': True}
    if explicit_cache and args.slide is not None:
        require(args.slide == SLIDE, 'Unexpected 21H461 slide')
        path = Path(explicit_cache).resolve()
        return path.read_bytes(), SLIDE, {'kernelcache': str(path), 'explicit': True}

    # Assembled tree/package links to the arch repository package; device sources
    # remain in UNIXNU_DEVICE. Use the original CLI to resolve merged OS defaults.
    arch = (tree / 'package').resolve().parent
    cli = arch / 'unixnu'
    require(cli.is_file(), 'Cannot resolve the main unixnu CLI; supply --kernelcache and --slide')
    command = [sys.executable, str(cli), 'status', '--json', '-D', str(device)]
    plan = json.loads(subprocess.check_output(command, cwd=arch, text=True))
    board = plan['board']
    spec = board['kernel']['prelink']
    slide = spec['slide']
    slide = int(slide, 0) if isinstance(slide, str) else slide
    require(spec['kernelcache_sha256'] == CACHE_SHA and slide == SLIDE,
            'Resolved profile does not select the reviewed 21H461 cache/slide')
    require(board['ipsw']['build'] == '21H461' and board['ipsw']['product'] == 'iPad7,1' and
            MODULE in spec['roots'], 'Resolved profile does not contain original ApplePMGR')
    if args.slide is not None:
        require(args.slide == slide, 'Explicit slide differs from resolved board')
    (output / 'resolved-board.json').write_text(json.dumps(plan, indent=2) + '\n')
    if explicit_cache:
        cache = Path(explicit_cache).resolve()
    elif spec.get('kernelcache'):
        cache = (tree / spec['kernelcache']).resolve()
    else:
        work = Path(plan['output'])
        require(work.name == 'd2xq' and work.parent.name == 'ipados-17.7.11-21H461' and
                work.parent.parent.name == 'd2xq', 'Unexpected format-2 d2xq output layout')
        cache = work.parents[2] / 'ipsw/21H461/kernelcache.decompressed'
    if not cache.is_file():
        require(not args.audit_only, 'Audit cache missing; provide an explicit local input')
        run([sys.executable, cli, 'ipsw', 'fetch', 'KernelCache', '-D', device],
            output, 'prepare-original-kernelcache', arch)
    return cache.read_bytes(), slide, {'kernelcache': str(cache.resolve()), 'resolved': True}


def audit_cache(data, slide, np):
    require(sha(data) == CACHE_SHA and len(data) == 46415872, 'Original 21H461 cache pin mismatch')
    image = np.macho(data)
    info, = [s for s in image['segments'] if s['name'] == '__PRELINK_INFO']
    offset = np.backed_offset(image['segments'], info['va'], info['filesize'], len(data))
    metadata = np.unserialize_xml(data[offset:offset + info['filesize']])
    module, = [item for item in metadata['_PrelinkInfoDictionary'] if item['CFBundleIdentifier'] == MODULE]
    offset = np.backed_offset(image['segments'], module['_PrelinkExecutableLoadAddr'], 32, len(data))
    segments = np.macho(data, offset)['segments']

    def read(address, size):
        offset = np.backed_offset(image['segments'], address, size, len(data))
        return data[offset:offset + size]

    def words(address, expected):
        actual = struct.unpack('<' + 'I' * len(expected), read(address, len(expected) * 4))
        require(actual == expected, f'Original instruction contract changed at {address:#x}')

    def segment_for(address, size):
        matches = [s for s in segments if s['name'] != '__LINKEDIT' and
                   s['va'] <= address and address + size <= s['va'] + s['size']]
        require(len(matches) == 1, f'Binding outside unique original PMGR segment: {address:#x}')
        return matches[0]

    # Constructor's actual vtable formation and original provider class size.
    words(0xfffffff0059f0058, (0xd0009788, 0x911b6108, 0xf9000008))
    words(0xfffffff0059f01cc, (0x52971503, 0x72a00083))
    words(0xfffffff005a22588, (0x5280e801,))  # Original factory allocation size 0x740.
    words(0xfffffff005a22660, (0xf9004660,))  # Nub's private provider field at +0x88.
    words(0xfffffff005a20fbc, (0x12800000, 0xd65f03c0, 0xd65f03c0))
    words(0xfffffff005a24adc, (0xf9404400, 0xb4000080, 0xf9400008,
                              0xf9450101, 0xd61f0020, 0x12800000, 0xd65f03c0))
    words(0xfffffff005a230ec, (0xf9404400, 0xb4000080, 0xf9400008,
                              0xf9450502, 0xd61f0040, 0xd65f03c0))
    vptr = 0xfffffff006ce26d8
    require(struct.unpack('<2Q', read(vptr + 320 * 8, 16)) ==
            (ORIGINAL['__ZN9ApplePMGR7readAonEv'], ORIGINAL['__ZN9ApplePMGR8writeAonEy']),
            'Original PMGR slots 320/321 differ')
    require(struct.unpack('<Q', read(vptr + 340 * 8, 8))[0] == 0,
            'Original PMGR vtable no longer ends after slot 339')
    def is_data_symbol(symbol):
        if 'MetaClass' not in symbol and 'metaclass' not in symbol:
            return False
        # Constructors/destructors mention OSMetaClass in their parameter type but live in text.
        for code in ('C1E', 'C2E', 'D0E', 'D1E', 'D2E'):
            if code in symbol:
                return False
        return True

    bindings = {}
    for name, address in ORIGINAL.items():
        data_symbol = is_data_symbol(name)
        segment = segment_for(address, 8 if data_symbol else 4)
        require(segment['name'] == ('__DATA' if data_symbol else '__TEXT_EXEC'),
                'Binding has wrong segment kind: ' + name)
        target = address + slide
        require(0 <= target < 1 << 64 and target % (8 if segment['name'] == '__DATA' else 4) == 0,
                'Invalid relocated PMGR binding: ' + name)
        bindings[name] = target
    segment_report = {s['name']: {'va': hex(s['va']), 'size': s['size'],
                      'sha256': sha(read(s['va'], s['filesize']))}
                      for s in segments if s['name'] != '__LINKEDIT'}
    require(segment_report['__TEXT_EXEC']['sha256'] ==
            '2c433279d004803b7ef02948e550ad6b77aa3f84b02a06a357e00fa738310a99',
            'Original ApplePMGR executable hash mismatch')
    return {'status': 'PASS', 'cache_bytes': len(data), 'cache_sha256': sha(data),
            'module': MODULE, 'segments': segment_report,
            'slide': slide, 'bindings': {name: hex(value) for name, value in bindings.items()},
            'provider_bytes': 0x4b8a8, 'nub_bytes': 0x740, 'first_opaque_slot': 177,
            'last_slot': 339, 'aon_slots': [320, 321], 'nub_provider_offset': 0x88,
            'aon_contract': 'original constant 0xffffffff read and immediate-return write',
            'runtime_observed': False}, bindings


def emit_headers(output, bindings):
    assembly = []
    for name, address in bindings.items():
        assembly += ['.globl ' + name, '.set ' + name + ', ' + hex(address)]
    (output / 'original-bindings.S').write_text('\n'.join(assembly) + '\n')
    header = ['#pragma once', '#include <IOKit/IOPMGR.h>',
              'class ApplePMGR : public IOPMGR {',
              '    OSDeclareAbstractStructors(ApplePMGR)',
              '    unsigned char originalPrivate[0x4b8a8 - sizeof(IOPMGR)];', 'public:']
    guards = []
    for slot in range(177, 340):
        if slot == 320:
            header += ['    virtual uint32_t readAon();']
        elif slot == 321:
            header += ['    virtual void writeAon(uint64_t);']
        else:
            header += [f'    virtual void opaque{slot}() = 0;']
            guards += [f'    void opaque{slot}() override {{ unavailable({slot}); }}']
    header += ['};', 'static_assert(sizeof(ApplePMGR) == 0x4b8a8, "Original provider layout");',
               'static constexpr uintptr_t kCI194OriginalReadAon = ' +
               hex(bindings['__ZN9ApplePMGR7readAonEv']) + 'ULL;',
               'static constexpr uintptr_t kCI194OriginalWriteAon = ' +
               hex(bindings['__ZN9ApplePMGR8writeAonEy']) + 'ULL;']
    (output / 'OriginalPMGRABI.h').write_text('\n'.join(header) + '\n')
    (output / 'GuardedPMGRSlots.inc').write_text('\n'.join(guards) + '\n')


def compile_command(record, source, obj, output, tree):
    args, command, index = record['arguments'], [], 0
    while index < len(args):
        value = args[index]
        if value == record['file']:
            command.append(source)
        elif value == '-o':
            command += ['-o', obj]
            index += 1
        elif value in ('-MF', '-MJ'):
            index += 1
        elif value == '-Xclang' and args[index + 1].startswith('-W'):
            index += 1
        elif value not in ('-MD', '-MP') and not value.startswith('-W'):
            command.append(value)
        index += 1
    command += ['-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-Wno-c99-designator',
                '-Wno-reorder-init-list', '-Wno-deprecated-enum-enum-conversion',
                '-Wno-unnecessary-virtual-specifier', '-Wno-missing-include-dirs',
                '-I', output, '-I', source.parent, '-I', tree / '.cache/xnu-work/osfmk']
    return command


def audit_object(path, np):
    data = path.read_bytes()
    image = np.macho(data)
    sections = [section for segment in image['segments'] for section in segment['sections']]
    cmd, = [offset for command, offset, length in image['commands'] if command == 2]
    symoff, count, stroff, strsize = struct.unpack_from('<4I', data, cmd + 8)
    names, defined = [], {}
    for index in range(count):
        at, kind, sect, _, address = struct.unpack_from('<IBBHQ', data, symoff + index * 16)
        end = data.find(b'\0', stroff + at, stroff + strsize)
        require(at < strsize and end >= 0, 'Invalid compiled symbol string')
        name = data[stroff + at:end].decode()
        names.append(name)
        if not kind & 0xe0 and kind & 0x0e == 0x0e and sect:
            require(name not in defined, 'Duplicate compiled definition: ' + name)
            defined[name] = (sect, address)
    for name in ('_sheng_pmgr_run', '_d2xq_ci194_pmgr_ready', '__GLOBAL__sub_I_ShengPlatformPower.cpp'):
        require(name in defined, 'Missing actual PMGR implementation: ' + name)
    require('_PE_halt_restart' not in names, 'PMGR object must not replace halt/restart')
    require(not any(s['name'] == '__mod_init_func' for s in sections),
            'PMGR local class must register only after original module load')
    number, address = defined['__ZTV18ShengPlatformPower']
    section = sections[number - 1]
    following = min([value for sec, value in defined.values() if sec == number and value > address] +
                    [section['va'] + section['size']])
    require(following - address == 342 * 8, 'Compiled provider vtable must contain exactly 340 slots')
    # Mach-O unsigned external pointer relocations must name the real AON methods.
    section_at = section['command_offset']
    reloff, nreloc = struct.unpack_from('<II', data, section_at + 56)
    relocations = {}
    for index in range(nreloc):
        at, bits = struct.unpack_from('<II', data, reloff + index * 8)
        if bits >> 27 & 1:
            require((bits & 0xffffff) < len(names), 'Compiled relocation symbol outside table')
            relocations[at] = (names[bits & 0xffffff], (bits >> 25) & 3, bits >> 28)
    for slot, name in ((320, '__ZN9ApplePMGR7readAonEv'), (321, '__ZN9ApplePMGR8writeAonEy')):
        offset = address - section['va'] + (2 + slot) * 8
        require(relocations.get(offset) == (name, 3, 0), 'Compiled AON slot does not bind original method')
    for segment in image['segments']:
        for section in segment['sections']:
            if section['name'] == '__text':
                code = data[section['fileoff']:section['fileoff'] + section['size']]
                require(not any(struct.unpack_from('<I', code, i)[0] & 0xffe0001f in
                                (0xd4000002, 0xd4000003) for i in range(0, len(code) - 3, 4)),
                        'PMGR provider must not contain HVC/SMC instructions')
    return {'status': 'PASS', 'sha256': sha(data), 'vtable_slots': 340,
            'aon_slot_relocations': 'original methods', 'automatic_class_initializer': False,
            'halt_restart_import': False, 'hvc_smc_instructions': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, default=ROOT)
    parser.add_argument('--sources', type=Path, help='Device checkout; default UNIXNU_DEVICE')
    parser.add_argument('--kernelcache', type=Path)
    parser.add_argument('--ipsw', type=Path, help='Explicit local IPSW, audit-only')
    parser.add_argument('--slide', type=lambda value: int(value, 0))
    parser.add_argument('--layout', type=Path, help='Optional prelink layout; its slide must match')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--audit-only', action='store_true')
    args = parser.parse_args()
    tree = args.tree.resolve()
    device = args.sources or os.environ.get('UNIXNU_DEVICE')
    device = Path(device).resolve() if device else tree
    output = (args.output or tree / 'build/sheng-platform-power').resolve()
    output.mkdir(parents=True, exist_ok=True)
    np_path = tree / 'kits/XnuPrelink/native_prelink.py'
    spec = importlib.util.spec_from_file_location('ci194_native_prelink', np_path)
    np = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(np)
    data, slide, origin = resolve_inputs(args, tree, device, output)
    if args.layout:
        layout_slide = json.loads(args.layout.read_text())['slide']
        layout_slide = int(layout_slide, 0) if isinstance(layout_slide, str) else layout_slide
        require(layout_slide == slide, 'Supplied prelink layout slide mismatch')
    audit, bindings = audit_cache(data, slide, np)
    audit['input'] = origin
    (output / 'original-audit.json').write_text(json.dumps(audit, indent=2) + '\n')
    emit_headers(output, bindings)
    if args.audit_only:
        print('PASS: exact original 21H461 ApplePMGR cache/module/ABI/instruction bindings')
        print(output / 'original-audit.json')
        return

    source = device / 'package/ShengPlatformPower.cpp'
    if not source.is_file():
        source = tree / 'kits/d2xq/ShengPlatformPower.cpp'
    require(source.is_file(), 'Missing device ShengPlatformPower.cpp')
    recipe = tree / 'build/xnu/ios-obj/DEVELOPMENT_ARM64_BCM2837/iokit/DEVELOPMENT/IOPlatformExpert.cpo.json'
    record = json.loads(recipe.read_text().strip().rstrip(','))
    obj = output / 'ShengPlatformPower.o'
    run(compile_command(record, source, obj, output, tree), output, 'compile', record['directory'])
    sdk = subprocess.check_output(['xcrun', '--sdk', 'iphoneos', '--show-sdk-path'], text=True).strip()
    run(['xcrun', 'clang', '-target', 'arm64-apple-ios17.0', '-isysroot', sdk,
         '-c', output / 'original-bindings.S', '-o', output / 'original-bindings.o'], output, 'bindings')
    objcopy = shutil.which('llvm-objcopy') or '/opt/homebrew/opt/llvm/bin/llvm-objcopy'
    late = output / 'ShengPlatformPower.late.o'
    run([objcopy, '--remove-section=__DATA,__mod_init_func',
         '--globalize-symbol=__GLOBAL__sub_I_ShengPlatformPower.cpp', obj, late], output, 'late-registration')
    object_audit = audit_object(late, np)
    (output / 'compiled-audit.json').write_text(json.dumps(object_audit, indent=2) + '\n')
    archive = output / 'libShengPlatformPower.a'
    archive.unlink(missing_ok=True)
    run(['xcrun', 'ar', 'rcs', archive, late, output / 'original-bindings.o'], output, 'archive')
    (output / 'inputs.json').write_text(json.dumps({
        'source': str(source.resolve()), 'source_sha256': sha(source.read_bytes()),
        'public_header_sha256': sha((source.parent / 'CI194PMGR.h').read_bytes()),
        'compile_recipe': str(recipe), 'compile_recipe_sha256': sha(recipe.read_bytes()),
        'original_audit_sha256': sha((output / 'original-audit.json').read_bytes()),
        'bindings_sha256': sha((output / 'original-bindings.o').read_bytes()),
        'compiled_audit': object_audit, 'archive': str(archive), 'archive_sha256': sha(archive.read_bytes()),
    }, indent=2) + '\n')
    print(archive)


if __name__ == '__main__':
    main()
