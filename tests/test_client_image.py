#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Bounded raw-file PE32 proof provider; optional client bytes remain private."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--client',type=Path,help='Optional read-only original PE32 client; never executed or copied')
    args=parser.parse_args();out=args.output or fp.output_dir()/'client-image'
    assert not out.exists() or not any(out.iterdir()),'Preserve prior evidence; output must be empty'
    out.mkdir(parents=True,exist_ok=True)
    paths=[Path(__file__),fp.REPO/'northlight_paths.py',fp.TESTS/'test_client_image.cpp',fp.src('client_image.h')]
    if args.client:paths.append(args.client.resolve())
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    before={str(p.resolve()):sha(p) for p in paths};commands=[];results=[]
    (out/'inputs-before.json').write_text(json.dumps(before,indent=2)+'\n')
    for mode,flags in [('O2',['-O2']),('O0',['-O0']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=out/mode
        compile_command=['clang++','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(fp.TESTS/'test_client_image.cpp'),'-o',str(exe)]
        for label,command in [('compile',compile_command),('run',[str(exe),*([str(args.client.resolve())] if args.client else [])])]:
            commands.append(command);(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
            result=subprocess.run(command,capture_output=True,text=True);(out/(mode+'-'+label+'.log')).write_text(result.stdout+result.stderr)
            if result.returncode:print(result.stdout+result.stderr,file=sys.stderr,flush=True)
            result.check_returncode()
            if label=='run':print(result.stdout,end='',flush=True)
        results.append({'mode':mode,'executable_sha256':sha(exe),'pass':True})
    assert all(Path(p).is_file() and sha(Path(p))==h for p,h in before.items()),'Source/client inputs changed'
    (out/'validation.json').write_text(json.dumps({'pass':True,'inputs':before,'inputs_unchanged':True,'results':results,'commands':commands,'compiler':subprocess.check_output(['clang++','--version'],text=True),'client_executed':False,'private_client_bytes_committed':False,'game_launched':False},indent=2)+'\n')
if __name__=='__main__':main()
