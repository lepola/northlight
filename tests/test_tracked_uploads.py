#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Run the portable tracked-buffer contract on Windows, including replay upload elision."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.output_dir()
stubs=out/'tracked-upload-stubs';stubs.mkdir(parents=True,exist_ok=True)
(stubs/'d3d9.h').write_text('#pragma once\n')
source=fp.TESTS/'test_tracked_buffers.cpp'
watched=[source,Path(__file__),*(fp.src(n) for n in ('tracked_buffers.h','replay_copies.h','unlock_source.h','replay_thread.h'))]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p):sha(p) for p in watched};records=[]
for optimization in ('-O2','-O0'):
    exe=out/('tracked-uploads-'+optimization[1:]+'.exe')
    cmd=[fp.zig(),'c++','-target','x86-windows-gnu',optimization,'-UNDEBUG','-std=c++17','-I',stubs,*fp.test_include_flags(),source,'-o',exe]
    build=subprocess.run(list(map(str,cmd)),env=fp.zig_env(),capture_output=True,text=True)
    (out/('tracked-uploads-'+optimization[1:]+'.compile.log')).write_text(build.stdout+build.stderr)
    assert build.returncode==0,build.stderr
    run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
    print(run.stdout,flush=True);assert run.returncode==0,run.stderr
    records.append({'optimization':optimization,'exe_sha256':sha(exe),'output':run.stdout})
tracked=fp.src('tracked_buffers.h').read_text();copies=fp.src('replay_copies.h').read_text()
controls={
    'partial-discard':(tracked,copies.replace('c.state.load()==Slot::Valid&&c.complete&&','c.state.load()==Slot::Valid&&'), '!upload(0,4096,D3DLOCK_NOOVERWRITE)'),
    'synchronization':(tracked.replace('((flags==0&&!(record.metadata.usage&D3DUSAGE_DYNAMIC))||(discardBytes&&flags==D3DLOCK_NOOVERWRITE))','(flags==0||flags==D3DLOCK_DISCARD||flags==D3DLOCK_NOOVERWRITE)'),copies,'!upload(0,4096,0)'),
    'wrong-target':(tracked.replace('source.target==record.exposed&&data','source.target&&data'),copies,'!source.skipped()'),
}
for name,(header,copy_header,expected) in controls.items():
    folder=out/name;folder.mkdir(exist_ok=True)
    (folder/'tracked_buffers.h').write_text(header);(folder/'replay_copies.h').write_text(copy_header)
    (folder/'replay_copy_reader.h').write_bytes(fp.src('replay_copy_reader.h').read_bytes())
    exe=folder/'negative.exe'
    cmd=[fp.zig(),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-I',stubs,'-I',folder,*fp.test_include_flags(),source,'-o',exe]
    subprocess.run(list(map(str,cmd)),env=fp.zig_env(),check=True)
    run=subprocess.run([str(exe)],capture_output=True,text=True,timeout=60)
    assert run.returncode!=0 and expected in run.stderr,(name,run.stdout,run.stderr)
    records.append({'negative_control':name,'detected':True,'stderr':run.stderr})
    print('PASS detects unsafe '+name,flush=True)
assert all(sha(Path(p))==h for p,h in inputs.items())
(out/'tracked-upload-validation.json').write_text(json.dumps({'inputs':inputs,'records':records,'sanitizers':False,'real_driver':False},indent=2)+'\n')
