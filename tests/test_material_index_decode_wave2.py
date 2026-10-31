#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Focused index decode and remapping proof, including scalar fallback."""
import subprocess, sys, tempfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='material-index-decode-') as tmp:
    for flags in (['-O2'],['-O2','-mno-sse2']):
        exe=Path(tmp)/('index'+str(len(flags))+'.exe')
        cmd=[str(fp.zig()),'c++','-target','x86-windows-gnu',*flags,'-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(fp.TESTS/'material_index_decode_wave2.cpp'),'-o',str(exe)]
        subprocess.run(cmd,check=True,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        subprocess.run([str(exe)],check=True,creationflags=subprocess.BELOW_NORMAL_PRIORITY_CLASS)
