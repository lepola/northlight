#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Execute the real boundary gate after world capture, including full-size offscreen UI."""
from pathlib import Path
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
fixture = runpy.run_path(str(ROOT / 'tests/test_model_world_context.py'))
fp = fixture['fp']
harness = fixture['HARNESS'].split('int main(){')[0]
harness = harness.replace('__MODEL__', fixture['MODEL']).replace('__BEFORE__', fixture['BEFORE']).replace('__PROJECTION__', fixture['PROJECTION'])
harness += r'''
int main(){
    auto* shader=reinterpret_cast<IDirect3DVertexShader9*>(1);unsigned cases=0;
    for(bool hdr:{true,false})for(int tag:{0,2})for(bool input:{true,false})for(bool back:{false,true}){
        Device p;p.hdr.active=hdr;p.vc.entry=tag;p.vc.skinned=false;
        p.terrain=p.projectionValid=true;p.ortho=true;p.uiInputs=input;p.backTarget=back;
        for(float& value:p.state.rows)value=0;p.state.rows[0]=p.state.rows[5]=p.state.rows[10]=p.state.rows[15]=1;
        p.state.z=0;p.state.zf=D3DCMP_ALWAYS;
        p.beforeDraw(shader);
        const bool accepted=back&&((!hdr&&tag==2)||input);
        if(p.effects!=unsigned(accepted)){std::printf("FAIL HDR=%d tag=%d safeInput=%d mainTarget=%d effects=%u expected=%u\n",hdr,tag,input,back,p.effects,unsigned(accepted));std::fflush(stdout);}
        assert(p.effects==unsigned(accepted)&&p.spells==unsigned(accepted));
        if(!accepted){
            // Rejecting an intermediate pass must leave the current world available
            // for the following real UI; it must not freeze a previous scene.
            assert(!p.applied&&!p.hdrAtUiBoundary);
            p.backTarget=p.uiInputs=true;p.beforeDraw(shader);
            assert(p.effects==1&&p.spells==1&&p.applied);
        }
        ++cases;
    }
    std::printf("PASS effect UI target: %u world/offscreen/scene-input/main-UI cases\n",cases);
}
'''
with tempfile.TemporaryDirectory(prefix='effect-ui-target-') as tmp:
    cpp, exe = Path(tmp) / 'test.cpp', Path(tmp) / 'test.exe'
    cpp.write_text(harness)
    for optimization in ('-O2', '-O0'):
        subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', optimization, '-UNDEBUG',
                        *fp.include_flags(), str(cpp), '-luser32', '-o', str(exe)], env=fp.zig_env(), check=True)
        subprocess.run([str(exe)], check=True)
