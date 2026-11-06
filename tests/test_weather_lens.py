#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Bounded coalescence, snow/water transfer, thermal occlusion and sparse geometry."""
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
for opt in ('0','2'):
    exe=out/('weather-lens-O'+opt+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O'+opt,'-UNDEBUG','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(fp.TESTS/'test_weather_lens.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    subprocess.run([str(exe)],check=True)
