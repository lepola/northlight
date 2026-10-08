#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.175 (S2) terrain shadow selection: native model test of terrain_shadow_candidates.h (clang++,
plain and ASan/UBSan: the listed selection with the chunk bitmap equals the set rule, same order) and a
wiring audit of world_renderer.h (the terrain-outside-fixed list rebuilt at the mesh commit only, used
only for the batches and fixed set it was built from). No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
w=fp.src('world_renderer.h').read_text()
checks={
 'list built with the terrain list at the mesh commit, against the committed fixed set':'if(!fixed.count({batches[i].chunkX,batches[i].chunkY}))shadowTerrainList.push_back(uint32_t(i));' in w
    and 'terrainBatchListSize=batches.size();terrainListFixed=&fixed;terrainBatchListGeneration=meshGeneration;' in w and w.count('rebuildTerrainLists();')==1,
 'listed selection only for its generation, batches and fixed set; the set rule otherwise':'if(terrainBatchListGeneration==meshGeneration&&terrainBatchListSize==batches.size()&&terrainListFixed==&fixedTerrainChunks())\n                    terrainCandidates.prepareListed(batches,shadowTerrainList,fixedTerrainChunks(),liveTerrainChunks,captureSampled);\n                else terrainCandidates.prepare(batches,fixedTerrainChunks(),liveTerrainChunks,captureSampled);' in w, # 0.3.200 (jobs): inside terrainJob
 'live chunks: the set plus bitmap':'NorthlightTerrainCandidates::ChunkSet liveTerrainChunks;' in w,
}
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-terrain-candidates-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(HERE/'test_terrain_shadow_candidates.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS terrain shadow candidates: model and wiring')
