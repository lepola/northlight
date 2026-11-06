#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Actual shared snapshot retirement; fake COM asserts owner-thread releases.

O2/O0, ASan/UBSan and TSan cover queued demand, synchronous critical release,
queue saturation, escaped holders, prediction invalidation,
rebuild and complete ledger drainage. Compiled destructive controls must fail.
Allocation failure is injected at O2/O0 and ASan/UBSan; TSan owns operator new.
Component timing excludes population and is not a game FPS measurement.
"""
from pathlib import Path
import ast,hashlib,json,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.output_dir()/'snapshot-retirement';out.mkdir(parents=True,exist_ok=False)
source=fp.TESTS/'test_snapshot_retirement.cpp';header=fp.src('draw_snapshot.h')
stub_source=fp.TESTS/'test_terrain_snapshot.py'
stub=next(ast.literal_eval(n.value) for n in ast.parse(stub_source.read_text()).body if isinstance(n,ast.Assign) and any(getattr(t,'id','')=='stub' for t in n.targets))
watched=[header,source,stub_source,Path(__file__),fp.src('cpu_retirement.h'),fp.src('source_identity.h')]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p):sha(p) for p in watched};report=dict(pass_=False,inputs=inputs,runs=[],game_or_gpu_launched=False)
original=header.read_text()
modes=[('O2',['-O2']),('O0',['-O0']),('ASan-UBSan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']),('TSan',['-O1','-g','-fsanitize=thread'])]
controls=[('no-retirement','if((retirement||batch)&&!sources_->entries.empty())try{','if(false&&(retirement||batch)&&!sources_->entries.empty())try{','RETIREMENT_NOT_QUEUED'),
          ('no-index-retirement','if((retirement||batch)&&indexCacheEntries())try{','if(false&&(retirement||batch)&&indexCacheEntries())try{','INDEX_RETIREMENT_NOT_QUEUED'),
          ('worker-COM','declaration->Release();declaration=nullptr;','/* counterfactual: COM ownership reaches the worker */','COM_RELEASE_BEFORE_ENQUEUE')]
try:
    for name,body,rejection in [('current',original,None),*[(n,original.replace(a,b),r) for n,a,b,r in controls]]:
        folder=out/name;folder.mkdir();(folder/'d3d9.h').write_text(stub);(folder/'draw_snapshot.h').write_text(body)
        for mode,flags in modes if name=='current' else modes[:1]:
            executable=folder/mode
            command=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pthread',*flags,'-I',str(folder),*fp.test_include_flags(),str(source),'-o',str(executable)]
            compiled=subprocess.run(command,capture_output=True,text=True);(folder/(mode+'-compile.log')).write_text(compiled.stdout+compiled.stderr)
            assert compiled.returncode==0,(name,mode,compiled.stderr)
            result=subprocess.run([str(executable)],capture_output=True,text=True,timeout=120);log=result.stdout+result.stderr;(folder/(mode+'.log')).write_text(log)
            report['runs'].append(dict(name=name,mode=mode,exit_code=result.returncode,expected_rejection=rejection,executable_sha256=sha(executable)))
            if rejection:assert result.returncode!=0 and rejection in log,(name,mode,log)
            else:
                assert result.returncode==0 and 'SNAPSHOT_RETIRE_PASS ' in log,(name,mode,log)
                for case in ['demand','critical','queue-full','indices-only','source-priority',*(['allocation-failure'] if mode!='TSan' else [])]:assert 'case='+case+' ' in log,(name,mode,case)
            print(name,mode,result.returncode,log[-800:],flush=True)
    report['pass_']=True
finally:
    report['inputs_unchanged']=all(sha(Path(p))==v for p,v in inputs.items());report['pass_'] &= report['inputs_unchanged']
    (out/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
assert report['pass_']
