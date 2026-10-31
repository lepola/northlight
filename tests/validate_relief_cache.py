"""Bounded cache eviction at the logged 715 MiB world-loading stall, without disabling relief."""
from pathlib import Path
import argparse,json,subprocess,sys
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root));import northlight_paths as fp
p=argparse.ArgumentParser();p.add_argument('--backend',required=True);a=p.parse_args()
name='native' if 'windows' in a.backend.lower() else 'dxvk';out=fp.out()/('relief-cache-'+name);out.mkdir(exist_ok=True)
env=fp.zig_env();env.update(TEMP=str(fp.out()/'tmp'),TMP=str(fp.out()/'tmp'))
records=[]
for name in ['test_auto_normal_memory','perf_top5_cache','perf_displacement_cache','parallax_msaa_gpu']:
    exe=out/(name+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec',*fp.test_include_flags(),str(root/'tests'/(name+'.cpp')),'-luser32','-o',str(exe)],env=env,check=True)
    args=[] if name=='test_auto_normal_memory' else [a.backend]+([str(fp.out()/('oblique-cobble' if name=='perf_displacement_cache' else 'bump-recon'))+'/'] if name in ['perf_displacement_cache','parallax_msaa_gpu'] else [])
    run=subprocess.run([str(exe),*args],cwd=out,env=env,text=True,capture_output=True,timeout=120)
    (out/(name+'.log')).write_text(run.stdout+run.stderr);assert run.returncode==0,run.stdout+run.stderr
    records.append({'test':name,'output':run.stdout});print(run.stdout,flush=True)
(out/'validation.json').write_text(json.dumps(records,indent=2)+'\n')
