// Zone skybox state (DayNight::RenderSky) decoded from the sky block: skyboxCover, nativePlanets, the per-frame mode, the field signatures.
#include "celestial_context.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>
using namespace NorthlightCelestial;
static constexpr float NaN=std::numeric_limits<float>::quiet_NaN();
struct Block {
    std::vector<unsigned char> memory=std::vector<unsigned char>(0x388);
    template<class T> void put(size_t offset,T v){std::memcpy(memory.data()+offset,&v,sizeof v);}
    Block(){ // a valid noon sky: sun z=.8, moon below, nothing skybox
        put<float>(4,.5f);
        for(int i=0;i<3;++i){
            const float z=i==0?.8f:-.8f,c[3]={12*std::sqrt(1-z*z),0,12*z};
            BodyRecord b={};std::memcpy(b.center,c,12);b.color=0xffffffff;b.texture=1;b.size=1;
            std::memcpy(memory.data()+0x328+i*32,&b,32);
        }
    }
    void skybox(uint32_t p,float w){put<uint32_t>(0x5c,p);put<float>(0x60,w);}
    void blend(int i,uint32_t p,float w,uint32_t flags){put<uint32_t>(0x64+4*i,p);put<float>(0x70+4*i,w);put<uint32_t>(0x7c+4*i,flags);}
    bool decoded(Context& c){
        Snapshot s;auto reader=[&](uintptr_t address,void* dst,size_t size){assert(address==0xd38b00&&size==memory.size());std::memcpy(dst,memory.data(),size);return true;};
        const float camera[3]={},direct[3]={1,1,1};
        return readSnapshot(reader,s)&&decode(s,camera,direct,c);
    }
};
// One frame's decision on a fresh mode with no history.
static bool skyboxFrame(const Context& c){FrameMode m;return m.decide(c,0);}
static Context ctx(Block& b){Context c;c.skyboxCover=-1;assert(b.decoded(c));return c;}
int main() {
    const uint32_t P=0x1a2b3c40;
    {Block b;Context c=ctx(b);assert(c.skyboxCover==0&&c.nativePlanets&&!skyboxFrame(c));} // null pointers
    {Block b;b.skybox(0,1);b.blend(0,0,1,0);Context c=ctx(b);assert(c.skyboxCover==0&&c.nativePlanets&&!skyboxFrame(c));} // weight without a model
    {Block b;b.skybox(P,1);Context c=ctx(b);assert(c.skyboxCover==1&&!c.nativePlanets&&skyboxFrame(c));} // full skybox: no native planets
    {Block b;b.skybox(P,.995f);Context c=ctx(b);assert(!c.nativePlanets&&skyboxFrame(c));}
    {Block b;b.skybox(P,.99f);Context c=ctx(b);assert(c.nativePlanets&&std::fabs(c.skyboxCover-.99f)<1e-6f&&skyboxFrame(c));} // RenderSky: > 0.99 only
    {Block b;b.skybox(P,.5f);Context c=ctx(b);assert(c.skyboxCover==.5f&&c.nativePlanets&&skyboxFrame(c));} // partial: planets drawn, skybox over them
    {Block b;b.skybox(P,.004f);Context c=ctx(b);assert(c.skyboxCover==.004f&&c.nativePlanets&&!skyboxFrame(c));} // a faint skybox stays on the planet phase
    {Block b;b.skybox(P,.01f);assert(!skyboxFrame(ctx(b)));b.skybox(P,.02f);assert(!skyboxFrame(ctx(b)));b.skybox(P,.03f);assert(skyboxFrame(ctx(b)));} // enters above .02
    {Block b;b.blend(1,P,1,0);Context c=ctx(b);assert(c.skyboxCover==1&&!c.nativePlanets&&skyboxFrame(c));} // full blend skybox, flags clear
    {Block b;b.blend(1,P,1,1);Context c=ctx(b);assert(c.skyboxCover==1&&c.nativePlanets&&skyboxFrame(c));} // flag set: RenderSky still draws the objects, the skybox covers them
    {Block b;b.blend(0,P,.3f,0);b.blend(2,P+8,.7f,0);b.skybox(P+16,.2f);Context c=ctx(b);assert(c.skyboxCover==.7f&&c.nativePlanets);} // maximum over all four
    {Block b;b.skybox(P,NaN);Context c=ctx(b);assert(c.skyboxCover==0&&c.nativePlanets&&!skyboxFrame(c));} // NaN weight is no skybox
    {Block b;b.skybox(P,NaN);b.blend(0,P,.6f,0);Context c=ctx(b);assert(c.skyboxCover==.6f);}
    {Block b;b.skybox(P,std::numeric_limits<float>::infinity());Context c=ctx(b);assert(c.skyboxCover==0&&c.nativePlanets);}
    {Block b;b.skybox(P,-2);Context c=ctx(b);assert(c.skyboxCover==0&&c.nativePlanets);} // clamped to 0..1
    {Block b;b.skybox(P,7);Context c=ctx(b);assert(c.skyboxCover==1&&!c.nativePlanets);}
    {Block b;b.skybox(0x00000040,1);Context c=ctx(b);assert(c.skyboxCover==0&&c.nativePlanets);} // garbage pointer (below the first 64K page)
    {Block b;b.skybox(P,.5f);Block clean;Context c=ctx(b),d=ctx(clean);assert(c.dayFraction==d.dayFraction&&c.sunWeight==d.sunWeight);} // only the skybox fields differ
    // A torn read of the skybox fields is a torn read like any other.
    {Block b;b.skybox(P,.5f);Snapshot s;int n=0;auto torn=[&](uintptr_t,void* dst,size_t size){std::memcpy(dst,b.memory.data(),size);if(n++==0)b.put<float>(0x60,.6f);return true;};assert(!readSnapshot(torn,s));}
    // Per-frame mode: decided once from the first context, the frame boundary re-opens it.
    {Block none,full;full.skybox(P,1);Context a=ctx(none),f=ctx(full);FrameMode m;
     assert(!m.decided()&&!m.skybox());
     assert(!m.decide(a,0)&&m.decided()&&!m.skybox());
     assert(!m.decide(f,1)&&!m.skybox()&&m.switches==0); // the late context cannot flip the claim's decision
     m.endFrame();assert(!m.decided());
     assert(m.decide(f,2)&&m.skybox()&&m.switches==1);assert(m.decide(a,3)&&m.skybox());}
    // Hysteresis: enter above .02 at once, leave only below .005 held 250 ms; 0.01/0.006 alternation never flaps.
    {Block b;Context in,mid,low,none;b.skybox(P,.03f);in=ctx(b);b.skybox(P,.01f);mid=ctx(b);b.skybox(P,.006f);low=ctx(b);b.skybox(P,.001f);Context quiet=ctx(b);
     Block z;none=ctx(z);
     FrameMode m;uint32_t t=1000;
     auto frame=[&](const Context& c){bool r=m.decide(c,t);m.endFrame();t+=16;return r;};
     for(int i=0;i<60;++i)assert(!frame(i&1?mid:low)); // never entered
     assert(frame(in)&&m.switches==1);
     for(int i=0;i<600;++i)assert(frame(i&1?mid:low)); // between .005 and .02: stays
     for(int i=0;i<10;++i)assert(frame(quiet)); // 160 ms of quiet: still on
     assert(frame(mid));for(int i=0;i<10;++i)assert(frame(quiet)); // the quiet timer restarted
     bool left=false;for(int i=0;i<40&&!left;++i)left=!frame(quiet);assert(left&&m.switches==2);
     assert(!frame(none)&&!frame(low));
     // Quiet from the first frame: leaves exactly Hold ms after it began.
     FrameMode n;n.decide(in,0);n.endFrame();n.decide(quiet,100);n.endFrame();assert(n.decide(quiet,349)&&(n.endFrame(),true));assert(!n.decide(quiet,350));
     // A game that skips its planets (full skybox) keeps the mode regardless of cover history.
     Block f;f.skybox(P,1);FrameMode k;assert(k.decide(ctx(f),0));k.endFrame();assert(k.decide(ctx(f),10000));}
    // Sequencing of one frame, as the renderer runs it: claim (native billboards drawn by the game) -> render() fallback.
    {struct Sim {
         FrameMode mode;unsigned frames=1,renderFrame=~0u;uint32_t now=1000;
         struct Out{bool suppressed[2],early[2],late[2],native[2],tail[2];}; // tail: PR #90's late halo-only path (sun only: `if(i==1)continue;`)
         // billboards: the game issues its sun/moon draw this frame. lateWorld: render() runs (world drawn).
         Out frame(const Context& c,bool billboards,bool lateWorld=true){
             Out o={};
             for(int i=0;i<2;++i)if(billboards){
                 if(!mode.decided())mode.decide(c,now);
                 if(mode.skybox()){o.suppressed[i]=NorthlightCelestial::suppressNative(renderFrame,frames);o.native[i]=!o.suppressed[i];}
                 else o.early[i]=true;
             }
             if(lateWorld){
                 mode.decide(c,now);
                 for(int i=0;i<2;++i)o.late[i]=NorthlightCelestial::lateBody(mode.skybox(),o.early[i],false,!o.native[i],c.skyboxCover>0);
                 for(int i=0;i<2;++i)o.tail[i]=i==0&&!o.late[i]&&!o.early[i]&&!o.native[i];
                 renderFrame=frames;
             }
             mode.endFrame();++frames;now+=16;return o;
         }
     } sim;
     Block none,part,full;part.skybox(P,.5f);full.skybox(P,1);Context n=ctx(none),p=ctx(part),f=ctx(full);
     for(int i=0;i<5;++i){auto o=sim.frame(n,true);assert(o.early[0]&&o.early[1]&&!o.late[0]&&!o.late[1]&&!o.suppressed[0]);} // planet phase only
     {auto o=sim.frame(p,true);assert(o.suppressed[0]&&o.suppressed[1]&&!o.early[0]&&!o.native[0]&&o.late[0]&&o.late[1]);} // partial skybox: claim suppresses, late draws both
     {auto o=sim.frame(f,false);assert(!o.suppressed[0]&&!o.early[0]&&o.late[0]&&o.late[1]);} // full skybox: the game draws no billboard, no claim, late draws both
     // The native billboard was seen drawing (not claimed): no late disc, so no double draw.
     {Sim s2;auto o=s2.frame(p,true);assert(o.native[0]&&o.native[1]&&!o.late[0]&&!o.late[1]&&!o.suppressed[0]);} // first skybox frame: no render() yet, the native draws stay
     // Native frame, no claim, billboard not drawn and not observed (interior skybox missed by the snapshot): late fallback.
     {Sim s3;for(int i=0;i<3;++i)s3.frame(n,true);auto o=s3.frame(n,false);assert(!o.early[0]&&!o.late[0]&&!o.late[1]&&o.tail[0]&&!o.tail[1]);} // cover 0 (underwater, no sky drawn): PR #90 exactly: the unobserved sun keeps its late tail halo, the moon nothing
     {Block weak;weak.skybox(P,.01f);Context w=ctx(weak);Sim s4;for(int i=0;i<3;++i)s4.frame(n,true);auto o=s4.frame(w,false);assert(!s4.mode.state&&o.late[0]&&o.late[1]&&!o.tail[0]);} // native mode, skybox evidence: late fallback
     {Sim s5;for(int i=0;i<3;++i)s5.frame(n,true);auto o=s5.frame(p,false);assert(o.late[0]&&o.late[1]);}
     // Pure rule table.
     assert(!NorthlightCelestial::lateBody(true,true,false,true,true,true)); // confirmed layer insertion must not be repainted
     assert(NorthlightCelestial::lateBody(true,true,false,true,true)); // a skybox appearing after the planet snapshot retains the late fallback
     assert(NorthlightCelestial::lateBody(false,false,false,true,true)&&!NorthlightCelestial::lateBody(false,true,false,true,true)&&!NorthlightCelestial::lateBody(false,false,true,true,true)&&!NorthlightCelestial::lateBody(false,false,false,true,false));
     assert(!NorthlightCelestial::lateBody(false,false,false,false,true)&&!NorthlightCelestial::lateBody(true,false,false,false,true)&&NorthlightCelestial::lateBody(true,false,false,true,false));
     assert(NorthlightCelestial::suppressNative(4,5)&&!NorthlightCelestial::suppressNative(~0u,1)&&!NorthlightCelestial::suppressNative(3,5));}
    // Unverified skybox offsets force the safe skybox path, whatever the block says.
    {Block none;Context c=ctx(none);assumeSkybox(c,true);assert(!skyboxFrame(c));assumeSkybox(c,false);assert(c.skyboxCover==1&&!c.nativePlanets&&skyboxFrame(c));}
    // Signatures: the four RenderSky sequences cover 0xd38b5c, 60, 64, 70 and 7c; each one is checked.
    {size_t count=0;auto table=skyboxSignatures(count);assert(count==4);
     auto codeReader=[&](uintptr_t address,void* dst,size_t size){for(size_t i=0;i<count;++i)if(table[i].address==address){assert(size==table[i].size&&size<=32);std::memcpy(dst,table[i].bytes,size);return true;}return false;};
     assert(verifySkyboxCode(codeReader));
     for(size_t bad=0;bad<count;++bad){
         auto changed=[&](uintptr_t address,void* dst,size_t size){bool ok=codeReader(address,dst,size);if(address==table[bad].address)static_cast<unsigned char*>(dst)[0]^=1;return ok;};
         assert(!verifySkyboxCode(changed));}
     assert(!verifySkyboxCode([](uintptr_t,void*,size_t){return false;}));
     // The celestial planets stay independent of the skybox table.
     size_t base=0;const auto* planets=signatures(base);for(size_t i=0;i<base;++i)for(size_t k=0;k<count;++k)assert(planets[i].address!=table[k].address);
     auto onlyPlanets=[&](uintptr_t address,void* dst,size_t size){size_t n=0;const auto* t=signatures(n);for(size_t i=0;i<n;++i)if(t[i].address==address){std::memcpy(dst,t[i].bytes,size);return true;}return false;};
     assert(verifyCode(onlyPlanets)&&!verifySkyboxCode(onlyPlanets));}
    std::puts("PASS skybox decode, cover/nativePlanets rules, frame mode, signatures");
}
