#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client,stormlib
"""Frozen terrain capture differential; shared index read proofs keep the 32 MiB/8192 caps."""
import argparse
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import statistics
import struct
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path)
parser.add_argument('--benchmark',action='store_true')
parser.add_argument('--pairs',type=int,default=9)
args=parser.parse_args()
if args.pairs<1:parser.error('--pairs must be positive')
out=(args.output or fp.output_dir()).resolve();out.mkdir(parents=True,exist_ok=True)
sha=lambda data:hashlib.sha256(data).hexdigest()
baseline='bd412b50bd8de060a53f5ada10fc5533fbf81da1'
relative='src/world/terrain_capture_bounds.h'
original=subprocess.check_output(['git','show',baseline+':'+relative],cwd=fp.REPO)
assert sha(original)=='306b0d4547d4b8befa8d81b8c18330106cda845fd7def66fc6c36e4e903a00ae'
current=fp.src('terrain_capture_bounds.h').read_bytes()
fixture_source=fp.TESTS/'test_terrain_snapshot.py'
literals={n.targets[0].id:ast.literal_eval(n.value) for n in ast.parse(fixture_source.read_text()).body
          if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id in ('stub','harness')}
stub=literals['stub']
first=stub.index('using UINT');last=stub.index('constexpr unsigned D3DUSAGE')
stub=stub[:first]+'#include <windows.h>\nconstexpr HRESULT D3D_OK=0,D3DERR_INVALIDCALL=HRESULT(0x8876086cu);\n'+stub[last:]
(out/'d3d9.h').write_text(stub,encoding='utf-8')
(out/'terrain_index_reference.h').write_text(literals['harness'].split('int main(){')[0],encoding='utf-8')

# Geometry comes from the tester's client, with identities pinned like client_fixtures.
# No client bytes or generated geometry belong in the repository.
fp.use_source_modules()
from world_scene_builder import Assets,Builder,chunks
assets=Assets(archives='stock');builder=Builder(assets);payload=bytearray();count=0;sources=[]
try:
    for path,expected in (
        ('world\\maps\\azeroth\\azeroth_30_48.adt','a162b07d3989b750c145b7985d0699c01f380c0e40ed31fbabab4f162da0f9be'),
        ('world\\maps\\azeroth\\azeroth_31_49.adt','fb603c271720660707f55278d2ec25bd82e6c456083970aeeb0bb1d01d2e8e68')):
        raw=assets.read(path);assert sha(raw)==expected,(path,sha(raw))
        sources.append({'path':path,'sha256':expected,'archive':assets.origin(path)})
        for tag,data in chunks(raw):
            if tag!='MCNK':continue
            builder.reset();builder.terrain(data,[])
            if not builder.triangles:continue
            vertices=builder.vertices;triangles=builder.triangles
            payload+=struct.pack('<II',len(vertices),len(triangles)*3)
            payload+=b''.join(struct.pack('<3f',*p[:3]) for p in vertices)
            payload+=b''.join(struct.pack('<3H',*t[:3]) for t in triangles)
            count+=1
finally:assets.close()
fixture=struct.pack('<I',count)+payload
assert count==507 and sha(fixture)=='af62810b3619d340ddf7807860840a6e993e4948ba9512d89dd3e2e9cdcaa267'
geometry=out/'stock-terrain.bin';geometry.write_bytes(fixture)
environment=dict(os.environ,NORTHLIGHT_OUT=str(out/'runtime'),ZIG_GLOBAL_CACHE_DIR=str(out/'zig-cache'),TEMP=str(out/'temp'),TMP=str(out/'temp'))
Path(environment['TEMP']).mkdir(exist_ok=True)
compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu']
common=['-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror','-I',str(out),*fp.test_include_flags()]
source=fp.TESTS/'test_terrain_index_read_proofs.cpp'
inputs={str(p):sha(p.read_bytes()) for p in [source,fixture_source,Path(__file__),fp.src('terrain_capture_bounds.h')]}
runs=[];executables={}


def build(label,header,flags,optimized=False):
    directory=out/label;directory.mkdir(exist_ok=True)
    (directory/'terrain_capture_bounds.h').write_bytes(header)
    exe=directory/'oracle.exe'
    command=[*compiler,*flags,'-I',str(directory),*common,*(['-DINDEX_READ_CURRENT'] if optimized else []),str(source),'-o',str(exe)]
    subprocess.run(command,env=environment,check=True)
    return exe,command


