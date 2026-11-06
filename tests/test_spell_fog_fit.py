#!/usr/bin/env python3
# northlight-test: requires=cxx manual  (requires a prior spell_fog.h)
"""Compare the optimized fog fitter with a preserved header; no game or GPU.

Usage: python tests/test_spell_fog_fit.py path/to/prior/spell_fog.h
"""
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import shutil
import subprocess
import tempfile

prior=Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix='northlight-spell-fit-') as tmp:
    folder=Path(tmp)
    shutil.copy2(prior,folder/'spell_fog_reference.h')
    binary=folder/('test.exe' if sys.platform=='win32' else 'test')
    compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if sys.platform=='win32' else ['clang++']
    subprocess.run([*compiler,'-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
                    '-I',str(folder),*fp.test_include_flags(),
                    str(Path(__file__).with_suffix('.cpp')),'-o',str(binary)],env=fp.zig_env() if sys.platform=='win32' else None,check=True)
    subprocess.run([str(binary)],check=True)
