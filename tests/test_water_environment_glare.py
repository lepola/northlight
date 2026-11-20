#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Execute the production environment-glare publisher without a device or screen history."""
import pathlib,subprocess,tempfile,sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]))
import northlight_paths as fp
source=fp.src('celestial_disc_renderer.h').read_text()
a=source.index('    unsigned waterGlare(');b=source.index('\n#endif',a)
method=source[a:b]
shader=fp.src('world_effects.hlsl').read_text();gradient=shader[shader.index('float4 WaterSkyGradient('):]
assert all(name not in gradient for name in ('WaterSunHalo','WaterSunRing','WaterMoonHalo','WaterMoonRing'))
assert 'waterGlare(sky,celestialGlowHue(),' in fp.src('renderer.cpp').read_text()
program=r"""
#include <cassert>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>
#include "celestial_context.h"
#include "celestial_disc.h"
#include "celestial_glow.h"
struct IDirect3DTexture9 {};
struct Publisher {
    bool failed=false;IDirect3DTexture9* texture[2]={};unsigned textureWidth[2]={0,128};
    static constexpr float SunGlareSupport=NorthlightCelestialGlow::SunGlareSupport,MoonGlareSupport=NorthlightCelestialGlow::MoonGlareSupport;
METHOD
};
int main(){
    Publisher p;IDirect3DTexture9 lunar;p.texture[1]=&lunar;
    NorthlightCelestial::Context context;NorthlightCelestialGlow::Hue hue;
    for(auto* body:{&context.sun,&context.moon}){
        body->direction[0]=std::sqrt(.96f);body->direction[2]=.2f;
        body->tint[0]=body->tint[1]=body->tint[2]=1;body->alpha=1;body->angularRadius=.05f;
    }
    float reference[13][4],output[13][4];IDirect3DTexture9* moonBody=nullptr;
    assert(p.waterGlare(context,hue,1,reference,moonBody)==3);
    assert(reference[4][2]==1&&reference[9][2]==1&&moonBody==&lunar);
    // No halo textures or frame history are available to this production method.
    // The first frame and every subsequent call must publish the same field.
    for(unsigned i=0;i<100;++i){
        assert(p.waterGlare(context,hue,1,output,moonBody)==3);
        assert(std::memcmp(reference,output,sizeof output)==0);
    }
    unsigned transitions=0;
    for(float weather:{1.f,.5f,.01f,0.f,.01f,.5f,1.f}){
        assert(p.waterGlare(context,hue,weather,output,moonBody)==3);
        if(weather==0){for(auto& row:output)for(float x:row)assert(x==0);}
        else {
            for(unsigned body=0;body<2;++body){
                const unsigned row=body*5;
                assert(std::fabs(output[row+1][2]-reference[row+1][2]*weather)<1e-6);
                assert(std::fabs(output[row+2][3]-reference[row+2][3]*weather)<1e-6);
            }
        }
        ++transitions;
    }
    context.sun.alpha=0;assert(p.waterGlare(context,hue,1,output,moonBody)==3&&output[4][0]==0);
    context.sun.alpha=1;assert(p.waterGlare(context,hue,1,output,moonBody)==3&&std::memcmp(reference,output,sizeof output)==0);
    for(float bad:{-1.f,2.f,std::numeric_limits<float>::quiet_NaN()}){
        assert(p.waterGlare(context,hue,bad,output,moonBody)==0);
        for(auto& row:output)for(float x:row)assert(x==0);
    }
    context.sun.direction[0]=std::numeric_limits<float>::quiet_NaN();
    assert(p.waterGlare(context,hue,1,output,moonBody)==2&&output[4][2]==0&&output[9][2]==1);
    std::puts("PASS production environment glare: stable first/repeated frames, no screen history, weather/opacity zero-return, invalid-source isolation");
}
""".replace('METHOD',method)
with tempfile.TemporaryDirectory(prefix='northlight-environment-glare-') as tmp:
    cpp=pathlib.Path(tmp)/'test.cpp';exe=pathlib.Path(tmp)/'test';cpp.write_text(program)
    subprocess.run(['clang++','-std=c++17','-O1','-g','-fsanitize=address,undefined',*fp.test_include_flags(),str(cpp),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
