#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Execute all geometry reserve/contiguous/overflow/generation boundary checks."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='geometry-memory-') as folder:
    for mode,flags in [('O2',['-O2']),('sanitizers',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(folder)/mode
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(fp.TESTS/'test_geometry_memory.cpp'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
