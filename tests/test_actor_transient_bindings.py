#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Compare bounded transient bindings to the frozen allocating implementation."""
from pathlib import Path
import argparse,ast,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
parser=argparse.ArgumentParser();parser.add_argument('--baseline-ref',default='4bc83d6');args=parser.parse_args()
old=subprocess.check_output(['git','show',args.baseline_ref+':src/replay/actor_deformation.h'],cwd=fp.REPO,text=True)
(out/'actor_reference.h').write_text(old.replace('NorthlightActorDeformation','ActorReference'))
tree=ast.parse((fp.TESTS/'test_actor_deformation.py').read_text())
harness=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='harness' for t in n.targets))
(out/'actor_probe_program.h').write_text(harness[harness.index('std::vector<Word> code('):harness.index('int main(')])
for opt in ['2','0']:
    exe=out/('actor-transient-O'+opt+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O'+opt,'-UNDEBUG','-std=c++17','-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'test_actor_transient_bindings.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    native=[str(client_fixtures.four_bone_vs3())] if client_fixtures.shaders_available() else []
    p=subprocess.run([str(exe),*native],check=True,capture_output=True,text=True)
    if args.baseline_ref=='4bc83d6':assert 'oldAllocations=3600 newAllocations=0' in p.stdout
    (out/('O'+opt+'-'+args.baseline_ref+'.log')).write_text(p.stdout);print(p.stdout,end='')
