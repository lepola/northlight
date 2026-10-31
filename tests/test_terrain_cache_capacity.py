#!/usr/bin/env python3
# northlight-test: requires=cxx manual  (needs --baseline)
"""Reproduce the observed 778-draw cyclic terrain workload using real cache code.
CPU fake D3D only; compares the backed-up header, never runs the game.
"""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import argparse, ast, json, subprocess, tarfile, tempfile
from pathlib import Path
HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser()
parser.add_argument('--baseline', type=Path, required=True)
args = parser.parse_args()
nodes = ast.parse((HERE / 'test_terrain_snapshot.py').read_text()).body
values = {n.targets[0].id: ast.literal_eval(n.value) for n in nodes
          if isinstance(n, ast.Assign) and isinstance(n.targets[0], ast.Name)
          and n.targets[0].id in ('stub', 'harness')}
source = values['harness'].split('int main(){')[0] + r'''
static uint64_t identity(void*,bool index){return index?12:11;}
static uint64_t version(void*,bool index){return index?22:21;}
int main(){
 Device d;d.put(4,{-10200,-1180,3});d.put(5,{-10198,-1180,4});d.put(6,{-10200,-1178,5});
 FrameCache cache;cache.setIdentityProvider(identity);cache.setVersionProvider(version);
 std::shared_ptr<const MeshSnapshot> out;Diagnostics why;
 unsigned hits=0,locks=0;uint64_t bytes=0;
 for(unsigned cycle=0;cycle<3;++cycle){
  cache.clearFrame();hits=0;unsigned before=d.vb.locks+d.ib.locks;
  for(unsigned i=0;i<778;++i){
   assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,-3,7,3+i,1,1,d.view,out,&why));
   hits+=why.trackedCacheHit;
   assert(out->positions.size()==3&&out->indices.size()==3);
   assert(out->positions[0].z==3&&out->positions[1].z==4&&out->positions[2].z==5);
   for(unsigned j=0;j<3;++j)assert(out->indices[j]==j);
  }
  locks=d.vb.locks+d.ib.locks-before;bytes=cache.bytesRead();
  if(cycle){assert(hits==EXPECTED_HITS);assert((bytes==0)==(EXPECTED_HITS==778));}
 }
 assert(d.vb.refs==1&&d.ib.refs==1&&d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
 std::printf("{\"draws\":778,\"warm_hits\":%u,\"warm_locks\":%u,\"warm_read_bytes\":%llu,\"entries\":%zu}\n",hits,locks,(unsigned long long)bytes,cache.persistentEntries());
}
'''
report = {'game_launched': False, 'scope': '778 distinct immutable tracked draw contracts; exact vertex/index equality; fake D3D, not FPS', 'runs': []}
with tempfile.TemporaryDirectory(prefix='terrain-capacity-') as temp:
    root = Path(temp)
    with tarfile.open(args.baseline) as archive:
        names = [m for m in archive.getmembers() if m.name.endswith('/renderer/terrain_capture_bounds.h')]
        assert len(names) == 1
        baseline = archive.extractfile(names[0]).read()
    for name, header, expected in [('baseline-512', baseline, 0), ('current-bounded', fp.src('terrain_capture_bounds.h').read_bytes(), 778)]:
        directory = root / name
        directory.mkdir()
        (directory / 'terrain_capture_bounds.h').write_bytes(header)
        (directory / 'd3d9.h').write_text(values['stub'])
        (directory / 'test.cpp').write_text(source.replace('EXPECTED_HITS', str(expected)))
        for mode, flags in [('O2', ['-O2']), ('asan-ubsan', ['-O1', '-g', '-fsanitize=address,undefined'])]:
            subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wextra', '-Werror', *flags, '-I', str(directory), *fp.test_include_flags(), str(directory / 'test.cpp'), '-o', str(directory / 'test')], check=True)
            data = json.loads(subprocess.check_output([str(directory / 'test')], text=True))
            report['runs'].append(dict(version=name, mode=mode, **data))
print(json.dumps(report, indent=2))
(fp.output_dir() / 'terrain-capacity.json').write_text(json.dumps(report, indent=2) + '\n')
