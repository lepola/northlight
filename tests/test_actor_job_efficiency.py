#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Exact worker scene output and allocations against the pre-wave resolver."""
from pathlib import Path
import ast,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
old=subprocess.check_output(['git','show','4bc83d6:src/replay/actor_scene_job.h'],cwd=fp.REPO,text=True)
(out/'actor_job_reference.h').write_text(old.replace('NorthlightActorGeometry','ActorJobReference'))
tree=ast.parse((fp.TESTS/'test_actor_deformation.py').read_text())
harness=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='harness' for t in n.targets))
(out/'actor_probe_program.h').write_text(harness[harness.index('std::vector<Word> code('):harness.index('int main(')])
source=fp.src('world_renderer.h').read_text()
assert 'packet.sharedPosition=it->second' in source and 'packet.sharedUV=uvProgram->second' in source
for opt in ['2','0']:
    exe=out/('actor-job-efficiency-O'+opt+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O'+opt,'-UNDEBUG','-std=c++17','-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'test_actor_job_efficiency.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],env=fp.zig_env(),check=True)
    p=subprocess.run([str(exe)],check=True,capture_output=True,text=True);(out/('O'+opt+'.log')).write_text(p.stdout);print(p.stdout,end='')
