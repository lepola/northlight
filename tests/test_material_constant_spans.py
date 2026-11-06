#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Exact material dirty spans with the production byte comparator and paired timing."""
from pathlib import Path
import subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
for opt in ['2','0']:
    exe=out/('material-spans-O'+opt+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O'+opt,'-UNDEBUG','-Wall','-Wextra','-Werror',*fp.test_include_flags(),str(fp.TESTS/'test_material_constant_spans.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    p=subprocess.run([str(exe),*(['--check-only'] if opt=='0' else [])],check=True,text=True,capture_output=True)
    (out/('O'+opt+'.log')).write_text(p.stdout);print(p.stdout,end='')
