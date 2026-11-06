#!/usr/bin/env python3
# northlight-test: requires=cxx manual  (requires a prior probe_blend.h)
"""Compare the optimized GI publication with a preserved header; no game or GPU.

Usage: python tests/test_probe_blend_equivalence.py path/to/prior/probe_blend.h
"""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import shutil
import subprocess
import tempfile

args=[a for a in sys.argv[1:] if a!='--benchmark']
benchmark='--benchmark' in sys.argv[1:]
if not args:raise SystemExit('Usage: test_probe_blend_equivalence.py prior/probe_blend.h [--benchmark]')
prior=Path(args[0]).resolve()
with tempfile.TemporaryDirectory(prefix='northlight-probe-blend-') as tmp:
    folder=Path(tmp)
    shutil.copy2(prior,folder/'probe_blend_reference.h')
    binary=folder/'test'
    subprocess.run(['clang++','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
                    '-I',str(folder),*fp.test_include_flags(),
                    str(Path(__file__).with_suffix('.cpp')),'-o',str(binary)],check=True)
    subprocess.run([str(binary),*(['--benchmark'] if benchmark else [])],check=True)
