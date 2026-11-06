"""Reproduce the original dangling callback and verify module lifetime on reload."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser=argparse.ArgumentParser(__doc__)
parser.add_argument('--client',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
parser.add_argument('--baseline',type=Path,help='Optional first test DLL, expected to fail with switch=1.')
args=parser.parse_args();output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
exe=output/'dxvk_engine_name_reload.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2',
                str(fp.TESTS/'dxvk_engine_name_reload.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
variants=[('fixed',fp.RENDERER/'frd9.dll')]
if args.baseline:variants.insert(0,('baseline',args.baseline.resolve()))
report={'game_launched':False,'client_modified':False,'runs':[]}
for label,dll in variants:
    for switch in (0,1):
        folder=output/f'{label}-{switch}';folder.mkdir(exist_ok=True)
        shutil.copy2(exe,folder/exe.name);shutil.copy2(dll,folder/'d3d9.dll')
        runtime=folder/'renderer-backends/dxvk/dxvk_d3d9.dll';runtime.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(args.client/'renderer-backends/dxvk/dxvk_d3d9.dll',runtime)
        (folder/'northlight-renderer.ini').write_text(f'[Renderer]\nBackend=dxvk\nExperimentalHDR=0\nDxvkEngineNameWorkaround={switch}\n')
        (folder/'dxvk.conf').write_text('dxvk.allowFse = False\n')
        env=dict(os.environ,DXVK_LOG_PATH=str(folder),DXVK_CONFIG_FILE=str(folder/'dxvk.conf'),DXVK_CONFIG='')
        result=subprocess.run([str(folder/exe.name)],cwd=folder,env=env,text=True,capture_output=True,timeout=30)
        log=(folder/'northlight-renderer.log').read_text(errors='replace')
        entry={'variant':label,'switch':switch,'dll_sha256':hashlib.sha256(dll.read_bytes()).hexdigest(),
               'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr,
               'workaround_lines':[line for line in log.splitlines() if line.startswith('DXVK3 engine-name')]}
        report['runs'].append(entry);(output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
        print(label,switch,hex(result.returncode),result.stdout.strip(),flush=True)
        if label=='baseline' and switch:
            assert result.returncode==0xc0000005,entry
            assert 'getproc-already-intercepted' in log,entry
        else:
            assert result.returncode==0 and 'cycle=3 completed' in result.stdout,entry
            if switch:
                assert result.stdout.count('resident=1')==3 and 'reserved=1' not in result.stdout,entry
                assert log.count('instance original=DXVK replacement=Northlight result=0')==3,entry
print('PASS callback lifetime: '+str(output/'validation.json'))
