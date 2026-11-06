"""Portable replay/atlas scenarios, using the existing queue's SDK test fixture."""
from pathlib import Path
import ast,json,subprocess,sys
root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root))
import northlight_paths as fp
fp.use_source_modules()
source=root/'tests/test_command_stream.py';tree=ast.parse(source.read_text())
# Reuse fixture definitions only. The separate legacy DIRECT/HDR source audit
# is unrelated to these texture scenarios and has its own test entry point.
definitions=[n for n in tree.body if isinstance(n,(ast.Import,ast.ImportFrom,ast.Assign,ast.AnnAssign,ast.FunctionDef))]
ns={'__file__':str(source)};exec(compile(ast.Module(body=definitions,type_ignores=[]),str(source),'exec'),ns)
out=fp.out()/'material-stream';out.mkdir(exist_ok=True)
(out/'d3d9_stub.h').write_text('#ifdef __uuidof\n#undef __uuidof\n#endif\n#ifndef FALSE\n#define FALSE 0\n#endif\n'+ns['stub']())
(out/'cs_generated.inc').write_text(ns['generated_cpp']())
includes=[]
if sys.platform=='win32':
    # The Linux SDK stub has integer COM IDs and deliberately no Win32 types.
    # Select the production portable host branches in private header copies;
    # the full DLL build separately checks all actual Win32 branches.
    host=out/'host-source';host.mkdir(exist_ok=True)
    for file in (root/'src').rglob('*'):
        if file.suffix in ('.h','.inl'):
            target=host/file.relative_to(root/'src');target.parent.mkdir(parents=True,exist_ok=True)
            target.write_text(file.read_text().replace('_WIN32','NORTHLIGHT_HOST_TEST_WINDOWS_DISABLED'))
    includes=['-I'+str(host/d.name) for d in host.iterdir() if d.is_dir()]
env=fp.zig_env();env.update(TEMP=str(fp.out()/'tmp'),TMP=str(fp.out()/'tmp'));exe=out/'stream.exe'
subprocess.run([str(fp.zig()),'c++','-O2','-UNDEBUG','-std=c++17','-DNL_STUB','-I'+str(out),*includes,*fp.test_include_flags(),str(root/'tests/test_command_stream.cpp'),'-o',str(exe)],env=env,check=True)
outputs=[]
for scenario in ('particletextures','materialtextures'):
    run=subprocess.run([str(exe),scenario],capture_output=True,text=True,timeout=90,env=env);outputs.append(run.stdout+run.stderr)
    if run.returncode:raise RuntimeError(outputs[-1])
    assert 'PASS' in run.stdout;print(run.stdout,flush=True)
(out/'validation.json').write_text(json.dumps({'scenarios':['particletextures','materialtextures'],'output':outputs},indent=2)+'\n')
