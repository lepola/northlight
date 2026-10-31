#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig manual timing
"""Frozen material replay-content A/B; native D3D9 or DXVK, no game launch."""
import argparse, hashlib, json, math, os
from pathlib import Path
import re, shutil, statistics, struct, subprocess, sys

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--repo',type=Path,default=Path(__file__).resolve().parents[1])
p.add_argument('--baseline-ref',required=True,help='Full immutable 40-character baseline commit SHA')
p.add_argument('--fixture-root',type=Path,help='Root containing prepared model-materials-dxvk and material-msaa-release fixture folders')
p.add_argument('--backend',required=True,type=Path)
p.add_argument('--output',required=True,type=Path)
p.add_argument('--quick',action='store_true')
p.add_argument('--pairs',type=int,default=3)
p.add_argument('--reuse-exes',type=Path)
a=p.parse_args();sys.path.insert(0,str(a.repo.resolve()))
import northlight_paths as fp
BASELINE=a.baseline_ref.lower()
assert re.fullmatch(r'[0-9a-f]{40}',BASELINE),'Use a full immutable baseline commit SHA'
assert subprocess.check_output(['git','rev-parse',BASELINE+'^{commit}'],cwd=fp.REPO,text=True).strip()==BASELINE,'Baseline must identify that exact commit'
fixture_root=a.fixture_root.resolve(strict=True) if a.fixture_root else fp.out()
assert a.pairs>=2
out=a.output.resolve();assert not out.exists() or not any(out.iterdir()),'Use a new empty output directory'
out.mkdir(parents=True,exist_ok=True);backend=a.backend.resolve(strict=True)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
def sources():
    paths=[f for name in ('src','shaders','scripts','tests') for f in (fp.REPO/name).rglob('*') if f.is_file() and '__pycache__' not in f.parts]
    return {str(f.relative_to(fp.REPO)):sha(f) for f in sorted(paths)}
before=sources();runner_hash=sha(Path(__file__));fixture_origins={}
if a.reuse_exes:
    previous=json.loads((a.reuse_exes/'validation.json').read_text())
    assert previous.get('baseline_ref')==BASELINE,'Reused executable baseline does not match requested baseline'
    assert previous['source_after']==before and previous['runner_sha256']==runner_hash,'Reused executable source identity is stale'
    assert all(sha(a.reuse_exes/(label+'.exe'))==previous['executables'][label] for label in ('old','new')),'Reused executable changed'
compiler_path=fp.zig().resolve(strict=True)
compiler=dict(path=str(compiler_path),version=subprocess.check_output([str(compiler_path),'version'],text=True).strip(),sha256=sha(compiler_path))
if a.reuse_exes:assert previous.get('compiler')==compiler,'Reused executable compiler provenance does not match'
inputs=out/'inputs';inputs.mkdir();overrides=out/'baseline';overrides.mkdir()
for relative in ('src/water/auto_bump_renderer.h','src/water/auto_displacement_renderer.h','src/world/draw_snapshot.h','src/replay/replay_gpu_cache.h','src/replay/actor_deformation.h','src/replay/replay_bounds.h','src/generated/auto_bump_compiled_shaders.h'):
    (overrides/Path(relative).name).write_bytes(subprocess.check_output(['git','show',BASELINE+':'+relative],cwd=fp.REPO))
cases=[('model-weighted4',fixture_root/'model-materials-dxvk/3-weighted4-combiners_opaque','model4'),('terrain',fixture_root/'material-msaa-release/terrain',None)]
if not a.quick:cases += [('model-rigid',fixture_root/'model-materials-dxvk/3-rigid-combiners_opaque','model'),('wall',fixture_root/'material-msaa-release/wall','wall')]
fixtures=[]
for name,source,family in cases:
    folder=inputs/name;folder.mkdir()
    for f in source.iterdir():
        if f.name.startswith('source.fgr') or f.name in ('terrain-vs.bin','terrain-ps.bin','PreviewBase.bin'):shutil.copy2(f,folder/f.name)
    for f in ('source.fgr','terrain-vs.bin','terrain-ps.bin','PreviewBase.bin'):assert (folder/f).is_file(),(name,f)
    fixtures.append((name,folder,family));fixture_origins[name]=dict(source=str(source),family=family)
input_hashes={str(f.relative_to(out)):sha(f) for f in sorted(inputs.rglob('*')) if f.is_file()}
env=fp.zig_env();temp=out/'tmp';temp.mkdir();env.update(TEMP=str(temp),TMP=str(temp));commands=[];executables={}
for label in ('old','new'):
    exe=out/(label+'.exe')
    if a.reuse_exes:shutil.copy2(a.reuse_exes/(label+'.exe'),exe)
    else:
        command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-DNORTHLIGHT_MATERIAL_MIRROR','-DNORTHLIGHT_OBLIQUE_WIDTH=320','-DNORTHLIGHT_OBLIQUE_HEIGHT=200',*(['-DNORTHLIGHT_TEST_CAPTURE_BASELINE','-I'+str(overrides)] if label=='old' else ['-DNORTHLIGHT_TEST_REPLAY_CONTENT_CURRENT']),*fp.test_include_flags(),str(fp.TESTS/'auto_normal_oblique_gpu.cpp'),'-luser32','-luuid','-o',str(exe)]
        commands.append(command);subprocess.run(command,env=env,check=True,timeout=180)
        data=bytearray(exe.read_bytes());at=struct.unpack_from('<I',data,0x3c)[0]+22;struct.pack_into('<H',data,at,struct.unpack_from('<H',data,at)[0]|0x20);exe.write_bytes(data)
    executables[label]=exe
