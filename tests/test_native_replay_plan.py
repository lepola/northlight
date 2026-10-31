#!/usr/bin/env python3
"""Independent byte/order checks for the import-plan compiler, not a scene replay."""
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import compile_native_replay_plan as plan


def main():
    checks = []
    def check(name, callback, rejected=False):
        try:
            callback()
        except ValueError:
            if not rejected:
                raise
        else:
            if rejected:
                raise AssertionError(name + ' accepted invalid input')
        checks.append(name)

    def proof_blob(entries=((0x401000, b'abc', 1),), prefix=0):
        payload = b''.join(value for _, value, _ in entries)
        table = b''.join(struct.pack('<QII', address, len(value), seen) for address, value, seen in entries)
        total = 48 + len(table) + len(payload) + 4
        data = struct.pack('<8s4I2Q2I', b'NLCODE1\0', 1, total, len(entries), len(payload),
                           sum(seen for _, _, seen in entries), prefix, 0, 0) + table + payload
        return data + struct.pack('<I', zlib.crc32(data))

    observed = proof_blob()
    check('code-proof-framing', lambda: plan.code_observation(observed))
    check('code-proof-crc', lambda: plan.code_observation(observed[:-1] + bytes([observed[-1] ^ 1])), True)
    check('code-proof-unseen-prefix', lambda: plan.code_observation(proof_blob(prefix=1)), True)
    check('code-proof-duplicate', lambda: plan.code_observation(proof_blob(((0x401000, b'a', 1), (0x401000, b'b', 1)))), True)
    check('code-proof-span-bound', lambda: plan.code_observation(proof_blob(((0x401000, b'a' * 513, 1),))), True)
    check('code-proof-zero-observations', lambda: plan.code_observation(proof_blob(((0x401000, b'a', 0),))), True)
    with tempfile.TemporaryDirectory(prefix='nljoin-code-') as folder:
        root = Path(folder); host = {'session': 'fixture', 'client_code_policy': 'strict-per-read'}
        (root / 'client-code.nlcode').write_bytes(observed)
        status = dict(format=1, session='fixture', complete=True, prefix_calls=0, policy='strict-per-read',
                      bytes=len(observed), sha256=hashlib.sha256(observed).hexdigest(), fps_eligible=False)
        def save_status():
            (root / 'client-code-status.json').write_text(json.dumps(status))
        save_status()
        check('code-proof-complete-sidecar', lambda: plan.client_code_inputs(root, host, False, {}))
        check('genuine-code-proof-required', lambda: plan.client_code_inputs(root, {'session': 'fixture'}, False, {}), True)
        assert plan.client_code_inputs(root, {'session': 'fixture'}, True, {}) == (b'', None)
        checks.append('explicit-authored-raw-compatibility')
        status['complete'] = False; save_status()
        check('code-proof-incomplete-sidecar', lambda: plan.client_code_inputs(root, host, False, {}), True)
        status['complete'] = True; status['sha256'] = '0' * 64; save_status()
        check('code-proof-sidecar-hash', lambda: plan.client_code_inputs(root, host, False, {}), True)

    def packet(entries):
        payload = b''.join(struct.pack('<QII', address, size, data is not None) + (data or b'') for address, size, data in entries)
        data = struct.pack('<8s6IQ', b'NLSNAP1\0', 1, 44 + len(payload), 1, len({(a, n) for a, n, _ in entries}),
                           len(entries), sum(len(p) if p is not None else 0 for _, _, p in entries), 1) + payload
        return data + struct.pack('<I', zlib.crc32(data))

    empty = [(address, 4, None) for address in (0xd38e38, 0xd38e58, 0xd38e78, 0xd38f50, 0xd39000)]
    data = packet(empty)
    check('known-failed-typed-roots', lambda: plan.admit_no_resource_aliases(data))
    check('missing-typed-roots', lambda: plan.admit_no_resource_aliases(packet(empty[:-1])), True)
    zero = [(address, 4, struct.pack('<I', 0)) for address in (0xd38e38, 0xd38e38, 0xd38e58, 0xd38e58,
                                                               0xd38e78, 0xd38e78, 0xd38f50, 0xd39000)]
    check('zero-identities-double-read', lambda: plan.admit_no_resource_aliases(packet(zero)))
    fields = bytearray(0x40); struct.pack_into('<I', fields, 0x24, 0x20000)
    nonzero = [(0xd38e38, 4, struct.pack('<I', 0x10000)), (0x10020, 0x40, fields), (0x20038, 4, struct.pack('<I', 0x30000))]
    check('nonzero-typed-identity', lambda: plan.admit_no_resource_aliases(packet(nonzero + empty[1:])), True)
    complete_chain = nonzero * 2 + empty[1:]
    assert plan.native_resource_identities(packet(complete_chain)) == [0x30000]
    checks.append('typed-alias-collector-deduplicates-double-read')
    changed_chain = copy.deepcopy(complete_chain)
    changed_chain[5] = (0x20038, 4, struct.pack('<I', 0x40000))
    assert plan.native_resource_identities(packet(changed_chain)) == [0x30000, 0x40000]
    checks.append('typed-alias-collector-keeps-distinct-FIFO-results')
    invalid_chain = copy.deepcopy(complete_chain)
    invalid_chain[2] = (0x20038, 4, struct.pack('<I', 3))
    check('typed-alias-collector-invalid-terminal', lambda: plan.native_resource_identities(packet(invalid_chain)), True)
    check('typed-alias-invalid-handle', lambda: plan.native_resource_identities(packet(
        [(0xd38e38, 4, struct.pack('<I', 0x10003))] + empty[1:])), True)
    invalid_fields = bytearray(fields); struct.pack_into('<I', invalid_fields, 0x24, 0x20003)
    check('typed-alias-invalid-GX', lambda: plan.native_resource_identities(packet(
        [(0xd38e38, 4, struct.pack('<I', 0x10000)), (0x10020, 0x40, invalid_fields)] + empty[1:])), True)
    variant_fields = bytearray(fields); variant_fields[8] = 4; struct.pack_into('<I', variant_fields, 0x3c, 0x30003)
    check('typed-alias-invalid-variant', lambda: plan.native_resource_identities(packet(
        [(0xd38e38, 4, struct.pack('<I', 0x10000)), (0x10020, 0x40, variant_fields)] + empty[1:])), True)
    journal = [{'record': 0, 'device_key': 0x20000, 'frame': 0, 'epoch': 0, 'thread': 99, 'trigger': 1,
                'draw_ordinal': 1, 'bytes': len(data), 'archive_crc32': struct.unpack_from('<I', data, len(data) - 4)[0]}]
    marker = ('NLSNAP1;session=fixture;record=0;device=20000;frame=0;epoch=0;thread=99;trigger=1;draw=1;bytes=' +
              str(len(data)) + ';crc=' + format(journal[0]['archive_crc32'], '08x'))
    def row(number, name, text, thread=2):
        return {'call': number, 'thread': thread, 'name': name, 'text': text}
    rows = [row(0, 'Direct3DCreate9', 'SDKVersion = 32) = 0x10000'),
            row(1, 'IDirect3D9::CreateDevice', 'this = 0x10000, ppReturnedDeviceInterface = &0x20000) = D3D_OK'),
            row(3, 'D3DPERF_SetMarker', 'col = 0x4e4c0001, wszName = L"' + marker + '")'),
            row(2, 'IDirect3DDevice9::DrawPrimitive', 'this = 0x20000) = D3D_OK'),
            row(4, 'IDirect3DDevice9::Present', 'this = 0x20000) = D3D_OK'),
            row(5, 'IDirect3DDevice9::Release', 'this = 0x20000) = 0')]
    summary, records = plan.join(rows, 'fixture', journal)
    assert summary == {'factory_call': 0, 'create_call': 1, 'factory': 0x10000, 'device': 0x20000, 'draws': 1,
                       'presents': 1, 'resets': 0, 'final_release': 5, 'trace_thread': 2, 'os_thread': 99}
    assert records[0][:5] == (2, 3, 0x20000, 1, 0)
    checks.append('nested-dispatch-preserves-marker-before-parent')
    check('numeric-sort-is-invalid', lambda: plan.join(sorted(rows, key=lambda r: r['call']), 'fixture', journal), True)
    check('missing-final-release', lambda: plan.join(rows[:-1], 'fixture', journal), True)
    check('missing-first-marker', lambda: plan.join(rows[:2] + rows[3:], 'fixture', journal), True)
    for label, old, new in [('wrong-color', '0x4e4c0001', '0x4e4c0002'), ('wrong-session', 'session=fixture', 'session=other'),
                            ('wrong-device', 'device=20000', 'device=30000'), ('wrong-frame', 'frame=0', 'frame=1'),
                            ('wrong-epoch', 'epoch=0', 'epoch=1'), ('wrong-record', 'record=0', 'record=1'),
                            ('wrong-OS-thread', 'thread=99', 'thread=98'), ('wrong-draw', 'draw=1', 'draw=2')]:
        changed = copy.deepcopy(rows); changed[2]['text'] = changed[2]['text'].replace(old, new)
        check(label, lambda changed=changed: plan.join(changed, 'fixture', journal), True)
    for label, index, key, value in [('wrong-dispatch-thread', 2, 'thread', 3), ('wrong-parent-call', 3, 'call', 4),
                                    ('failed-present', 4, 'text', 'this = 0x20000) = D3DERR_DEVICELOST'),
                                    ('unowned-draw', 3, 'text', 'this = 0x30000) = D3D_OK'),
                                    ('unsupported-Ex', 4, 'name', 'IDirect3DDevice9Ex::PresentEx')]:
        changed = copy.deepcopy(rows); changed[index][key] = value
        check(label, lambda changed=changed: plan.join(changed, 'fixture', journal), True)
    with tempfile.TemporaryDirectory(prefix='nljoin-order-') as folder:
        dump = Path(folder) / 'trace.dump'
        dump.write_text('// process.name = "fixture.exe"\n// process.commandLine = "fixture.exe args"\n' +
                        '\n'.join(f"{r['call']} @{r['thread']:x} {r['name']}({r['text']}" for r in rows), encoding='utf-8')
        assert list(plan.calls(dump)) == rows
        checks.append('official-text-record-order')
        checks.append('official-property-preamble')
        check('explicit-call-capacity', lambda: list(plan.calls(dump, max_calls=5)), True)
        dump.write_text(dump.read_text() + '\n5 @2 IDirect3D9::Release(this = 0x10000) = 0\n')
        check('duplicate-parser-call', lambda: list(plan.calls(dump)), True)
        dump.write_text('0 @0 Direct3DCreate9(SDKVersion = 32) = 0x10000 // incomplete\n')
        check('incomplete-parser-call', lambda: list(plan.calls(dump)), True)
        dump.write_text('arbitrary preamble\n0 @0 Direct3DCreate9(SDKVersion = 32) = 0x10000\n')
        check('unknown-parser-preamble', lambda: list(plan.calls(dump)), True)
        bounded = Path(folder) / 'bounded'; bounded.mkdir()
        original_limit = plan.MAX_DUMP_BYTES
        try:
            plan.MAX_DUMP_BYTES = 1024
            check('official-output-byte-capacity', lambda: plan.dump_trace(
                [sys.executable, '-c', 'import sys; sys.stdout.buffer.write(b"x"*2048)'], bounded, os.environ.copy()), True)
            assert (bounded / 'trace.dump').stat().st_size <= 1024
        finally:
            plan.MAX_DUMP_BYTES = original_limit
    sources = [Path(__file__), Path(plan.__file__), Path(plan.__file__).with_name('prepare_native_scene.py')]
    report = {'pass': True, 'checks': checks, 'source_sha256': {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources},
              'authored_parser_contract_only': True, 'actual_trace_executed': False, 'scene_ready': False, 'fps_claimed': False}
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
