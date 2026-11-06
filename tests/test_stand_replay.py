#!/usr/bin/env python3
# northlight-test: requires=cxx,zig,client manual
"""Run only imported-context cases on the actual stream/replayer with a fake D3D target."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0,str(fp.TESTS))
import test_command_stream as stream

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--client',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True,exist_ok=False)
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    inputs=[Path(__file__),args.client,fp.TESTS/'test_stand_replay.cpp',fp.TESTS/'test_command_stream.cpp',
            fp.TESTS/'test_command_stream.py',fp.TESTS/'command_stream_targets.h',fp.TESTS/'command_stream_scenarios.h',
            *[p for p in fp.src('stream_device.h').parent.iterdir() if p.suffix in ('.h','.inl')],fp.src('client_image.h')]
    before={str(p.resolve()):sha(p) for p in inputs};records=[]
    (args.output/'d3d9_stub.h').write_text('#ifndef FALSE\n#define FALSE 0\n#endif\n'+stream.stub())
    (args.output/'cs_generated.inc').write_text(stream.generated_cpp())
    env=os.environ.copy();env['PYTHONDONTWRITEBYTECODE']='1'
    try:
        for mode,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize=vptr','-fno-omit-frame-pointer'])]:
            binary=args.output/mode
            command=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pthread','-DNL_STUB',*flags,
                     '-I',str(args.output),*fp.test_include_flags(),str(fp.TESTS/'test_stand_replay.cpp'),'-o',str(binary)]
            calls=[('compile',command)]+[(case,[str(binary),str(args.client),case]) for case in
                ('ordered','retired-owner','init-failure','unavailable','throw-start')]
            for name,call in calls:
                row={'mode':mode,'name':name,'command':call};records.append(row)
                result=subprocess.run(call,cwd=fp.REPO,env=env,capture_output=True,text=True,timeout=120)
                row['exit_code']=result.returncode
                (args.output/(mode+'-'+name+'.log')).write_text(result.stdout+result.stderr)
                if result.returncode:print(result.stdout+result.stderr,file=sys.stderr)
                result.check_returncode();print(result.stdout,end='',flush=True)
    finally:
        unchanged=all(Path(p).is_file() and sha(Path(p))==h for p,h in before.items())
        result={'pass':len(records)==12 and all(r.get('exit_code')==0 for r in records),'checks':records,
                'inputs':before,'inputs_unchanged':unchanged,'actual_replayer':True,'target':'fake D3D CPU only',
                'game_launched':False,'gpu_used':False,'whole_scene_replay':False,'fps_claimed':False}
        (args.output/'validation.json').write_text(json.dumps(result,indent=2)+'\n')
    assert unchanged,'Relevant source/client inputs changed'

if __name__=='__main__':main()
