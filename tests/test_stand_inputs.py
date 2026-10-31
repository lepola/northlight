#!/usr/bin/env python3
# northlight-test: requires=zig,client manual
"""Dedicated Windows imported-context host; reads the client file, never executes it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--client',type=Path,default=fp.client_root()/'Wow.exe')
    args=parser.parse_args()
    if os.name!='nt':raise SystemExit('Use the Windows CPU host; no game or graphics runtime is involved.')
    args.output.mkdir(parents=True,exist_ok=False)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    paths=[Path(__file__),fp.TESTS/'test_stand_inputs.cpp',fp.src('stand_inputs.h'),fp.src('client_image.h'),
           fp.src('world_context.h'),fp.src('snapshot_store.h'),args.client.resolve(),fp.zig()]
    before={str(p.resolve()):sha(p) for p in paths}
    env=os.environ.copy()
    for key in ('TEMP','TMP','ZIG_LOCAL_CACHE_DIR','ZIG_GLOBAL_CACHE_DIR'):
        path=args.output/key.lower();path.mkdir();env[key]=str(path.resolve())
    records=[]
    try:
        for mode in ('O2','O0'):
            exe=args.output/(mode+'.exe')
            command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',
                     '-'+mode,*fp.test_include_flags(),str(fp.TESTS/'test_stand_inputs.cpp'),'-o',str(exe)]
            for stage,call in [('compile',command),('run',[str(exe.resolve()),str(args.client.resolve())])]:
                row={'mode':mode,'stage':stage,'command':call};records.append(row)
                (args.output/'commands.json').write_text(json.dumps(records,indent=2)+'\n')
                r=subprocess.run(call,cwd=fp.REPO,env=env,capture_output=True,text=True)
                row['exit_code']=r.returncode
                (args.output/(mode+'-'+stage+'.log')).write_text(r.stdout+r.stderr)
                if r.returncode:print(r.stdout+r.stderr,file=sys.stderr)
                r.check_returncode()
                if stage=='run':print(r.stdout,end='',flush=True)
    finally:
        unchanged=all(p.is_file() and sha(p)==h for name,h in before.items() for p in [Path(name)])
        result={'pass':len(records)==4 and all(r.get('exit_code')==0 for r in records),'commands':records,
                'inputs':before,'inputs_unchanged':unchanged,'client_executed':False,'game_launched':False,
                'runtime_values':'authored coherent snapshot, not a recorded game scene','gpu_used':False,'full_scene_fps_claimed':False}
        (args.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    assert unchanged,'Source or client file changed'
if __name__=='__main__':main()
