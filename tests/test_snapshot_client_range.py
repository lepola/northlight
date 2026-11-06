#!/usr/bin/env python3
# northlight-test: requires=cxx,client
"""Actual PE headers prove writable-executable sections cannot bypass snapshots."""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--client',type=Path)
    args=parser.parse_args();client=(args.client or fp.client_root()/'Wow.exe').resolve()
    out=args.output or fp.output_dir()/'snapshot-client-range'
    assert not out.exists() or not any(out.iterdir()),'Preserve old evidence; use an empty output directory'
    out.mkdir(parents=True,exist_ok=True)
    source=fp.TESTS/'test_game_snapshot.cpp';store=fp.src('snapshot_store.h');game=fp.src('game_snapshot.h')
    sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
    flags=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*fp.test_include_flags()]
    commands=[]
    def call(label,command):
        commands.append(command);(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        result=subprocess.run(command,capture_output=True,text=True)
        (out/(label+'.log')).write_text(result.stdout+result.stderr)
        return result
    dependencies=call('dependencies',[*flags,'-MM',str(source)]);dependencies.check_returncode()
    paths=[Path(p) for p in shlex.split(dependencies.stdout.replace('\\\n',' ').split(': ',1)[1])]
    paths.extend([Path(__file__).resolve(),fp.REPO/'northlight_paths.py',client])
    before={str(p.resolve()):sha(p) for p in paths}
    (out/'inputs-before.json').write_text(json.dumps(before,indent=2)+'\n')
    results=[]
    for mode,extra in [('O2',['-O2']),('O0',['-O0']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=out/mode
        for label,command in [('compile',[*flags,*extra,str(source),'-o',str(exe),'-pthread']),('run',[str(exe),str(client)])]:
            result=call(mode+'-'+label,command)
            if result.returncode:print(result.stdout+result.stderr,file=sys.stderr,flush=True)
            result.check_returncode()
            if label=='run':print(result.stdout,end='',flush=True)
        results.append({'mode':mode,'pass':True,'executable_sha256':sha(exe)})
    # Exact previous section predicate, isolated from the production source.
    old=store.read_text();unsafe=old.replace('if((flags&0x80000000u)||!(flags&(0x20u|0x20000000u))||!virtualSize)continue;',
                                         'if(!(flags&(0x20u|0x20000000u))||!virtualSize)continue;')
    assert unsafe!=old and unsafe.count('if(!(flags&(0x20u|0x20000000u))||!virtualSize)continue;')==1
    private=out/'old-filter';private.mkdir();(private/'snapshot_store.h').write_text(unsafe);(private/'game_snapshot.h').write_bytes(game.read_bytes())
    generated={str(p):sha(p) for p in private.iterdir()}
    exe=out/'old-filter-test';result=call('old-filter-compile',[*flags[:1],'-I',str(private),*flags[1:],'-O2',str(source),'-o',str(exe),'-pthread']);result.check_returncode()
    result=call('old-filter-run',[str(exe),str(client)])
    assert result.returncode!=0 and 'SNAPSHOT_WRITABLE_GLOBAL_MISMATCH' in result.stderr,result.stdout+result.stderr
    assert {str(p):sha(Path(p)) for p in before}==before,'Source/client inputs changed'
    assert {str(p):sha(Path(p)) for p in generated}==generated,'Generated negative inputs changed'
    (out/'validation.json').write_text(json.dumps({'pass':True,'inputs':before,'inputs_unchanged':True,'generated_inputs':generated,'generated_inputs_unchanged':True,'results':results,'old_filter_rejected':True,'negative_returncode':result.returncode,'negative_executable_sha256':sha(exe),'commands':commands,'compiler':subprocess.check_output(['clang++','--version'],text=True),'client_executed':False,'private_client_bytes_committed':False,'game_launched':False},indent=2)+'\n')
    print('PASS actual PE snapshot classification and exact old-filter negative control')
if __name__=='__main__':main()
