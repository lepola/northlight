# northlight-test: requires=windows-gpu,zig manual
"""Check periodic EVENT flushing on specified real backends in a hidden window."""
import argparse, hashlib, json, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT))
import northlight_paths as fp
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--backend',action='append',required=True)
args=parser.parse_args();out=args.output.resolve();out.mkdir(parents=True,exist_ok=False)
assert 'wow.exe' not in subprocess.check_output(['tasklist','/fi','IMAGENAME eq wow.exe','/fo','csv','/nh'],text=True).lower()
source=Path(__file__).with_name('event_flush_gpu.cpp');exe=out/'event-flush.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(source),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
report={'source_sha256':hashlib.sha256(source.read_bytes()).hexdigest(),'query_poll_sha256':hashlib.sha256(fp.src('query_poll.h').read_bytes()).hexdigest(),'game_launched':False,'runs':[]}
for i,backend in enumerate(args.backend):
    run=subprocess.run([str(exe),backend],cwd=out,capture_output=True,text=True,timeout=120)
    (out/f'{i}.log').write_text(run.stdout+run.stderr)
    assert run.returncode==0 and 'EVENT_FLUSH_GPU_PASS' in run.stdout,run.stdout+run.stderr
    report['runs'].append({'backend':backend,'sha256':hashlib.sha256(Path(backend).read_bytes()).hexdigest(),'result':run.stdout.strip()});print(backend,run.stdout,flush=True)
(out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
