"""Production halo/veil replay against opaque walls, with an installed negative control."""
from pathlib import Path
import argparse, json, subprocess, sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p=argparse.ArgumentParser(__doc__)
p.add_argument('--baseline', type=Path, default=fp.TESTS/'fixtures/celestial-before/glare-before-CelestialHaloVisibilityPS.bin')
args=p.parse_args()
out=fp.out()/'glare-wall';out.mkdir(parents=True,exist_ok=True);exe=out/'glare-wall.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG',*fp.test_include_flags(),str(fp.TESTS/'glare_wall_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
report={'game_launched':False,'baseline':str(args.baseline.resolve()),'runs':[]}
for name,backend in [('native',Path('C:/Windows/SysWOW64/d3d9.dll')),('dxvk2',fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll'),('dxvk3',fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll')]:
    result=subprocess.run([str(exe),str(backend),str(args.baseline.resolve())],cwd=out,capture_output=True,text=True,timeout=60)
    (out/(name+'.log')).write_text(result.stdout+result.stderr)
    report['runs'].append({'backend':name,'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(name,result.returncode,result.stdout.strip().splitlines()[-1:] or result.stderr,flush=True)
    if result.returncode:raise SystemExit(result.stderr or result.stdout)
