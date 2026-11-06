#!/usr/bin/env python3
# northlight-test: requires=windows-gpu,zig,client
"""Independent scalar PCF/PCSS, current-pose, crop, and production volume endpoints."""
from pathlib import Path
import argparse,json,os,shutil,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--backend',type=Path)
parser.add_argument('--output',type=Path)
parser.add_argument('--timing',action='store_true')
args=parser.parse_args()
out=(args.output or fp.output_dir()).resolve();out.mkdir(parents=True,exist_ok=True)
backend=args.backend or Path(os.environ.get('NORTHLIGHT_TEST_D3D9',str(fp.client_root()/'renderer-backends/dxvk2/dxvk2_d3d9.dll')))
fp.use_source_modules()
from shader_compiler import Compiler
compiler=Compiler(allow_compatible_binary=True)
probe=out/'numeric-probe';probe.mkdir(exist_ok=True)
for name in ('world_effects.hlsl','auto_bump_normal.hlsl','shadow_transition.hlsl'):shutil.copy2(fp.SHADERS/name,probe/name)
probe_source=probe/'shadow_transition_numeric_probe.hlsl'
probe_source.write_text('#include "shadow_transition.hlsl"\n'+(fp.TESTS/'shadow_transition_numeric_probe.hlsl').read_text(),encoding='utf-8',newline='\n')
for entry in ('TransitionWorldPoint','TransitionCurrentPoints','TransitionPreviousPoints','TransitionPreviousCoverage','TransitionCurrentQuery','TransitionPreviousQuery'):compiler.compile(probe_source,entry,'ps_3_0',out/(entry+'.bin'))
exe=out/'shadow-transition-gpu.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-ffp-contract=off','-Wno-return-type',*fp.test_include_flags(),str(fp.TESTS/'shadow_transition_gpu.cpp'),str(fp.src('world_gi.cpp')),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
run=subprocess.run([str(exe),str(fp.COMPILED),str(backend),str(out/'result.json'),str(out),*(['--timing'] if args.timing else [])],cwd=out,env=fp.zig_env(),capture_output=True,text=True,timeout=120)
(out/'gpu.log').write_text(run.stdout+run.stderr,encoding='utf-8');print(run.stdout,end='',flush=True)
assert run.returncode==0,run.stderr
report=json.loads((out/'result.json').read_text())
expected={'failures':0,'surface_cases':210,'unequal_near_far_epoch_cases':42,'scalar_checks':161280,'volume_checks':36864,'crop_checks':7340032,'dynamic_redirect_checks':2097152,'sky_water_skip_checks':3072,'lifted_border_negative_controls':16,'production_fp16_visibility_endpoint_error':0}
for key,value in expected.items():assert report[key]==value,(key,report[key],value)
assert report['scalar_max_error']<=3e-5 and report['endpoint_max_error']<=3e-5
print('PASS static direction transition PCF/PCSS and production volume oracles')
