#!/usr/bin/env python3
# northlight-test: requires=cxx,zig
"""Final-only queue idle attribution, publication races and disabled-path checks."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0, str(fp.TESTS))
import test_command_stream as stream


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


source = fp.TESTS / 'test_stream_idle_attribution.cpp'
renderer = fp.src('renderer.cpp').read_text()
device = fp.src('stream_device.h').read_text()
assert 'options.idleProfiling=&NorthlightRenderThreadProbe::profiling;' in renderer
assert 'replayer.idleProfiling=opt.idleProfiling;' in device
assert 'const bool timedWait=!pending_.empty();if(timedWait)pollQueries();' in fp.src('replay_thread.h').read_text()
inputs = [p for p in fp.SRC.rglob('*') if p.is_file()] + [source, Path(__file__),
    fp.TESTS / 'test_command_stream.cpp', fp.TESTS / 'test_command_stream.py',
    fp.TESTS / 'command_stream_targets.h', fp.TESTS / 'command_stream_scenarios.h']
before = {str(p): sha(p) for p in inputs}
records = []
with tempfile.TemporaryDirectory(prefix='stream-idle-attribution-') as directory:
    root = Path(directory)
    (root / 'd3d9_stub.h').write_text('#ifndef FALSE\n#define FALSE 0\n#endif\n' + stream.stub())
    (root / 'cs_generated.inc').write_text(stream.generated_cpp())
    for name, flags in [
        ('O2', ['-O2']), ('O0', ['-O0']),
        ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize=vptr', '-fno-omit-frame-pointer']),
        ('tsan', ['-O1', '-g', '-fsanitize=thread']),
    ]:
        binary = root / ('fixture-' + name)
        command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', '-DNL_STUB',
                   *flags, '-I', str(root), *fp.test_include_flags(), str(source), '-o', str(binary)]
        print('COMPILE', command, flush=True)
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary)], text=True, capture_output=True, timeout=120)
        print(name, result.stdout, result.stderr, flush=True)
        result.check_returncode()
        assert 'STREAM_IDLE_ATTRIBUTION_PASS groups=15' in result.stdout
        assert not any(message in result.stderr for message in ('ThreadSanitizer', 'AddressSanitizer', 'runtime error'))
        records.append({'mode': name, 'command': command, 'exe_sha256': sha(binary), 'stdout': result.stdout, 'stderr': result.stderr})
assert all(sha(Path(path)) == expected for path, expected in before.items()), 'Source/helper changed during final validation'
output = fp.output_dir()
output.mkdir(parents=True, exist_ok=True)
(output / 'stream-idle-attribution-validation.json').write_text(json.dumps({
    'inputs': before, 'inputs_unchanged': True, 'records': records, 'game_launched': False, 'gpu_used': False}, indent=2) + '\n')
print('PASS actual queue/replayer publication, waits, loss/reset, interruption, bounded reports and profile-off paths; O2/O0/ASan/UBSan/TSan.')
