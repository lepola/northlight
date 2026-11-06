# northlight-test: requires=windows-gpu,zig manual
"""Check HDR COUNT2 admission and optionally time two 4K compatibility draws."""
import argparse, hashlib, json, os, subprocess, sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--backend',type=Path,action='append',required=True)
parser.add_argument('--benchmark',action='store_true')
args=parser.parse_args();out=args.output.resolve()
if fp.is_client(out):parser.error('Use an isolated test output directory.')
assert 'wow.exe' not in subprocess.check_output(['tasklist','/fi','IMAGENAME eq wow.exe','/fo','csv','/nh'],text=True).lower()
out.mkdir(parents=True,exist_ok=False)
source=Path(__file__).with_name('hdr_texture_transform_gpu.cpp');exe=out/'hdr-transform.exe'
run=subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-return-type',*fp.test_include_flags(),str(source),'-luser32','-o',str(exe)],env=fp.zig_env(),capture_output=True,text=True)
(out/'compile.log').write_text(run.stdout+run.stderr);assert run.returncode==0,run.stderr
report={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'hdr_color_sha256':hashlib.sha256(fp.src('hdr_color.h').read_bytes()).hexdigest(),'game_launched':False,'runs':[]}
for i,backend in enumerate(args.backend):
    backend=backend.resolve()
    run=subprocess.run([str(exe),str(backend)]+(['benchmark'] if args.benchmark else []),cwd=out,env=dict(os.environ,DXVK_LOG_PATH=str(out)),capture_output=True,text=True,timeout=120)
    (out/f'{i}.log').write_text(run.stdout+run.stderr)
    assert run.returncode==0 and 'HDR_TEXTURE_TRANSFORM_PASS' in run.stdout,run.stdout+run.stderr
    result=[line for line in run.stdout.splitlines() if line.startswith('HDR_')]
    report['runs'].append({'backend':str(backend),'sha256':hashlib.sha256(backend.read_bytes()).hexdigest(),'result':result});print(backend,*result,sep='\n',flush=True)
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