records=[];output_pairs=0;paired_files=0
for name,source,family in fixtures:
    for msaa in ((2,8) if a.quick else (0,2,4,8)):
        for changing in (False,True):
            for pair in range(a.pairs):
                folders={};rows={}
                for label in (('old','new') if pair%2==0 else ('new','old')):
                    folder=out/f'{name}-msaa{msaa}-changed{int(changing)}-pair{pair}-{label}';shutil.copytree(source,folder);folders[label]=folder
                    runenv={k:v for k,v in env.items() if not k.startswith('NORTHLIGHT_TEST_') and k!='NORTHLIGHT_EXPERIMENTAL_SHARED_POM'}
                    runenv.update(NORTHLIGHT_TEST_MSAA=str(msaa),NORTHLIGHT_TEST_IDENTICAL_REWRITE='1',NORTHLIGHT_TEST_DEFERRED_CAPTURE='1',NORTHLIGHT_TEST_MATERIAL_BATCH_TIMING='1',NORTHLIGHT_TEST_MATERIAL_BATCH_VALIDATE='1',NORTHLIGHT_TEST_CAPTURE_DRAWS='32',NORTHLIGHT_TEST_MESH_GRID='16')
                    if changing:runenv['NORTHLIGHT_TEST_REWRITE_CHANGED']='1'
                    command=[str(executables[label]),str(backend),str(folder)+'/', '12','.7',*([family] if family else [])];commands.append(command)
                    result=subprocess.run(command,cwd=out,env=runenv,capture_output=True,text=True,timeout=120);(folder/'run.log').write_bytes((result.stdout+result.stderr).encode())
                    assert result.returncode==0,(name,msaa,changing,label,result.stdout[-3000:],result.stderr[-1500:])
                    timing=re.search(r'MATERIAL_BATCH_TIMING .*gpuMs=([\d.]+) submitMs=([\d.]+)',result.stdout);counts=re.search(r'MATERIAL_IDENTICAL_REWRITE (.*)',result.stdout);assert timing and counts
                    values={k:int(v) for k,v in re.findall(r'(\w+)=(\d+)',counts[1])}
                    row=dict(fixture=name,msaa=msaa,changing=changing,pair=pair,label=label,gpu_ms=float(timing[1]),cpu_ms=float(timing[2]),**values);rows[label]=row;records.append(row);print(json.dumps(row),flush=True)
                for k in ('frames','draws','readBytes','changed'):assert rows['old'][k]==rows['new'][k],(k,rows)
                if msaa:
                    assert rows['old']['resident']==0 and rows['old']['up']>0,rows
                    if changing:assert rows['new']['resident']==0 and rows['new']['up']==rows['old']['up'],rows
                    else:assert rows['new']['resident']>0 and rows['new']['up']<rows['old']['up'] and rows['new']['verifiedHits']>0,rows
                old=sorted(folders['old'].glob('batch-*.fgr'));assert len(old)==3,(name,msaa,'Incomplete output')
                assert {f.name for f in old}=={f.name for f in folders['new'].glob('batch-*.fgr')}
                for f in old:assert f.read_bytes()==(folders['new']/f.name).read_bytes(),(name,msaa,changing,pair,f.name,'Pixel mismatch')
                output_pairs+=1;paired_files+=len(old)
after=sources();assert before==after,'Source changed during validation';assert runner_hash==sha(Path(__file__))
assert sha(compiler_path)==compiler['sha256'],'Compiler changed during validation'
assert input_hashes=={str(f.relative_to(out)):sha(f) for f in sorted(inputs.rglob('*')) if f.is_file()},'Inputs changed during validation'
summary={}
for changing in (False,True):
    group={}
    for label in ('old','new'):
        rows=[r for r in records if r['label']==label and r['changing']==changing and r['msaa']]
        group[label]={k:dict(median=statistics.median(r[k] for r in rows),p95=sorted(r[k] for r in rows)[math.ceil(len(rows)*.95)-1],maximum=max(r[k] for r in rows)) for k in ('cpu_ms','gpu_ms')}
    group['paired_cpu_percent']=[100*(1-next(r['cpu_ms'] for r in records if r['fixture']==old['fixture'] and r['msaa']==old['msaa'] and r['changing']==changing and r['pair']==old['pair'] and r['label']=='new')/old['cpu_ms']) for old in records if old['label']=='old' and old['changing']==changing and old['msaa']]
    summary['changing' if changing else 'identical']=group
report=dict(status='pass',baseline_ref=BASELINE,compiler=compiler,source_before=before,source_after=after,runner_sha256=runner_hash,backend=dict(path=str(backend),sha256=sha(backend)),baseline_headers={f.name:sha(f) for f in overrides.iterdir()},inputs=input_hashes,executables={k:sha(v) for k,v in executables.items()},commands=commands,fixture_origins=fixture_origins,output_pairs=output_pairs,bit_exact_paired_output_files=paired_files,records=records,summary=summary,limitations='Thirty-two repeated draws of one synthetic indexed grid per frame, with authored palettes, reused MPQ-authentic shader binaries and fixed texture inputs. Changing-byte controls alter the last UV each rewritten generation; they do not reproduce many distinct live draw meshes. Not a historical draw capture or whole-game FPS test. CPU samples are full submission averages over eight measured render calls after four warm calls; p95/max are tails of run means, not individual frame latency. Cold driver and source initialization are outside those averages. No game launch.')
(out/'validation.json').write_bytes((json.dumps(report,indent=2)+'\n').encode());print(json.dumps(dict(status='pass',output_pairs=output_pairs,bit_exact_paired_output_files=paired_files,summary=summary),indent=2),flush=True)
