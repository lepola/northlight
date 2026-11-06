# northlight-test: requires=windows-gpu,zig
"""Run the x86 Windows import/ABI oracle; does not install or launch WoW."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

exe=fp.output_dir()/'dxvk_engine_name_test.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2',
                *fp.include_flags(),str(fp.TESTS/'test_dxvk_engine_name.cpp'),
                '-o',str(exe)],env=fp.zig_env(),check=True)
subprocess.run([str(exe)],check=True,timeout=30)
