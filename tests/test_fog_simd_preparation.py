#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Exact fog/cloud SIMD preparation, sanitizer and scalar fallback checks; no game."""
from pathlib import Path
import argparse
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(__doc__);parser.add_argument('--benchmark',action='store_true');args=parser.parse_args()
with tempfile.TemporaryDirectory(prefix='northlight-fog-simd-') as tmp:
    for mode,flags in [('release',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']),('scalar',['-O2','-U__SSE2__'])]:
        exe=Path(tmp)/mode
        subprocess.run(['clang++','-std=c++17','-ffp-contract=off','-frounding-math',*flags,'-UNDEBUG','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),'-o',str(exe)],check=True)
        print(mode,flush=True)
        subprocess.run([str(exe),*(['--benchmark'] if mode=='release' and args.benchmark else [])],check=True)
