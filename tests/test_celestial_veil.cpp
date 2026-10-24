// veil and glow CPU policy: gating, constants, wrap/normalised visibility
// and the recorded D3D call sequence of the veil draw. Native; no device/game.
#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
// Minimal D3D9 subset with the real d3d9types.h values.
using HRESULT=long;using DWORD=unsigned;using UINT=unsigned;using LONG=long;
inline bool SUCCEEDED(HRESULT h){return h>=0;}inline bool FAILED(HRESULT h){return h<0;}
using D3DRENDERSTATETYPE=unsigned;using D3DSAMPLERSTATETYPE=unsigned;using D3DPRIMITIVETYPE=unsigned;
constexpr DWORD FALSE=0,TRUE=1;
constexpr D3DRENDERSTATETYPE D3DRS_ZENABLE=7,D3DRS_ZWRITEENABLE=14,D3DRS_ALPHATESTENABLE=15,D3DRS_SRCBLEND=19,D3DRS_DESTBLEND=20,D3DRS_CULLMODE=22,
    D3DRS_ALPHABLENDENABLE=27,D3DRS_FOGENABLE=28,D3DRS_STENCILENABLE=52,D3DRS_CLIPPLANEENABLE=152,D3DRS_COLORWRITEENABLE=168,D3DRS_BLENDOP=171,
    D3DRS_SCISSORTESTENABLE=174,D3DRS_SRGBWRITEENABLE=194,D3DRS_SEPARATEALPHABLENDENABLE=206;
constexpr DWORD D3DBLEND_ONE=2,D3DBLEND_SRCALPHA=5,D3DBLEND_INVDESTCOLOR=10,D3DBLENDOP_ADD=1,D3DCULL_NONE=1,D3DTADDRESS_CLAMP=3,D3DTEXF_NONE=0,D3DTEXF_POINT=1,D3DTEXF_LINEAR=2;
constexpr D3DSAMPLERSTATETYPE D3DSAMP_ADDRESSU=1,D3DSAMP_ADDRESSV=2,D3DSAMP_MAGFILTER=5,D3DSAMP_MINFILTER=6,D3DSAMP_MIPFILTER=7,D3DSAMP_SRGBTEXTURE=11;
constexpr DWORD D3DFVF_XYZRHW=4,D3DFVF_TEX1=0x100;constexpr D3DPRIMITIVETYPE D3DPT_TRIANGLESTRIP=5;
struct RECT{LONG left,top,right,bottom;};
struct D3DVIEWPORT9{DWORD X,Y,Width,Height;float MinZ,MaxZ;};
template<class T>void drop(T*& p){if(p)p->Release();p=nullptr;}
#include "celestial_glow.h"
#include "celestial_veil.h"
#include "apparent_horizon.h"
using namespace NorthlightCelestialGlow;

struct Surface{int references=1;void Release(){--references;}};
struct Texture{};struct Shader{};
struct Device{
    Surface* bound=nullptr;std::vector<std::string> calls;DWORD states[256]={};RECT scissor={};
    unsigned low=0,high=0,draws=0;float c47[4]={};
    HRESULT GetRenderTarget(DWORD,Surface** out){calls.push_back("GetRenderTarget");if(bound)++bound->references;*out=bound;return 0;}
    HRESULT SetRenderTarget(DWORD,Surface* s){calls.push_back("SetRenderTarget");bound=s;return 0;}
    void SetViewport(const D3DVIEWPORT9*){calls.push_back("SetViewport");}
    void SetVertexShader(void*){calls.push_back("SetVertexShader");}
    void SetFVF(DWORD){calls.push_back("SetFVF");}
    void SetPixelShader(Shader*){calls.push_back("SetPixelShader");}
    void SetRenderState(D3DRENDERSTATETYPE s,DWORD v){calls.push_back("SetRenderState");states[s]=v;}
    void SetTexture(DWORD,Texture*){calls.push_back("SetTexture");}
    void SetSamplerState(DWORD,D3DSAMPLERSTATETYPE,DWORD){calls.push_back("SetSamplerState");}
    void SetScissorRect(const RECT* r){calls.push_back("SetScissorRect");scissor=*r;}
    HRESULT SetPixelShaderConstantF(UINT start,const float* c,UINT count){calls.push_back("SetPixelShaderConstantF");
        if(start==0)low=count;else{assert(start==46);high=count;for(int k=0;k<4;++k)c47[k]=c[4+k];}return 0;}
    HRESULT DrawPrimitiveUP(D3DPRIMITIVETYPE type,UINT count,const void*,UINT stride){calls.push_back("DrawPrimitiveUP");assert(type==D3DPT_TRIANGLESTRIP&&count==2&&stride==24);++draws;return 0;}
    unsigned count(const char* name)const{unsigned n=0;for(auto& c:calls)n+=c==name;return n;}
};

