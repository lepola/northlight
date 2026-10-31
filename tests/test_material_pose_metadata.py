#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client,stormlib
"""Exact CPU pose/ownership oracle with generated wrappers; paired timing is opt-in."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess
import sys
sys.path[:0]=[str(Path(__file__).resolve().parents[1]),str(Path(__file__).resolve().parent)]
import northlight_paths as fp
import client_fixtures

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path)
parser.add_argument('--benchmark',action='store_true',help='also run paired CPU timing after correctness')
parser.add_argument('--pairs',type=int,default=9)
args=parser.parse_args()
if args.pairs<1:parser.error('--pairs must be positive')
out=(args.output or fp.output_dir()/'material-pose-metadata').resolve()
out.mkdir(parents=True,exist_ok=True)
def sha(data):return hashlib.sha256(data).hexdigest()

# Freeze the pre-optimization class instead of editing the implementation under test.
baseline='7643ba183488c227806267ed0dcd07f184ff41b3'
baseline_path='src/water/auto_displacement_renderer.h'
baseline_bytes=subprocess.check_output(['git','show',baseline+':'+baseline_path],cwd=fp.REPO)
assert sha(baseline_bytes)=='ff058d985901a68e70513736fbf15e9f372531b978bc3c87f3ffde74dc3bc807','Frozen control source changed'
original=baseline_bytes.decode('utf-8')
assert 'poseMetadata' not in original
# Expose the baseline's complete-key calculation through the comparison API.
old_call='poseEnvelopes.calculate(program,*source,source,elements,count,constants,identity,envelopeBudget,result)'
assert original.count(old_call)==1
adapter='''    NorthlightReplayBounds::Status poseEnvelope(const Program& program,const std::shared_ptr<const Mesh>& source,
        const D3DVERTEXELEMENT9* elements,UINT count,const float* bank,NorthlightReplayBounds::Bounds& result,
        IDirect3DVertexShader9*,IDirect3DVertexDeclaration9*){
        const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
        return '''+old_call.replace(',constants,',',bank,')+''';
    }
'''
marker='    bool distantPoseBounds('
assert original.count(marker)==1
control=original.replace(marker,adapter+marker,1)
fixture_source=fp.TESTS/'displacement_state_reuse.cpp'
reference=fixture_source.read_text(encoding='utf-8')
assert reference.count('int main(){')==1 and reference.count('struct DisplacementStateTestAccess {')==1
reference=reference.split('int main(){')[0]
reference=reference.replace('struct DisplacementStateTestAccess {','struct DisplacementStateTestAccess {\n    static void poseMetadataChecks(const char* path);\n    template<class G> static double poseMetadataTiming(const NorthlightActorDeformation::Program& p,unsigned draws,bool rotating);')
source=fp.src('auto_displacement_renderer.h').read_text(encoding='utf-8')
(out/'pose_metadata_control.h').write_bytes(control.encode())
(out/'pose_metadata_reference.h').write_bytes(reference.encode())

environment=dict(os.environ,NORTHLIGHT_OUT=str(out/'runtime'),ZIG_GLOBAL_CACHE_DIR=str(out/'zig-cache'),TEMP=str(out/'temp'),TMP=str(out/'temp'))
Path(environment['TEMP']).mkdir(exist_ok=True)
# client_fixtures checks the stock shader digest; keep its bytes in private output.
os.environ['NORTHLIGHT_OUT']=environment['NORTHLIGHT_OUT']
fixture=client_fixtures.four_bone_vs3()
compiler=[str(fp.zig()),'c++','-target','x86-windows-gnu']
common=['-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-I',str(out),*fp.test_include_flags()]
watched=[p for p in fp.SRC.rglob('*') if p.is_file()]+[fixture_source,fp.TESTS/'test_material_pose_metadata.cpp',fp.TESTS/'test_replay_bounds_metadata.cpp',Path(__file__)]
inputs={str(p.resolve()):sha(p.read_bytes()) for p in watched}
runs=[]
sanitizers=[]


def run_oracles(label,flags):
    pose=None
    for name,filename,arguments,expected in (
        ('pose','test_material_pose_metadata.cpp',['checks',str(fixture)],'MATERIAL_POSE_METADATA_PASS comparisons=2400 '),
        ('ownership','test_replay_bounds_metadata.cpp',[],'metadata cache: identities, hit reuse,')):
        exe=out/f'{name}-{label}.exe'
        command=[*compiler,*flags,*common,str(fp.TESTS/filename),'-luser32','-luuid','-o',str(exe)]
        subprocess.run(command,env=environment,check=True)
        result=subprocess.run([str(exe),*arguments],env=environment,capture_output=True,text=True,timeout=120)
        (out/f'{name}-{label}.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        result.check_returncode()
        assert expected in result.stdout,result.stdout+result.stderr
        runs.append({'name':name,'mode':label,'command':command,'run_command':[str(exe),*arguments],
                     'exe_sha256':sha(exe.read_bytes()),'stdout':result.stdout,'stderr':result.stderr})
        print(label,result.stdout.strip(),flush=True)
        if name=='pose':pose=exe
    return pose


exe=run_oracles('O2',['-O2'])
# Probe compiler/runtime availability; a supported sanitizer's oracle failure is fatal.
probe=out/'sanitizer-probe.cpp'
probe.write_text('#include <cstdlib>\nint main(){void* p=std::malloc(4);std::free(p);}\n',encoding='utf-8')
for label,flag in [('ASan-UBSan','address,undefined'),('UBSan','undefined')]:
    flags=['-O2','-g','-fsanitize='+flag,'-fno-sanitize-recover=all','-fno-omit-frame-pointer']
    target=out/(label+'-probe.exe')
    command=[*compiler,*flags,str(probe),'-o',str(target)]
    result=subprocess.run(command,env=environment,capture_output=True,text=True)
    diagnostic=result.stdout+result.stderr
    supported=result.returncode==0
    if supported:
        result=subprocess.run([str(target)],env=environment,capture_output=True,text=True,timeout=30)
        supported=result.returncode==0
        diagnostic+=result.stdout+result.stderr
    sanitizers.append({'mode':label,'available':supported,'command':command,'diagnostic':diagnostic})
    (out/f'{label}-probe.log').write_text(diagnostic,encoding='utf-8')
    if supported:
        run_oracles(label,flags)
        break
    print('SKIP',label,'compiler/runtime unavailable; see',out/f'{label}-probe.log',flush=True)

rows=[]
for block in range(args.pairs+2 if args.benchmark else 0):
    for workload,draws in (('warm',24),('warm',192),('warm',576),('rotating',192)):
        for label in (('prepared','uncached') if block%2 else ('uncached','prepared')):
            line=subprocess.check_output([str(exe),label,str(fixture),str(draws),workload],env=environment,text=True)
            row=json.loads(line)
            row.update(block=block-2,warmup=block<2)
            rows.append(row)
summary={}
for workload,draws in ((('warm',24),('warm',192),('warm',576),('rotating',192)) if args.benchmark else ()):
    pair={label:statistics.median(r['ms_per_frame'] for r in rows if not r['warmup'] and r['draws']==draws and r['workload']==workload and r['variant']==label) for label in ('prepared','uncached')}
    summary[workload+'-'+str(draws)]={**pair,'ratio':pair['prepared']/pair['uncached'],'saved_ms':pair['uncached']-pair['prepared']}
assert all(sha(Path(p).read_bytes())==digest for p,digest in inputs.items()),'Oracle inputs changed during validation'
report={'source_sha256':sha(source.encode()),'control_sha256':sha(control.encode()),'reference_sha256':sha(reference.encode()),'fixture_sha256':sha(fixture.read_bytes()),
        'baseline':baseline,'baseline_path':baseline_path,'baseline_sha256':sha(baseline_bytes),'inputs':inputs,'inputs_unchanged':True,
        'runs':runs,'sanitizers':sanitizers,'game_launched':False,'GPU_used':False,'quality_parameters_changed':False,
        'timings_claimed':args.benchmark,'pairs':args.pairs if args.benchmark else 0,'rows':rows,'summary':summary}
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(summary,indent=2))
