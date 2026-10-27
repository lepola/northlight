"""Isolated DXVK 3 off/on SDR/HDR smoke runs plus DXVK 2 exclusion."""
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
parser.add_argument('--with-hdr',action='store_true',help='Also test an integrated HDR build')
parser.add_argument('--client',type=Path,required=True)
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
exe=output/'dxvk_engine_name_gpu.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2',
                str(fp.TESTS/'dxvk_engine_name_gpu.cpp'),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
report={'renderer_sha256':sha(fp.RENDERER/'frd9.dll'),'client_modified':False,'game_launched':False,
        'monitor_colours_verified':False,'alt_tab_verified':False,'magnifier_verified':False,'runs':[]}
cases=[('dxvk',0,0,0),('dxvk',0,0,1),('dxvk',1,0,1),('dxvk2',1,0,1)]
if args.with_hdr:cases += [('dxvk',0,1,1),('dxvk',1,1,1),('dxvk2',1,1,1)]
baseline_wait=False
for backend,switch,hdr,wait in cases:
    name=f'{backend}-workaround{switch}-hdr{hdr}-wait{wait}';folder=output/name;folder.mkdir(exist_ok=True)
    shutil.copy2(exe,folder/exe.name);shutil.copy2(fp.RENDERER/'frd9.dll',folder/'d3d9.dll')
    runtime=folder/'renderer-backends'/backend/(backend+'_d3d9.dll');runtime.parent.mkdir(parents=True,exist_ok=True)
    shutil.copy2(args.client/'renderer-backends'/backend/runtime.name,runtime)
    (folder/'northlight-renderer.ini').write_text(f'[Renderer]\nBackend={backend}\nExperimentalHDR={hdr}\nDxvkEngineNameWorkaround={switch}\nDxvkPresentWaitWorkaround={wait}\nHDRPaperWhite=203\nHDRUIWhite=160\n')
    (folder/'northlight-quality.ini').write_text('[Quality]\nPreset=Performance\nCommandStream=1\nStreamFramesAhead=1\nStreamFrameSkip=0\nDiagnostics=0\nRenderProfile=0\nSpellLayer=0\nEquipmentLights=0\nGI=0\nActorShadows=0\nContactAO=0\nNameMask=0\nWeather=0\n')
    (folder/'dxvk.conf').write_text('dxvk.allowFse = False\n')
    env=dict(os.environ,DXVK_CONFIG_FILE=str(folder/'dxvk.conf'),DXVK_LOG_PATH=str(folder))
    env.pop('DXVK_HDR',None)
    # Prove that the active workaround overrides an inherited request for FSE.
    env['DXVK_CONFIG']='dxvk.allowFse = True;' if switch and backend=='dxvk' else ''
    result=subprocess.run([str(folder/exe.name)],cwd=folder,env=env,text=True,capture_output=True,timeout=60)
    northlight=(folder/'northlight-renderer.log').read_text(errors='replace') if (folder/'northlight-renderer.log').exists() else ''
    dxvk=(folder/'dxvk_engine_name_gpu_d3d9.log').read_text(errors='replace') if (folder/'dxvk_engine_name_gpu_d3d9.log').exists() else ''
    evidence={'name':name,'exit_code':result.returncode,'stdout':result.stdout,'stderr':result.stderr,
              'backend_sha256':sha(runtime),'workaround_lines':[line for line in northlight.splitlines() if line.startswith('DXVK3 engine-name')],
              'hdr_lines':[line for line in northlight.splitlines() if line.startswith('HDR experimental')],
              'fse_false':'info:    dxvk.allowFse = False' in dxvk and 'info:    dxvk.allowFse = True' not in dxvk,
              'scRGB_presented':'Color space:  VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT' in dxvk,
              'hdr10_presented':'Color space:  VK_COLOR_SPACE_HDR10_ST2084_EXT' in dxvk}
    report['runs'].append(evidence);(output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
    print(name,result.stdout.strip() or result.stderr.strip() or result.returncode,flush=True)
    assert result.returncode==0,evidence
    assert evidence['fse_false'],evidence
    changed='instance original=DXVK replacement=Northlight result=0' in northlight
    assert changed==bool(switch and backend=='dxvk'),evidence
    if backend=='dxvk' and 'info:  NVIDIA ' in dxvk:
        disabled='DXVK3 NVIDIA present-wait feature disabled type=1000248000' in northlight
        assert not disabled or wait,evidence
        enabled_section=dxvk.split('info:  Enabled extensions:',1)[1].split('info:  Enabled features:',1)[0]
        if not wait:baseline_wait='VK_KHR_present_wait\n' in enabled_section
        if wait and baseline_wait:assert disabled,evidence
        if wait:assert 'VK_KHR_present_wait\n' not in enabled_section,evidence
    assert (evidence['scRGB_presented'] or evidence['hdr10_presented'])==bool(hdr),evidence
    if hdr:assert 'requested=1 active=1' in northlight,evidence
print('PASS: '+str(output/'validation.json'))
