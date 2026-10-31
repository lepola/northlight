#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""CPU-only x86 memory inventories: real BVH/mesh ownership and fake D3D descriptions."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source=fp.src('world_renderer.h').read_text()
for stage in ['before-entry-reclaim','before-load','after-bvh','after-terrain-load','after-shadow-plan','after-pages','after-seal']:
    assert f'profileMemory("{stage}"' in source,stage
assert 'scopeTotalsNotAdditive=1' in source and 'residentVramMeasured=0' in source
assert 'now-resourceMemoryProfileAt<10000' in source and 'started-memoryFootprintReportAt>=10000' in source
assert 'memoryProfileMs-previousProfileMs' in source # diagnostic work is excluded from algorithm phase times
panel=fp.src('settings_panel.cpp').read_text()
assert 'case Page::Diagnostics:memoryDiagnostics();' in panel
assert 'M::store().tryRead(memorySnapshot)' in panel and 'now-memoryReadAt>=1000' in panel
for forbidden in ['GetLevelDesc','GetRenderTargetData','VirtualQuery','GlobalMemoryStatusEx','accountMemory(']:assert forbidden not in panel,forbidden
# Admission policy is not modified by diagnostic counters.
policy=fp.src('geometry_memory.h').read_text()
for setting in ['ProcessReserve=512*MiB','EntryHeadroom=256*MiB','InBuildHeadroom=128*MiB']:
    assert setting in policy
with tempfile.TemporaryDirectory(prefix='memory-footprint-') as folder:
    exe=Path(folder)/'test.exe'
    for optimization in ['-O2','-O0']:
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17',optimization,'-UNDEBUG','-Wall','-Wextra','-Werror',
                        *fp.test_include_flags(),str(fp.TESTS/'test_memory_footprint.cpp'),str(fp.src('world_gi.cpp')),str(fp.src('world_mesh_plan.cpp')),'-o',str(exe)],env=fp.zig_env(),check=True)
        subprocess.run([str(exe)],check=True)
