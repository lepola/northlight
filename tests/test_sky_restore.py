#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Native sky sequence matching and bounded managed-mip reads, without a GPU."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

with tempfile.TemporaryDirectory(prefix='northlight-sky-restore-') as tmp:
    for name in ('test_sky_restore', 'test_sky_mip_hash'):
        binary = Path(tmp)/(name+'.exe' if os.name=='nt' else name)
        compiler = ([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2'] if os.name=='nt' else
                    ['c++','-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])
        subprocess.run([*compiler,'-std=c++17','-UNDEBUG',*fp.include_flags(),
                        str(fp.TESTS/(name+'.cpp')),'-o',str(binary)],env=fp.zig_env() if os.name=='nt' else None,check=True)
        subprocess.run([str(binary)],check=True,timeout=20)
print('PASS sky-layer sequence, target invalidation, managed mip bounds and lock failures')
