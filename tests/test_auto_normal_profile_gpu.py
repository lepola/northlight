#!/usr/bin/env python3
# northlight-test: requires=zig,windows-gpu
"""Model normal rebuilds skip CPU source reads and preserve the VS bank."""
from pathlib import Path
import os,re,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir()/'auto-normal-profile-gpu';out.mkdir(parents=True,exist_ok=True)
binary=out/'normal-profile.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-Wno-microsoft-exception-spec','-std=c++17',*fp.test_include_flags(),str(Path(__file__).with_name('auto_normal_profile_gpu.cpp')),'-luser32','-o',str(binary)],env=fp.zig_env(),check=True)
runtime=Path(os.environ['SystemRoot'])/'SysWOW64/d3d9.dll'
run=subprocess.run([str(binary),str(runtime),str(out),'optimized'],cwd=out,env=fp.zig_env(),capture_output=True,text=True,timeout=90)
(out/'gpu.log').write_text(run.stdout+run.stderr,encoding='utf-8')
assert run.returncode==0,run.stdout+run.stderr
assert re.search(r'NORMAL_PROFILE_PASS cases=18 modelSourceReads=0 modelSourceBytes=0 worldSourceReads=9 ',run.stdout),run.stdout
for width in (32,128,256):
    positive=(out/f'{width}-0-model.rgba').read_bytes()
    assert positive==(out/f'{width}-1-model.rgba').read_bytes()
    assert positive!=(out/f'{width}-2-model.rgba').read_bytes(),"Model polarity fixture must render relief"
print(run.stdout.strip())
