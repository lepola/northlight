#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Read-only frontend lighting evidence; no device calls or native state changes."""
import subprocess
import sys
import tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='native-local-light-') as folder:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(folder)/label
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),
                        str(fp.TESTS/'test_native_local_light_probe.cpp'),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
