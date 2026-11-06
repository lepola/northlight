#!/usr/bin/env python3
# northlight-test: requires=zig
"""Quality parser/presets/default identity: O2/debug on Windows, O2/ASan/UBSan elsewhere."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
from pathlib import Path
import subprocess,tempfile
import os
HERE=Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='northlight-quality-') as tmp:
    compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu'] if os.name=='nt' else ['clang++']
    variants=[('O2',['-O2']),('debug',['-O0','-g'])] if os.name=='nt' else [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]
    for label,flags in variants:
        exe=Path(tmp)/('test-'+label)
        subprocess.run([*compiler,'-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_quality_settings.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],cwd=fp.src('windows-package/northlight-quality.ini').parents[1],text=True),end='',flush=True)  # the test reads windows-package/northlight-quality.ini
