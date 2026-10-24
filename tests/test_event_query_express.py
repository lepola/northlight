#!/usr/bin/env python3
# northlight-test: requires=cxx,zig
"""Real command-stream EVENT wait versus the ordered fallback; no GPU/game."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0, str(fp.TESTS))
import test_command_stream as stream

with tempfile.TemporaryDirectory(prefix='event-boundary-') as directory:
    root = Path(directory)
    (root / 'd3d9_stub.h').write_text('#ifndef FALSE\n#define FALSE 0\n#endif\n' + stream.stub())
    (root / 'cs_generated.inc').write_text(stream.generated_cpp())
    for name, flags, mode in [
        ('ordered-control', ['-O2'], 'baseline'),
        ('immediate-mailbox-control', ['-O2'], 'ungated'),
        ('current', ['-O2'], ''),
        ('asan', ['-O1', '-g', '-fsanitize=address,undefined', '-fno-sanitize=vptr', '-fno-omit-frame-pointer'], ''),
        ('tsan', ['-O1', '-g', '-fsanitize=thread'], ''),
    ]:
        overlay = root / name
        overlay.mkdir()
        for header in fp.src('stream_proxies.h').parent.glob('*.h'):
            shutil.copyfile(header, overlay / header.name)
        if mode == 'baseline':
            # Disable only the new boundary branch. The production body still
            # uses the original ordered runTask path and the same COM pin.
            header = overlay / 'stream_proxies.h'
            text = header.read_text()
            guard = 'if(event&&!inPumpedWait&&epoch==core->queryEpoch.load()'
            assert text.count(guard) == 1
            header.write_text(text.replace(guard, 'if(false&&event&&!inPumpedWait&&epoch==core->queryEpoch.load()'))
        elif mode == 'ungated':
            # The previous candidate serves the mailbox immediately, before
            # its captured END is guaranteed to have replayed.
            header = overlay / 'stream_proxies.h'
            text = header.read_text()
            readiness = '[](void* p){return (*static_cast<Work*>(p)->canRun)();}'
            assert text.count(readiness) == 1
            header.write_text(text.replace(readiness, 'nullptr'))
        binary = root / (name + '.exe')
        command = ['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-pthread', '-DNL_STUB',
                   *flags, '-I', str(overlay), '-I', str(root), *fp.test_include_flags(),
                   str(fp.TESTS / 'test_event_query_express.cpp'), '-o', str(binary)]
        print('COMPILE', command, flush=True)
        subprocess.run(command, check=True)
        result = subprocess.run([str(binary), *([mode] if mode else [])], text=True, capture_output=True, timeout=120)
        print(name, result.stdout, result.stderr, flush=True)
        result.check_returncode()
        assert not any(message in result.stderr for message in ['ThreadSanitizer', 'AddressSanitizer', 'runtime error'])
print('PASS production EVENT boundary wait and ordered negative control; native O2, ASan/UBSan and TSan; no game/GPU.')
