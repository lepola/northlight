#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Allocation-free actor distance preflight against the original one-vertex path."""
from pathlib import Path
import ast,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
tree=ast.parse((fp.TESTS/'test_actor_deformation.py').read_text())
harness=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='harness' for t in n.targets))
factory=harness[harness.index('std::vector<Word> code('):harness.index('int main(')]
(out/'actor_probe_program.h').write_text(factory)
native=[str(client_fixtures.four_bone_vs3())] if client_fixtures.shaders_available() else []
if not native:print('SKIP optional stock shader case: client/StormLib unavailable; synthetic SM1/2/3 checks remain')
source=fp.src('world_renderer.h').read_text();body=source[source.index('    void appendActor('):source.index('    void finishActorScene(){')]
assert 'sampledVertexInputsAt(program,replay.mesh(),elements,n,0,inputs)' in body
assert 'sampledDistanceFromInputs(program,inputs,replay.constants,context.inverseView,origin,unusedSquared,firstWorld)' in body
assert 'NorthlightGI::dot(distance,distance)>96*96' in body and 'Mesh first;' not in body
for opt in ['2','0']:
    exe=out/('actor-probe-O'+opt+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O'+opt,'-UNDEBUG','-std=c++17','-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'test_actor_probe_vertex.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    p=subprocess.run([str(exe),*native],check=True,capture_output=True,text=True);(out/('O'+opt+'.log')).write_text(p.stdout);print(p.stdout,end='')