def check_pair(mode,flags):
    results={}
    for label,header in [('baseline',original),('current',current)]:
        exe,command=build(label+'-'+mode,header,flags,label=='current')
        audit=out/(label+'-'+mode+'.audit')
        run=[str(exe),str(geometry),'checks',str(audit)]
        result=subprocess.run(run,env=environment,capture_output=True,text=True,timeout=120)
        (out/(label+'-'+mode+'.log')).write_text(result.stdout+result.stderr,encoding='utf-8')
        result.check_returncode();assert 'TERRAIN_INDEX_READ_PASS chunks=507 ' in result.stdout
        print(mode,label,result.stdout.strip(),flush=True)
        digest=re.search(r'TERRAIN_INDEX_READ_PASS chunks=507 digest=(\w+)',result.stdout)[1]
        results[label]=(audit.read_bytes(),digest)
        runs.append({'mode':mode,'label':label,'compile':command,'run':run,'stdout':result.stdout,
                     'audit_sha256':sha(results[label][0]),'exe_sha256':sha(exe.read_bytes())})
        if mode=='O2':executables[label]=exe
    assert results['baseline']==results['current'],'Frozen/current geometry, bounds, source ownership or pressure digest differs'


check_pair('O2',['-O2'])
probe=out/'sanitizer-probe.cpp';probe.write_text('#include <cstdlib>\nint main(){void* p=std::malloc(4);std::free(p);}\n')
sanitizers=[]
for name,flag in [('ASan-UBSan','address,undefined'),('UBSan','undefined')]:
    flags=['-O2','-g','-fsanitize='+flag,'-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    target=out/(name+'-probe.exe');command=[*compiler,*flags,str(probe),'-o',str(target)]
    result=subprocess.run(command,env=environment,capture_output=True,text=True)
    diagnostic=result.stdout+result.stderr;supported=result.returncode==0
    if supported:
        result=subprocess.run([str(target)],env=environment,capture_output=True,text=True,timeout=30)
        supported=result.returncode==0;diagnostic+=result.stdout+result.stderr
    sanitizers.append({'mode':name,'available':supported,'command':command,'diagnostic':diagnostic})
    if supported:check_pair(name,flags);break

# Each intentionally broken source must fail the same runnable oracle.
negative=[]
for name,guard,occurrences in [('stale-index-generation','lastIndexRead.revision==before.indexVersion&&',1),
                               ('wrong-index-range','lastIndexRead.offset==indexOffset&&',1),
                               ('wrong-index-topology','indexPlan->topology==topology&&',2)]:
    assert current.count(guard.encode())==occurrences
    mutant=current.replace(guard.encode(),b'',1)
    exe,command=build(name,mutant,['-O2'],True)
    run=[str(exe),str(geometry),'checks',str(out/(name+'.audit'))]
    result=subprocess.run(run,env=environment,capture_output=True,text=True,timeout=120)
    assert result.returncode!=0,name+' escaped the oracle'
    negative.append({'name':name,'compile':command,'run':run,'returncode':result.returncode,'stdout':result.stdout,'stderr':result.stderr})

rows=[];summary={}
if args.benchmark:
    for pair in range(args.pairs+2):
        for workload in ('cold','unique','warm'):
            for label in (('baseline','current') if pair%2==0 else ('current','baseline')):
                text=subprocess.check_output([str(executables[label]),str(geometry),workload],env=environment,text=True)
                row=dict(re.findall(r'(\w+)=([^\s]+)',text));row.update(label=label,pair=pair-2,warmup=pair<2);rows.append(row)
    for workload in ('cold','unique','warm'):
        medians={label:statistics.median(float(row['ms'])/30 for row in rows if row['mode']==workload and row['label']==label and not row['warmup']) for label in ('baseline','current')}
        summary[workload]={**medians,'saved_ms_per_700_draws':medians['baseline']-medians['current'],'ratio':medians['current']/medians['baseline']}
        assert len({row['digest'] for row in rows if row['mode']==workload})==1
assert all(sha(Path(p).read_bytes())==value for p,value in inputs.items()),'Inputs changed during validation'
report={'baseline':baseline,'baseline_sha256':sha(original),'source_sha256':sha(current),'inputs':inputs,'runs':runs,'sanitizers':sanitizers,
        'fixture':{'sha256':sha(fixture),'chunks':count,'sources':sources},'negative_controls':negative,'timings_claimed':args.benchmark,'rows':rows,'summary':summary,
        'limits':{'MiB':32,'entries':8192},'game_launched':False,'GPU_used':False,'limitations':['Fake D3D resources use authentic stock ADT geometry; these are CPU timings, not a live native draw-range trace.','Native index-range reuse frequency is not present in the frozen game log.']}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary,indent=2))
print('PASS exact frozen capture/ownership, 507 stock chunks, 16/32-bit indices, bounds, resets, mutations, byte/count pressure and negative controls')
