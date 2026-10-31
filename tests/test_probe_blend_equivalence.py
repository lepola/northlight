#!/usr/bin/env python3
# northlight-test: requires=cxx
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
prior=Path(args[0]).resolve() if args else fp.TESTS/'fixtures/probe_blend_reference.h'
with tempfile.TemporaryDirectory(prefix='northlight-probe-blend-') as tmp:
    folder=Path(tmp)
    shutil.copy2(prior,folder/'probe_blend_reference.h')
    binary=folder/'test'
    subprocess.run(['clang++','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
                    '-I',str(folder),*fp.test_include_flags(),
                    str(Path(__file__).with_suffix('.cpp')),'-o',str(binary)],check=True)
    subprocess.run([str(binary),*(['--benchmark'] if benchmark else [])],check=True)
