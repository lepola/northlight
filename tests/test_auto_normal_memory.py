#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Check compressed mip accounting and the adaptive bounded material-cache capacity."""
from pathlib import Path
import subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True);exe=out/'auto-normal-memory.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
                *fp.include_flags(),str(Path(__file__).with_suffix('.cpp')),'-o',str(exe)],env=fp.zig_env(),check=True)
subprocess.run([str(exe)],check=True)
