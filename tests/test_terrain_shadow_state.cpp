#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
using HRESULT=long;
constexpr HRESULT D3D_OK=0;
#define SUCCEEDED(x) ((x)>=0)
struct Shader {int releases=0;void Release(){++releases;}};
using IDirect3DVertexShader9=Shader;
using IDirect3DPixelShader9=Shader;
struct CpuScope {explicit CpuScope(int){}};
struct DeviceState{
    Shader* vertex=nullptr;Shader* pixel=nullptr;std::array<float,896> constants{};
    unsigned calls=0,failAt=0;
    bool fail(){return ++calls==failAt;}
    HRESULT GetPixelShaderConstantF(unsigned base,float* out,unsigned count){if(fail())return -1;std::memcpy(out,constants.data()+base*4,count*16);return 0;}
    HRESULT SetPixelShaderConstantF(unsigned base,const float* in,unsigned count){if(fail())return -1;std::memcpy(constants.data()+base*4,in,count*16);return 0;}
    HRESULT SetVertexShader(Shader* shader){if(fail())return -1;vertex=shader;return 0;}
    HRESULT SetPixelShader(Shader* shader){if(fail())return -1;pixel=shader;return 0;}
};
struct Hook{
    DeviceState* ext=nullptr;
    Shader *shadowSwapOriginal=nullptr,*shadowSwapReplacement=nullptr,*shadowSwapVertexOriginal=nullptr,*shadowSwapVertexReplacement=nullptr;
    unsigned shadowSwapConstantBase=0,shadowSwapConstantCount=0,terrainShadowDraws=0;
    float shadowSwapConstants[24]={};
    int sampledHookTimer(){return 0;}
    // Exact current production method, extracted by the Python driver.
#include "terrain_shadow_draw.inc"
};
int main(){
    unsigned cases=0;
    for(unsigned count:{3u,6u})for(unsigned fail=0;fail<=4;++fail)for(bool claimed:{false,true})for(HRESULT drawResult:{0l,-7l}){
        Shader v,p,newV,newP;DeviceState state;state.vertex=&v;state.pixel=&p;state.failAt=fail;
        for(unsigned i=0;i<state.constants.size();++i)state.constants[i]=float(i)*.03125f;
        const auto before=state.constants;
        Hook hook;hook.ext=&state;hook.shadowSwapOriginal=&p;hook.shadowSwapReplacement=&newP;hook.shadowSwapVertexOriginal=&v;hook.shadowSwapVertexReplacement=&newV;
        hook.shadowSwapConstantBase=224-count;hook.shadowSwapConstantCount=count;std::fill_n(hook.shadowSwapConstants,24,-10.f);
        unsigned draws=0;HRESULT result=hook.terrainShadowDraw(claimed,[&](){
            ++draws;
            if(fail){assert(state.pixel==&p&&state.vertex==&v&&state.constants==before);}
            else{assert(state.pixel==&newP&&state.vertex==&newV);for(unsigned i=0;i<count*4;++i)assert(state.constants[(224-count)*4+i]==-10.f);}
            return drawResult;
        });
        assert(draws==unsigned(!claimed)&&result==(claimed?D3D_OK:drawResult));
        assert(state.pixel==&p&&state.vertex==&v&&state.constants==before);
        assert(v.releases==1&&p.releases==1&&!newV.releases&&!newP.releases);
        assert(!hook.shadowSwapOriginal&&!hook.shadowSwapReplacement&&!hook.shadowSwapVertexOriginal&&!hook.shadowSwapVertexReplacement);++cases;
    }
    std::printf("PASS %u actual draw-wrapper state/refcount/failure/claimed cases\n",cases);
}
