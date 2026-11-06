#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client,stormlib
"""Current-pose reuse must equal the former independent SkinEnvelope exactly."""
import ast
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures

out = fp.output_dir()
out.mkdir(parents=True, exist_ok=True)
prefix = (fp.TESTS / 'displacement_state_reuse.cpp').read_text(encoding='utf-8').split('struct DisplacementStateTestAccess {')[0]
tree = ast.parse((fp.TESTS / 'test_replay_skin_safety.py').read_text(encoding='utf-8'))
fixture = next(ast.literal_eval(n.value) for n in tree.body if isinstance(n, ast.Assign) and any(isinstance(t, ast.Name) and t.id == 'harness' for t in n.targets)).split('int main(')[0]
source = prefix + fixture + r'''
struct DisplacementStateTestAccess {
    static void run(const char* path){
        { // An exhausted range proof must fail before touching the device.
            NorthlightAutoDisplacement g(nullptr);auto* key=reinterpret_cast<IDirect3DVertexShader9*>(uintptr_t(1));
            g.materials[key].skinned=g.materials[key].view.skinned=true;
            g.poseEnvelopes.chargeCheapPreparation(UINT64_MAX);
            NorthlightAutoDisplacement::Args args{};float nearest=17,farthest=23;
            for(unsigned i=0;i<10000;++i)assert(!g.nearestMaterialDepth(key,args,nearest,false,nullptr,&farthest));
            assert(nearest==17&&farthest==23&&!g.poseEnvelopes.canReady());
            g.endFrame();assert(g.poseEnvelopes.canReady());
        }
        auto program=load(path);
        if(!program.kernel.skin){std::swap(program.operations[0].source[0],program.operations[0].source[1]);buildKernel(program);}
        assert(program.kernel.skin&&SkinEnvelope::supports(program));
        NorthlightAutoDisplacement g(nullptr);g.range=12;
        float identity[16]={};for(unsigned k=0;k<4;++k)identity[5*k]=1;
        unsigned checked=0;
        for(unsigned generation=0;generation<3;++generation){
            auto mesh=geometry(1200);float changed=float(generation)*20;
            std::memcpy(mesh.streams[0].bytes.data(),&changed,4);
            auto owner=std::make_shared<const Mesh>(std::move(mesh));
            SkinEnvelope independent;
            for(unsigned v=0;v<owner->vertexCount;++v){Four inputs[16];
                assert(sampledVertexInputsAt(program,*owner,declaration,4,v,inputs)&&independent.add(inputs));}
            for(unsigned pose=0;pose<40;++pose){
                palette(g.constants,pose);
                auto current=program;
                if(pose&1)current.definitions.push_back({31,{1,0,0,float(pose)}});
                Bounds reused;float low[3],high[3];
                for(unsigned attempt=0;attempt<1000&&!reused.valid;++attempt){g.endFrame();
                    g.distantPoseBounds(current,owner,declaration,4,low,high,nullptr,nullptr,&reused);}
                Bounds old;assert(reused.valid&&g.evaluateLocalSkin(independent,current,g.constants,identity,old));
                assert(!std::memcmp(reused.low,old.low,sizeof old.low)&&!std::memcmp(reused.high,old.high,sizeof old.high));
                enclosed(current,*owner,g.constants,identity,reused);++checked;
                // A failed current-pose proof must not expose the previous result.
                g.endFrame();g.envelopeBudget.maxOperations=0;
                g.distantPoseBounds(current,owner,declaration,4,low,high,nullptr,nullptr,&reused);
                assert(!reused.valid);g.envelopeBudget.maxOperations=Budget{}.maxOperations;
            }
        }
        std::printf("MATERIAL_POSE_REUSE_PASS exact_bounds=%u current_palette definitions source_generations budget_failure\n",checked);
    }
};
int main(int argc,char** argv){assert(argc==2);DisplacementStateTestAccess::run(argv[1]);}
'''
cpp = out / 'pose-reuse.cpp'
cpp.write_text(source, encoding='utf-8')
fixture_path = client_fixtures.four_bone_vs3()
for opt in ('-O0', '-O2'):
    exe = out / ('pose-reuse' + opt + '.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', opt, '-UNDEBUG', '-std=c++17',
                    '-Wno-microsoft-exception-spec', *fp.test_include_flags(), str(cpp), '-luser32', '-luuid', '-o', str(exe)], env=fp.zig_env(), check=True)
    subprocess.run([str(exe), str(fixture_path)], check=True)
