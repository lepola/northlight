#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <functional>
#include "celestial_terrain.h"
#include "diagnostics_switch.h"
#include "world_camera.h"
// Include after SavedState/drop/logf. Validated native body draws are replaced.
#include "celestial_disc.h"
#include "celestial_disc_native.h"
#include "celestial_glare_native.h"
#include "celestial_context.h"
#include "celestial_profiles.h"
#include "celestial_halo_visibility.h"
#include "celestial_glow.h"
#include "celestial_veil.h"
#include "celestial_disc_compiled_shaders.h"
class NorthlightCelestialDiscRenderer {
    // Terrain-mask extent (mask, prepare gate, c46.y) and glare support (scissor, c12.y) differ since r43.
    static constexpr float SunMaskRadius=NorthlightCelestialGlow::SunMaskRadius,MoonMaskRadius=NorthlightCelestialGlow::MoonMaskRadius;
    static constexpr float SunGlareSupport=NorthlightCelestialGlow::SunGlareSupport,MoonGlareSupport=NorthlightCelestialGlow::MoonGlareSupport;
    IDirect3DDevice9* d;std::string root;NorthlightCelestialDisc::NativeObserver observer;
    IDirect3DTexture9* texture[2]={};IDirect3DPixelShader9* ps=nullptr;
    IDirect3DTexture9* terrainMask[2]={};IDirect3DSurface9* terrainSurface[2]={};
    IDirect3DVertexShader9* terrainVS=nullptr;IDirect3DPixelShader9* terrainPS=nullptr;
    bool terrainReady[2]={};uint64_t terrainGeneration[2]={};float terrainMatrix[2][16]={};
    std::function<uint64_t()> terrainEpoch;
    std::function<bool(unsigned,const float*)> terrainDraw; /* (body, matrix) */
    std::function<void(unsigned)> terrainReuse; /* body: the mask was reused (diagnostics) */
    // The mask is centered on the body, not the camera's look direction. Exact
    // camera/body/mesh keys allow reuse across camera rotation without lag.
    bool prepareTerrain(unsigned body,const NorthlightCelestialDisc::Disc& disc,const float* view){
        terrainReady[body]=false;
        const uint64_t generation=terrainEpoch?terrainEpoch():0;
        if(!generation||!terrainDraw)return true;
        const unsigned size=body?1024:2048;
        float matrix[16];NorthlightCelestialTerrain::matrix(disc,view+12,body?MoonMaskRadius:SunMaskRadius,size,matrix);
        if(terrainMask[body]&&terrainGeneration[body]==generation&&!std::memcmp(matrix,terrainMatrix[body],sizeof matrix)){
            terrainReady[body]=true;if(terrainReuse)terrainReuse(body);return true;
        }
        SavedState saved(d);if(!saved.ok)return false;
        if(!terrainVS&&FAILED(d->CreateVertexShader(kCelestialTerrainVSShader,&terrainVS)))return false;
        if(!terrainPS&&FAILED(d->CreatePixelShader(kCelestialTerrainPSShader,&terrainPS)))return false;
        if(!terrainMask[body]){
            if(FAILED(d->CreateTexture(size,size,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&terrainMask[body],nullptr)))return false;
        }
        if(!terrainSurface[body]&&FAILED(terrainMask[body]->GetSurfaceLevel(0,&terrainSurface[body])))return false;
        terrainGeneration[body]=0; // a failed draw must not publish a partial mask
        d->SetDepthStencilSurface(nullptr);
        for(unsigned k=0;k<16;++k)d->SetTexture(k,nullptr);
        for(unsigned k=1;k<4;++k)d->SetRenderTarget(k,nullptr);
        if(FAILED(d->SetRenderTarget(0,terrainSurface[body])))return false;
        D3DVIEWPORT9 vp={0,0,size,size,0,1};d->SetViewport(&vp);
        const struct{D3DRENDERSTATETYPE s;DWORD v;} states[]={
            {D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},
            {D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_COLORWRITEENABLE,15},
            {D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},
            {D3DRS_CLIPPLANEENABLE,0},{D3DRS_CLIPPING,TRUE},{D3DRS_SRGBWRITEENABLE,FALSE},
            {D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_MULTISAMPLEMASK,0xffffffff}};
        for(const auto& s:states)d->SetRenderState(s.s,s.v);
        if(FAILED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0)))return false;
        // World pages retain their 32-byte stride; this declaration consumes
        // only position. Every covered fragment writes 1, so no depth target,
        // sorting, texture fetch or CPU readback is necessary for the union.
        d->SetFVF(D3DFVF_XYZ);
        for(unsigned k=0;k<4;++k)d->SetStreamSourceFreq(k,1);
        if(FAILED(d->SetVertexShader(terrainVS))||FAILED(d->SetPixelShader(terrainPS))||
           FAILED(d->SetVertexShaderConstantF(0,matrix,4))||!terrainDraw(body,matrix))return false;
        std::memcpy(terrainMatrix[body],matrix,sizeof matrix);terrainGeneration[body]=generation;
        terrainReady[body]=true;return true;
    }
    IDirect3DTexture9* haloVisibility[2][2]={};IDirect3DSurface9* haloSurface[2][2]={};
    IDirect3DPixelShader9* haloPS=nullptr;unsigned haloIndex[2]={};
    NorthlightCelestialHalo::History haloHistory[2];float haloDay=0;
    // wrap ring (sun only): the unchanged halo PS on 2-4R taps, its own 1x1 ping-pong.
    IDirect3DTexture9* ringVisibility[2]={};IDirect3DSurface9* ringSurface[2]={};unsigned ringIndex=0;
    NorthlightCelestialHalo::History ringHistory;bool ringWritten=false;unsigned ringFrame=0;
    IDirect3DPixelShader9* veilPS=nullptr;
    // 0.3.198 (rain): detected rain or snow dims the sun and moon: disc opacity and the sun glare weights (which the veil inherits) x
    // weatherEffects().discGain() = 1 - 0.85 f, set once per frame before render() (one frame late for the sky-phase disc); exactly 1 when dry.
    std::atomic<float> weatherGain{1.f};
