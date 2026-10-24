#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Staged entry reclaim: execute the actual worker block and source lease lifecycle."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source=fp.src('world_renderer.h').read_text()
block=source.split('// BEGIN STAGED ENTRY RECLAIM:')[1].split('\n',1)[1].split('// END STAGED ENTRY RECLAIM')[0]
# Global trim and map/reset keep the original complete release operation.
assert 'workerMemoryTrim.exchange(false,std::memory_order_relaxed))resetPreparation()' in source
assert 'if(preparationMap!=r.map){resetPreparation();preparationMap=r.map;}' in source
assert 'auto resetPreparation=[&]{meshPreparation.reset();sceneSources.reset();localGeometry.reset();};' in source
with tempfile.TemporaryDirectory(prefix='scene-entry-') as folder:
    root=Path(folder);(root/'scene_entry_reclaim.inl').write_text(block,encoding='utf-8')
    for optimization in ['-O2','-O0']:
        exe=root/'test.exe'
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17',optimization,'-UNDEBUG','-Wall','-Wextra','-Werror',
                        '-I',str(root),*fp.test_include_flags(),str(fp.TESTS/'test_scene_entry_reclaim.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],env=fp.zig_env(),check=True)
        subprocess.run([str(exe),str(root/'data')],check=True)
