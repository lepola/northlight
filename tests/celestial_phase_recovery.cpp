// Executes the production claim method with controlled client reads and draw calls.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include "celestial_disc.h"
#include "celestial_glow.h"
using DWORD=std::uint32_t;using UINT=unsigned;using HRESULT=int;
enum D3DPRIMITIVETYPE {D3DPT_TRIANGLELIST=4,D3DPT_TRIANGLESTRIP=5,D3DPT_TRIANGLEFAN=6};
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
struct D3DVIEWPORT9 {DWORD X=0,Y=0,Width=1280,Height=720;float MinZ=.9990234375f,MaxZ=1;};
struct D3DSURFACE_DESC {UINT Width=1280,Height=720;};
struct IDirect3DBaseTexture9 {};
struct IDirect3DSurface9 {D3DSURFACE_DESC desc;HRESULT GetDesc(D3DSURFACE_DESC* out){*out=desc;return 0;}};
template<class T>void drop(T*& p){p=nullptr;}
void logf(const char*,...){}
static DWORD now=1000;DWORD GetTickCount(){return now;}
static bool cameraValid=true,skyValid=true;static float skyCover=0;static const char* currentMap="Northrend";
namespace NorthlightWorldCamera {
struct Camera {float camera[3]={},inverseView[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};};
struct Diagnostics {unsigned reason=0;};const char* rejectName(unsigned){return "fixture";}
bool readSkyPhase(char (&map)[64],float,Camera& camera,Diagnostics*){camera=Camera{};std::strcpy(map,currentMap);return cameraValid;}
}
namespace NorthlightCelestial {
struct Body {float direction[3]={0,0,1},tint[3]={1,1,1},alpha=1,angularRadius=.02f,emission=1;};
struct Context {Body sun,moon;float skyboxCover=0;bool nativePlanets=true;};
struct FrameMode {int mode=-1;bool skybox()const{return mode==1;}bool decide(const Context& c){if(mode<0)mode=(!c.nativePlanets||c.skyboxCover>.01f)?1:0;return mode==1;}void reset(){mode=-1;}};
bool read(const float*,const float*,Context& sky){sky=Context{};sky.skyboxCover=skyCover;return skyValid;}
void resolveRendererSky(Context&,const float*){}
bool skyboxVerified(){return true;}
}
namespace NorthlightCelestialProfiles {struct Profile {};void apply(Profile,const float*,NorthlightCelestial::Context&){} }
struct Device {
    IDirect3DBaseTexture9 texture;IDirect3DSurface9 target;bool targetValid=true;
    HRESULT GetTexture(unsigned,IDirect3DBaseTexture9** out){*out=&texture;return 0;}
    HRESULT GetViewport(D3DVIEWPORT9* out){*out=D3DVIEWPORT9{};return 0;}
    HRESULT GetRenderTarget(unsigned,IDirect3DSurface9** out){*out=targetValid?&target:nullptr;return targetValid?0:-1;}
};
struct Phase {
    Device device,*d=&device;struct Observer {int body=1;int claimIndex(IDirect3DBaseTexture9*){return body;}} observer;
    bool earlyValid=true,failed=false;unsigned frames=1,ownFrame=0;
    NorthlightCelestial::Context earlySkyHeld;std::string earlySkyMap;DWORD earlySkyAt=0;bool earlySkyOk=false;
    float earlyProjection[3]={1,1,1},earlyWorldMaxDepth=.94f;UINT earlyWidth=1280,earlyHeight=720;
    unsigned drawnEarly=0,drawnSkyboxEarly=0,offscreenOwned=0,claimedDraws=0,earlyFailures=0;
    unsigned claimGate=0,claimNoBody=0,claimCamera=0,claimOffscreen=0,claimDrawn=0,claimSecondary=0,claimSkyHeld=0;
    static constexpr float MoonGlareSupport=NorthlightCelestialGlow::MoonGlareSupport;
    NorthlightCelestialDisc::SkyBand skyBand;unsigned surfaceDraws=0,haloDraws=0;
    NorthlightCelestial::FrameMode skyMode;unsigned suppressed=0,suppressedDraws=0,renderFrame=~0u;
    bool skyboxFrame(const NorthlightCelestial::Context& c){return skyMode.decide(c);}
    bool claimSuppress(int body){if(renderFrame+1!=frames)return false;ownFrame=frames;suppressed|=1u<<body;++suppressedDraws;return true;}
    bool load(){return true;}
    bool prepareTerrain(unsigned,const NorthlightCelestialDisc::Disc&,const float*){return true;}
    bool drawDisc(unsigned,IDirect3DSurface9*,UINT,UINT,const float*,const float*,const NorthlightCelestialDisc::Disc&,const int*,
                  void*,void*,float,float,float,float,void*,bool,float,bool glare=false){if(glare)++haloDraws;else ++surfaceDraws;return true;}
#include "celestial_phase_methods.inc"
    bool claim(){return claimNativeDraw(D3DPT_TRIANGLELIST,2,true,[](const char*,const float*){return NorthlightCelestialProfiles::Profile{};});}
    void next(){++frames;drawnEarly=drawnSkyboxEarly=offscreenOwned=0;now+=16;skyMode.reset();suppressed=0;}
};
int main(){
    Phase p;
    for(unsigned frame=0;frame<120;++frame){assert(p.claim());assert(p.drawnEarly==3&&p.owning());p.next();}
    assert(p.surfaceDraws==240&&p.haloDraws==240); // no late world render during the sky-only run
    const unsigned draws=p.haloDraws;assert(p.claim()&&p.claim()&&p.haloDraws==draws+2); // one halo per body per frame
    p.next();skyValid=false;assert(p.claim()&&p.claimSkyHeld==1&&p.drawnEarly==3);
    p.next();now=p.earlySkyAt+2000;assert(p.claim()&&p.drawnEarly==3); // inclusive bounded hold
    p.next();now=p.earlySkyAt+2001;assert(!p.claim()&&p.drawnEarly==0); // restore natives, no stale placement
    skyValid=true;p.next();assert(p.claim());p.next();skyValid=false;currentMap="Azeroth";assert(!p.claim());
    currentMap="Northrend";cameraValid=false;assert(!p.claim());cameraValid=true;
    p.earlySkyOk=false; // reset() clears this production hold
    assert(!p.claim());
    skyValid=true;p.device.target.desc.Width=640;assert(!p.claim());p.device.target.desc.Width=1280;
    p.device.targetValid=false;assert(!p.claim());p.device.targetValid=true;
    p.earlyValid=false;assert(!p.claim());p.earlyValid=true;p.failed=true;assert(!p.claim());
    p.failed=false;skyValid=true;now=UINT32_MAX-20;p.next();assert(p.claim());
    skyValid=false;p.next();now=10;assert(p.claim()); // tick wrap keeps the same bounded interval
    // Skybox frame: the claim draws no early disc and suppresses the native body for the late disc (given a render() last frame).
    skyValid=true;skyCover=.5f;p.next();p.renderFrame=p.frames-1;
    const unsigned surfaces=p.surfaceDraws,halos=p.haloDraws;
    p.observer.body=0;assert(p.claim()&&p.drawnEarly==0&&p.suppressed==1&&p.suppressedDraws==1);
    p.observer.body=1;skyCover=0;assert(p.claim()&&p.drawnEarly==0&&p.suppressed==3); // the first read decided: the frame stays a skybox frame
    assert(p.surfaceDraws==surfaces&&p.haloDraws==halos);
    p.next();p.renderFrame=~0u;p.observer.body=1;skyCover=.5f;assert(!p.claim()&&p.suppressed==0&&p.drawnEarly==0); // no late render yet: the native draw stays
    p.next();skyCover=0;p.renderFrame=p.frames-1;assert(p.claim()&&p.drawnEarly==3&&p.suppressed==0); // skybox gone: planet phase again
    // A verified cloud-layer trigger must repaint after a planet-phase disc covered by a later skybox.
    p.next();skyCover=1;p.drawnEarly=1;p.renderFrame=p.frames-1;
    const unsigned before=p.surfaceDraws;
    assert(p.claimNativeDraw(D3DPT_TRIANGLELIST,768,true,[](const char*,const float*){return NorthlightCelestialProfiles::Profile{};},true));
    assert(p.drawnSkyboxEarly==3&&p.surfaceDraws==before+2);
    assert(p.claimNativeDraw(D3DPT_TRIANGLELIST,768,true,[](const char*,const float*){return NorthlightCelestialProfiles::Profile{};},true));
    assert(p.surfaceDraws==before+2);
    std::puts("PASS celestial phase: 120 sky-only frames, one draw per body, transient sky hold, expiry/map/camera/reset/target gates, clock wrap");
}