public:
    void setWeatherGain(float g){weatherGain.store(std::isfinite(g)?std::clamp(g,0.f,1.f):1.f,std::memory_order_relaxed);}
private:
    NorthlightCelestialGlow::Hue glow; // the last render()'s hue; the sky-phase disc uses it one frame later
    // This frame's sun glare, kept for the ring pass and the veil (same placement and constants).
    struct Glare {bool valid=false;NorthlightCelestialDisc::Disc disc{};float projection[3]={},inverseView[16]={},constants[NorthlightCelestialVeil::Registers][4]={};
        int bounds[4]={};IDirect3DTexture9 *depth=nullptr,*water=nullptr;UINT width=0,height=0;float fade=0;} sunGlare;
    // Screen-edge fade per body, and whether the halo value is a live measurement to hold.
    NorthlightCelestialGlow::Fade edge[2];bool haloWritten[2]={};unsigned haloFrame[2]={};
    bool loaded=false,failed=false;unsigned frames=0,drawn=0;
    NorthlightCelestialDisc::Placement earlyPlacement[2];
    NorthlightCelestialDisc::SkyBand skyBand;
    bool glareRead=false;unsigned drawnLate=0,offscreenOwned=0,glareClaims=0;NorthlightCelestialDisc::Identities glareIdentities;
    // native body draws suppressed this and the previous frame (sun 1,
    // moon 2). The late disc owns a suppressed body (not "observed", so
    // canFallback stays true); the native glare is suppressed for owned bodies.
    unsigned suppressed=0,suppressedPrevious=0,suppressedDraws=0,suppressedGlares=0,ownFrame=~0u;
    // Owned = the previous frame's render() ran with a valid sky context (so the
    // late discs are live). World debug and F10 skip render(): natives return.
    bool owning()const{return ownFrame+1==frames;}
    static constexpr bool EarlyDisc=false; // F1 (the early sky-phase disc) stays off: suppression only
    bool check(HRESULT hr,const char* stage){if(SUCCEEDED(hr))return true;if(!failed)logf("CELESTIAL disabled stage=%s hr=%08lx",stage,(unsigned long)hr);failed=true;return false;}
    bool load(){if(loaded)return true;if(failed)return false;loaded=observer.load(root);if(!loaded){failed=true;logf("CELESTIAL native texture identity signature gate unavailable");}return loaded;}
    bool resources(){
        if(!load())return false;if(!ps&&!check(d->CreatePixelShader(kCelestialDiscPSShader,&ps),"shader"))return false;
        for(unsigned body=0;body<2;++body)if(!texture[body]){
            NorthlightCelestialDisc::Texture source;if(!NorthlightCelestialDisc::loadTexture(root+(body?"/moon.fct":"/sun.fct"),source)){failed=true;return false;}
            if(body==1){NorthlightCelestialDisc::softenMoonSurface(source);NorthlightCelestialDisc::softenMoonRim(source);}
            if(!check(d->CreateTexture(source.width,source.height,1,0,D3DFMT_A8R8G8B8,D3DPOOL_MANAGED,&texture[body],nullptr),"body texture"))return false;
            D3DLOCKED_RECT lock={};if(!check(texture[body]->LockRect(0,&lock,nullptr,0),"body lock"))return false;
            bool valid=lock.pBits&&lock.Pitch>=INT(source.width*4);
            if(valid)for(unsigned y=0;y<source.height;++y)for(unsigned x=0;x<source.width;++x){const auto* p=source.rgba.data()+(y*source.width+x)*4;auto* q=static_cast<unsigned char*>(lock.pBits)+std::size_t(y)*unsigned(lock.Pitch)+x*4;q[0]=p[2];q[1]=p[1];q[2]=p[0];q[3]=p[3];}
            if(!check(texture[body]->UnlockRect(0),"body unlock")||!valid){failed=true;return false;}
        }
        return true;
    }
    bool haloResources(){
        if(!haloPS&&!check(d->CreatePixelShader(kCelestialHaloVisibilityPSShader,&haloPS),"halo visibility shader"))return false;
        for(unsigned body=0;body<2;++body)for(unsigned j=0;j<2;++j)if(!haloVisibility[body][j]){
            if(!check(d->CreateTexture(1,1,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&haloVisibility[body][j],nullptr),"halo history texture")||
               !check(haloVisibility[body][j]->GetSurfaceLevel(0,&haloSurface[body][j]),"halo history surface"))return false;
        }
        return true;
    }
    bool ringResources(){
        for(unsigned j=0;j<2;++j)if(!ringVisibility[j]){
            if(!check(d->CreateTexture(1,1,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&ringVisibility[j],nullptr),"ring history texture")||
               !check(ringVisibility[j]->GetSurfaceLevel(0,&ringSurface[j]),"ring history surface"))return false;
        }
        return true;
    }
    // Disc centre outside the screen, in projected disc radii (0 on screen).
    static float centreOutside(const NorthlightCelestialDisc::Disc& disc,const float* inverseView,const float* projection,UINT width,UINT height){
        float centre[2],rim[2];bool front=true;
        for(unsigned tap=0;tap<2;++tap){
            float v[3]={};for(unsigned row=0;row<3;++row)for(unsigned k=0;k<3;++k)v[row]+=(disc.direction[k]+(tap?disc.tangentRadius*disc.right[k]:0.f))*inverseView[row*4+k];
            const float forward=v[2]*projection[2];if(forward<=.000001f){front=false;break;}
            float* out=tap?rim:centre;out[0]=(v[0]*projection[0]/forward*.5f+.5f)*width;out[1]=(.5f-v[1]*projection[1]/forward*.5f)*height;
        }
        if(!front)return 1e9f;
        return NorthlightCelestialGlow::outsideRadii(true,centre[0],centre[1],std::hypot(rim[0]-centre[0],rim[1]-centre[1]),float(width),float(height));
    }
    // Source-visibility taps c13..c44: xy scene UV (-1 off screen), zw terrain-mask UV.
    // Off-screen slots are refilled from on-screen taps; returns the on-screen fraction.
    static float sourceTaps(const NorthlightCelestialDisc::Disc& disc,const float* inverseView,const float* projection,UINT width,UINT height,
                            float maskRadius,bool ring,float (*c)[4]){
        for(unsigned sample=0;sample<NorthlightCelestialHalo::Samples;++sample){
            float ray[3],v[3]={},ox,oy;
            if(ring)NorthlightCelestialGlow::ringOffset(sample,ox,oy);else NorthlightCelestialHalo::offset(sample,ox,oy);
            c[13+sample][0]=c[13+sample][1]=-1;
            c[13+sample][2]=.5f+.5f*ox/maskRadius;c[13+sample][3]=.5f-.5f*oy/maskRadius;
            for(unsigned k=0;k<3;++k)ray[k]=disc.direction[k]+disc.tangentRadius*(disc.right[k]*ox+disc.up[k]*oy);
            for(unsigned row=0;row<3;++row)for(unsigned k=0;k<3;++k)v[row]+=ray[k]*inverseView[row*4+k];
            const float forward=v[2]*projection[2];if(forward<=.000001f)continue;
            const float x=(v[0]*projection[0]/forward*.5f+.5f)*width,y=(.5f-v[1]*projection[1]/forward*.5f)*height;
            if(x<0||x>=width||y<0||y>=height)continue;
            c[13+sample][0]=x/width;c[13+sample][1]=y/height;
        }
        return NorthlightCelestialGlow::normalizeOnScreen(c+13,NorthlightCelestialHalo::Samples);
    }
