#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Byte-exact GI atlas packing and a separate CPU microbenchmark; no game."""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import subprocess
import tempfile

with tempfile.TemporaryDirectory(prefix='northlight-probe-upload-') as tmp:
    binary=Path(tmp)/'test'
    for mode,flags in [('release',['-O2']),('sanitized',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]:
        subprocess.run(['clang++','-std=c++17',*flags,'-UNDEBUG','-Wall','-Wextra','-Werror',
                        *fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),
                        '-o',str(binary)],check=True)
        print(mode,flush=True)
        subprocess.run([str(binary)],check=True)
