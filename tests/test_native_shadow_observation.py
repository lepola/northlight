#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Pure native-shadow observation proof; no D3D/device/game or asset requirement."""
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
    parser.add_argument('--corpus',type=Path,help='Optional private NSC1 corpus; original bytecode stays outside Git')
    args=parser.parse_args();out=args.output or fp.output_dir()/'native-shadow-observation'
    assert not out.exists() or not any(out.iterdir()),'Preserve old evidence; use an empty output directory'
    out.mkdir(parents=True,exist_ok=True)
    paths=[Path(__file__),fp.REPO/'northlight_paths.py',fp.TESTS/'test_native_shadow_observation.cpp',fp.src('native_shadow_observation.h'),fp.src('patch_shadow_shader.h')]
    if args.corpus:paths.append(args.corpus.resolve())
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    before={str(p.resolve()):sha(p) for p in paths};commands=[];results=[]
    for mode,flags in [('O2',['-O2']),('O0',['-O0']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=out/mode
        command=['clang++','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(fp.TESTS/'test_native_shadow_observation.cpp'),'-o',str(exe)]
        for label,call in [('compile',command),('run',[str(exe),*([str(args.corpus.resolve())] if args.corpus else [])])]:
            commands.append(call);(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
            r=subprocess.run(call,capture_output=True,text=True);(out/(mode+'-'+label+'.log')).write_text(r.stdout+r.stderr)
            if r.returncode:print(r.stdout+r.stderr,file=sys.stderr,flush=True)
            r.check_returncode()
            if label=='run':print(r.stdout,end='',flush=True)
        results.append({'mode':mode,'executable_sha256':sha(exe),'pass':True})
    assert all(Path(p).is_file() and sha(Path(p))==h for p,h in before.items()),'Source/corpus changed'
    (out/'validation.json').write_text(json.dumps({'inputs':before,'inputs_unchanged':True,'results':results,'commands':commands,'compiler':subprocess.check_output(['clang++','--version'],text=True),'original_bytecode_in_repository':False,'game_launched':False},indent=2)+'\n')
if __name__=='__main__':main()
