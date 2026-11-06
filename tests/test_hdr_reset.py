#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Physical reset count, original parameters and rollback using the real x86 SDK."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]));import northlight_paths as fp
if os.name!='nt':
    raise SystemExit('This x86 Windows CPU fixture requires a Windows host.')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs=[fp.src('hdr_presentation.h'),fp.src('forwarders.h'),fp.TESTS/'hdr_reset.cpp',Path(__file__)]
before={str(p):sha(p) for p in inputs};runs=[]
with tempfile.TemporaryDirectory(prefix='northlight-hdr-reset-') as name:
    for mode in ['ReleaseFast','Debug']:
        binary=Path(name)/(mode+'.exe')
        command=[str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17','-O2' if mode=='ReleaseFast' else '-O0','-fno-rtti',*fp.include_flags(),str(fp.TESTS/'hdr_reset.cpp'),'-o',str(binary)]
        built=subprocess.run(command,capture_output=True,text=True,env=fp.zig_env());assert built.returncode==0,built.stdout+built.stderr
        result=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30);assert result.returncode==0,result.stdout+result.stderr
        assert 'HDR_RESET_PASS cases=14 ' in result.stdout
        runs.append(dict(mode=mode,command=command,sha256=sha(binary),stdout=result.stdout));print(result.stdout,flush=True)
assert all(sha(Path(p))==h for p,h in before.items())
(fp.output_dir()/'hdr-reset-validation.json').write_text(json.dumps(dict(inputs=before,inputs_unchanged=True,runs=runs,gpu_used=False),indent=2)+'\n')
