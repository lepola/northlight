"""Small real-D3D9 overlay regression; never starts WoW or changes MPQs."""
from pathlib import Path
import argparse,json,shutil,subprocess,sys
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
import northlight_paths as fp
p=argparse.ArgumentParser();p.add_argument('--backend',required=True);p.add_argument('--compile-only',action='store_true');args=p.parse_args()
out=fp.out()/'displacement-overlay';out.mkdir(exist_ok=True)
tmp=out/'tmp';tmp.mkdir(exist_ok=True);env=fp.zig_env();env.update(TEMP=str(tmp),TMP=str(tmp))
exe=out/'overlay.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17',*fp.test_include_flags(),str(root/'tests/test_displacement_overlay.cpp'),'-luser32','-o',str(exe)],env=env,check=True)
if args.compile_only:print('DISPLACEMENT_OVERLAY_COMPILE_PASS');raise SystemExit()
results=[]
for model in ('2','3'):
    folder=out/('sm'+model);folder.mkdir(exist_ok=True)
    for fixture in (fp.out()/'oblique-cobble').iterdir():
        if fixture.name.startswith('source.fgr') or fixture.name=='PreviewBase.bin':shutil.copy2(fixture,folder/fixture.name)
    shutil.copy2(fp.out()/'bump-recon'/f'shaders_vertex_vs_{model}_0_terrain.bls-0.bin',folder/'terrain-vs.bin')
    run=subprocess.run([str(exe),str(Path(args.backend).resolve()),str(folder)+'/'],cwd=folder,env=env,capture_output=True,text=True,timeout=60)
    (folder/'gpu.log').write_text(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError(run.stdout+run.stderr)
    assert 'DISPLACEMENT_OVERLAY_PASS' in run.stdout
    results.append({'model':model,'result':run.stdout.strip().splitlines()[-1]});print(results[-1],flush=True)
(out/'validation.json').write_text(json.dumps(results,indent=2)+'\n')
