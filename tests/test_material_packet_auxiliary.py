#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Differential production packet capture/flush, unused VS banks only; no GPU/game."""
import hashlib,json,re,subprocess,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
watched=[p for p in fp.SRC.rglob('*') if p.is_file()]+[fp.TESTS/name for name in ['displacement_state_reuse.cpp','test_material_batch_bindings.cpp','test_material_packet_auxiliary.cpp']]+[Path(__file__)]
inputs={str(p):sha(p) for p in watched}
out=fp.output_dir()/'material-packet-auxiliary';out.mkdir(parents=True,exist_ok=True)
reference=(fp.TESTS/'displacement_state_reuse.cpp').read_text().split('int main(){')[0]
reference=reference.replace('struct MaterialCaptureStateTestAccess {','struct MaterialCaptureStateTestAccess {\n    static void replayBindings();\n    static void replayConstantRanges();\n    static void packetAuxiliary();')
(out/'material_bindings_reference.h').write_text(reference)
(out/'material_packet_bindings.h').write_text((fp.TESTS/'test_material_batch_bindings.cpp').read_text().rsplit('int main(){',1)[0])
header=fp.src('auto_bump_renderer.h').read_text()
assignment='packet->vertexFloatOnly=vertex.floatOnly;'
assert header.count(assignment)==1
control=header.replace(assignment,'packet->vertexFloatOnly=false;')
unsafe=header.replace(assignment,'packet->vertexFloatOnly=true;')
assert unsafe!=header
unsafe_previous=header.replace('previous&&!previous->vertexFloatOnly&&','previous&&')
assert unsafe_previous!=header
unsafe_pixel=header.replace('packet->pixelFloatOnly=model.pixelFloatOnly;','packet->pixelFloatOnly=true;')
unsafe_previous_pixel=header.replace('previous&&!previous->pixelFloatOnly&&','previous&&')
assert unsafe_pixel!=header and unsafe_previous_pixel!=header
records=[]
for mode in ['-O2','-O0']:
    cells={}
    for name,source in [('control',control),('current',header),*([('negative-used-bank',unsafe),('negative-previous-bank',unsafe_previous),('negative-used-pixel-bank',unsafe_pixel),('negative-previous-pixel-bank',unsafe_previous_pixel)] if mode=='-O2' else [])]:
        folder=out/(name+mode[1:]);folder.mkdir(exist_ok=True);(folder/'auto_bump_renderer.h').write_text(source)
        exe=folder/'fixture.exe'
        command=[str(fp.zig()),'c++','-target','x86-windows-gnu',mode,'-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-I',str(folder),'-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'test_material_packet_auxiliary.cpp'),'-luser32','-luuid','-o',str(exe)]
        subprocess.run(command,env=fp.zig_env(),check=True)
        result=subprocess.run([str(exe)],capture_output=True,text=True)
        (folder/'run.log').write_text(result.stdout+result.stderr)
        if name.startswith('negative'):
            assert result.returncode!=0 and 'MATERIAL_AUXILIARY_BANK_MISMATCH' in result.stderr,result.stdout+result.stderr
            records.append(dict(mode=mode,name=name,rejected=True,stderr=result.stderr));continue
        assert result.returncode==0,result.stdout+result.stderr
        assert 'MATERIAL_AUXILIARY_PASS cases=10 ' in result.stdout
        rows=[dict(re.findall(r'(\w+)=([^\s]+)',line)) for line in result.stdout.splitlines() if line.startswith('MATERIAL_AUXILIARY_CASE ')]
        assert len(rows)==10;cells[name]=rows
        records.append(dict(mode=mode,name=name,command=command,exe_sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),rows=rows))
    for before,after in zip(cells['control'],cells['current']):
        for key in ['mixed','failure','success','queued','draws','hash','pixelGetInt','pixelGetBool','pixelSetInt','pixelSetBool','packetSize','restored']:assert before[key]==after[key],(key,before,after)
        for key in ['getInt','getBool','setInt','setBool']:assert int(after[key])<=int(before[key]),(key,before,after)
        if after['failure']=='0':
            for key in ['getInt','getBool','setInt','setBool']:assert int(after[key])<int(before[key]),(key,before,after)
assert fp.src('auto_bump_renderer.h').read_text()==header
assert all(sha(Path(path))==digest for path,digest in inputs.items()),'Inputs changed during validation'
(out/'validation.json').write_text(json.dumps(dict(inputs=inputs,inputs_unchanged=True,records=records,production_capture_flush=True,gpu_or_game_launched=False,timings_claimed=False),indent=2)+'\n')
print('PASS exact consumed VS/pixel banks, zero unused VS I/B getters/replay, unknown fallback, shader changes, resident/UP, failures and restoration; negative used-bank control rejected.')
