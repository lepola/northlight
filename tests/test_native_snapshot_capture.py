#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Bounded receipt/marker/status contract; no D3D, game, trace authenticity or FPS claim."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--tsan', action='store_true')
    args = parser.parse_args()
    source = fp.TESTS / 'test_native_snapshot_capture.cpp'
    inputs = [Path(__file__), source, fp.src('native_snapshot_capture.h'), fp.src('snapshot_capture_sink.h'),
              fp.src('snapshot_archive.h'), fp.src('snapshot_store.h')]
    before = {str(path): sha(path) for path in inputs}
    root = fp.output_dir()
    root.mkdir(parents=True, exist_ok=True)
    folder = Path(tempfile.mkdtemp(prefix='native-snapshot-capture-', dir=root))
    grammar = re.compile(r'NLSNAP1;session=([A-Za-z0-9-]{1,64});record=([0-9]+);device=([0-9a-f]+);frame=([0-9]+);epoch=([0-9]+);thread=([0-9]+);trigger=([1-3]);draw=([0-9]+);bytes=([0-9]+);crc=([0-9a-f]{8})')
    records = []
    variants = [('O2', ['-O2']), ('O0', ['-O0']), ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-omit-frame-pointer'])]
    if args.tsan:
        variants.append(('tsan', ['-O1', '-g', '-fsanitize=thread', '-fno-omit-frame-pointer']))
    for mode, flags in variants:
        executable, output = folder / ('fixture-' + mode), folder / (mode + '-output')
        command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', *flags,
                   *fp.test_include_flags(), str(source), '-o', str(executable)]
        print('COMPILE', command, flush=True)
        compiled = subprocess.run(command, capture_output=True, text=True)
        (folder / (mode + '-compile.log')).write_text(compiled.stdout + compiled.stderr)
        compiled.check_returncode()
        run = subprocess.run([str(executable), str(output)], capture_output=True, text=True, timeout=60)
        (folder / (mode + '-run.log')).write_text(run.stdout + run.stderr)
        print(run.stdout, run.stderr, flush=True)
        run.check_returncode()
        assert re.search(r'NATIVE_SNAPSHOT_CAPTURE_PASS groups=2[0-9] producerAllocations=0 exactReceiptCRC=1 markerOutsideCompleted=1 traceJoinValidated=0', run.stdout)
        statuses = {}
        for path in output.glob('*/native-capture-status.json'):
            if path.parent.name == 'status-collision':
                assert path.read_bytes() == b'immutable'
                continue
            status = json.loads(path.read_text())
            assert status['format'] == 1 and status['session'] == 'capture-oracle'
            assert status['diagnostic'] and not status['fps_eligible'] and not status['trace_join_validated']
            assert status['capture_layer'] == 'above-northlight' and not status['capture_layer_validated']
            assert status['status_allowance_bytes'] == 768 and status['budget_bytes'] <= 256 << 20
            assert status['finished']
            if path.parent.name in ('roundtrip', 'lifetime'):
                assert status['complete'] and status['started'] and not status['pending']
                assert status['accepted'] == status['retired'] == status['marked'] and not status['failures'] and not status['sink_failures']
            else:
                assert not status['complete'] and (status['failures'] or status['sink_failures'])
            statuses[path.parent.name] = status
        assert set(statuses) == {'roundtrip', 'unobserved', 'observe-first', *(f'provenance-{i}' for i in range(6)),
                                 'marker-failure', 'reentrant', 'concurrent', 'overflow', 'budget', 'byte-budget', 'archive',
                                 'producer', 'lifetime', 'packet-collision'}
        assert not (output / 'roundtrip/native-capture-status.pending').exists()
        assert not (output / 'lifetime/native-capture-status.pending').exists()
        assert (output / 'pending-collision/native-capture-status.pending').read_bytes() == b'immutable'
        assert not (output / 'pending-collision/native-capture-status.json').exists()
        assert (output / 'status-collision/native-capture-status.json').read_bytes() == b'immutable'
        assert json.loads((output / 'status-collision/native-capture-status.pending').read_text())['complete']
        markers = []
        for line in (output / 'roundtrip.markers.txt').read_text().splitlines():
            match = grammar.fullmatch(line)
            assert match, line
            session, record, device, frame, epoch, thread, trigger, draw, size, crc = match.groups()
            record, size, crc = int(record), int(size), int(crc, 16)
            assert session == 'capture-oracle' and int(device, 16) == 0x123400
            assert int(frame) == 7 + record and int(epoch) == 3 and int(thread) == 42 and int(trigger) == 2 and int(draw) == 123
            raw = (output / 'roundtrip' / f'{record:06d}.nlsnap').read_bytes()
            assert len(raw) == size and raw[:8] == b'NLSNAP1\0' and struct.unpack_from('<I', raw, 12)[0] == size
            assert struct.unpack_from('<Q', raw, 32)[0] == int(draw) and struct.unpack_from('<I', raw, 16)[0] == int(trigger)
            assert crc == struct.unpack_from('<I', raw, size - 4)[0] == zlib.crc32(raw[:-4])
            markers.append(dict(record=record, marker=line, archive_sha256=sha(output / 'roundtrip' / f'{record:06d}.nlsnap')))
        assert [item['record'] for item in markers] == [0, 1]
        maximum = (output / 'maximum.marker').read_text().strip()
        match = grammar.fullmatch(maximum)
        assert match and len(maximum) < 320 and len(match[1]) == 64
        assert int(match[4]) == int(match[8]) == 2**64 - 1 and int(match[5]) == int(match[6]) == 2**32 - 1
        for invalid in (maximum + ';extra=1', 'prefix' + maximum, maximum.replace(';device=', ';device=0x'),
                        maximum.replace(';crc=ffffffff', ';crc=fffffff'), maximum.replace(';trigger=3', ';trigger=0'),
                        maximum.replace('session=', 'session=;'), maximum + '\x00'):
            assert grammar.fullmatch(invalid) is None
        records.append(dict(mode=mode, command=command, stdout=run.stdout, stderr=run.stderr,
                            executable_sha256=sha(executable), statuses=statuses, markers=markers,
                            output_files={str(path.relative_to(output)): sha(path) for path in sorted(output.rglob('*')) if path.is_file()}))
    assert before == {path: sha(Path(path)) for path in before}, 'Frozen source changed'
    (folder / 'validation.json').write_text(json.dumps(dict(status='pass', inputs=before, inputs_unchanged=True,
        records=records, producer_allocations=0, trace_join_validated=False, game_launched=False,
        gpu_used=False, full_scene_fps_claimed=False), indent=2) + '\n')
    print('PASS exact receipt CRC, separate marker observer, provenance/order/overflow failures and exclusive owner status; no trace/FPS claim')


if __name__ == '__main__':
    main()
