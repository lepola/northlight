#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exact source-index sharing against the frozen pre-sharing terrain cache, same32MiB cap."""
import argparse
import ast
import json
from pathlib import Path
import re
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-ref', default='11f79a8')
args=parser.parse_args()
out=fp.out()/'terrain-source-indices'
out.mkdir(parents=True, exist_ok=True)
fixture=ast.parse((fp.TESTS/'test_terrain_snapshot.py').read_text())
strings={n.targets[0].id:ast.literal_eval(n.value) for n in fixture.body
         if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id in ('stub','harness')}
stub=strings['stub']
if sys.platform=='win32':
    stub=stub[:stub.index('using UINT')]+'#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);\n'+stub[stub.index('constexpr unsigned D3DUSAGE'):]
(out/'d3d9.h').write_text(stub)
relative=fp.src('terrain_capture_bounds.h').relative_to(fp.REPO).as_posix()
(out/'baseline.h').write_bytes(subprocess.check_output(['git','show',args.baseline_ref+':'+relative],cwd=fp.REPO))
# Reuse the existing exact geometry/lifetime oracle with4000draws instead of2800;
# the larger rotated working set exposes the source-index duplication under the same cap.
tree=ast.parse((fp.TESTS/'test_terrain_rotation_cache.py').read_text())
node=next(n.value for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='source' for t in n.targets))
prefix=ast.literal_eval(node.left.left)
body=ast.literal_eval(node.right).replace('2800','4000').replace('700','1000').replace('699-n','999-n').replace('snapshot->positions.size()*32','snapshot->positions.size()*20')
body=body.replace('assert(cache.readMeshShared(&d,D3DPT_TRIANGLELIST,INT(draw*290),0,290,0,512,d.view,snapshot));','if(!cache.readMeshShared(&d,D3DPT_TRIANGLELIST,INT(draw*290),0,290,0,512,d.view,snapshot)){std::printf("FAILED label=%s frame=%u draw=%u bytes=%llu\\n",label,frame,draw,(unsigned long long)cache.bytesRead());std::fflush(stdout);std::abort();}')
(out/'source.cpp').write_bytes((prefix+strings['harness'].split('int main(){')[0]+body).encode())
rows=[]
for label,header in [('baseline',(out/'baseline.h').read_bytes()),('current',fp.src('terrain_capture_bounds.h').read_bytes())]:
    directory=out/label;directory.mkdir(exist_ok=True)
    (directory/'terrain_capture_bounds.h').write_bytes(header)
    exe=directory/'source.exe'
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',
                    '-Wall','-Wextra','-Werror','-I',str(directory),'-I',str(out),*fp.test_include_flags(),
                    str(out/'source.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    result=subprocess.check_output([str(exe)],text=True);print(label,result,end='')
    row=next(dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in result.splitlines() if line.startswith('TERRAIN_ROTATION'))
    row['label']=label;rows.append(row)
assert len(rows)==2 and rows[0]['geometryDigest']==rows[1]['geometryDigest']
assert int(rows[0]['misses'])>0 and int(rows[1]['hits'])==8000
assert all(int(rows[1][key])==0 for key in ('misses','readBytes','replacementVertexBytes','evictions'))
assert all(int(row['retainedBytes'])<=32*1024*1024 and int(row['retainedEntries'])<=4096 for row in rows)
(out/'validation.json').write_bytes((json.dumps({'baseline':args.baseline_ref,'gameLaunched':False,'rows':rows},indent=2)+'\n').encode())
