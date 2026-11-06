#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>
using HRESULT=long;
constexpr HRESULT D3D_OK=0;
#define SUCCEEDED(x) ((x)>=0)
struct Shader {int releases=0;void Release(){++releases;}};
using IDirect3DVertexShader9=Shader;
using IDirect3DPixelShader9=Shader;
struct CpuScope {explicit CpuScope(int){}};
struct SwapCalls {unsigned getRequests=0,getNative=0,setVS=0,setPS=0,setPSConstants=0,constantRegisters=0,failures=0;};
struct DeviceState{
    Shader* vertex=nullptr;Shader* pixel=nullptr;std::array<float,896> constants{};
    unsigned calls=0,failAt=0;bool cachedGet=false;std::uint64_t* forwarded=nullptr;std::vector<unsigned> trace;
    bool fail(unsigned api){trace.push_back(api);return ++calls==failAt;}
    HRESULT GetPixelShaderConstantF(unsigned base,float* out,unsigned count){if(!cachedGet)++*forwarded;if(fail(1))return -1;std::memcpy(out,constants.data()+base*4,count*16);return 0;}
    HRESULT SetPixelShaderConstantF(unsigned base,const float* in,unsigned count){if(fail(4))return -1;std::memcpy(constants.data()+base*4,in,count*16);return 0;}
    HRESULT SetVertexShader(Shader* shader){if(fail(2))return -1;vertex=shader;return 0;}
    HRESULT SetPixelShader(Shader* shader){if(fail(3))return -1;pixel=shader;return 0;}
};
struct Hook{
    DeviceState* ext=nullptr;
    bool gateFrame=false;struct {std::uint64_t forwarded=0;} mirrorState;SwapCalls terrainSwapCalls;
    Shader *shadowSwapOriginal=nullptr,*shadowSwapReplacement=nullptr,*shadowSwapVertexOriginal=nullptr,*shadowSwapVertexReplacement=nullptr;
    unsigned shadowSwapConstantBase=0,shadowSwapConstantCount=0,terrainShadowDraws=0;
    float shadowSwapConstants[24]={};
    int sampledHookTimer(){return 0;}
    template<class Draw> HRESULT materialNativeDraw(Draw draw,Shader* =nullptr,Shader* =nullptr){return draw();}
    // Exact current production method, extracted by the Python driver.
#include "terrain_shadow_draw.inc"
};
void checkCalls(const Hook& hook,const DeviceState& state,unsigned count,const std::vector<unsigned>& expected){
    assert(state.trace==expected&&state.calls==expected.size());
    const auto& c=hook.terrainSwapCalls;
    auto attempts=[&](unsigned api){return hook.gateFrame?unsigned(std::count(expected.begin(),expected.end(),api)):0u;};
    assert(c.getRequests==attempts(1)&&c.getNative==(state.cachedGet?0:attempts(1)));
    assert(c.setVS==attempts(2)&&c.setPS==attempts(3)&&c.setPSConstants==attempts(4));
    assert(c.constantRegisters==attempts(4)*count);
    assert(c.failures==unsigned(hook.gateFrame&&state.failAt&&state.failAt<=state.calls));
}
int main(){
    unsigned cases=0;
    for(unsigned count:{3u,6u})for(unsigned fail=0;fail<=7;++fail)for(bool claimed:{false,true})for(HRESULT drawResult:{0l,-7l})for(bool profile:{false,true})for(bool cached:{false,true}){
        if(cached&&fail==1)continue; // A cached success cannot simulate a native getter failure.
        Shader v,p,newV,newP;DeviceState state;state.vertex=&v;state.pixel=&p;state.failAt=fail;state.cachedGet=cached;
        for(unsigned i=0;i<state.constants.size();++i)state.constants[i]=float(i)*.03125f;
        const auto before=state.constants;
        Hook hook;hook.ext=&state;hook.gateFrame=profile;state.forwarded=&hook.mirrorState.forwarded;
        hook.shadowSwapOriginal=&p;hook.shadowSwapReplacement=&newP;hook.shadowSwapVertexOriginal=&v;hook.shadowSwapVertexReplacement=&newV;
        hook.shadowSwapConstantBase=224-count;hook.shadowSwapConstantCount=count;std::fill_n(hook.shadowSwapConstants,24,-10.f);
        unsigned draws=0;HRESULT result=hook.terrainShadowDraw(claimed,[&](){
            ++draws;
            if(fail&&fail<=4){assert(state.pixel==&p&&state.vertex==&v&&state.constants==before);}
            else{assert(state.pixel==&newP&&state.vertex==&newV);for(unsigned i=0;i<count*4;++i)assert(state.constants[(224-count)*4+i]==-10.f);}
            return drawResult;
        });
        assert(draws==unsigned(!claimed)&&result==(claimed?D3D_OK:drawResult));
        assert(state.pixel==(!claimed&&fail==6?&newP:&p));
        assert(state.vertex==(!claimed&&fail==7?&newV:&v));
        auto after=before;if(!claimed&&fail==5)std::fill_n(after.data()+(224-count)*4,count*4,-10.f);
        assert(state.constants==after);
        assert(v.releases==1&&p.releases==1&&!newV.releases&&!newP.releases);
        assert(!hook.shadowSwapOriginal&&!hook.shadowSwapReplacement&&!hook.shadowSwapVertexOriginal&&!hook.shadowSwapVertexReplacement);
        std::vector<unsigned> trace;
        if(!claimed){if(fail==1)trace={1};else if(fail==2)trace={1,2,4,3,2};else if(fail==3)trace={1,2,3,4,3,2};else trace={1,2,3,4,4,3,2};}
        checkCalls(hook,state,count,trace);++cases;
    }
    for(unsigned fail=0;fail<=2;++fail)for(bool profile:{false,true})for(bool claimed:{false,true}){
        Shader p,newP;DeviceState state;state.pixel=&p;state.failAt=fail;
        Hook hook;hook.ext=&state;hook.gateFrame=profile;state.forwarded=&hook.mirrorState.forwarded;hook.shadowSwapOriginal=&p;hook.shadowSwapReplacement=&newP;
        unsigned draws=0;const auto hr=hook.terrainShadowDraw(claimed,[&]{++draws;assert(state.pixel==(fail==1?&p:&newP));return -7l;});
        assert(draws==unsigned(!claimed)&&hr==(claimed?D3D_OK:-7l));
        assert(state.pixel==(!claimed&&fail==2?&newP:&p)&&p.releases==1&&!newP.releases);
        assert(!hook.shadowSwapOriginal&&!hook.shadowSwapReplacement);
        checkCalls(hook,state,0,claimed?std::vector<unsigned>{}:std::vector<unsigned>{3,3});++cases;
    }
    for(bool planned:{false,true})for(bool profile:{false,true})for(bool claimed:{false,true}){
        Shader v,p;DeviceState state;state.vertex=&v;state.pixel=&p;
        Hook hook;hook.ext=&state;hook.gateFrame=profile;state.forwarded=&hook.mirrorState.forwarded;
        if(planned){hook.shadowSwapOriginal=&p;hook.shadowSwapVertexOriginal=&v;}
        unsigned draws=0;const auto hr=hook.terrainShadowDraw(claimed,[&]{++draws;assert(state.vertex==&v&&state.pixel==&p);return -7l;});
        assert(draws==unsigned(!claimed)&&hr==(claimed?D3D_OK:-7l));assert(v.releases==int(planned)&&p.releases==int(planned));
        checkCalls(hook,state,0,{});++cases;
    }
    std::printf("PASS %u actual draw-wrapper state/refcount/failure/claimed/profile/native-call cases\n",cases);
}
