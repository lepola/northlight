# northlight-test: requires=cxx
"""Exercise the real shelter worker against floor and roof BVHs, without D3D or game."""
from pathlib import Path
import subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='northlight-shelter-') as tmp:
    exe=Path(tmp)/'shelter.exe'
    subprocess.run(['clang++','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),str(fp.src('world_gi.cpp')),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True,timeout=10)
