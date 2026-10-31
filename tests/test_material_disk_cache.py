#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Persistent ready-map files: restart, content/parameter identity, corruption and slow I/O."""
import subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir()/'material-disk-store';out.mkdir(parents=True,exist_ok=True)
exe=out/'store.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(fp.TESTS/'material_disk_store.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
# Each invocation is a new process; avoid an in-process cache masquerading as persistence.
import tempfile
with tempfile.TemporaryDirectory(prefix='maps-',dir=out) as folder:
    for mode in ['produce','consume','checks','consume']:
        r=subprocess.run([str(exe),folder,mode],capture_output=True,text=True,timeout=20)
        (out/(mode+'.log')).write_text(r.stdout+r.stderr,encoding='utf-8')
        assert r.returncode==0,r.stdout+r.stderr
        print(r.stdout.strip(),flush=True)
