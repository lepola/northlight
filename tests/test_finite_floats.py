#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Bounded finite float vectors retain scalar acceptance for every lane."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='finite-floats-') as directory:
    for label,flags in [('O2',['-O2']),('scalar',['-O2','-U__SSE2__']),('ASan-UBSan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(directory)/label
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(fp.TESTS/'finite_floats.cpp'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
