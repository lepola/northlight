#!/usr/bin/env python3
# northlight-test: requires=zig,client manual
"""Load the dedicated x86 DLL in a private CPU host; never create a graphics device."""
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

MODES=('valid','before-open','before-snapshot','wrong-thread','corrupt','order',
       'code-proof-valid','code-proof-repeat','code-proof-corrupt','code-proof-late-snapshot')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--dll',type=Path,required=True)
    parser.add_argument('--client',type=Path,default=fp.client_root()/'Wow.exe')
    parser.add_argument('--modes',nargs='+',choices=MODES,default=MODES)
    args=parser.parse_args()
    if os.name!='nt':raise SystemExit('This scenario requires the Windows x86 CPU host.')
    if b'NORTHLIGHT STAND REPLAY BUILD' not in args.dll.read_bytes():
        raise SystemExit('Only a dedicated stand DLL is accepted.')
    args.output.mkdir(parents=True,exist_ok=False)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    paths=[Path(__file__),Path(fp.__file__),fp.TESTS/'test_stand_api.cpp',args.dll.resolve(),args.client.resolve(),fp.zig(),
           fp.src('snapshot_archive.h'),fp.src('snapshot_store.h'),fp.src('client_code_observation.h'),fp.src('client_image.h')]
    before={str(p.resolve()):sha(p) for p in paths};records=[]
    env=os.environ.copy()
    for key in ('TEMP','TMP','ZIG_LOCAL_CACHE_DIR','ZIG_GLOBAL_CACHE_DIR'):
        path=args.output/key.lower();path.mkdir();env[key]=str(path.resolve())
    private_dll=args.output/'subject.dll';shutil.copyfile(args.dll,private_dll)
    assert sha(private_dll)==before[str(args.dll.resolve())]
    exe=args.output/'host.exe'
    command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
             *fp.test_include_flags(),str(fp.TESTS/'test_stand_api.cpp'),'-o',str(exe.resolve())]
    calls=[('compile',command)]+[(mode,[str(exe.resolve()),str(private_dll.resolve()),str(args.client.resolve()),mode])
        for mode in args.modes]
    try:
        for name,call in calls:
            row={'name':name,'command':call};records.append(row)
            (args.output/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
            result=subprocess.run(call,cwd=args.output,env=env,capture_output=True,text=True)
            row['exit_code']=result.returncode
            (args.output/(name+'.log')).write_text(result.stdout+result.stderr)
            if result.returncode:print(result.stdout+result.stderr,file=sys.stderr)
            result.check_returncode();print(result.stdout,end='',flush=True)
    finally:
        unchanged=all(Path(p).is_file() and sha(Path(p))==h for p,h in before.items())
        private_unchanged=private_dll.is_file() and sha(private_dll)==before[str(args.dll.resolve())]
        validation={'pass':len(records)==len(args.modes)+1 and all(r.get('exit_code')==0 for r in records) and unchanged and private_unchanged,
                    'commands':records,'inputs':before,'inputs_unchanged':unchanged,
                    'private_dll_unchanged':private_unchanged,
                    'available_modes':MODES,'selected_modes':args.modes,'expected_cases':len(args.modes),
                    'completed_cases':sum(r['name']!='compile' and r.get('exit_code')==0 for r in records),
                    'client_executed':False,'game_launched':False,'graphics_device_created':False,
                    'code_proof_observations':'authored raw-file samples; not loaded game code',
                    'authentic_loaded_code_capture':False,'whole_scene_replay':False,'full_scene_fps_claimed':False}
        (args.output/'validation.json').write_text(json.dumps(validation,indent=2)+'\n')
    assert unchanged,'Source, client or DLL input changed'
    assert private_unchanged,'Private DLL copy changed'

if __name__=='__main__':main()
