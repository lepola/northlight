#!/usr/bin/env python3
"""Mutate an independently prepared NLJOIN1 plan against the private host.

Manual acceptance only: requires the parent's admitted positive plan and built
host. This does not compile a plan, build a host, launch a game, or claim scene
fidelity/FPS. By default every mutation must fail before loading the stand DLL;
--dispatch-negatives additionally exercises actual private-host dispatch.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import zlib


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def game_absent():
    result = subprocess.run(['tasklist', '/FI', 'IMAGENAME eq Wow.exe', '/FO', 'CSV', '/NH'],
                            capture_output=True, text=True, timeout=20, check=True)
    if '"wow.exe"' in result.stdout.lower():
        raise RuntimeError('Game is running; no replay was started')
    return {'command': result.args, 'stdout': result.stdout, 'stderr': result.stderr}


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--host', type=Path, required=True)
    parser.add_argument('--plan', type=Path, required=True)
    parser.add_argument('--trace', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cwd', type=Path)
    parser.add_argument('--dispatch-negatives', action='store_true')
    parser.add_argument('--allow-authored-transport', action='store_true')
    parser.add_argument('--alias-fixture', type=Path)
    args = parser.parse_args()
    host, plan_path, trace_path = (p.resolve() for p in (args.host, args.plan, args.trace))
    if os.name != 'nt' or host.name.lower() != 'northlight-native-retrace.exe':
        raise ValueError('Only the distinctly named private Windows replay host is supported')
    out = args.output.resolve()
    if out.exists() or any(p == out or p.is_relative_to(out) for p in (host, plan_path, trace_path)):
        raise ValueError('Evidence output must be new and separate from frozen inputs')
    data = plan_path.read_bytes()
    if len(data) < 256 or data[:8] != b'NLJOIN1\0' or struct.unpack_from('<II', data, 8) != (1, 256):
        raise ValueError('A valid parent-authored NLJOIN1 fixture is required')
    count, flags, session_n, dll_n, trace_n, pe_n = struct.unpack_from('<6I', data, 24)
    if not 1 <= count <= 20000 or flags not in (5, 7, 9, 11, 13, 15) or not 1 <= session_n <= 64:
        raise ValueError('Positive fixture is not the supported above-Northlight transport')
    if flags & 4 and not args.allow_authored_transport:
        raise ValueError('Authored transport requires explicit --allow-authored-transport; it is never FPS eligible')
    pe_at = 256 + session_n + dll_n + trace_n
    records_at = pe_at + pe_n
    packets_at = records_at + count * 80
    if packets_at >= len(data) or hashlib.sha256(data[256:]).digest() != data[224:256]:
        raise ValueError('Positive fixture payload bounds/hash differ')
    dll = Path(data[256 + session_n:256 + session_n + dll_n].decode('utf-8')).resolve()
    selected = Path(data[256 + session_n + dll_n:pe_at].decode('utf-8')).resolve()
    if selected != trace_path or hashlib.sha256(trace_path.read_bytes()).digest() != data[128:160]:
        raise ValueError('Positive fixture identifies another trace')
    if bytes.fromhex(digest(dll)) != data[192:224]:
        raise ValueError('Positive fixture identifies another stand DLL')
    watched = [Path(__file__).resolve(), host, plan_path, trace_path, dll]
    root = Path(__file__).resolve().parents[1]
    watched += [root / 'src/stream' / name for name in
                ('native_retrace_bridge.h', 'native_retrace_bridge.cpp', 'client_image.h',
                 'snapshot_archive.h', 'snapshot_store.h')]
    watched.append(root / 'scripts/prepare_native_retrace_host.py')
    watched.append(root / 'tests/test_native_retrace_aliases.cpp')
    alias_fixture = args.alias_fixture.resolve() if args.alias_fixture else None
    if alias_fixture:
        if alias_fixture.name.lower() != 'northlight-native-alias-fixture.exe':
            raise ValueError('Use the distinctly named private CPU alias fixture')
        watched.append(alias_fixture)
    watched += [root / 'src/sky' / name for name in ('celestial_disc_native.h', 'celestial_glare_native.h')]
    before = {str(p): digest(p) for p in watched}
    out.mkdir(parents=True)
    report = {'format': 1, 'inputs': before, 'cases': [], 'game_launched': False,
              'full_scene_replay_ready': False, 'fps_claimed': False,
              'dispatch_negatives': args.dispatch_negatives, 'alias_cases': [],
              'authored_transport': bool(flags & 4), 'fps_eligible': False}
    cases = []

    def changed(name, reason, edit, payload=False, dispatch=False, authored=None):
        value = bytearray(data)
        edit(value)
        if payload:
            value[224:256] = hashlib.sha256(value[256:]).digest()
        cases.append((name, reason, bytes(value), dispatch, authored))

    def u32(value, at, number):
        struct.pack_into('<I', value, at, number)

    def u64(value, at, number):
        struct.pack_into('<Q', value, at, number)

    changed('magic', 'plan-header', lambda b: b.__setitem__(0, b[0] ^ 1))
    changed('version', 'plan-header', lambda b: u32(b, 8, 2))
    changed('header-size', 'plan-header', lambda b: u32(b, 12, 255))
    changed('total-size', 'plan-header', lambda b: u64(b, 16, len(b) + 1))
    changed('unknown-flags', 'capture-layer-or-required-aliases', lambda b: u32(b, 28, flags | 16))
    changed('alias-flag-mismatch', 'plan-alias-flag', lambda b: u32(b, 28, flags ^ 2))
    changed('authored-without-allowance', 'authored-not-allowed', lambda b: u32(b, 28, flags | 4), authored=False)
    changed('record-cap', 'plan-capacity', lambda b: u32(b, 24, 20001))
    changed('payload-hash', 'plan-payload', lambda b: b.__setitem__(224, b[224] ^ 1))
    changed('session', 'plan-session', lambda b: b.__setitem__(256, ord(';')), payload=True)
    changed('pe-bytes', 'plan-pe', lambda b: b.__setitem__(pe_at, b[pe_at] ^ 1), payload=True)
    changed('trace-hash', 'selected-trace', lambda b: b.__setitem__(128, b[128] ^ 1))
    changed('dll-hash', 'stand-dll-hash', lambda b: b.__setitem__(192, b[192] ^ 1))
    changed('record-reserved', 'packet-record', lambda b: u32(b, records_at + 76, 1), payload=True)
    changed('packet-offset', 'packet-record', lambda b: u64(b, records_at + 64, 1), payload=True)
    old_crc = struct.unpack_from('<I', data, records_at + 60)[0]
    changed('packet-crc', 'packet-archive', lambda b: u32(b, records_at + 60, old_crc ^ 1), payload=True)
    first_bytes = struct.unpack_from('<I', data, records_at + 56)[0]
    copies = struct.unpack_from('<I', data, packets_at + 24)[0]
    copy_at = packets_at + 40
    required = None
    fields = {}
    for _ in range(copies):
        address, size, ok = struct.unpack_from('<QII', data, copy_at)
        if address == 0xd38e38 and size == 4:
            required = copy_at
        fields.setdefault((address, size), []).append((ok, copy_at + 16))
        copy_at += 16 + (size if ok else 0)
    if required is None:
        raise ValueError('Positive fixture lacks the required first typed body key')

    def packet_crc(value):
        crc = zlib.crc32(value[packets_at:packets_at + first_bytes - 4])
        u32(value, packets_at + first_bytes - 4, crc)
        u32(value, records_at + 60, crc)

    def missing_typed_key(value):
        # Rename every FIFO copy of the same key, preserving archive validity.
        at = packets_at + 40
        for _ in range(copies):
            address, size, ok = struct.unpack_from('<QII', value, at)
            if address == 0xd38e38 and size == 4:
                u64(value, at, 0xd38e3c)
            at += 16 + (size if ok else 0)
        packet_crc(value)

    changed('missing-typed-key', 'packet-typed-identity', missing_typed_key, payload=True)
    terminal = None

    def first_integer(address):
        values = fields.get((address, 4), [])
        return struct.unpack_from('<I', data, values[0][1])[0] if values and values[0][0] else None

    for body in (0xd38e28, 0xd38e48, 0xd38e68):
        handle = first_integer(body + 0x10)
        if not handle:
            continue
        bank = fields.get((handle + 0x20, 64), [])
        if not bank or not bank[0][0]:
            continue
        at = bank[0][1]
        gx, variant = struct.unpack_from('<I', data, at + 0x24)[0], struct.unpack_from('<I', data, at + 0x3c)[0]
        if data[at + 8] & 4 and variant:
            gx = first_integer(variant + 0x18)
        if gx and fields.get((gx + 0x38, 4)) and all(ok for ok, _ in fields[gx + 0x38, 4]):
            terminal = gx + 0x38
            break
    if terminal is None:
        raise ValueError('Positive fixture needs a successful typed handle/fields/gx/terminal chain')

    def invalid_typed_identity(value):
        for ok, at in fields[terminal, 4]:
            if not ok:
                raise ValueError('Terminal mutation cannot invent a successful read')
            u32(value, at, 1)
        packet_crc(value)

    changed('invalid-typed-identity', 'packet-typed-identity', invalid_typed_identity, payload=True)
    changed('truncated', 'plan-header', lambda b: b.pop())
    if args.dispatch_negatives:
        first_target = struct.unpack_from('<Q', data, records_at)[0]
        create = struct.unpack_from('<Q', data, 64)[0]
        if first_target <= create + 1:
            raise ValueError('Dispatch fixture needs a pre-draw state call for the independent wrong-call mutation')
        changed('wrong-target-call', 'target-draw', lambda b: u64(b, records_at, first_target - 1), payload=True, dispatch=True)
        old_thread = struct.unpack_from('<I', data, 120)[0]

        def wrong_owner(value):
            u32(value, 120, old_thread + 1)
            for i in range(count):
                u32(value, records_at + i * 80 + 48, old_thread + 1)

        changed('wrong-trace-owner', 'call-owner-or-domain', wrong_owner, payload=True, dispatch=True)
        old_os = struct.unpack_from('<I', data, 124)[0]

        def wrong_source_owner(value):
            u32(value, 124, old_os + 1)
            for i in range(count):
                u32(value, records_at + i * 80 + 52, old_os + 1)

        changed('wrong-source-owner', 'marker-contents', wrong_source_owner, payload=True, dispatch=True)
        old_device = struct.unpack_from('<Q', data, 80)[0]

        def wrong_device(value):
            u64(value, 80, old_device + 4)
            for i in range(count):
                u64(value, records_at + i * 80 + 16, old_device + 4)

        changed('wrong-device', 'device-result', wrong_device, payload=True, dispatch=True)

        def wrong_epoch(value):
            total = struct.unpack_from('<Q', value, 104)[0]
            epoch = struct.unpack_from('<I', value, records_at + 40)[0]
            u64(value, 104, max(total, epoch + 1))
            u32(value, records_at + 40, epoch + 1)

        changed('wrong-epoch', 'marker-contents', wrong_epoch, payload=True, dispatch=True)
        total_draw = struct.unpack_from('<Q', data, 88)[0]
        changed('wrong-final-count', 'incomplete-transport', lambda b: u64(b, 88, total_draw + 1), dispatch=True)
    try:
        report['processes_before'] = game_absent()
        if alias_fixture:
            alias_modes = [('positive', None), ('missing', 'alias-missing-object'),
                           ('wrong-kind', 'alias-object-kind'), ('dead', 'alias-dead-object'),
                           ('canonical-dead', 'alias-canonical-lifetime'),
                           ('mapping-changed', 'alias-replayed-generation'), ('reuse', 'object-reuse-or-kind'),
                           ('dead-getter', 'object-reuse-or-kind'), ('stale-interface', 'object-reuse-or-kind'),
                           ('index-blob', 'argument-u32'), ('index-kind', 'input-object-kind')]
            for mode, refusal in alias_modes:
                report['alias_before_' + mode] = game_absent()
                command = [str(alias_fixture), mode]
                result = subprocess.run(command, capture_output=True, text=True, timeout=30)
                (out / ('alias-' + mode + '.stdout')).write_text(result.stdout, encoding='utf-8')
                (out / ('alias-' + mode + '.stderr')).write_text(result.stderr, encoding='utf-8')
                passed = (result.returncode == 3 and ('NORTHLIGHT_NATIVE_BRIDGE refused reason=' + refusal) in result.stderr
                          if refusal else result.returncode == 0 and 'NORTHLIGHT_NATIVE_ALIAS_FIXTURE PASS' in result.stdout)
                passed = passed and 'warning:' not in result.stderr.lower()
                report['alias_cases'].append({'mode': mode, 'command': command, 'exit_code': result.returncode,
                                              'expected_refusal': refusal, 'passed': passed})
                report['alias_after_' + mode] = game_absent()
                if not passed:
                    raise AssertionError(f'Alias CPU fixture {mode} failed; evidence preserved')
                if any(digest(Path(p)) != sha for p, sha in before.items()):
                    raise AssertionError('Frozen source/input changed during alias validation')
        for name, reason, value, dispatch, authored in cases:
            report['process_before_' + name] = game_absent()
            fixture = out / (name + '.nlj')
            fixture.write_bytes(value)
            env = os.environ.copy()
            env['NORTHLIGHT_RETRACE_PLAN'] = str(fixture)
            env.pop('NORTHLIGHT_RETRACE_ALLOW_AUTHORED', None)
            if args.allow_authored_transport and authored is not False:
                env['NORTHLIGHT_RETRACE_ALLOW_AUTHORED'] = '1'
            command = [str(host), '--headless', str(trace_path)]
            result = subprocess.run(command, cwd=str((args.cwd or host.parent).resolve()), env=env,
                                    capture_output=True, text=True, timeout=90)
            (out / (name + '.stdout')).write_text(result.stdout, encoding='utf-8')
            (out / (name + '.stderr')).write_text(result.stderr, encoding='utf-8')
            expected = 'NORTHLIGHT_NATIVE_BRIDGE refused reason=' + reason
            passed = (result.returncode in (1, 3) and expected in result.stderr
                      and 'NORTHLIGHT_NATIVE_BRIDGE PASS' not in result.stderr)
            case = {'name': name, 'expected_refusal': reason, 'passed': passed,
                    'plan_sha256': digest(fixture), 'command': command, 'exit_code': result.returncode,
                    'dispatch_used': dispatch}
            report['cases'].append(case)
            report['process_after_' + name] = game_absent()
            if not passed:
                raise AssertionError(f'{name}: expected specific refusal {reason}; evidence preserved')
            if not dispatch and 'NORTHLIGHT STAND REPLAY BUILD' in result.stderr:
                raise AssertionError('A preflight mutation reached the stand DLL')
            if any(digest(Path(p)) != sha for p, sha in before.items()):
                raise AssertionError('Frozen source/input changed during transport validation')
        report['processes_after'] = game_absent()
        report['inputs_unchanged'] = all(digest(Path(p)) == sha for p, sha in before.items())
        report['pass'] = report['inputs_unchanged'] and all(case['passed'] for case in report['cases'])
    except Exception as error:
        report['pass'] = False
        report['failure'] = str(error)
        raise
    finally:
        (out / 'validation.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print('PASS private native transport mutations; no game-scene/FPS acceptance')


if __name__ == '__main__':
    main()