public:
    void setTerrainSource(std::function<uint64_t()> epoch,std::function<bool(unsigned,const float*)> draw,std::function<void(unsigned)> reuse=nullptr){terrainEpoch=std::move(epoch);terrainDraw=std::move(draw);terrainReuse=std::move(reuse);}
    explicit NorthlightCelestialDiscRenderer(IDirect3DDevice9* device,std::string sourceRoot="world-cache/celestial"):d(device),root(std::move(sourceRoot)){}
    ~NorthlightCelestialDiscRenderer(){reset();}
    NorthlightCelestialDiscRenderer(const NorthlightCelestialDiscRenderer&)=delete;
    void reset(){for(auto& s:ringSurface)drop(s);for(auto& t:ringVisibility)drop(t);drop(veilPS);ringIndex=0;ringHistory={};ringWritten=false;sunGlare=Glare{};glow=NorthlightCelestialGlow::Hue{};
        for(unsigned i=0;i<2;++i){edge[i]={};haloWritten[i]=false;haloFrame[i]=0;}
        for(auto& t:terrainMask)drop(t);for(auto& t:terrainSurface)drop(t);drop(terrainVS);drop(terrainPS);for(unsigned i=0;i<2;++i){terrainReady[i]=false;terrainGeneration[i]=0;}skyBand={};for(auto& h:haloHistory)h={};for(auto& body:haloSurface)for(auto& s:body)drop(s);for(auto& body:haloVisibility)for(auto& t:body)drop(t);drop(haloPS);haloIndex[0]=haloIndex[1]=0;for(auto& p:earlyPlacement)p.valid=false;for(auto& t:texture)drop(t);drop(ps);observer.reset();loaded=false;failed=false;drawn=0;earlyValid=false;drawnEarly=0;earlyWidth=earlyHeight=0;glareRead=false;drawnLate=offscreenOwned=0;suppressed=suppressedPrevious=0;ownFrame=~0u;}
    // Ownership: once the renderer has drawn discs for a validated sky context,
    // the game's own sun/primary-moon billboards are skipped on later frames.
    // Zone skyboxes drawn after the native billboard hid it while the shafts
    // still pointed at the real body; the renderer's disc is depth-occluded by
    // the world instead. Mod off (F10) or an invalid CURRENT sky restores native
    // draws. Missing a terrain/effect pass must not alternate native/custom moon.
    unsigned claimedDraws=0;
    void setIdentityMap(std::function<std::uintptr_t(std::uintptr_t)> map){observer.setIdentityMap(std::move(map));glareRead=false;}
    void endFrame(){suppressedPrevious=suppressed;suppressed=0;terrainReady[0]=terrainReady[1]=false;sunGlare.valid=false;for(auto& p:earlyPlacement)p.valid=false;observer.beginFrame();drawn=0;++frames;drawnEarly=0;drawnLate=offscreenOwned=0;glareRead=false;}
    // Sky-phase draw: the native billboard is replaced IN PLACE by our disc,
    // drawn with sky-band depth before the zone skybox and the world, so
    // everything drawn later (skybox mountains, terrain, trees) covers it
    // exactly like the native moon. Uses the current client camera (independent
    // reader), a validated projection, and a fresh orbit/appearance snapshot.
    float earlyProjection[3]={};float earlyWorldMaxDepth=1;bool earlyValid=false;UINT earlyWidth=0,earlyHeight=0;unsigned drawnEarly=0,earlyFailures=0;
    unsigned claimGate=0,claimNoBody=0,claimCamera=0,claimOffscreen=0,claimDrawn=0,claimSecondary=0;
    // Side-effect-free prefixes of the claim/observe gates, so callers can skip
    // their viewport validation for draws both claims would reject anyway.
    // False here implies claimNativeGlare and claimNativeDraw return false;
    // only claimGate is counted here instead (viewport rejects now included).
    static bool skyTriangles(D3DPRIMITIVETYPE type){return type==D3DPT_TRIANGLELIST||type==D3DPT_TRIANGLESTRIP||type==D3DPT_TRIANGLEFAN;}
    bool nativeClaimPossible(D3DPRIMITIVETYPE type,UINT count){
        const bool glare=!failed&&loaded&&(drawnEarly|drawnLate|offscreenOwned|suppressed|suppressedPrevious)&&count==2&&skyTriangles(type);
        const bool draw=earlyValid&&!failed&&count&&count<=4&&skyTriangles(type);
        if(!glare&&!draw)++claimGate;return glare||draw;
    }
    bool nativeObservePossible(HRESULT result,D3DPRIMITIVETYPE type,UINT count){return SUCCEEDED(result)&&!failed&&count&&count<=4&&skyTriangles(type);}
    template<class Palette> bool claimNativeDraw(D3DPRIMITIVETYPE type,UINT count,bool skyPhase,Palette palette){
        if(!earlyValid||failed||!skyPhase||count>4||!count||
           (type!=D3DPT_TRIANGLELIST&&type!=D3DPT_TRIANGLESTRIP&&type!=D3DPT_TRIANGLEFAN)||!load()){++claimGate;return false;}
        IDirect3DBaseTexture9* source=nullptr;int body=-1;
        if(SUCCEEDED(d->GetTexture(0,&source))){body=observer.claimIndex(source);drop(source);}
        if(body<0){++claimNoBody;return false;}
        if(!owning())return false; // no live late disc (world debug, no sky context): keep the native one
        if(body==2){++claimSecondary;return true;} // native moon02 is skipped, never redrawn
        // F1b: skip the native billboard; the late disc (canFallback: not observed) draws it.
        if(!EarlyDisc){suppressed|=1u<<body;++suppressedDraws;return true;}
        D3DVIEWPORT9 nativeViewport={};
        if(SUCCEEDED(d->GetViewport(&nativeViewport)))
            skyBand.observe(nativeViewport.MinZ,nativeViewport.MaxZ,earlyWorldMaxDepth);
        if(drawnEarly&(1u<<body))return true; // replace each body only once per frame
        // Current camera from the client; without it the native draw stays.
        char map[64]={};NorthlightWorldCamera::Camera camera;NorthlightWorldCamera::Diagnostics why;
        if(!NorthlightWorldCamera::readSkyPhase(map,earlyProjection[2],camera,&why)){++claimCamera;if(earlyFailures++<4)logf("CELESTIAL early draw skipped: camera %s",NorthlightWorldCamera::rejectName(why.reason));return false;}
        NorthlightCelestial::Context currentSky;const float neutralDirect[3]={1,1,1};
        if(!NorthlightCelestial::read(camera.camera,neutralDirect,currentSky)){++claimCamera;return false;}
        NorthlightCelestial::resolveRendererSky(currentSky,neutralDirect);
        NorthlightCelestialProfiles::apply(palette(map,camera.camera),neutralDirect,currentSky);
        IDirect3DSurface9* target=nullptr;D3DSURFACE_DESC desc={};
        if(FAILED(d->GetRenderTarget(0,&target))||!target)return false;
        struct Release {IDirect3DSurface9* p;~Release(){drop(p);}} guard{target};
        if(FAILED(target->GetDesc(&desc))||desc.Width!=earlyWidth||desc.Height!=earlyHeight)return false;
        const NorthlightCelestial::Body* bodies[]={&currentSky.sun,&currentSky.moon};
        NorthlightCelestialDisc::Disc disc;int bounds[4];
        ++claimedDraws;
        if(!NorthlightCelestialDisc::prepare(bodies[body]->direction,bodies[body]->tint,bodies[body]->alpha,bodies[body]->angularRadius,disc)){
            if(bodies[body]->direction[2]<=0)offscreenOwned|=1u<<body;
            ++claimOffscreen;return true;
        }
        if(!NorthlightCelestialDisc::screenBounds(disc,camera.inverseView,earlyProjection[0],earlyProjection[1],earlyProjection[2],desc.Width,desc.Height,bounds)){
            // A warped body outside the viewport must not leave its old glare
            // visible inside the viewport. Current validated placement owns it.
            offscreenOwned|=1u<<body;++claimOffscreen;return true;
        }
        if(!prepareTerrain(unsigned(body),disc,camera.inverseView))return false;
        if(!drawDisc(unsigned(body),target,desc.Width,desc.Height,earlyProjection,camera.inverseView,disc,bounds,nullptr,nullptr,0,earlyWorldMaxDepth,1,2,nullptr,false,bodies[body]->emission))return false;
        drawnEarly|=1u<<body;++claimDrawn;return true;
    }
    bool claimNativeGlare(D3DPRIMITIVETYPE type,UINT count){
        const unsigned owned=drawnEarly|drawnLate|offscreenOwned|suppressed|suppressedPrevious;
        if(failed||!loaded||!owning()||!owned||count!=2||
           (type!=D3DPT_TRIANGLELIST&&type!=D3DPT_TRIANGLESTRIP&&type!=D3DPT_TRIANGLEFAN))return false;
        static const bool supported=NorthlightWorldContext::supportedClient()&&NorthlightCelestialGlare::verify(NorthlightWorldContext::readSelf);
        if(!supported)return false;
        auto readGlare=[&]{auto ids=NorthlightCelestialGlare::readIdentities(NorthlightWorldContext::readSelf);
            return NorthlightCelestialDisc::mapIdentities(ids,[&](std::uintptr_t exposed){return observer.mapIdentity(exposed);});};
        if(!glareRead){glareIdentities=readGlare();glareRead=true;}
        IDirect3DBaseTexture9* source=nullptr;
        if(FAILED(d->GetTexture(0,&source))||!source)return false;
        const auto bound=reinterpret_cast<std::uintptr_t>(source);drop(source);
        if(bound!=glareIdentities.texture[0]&&bound!=glareIdentities.texture[1])return false;
        // Re-read on a match: lazy texture allocation or streaming cannot make
        // a stale identity suppress another object's draw.
        const auto current=readGlare();
        const int body=NorthlightCelestialGlare::claim(glareIdentities,current,bound,owned);
        if(body<0)return false;
        ++suppressedGlares;
        if(++glareClaims<=4||(glareClaims%1200==0&&NorthlightDiagnostics::enabled()))logf("CELESTIAL native glare replaced body=%d claims=%u (warped disc owns centered halo)",body,glareClaims);
        return true;
    }
    bool drawDisc(unsigned i,IDirect3DSurface9* target,UINT width,UINT height,const float* projection,const float* inverseView,
                  const NorthlightCelestialDisc::Disc& disc,const int* bounds,IDirect3DTexture9* depth,IDirect3DTexture9* water,
                  float minZ,float maxZ,float nearZ,float farZ,const float* legacyFog,bool occlude,float emission,bool glare=false,float repairDepth=-1){
        float c[NorthlightCelestialVeil::Registers][4]={};
        // Glare: taps and the screen-edge fade first, so a skipped glare costs no D3D call.
        float onScreen=0,fade=1;bool hold=false;
        if(glare){
            onScreen=sourceTaps(disc,inverseView,projection,width,height,i?MoonMaskRadius:SunMaskRadius,false,c);
            // With no tap on screen the last measured visibility is held while
            // the fade runs out; if none was measured in the previous frame,
            // visibility is unknown: draw nothing and fade in from 0 later.
            hold=!(onScreen>0);
            const bool live=haloWritten[i]&&haloFrame[i]+1==frames;
            if(hold&&!live){edge[i].zero(GetTickCount(),frames);return true;}
            fade=edge[i].update(GetTickCount(),frames,NorthlightCelestialGlow::edgeFade(centreOutside(disc,inverseView,projection,width,height)));
            if(fade<1.f/512)return true;
        }
        SavedState saved(d);if(!saved.ok||!resources())return false;
        // SetRenderTarget resets the viewport, including MinZ/MaxZ to 0..1.
        // Read the client's sky band BEFORE that call, and also bound the disc
        // behind the measured world band. Otherwise .92 lands inside 0.. .94
        // world depth and rejects trees drawn later (the Duskwood regression).
        D3DVIEWPORT9 skyViewport={0,0,width,height,0,1};float spriteDepth=0;
        if(!occlude){
            if(FAILED(d->GetViewport(&skyViewport))||
               !NorthlightCelestialDisc::skyDepth(skyViewport.MinZ,skyViewport.MaxZ,maxZ,spriteDepth))return false;
            IDirect3DSurface9* skyDepthSurface=nullptr;
            const HRESULT hr=d->GetDepthStencilSurface(&skyDepthSurface);
            const bool hasDepth=SUCCEEDED(hr)&&skyDepthSurface;drop(skyDepthSurface);
            if(!hasDepth)return false;
        }
        DWORD srgb=0;if(!check(d->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb),"read color encoding"))return false;
        if(occlude)d->SetDepthStencilSurface(nullptr);for(unsigned k=1;k<4;++k)d->SetRenderTarget(k,nullptr);
        for(unsigned k=0;k<16;++k)d->SetTexture(k,nullptr);
        if(!check(d->SetRenderTarget(0,target),"target"))return false;
        d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);d->SetPixelShader(ps);
        // Sky phase (occlude=false): behave like the native sprite. The sky
        // occupies its own depth band (viewport MinZ..MaxZ, e.g. .94..1) and
        // the native sprite writes depth inside it, so the world drawn later
        // (real mountains, z below the band) covers it and the sky dome drawn
        // after it in the band does not. Without depth our quad was painted
        // over the mountains and then cut by the dome.
        const struct{D3DRENDERSTATETYPE s;DWORD v;} states[]={{D3DRS_ZENABLE,DWORD(occlude?FALSE:TRUE)},{D3DRS_ZWRITEENABLE,DWORD(occlude?FALSE:TRUE)},{D3DRS_ZFUNC,D3DCMP_LESSEQUAL},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,TRUE},
          {D3DRS_SRCBLEND,DWORD(glare?D3DBLEND_INVDESTCOLOR:D3DBLEND_SRCALPHA)},{D3DRS_DESTBLEND,DWORD(glare?D3DBLEND_ONE:D3DBLEND_INVSRCALPHA)},{D3DRS_BLENDOP,D3DBLENDOP_ADD},{D3DRS_SEPARATEALPHABLENDENABLE,FALSE},
          {D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_COLORWRITEENABLE,7},{D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,TRUE},
          {D3DRS_CLIPPLANEENABLE,0},{D3DRS_DEPTHBIAS,0},{D3DRS_SLOPESCALEDEPTHBIAS,0},{D3DRS_FILLMODE,D3DFILL_SOLID},{D3DRS_MULTISAMPLEMASK,0xffffffff},{D3DRS_WRAP0,0}};
        for(auto& s:states)d->SetRenderState(s.s,s.v);
        d->SetTextureStageState(0,D3DTSS_TEXCOORDINDEX,0);d->SetTextureStageState(0,D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE);
        for(unsigned k=0;k<6;++k){d->SetSamplerState(k,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(k,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            d->SetSamplerState(k,D3DSAMP_MINFILTER,(k==1||k==4)?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(k,D3DSAMP_MAGFILTER,(k==1||k==4)?D3DTEXF_LINEAR:D3DTEXF_POINT);
            d->SetSamplerState(k,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(k,D3DSAMP_SRGBTEXTURE,FALSE);}
        d->SetTexture(0,depth);d->SetTexture(2,water);d->SetTexture(4,terrainReady[i]?terrainMask[i]:nullptr);
        const float terrainPolicy[]={terrainReady[i]?1.f:0.f,i?MoonMaskRadius:SunMaskRadius,0,0};
        d->SetPixelShaderConstantF(46,terrainPolicy,1);
        std::memcpy(c[46],terrainPolicy,16);c[0][0]=1.f/width;c[0][1]=1.f/height;c[0][2]=minZ;c[0][3]=1/(maxZ-minZ);
        // Glare: c12.x = hot-core rate, c12.z = core weight (moon: strength),
        // c47.w = tail weight, growing toward a low sun (elevation = direction z).
        c[12][0]=glare?NorthlightCelestialGlow::SunCoreRate:emission;c[12][1]=i?MoonGlareSupport:SunGlareSupport;
        c[12][2]=i?NorthlightCelestialGlow::MoonGlareStrength:NorthlightCelestialGlow::SunCoreWeight;c[12][3]=glare?1.f:0.f;
        // The glare reads the ring the previous frame's ring pass wrote (sun only).
        const bool ring=glare&&!i&&ringWritten&&ringFrame+1==frames;
        if(glare)NorthlightCelestialGlow::glareConstants(glow,i,disc.tint,NorthlightCelestialGlow::sunTailWeight(disc.direction[2]),ring?NorthlightCelestialGlow::Wrap:0.f,c[47],c[48]);
        else NorthlightCelestialGlow::constants(glow,i,0.f,c[47],c[48]);
        c[12][2]*=fade;c[47][3]*=fade;
        if(glare){const float gain=weatherGain.load(std::memory_order_relaxed);c[12][2]*=gain;c[47][3]*=gain;} /* 0.3.198 (rain): x1 when dry */
        (void)legacyFog;c[11][0]=repairDepth;c[11][1]=repairDepth>=0?1.f:0.f;c[11][2]=NorthlightCelestialDisc::RepairDepthTolerance;c[11][3]=i==0?1.f:0.f;
        std::memcpy(c[1],projection,12);c[1][3]=water?1.f:0.f;std::memcpy(c[2],inverseView,64);c[10][0]=srgb?1.f:0.f;c[10][1]=nearZ;c[10][2]=farZ;c[10][3]=occlude?1.f:0.f;
        struct V{float x,y,z,w,u,v;} vertices[]={{-.5f,-.5f,spriteDepth,1,0,0},{float(width)-.5f,-.5f,spriteDepth,1,1,0},{-.5f,float(height)-.5f,spriteDepth,1,0,1},{float(width)-.5f,float(height)-.5f,spriteDepth,1,1,1}};
        D3DVIEWPORT9 viewport={0,0,width,height,0,1};d->SetViewport(&viewport);
        std::memcpy(c[6],disc.direction,12);c[6][3]=disc.tangentRadius;std::memcpy(c[7],disc.right,12);std::memcpy(c[8],disc.up,12);
        std::memcpy(c[9],disc.tint,12);c[9][3]=glare?disc.opacity:disc.opacity*weatherGain.load(std::memory_order_relaxed);RECT clip /* 0.3.198 (rain): the glare keeps its opacity (the veil reads it); its weights carry the gain */={bounds[0],bounds[1],bounds[2],bounds[3]};
        d->SetScissorRect(&clip);d->SetTexture(1,texture[i]);
        if(!check(d->SetPixelShaderConstantF(47,c[47],2),"glow constants"))return false;
        if(glare){
            if(!haloResources())return false;
            auto history=haloHistory[i];history.advance(GetTickCount(),frames,width,height,inverseView+12,haloDay,c[45]);
            // Holding: no pass; the history stays continuous, so the next
            // measurement blends from the held value with the usual 60/140 ms.
            if(hold)haloHistory[i]=history;
            else{
            const unsigned previous=haloIndex[i],next=1-previous;
            // One pixel per body, no CPU readback. Ordinary disc/halo pixels
            // retain their own strict geometry/water depth test below.
            d->SetTexture(3,haloVisibility[i][previous]);
            if(!check(d->SetRenderTarget(0,haloSurface[i][next]),"halo visibility target"))return false;
            D3DVIEWPORT9 tiny={0,0,1,1,0,1};d->SetViewport(&tiny);
            d->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE);
            d->SetRenderState(D3DRS_SCISSORTESTENABLE,FALSE);d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE);
            d->SetPixelShader(haloPS);
            const V pixel[]={{-.5f,-.5f,0,1,0,0},{.5f,-.5f,0,1,1,0},{-.5f,.5f,0,1,0,1},{.5f,.5f,0,1,1,1}};
            if(!check(d->SetPixelShaderConstantF(0,c[0],47),"halo visibility constants")||
               !check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,pixel,sizeof(V)),"halo visibility draw"))return false;
            haloHistory[i]=history;haloIndex[i]=next;haloWritten[i]=true;
            if(!check(d->SetRenderTarget(0,target),"halo output target"))return false;
            d->SetViewport(&viewport);d->SetScissorRect(&clip);d->SetRenderState(D3DRS_SCISSORTESTENABLE,TRUE);
            d->SetRenderState(D3DRS_SRGBWRITEENABLE,srgb);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE);d->SetPixelShader(ps);
            }
            haloFrame[i]=frames;
            IDirect3DTexture9* halo=haloVisibility[i][haloIndex[i]];
            d->SetTexture(3,halo);
            // Without a ring the shader's max(halo, 0*ring) reads the halo value.
            d->SetTexture(5,ring?ringVisibility[ringIndex]:halo);
            if(!i){auto& g=sunGlare;g.valid=true;g.disc=disc;std::memcpy(g.projection,projection,12);std::memcpy(g.inverseView,inverseView,64);
                std::memcpy(g.constants,c,sizeof c);std::memcpy(g.bounds,bounds,sizeof g.bounds);g.depth=depth;g.water=water;g.width=width;g.height=height;g.fade=fade;}
        }
        if(!glare&&frames%600==0&&NorthlightDiagnostics::enabled())logf("CELESTIAL disc body=%u early=%d depth=%.7f band=%.3f..%.3f worldMax=%.7f screen=(%d,%d)-(%d,%d) direction=(%.3f,%.3f,%.3f)",i,occlude?0:1,spriteDepth,skyViewport.MinZ,skyViewport.MaxZ,maxZ,bounds[0],bounds[1],bounds[2],bounds[3],disc.direction[0],disc.direction[1],disc.direction[2]);
        if(!check(d->SetPixelShaderConstantF(0,c[0],18),"constants")||!check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(V)),"disc draw")){if(glare&&!i)sunGlare.valid=false;return false;}
        if(!occlude){auto& p=earlyPlacement[i];p.disc=disc;p.depth=spriteDepth;p.emission=emission;p.width=width;p.height=height;
            std::memcpy(p.projection,projection,12);std::memcpy(p.inverseView,inverseView,64);p.valid=true;}
        ++drawn;return true;
    }
    // Call only for the original draw's returned success, never replay/capture.
    // skyPhase is the host main-world pre-UI phase, independent of terrain order.
    void observeNativeDraw(HRESULT result,D3DPRIMITIVETYPE type,UINT count,bool skyPhase){
        if(FAILED(result)||failed||!skyPhase||count>4||!count||
           (type!=D3DPT_TRIANGLELIST&&type!=D3DPT_TRIANGLESTRIP&&type!=D3DPT_TRIANGLEFAN)||!load())return;
        // Exact current body texture identity is sufficient. No generic alpha
        // particle discovery or render-state queries are needed.
        IDirect3DBaseTexture9* source=nullptr;if(SUCCEEDED(d->GetTexture(0,&source))){observer.observe(source);drop(source);}
    }
    bool render(IDirect3DSurface9* target,IDirect3DTexture9* depth,IDirect3DTexture9* water,UINT width,UINT height,const float* legacyFog,
                float minZ,float maxZ,float nearZ,float farZ,const float* projection,const float* inverseView,const NorthlightCelestial::Context& context,
                const NorthlightCelestialGlow::Hue& hue){
        glow=hue;
        if(failed||!target||!depth||!width||!height||!projection||!inverseView||!std::isfinite(minZ)||!std::isfinite(maxZ)||maxZ<=minZ||
           !std::isfinite(nearZ)||!std::isfinite(farZ)||nearZ<=0||farZ<=nearZ||!load())return false;
        observer.finishObservations();ownFrame=frames;
        haloDay=context.dayFraction;
        NorthlightCelestialDisc::Disc disc[2];int bounds[2][4];bool visible[2]={};
        const NorthlightCelestial::Body* bodies[]={&context.sun,&context.moon};
        // Renderer policy has already resolved appearance, including intentional
        // profile dimming. Never normalize it differently from the early disc.
        float tint[2][3];
        for(unsigned i=0;i<2;++i)std::memcpy(tint[i],bodies[i]->tint,12);
        // Remember what the sky-phase draw needs next frame.
        std::memcpy(earlyProjection,projection,12);earlyWorldMaxDepth=maxZ;earlyWidth=width;earlyHeight=height;earlyValid=true;
        for(unsigned i=0;i<2;++i){
            const bool early=(drawnEarly&(1u<<i))!=0;
            if((!early&&!observer.lateDisc(i,suppressed))||
               !NorthlightCelestialDisc::prepare(bodies[i]->direction,tint[i],bodies[i]->alpha,bodies[i]->angularRadius,disc[i]))continue;
            visible[i]=!early&&NorthlightCelestialDisc::screenBounds(disc[i],inverseView,projection[0],projection[1],projection[2],width,height,bounds[i]);
            if(!early){
                auto support=disc[i];support.tangentRadius*=i?MoonMaskRadius:SunMaskRadius;int supportBounds[4];
                // Intentional: with the 10R mask support off screen the centre is
                // >= 10R outside, where the glare's edge fade is 0 anyway.
                if(!NorthlightCelestialDisc::screenBounds(support,inverseView,projection[0],projection[1],projection[2],width,height,supportBounds))continue;
                if(!prepareTerrain(i,disc[i],inverseView))return false;
            }
            if(visible[i]&&!drawDisc(i,target,width,height,projection,inverseView,disc[i],bounds[i],depth,water,minZ,maxZ,nearZ,farZ,legacyFog,true,bodies[i]->emission,false,skyBand.depth(maxZ)))return false;
            if(visible[i])drawnLate|=1u<<i;
            // Repaint surviving early-disc pixels after native sky layers.
            // Exact stored camera/projection/size prevents a second silhouette
            // on camera movement. The raw D24 depth test retains nearer sky occluders
            // as well as world geometry; this is not a generic sky-depth test.
            const auto& placement=earlyPlacement[i];
            const float* bodyProjection=projection;const float* bodyView=inverseView;
            float emission=bodies[i]->emission;
            if(early&&placement.valid&&placement.width==width&&placement.height==height){
                disc[i]=placement.disc;bodyProjection=placement.projection;bodyView=placement.inverseView;emission=placement.emission;
                if(NorthlightCelestialDisc::screenBounds(disc[i],bodyView,bodyProjection[0],bodyProjection[1],bodyProjection[2],width,height,bounds[i])&&
                   !drawDisc(i,target,width,height,bodyProjection,bodyView,disc[i],bounds[i],depth,water,minZ,maxZ,nearZ,farZ,legacyFog,true,emission,false,placement.depth))return false;
            }
            // Halo uses identical placement; only the sun also brightens its center.
            auto halo=disc[i];halo.tangentRadius*=i?MoonGlareSupport:SunGlareSupport;
            if(NorthlightCelestialDisc::screenBounds(halo,bodyView,bodyProjection[0],bodyProjection[1],bodyProjection[2],width,height,bounds[i])&&
               !drawDisc(i,target,width,height,bodyProjection,bodyView,disc[i],bounds[i],depth,water,minZ,maxZ,nearZ,farZ,legacyFog,true,emission,true,early&&placement.valid?placement.depth:skyBand.depth(maxZ)))return false;
        }
        if(frames%120==0&&NorthlightDiagnostics::enabled())logf("CELESTIAL fallback=%u earlyMask=%u nativeMask=%u identitySnapshots=%u ambiguousBodies=%u claimedNativeDraws=%u claimGate=%u claimNoBody=%u claimCamera=%u claimOffscreen=%u claimDrawn=%u claimSecondary=%u suppressedMask=%u suppressedDraws=%u suppressedGlares=%u",drawn,drawnEarly,observer.mask(),observer.attempts(),observer.ambiguities(),claimedDraws,claimGate,claimNoBody,claimCamera,claimOffscreen,claimDrawn,claimSecondary,suppressed,suppressedDraws,suppressedGlares);
        return drawn!=0||drawnEarly!=0;
    }
    // wrap ring: after render(), before the scene copy, in the same group of
    // 1x1 visibility passes. The unchanged CelestialHaloVisibilityPS runs on
    // 2-4R taps (inside the 10R sun mask) into its own history. The veil reads
    // it this frame, the sun glare next frame. Sun only; the moon has no veil.
    bool renderRing(){
        const auto& g=sunGlare;
        if(failed||!g.valid)return false;
        float c[NorthlightCelestialVeil::Registers][4];std::memcpy(c,g.constants,sizeof c);
        if(!(sourceTaps(g.disc,g.inverseView,g.projection,g.width,g.height,SunMaskRadius,true,c)>0))return false;
        SavedState saved(d);if(!saved.ok||!haloResources()||!ringResources())return false;
        // A skipped frame, resize, teleport or clock jump resets it, like the halo.
        auto history=ringHistory;history.advance(GetTickCount(),frames,g.width,g.height,g.inverseView+12,haloDay,c[45]);
        const unsigned previous=ringIndex,next=1-previous;
        d->SetDepthStencilSurface(nullptr);for(unsigned k=1;k<4;++k)d->SetRenderTarget(k,nullptr);
        for(unsigned k=0;k<16;++k)d->SetTexture(k,nullptr);
        if(!check(d->SetRenderTarget(0,ringSurface[next]),"ring visibility target"))return false;
        D3DVIEWPORT9 tiny={0,0,1,1,0,1};d->SetViewport(&tiny);
        const struct{D3DRENDERSTATETYPE s;DWORD v;} states[]={{D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},
          {D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_FOGENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_SCISSORTESTENABLE,FALSE},
          {D3DRS_CLIPPLANEENABLE,0},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_FILLMODE,D3DFILL_SOLID}};
        for(auto& s:states)d->SetRenderState(s.s,s.v);
        d->SetVertexShader(nullptr);d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1);d->SetStreamSourceFreq(0,1);d->SetPixelShader(haloPS);
        for(unsigned k:{0u,2u,3u,4u}){d->SetSamplerState(k,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP);d->SetSamplerState(k,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP);
            d->SetSamplerState(k,D3DSAMP_MINFILTER,k==4?D3DTEXF_LINEAR:D3DTEXF_POINT);d->SetSamplerState(k,D3DSAMP_MAGFILTER,k==4?D3DTEXF_LINEAR:D3DTEXF_POINT);
            d->SetSamplerState(k,D3DSAMP_MIPFILTER,D3DTEXF_NONE);d->SetSamplerState(k,D3DSAMP_SRGBTEXTURE,FALSE);}
        d->SetTexture(0,g.depth);d->SetTexture(2,g.water);d->SetTexture(3,ringVisibility[previous]);d->SetTexture(4,terrainReady[0]?terrainMask[0]:nullptr);
        struct V{float x,y,z,w,u,v;};const V pixel[]={{-.5f,-.5f,0,1,0,0},{.5f,-.5f,0,1,1,0},{-.5f,.5f,0,1,0,1},{.5f,.5f,0,1,1,1}};
        if(!check(d->SetPixelShaderConstantF(0,c[0],47),"ring visibility constants")||
           !check(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,pixel,sizeof(V)),"ring visibility draw"))return false;
        ringHistory=history;ringIndex=next;ringWritten=true;ringFrame=frames;return true;
    }
    NorthlightCelestialVeil::Stats veilStats;
    // veil over geometry (sun). Call after the world composite and water,
    // before UI, with the target water composited into; the caller's SavedState
    // restores the state. hazeAtSun: NorthlightCelestialGlow::hazeAtSun().
    bool renderVeil(IDirect3DSurface9* target,float hazeAtSun,float skyTransmittance=1){
        const auto& g=sunGlare;
        if(failed||!g.valid||!target)return false;
        float c[NorthlightCelestialVeil::Registers][4];std::memcpy(c,g.constants,sizeof c);
        // The glare weights already carry the edge fade; scale both by the veil.
        const float veil=NorthlightCelestialGlow::veilStrength(c[9][3],1,hazeAtSun,skyTransmittance);
        c[12][2]*=veil;c[47][3]*=veil;
        c[10][0]=0; // SRGBWRITE off: encoded output, like the composite before it
        const bool ring=ringWritten&&ringFrame==frames;c[48][3]=ring?NorthlightCelestialGlow::Wrap:0.f;
        if(!(veil>0)||!(c[12][2]+c[47][3]>0)){++veilStats.skipped;return false;}
        if(!veilPS&&!check(d->CreatePixelShader(kCelestialVeilPSShader,&veilPS),"veil shader"))return false;
        IDirect3DTexture9* halo=haloVisibility[0][haloIndex[0]];
        IDirect3DTexture9* const textures[NorthlightCelestialVeil::Samplers]={g.depth,nullptr,g.water,halo,terrainReady[0]?terrainMask[0]:nullptr,ring?ringVisibility[ringIndex]:halo};
        return NorthlightCelestialVeil::draw(d,target,veilPS,textures,c,g.bounds,g.width,g.height,veilStats);
    }
};
