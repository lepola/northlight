#!/usr/bin/env python3
# northlight-test: requires=zig
"""Real StreamDevice redundant bindings: correctness, lifetime, and repeatable CPU timing."""
import ast,json,os,subprocess,sys,types
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
fp.use_source_modules()

out=fp.output_dir()/'pipeline-bindings-cpu';out.mkdir(parents=True,exist_ok=True)
# Reuse its stub/case generators without running the unrelated direct-replay audit.
source=fp.TESTS/'test_command_stream.py';tree=ast.parse(source.read_text(encoding='utf-8'))
tree.body=[n for n in tree.body if isinstance(n,(ast.Import,ast.ImportFrom,ast.Assign,ast.FunctionDef))]
checks=types.ModuleType('command_stream_generators');checks.__file__=str(source)
exec(compile(tree,str(source),'exec'),checks.__dict__)
(out/'d3d9_stub.h').write_text('#ifndef FALSE\n#define FALSE 0\n#endif\n'+checks.stub(),encoding='utf-8')
(out/'cs_generated.inc').write_text(checks.generated_cpp(),encoding='utf-8')
# A frozen source tree can use the identical harness and compiler for an A/B run.
baseline=os.environ.get('NORTHLIGHT_PIPELINE_BASELINE')
roots=[('current',fp.REPO)]+([('baseline',Path(baseline))] if baseline else [])
report={}
for label,root in roots:
    includes=[str(Path(f).relative_to(fp.REPO)) if Path(f).is_absolute() else f for f in fp.test_include_flags()]
    includes=[str(root/f) if f!='-I' else f for f in includes]
    exe=out/(label+'.exe')
    command=[str(fp.zig()),'c++',*(['-target','x86-windows-gnu'] if os.name=='nt' else []),'-std=c++17','-O2','-UNDEBUG','-DNL_STUB','-pthread','-I',str(out),*includes,str(fp.TESTS/'pipeline_bindings_cpu.cpp'),'-o',str(exe)]
    subprocess.run(command,check=True,env=fp.zig_env())
for repeat in range(3):
    for label,_ in (roots if repeat%2==0 else roots[::-1]):
        result=subprocess.run([str(out/(label+'.exe'))],capture_output=True,text=True,check=True,timeout=180)
        assert 'PIPELINE_BINDINGS_CPU_PASS' in result.stdout
        (out/(label+f'-{repeat}.log')).write_text(result.stdout,encoding='utf-8')
        for stage in ('BINDINGS_CPU','CLASSIFY_CPU'):
            report.setdefault(label,{}).setdefault(stage,[]).extend(float(line.split('ms=')[1].split()[0]) for line in result.stdout.splitlines() if line.startswith(stage))
result=subprocess.run([str(out/'current.exe'),'full'],capture_output=True,text=True,timeout=900)
(out/'command-stream-full.log').write_text(result.stdout+result.stderr,encoding='utf-8')
result.check_returncode()
(out/'timings.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
import statistics
for label,stages in report.items():
    for stage,samples in stages.items():print(label,stage,'median_ms',statistics.median(samples),'samples',len(samples))
print('PASS production stream bindings, identical filtered command counts and resource lifetime')
