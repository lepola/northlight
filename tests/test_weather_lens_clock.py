#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""CPU-only animation speed/continuity checks at 30..240 FPS."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
for opt in ('0','2'):
    exe=out/('lens-clock-O'+opt+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O'+opt,'-UNDEBUG',*fp.test_include_flags(),str(fp.TESTS/'test_weather_lens_clock.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    subprocess.run([str(exe)],check=True)
