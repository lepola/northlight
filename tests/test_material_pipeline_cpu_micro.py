#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exact CPU regressions for material height, ownership, constants and deformation."""
import hashlib,json,re,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

out=fp.output_dir()/'material-pipeline-micro';out.mkdir(parents=True,exist_ok=True)
helper=(fp.TESTS/'displacement_state_reuse.cpp').read_text().split('int main(){')[0]
helper=helper.replace('struct MaterialCaptureStateTestAccess {','struct MaterialCaptureStateTestAccess {\n static void replayBindings();static void replayConstantRanges();static void microPool();')
(out/'material_bindings_reference.h').write_bytes(('#pragma once\n'+helper).encode())
actor=fp.src('actor_deformation.h').read_text()
poison=actor[actor.index('inline bool worldPositionsPrepared'):actor.index('// One currently referenced vertex')]
for name in ['worldPositionsPrepared','textureUVsPrepared','worldPositions','textureUVs']:
    poison=re.sub(r'\b'+name+r'\(',name+'Poison(',poison)
assert poison.count('Four inputs[16];if(clearInputs)')==4
poison=poison.replace('Four inputs[16];if(clearInputs)','Four inputs[16];std::memset(inputs,0xa5,sizeof inputs);if(clearInputs)')
(out/'material_input_poison.h').write_bytes(('namespace NorthlightActorDeformation{\n'+poison+'\n}\n').encode())
watch=[p for p in fp.SRC.rglob('*') if p.is_file()]+[fp.TESTS/name for name in ['displacement_state_reuse.cpp','test_material_batch_bindings.cpp','material_pipeline_cpu_micro.cpp']]+[Path(__file__)]
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
inputs={str(p):sha(p) for p in watch};runs=[]
for mode in ['O2','O0']:
    exe=out/(mode+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-'+mode,'-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-Wno-return-type','-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'material_pipeline_cpu_micro.cpp'),'-luser32','-luuid','-o',str(exe)],env=fp.zig_env(),check=True)
    result=subprocess.run([str(exe)],text=True,capture_output=True)
    (out/(mode+'.log')).write_text(result.stdout+result.stderr)
    assert result.returncode==0 and 'MATERIAL_PIPELINE_MICRO_PASS groups=9' in result.stdout,result.stdout+result.stderr
    runs.append(mode);print(mode,'PASS exact CPU material regressions',flush=True)
assert all(sha(Path(p))==digest for p,digest in inputs.items()),'Inputs changed during validation'
(out/'validation.json').write_text(json.dumps({'pass':True,'inputs':inputs,'modes':runs,'groups':9,'GPU_used':False,'game_launched':False},indent=2)+'\n')
