# northlight-test: requires=cxx
"""Execute the generated terrain handoff arithmetic; never starts a device/game."""
from pathlib import Path
import subprocess, sys, tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
with tempfile.TemporaryDirectory(prefix='northlight-terrain-coverage-') as temporary:
    exe=Path(temporary)/'test-coverage'
    subprocess.run(['c++','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(Path(__file__).with_suffix('.cpp')),str(fp.src('world_gi.cpp')),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
    source=fp.src('renderer.cpp').read_text(encoding='utf-8')
    start=source.index('template<class Draw> HRESULT terrainShadowDraw(')
    end=source.index('{',start);depth=1;end+=1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    (Path(temporary)/'terrain_shadow_draw.inc').write_text(source[start:end],encoding='utf-8')
    state=Path(temporary)/'test-state'
    subprocess.run(['c++','-std=c++17','-O2','-UNDEBUG','-I',temporary,str(Path(__file__).with_name('test_terrain_shadow_state.cpp')),'-o',str(state)],check=True)
    subprocess.run([str(state)],check=True)
