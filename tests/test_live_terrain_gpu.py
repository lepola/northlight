#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Live terrain upload (0.3.176 U1a/U1b/U1c): native fake-D3D test of live_terrain_gpu.h (the arena,
retirement and rollover cases, plus prepare()/write() against the 0.3.175 indices() concatenation on
twin caches), the fixed-chunk bitmap beside the set (test_fixed_chunk_bits.cpp), each plain and
ASan/UBSan, and a wiring audit of uploadLiveTerrain (counts instead of index vectors, the same resets
on the same paths, the bitmap filled at the mesh commit and dropped with the set). No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
w=fp.src('world_renderer.h').read_text();pr=fp.src('world_point_rendering.inl').read_text()
up=w[w.index('    bool uploadLiveTerrain(){'):w.index('    // Win32 wide-path read')]
reset='liveTerrainIndexCount=liveDirectionalIndexCount=0;liveIndexBase=0;liveTerrainChunks.clear();uploadedTerrain.clear();return true;'
release=w[w.index('    void releaseGPU(){'):]
release=release[:release.index('\n')]
checks={
 'no index vectors left: counts only':'liveTerrainIndices' not in w+pr and 'liveDirectionalIndices' not in w+pr
    and 'size_t liveTerrainIndexCount=0,liveDirectionalIndexCount=0;' in w,
 'S_FALSE and index-growth refusal keep the 0.3.175 reset':up.count(reset)==2
    and 'if(terrainResult==S_FALSE){' in up and 'if(!admitsGrowth("live-terrain-index-growth",ibCapacity)){\n                '+reset in up,
 'totals before the Lock, the same ring Lock size, lists written inside it, Unlock right after':
    'UINT ib=UINT((liveTerrainIndexCount+liveDirectionalIndexCount)*sizeof(uint32_t));' in up
    and 'if(!check(liveIndicesGPU->Lock(slot.offset,ib,&data,slot.flags),"live index upload"))return false;\n        liveTerrainGPU.write(static_cast<uint32_t*>(data));\n        if(!check(liveIndicesGPU->Unlock(),"live index unlock"))return false;' in up
    and up.index('liveTerrainGPU.prepare(')<up.index('->Lock('),
 'membership failure and the empty case as before':'},liveTerrainIndexCount,liveDirectionalIndexCount))return check(E_FAIL,"live terrain arena membership");' in up
    and 'if(!liveTerrainIndexCount){uploadedTerrain=frameTerrain;liveTerrainGeneration=meshGeneration;return true;}' in up,
 'readers use the counts':'if(liveTerrainIndexCount){' in pr and 'if(liveDirectionalIndexCount){' in w
    and 'UINT(liveIndexBase+liveTerrainIndexCount),UINT(liveDirectionalIndexCount/3)),"live terrain shadow")' in w,
 'bitmap filled from the committed set at the swap, dropped with the set':'std::swap(fixedTerrain,committedFixed);std::swap(uploadedStaticOwners,committedOwners);fixedTerrainBits.assign(fixedTerrain.get());' in w
    and 'fixedTerrain.reset();fixedTerrainBits.reset();' in release and w.count('fixedTerrainBits.assign(')==1,
 'bitmap only for the set it describes, the set otherwise':'const auto& fixed=fixedTerrainChunks();const bool bits=fixedTerrainBits.source()==&fixed;' in up
    and 'return bits?fixedTerrainBits.contains(c):fixed.count(c)!=0;' in up,
 'fixedTerrain stays the shared immutable set':'std::shared_ptr<const std::set<std::pair<int,int>>> fixedTerrain;' in w,
}
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-live-terrain-') as tmp:
    for source in ['test_live_terrain_gpu.cpp','test_fixed_chunk_bits.cpp']:
        for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
            exe=Path(tmp)/(Path(source).stem+'-'+label)
            subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(HERE/source),'-o',str(exe)],check=True)
            print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS live terrain upload: cache, prepare/write, bitmap and wiring')
