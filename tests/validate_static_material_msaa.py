"""POM admission, single-sample material replay, binding restoration and pressure recovery."""
from pathlib import Path
import argparse,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
p=argparse.ArgumentParser(__doc__);p.add_argument('--backend',type=Path,required=True);args=p.parse_args()
out=fp.out()/'static-material-msaa';out.mkdir(parents=True,exist_ok=True);exe=out/'static-material-msaa.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wno-microsoft-exception-spec',*fp.test_include_flags(),str(fp.TESTS/'parallax_msaa_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
run=subprocess.run([str(exe),str(args.backend.resolve()),str(fp.out()/'bump-recon')+'/'],cwd=out,capture_output=True,text=True,timeout=90)
(out/'gpu.log').write_text(run.stdout+run.stderr);print(run.stdout,flush=True)
assert run.returncode==0,run.stderr
assert run.stdout.count('STATIC_CAPTURE_MSAA_PASS')>=2
assert 'MATERIAL_PRESSURE_PASS' in run.stdout
