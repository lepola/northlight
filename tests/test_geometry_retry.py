#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Replay notifications through the production builder's complete retry lambda."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

p=argparse.ArgumentParser();p.add_argument('--log',type=Path);args=p.parse_args()
out=fp.output_dir()/'geometry-retry';out.mkdir(parents=True,exist_ok=False)
header=fp.src('world_renderer.h');source=Path(__file__).with_suffix('.cpp')
text=header.read_text();begin=text.index('            auto deferBuild=');end=text.index('            // Issue #40:',begin);body=text[begin:end]
current='return stopping||builderExit||!NorthlightWorldStreaming::applicable(r.map,r.geometryCenter,request.map,request.camera);'
old='return stopping||builderExit||request.id!=r.id;'
assert body.count(current)==1
inputs=[header,source,Path(__file__),fp.src('world_streaming.h'),fp.src('geometry_memory.h')]
sha=lambda path:hashlib.sha256(path.read_bytes()).hexdigest()
before={str(path):sha(path) for path in inputs}
replay=[]
if args.log:
    before[str(args.log)]=sha(args.log)
    for line in args.log.read_text().splitlines():
        match=re.match(r'GI request tick=(\d+) id=(\d+) reason=\d+ camera=\(([^)]+)\)',line)
        if match:
            at,id=map(int,match.group(1,2));xyz=list(map(float,match[3].split()));assert len(xyz)==3
            if replay:assert at>=replay[-1][0]
            replay.append((at,id,*xyz))
    assert len(replay)>1
    (out/'camera.txt').write_text(''.join(' '.join(map(str,row))+'\n' for row in replay))
records=[]
for name,code in [('current',body),('old-id-wake',body.replace(current,old))]:
    folder=out/name;folder.mkdir();(folder/'geometry_retry.inl').write_text(code)
    for mode,flags in [('O2',['-O2']),('O0',['-O0']),('sanitizers',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=folder/mode;command=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,'-I',str(folder),*fp.test_include_flags(),str(source),'-o',str(exe)]
        if name=='old-id-wake':command.insert(1,'-DNORTHLIGHT_OLD_RETRY=1')
        subprocess.run(command,check=True)
        result=subprocess.run([str(exe),*([str(out/'camera.txt')] if replay else [])],capture_output=True,text=True,timeout=30)
        (folder/(mode+'.log')).write_text(result.stdout+result.stderr);assert result.returncode==0,result.stdout+result.stderr
        records.append(dict(name=name,mode=mode,output=result.stdout,command=command));print(name,mode,result.stdout,flush=True)
assert all(sha(Path(path))==value for path,value in before.items()),'Inputs changed during validation'
(out/'validation.json').write_text(json.dumps(dict(inputs=before,unchanged=True,captured_requests=len(replay),records=records,game_or_gpu_launched=False),indent=2)+'\n')
