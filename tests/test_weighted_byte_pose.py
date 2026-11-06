#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client,stormlib
"""Exact byte-index weighted-pose fallback and paired CPU bounds benchmark."""
import subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures
out=fp.output_dir()/'weighted-byte-pose';out.mkdir(parents=True,exist_ok=True)
fixtures=[client_fixtures.four_bone_vs3(),client_fixtures.four_bone_body_vs3()]
for opt in ['-O2','-O0']:
 exe=out/('weighted-byte-pose'+opt+'.exe')
 subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu',opt,'-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(fp.TESTS/'weighted_byte_pose.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
 subprocess.run([str(exe),*map(str,fixtures)],check=True)
