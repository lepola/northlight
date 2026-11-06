#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exercise HDR shader-cache COM ownership and deterministic address reuse without a GPU."""
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source = fp.src('hdr_color.h').read_text()
ownership = 'original->AddRef();it->second.original=original;'
assert source.count(ownership) == 1
with tempfile.TemporaryDirectory(prefix='northlight-hdr-color-lifetime-') as tmp:
    folder = Path(tmp)
    for label, header in [('fixed', source), ('before-fix', source.replace(ownership, 'it->second.original=nullptr;'))]:
        (folder/'hdr_color.h').write_text(header)
        exe = folder/(label+'.exe')
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                        '-Wno-microsoft-exception-spec', '-I'+str(folder), *fp.test_include_flags(),
                        str(fp.TESTS/'hdr_color_lifetime.cpp'), '-o', str(exe)], env=fp.zig_env(), check=True)
        run = subprocess.run([str(exe)], text=True, capture_output=True, timeout=30)
        if label == 'before-fix':
            assert run.returncode != 0 and 'old->refs==2' in run.stderr, run.stdout+run.stderr
            print('before-fix: expected retained-owner/refcount assertion failed')
        else:
            assert run.returncode == 0, run.stdout+run.stderr
            print(run.stdout, end='')
