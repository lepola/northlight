#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client,stormlib
"""Exact water MRT/HDR interaction; client shader bytes remain output artifacts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()
from world_scene_builder import Assets
from extract_signatures import variants,fnv1a64
from water_shader_patch import patch_mrt
from shader_compiler import Compiler
from windows_gpu_guard import processes

parser=argparse.ArgumentParser(__doc__)
parser.add_argument('--output',type=Path,default=fp.output_dir()/'hdr-water-transfer')
parser.add_argument('--benchmark',action='store_true')
parser.add_argument('--baseline-header',type=Path)
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
sha=lambda p:hashlib.sha256(Path(p).read_bytes()).hexdigest()
inputs=[fp.TESTS/'hdr_water_transfer_gpu.cpp',fp.TESTS/'hdr_water_transfer_fixture.hlsl',Path(__file__),fp.src('hdr_color.h'),fp.src('hdr_ui_patch.h'),fp.src('water_shader_patch.h'),fp.src('hdr_compiled_shaders.h')]
report={'pass':False,'game_launched':False,'client_modified':False,'controlled_workload_not_client_fps':True,'processes':processes(),'inputs':{str(p.relative_to(fp.REPO)):sha(p) for p in inputs},'runs':[]}
report['validation_scope']={'native':'Authored compiler-valid dual-output MRT; unchanged auxiliary output and independent HDR/alpha bounds at MSAA0. Production water derivatives are rejected by native CreatePixelShader before HDR. MSAA2 runs an unmodified binding control only.','dxvk2_dxvk3':'Exact client original versus production water derivative native-RGBA parity; bit-exact auxiliary mask plus numeric distance/coverage and independent HDR/alpha bounds at MSAA0/2.'}
def save(): (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
save();assets=Assets(archives='all')
try:
    path='shaders\\pixel\\ps_3_0\\psliquidwater.bls'
    native=next(code for index,_,code in variants(assets.read(path)) if index==0)
    derived,_=patch_mrt(native)
    report['client_shader']={'path':path,'index':0,'provider':assets.origin(path),'native_fnv':f'{fnv1a64(native):016x}','derived_fnv':f'{fnv1a64(derived):016x}','native_sha256':hashlib.sha256(native).hexdigest(),'derived_sha256':hashlib.sha256(derived).hexdigest()}
    assert fnv1a64(native)==0x7d4f078fa1876a09 and fnv1a64(derived)==0xe79938975a92b584
finally: assets.close()
(out/'native.bin').write_bytes(native);(out/'water-mrt.bin').write_bytes(derived)
compiler=Compiler(allow_compatible_binary=True)
for entry,target,name in [('WaterVS','vs_3_0','fixture-vs.bin'),('WaterPS','ps_3_0','fixture-ps.bin'),('WaterMRTPS','ps_3_0','fixture-mrt-ps.bin')]:
    compiler.compile(fp.TESTS/'hdr_water_transfer_fixture.hlsl',entry,target,out/name)

def build(exe,include=None):
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-return-type','-Wno-microsoft-exception-spec',*(['-I'+str(include)] if include else []),*fp.include_flags(),str(fp.TESTS/'hdr_water_transfer_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
exe=out/'hdr-water-transfer.exe';build(exe)
old_exe=None
if args.baseline_header:
    baseline=out/'baseline';baseline.mkdir(exist_ok=True)
    shutil.copyfile(fp.src('hdr_color.h'),baseline/'hdr_color.h');shutil.copyfile(args.baseline_header,baseline/'hdr_ui_patch.h')
    report['baseline_patch_sha256']=sha(baseline/'hdr_ui_patch.h');old_exe=out/'hdr-water-transfer-baseline.exe';build(old_exe,baseline)
backends=[('native',Path(os.environ['WINDIR'])/'SysWOW64/d3d9.dll'),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('dxvk3',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]
def run(binary,name,backend,samples,baseline=False,coverage=None,block=0):
    command=[str(binary),str(backend),str(samples),str(out/'fixture-vs.bin'),str(out/'fixture-ps.bin'),str(out/'native.bin'),str(int(baseline)),str(out/'fixture-mrt-ps.bin'),str(int(name=='native'))]
    if coverage:command.append(coverage)
    result=subprocess.run(command,cwd=out,text=True,capture_output=True,timeout=180)
    label=f'{name}-msaa{samples}-'+('baseline' if baseline else 'candidate')+(f'-{coverage}-block{block}' if coverage else '')
    (out/(label+'.log')).write_text(result.stdout+result.stderr)
    report['runs'].append({'name':label,'backend_sha256':sha(backend),'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr});save()
    print(label,result.stdout+result.stderr,end='',flush=True);result.check_returncode()
    expected='HDR_WATER_TIMING' if coverage else 'HDR_WATER_OLD_REJECTION_REPRODUCED' if baseline else 'HDR_WATER_NATIVE_MSAA_BINDING_CONTROL' if name=='native' and samples else 'HDR_WATER_TRANSFER_PASS'
    assert expected in result.stdout
if old_exe:run(old_exe,*backends[0],0,True)
for name,backend in backends:
    for samples in (0,2):run(exe,name,backend,samples)
if args.benchmark:
    assert old_exe,'--benchmark requires --baseline-header'
    report['benchmark_load']={'processes':processes()}
    for label,query in [('gpu',['--query-gpu=timestamp,name,utilization.gpu,memory.used,power.draw']),('gpu_processes',['--query-compute-apps=pid,process_name,used_memory'])]:
        load=subprocess.run(['nvidia-smi',*query,'--format=csv'],capture_output=True,text=True,check=True)
        report['benchmark_load'][label]=load.stdout
    (out/'benchmark-load.json').write_text(json.dumps(report['benchmark_load'],indent=2)+'\n');save()
    for name,backend in backends[1:]:
        for coverage in ('sparse','dense'):
            for block,baseline in enumerate((True,False,False,True)):
                run(old_exe if baseline else exe,name,backend,2,baseline,coverage,block)
    summary=[]
    for name,_ in backends[1:]:
        for coverage in ('sparse','dense'):
            groups=[[],[]];blocks=[]
            for block,baseline in enumerate((True,False,False,True)):
                label=f'{name}-msaa2-'+('baseline' if baseline else 'candidate')+f'-{coverage}-block{block}'
                record=next(r for r in report['runs'] if r['name']==label);values=[]
                for line in record['stdout'].splitlines():
                    if line.startswith('HDR_WATER_TIMING '):
                        row=dict(item.split('=',1) for item in line.split()[1:]);values.append(float(row['gpu_ms']))
                assert len(values)==8;groups[int(not baseline)].extend(values);blocks.append(statistics.median(values))
            summary.append({'backend':name,'coverage':coverage,'width':3840,'height':2160,'msaa':2,'draws':332,'source':'authentic client pixel shader plus production water MRT derivative; controlled geometry and constants; HDR replay segment only','samples_per_mode':16,'baseline_ms':{'median':statistics.median(groups[0]),'min':min(groups[0]),'max':max(groups[0])},'candidate_ms':{'median':statistics.median(groups[1]),'min':min(groups[1]),'max':max(groups[1])},'abba_block_medians_ms':blocks,'abba_mean_delta_ms':((blocks[1]-blocks[0])+(blocks[2]-blocks[3]))/2,'eliminated_per_frame':{'full_resolution_clears':332,'msaa_resolves':332,'composite_draws':332}})
    report['timing_summary']=summary
report['inputs_unchanged']=all(sha(fp.REPO/path)==digest for path,digest in report['inputs'].items())
report['pass']=report['inputs_unchanged'];save();assert report['pass']