int main(){
    // Gating: the veil strength folds opacity, the edge fade and the horizon
    // haze at the sun; 0 means no draw call at all.
    assert(std::fabs(veilStrength(1,1,0)-VeilStrength)<1e-6f); // scales both glare weights
    assert(veilStrength(1,1,1)==0&&veilStrength(0,1,0)==0&&veilStrength(1,0,0)==0);
    assert(veilStrength(.001f,1,0)==0); // below 1/512: skipped
    assert(std::fabs(veilStrength(1,1,.5f)-.5f*veilStrength(1,1,0))<1e-6f);
    assert(std::fabs(veilStrength(1,1,0,.4f)-.4f*veilStrength(1,1,0))<1e-6f&&veilStrength(1,1,0,0)==0);
    // Horizon haze: the WorldComposite formula at the sun's elevation.
    const float tau=.7f*1.44269504f,band=1.44269504f/std::sin(6*3.14159265f/180);
    assert(hazeAtSun(0,band,.1f)==0&&hazeAtSun(tau,band,NAN)==0);
    assert(std::fabs(hazeAtSun(tau,band,0)-(1-std::exp(-.7f)))<1e-5f);
    assert(hazeAtSun(tau,band,-.2f)==hazeAtSun(tau,band,0)); // below the horizon: full band
    assert(hazeAtSun(tau,band,.05f)<hazeAtSun(tau,band,0)&&hazeAtSun(tau,band,.7f)<.002f);
    // Occlusion = max(disc, wrap x ring): trunk (disc hidden, ring visible)
    // keeps a wrapped glow; roof or hill (both hidden) none.
    assert(std::fabs(occlusion(0,1,Wrap)-Wrap)<1e-7f&&occlusion(0,0,Wrap)==0&&occlusion(1,1,Wrap)==1&&occlusion(.3f,1,0)==.3f);
    // Ring taps: equal-area 2-4R annulus, inside the 20R sun mask.
    float mx=0,my=0;
    for(unsigned i=0;i<NorthlightCelestialHalo::Samples;++i){float x,y;ringOffset(i,x,y);const float r=std::sqrt(x*x+y*y);
        assert(r>RingInner&&r<RingOuter&&r<SunMaskRadius);mx+=x;my+=y;}
    assert(std::fabs(mx/32)<.12f&&std::fabs(my/32)<.12f);
    assert(RingOuter<=SunMaskRadius&&SunMaskRadius>=SunGlareSupport&&MoonMaskRadius>=MoonGlareSupport);
    // On-screen normalisation: the unchanged shader's 1/32 mean equals the
    // mean over the taps on screen; every on-screen tap keeps an even share.
    for(unsigned onScreen:{0u,1u,7u,16u,20u,31u,32u}){
        float taps[32][4];
        for(unsigned i=0;i<32;++i){taps[i][0]=i<onScreen?float(i)/64:-1;taps[i][1]=taps[i][0];taps[i][2]=taps[i][3]=float(i);}
        const float fraction=normalizeOnScreen(taps,32);
        assert(fraction==float(onScreen)/32);
        unsigned uses[32]={};
        for(unsigned s=0;s<32;++s){
            if(!onScreen){assert(taps[s][0]<0);continue;}
            assert(taps[s][0]>=0);const unsigned from=unsigned(taps[s][2]);assert(from<onScreen&&taps[s][0]==float(from)/64);++uses[from];
        }
        for(unsigned i=0;i<onScreen;++i)assert(uses[i]>=32/onScreen&&uses[i]<=32/onScreen+1);
    }
    // Screen-edge fade: by the disc centre's distance outside the screen
    // (continuous, 1R..6R), filtered 60/140 ms like the halo; no pop.
    assert(edgeFade(0)==1&&edgeFade(1)==1&&edgeFade(6)==0&&edgeFade(1e9f)==0);
    {float last=1;for(int i=0;i<=700;++i){const float f=edgeFade(i*.01f);assert(f<=last&&last-f<.004f);last=f;}}
    assert(outsideRadii(true,100,100,23,1728,1117)==0&&outsideRadii(false,100,100,23,1728,1117)>1e8f);
    assert(std::fabs(outsideRadii(true,-46,500,23,1728,1117)-2)<1e-6f&&std::fabs(outsideRadii(true,1728+69,1117+92,23,1728,1117)-5)<1e-5f);
    {   // A camera turn at 30 deg/s moves the sun ~23 px (1R) per 43 ms: the
        // filtered fade changes by < .1 per 60 fps frame, never in one step.
        Fade f;float value=f.update(1000,0,1);assert(value==1);
        for(unsigned i=1;i<=60;++i){const float target=edgeFade(1+5*float(i)/60);const float next=f.update(1000+i*16,i,target);assert(next<=value&&value-next<.1f&&next>=target);value=next;}
        for(unsigned i=61;i<=120;++i)value=f.update(1000+i*16,i,0);assert(value<.001f);
        assert(f.update(5000,500,.7f)==.7f); // stale/skipped: starts at the target
        f.zero(6000,600);assert(f.value==0);const float rise=f.update(6016,601,1);assert(rise>0&&rise<.15f); // unknown visibility fades in
    }
    // Constants: hue/mix (c47), hot core/wrap (c48); the moon has no ring.
    Hue hue;hue.sun[0]=1;hue.sun[1]=.3f;hue.sun[2]=0;hue.sunCore[0]=1;hue.sunCore[1]=.93f;hue.sunCore[2]=.78f;hue.strength=2;
    float c47[4],c48[4];constants(hue,0,Wrap,c47,c48);
    assert(c47[0]==1&&c47[1]==.3f&&c47[3]==1&&c48[1]==.93f&&c48[3]==Wrap);
    constants(hue,1,Wrap,c47,c48);assert(c48[3]==0&&c47[0]==hue.moon[0]&&c48[2]==hue.moon[2]&&c47[3]==0); // moon: its own mix
    hue.moonStrength=.5f;constants(hue,1,Wrap,c47,c48);assert(c47[3]==.5f);
    constants(Hue{},0,0,c47,c48);assert(c47[3]==0&&c48[3]==0); // mix 0: the profile disc tint (today)
    // Profiles (0.3.164): a small hot core (x >= 1 only to ~1.6R) and a long
    // tinted tail to 20R; the output is 1-exp(-x): no plateau or clip rim. A low
    // sun widens only the tail, continuously in elevation.
    float last=1e9f;
    for(int i=0;i<=2000;++i){const float v=sunProfile(i*.01f);assert(v<=last);last=v;}
    assert(sunProfile(1.5f)>1&&sunProfile(2)<1&&sunProfile(4)<.7f&&sunProfile(10)>.2f&&sunProfile(20)==0);
    assert(sunProfile(4,SunTailWeightLow)>sunProfile(4)&&sunProfile(0,SunTailWeightLow)-sunProfile(0)<1);
    assert(lowSun(.5f)==0&&lowSun(LowSunStart)==0&&lowSun(LowSunEnd)==1&&lowSun(-.1f)==1&&lowSun(NAN)==0);
    assert(sunTailWeight(.5f)==SunTailWeight&&sunTailWeight(0)==SunTailWeightLow);
    {float previous=sunTailWeight(.3f);for(int i=0;i<=300;++i){const float t=sunTailWeight(.3f-i*.001f);assert(t>=previous-1e-6f&&t-previous<.02f);previous=t;}} // continuous in elevation
    {   // Soft shoulder: never reaches 1, monotonic in x, identity at 0.
        float prev=.4f;for(int i=1;i<=400;++i){const float v=shoulder(.4f,i*.02f,1);assert(v>prev&&v<1);prev=v;}assert(std::fabs(shoulder(.4f,0,1)-.4f)<1e-6f);
        // Sampled every .25R to 12R over a green sky: luminance monotonic, no second-difference spike.
        float lum[49];const float sky[3]={.3f,.5f,.4f},hue[3]={.76f,1,.64f},core[3]={.95f,1,.74f};
        for(int i=0;i<49;++i){const float r=i*.25f,x=sunProfile(r),w=coreWeight(r);float l=0;const float weight[3]={.2126f,.7152f,.0722f};
            for(int k=0;k<3;++k)l+=weight[k]*shoulder(sky[k],x,hue[k]+(core[k]-hue[k])*w);lum[i]=l;}
        for(int i=1;i<49;++i)assert(lum[i]<=lum[i-1]+1e-6f);
        for(int i=7;i<48;++i)assert(std::fabs(lum[i+1]-2*lum[i]+lum[i-1])<.012f);
    }
    {   // disc hidden (a near mountain), ring visible -> the core is NEVER
        // drawn; only the tail shifted out by 3R wraps (a soft plateau at the
        // tail's 3R level). Disc visible -> unchanged.
        for(int i=0;i<=48;++i){const float r=i*.25f,hidden=sunGlare(r,0,1,Wrap),visible=sunGlare(r,1,0,Wrap);
            const float tailOnly=SunTailWeight*sunTail(std::sqrt(r*r+9))*Wrap*(1-smoothstep(7,20,r));
            assert(std::fabs(hidden-tailOnly)<1e-6f);                       // no core term at any radius
            assert(hidden<=SunTailWeight*Wrap*sunTail(3)+1e-6f);            // never above the wrapped tail's 3R level
            assert(std::fabs(visible-sunProfile(r))<1e-5f*std::max(1.f,visible));} // unobstructed: unchanged profile
        assert(sunGlare(0,0,1,Wrap)<.25f*sunGlare(0,1,0,Wrap));             // no core blob over the occluder
        assert(sunGlare(1,0,0,Wrap)==0&&sunGlare(8,0,0,Wrap)==0);           // fully hidden: nothing
        float prev=sunGlare(0,.4f,1,Wrap);for(int i=1;i<=1200;++i){const float v=sunGlare(i*.01f,.4f,1,Wrap);assert(std::fabs(v-prev)<.03f);prev=v;} // partly hidden: continuous, no step
        // Monotonic sweep: for every (disc, ring) visibility the profile never
        // rises with radius, so a barely hidden sun has no dark ring around it
        // (the former 1.5R..3R fade-in of the ring rose again after a dip at ~2R,
        // e.g. disc .19, ring .8).
        for(int a=0;a<=20;++a)for(int b=0;b<=20;++b){const float disc=a*.05f,ring=b*.05f;
            float last=sunGlare(0,disc,ring,Wrap);
            for(int i=1;i<=240;++i){const float v=sunGlare(i*.05f,disc,ring,Wrap);assert(v<=last+1e-6f);last=v;}}
    }
    {   // Glare colour: mix folded in on the CPU (fallback tint at mix 0); c47.w = tail, moon has none.
        Hue h;const float tint[3]={1,.96f,.88f};h.sun[0]=.76f;h.sun[1]=1;h.sun[2]=.64f;h.sunCore[0]=.95f;h.sunCore[1]=1;h.sunCore[2]=.74f;h.strength=1;
        float a[4],b[4];glareConstants(h,0,tint,SunTailWeight,Wrap,a,b);
        assert(a[0]==.76f&&a[1]==1&&a[3]==SunTailWeight&&b[0]==.95f&&b[3]==Wrap);
        h.strength=0;glareConstants(h,0,tint,SunTailWeight,0,a,b);assert(a[1]==.96f&&b[1]==.96f&&b[3]==0);
        h.strength=.5f;glareConstants(h,0,tint,1,0,a,b);assert(std::fabs(a[2]-(.88f+(.64f-.88f)*.5f))<1e-6f); // == shader lerp(tint,hue,mix)
        h.moonStrength=1;glareConstants(h,1,tint,SunTailWeight,Wrap,a,b);assert(a[0]==h.moon[0]&&b[2]==h.moon[2]&&a[3]==0&&b[3]==0);
    }
    assert(moonProfile(1)==.35f&&moonProfile(3)>.05f&&moonProfile(6)==0);
    assert(std::fabs(MoonGlareStrength*moonProfile(1.33f)-.27f)<.005f&&std::fabs(MoonGlareStrength*moonProfile(1.9f)-.175f)<.005f);
    assert(sunDiscAlpha(.44f)==1&&sunDiscAlpha(1)==0&&sunDiscAlpha(.7f)>0&&sunDiscAlpha(.7f)<1);
    assert(coreWeight(0)==1&&coreWeight(1)<.5f&&coreWeight(1.5f)<.1f&&coreWeight(2)<.01f); // the core's own falloff
    {   // Hot core (haze's Durotar values): core at the disc, hue outside, today's tint at strength 0.
        const float tint[3]={1,.96f,.88f},sun[3]={1,.57f,.24f},core[3]={1,.91f,.66f};float out[3];
        glowColour(tint,sun,core,1,0,out);for(int k=0;k<3;++k)assert(std::fabs(out[k]-core[k])<1e-6f);
        glowColour(tint,sun,core,1,6,out);for(int k=0;k<3;++k)assert(std::fabs(out[k]-sun[k])<1e-6f);
        glowColour(tint,sun,core,0,1,out);for(int k=0;k<3;++k)assert(out[k]==tint[k]);
        float last[3]={1,1,1};for(int i=0;i<=600;++i){glowColour(tint,sun,core,1,i*.01f,out);for(int k=0;k<3;++k){assert(out[k]<=last[k]+1e-6f);last[k]=out[k];}}
    }

    // Draw sequence: the target water used is still bound -> no SetRenderTarget
    // (no new render pass); one scissored additive draw; c0..c12 and c46..c48.
    Surface target,other;Texture depth,halo;Shader ps;Texture* const textures[NorthlightCelestialVeil::Samplers]={&depth,nullptr,nullptr,&halo,nullptr,&halo};
    float c[NorthlightCelestialVeil::Registers][4]={};c[12][2]=SunCoreWeight*veilStrength(1,1,0);c[47][3]=SunTailWeight*veilStrength(1,1,0);c[47][0]=.5f;
    const int bounds[4]={100,50,900,700};
    {
        Device d;d.bound=&target;NorthlightCelestialVeil::Stats stats;
        assert(NorthlightCelestialVeil::draw(&d,&target,&ps,textures,c,bounds,1728,1117,stats));
        assert(d.count("SetRenderTarget")==0&&stats.targetSwitches==0&&d.draws==1&&stats.draws==1&&d.count("DrawPrimitiveUP")==1);
        assert(d.calls.back()=="DrawPrimitiveUP"&&target.references==1);
        assert(d.states[D3DRS_ALPHABLENDENABLE]==TRUE&&d.states[D3DRS_SRCBLEND]==D3DBLEND_INVDESTCOLOR&&d.states[D3DRS_DESTBLEND]==D3DBLEND_ONE);
        assert(d.states[D3DRS_BLENDOP]==D3DBLENDOP_ADD&&d.states[D3DRS_ZENABLE]==FALSE&&d.states[D3DRS_ZWRITEENABLE]==FALSE);
        assert(d.states[D3DRS_SCISSORTESTENABLE]==TRUE&&d.states[D3DRS_COLORWRITEENABLE]==7&&d.states[D3DRS_SRGBWRITEENABLE]==FALSE);
        assert(d.scissor.left==100&&d.scissor.top==50&&d.scissor.right==900&&d.scissor.bottom==700);
        assert(d.low==13&&d.high==3&&d.c47[0]==.5f&&d.count("SetTexture")==6);
    }
    {   // Another target bound: exactly one switch, counted.
        Device d;d.bound=&other;NorthlightCelestialVeil::Stats stats;
        assert(NorthlightCelestialVeil::draw(&d,&target,&ps,textures,c,bounds,1728,1117,stats));
        assert(d.count("SetRenderTarget")==1&&stats.targetSwitches==1&&d.bound==&target&&other.references==1);
    }
    {   // Strength 0 (haze, horizon, effects gated by the caller) or empty scissor: no call at all.
        float off[NorthlightCelestialVeil::Registers][4]={};Device d;d.bound=&target;NorthlightCelestialVeil::Stats stats;
        assert(!NorthlightCelestialVeil::draw(&d,&target,&ps,textures,off,bounds,1728,1117,stats));
        const int empty[4]={10,10,10,20};
        assert(!NorthlightCelestialVeil::draw(&d,&target,&ps,textures,c,empty,1728,1117,stats));
        assert(d.calls.empty()&&stats.skipped==2&&stats.draws==0);
    }
    {   // Diffuse halo fades while the disc crosses the true horizon.
        const float r=.0211f;
        assert(ringWrap(-r,r)==0&&std::fabs(ringWrap(r,r)-Wrap)<1e-6f);
        assert(std::fabs(ringWrap(0,r)-Wrap*.5f)<1e-6f);
        assert(ringWrap(-.5f*r,r)>0&&ringWrap(.5f*r,r)<Wrap);
        assert(ringWrap(.5f,r)==Wrap&&ringWrap(NAN,r)==0&&ringWrap(.5f,0)==0);
        // A low, fully risen sun behind a wing keeps a broad halo. Previously
        // the unrelated 1R..3R gate removed 84% before any occlusion was measured.
        const float old=Wrap*smoothstep(r,3*r,1.5f*r);
        const float halo=sunGlare(3.f,0.f,1.f,ringWrap(1.5f*r,r));
        assert(halo>sunGlare(3.f,0.f,1.f,old)*6.f);
        assert(halo>sunGlare(3.f,1.f,1.f,Wrap)*.5f);
        assert(sunGlare(3.f,0.f,0.f,Wrap)==0); // a fully covered halo still goes dark
    }
    {   // The apparent horizon: the lowest surface under the eye and toward the sun, seen at the far clip; 0..-sin 3 deg.
        namespace H=NorthlightApparentHorizon;const float eye[3]={0,0,30},sun[3]={1,0,.1f};
        auto flat=[](float level){return [level](float,float,float& h){h=level;return true;};};
        const float dz=-30,expect=dz/std::sqrt(dz*dz+1000.f*1000.f);
        assert(std::fabs(H::sine(eye,sun,1000,flat(0))-expect)<1e-6f);
        assert(H::sine(eye,sun,1000,flat(200))==0&&H::sine(eye,sun,1000,[](float,float,float&){return false;})==0);
        assert(H::sine(eye,sun,100,flat(-5000))==-H::Deepest&&H::sine(eye,sun,NAN,flat(0))==0);
        float seen[3];unsigned n=0;H::sine(eye,sun,1000,[&](float x,float y,float& h){seen[n++]=x;assert(y==0);h=x>200?-50:0;return true;});
        assert(n==3&&seen[0]==0&&seen[1]==120&&seen[2]==240);
        assert(std::fabs(H::sine(eye,sun,1000,[](float x,float,float& h){h=x>200?-20:0;return true;})-(-50/std::sqrt(50.f*50+1e6f)))<1e-6f);
        H::Smooth s;assert(s.update(-.05f,0)==-.05f&&s.update(0,400)>-.05f*.37f-1e-6f&&s.update(0,400)<0);
        assert(s.update(-1,10000)==-H::Deepest&&s.update(NAN,10001)<0);
    }
    std::puts("PASS veil: gating (strength/haze/opacity/fade), haze-at-sun, wrap occlusion, ring taps in the 20R mask, on-screen normalisation 0..32 taps, hue constants, glare profiles, draw sequence (no RT switch when bound, 1 additive scissored draw, no call when off)");
}
