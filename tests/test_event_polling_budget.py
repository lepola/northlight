#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Deterministic production EVENT polling deadlines, stalled clocks and fast yields."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='event-budget-') as tmp:
    exe=Path(tmp)/'budget'
    for name,flags in [('O2',['-O2']),('O0',['-O0']),('sanitizers',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True,timeout=30)
