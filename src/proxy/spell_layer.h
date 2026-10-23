#pragma once
// Task 34: the spell layer. Translucent M2 draws (particles, ribbons, spell batches, projected ground effects) are drawn before the effects
// boundary B and so get relight/shadow/AO/fog computed from the background behind them. The layer DEFERS those draws: it records the exact
// draw-relevant device state of each (a state delta against what the earlier entries of the replay already set: Model, Op), guards the vertex/index bytes it reads, skips the real draw, and replays all of
// them in order right after the effects at B, against the final world depth. Nothing here logs and nothing calls Windows; only IDirect3DDevice9 (the extension device) and the
// resources reached through it. Single thread: the one that executes the device (replay thread with the stream on, else the game thread); no locks.
//
// Wiring (per draw, before B, in this order):
//   facts=DrawFacts{...}; fade.update(...) feeds facts.fadeRun, layer.pending()>0 is facts.pending; r=classify(facts,mask);
//   Defer     -> if(layer.defer(args,rt0,ds,r.sub)) return D3D_OK (real draw skipped); else draw in place (layer already flushed what was pending)
//   Hazard    -> layer.hazard(r.reason,true,r.inPlace) then draw in place;  InPlace -> layer.noteInPlace(r.inPlace), draw in place;  others -> draw in place.
// Other events: Clear/StretchRect/... -> hazard(Clear|Copy); texture writes -> onTextureWrite(tex) BEFORE the write (a split, not a hazard, up to Config::maxTexSplits per frame); ProcessVertices dest -> onBufferGpuWrite(buf);
// Buffer::Lock BEFORE forwarding, only when the buffer is marked (markGuard callback) -> onBufferWrite(rawBuffer,offset,size,flags,usage,locks);
// EndScene/Present before B -> hazard(Frame, false); frame start -> beginFrame(pressure); at B after the effects -> replay();
// device Reset/destroy/extension fault -> reset() (drops everything, no draw). Keys are the interface pointers GetStreamSource/GetIndices/GetTexture
// return on the device given to attach() (raw, unwrapped objects); the integration must pass those same pointers to the hooks.
// Include order: this header includes saved_state.h itself (pragma once), which needs the D3D9 API and SUCCEEDED/FAILED in scope.
#ifndef NORTHLIGHT_SPELL_LAYER_TEST_API
#include <d3d9.h>
#endif
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
#include <vector>
#include "saved_state.h"
#include "synced_slots.h"
#include "spell_fog.h"
#include "shader_read_set.h"

namespace NorthlightSpellLayer {
constexpr unsigned kBlendZero=1,kBlendOne=2,kSrcColor=3,kInvSrcColor=4,kSrcAlpha=5,kInvSrcAlpha=6,kDestAlpha=7,kInvDestAlpha=8,kDestColor=9,kInvDestColor=10,kBlendOpAdd=1; /* D3DBLEND_*, D3DBLENDOP_ADD */
constexpr unsigned kLockReadOnly=0x10,kLockNoOverwrite=0x1000,kLockDiscard=0x2000,kUsageDynamic=0x200; /* D3DLOCK_*, D3DUSAGE_DYNAMIC */
static_assert(unsigned(D3DLOCK_READONLY)==0x10&&unsigned(D3DLOCK_NOOVERWRITE)==0x1000&&unsigned(D3DLOCK_DISCARD)==0x2000&&unsigned(D3DUSAGE_DYNAMIC)==0x200,"lock flag values");
constexpr unsigned kCooldownFrames=30,kMaxStreams=4,kMaxTextures=20,kMaxDecl=65,kMinPsMajor=2; /* kMaxTextures: pixel samplers 0..15 and the vertex texture samplers D3DVERTEXTEXTURESAMPLER0..3 (stages 257..260) */

// ---- a) classification (pure) ----
enum class Family:uint8_t{None,Particle,ParticleUnlit,Model,Projected,Ripples}; // M2 vertex shader family, known at shader creation; Ripples (WaterRipples, not an M2 shader) are treated like water
enum Sub:unsigned{SubAdditive=1,SubAlpha=2,SubModulate=4,SubOther=8};
// SpellFog: the PS fog colour a folded record must carry, by blend (the shader mixes lerp(cF, X, fog) and, with the fog folded, fog < 1 even where the game's own fog is off, where the game does not
// maintain cF): additive (dest ONE) and premultiplied (ONE / INVSRCALPHA) black, modulate (DESTCOLOR / ZERO, ZERO / SRCCOLOR) white, mod2x (DESTCOLOR / SRCCOLOR) mid grey, straight alpha the in-scatter
// colour; any other blend is not folded.
enum class FogTarget:uint8_t{None,Black,White,Grey,Scatter};
inline FogTarget fogTarget(unsigned src,unsigned dst,unsigned op=kBlendOpAdd){
    if(op!=kBlendOpAdd)return FogTarget::None;                 // MIN / MAX / SUBTRACT / REVSUBTRACT: the factors do not mean what the table assumes (the separate alpha blend only touches alpha, not this choice)
    if(dst==kBlendOne)return FogTarget::Black;
    if(src==kBlendOne&&dst==kInvSrcAlpha)return FogTarget::Black;
    if((src==kDestColor&&dst==kBlendZero)||(src==kBlendZero&&dst==kSrcColor))return FogTarget::White;
    if(src==kDestColor&&dst==kSrcColor)return FogTarget::Grey;
    if(src==kSrcAlpha&&dst==kInvSrcAlpha)return FogTarget::Scatter;
    return FogTarget::None;
}
enum class Kind:uint8_t{Ignore,Opaque,TranslucentZ,Defer,InPlace,Hazard};
enum class InPlaceKind:uint8_t{None,Water,Rain,Name,Fade};
enum class Reason:uint8_t{Clear,Copy,TexWrite,Sample,BufWrite,GpuWrite,Order,Query,Capacity,Capture,Admit,Frame,Count};
constexpr unsigned kReasons=unsigned(Reason::Count);
inline const char* reasonName(Reason r){
    static const char* const n[]={"clear","copy","texwrite","sample","bufwrite","gpuwrite","order","query","capacity","capture","admit","frame","none"};
    return n[unsigned(r)<=kReasons?unsigned(r):kReasons];
}
struct DrawFacts{
    bool zEnable=false,zWrite=false,blend=false,separateAlpha=false,stencil=false;
    unsigned srcBlend=kBlendOne,destBlend=kBlendZero,blendOp=kBlendOpAdd,colorWrite=15; // colorWrite: the RGB bits (7) matter
    Family family=Family::None;
    bool skinned=false,water=false,uiTag=false,rain=false,claimed=false,fixedFunction=false,worldDomain=false;
    bool rt0IsTarget=false;               // RT0 is the layer target (or would become it: the first candidate's back-buffer-sized world target)
    bool dsIsWorldDepth=false,mrtBound=false,instanced=false,occlusionActive=false;
    bool samplesRenderTargetTexture=false; // the draw samples the layer target's container texture
    bool fadeRun=false;                    // FadeRun::update() result for this draw
    bool pending=false;                    // deferred draws are pending (Layer::pending() > 0): water, rain and names must not be drawn under them
};
struct Result{Kind kind=Kind::Ignore;unsigned sub=0;InPlaceKind inPlace=InPlaceKind::None;Reason reason=Reason::Count;};
inline bool opaqueBlend(const DrawFacts& f){return !f.blend||(f.srcBlend==kBlendOne&&f.destBlend==kBlendZero);}
inline unsigned subClass(const DrawFacts& f){
    if(f.separateAlpha||f.blendOp!=kBlendOpAdd)return SubOther;
    if(f.srcBlend==kDestColor||f.destBlend==kSrcColor)return SubModulate;
    if(f.destBlend==kBlendOne)return SubAdditive;
    if(f.destBlend==kInvSrcAlpha)return SubAlpha;
    return SubOther;
}
// Z-writing draws never defer and stay in place (their depth occludes the replay). Colour-writing no-Z draws into the target are deferred when they
// are one of the four families or a fixed-function draw (ribbons, beams; DrawFacts::fixedFunction) with a non-opaque blend. Water ripples (Family::Ripples) count as water. Water, rain and floating names are drawn in place when nothing is pending (InPlace, no
// effect), but a Hazard/Order while draws are pending: drawn in place they would end up UNDER the replayed spells, so the caller flushes first and
// the original order holds (Result::inPlace names which of them it was; Layer::hazard() then splits the frame instead of falling back). A fade colour pass stays an accepted reorder (its depth pre-pass occludes
// the replay). Everything else is a Hazard (unknown order dependency while draws are pending: the caller flushes). Claimed draws and draws into other targets are Ignore
// (unless they sample the target: Hazard/Sample). A non-world draw that writes colour into the target is a Hazard.
// The decision is one template over a facts accessor F, so the integration can read the device facts lazily, in decision order, and stop at the first that settles the
// draw (the cheap facts first: samples, claimed, RT0, colour write, world domain, Z write, blend). F supplies, as methods returning the DrawFacts field of that name:
// samples (samplesRenderTargetTexture), claimed, rt0IsTarget, colorWrite, worldDomain, zWrite, water, rain, uiTag, fadeRun, skinned, family, pending, fixedFunction, zEnable,
// stencil, dsIsWorldDepth, mrtBound, instanced, occlusionActive, and opaqueBlend() / subClass() computed from the blend facts (blend enable, src, dst, op, separate alpha).
// A fact is read at most when the decision reaches it, so a lazy F never needs the rest. EagerFacts adapts a filled DrawFacts (classify() below).
template<class F> inline Result classifyWith(F& f,unsigned classMask){
    Result r;
    auto hazard=[&](Reason why){r.kind=Kind::Hazard;r.reason=why;return r;};
    if(f.samples())return hazard(Reason::Sample);
    if(f.claimed()||!f.rt0IsTarget())return r;
    const bool rgb=(f.colorWrite()&7)!=0;
    if(!f.worldDomain())return rgb?hazard(Reason::Order):r;
    if(f.zWrite()){r.kind=(f.opaqueBlend()||!rgb)?Kind::Opaque:Kind::TranslucentZ;return r;}
    if(!rgb)return r;
    const bool water=f.water()||f.family()==Family::Ripples; // water ripples are drawn after the spells by the game: they behave like water
    if(water||f.rain()||f.uiTag()||(f.fadeRun()&&f.skinned())){
        r.inPlace=water?InPlaceKind::Water:f.rain()?InPlaceKind::Rain:f.uiTag()?InPlaceKind::Name:InPlaceKind::Fade;
        if(f.pending()&&r.inPlace!=InPlaceKind::Fade)return hazard(Reason::Order);
        r.kind=Kind::InPlace;return r;
    }
    if((f.family()==Family::None&&!f.fixedFunction())||f.opaqueBlend()||!f.zEnable()||f.stencil()||!f.dsIsWorldDepth()||f.mrtBound()||f.instanced())return hazard(Reason::Order);
    if(f.occlusionActive())return hazard(Reason::Query);
    const unsigned sub=f.subClass();
    if(!(classMask&sub))return hazard(Reason::Order);
    r.kind=Kind::Defer;r.sub=sub;return r;
}
struct EagerFacts{
    const DrawFacts& f;
    bool samples()const{return f.samplesRenderTargetTexture;}bool claimed()const{return f.claimed;}bool rt0IsTarget()const{return f.rt0IsTarget;}unsigned colorWrite()const{return f.colorWrite;}
    bool worldDomain()const{return f.worldDomain;}bool zWrite()const{return f.zWrite;}bool water()const{return f.water;}bool rain()const{return f.rain;}bool uiTag()const{return f.uiTag;}
    bool fadeRun()const{return f.fadeRun;}bool skinned()const{return f.skinned;}Family family()const{return f.family;}bool pending()const{return f.pending;}bool fixedFunction()const{return f.fixedFunction;}
    bool zEnable()const{return f.zEnable;}bool stencil()const{return f.stencil;}bool dsIsWorldDepth()const{return f.dsIsWorldDepth;}bool mrtBound()const{return f.mrtBound;}
    bool instanced()const{return f.instanced;}bool occlusionActive()const{return f.occlusionActive;}
    bool opaqueBlend()const{return NorthlightSpellLayer::opaqueBlend(f);}unsigned subClass()const{return NorthlightSpellLayer::subClass(f);}
};
inline Result classify(const DrawFacts& f,unsigned classMask){EagerFacts e{f};return classifyWith(e,classMask);}
// The game's glow (ffxGlow) after an ALLOWED copy of the layer's colour target into a default-pool texture (the integration remembers that texture for the frame): the glow combine
// passes rebuild the image from the copy and would otherwise be Order hazards (screen-space, colour into the target, no Z write). A pass is let through (Pass: drawn in place, nothing
// flushed) only when ALL hold: a glow copy happened this frame, the draw is an Order hazard, it is not world domain and writes no depth, it samples the remembered glow texture, its
// viewport is the full target, and fewer than kMaxGlowPasses passes were let through since that copy. A WORLD-domain draw that samples the glow texture while draws are deferred would
// read an image without them: SampleHazard (the caller flushes with Reason::Sample, the frame falls back). Everything else: None (the decision stands).
constexpr unsigned kMaxGlowPasses=4;
enum class GlowVerdict:uint8_t{None,Pass,SampleHazard};
inline GlowVerdict glowVerdict(bool copyThisFrame,bool orderHazard,bool worldDomain,bool zWrite,bool samplesGlow,bool fullViewport,unsigned passesSinceCopy){
    if(!copyThisFrame||!samplesGlow)return GlowVerdict::None;
    if(worldDomain)return GlowVerdict::SampleHazard;
    if(orderHazard&&!zWrite&&fullViewport&&passesSinceCopy<kMaxGlowPasses)return GlowVerdict::Pass;
    return GlowVerdict::None;
}
// A skinned Z-writing colour-less draw (the depth pre-pass of a fading actor) starts a run; the next non-skinned draw ends it.
struct FadeRun{
    bool active=false;
    bool update(bool skinned,bool zWrite,unsigned colorWrite){
        if(skinned&&zWrite&&(colorWrite&7)==0)active=true;else if(!skinned)active=false;
        return active;
    }
    void reset(){active=false;}
};

// ---- b) frame state machine and counters (pure) ----
enum class Mode:uint8_t{Off,Armed,Deferring,Fallback,Cooldown};
inline const char* modeName(Mode m){static const char* const n[]={"off","armed","deferring","fallback","cooldown"};return n[unsigned(m)];}
// Fallback: a hazard flushed (or a defer failed) this frame, or B has passed (frame closed): every later candidate is drawn in place. A hazard
// frame is followed by kCooldownFrames Cooldown frames; memory pressure gives Cooldown frames without counting them down.
class FrameState{
    Mode mode_=Mode::Off;unsigned cooldown_=0;bool cooldownNext_=false;
public:
    Mode mode()const{return mode_;}
    unsigned cooldownLeft()const{return cooldown_;}
    bool canDefer()const{return mode_==Mode::Armed||mode_==Mode::Deferring;}
    void begin(bool on,bool pressure){
        if(mode_==Mode::Fallback&&cooldownNext_)cooldown_=kCooldownFrames;
        cooldownNext_=false;
        if(!on)mode_=Mode::Off;
        else if(pressure)mode_=Mode::Cooldown;
        else if(cooldown_){--cooldown_;mode_=Mode::Cooldown;}
        else mode_=Mode::Armed;
    }
    void deferred(){if(mode_==Mode::Armed)mode_=Mode::Deferring;}
    void fallback(bool cooldown){if(canDefer()){mode_=Mode::Fallback;cooldownNext_=cooldown;}}
    void close(){fallback(false);} // B reached: no deferral for the rest of the frame, no cooldown
};
struct Counters{
    unsigned deferred[4]={},inPlace[4]={};        // by sub-class (additive, alpha, modulate, other); accepted in-place draws by kind (only fade is counted)
    unsigned orderBy[3]={};                       // Order hazards raised by water, rain, name draws while draws were pending
    unsigned hazards[kReasons]={};
    unsigned flushes=0,replays=0,replayedDraws=0,replaySkipped=0,discarded=0;
    unsigned restoreFallbacks=0,texSplits=0;      // drains that had to use the state block instead of the minimal restore; texture writes answered with a split (drain, keep deferring)
    unsigned restoreSets=0;                       // device Set* calls of the minimal restore (apart from replaySets)
    uint64_t recordSlots=0;                       // slots a record examined (read and compared): with the synced bits only the ones that may have changed
    unsigned fogCapped=0;                         // fits skipped because the frame's cap on new fits was reached (the game's value was kept)
    unsigned fogEntries=0;                        // records whose fog slots carried Northlight's fog (SpellFog)
    unsigned fogBareSkip=0;                        // bare-ramp shaders whose thick-fog fit was not good enough: nothing folded
    unsigned fogNoPsReg=0,fogOtherBlend=0;        // proven vertex shaders not folded: no proven, readable PS fog colour register; a blend with no neutral fog colour
    unsigned fogFlat=0,fogRefused=0,fogUnproven=0,fogNoRead=0; // of the records of a frame with an active estimate: folded from a flat game constant (Northlight's fog alone; also in fogEntries); proven vertex shader but the game's constant refused (identity, the cap excluded: fogCapped); the shader reads c30 but has no fog proof; the shader does not read c30
    unsigned stride0Refused=0;                    // defers refused (a Capture failure: flush + fallback) because a stride-0 stream's vertex size could not be determined
    unsigned checkAllocFails=0;                   // dirty cross-checks that could not run (no scratch memory): the record used the full compare and dirtyRecord went off
    unsigned dirtyMismatches=0;                   // Config::dirtyCheck: records whose synced-bit delta differed from the full compare's (must stay 0)
    unsigned drains=0;                            // replay sequences actually run, of every kind: B (replays), hazard flushes, splits, refused defers (replays counts the B ones only)
    unsigned fixed=0;                             // deferred draws that were fixed-function (also counted in deferred[])
    unsigned conflicts=0,materialized=0,recorded=0,replaySets=0; // recorded: draws recorded; replaySets: device Set* calls issued by replays
    uint64_t conflictBytes=0;
    unsigned arenaHigh=0;                         // bytes
    unsigned recordHigh=0;                        // most records pending at once
    uint64_t deltaWords=0;                        // words of state delta recorded
    unsigned capBy[4]={};                         // Capacity fallbacks by the limit that was hit: entries, delta bytes, arena (UP data and conflict copies) bytes, other (upload buffers, held objects, fixed-function records)
    unsigned overflowFrames=0,overflowAt=0,overflowCandidates=0; // frames that fell back on Capacity; entries pending when they did; deferrable draws of those frames from the overflow on (the overflowing draw included): the real demand
    void add(const Counters& o){
        for(int i=0;i<4;++i){deferred[i]+=o.deferred[i];inPlace[i]+=o.inPlace[i];}
        for(int i=0;i<3;++i)orderBy[i]+=o.orderBy[i];
        for(unsigned i=0;i<kReasons;++i)hazards[i]+=o.hazards[i];
        fixed+=o.fixed;flushes+=o.flushes;replays+=o.replays;drains+=o.drains;recordSlots+=o.recordSlots;stride0Refused+=o.stride0Refused;fogEntries+=o.fogEntries;fogFlat+=o.fogFlat;fogRefused+=o.fogRefused;fogUnproven+=o.fogUnproven;fogNoRead+=o.fogNoRead;fogBareSkip+=o.fogBareSkip;fogNoPsReg+=o.fogNoPsReg;fogOtherBlend+=o.fogOtherBlend;fogCapped+=o.fogCapped;checkAllocFails+=o.checkAllocFails;dirtyMismatches+=o.dirtyMismatches;restoreFallbacks+=o.restoreFallbacks;texSplits+=o.texSplits;restoreSets+=o.restoreSets;replayedDraws+=o.replayedDraws;replaySkipped+=o.replaySkipped;discarded+=o.discarded;
        conflicts+=o.conflicts;materialized+=o.materialized;recorded+=o.recorded;replaySets+=o.replaySets;conflictBytes+=o.conflictBytes;
        for(int i=0;i<4;++i)capBy[i]+=o.capBy[i];overflowFrames+=o.overflowFrames;overflowAt+=o.overflowAt;overflowCandidates+=o.overflowCandidates;
        deltaWords+=o.deltaWords;arenaHigh=std::max(arenaHigh,o.arenaHigh);recordHigh=std::max(recordHigh,o.recordHigh);
    }
    unsigned totalDeferred()const{return deferred[0]+deferred[1]+deferred[2]+deferred[3];}
    unsigned totalHazards()const{unsigned n=0;for(unsigned h:hazards)n+=h;return n;}
};

// ---- c) guard bookkeeping (pure) ----
// Per raw buffer a list of [lo,hi) byte ranges and the entry (and stream slot, 0xFF = index buffer) owning each. A write conflicts when it is not
// READONLY and is DISCARD, size 0 (whole), or overlaps a live range; NOOVERWRITE outside every range is free. A bounding box per buffer rejects the
// ring-buffer append (the common write) in O(1); only a write inside the box scans the ranges. Slots are reused across frames (no allocation once warm).
class GuardTable{
public:
    struct Range{uint32_t lo,hi,entry;uint8_t slot;bool live;};
    struct Hit{uint32_t buf,range;};
    struct Buf{void* key=nullptr;bool index=false;unsigned live=0;uint32_t boxLo=0,boxHi=0;std::vector<Range> ranges;};
private:
    std::vector<Buf> bufs_;size_t used_=0;
    std::vector<int32_t> table_;                      // open-addressing index of bufs_[0..used_) by key (-1 = empty): adding a draw is O(1), not a scan of every guarded buffer
    static size_t hashKey(const void* k){return (reinterpret_cast<uintptr_t>(k)>>4)*2654435761u;}
    void growTable(){
        size_t n=table_.empty()?64:table_.size();while(n<2*(used_+1))n*=2;
        if(n==table_.size())return;
        table_.assign(n,-1);
        for(size_t b=0;b<used_;++b){size_t i=hashKey(bufs_[b].key)&(n-1);while(table_[i]>=0)i=(i+1)&(n-1);table_[i]=int32_t(b);}
    }
    static bool overlaps(const Range& r,uint64_t lo,uint64_t hi){return r.live&&r.lo<hi&&lo<r.hi;}
public:
    const Buf* find(void* key)const{const int i=findIndex(key);return i<0?nullptr:&bufs_[size_t(i)];}
    int findIndex(void* key)const{
        if(table_.empty())return -1;
        const size_t m=table_.size()-1;
        for(size_t i=hashKey(key)&m;table_[i]>=0;i=(i+1)&m)if(bufs_[size_t(table_[i])].key==key)return table_[i];
        return -1;
    }
    bool guarded(void* key)const{const Buf* b=find(key);return b&&b->live;}
    const Buf& buf(uint32_t i)const{return bufs_[i];}
    size_t buffers()const{return used_;}
    // True when this is the buffer's first live guard (the caller marks the buffer).
    bool add(void* key,bool index,uint32_t lo,uint32_t hi,uint32_t entry,uint8_t slot){
        if(lo>=hi)return false;
        int i=findIndex(key);
        if(i<0){
            if(used_==bufs_.size())bufs_.emplace_back();
            i=int(used_++);Buf& b=bufs_[size_t(i)];b.key=key;b.index=index;b.live=0;b.boxLo=lo;b.boxHi=hi;b.ranges.clear();
            growTable();
            const size_t m=table_.size()-1;size_t t=hashKey(key)&m;while(table_[t]>=0)t=(t+1)&m;table_[t]=i;
        }
        Buf& b=bufs_[size_t(i)];
        const bool first=b.live==0;
        if(first){b.boxLo=lo;b.boxHi=hi;}else{b.boxLo=std::min(b.boxLo,lo);b.boxHi=std::max(b.boxHi,hi);}
        b.ranges.push_back(Range{lo,hi,entry,slot,true});++b.live;
        return first;
    }
    static bool writeConflict(const Buf& b,uint32_t offset,uint32_t size,uint32_t flags){
        if(!b.live||(flags&kLockReadOnly))return false;
        if((flags&kLockDiscard)||!size)return true;
        const uint64_t lo=offset,hi=lo+size;
        if(lo>=b.boxHi||hi<=b.boxLo)return false;
        for(const Range& r:b.ranges)if(overlaps(r,lo,hi))return true;
        return false;
    }
    bool conflicts(void* key,uint32_t offset,uint32_t size,uint32_t flags)const{const Buf* b=find(key);return b&&writeConflict(*b,offset,size,flags);}
    // Appends the live ranges the write conflicts with; returns how many.
    size_t collect(void* key,uint32_t offset,uint32_t size,uint32_t flags,std::vector<Hit>& out)const{
        const int i=findIndex(key);if(i<0||!writeConflict(bufs_[size_t(i)],offset,size,flags))return 0;
        const Buf& b=bufs_[size_t(i)];const bool all=(flags&kLockDiscard)||!size;const uint64_t lo=offset,hi=lo+size;size_t n=0;
        for(size_t k=0;k<b.ranges.size();++k)if(b.ranges[k].live&&(all||overlaps(b.ranges[k],lo,hi))){out.push_back(Hit{uint32_t(i),uint32_t(k)});++n;}
        return n;
    }
    // Drops one range; true when the buffer has no live guard left (the caller unmarks it).
    bool drop(uint32_t buf,uint32_t range){
        Buf& b=bufs_[buf];Range& r=b.ranges[range];
        if(r.live){r.live=false;--b.live;}
        return b.live==0;
    }
    const Range& range(const Hit& h)const{return bufs_[h.buf].ranges[h.range];}
    // Frees the storage (only with no live guard).
    void release(){std::vector<Buf>().swap(bufs_);std::vector<int32_t>().swap(table_);used_=0;}
    // Calls unmark(key) for every buffer that still has a live guard, then forgets all ranges (capacity kept).
    template<class F> void clear(F&& unmark){
        for(size_t i=0;i<used_;++i){Buf& b=bufs_[i];if(b.live)unmark(b.key);b.key=nullptr;b.live=0;b.ranges.clear();}
        if(used_)std::fill(table_.begin(),table_.end(),-1);
        used_=0;
    }
};

// ---- d) the layer (execution) ----
// A texture created with D3DUSAGE_RENDERTARGET or D3DUSAGE_DEPTHSTENCIL (2D or cube; a volume texture cannot be one): its contents change by rendering, which is invisible to the
// layer (SetRenderTarget is not hooked in stream mode), so a pending draw sampling it is refused. A level the driver cannot describe counts as one.
inline bool isRenderTargetTexture(IDirect3DBaseTexture9* t){
    D3DSURFACE_DESC d{};HRESULT hr=D3DERR_INVALIDCALL;
    switch(t->GetType()){
    case D3DRTYPE_TEXTURE:hr=static_cast<IDirect3DTexture9*>(t)->GetLevelDesc(0,&d);break;
    case D3DRTYPE_CUBETEXTURE:hr=static_cast<IDirect3DCubeTexture9*>(t)->GetLevelDesc(0,&d);break;
    default:return false;
    }
    return FAILED(hr)||(d.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL))!=0; // a depth-stencil texture (sampled depth) is written by rendering too
}
enum class DrawType:uint8_t{Primitive,Indexed,PrimitiveUP,IndexedUP};
inline uint32_t vertexCount(unsigned prim,uint32_t primCount){ // D3DPT_* vertex count of primCount primitives
    switch(prim){case 1:return primCount;case 2:return primCount*2;case 3:return primCount+1;case 4:return primCount*3;case 5:case 6:return primCount+2;default:return 0;}
}
// The arguments of the draw call, as the game made it. `first`: StartVertex (Primitive) or StartIndex (Indexed). UP data pointers are only read
// during defer(). Indexed: baseVertex/minIndex/numVertices as in DrawIndexedPrimitive; IndexedUP: minIndex/numVertices/indexData/indexFormat/vertexData.
struct DrawArgs{
    DrawType type=DrawType::Primitive;D3DPRIMITIVETYPE prim=D3DPT_TRIANGLELIST;
    INT baseVertex=0;UINT minIndex=0,numVertices=0,first=0,primCount=0;
    const void* vertexData=nullptr;UINT stride=0;const void* indexData=nullptr;D3DFORMAT indexFormat=D3DFMT_INDEX16;
    const NorthlightShaderReads::ReadSet* vsReads=nullptr;const NorthlightShaderReads::ReadSet* psReads=nullptr; // what the bound shaders can read (scanned at creation; null or !exact: everything)
    uint64_t vsHash=0;      // the bound vertex shader's hash (SPELLFOG diagnostics only)
    uint8_t vsFog=0;        // the bound vertex shader's fog output: 0 not proven, 1 provably min(pow(max(ramp, 0), c30.z), 1), 2 the same with the bare ramp in the pow (spell_fog_proof.h, at registration): only then is c30 replaced
    int psFogReg=-1;     // the bound pixel shader's proven fog colour register (legacy_fog.h ps3FogColorRegister; -1: not a proven ps_3_0 combiner): the alpha sub-class folds Northlight's in-scatter into it
    const void* vsFor=nullptr;const void* psFor=nullptr; // the shaders vsReads/psReads were looked up for: defer() uses a read set only when the shader it records is the same one
    unsigned psMajor=0;  // shader model major version of the bound pixel shader (from its version token at registration; 0 = unknown): defer() admits only >= kMinPsMajor
    static DrawArgs primitive(D3DPRIMITIVETYPE p,UINT start,UINT count){DrawArgs a;a.type=DrawType::Primitive;a.prim=p;a.first=start;a.primCount=count;return a;}
    static DrawArgs indexed(D3DPRIMITIVETYPE p,INT base,UINT minIdx,UINT num,UINT startIndex,UINT count){
        DrawArgs a;a.type=DrawType::Indexed;a.prim=p;a.baseVertex=base;a.minIndex=minIdx;a.numVertices=num;a.first=startIndex;a.primCount=count;return a;
    }
    static DrawArgs primitiveUP(D3DPRIMITIVETYPE p,UINT count,const void* data,UINT stride){
        DrawArgs a;a.type=DrawType::PrimitiveUP;a.prim=p;a.primCount=count;a.vertexData=data;a.stride=stride;return a;
    }
    static DrawArgs indexedUP(D3DPRIMITIVETYPE p,UINT minIdx,UINT num,UINT count,const void* idx,D3DFORMAT fmt,const void* data,UINT stride){
        DrawArgs a;a.type=DrawType::IndexedUP;a.prim=p;a.minIndex=minIdx;a.numVertices=num;a.primCount=count;a.indexData=idx;a.indexFormat=fmt;a.vertexData=data;a.stride=stride;return a;
    }
};
// The device state cache (DeviceMirror, device_mirror.h) as the layer reads it in bulk: pointers to its arrays and known flags (same layout and names),
// valid only between enter() and leave(). enter() takes the cache's guard and returns false when the cache cannot be used right now (disabled,
// recording a state block, raw scope): the layer then reads with Get calls. A slot whose known flag is false is read with the Get call that also teaches it.
// A known slot holds exactly what the Get call would return (the cache's own contract), so reading it here is the same read.
struct MirrorView{
    const DWORD* rs=nullptr;const bool* rsKnown=nullptr;                              // [256] by D3DRS id
    const DWORD (*smp)[16]=nullptr;const bool (*smpKnown)[16]=nullptr;                // [16 stages][16 sampler state types]
    IDirect3DBaseTexture9* const* tex=nullptr;const bool* texKnown=nullptr;           // [16]
    const float (*vsF)[4]=nullptr;const bool* vsFKnown=nullptr;                       // [256]
    const float (*psF)[4]=nullptr;const bool* psFKnown=nullptr;                       // [224]
    const int (*vsI)[4]=nullptr;const bool* vsIKnown=nullptr;                         // [16]
    const BOOL* vsB=nullptr;const bool* vsBKnown=nullptr;                             // [16]
    IDirect3DVertexShader9* const* vs=nullptr;const bool* vsKnown=nullptr;
    IDirect3DPixelShader9* const* ps=nullptr;const bool* psKnown=nullptr;
    IDirect3DVertexDeclaration9* const* decl=nullptr;const bool* declKnown=nullptr;
    const DWORD* fvf=nullptr;const bool* fvfKnown=nullptr;
    const D3DVIEWPORT9* vp=nullptr;const bool* vpKnown=nullptr;
    bool (*enter)(void* ctx)=nullptr;void (*leave)(void* ctx)=nullptr;void* ctx=nullptr;
};
struct Config{
    bool on=false;unsigned classMask=SubAdditive|SubAlpha|SubModulate|SubOther;
    unsigned maxEntries=4096,maxFixed=512; // maxFixed: fixed-function entries (each carries a 2.2 KB FfRec, allocated in chunks of 8)
    size_t arenaBytes=3u<<20,arenaStartBytes=512u<<10,uploadVbBytes=3u<<20,uploadIbBytes=768u<<10,deltaBytes=2u<<20; // uploadVbBytes matches arenaBytes: every copied vertex byte is uploaded once (a 1 MiB VB overflowed before the 3 MiB arena with ~1300 entries); the caps (entries, delta bytes, arena bytes, upload bytes) bound the layer's memory; deltaBytes: cap of the state delta storage, allocated on demand in chunks of deltaChunkWords words (an entry's delta never spans two)
    bool conflictCopy=true;                       // false on a non-DXVK backend: a conflicting write flushes instead of copying
    DWORD readBackLockFlags=D3DLOCK_READONLY;     // NorthlightUpload::readBackLock()
    void (*markGuard)(void* ctx,void* rawBuffer,bool guarded)=nullptr;void* markCtx=nullptr; // sets/clears the per-buffer "call onBufferWrite" flag
    NorthlightStateBlockPool* savedPool=nullptr;  // the renderer's SavedState pool for the replay scope (optional; used only when minimalRestore is off or a drain cannot use it)
    unsigned maxFogFits=16;                       // new fits per frame (each is a few microseconds); beyond it the game's constant is kept
    bool fogOn=false;                             // SpellFog: the fog slots of a record carry the game's value folded with Northlight's fog (spell_fog.h) instead of the game's value
    const NorthlightSpellFog::Frame* fog=nullptr; // Northlight's fog for the frame (the previous frame's estimate), owned by the integration; null or inactive: the game's values
    bool fogLog=false;                            // keep the first samples of each sub-class for the SPELLFOG diagnostics line (takeFogSamples)
    bool dirtyRecord=true;                        // a record examines only the slots whose synced bit (SyncedSlots, kept by the device mirror's setters) is clear; false: every slot of the read set, as before
    bool dirtyDisableOnMismatch=false;            // fail safe: the first record whose synced-bit delta differs from the full compare's switches dirtyRecord off for good (the full compare's delta is used)
    bool dirtyCheck=false;                        // tests: run both and count a difference in dirtyMismatches (the full compare's delta is the one used)
    SyncedSlots* synced=nullptr;                  // the bits; null (or the state cache unusable for a record): the full compare
    bool minimalRestore=true;                     // a drain restores exactly the slots it set (read before the first Set, restored after the last draw) instead of a D3DSBT_ALL state block; false: always the state block
    unsigned maxOrderSplits=8;                    // water, rain and name splits per frame; the next one is the Order hazard (flush, fallback, cooldown), like maxTexSplits
    unsigned maxTexSplits=8;                      // texture writes to a texture a pending draw samples are answered with a split (drain, keep deferring) up to this many per frame; the next one is a TexWrite hazard
    unsigned deltaChunkWords=16384;               // 64 KiB chunks (at most 65535: entries address a delta with 16-bit offsets)
    const struct MirrorView* source=nullptr;      // the device state cache for bulk reads (optional; null: every state is read with Get calls)
    uint64_t (*clock)(void* ctx)=nullptr;bool (*timing)(void* ctx)=nullptr;void* clockCtx=nullptr; // phase timing of the drains (diagnostics): timing() is asked once per drain, only when it says yes does the layer read clock() around save, upload and restore; null = never
    unsigned (*generation)(void* ctx)=nullptr;void* genCtx=nullptr; // changes whenever a texture object may have been created or destroyed (a raw address may name another texture): keys the per-texture render-target verdicts; null = no verdict cache
};

// The state of one deferred draw. Admission (defer() refuses everything else): a vertex shader AND a pixel shader of model >= kMinPsMajor are
// bound, or neither (a fixed-function draw). With both programmable stages bound D3D9 does not read the fixed-function state: texture stage states,
// the world/view/projection/texture transforms, lights and material are unused by the draw, so none of them is recorded. A fixed-function entry
// additionally has an FfRec (transforms, texture stage states of stages 0..7, material, lights 0..7); it is refused when vertex blending is enabled
// (WORLD1..3), and a replay diffs it against the previous fixed-function entry only (programmable entries leave that state alone), and it reads no shader
// constants and the texture stages 0..7 only. Everything else a draw can read is recorded as a DELTA against the replay model (Model, Op): all render
// states, the sampler states 1..13 of every stage with a texture bound, the constants of both stages, viewport, scissor rect (only with the scissor test),
// the user clip planes (only the enabled ones). A shader whose bytecode was scanned (DrawArgs::vsReads/psReads, shader_read_set.h) narrows the constants
// to the registers it reads and the texture stages to the samplers it declares (the vertex texture samplers 257..260 only when the vertex shader declares
// samplers); without a read set a stage reads everything. Textures, buffers, shaders and declarations are referenced once per frame by the layer (held
// table) because a delta is replayed after the game released them. Streams and index buffer are bound by drawEntry, not by the delta.
constexpr unsigned kVsFloat=256,kPsFloat=224,kInts=16,kBools=16,kClipPlanes=6,kSamplerFirst=1,kSamplerStates=13;
// The fixed-function state a draw with neither vertex nor pixel shader reads, recorded in FfRec (only for such draws): WORLD, VIEW, PROJECTION and
// TEXTURE0..7 transforms, the 18 valid texture stage states of stages 0..7, the material and lights 0..7. WORLD1..3 are never read: vertex blending is refused.
constexpr unsigned kFfTransforms=11,kFfStages=8,kTssStates=18,kFfLights=8;
inline constexpr uint16_t kFfTransformIds[kFfTransforms]={256,2,3,16,17,18,19,20,21,22,23}; // D3DTS_WORLD, VIEW, PROJECTION, TEXTURE0..7
inline constexpr uint16_t kTssIds[kTssStates]={1,2,3,4,5,6,7,8,9,10,11,22,23,24,26,27,28,32}; // D3DTSS_COLOROP .. D3DTSS_CONSTANT (the types that exist; 12..21, 25, 29..31 do not)
#ifndef NORTHLIGHT_SPELL_LAYER_TEST_API
static_assert(unsigned(D3DTS_WORLD)==256&&unsigned(D3DTS_VIEW)==2&&unsigned(D3DTS_PROJECTION)==3&&unsigned(D3DTS_TEXTURE0)==16&&unsigned(D3DTS_TEXTURE7)==23,"transform ids");
static_assert(unsigned(D3DTSS_COLOROP)==1&&unsigned(D3DTSS_COLORARG1)==2&&unsigned(D3DTSS_COLORARG2)==3&&unsigned(D3DTSS_ALPHAOP)==4&&unsigned(D3DTSS_ALPHAARG1)==5&&unsigned(D3DTSS_ALPHAARG2)==6&&
    unsigned(D3DTSS_BUMPENVMAT00)==7&&unsigned(D3DTSS_BUMPENVMAT01)==8&&unsigned(D3DTSS_BUMPENVMAT10)==9&&unsigned(D3DTSS_BUMPENVMAT11)==10&&unsigned(D3DTSS_TEXCOORDINDEX)==11&&
    unsigned(D3DTSS_BUMPENVLSCALE)==22&&unsigned(D3DTSS_BUMPENVLOFFSET)==23&&unsigned(D3DTSS_TEXTURETRANSFORMFLAGS)==24&&unsigned(D3DTSS_COLORARG0)==26&&unsigned(D3DTSS_ALPHAARG0)==27&&
    unsigned(D3DTSS_RESULTARG)==28&&unsigned(D3DTSS_CONSTANT)==32,"texture stage state ids");
#endif
// Every valid D3DRENDERSTATETYPE (103 values; d3d9types.h), id and name. The real build asserts each value against the SDK.
#define NL_SPELL_RS_LIST(X) \
    X(ZENABLE,7) X(FILLMODE,8) X(SHADEMODE,9) X(ZWRITEENABLE,14) X(ALPHATESTENABLE,15) X(LASTPIXEL,16) X(SRCBLEND,19) X(DESTBLEND,20) X(CULLMODE,22) \
    X(ZFUNC,23) X(ALPHAREF,24) X(ALPHAFUNC,25) X(DITHERENABLE,26) X(ALPHABLENDENABLE,27) X(FOGENABLE,28) X(SPECULARENABLE,29) X(FOGCOLOR,34) \
    X(FOGTABLEMODE,35) X(FOGSTART,36) X(FOGEND,37) X(FOGDENSITY,38) X(RANGEFOGENABLE,48) X(STENCILENABLE,52) X(STENCILFAIL,53) X(STENCILZFAIL,54) \
    X(STENCILPASS,55) X(STENCILFUNC,56) X(STENCILREF,57) X(STENCILMASK,58) X(STENCILWRITEMASK,59) X(TEXTUREFACTOR,60) \
    X(WRAP0,128) X(WRAP1,129) X(WRAP2,130) X(WRAP3,131) X(WRAP4,132) X(WRAP5,133) X(WRAP6,134) X(WRAP7,135) \
    X(CLIPPING,136) X(LIGHTING,137) X(AMBIENT,139) X(FOGVERTEXMODE,140) X(COLORVERTEX,141) X(LOCALVIEWER,142) X(NORMALIZENORMALS,143) \
    X(DIFFUSEMATERIALSOURCE,145) X(SPECULARMATERIALSOURCE,146) X(AMBIENTMATERIALSOURCE,147) X(EMISSIVEMATERIALSOURCE,148) X(VERTEXBLEND,151) X(CLIPPLANEENABLE,152) \
    X(POINTSIZE,154) X(POINTSIZE_MIN,155) X(POINTSPRITEENABLE,156) X(POINTSCALEENABLE,157) X(POINTSCALE_A,158) X(POINTSCALE_B,159) X(POINTSCALE_C,160) \
    X(MULTISAMPLEANTIALIAS,161) X(MULTISAMPLEMASK,162) X(PATCHEDGESTYLE,163) X(DEBUGMONITORTOKEN,165) X(POINTSIZE_MAX,166) X(INDEXEDVERTEXBLENDENABLE,167) \
    X(COLORWRITEENABLE,168) X(TWEENFACTOR,170) X(BLENDOP,171) X(POSITIONDEGREE,172) X(NORMALDEGREE,173) X(SCISSORTESTENABLE,174) X(SLOPESCALEDEPTHBIAS,175) \
    X(ANTIALIASEDLINEENABLE,176) X(MINTESSELLATIONLEVEL,178) X(MAXTESSELLATIONLEVEL,179) X(ADAPTIVETESS_X,180) X(ADAPTIVETESS_Y,181) X(ADAPTIVETESS_Z,182) \
    X(ADAPTIVETESS_W,183) X(ENABLEADAPTIVETESSELLATION,184) X(TWOSIDEDSTENCILMODE,185) X(CCW_STENCILFAIL,186) X(CCW_STENCILZFAIL,187) X(CCW_STENCILPASS,188) \
    X(CCW_STENCILFUNC,189) X(COLORWRITEENABLE1,190) X(COLORWRITEENABLE2,191) X(COLORWRITEENABLE3,192) X(BLENDFACTOR,193) X(SRGBWRITEENABLE,194) X(DEPTHBIAS,195) \
    X(WRAP8,198) X(WRAP9,199) X(WRAP10,200) X(WRAP11,201) X(WRAP12,202) X(WRAP13,203) X(WRAP14,204) X(WRAP15,205) \
    X(SEPARATEALPHABLENDENABLE,206) X(SRCBLENDALPHA,207) X(DESTBLENDALPHA,208) X(BLENDOPALPHA,209)
#define NL_SPELL_RS_ID(n,v) v,
inline constexpr uint16_t kRenderStateIds[]={NL_SPELL_RS_LIST(NL_SPELL_RS_ID)};
constexpr unsigned kRenderStates=unsigned(sizeof kRenderStateIds/sizeof kRenderStateIds[0]);
static_assert(kRenderStates==103,"the full D3D9 render state list");
#ifndef NORTHLIGHT_SPELL_LAYER_TEST_API
#define NL_SPELL_RS_CHECK(n,v) static_assert(unsigned(D3DRS_##n)==v,"D3DRS_" #n);
NL_SPELL_RS_LIST(NL_SPELL_RS_CHECK)
#undef NL_SPELL_RS_CHECK
#endif
#undef NL_SPELL_RS_ID
// States the backend gives side effects beyond a stored value (vendor hacks: POINTSIZE carries the RESZ/alpha-to-coverage codes, ADAPTIVETESS_X/Y the
// ATOC/A2M codes). Replay sets them only when the device value differs from the record, so a re-set never repeats the action.
inline constexpr bool vendorHackState(unsigned id){return id==154||id==180||id==181;}
constexpr unsigned rsIndex(unsigned id){for(unsigned i=0;i<kRenderStates;++i)if(kRenderStateIds[i]==id)return i;return kRenderStates;}
constexpr unsigned kRsClipPlaneEnable=rsIndex(152),kRsScissorTest=rsIndex(174),kRsVertexBlend=rsIndex(151),kRsIndexedVertexBlend=rsIndex(167); // indices into kRenderStateIds
inline DWORD samplerStage(unsigned slot){return slot<16?DWORD(slot):DWORD(257+(slot-16));}

// What a draw can read of the programmable state: bit i of vsF/psF is constant register i, vsI/vsB/psI/psB the int and bool registers, tex the texture slots
// (0..15 pixel samplers, 16..19 the vertex texture samplers 257..260: the binding and the sampler states). Everything else is not recorded and not replayed.
// The default is "everything" (a shader read set narrows it).
struct Reads{
    uint64_t vsF[4]={~0ull,~0ull,~0ull,~0ull},psF[4]={~0ull,~0ull,~0ull,(1ull<<(kPsFloat-192))-1};
    uint16_t vsI=0xFFFF,vsB=0xFFFF,psI=0xFFFF,psB=0xFFFF;uint32_t tex=(1u<<kMaxTextures)-1;
    void noConstants(){for(auto& w:vsF)w=0;for(auto& w:psF)w=0;vsI=vsB=psI=psB=0;}
};
// The per-entry state delta: a word stream, the changes the replay must make before this draw, relative to what the entries before it in the same replay
// sequence already made (see Model). Word: op<<26 | a<<16 | b, then the payload. Rs: a = index in kRenderStateIds, 1 value; RsCheck: the same, but the replay
// reads the device first and sets only when it differs (vendor-hack states on a sequence's first entry); Ss: a = slot*13+k, 1 value; Tex: a = slot, a pointer;
// VsF/PsF/VsI/PsI: a = first register, b = count, count*4 words; VsB/PsB: count words; Viewport: 6 words; Scissor: 4; Clip: a = plane, 4 words; Vs/Ps/Decl: a pointer; Fvf: 1 value.
namespace Op{enum:uint32_t{None,Rs,RsCheck,Ss,Tex,VsF,PsF,VsI,PsI,VsB,PsB,Viewport,Scissor,Clip,Vs,Ps,Decl,Fvf};}
constexpr unsigned kPtrWords=sizeof(void*)/4;
static_assert(sizeof(void*)%4==0&&sizeof(D3DVIEWPORT9)==24&&sizeof(RECT)==16,"delta payload sizes");
inline constexpr uint32_t opWord(uint32_t op,uint32_t a,uint32_t b){return op<<26|a<<16|b;}
// One decoder for both consumers (the model at record time and the device at replay), so they cannot disagree about what a delta means.
template<class S> inline void decodeOps(const uint32_t* w,const uint32_t* end,S& s){
    auto ptr=[&](){void* p;std::memcpy(&p,w,sizeof p);w+=kPtrWords;return p;};
    while(w<end){
        const uint32_t h=*w++,a=(h>>16)&1023,b=h&0xFFFF;
        switch(h>>26){
        case Op::Rs:s.rs(a,*w++,false);break;
        case Op::RsCheck:s.rs(a,*w++,true);break;
        case Op::Ss:s.ss(a,*w++);break;
        case Op::Tex:s.tex(a,ptr());break;
        case Op::VsF:s.vsF(a,b,w);w+=b*4;break;
        case Op::PsF:s.psF(a,b,w);w+=b*4;break;
        case Op::VsI:s.vsI(a,b,w);w+=b*4;break;
        case Op::PsI:s.psI(a,b,w);w+=b*4;break;
        case Op::VsB:s.vsB(a,b,w);w+=b;break;
        case Op::PsB:s.psB(a,b,w);w+=b;break;
        case Op::Viewport:s.viewport(w);w+=sizeof(D3DVIEWPORT9)/4;break;
        case Op::Scissor:s.scissor(w);w+=4;break;
        case Op::Clip:s.clip(a,w);w+=4;break;
        case Op::Vs:s.vs(ptr());break;
        case Op::Ps:s.ps(ptr());break;
        case Op::Decl:s.decl(ptr());break;
        case Op::Fvf:s.fvf(*w++);break;
        default:return;
        }
    }
}
// Byte i of word w is 1 when render state id w*8+i is one of the listed ones (the cache's known flags are 0/1 bytes: eight are tested with one and).
constexpr std::array<uint64_t,32> rsKnownMaskTable(){
    std::array<uint64_t,32> t{};
    for(unsigned i=0;i<kRenderStates;++i)t[kRenderStateIds[i]/8]|=1ull<<((kRenderStateIds[i]%8)*8);
    return t;
}
constexpr std::array<uint64_t,32> kRsKnownMask=rsKnownMaskTable();
constexpr std::array<uint64_t,4> rsIdMaskTable(){std::array<uint64_t,4> t{};for(unsigned i=0;i<kRenderStates;++i)t[kRenderStateIds[i]/64]|=1ull<<(kRenderStateIds[i]%64);return t;}
constexpr std::array<uint64_t,4> kRsIdMask=rsIdMaskTable();                // bit per valid D3DRS id
constexpr std::array<uint8_t,256> rsIndexTable(){std::array<uint8_t,256> t{};for(auto& x:t)x=255;for(unsigned i=0;i<kRenderStates;++i)t[kRenderStateIds[i]]=uint8_t(i);return t;}
constexpr std::array<uint8_t,256> kRsIndexOfId=rsIndexTable();             // id -> index in kRenderStateIds (255: not a state)
// known[0..n) all set: the flags are 0/1 bytes, eight are tested per load.
inline bool runKnown(const bool* known,unsigned n){
    unsigned i=0;
    for(;i+8<=n;i+=8){uint64_t w;std::memcpy(&w,known+i,8);if(w!=0x0101010101010101ull)return false;}
    for(;i<n;++i)if(!known[i])return false;
    return true;
}
inline void setBits(uint64_t* v,unsigned start,unsigned n){for(unsigned i=start;i<start+n;++i)v[i>>6]|=1ull<<(i&63);}
// The replay model: for each slot, the value the running replay has set by the time the next entry is replayed (valid only for slots set by an earlier
// entry of the same replay sequence; at the start of a sequence everything is unknown, the replay begins from whatever the effects left on the device).
// A record emits only the slots whose value differs from the model or is unknown in it, so the replay needs no compare.
struct Model{
    DWORD rs[256];bool rsValid;                                   // by D3DRS id, valid as a whole: the first entry sets every state
    DWORD ss[kMaxTextures][kSamplerStates];uint32_t ssValid;      // bit per slot: all its sampler states are set
    void* tex[kMaxTextures];uint32_t texValid;
    float vsF[kVsFloat][4];uint64_t vsFV[4];float psF[kPsFloat][4];uint64_t psFV[4];
    int vsI[kInts][4];BOOL vsB[kBools];int psI[kInts][4];BOOL psB[kBools];uint64_t vsIV,vsBV,psIV,psBV;
    D3DVIEWPORT9 vp;bool vpV;RECT scissor;bool scissorV;float clip[kClipPlanes][4];uint8_t clipV;
    void* vs;void* ps;bool vsV,psV;void* decl;DWORD fvf;uint8_t declMode,declSeen; // declMode: 0 unknown, 1 declaration, 2 FVF; declSeen: bit 0 a declaration, bit 1 an FVF was set by some entry of the sequence
    void reset(){
        std::memset(rs,0,sizeof rs);rsValid=false;ssValid=texValid=0;
        for(auto& w:vsFV)w=0;for(auto& w:psFV)w=0;vsIV=vsBV=psIV=psBV=0;
        vpV=scissorV=false;clipV=0;vsV=psV=false;declMode=declSeen=0;
    }
};
struct ModelSink{
    Model& m;
    void rs(unsigned i,DWORD v,bool){m.rs[kRenderStateIds[i]]=v;m.rsValid=true;}
    void ss(unsigned a,DWORD v){const unsigned t=a/kSamplerStates;m.ss[t][a%kSamplerStates]=v;m.ssValid|=1u<<t;}
    void tex(unsigned t,void* p){m.tex[t]=p;m.texValid|=1u<<t;}
    void vsF(unsigned s,unsigned n,const uint32_t* w){std::memcpy(m.vsF[s],w,size_t(n)*16);setBits(m.vsFV,s,n);}
    void psF(unsigned s,unsigned n,const uint32_t* w){std::memcpy(m.psF[s],w,size_t(n)*16);setBits(m.psFV,s,n);}
    void vsI(unsigned s,unsigned n,const uint32_t* w){std::memcpy(m.vsI[s],w,size_t(n)*16);setBits(&m.vsIV,s,n);}
    void psI(unsigned s,unsigned n,const uint32_t* w){std::memcpy(m.psI[s],w,size_t(n)*16);setBits(&m.psIV,s,n);}
    void vsB(unsigned s,unsigned n,const uint32_t* w){std::memcpy(m.vsB+s,w,size_t(n)*4);setBits(&m.vsBV,s,n);}
    void psB(unsigned s,unsigned n,const uint32_t* w){std::memcpy(m.psB+s,w,size_t(n)*4);setBits(&m.psBV,s,n);}
    void viewport(const uint32_t* w){std::memcpy(&m.vp,w,sizeof m.vp);m.vpV=true;}
    void scissor(const uint32_t* w){std::memcpy(&m.scissor,w,sizeof m.scissor);m.scissorV=true;}
    void clip(unsigned i,const uint32_t* w){std::memcpy(m.clip[i],w,16);m.clipV|=uint8_t(1u<<i);}
    void vs(void* p){m.vs=p;m.vsV=true;}
    void ps(void* p){m.ps=p;m.psV=true;}
    void decl(void* p){m.decl=p;m.declMode=1;m.declSeen|=1;}
    void fvf(DWORD v){m.fvf=v;m.declMode=2;m.declSeen|=2;}
};

// The bytes of one vertex of an FVF (D3DFVF_*): position (XYZ 12, XYZRHW 16, XYZW 16, XYZB1..5 12 + 4 per weight; the LASTBETA flags change the type of the last weight, not its size), normal 12,
// point size 4, diffuse 4, specular 4, and per texture set 8 bytes, or the D3DFVF_TEXCOORDSIZE of that set (code 0: 2 floats, 1: 3, 2: 4, 3: 1). False: no position, so no valid vertex.
inline bool fvfVertexSize(DWORD fvf,uint32_t& size){
    uint32_t n=0;
    switch(fvf&0x400E){case 0x2:n=12;break;case 0x4:n=16;break;case 0x4002:n=16;break;case 0x6:n=16;break;case 0x8:n=20;break;case 0xA:n=24;break;case 0xC:n=28;break;case 0xE:n=32;break;default:return false;}
    if(fvf&0x10)n+=12;if(fvf&0x20)n+=4;if(fvf&0x40)n+=4;if(fvf&0x80)n+=4;
    const unsigned sets=(fvf>>8)&0xF;if(sets>8)return false;
    for(unsigned i=0;i<sets;++i){static const uint8_t bytes[4]={8,12,16,4};n+=bytes[(fvf>>(16+2*i))&3];}
    size=n;return true;
}
// The first op in which two deltas differ, for the one-off SPELLDIRTY line: kind name, slot index (render state id, sampler state a = stage*13+type-1, texture stage, first register of a
// run, ...), and a value of each side (0 when that side has no such op; constant runs: count << 32 | a hash of their words).
struct DeltaDiff{
    bool have=false;char kind[12]={};unsigned index=0;uint64_t dirty=0,full=0;
};
class DeltaLister{
public:
    struct Op{uint8_t kind;unsigned index;uint64_t value;};
    std::vector<Op> ops;
    static const char* name(unsigned k){static const char* const n[]={"rs","ss","tex","vsF","psF","vsI","psI","vsB","psB","viewport","scissor","clip","vs","ps","decl","fvf"};return k<16?n[k]:"?";}
    static uint64_t run(unsigned n,const uint32_t* w,unsigned words){uint32_t h=2166136261u;for(unsigned i=0;i<words;++i)h=(h^w[i])*16777619u;return uint64_t(n)<<32|h;}
    void rs(unsigned i,DWORD v,bool){ops.push_back({0,kRenderStateIds[i],v});}
    void ss(unsigned a,DWORD v){ops.push_back({1,a,v});}
    void tex(unsigned t,void* p){ops.push_back({2,t,uint64_t(reinterpret_cast<uintptr_t>(p))});}
    void vsF(unsigned s,unsigned n,const uint32_t* w){ops.push_back({3,s,run(n,w,n*4)});}
    void psF(unsigned s,unsigned n,const uint32_t* w){ops.push_back({4,s,run(n,w,n*4)});}
    void vsI(unsigned s,unsigned n,const uint32_t* w){ops.push_back({5,s,run(n,w,n*4)});}
    void psI(unsigned s,unsigned n,const uint32_t* w){ops.push_back({6,s,run(n,w,n*4)});}
    void vsB(unsigned s,unsigned n,const uint32_t* w){ops.push_back({7,s,run(n,w,n)});}
    void psB(unsigned s,unsigned n,const uint32_t* w){ops.push_back({8,s,run(n,w,n)});}
    void viewport(const uint32_t* w){ops.push_back({9,0,run(1,w,sizeof(D3DVIEWPORT9)/4)});}
    void scissor(const uint32_t* w){ops.push_back({10,0,run(1,w,4)});}
    void clip(unsigned i,const uint32_t* w){ops.push_back({11,i,run(1,w,4)});}
    void vs(void* p){ops.push_back({12,0,uint64_t(reinterpret_cast<uintptr_t>(p))});}
    void ps(void* p){ops.push_back({13,0,uint64_t(reinterpret_cast<uintptr_t>(p))});}
    void decl(void* p){ops.push_back({14,0,uint64_t(reinterpret_cast<uintptr_t>(p))});}
    void fvf(DWORD v){ops.push_back({15,0,v});}
    static DeltaDiff diff(const uint32_t* a,unsigned na,const uint32_t* b,unsigned nb){
        DeltaLister x,y;decodeOps(a,a+na,x);decodeOps(b,b+nb,y);
        DeltaDiff d;d.have=true;auto put=[&](const char* k,unsigned i,uint64_t va,uint64_t vb){std::snprintf(d.kind,sizeof d.kind,"%s",k);d.index=i;d.dirty=va;d.full=vb;};
        const size_t n=std::min(x.ops.size(),y.ops.size());
        for(size_t i=0;i<n;++i)if(x.ops[i].kind!=y.ops[i].kind||x.ops[i].index!=y.ops[i].index||x.ops[i].value!=y.ops[i].value){
            if(x.ops[i].kind==y.ops[i].kind&&x.ops[i].index==y.ops[i].index)put(name(x.ops[i].kind),x.ops[i].index,x.ops[i].value,y.ops[i].value);
            else put(name(y.ops[i].kind),y.ops[i].index,0,y.ops[i].value); // the full compare has an op the dirty path lacks (the usual case: a changed slot it did not examine)
            return d;
        }
        if(x.ops.size()>n)put(name(x.ops[n].kind),x.ops[n].index,x.ops[n].value,0);
        else if(y.ops.size()>n)put(name(y.ops[n].kind),y.ops[n].index,0,y.ops[n].value);
        else put("words",0,na,nb);
        return d;
    }
};

#ifdef NORTHLIGHT_SPELL_LAYER_TEST_API
inline bool testFailAlloc=false; // tests: the held table and the cross-check scratch report an allocation failure
#endif
class Layer{
    struct Stream{IDirect3DVertexBuffer9* vb=nullptr;uint32_t off=0,stride=0,lo=0,hi=0,sno=0;int32_t blob=-1;};
    struct FfRec{                                     // only for fixed-function entries (Entry::ff), allocated in chunks of kFfChunk
        D3DMATRIX tf[kFfTransforms];DWORD tss[kFfStages][kTssStates];D3DMATERIAL9 mat;
        D3DLIGHT9 light[kFfLights];BOOL lightOn[kFfLights];bool lightSet[kFfLights]; // a light never set fails GetLight: lightSet=false (light zeroed, off)
    };
    static constexpr unsigned kFfChunk=8; // fixed-function records and delta words are allocated in chunks as a frame needs them and released by trim()
    struct Held{IUnknown* p=nullptr;uint16_t aux=0;uint8_t flags=0;};   // a reference the frame holds once per object (see intern())
    static constexpr uint8_t kNotRt=1,kInSamples=2,kHasAux=4;
    struct Entry{
        DrawType type=DrawType::Primitive;D3DPRIMITIVETYPE prim=D3DPT_TRIANGLELIST;
        INT base=0;UINT minIndex=0,numVertices=0,first=0,primCount=0;
        uint8_t nStreams=0,isz=0;Stream streams[kMaxStreams];
        IDirect3DIndexBuffer9* ib=nullptr;uint32_t ibLo=0,ibHi=0;int32_t ibBlob=-1; // streams and ib are kept alive by the frame's held references
        uint32_t upVtx=0,upStride=0,upIdx=0;D3DFORMAT upFmt=D3DFMT_INDEX16;
        uint16_t dChunk=0,dOff=0,dLen=0;               // the state delta: dLen words at dOff of delta chunk dChunk
        FfRec* ff=nullptr;                             // ff: set for fixed-function draws (no vertex and no pixel shader), else null
    };
    // What replay itself set on the device (streams and index buffer are rebound for materialized spans, so they are tracked apart from the records).
    struct Shadow{
        struct S{IDirect3DVertexBuffer9* vb=nullptr;UINT off=0,stride=0;bool valid=false;} st[16];
        uint32_t freq=0;IDirect3DIndexBuffer9* ib=nullptr;bool ibValid=false;
    };
    // The minimal restore's undo log: the device state as it was before the first Set of each slot the sequence touches (b: values, constants and sampler states; the
    // typed members: references the log holds until the restore, taken by Get calls). Only the slots model_ marks valid, the touched streams/index buffer and the
    // fixed-function state are filled; the rest of the struct is stale and never read.
    struct Undo{
        Model b;IDirect3DBaseTexture9* tex[kMaxTextures];IDirect3DVertexShader9* vs;IDirect3DPixelShader9* ps;IDirect3DVertexDeclaration9* decl;DWORD fvf;
        struct S{IDirect3DVertexBuffer9* vb;UINT off,stride,freq;} st[16];IDirect3DIndexBuffer9* ib;
        IDirect3DSurface9* rt[4];IDirect3DSurface9* ds;D3DVIEWPORT9 vp;RECT sc;FfRec ff;
    };
    // Conflict copy of one merged byte span of a buffer; entries share it. fmt (index blobs): 0 = 16-bit, 1 = 32-bit indices; the blob sits in the
    // upload index buffer of that format.
    struct Blob{uint32_t arenaOff,len,base,uploadOff;bool index;uint8_t fmt;};
    struct Seg{uint32_t lo,hi;size_t firstHit,lastHit;};
public:
    // The last event that made the frame fall back (a hazard that flushed or a refused defer), for the integration's diagnostics. eventSeq() changes
    // with every event; pending: entries pending when it happened (0: nothing was flushed, the frame still fell back).
    struct Event{Reason reason=Reason::Count;InPlaceKind by=InPlaceKind::None;unsigned pending=0;};
    static constexpr size_t modelBytes(){return sizeof(Model);}
    static constexpr size_t entryBytes(){return sizeof(Entry);}
    static constexpr size_t ffRecordBytes(){return sizeof(FfRec);}
private:
    IDirect3DDevice9* dev_=nullptr;Config cfg_;FrameState fm_;
    static constexpr unsigned kEntryChunk=256;           // entries are allocated in chunks as a frame needs them
    std::vector<std::unique_ptr<Entry[]>> entryChunks_;unsigned count_=0;
    Entry& entryAt(unsigned i){return entryChunks_[i/kEntryChunk][i%kEntryChunk];}
    bool ensureEntry(unsigned i){
        while(i/kEntryChunk>=entryChunks_.size()){Entry* p=new(std::nothrow) Entry[kEntryChunk];if(!p)return false;entryChunks_.emplace_back(p);}
        return true;
    }
    std::vector<std::unique_ptr<uint32_t[]>> deltaChunks_;unsigned deltaChunk_=0,deltaUsed_=0; // state deltas of the pending entries; deltaChunk_/deltaUsed_: the chunk being filled and its used words
    struct SyncPend{uint64_t rs[4]={},vsF[4]={},psF[4]={};uint16_t ss[16]={};uint16_t tex=0;bool vp=false;unsigned slots=0;bool fogVs=false;int fogPs=-1,fogApplied=0;uint8_t fogFlat=0,fogRefused=0,fogUnproven=0,fogNoRead=0,fogNoPsReg=0,fogOtherBlend=0,fogBareSkip=0;}; // fogVs/fogPs: the fog rows of this record (they hold Northlight's value, never synced)
    std::unique_ptr<uint32_t[]> checkScratch_;         // the dirty cross-check's copy of the first delta (kScratchWords)
    DeltaDiff dirtyFirst_;bool dirtyTaken_=false;       // the first synced-bit mismatch of the session (takeDirtyMismatch)
    // SpellFog state: the fit cache (one game constant per frame revision), the alpha colour cache, the sub-class and fog colour register of the draw being recorded, the first samples per sub-class.
    struct FogFitEntry{float key[3]={};float fit[3]={};bool ok=false,valid=false,bare=false,skip=false;unsigned rev=0;};
    FogFitEntry fogCache_[4];unsigned fogCacheNext_=0,fogFits_=0;bool fogFresh_=false;unsigned fogProven_=0; // 4 entries (game constant x frame revision), the fits made this frame, the integration's 'the estimate was published' flag, the vertex shader proof of the draw
    float fogColKey_[6]={},fogColOut_[3]={};bool fogColValid_=false;unsigned fogColRev_=~0u,fogSub_=0;int fogReg_=-1;
    float fogLastFit_[3]={};
public:
    struct FogSample{unsigned sub=0,kind=0;uint64_t vsHash=0;float game[4]={},gameColor[4]={},emit[4]={},emitColor[4]={};bool hasColor=false;}; // kind: 0 a falling game ramp folded, 1 a flat constant folded, 2 refused (emit = game)
private:
    unsigned long long fogFitCalls_=0;float fogSeen_[NorthlightSpellFog::kRamps][4]={};unsigned fogSeenN_=0,fogSeenNext_=0; // fits made on the record path; the last distinct falling-ramp constants seen (x, y, power, bare)
    FogSample fogSamples_[192];unsigned fogSampleN_=0,fogPerSub_[12]={};uint64_t fogVsHash_=0;bool fogCappedNow_=false,fogSkipNow_=false;
    SyncPend pend_;                                   // the slots the delta being built examined: they become synced when the record commits
    std::unique_ptr<Undo> undo_;                      // allocated at the first minimal restore
    std::vector<std::unique_ptr<FfRec[]>> ffChunks_;unsigned ffCount_=0;const FfRec* ffPrev_=nullptr; // ffPrev_: the last fixed-function record the running replay applied
    bool rsSkip_[kRenderStates]={};                   // a state the device refused to Get once: neither recorded nor replayed
    bool overflow_=false;                             // this frame fell back on Capacity: deferrable draws are still counted (noteCandidate)
    bool anySkip_=false;                              // some rsSkip_ is set
    Model model_;                                     // what the running replay will have set (see Model); reset with every frame/flush
    const MirrorView* mv_=nullptr;                    // set while a record holds the state cache's guard
    std::vector<Held> held_;std::vector<uint32_t> heldUsed_; // the references the frame holds: one per distinct object, in an open-addressing table (slots in heldUsed_)
    std::unique_ptr<uint32_t[]> scratch_;unsigned n_=0; // the delta being built (it becomes an entry's delta only when the record is committed)
    static constexpr unsigned kScratchWords=8192;     // bound of one delta: 206 render state + 520 sampler + 60 texture words, 1110+971 constant words with run headers, ints, bools, viewport, scissor, clip planes, bindings < 4000
    DWORD rsStage_[256];float vsStage_[kVsFloat][4],psStage_[kPsFloat][4]; // reads that could not come from the cache (indexed like the cache)
    const DWORD* rsCur_=nullptr;                      // the render states of the draw being recorded, by id
    unsigned sets_=0;                                 // device Set* calls of the running replay
    uint32_t stTouched_=0;bool ibTouched_=false,anyFf_=false;uint8_t ffLights_=0; // what the pending entries bind beyond the model: streams (bit per number), the index buffer, any fixed-function record, lights set by a record; reset with the frame
    Event event_;unsigned eventSeq_=0;
    std::unique_ptr<uint8_t[]> arena_;size_t arenaCap_=0;uint32_t arenaUsed_=0; // UP draw data and conflict copies, grown on demand (doubling) up to Config::arenaBytes; not zero-filled
    bool ensureArena(uint64_t need){
        if(need<=arenaCap_)return true;
        if(need>cfg_.arenaBytes)return false;
        size_t cap=arenaCap_?arenaCap_*2:std::max<size_t>(cfg_.arenaStartBytes,1u<<16);
        while(cap<need)cap*=2;cap=std::min<size_t>(cap,cfg_.arenaBytes);
        uint8_t* n=new(std::nothrow) uint8_t[cap];if(!n)return false;
        if(arenaUsed_)std::memcpy(n,arena_.get(),arenaUsed_);
        arena_.reset(n);arenaCap_=cap;return true;
    }
    std::vector<Blob> blobs_;
    GuardTable guards_;std::vector<GuardTable::Hit> hits_;std::vector<Seg> segs_;std::vector<uint32_t> order_;
    IDirect3DVertexBuffer9* upVb_=nullptr;IDirect3DIndexBuffer9* upIb_[2]={};uint32_t vbUsed_=0,ibUsed_[2]={},vbBase_=0,ibBase_[2]={};bool vbFresh_=true,ibFresh_[2]={true,true}; /* upload cursors of the FRAME (reset at beginFrame): *Base_ = the cursor when the running sequence began, *Fresh_ = the next upload DISCARDs (first of the frame, or after a wrap) */ /* one upload index buffer per index format: a 32-bit range must not be read as 16-bit */
    IDirect3DSurface9* target_=nullptr;IDirect3DSurface9* ds_=nullptr;
    struct Verdict{void* key=nullptr;bool rt=false;};  // per texture identity: D3DUSAGE_RENDERTARGET never changes for an object; open addressing, cleared when the generation changes or at 3/4 load
    static constexpr unsigned kVerdicts=1024;std::vector<Verdict> verdicts_;unsigned verdictCount_=0,verdictGen_=0;
    Counters frame_,last_,total_;bool firstHazard_=false,firstTaken_=false;Reason firstReason_=Reason::Count;

    template<class F> void count(F&& f){f(frame_);f(total_);}
    static uint32_t align16(uint32_t n){return (n+15u)&~15u;}
    void mark(void* key,bool on){if(cfg_.markGuard)cfg_.markGuard(cfg_.markCtx,key,on);}
    // ---- the frame's held references ----
    // Every object a pending draw needs after the game released it (textures, buffers, shaders, declarations) is referenced ONCE per frame, not once per draw:
    // intern() returns its table slot (aux/flags: per-object facts that never change, computed once). owned: the caller holds a reference from a Get call,
    // which is consumed. null: the table is full (the reference is released).
    static size_t hashPtr(const void* p){return (reinterpret_cast<uintptr_t>(p)>>4)*2654435761u;}
    Held* intern(IUnknown* p,bool owned){
        const size_t mask=held_.size()-1;size_t i=hashPtr(p)&mask;
        for(;held_[i].p;i=(i+1)&mask)if(held_[i].p==p){if(owned)p->Release();return &held_[i];}
        if(heldUsed_.size()>=held_.size()*3/4){if(owned)p->Release();return nullptr;}
        if(!owned)p->AddRef();
        held_[i]=Held{p,0,0};heldUsed_.push_back(uint32_t(i));return &held_[i];
    }
#ifdef NORTHLIGHT_SPELL_LAYER_TEST_API
    static bool allocFails(){return testFailAlloc;}
#else
    static bool allocFails(){return false;}
#endif
    bool allocHeld(){if(allocFails())return false;size_t slots=1024;while(slots<size_t(cfg_.maxEntries)*4)slots<<=1;try{held_.assign(slots,Held{});heldUsed_.reserve(slots*3/4);}catch(...){held_=std::vector<Held>();heldUsed_=std::vector<uint32_t>();return false;}return true;} // the held table, sized by maxEntries (trim() frees it while idle, defer() allocates it again)
    void releaseHeld(){for(uint32_t i:heldUsed_){held_[i].p->Release();held_[i]=Held{};}heldUsed_.clear();}
    bool reserveDelta(unsigned words,uint16_t& chunk,uint16_t& off){
        const unsigned cw=cfg_.deltaChunkWords,maxChunks=unsigned(std::max<size_t>(1,cfg_.deltaBytes/(size_t(cw)*4)));
        if(words>cw)return false;
        if(!deltaChunks_.empty()&&deltaUsed_+words>cw){
            if(deltaChunk_+1>=maxChunks)return false;
            ++deltaChunk_;deltaUsed_=0;
        }
        while(deltaChunk_>=deltaChunks_.size()){
            uint32_t* c=new(std::nothrow) uint32_t[cw];if(!c)return false;
            deltaChunks_.emplace_back(c);
        }
        chunk=uint16_t(deltaChunk_);off=uint16_t(deltaUsed_);deltaUsed_+=words;return true;
    }
    FfRec* ffAt(unsigned i){return &ffChunks_[i/kFfChunk][i%kFfChunk];}
    bool ensureFf(unsigned i){
        while(i/kFfChunk>=ffChunks_.size()){
            FfRec* p=new(std::nothrow) FfRec[kFfChunk];if(!p)return false; // uninitialised: readFf() fills every field
            ffChunks_.emplace_back(p);
        }
        return true;
    }
    void clearFrame(){
        guards_.clear([&](void* key){mark(key,false);}); // before the entries drop their references: the marked buffers are still alive
        releaseHeld();
        count_=0;ffCount_=0;deltaChunk_=0;deltaUsed_=0;model_.reset();if(cfg_.synced)cfg_.synced->clearAll();stTouched_=0;ibTouched_=anyFf_=false;ffLights_=0;blobs_.clear();hits_.clear();
        frame_.arenaHigh=std::max(frame_.arenaHigh,arenaUsed_);total_.arenaHigh=std::max(total_.arenaHigh,arenaUsed_);
        arenaUsed_=0;vbBase_=vbUsed_;ibBase_[0]=ibUsed_[0];ibBase_[1]=ibUsed_[1]; // the uploaded bytes of earlier drains stay where they are for the rest of the frame
        if(target_){target_->Release();target_=nullptr;}
        if(ds_){ds_->Release();ds_=nullptr;}
    }
    void resetUploadCursors(){vbUsed_=vbBase_=ibUsed_[0]=ibUsed_[1]=ibBase_[0]=ibBase_[1]=0;vbFresh_=ibFresh_[0]=ibFresh_[1]=true;}
    void releaseUploads(){
        if(upVb_){upVb_->Release();upVb_=nullptr;}
        for(auto*& ib:upIb_)if(ib){ib->Release();ib=nullptr;}
        resetUploadCursors();
    }
    void clearVerdicts(){if(verdictCount_){std::fill(verdicts_.begin(),verdicts_.end(),Verdict{});verdictCount_=0;}}
    bool renderTargetTexture(IDirect3DBaseTexture9* t){
        if(!cfg_.generation)return isRenderTargetTexture(t);
        if(verdicts_.empty())verdicts_.resize(kVerdicts);
        size_t i=(reinterpret_cast<uintptr_t>(t)>>4)*2654435761u&(kVerdicts-1);
        for(;verdicts_[i].key;i=(i+1)&(kVerdicts-1))if(verdicts_[i].key==static_cast<void*>(t))return verdicts_[i].rt;
        const bool rt=isRenderTargetTexture(t);
        if(verdictCount_<kVerdicts*3/4){verdicts_[i]=Verdict{static_cast<void*>(t),rt};++verdictCount_;}
        return rt;
    }
    void noteHazard(Reason r){
        count([&](Counters& c){++c.hazards[unsigned(r)];});
        if(!firstHazard_){firstHazard_=true;firstReason_=r;}
    }
    void noteEvent(Reason r,InPlaceKind by){event_=Event{r,by,count_};++eventSeq_;}
    // Every failure of defer() lands here: flush what is pending (in order, before the caller's in-place draw), then the frame falls back.
    // Capacity: the rest of the frame is drawn the old way (never part before and part after); the overflowing draw is the first of the candidates counted from here on.
    bool capFail(unsigned kind){
        count([&](Counters& c){++c.capBy[kind];++c.overflowFrames;c.overflowAt+=count_;++c.overflowCandidates;});
        overflow_=true;return fail(Reason::Capacity);
    }
    bool fail(Reason r){
        noteHazard(r);noteEvent(r,InPlaceKind::None);
        if(count_){count([](Counters& c){++c.flushes;});drain();}
        else releaseHeld(); // nothing pending: the references this refused record took are not needed
        fm_.fallback(r!=Reason::Frame);
        return false;
    }
    void drain(){count([](Counters& c){++c.drains;});timing_=cfg_.clock&&cfg_.timing&&cfg_.timing(cfg_.clockCtx);runReplay();clearFrame();}

    // Phase timing (sampled frames): ticks of the drains' state save, upload and state restore, read by the integration with phaseTicks().
    bool timing_=false;uint64_t tSave_=0,tUpload_=0,tRestore_=0,tCheck_=0;
    struct Phase{
        Layer& l;uint64_t* out;uint64_t t0=0;
        Phase(Layer& layer,uint64_t* o):l(layer),out(o){if(out)t0=l.cfg_.clock(l.cfg_.clockCtx);}
        ~Phase(){if(out)*out+=l.cfg_.clock(l.cfg_.clockCtx)-t0;}
        Phase(const Phase&)=delete;Phase& operator=(const Phase&)=delete;
    };
    // Writes the running sequence's blobs into the upload buffer, only the bytes it uses: the first upload of the frame (or after a wrap) DISCARDs [0, end), later drains of the
    // frame append at their cursor with NOOVERWRITE (the draws of the earlier drains are submitted and read other bytes). True also when the sequence has no blob of that buffer.
    bool upload(bool index,unsigned fmt=0){
        uint32_t lo=0xFFFFFFFFu,hi=0;
        for(const Blob& b:blobs_)if(b.index==index&&(!index||b.fmt==fmt)){lo=std::min(lo,b.uploadOff);hi=std::max(hi,b.uploadOff+align16(b.len));}
        if(lo>=hi)return true;
        bool& fresh=index?ibFresh_[fmt]:vbFresh_;const uint32_t from=fresh?0:lo;void* p=nullptr;
        const DWORD flags=fresh?D3DLOCK_DISCARD:D3DLOCK_NOOVERWRITE;
        if(index){if(!upIb_[fmt]||FAILED(upIb_[fmt]->Lock(from,hi-from,&p,flags))||!p)return false;}
        else if(!upVb_||FAILED(upVb_->Lock(from,hi-from,&p,flags))||!p)return false;
        fresh=false;
        for(const Blob& b:blobs_)if(b.index==index&&(!index||b.fmt==fmt))std::memcpy(static_cast<uint8_t*>(p)+(b.uploadOff-from),arena_.get()+b.arenaOff,b.len);
        if(index)upIb_[fmt]->Unlock();else upVb_->Unlock();
        return true;
    }

    // ---- replay: apply the entry's delta ----
    struct DevSink{
        IDirect3DDevice9* d;unsigned& sets;
        void rs(unsigned i,DWORD v,bool check){
            const D3DRENDERSTATETYPE id=D3DRENDERSTATETYPE(kRenderStateIds[i]);
            if(check){DWORD cur=0;if(SUCCEEDED(d->GetRenderState(id,&cur))&&cur==v)return;} // a vendor-hack state: set only when the device differs, so a re-set never repeats the action
            d->SetRenderState(id,v);++sets;
        }
        void ss(unsigned a,DWORD v){d->SetSamplerState(samplerStage(a/kSamplerStates),D3DSAMPLERSTATETYPE(kSamplerFirst+a%kSamplerStates),v);++sets;}
        void tex(unsigned t,void* p){d->SetTexture(samplerStage(t),static_cast<IDirect3DBaseTexture9*>(p));++sets;}
        void vsF(unsigned s,unsigned n,const uint32_t* w){d->SetVertexShaderConstantF(s,reinterpret_cast<const float*>(w),n);++sets;}
        void psF(unsigned s,unsigned n,const uint32_t* w){d->SetPixelShaderConstantF(s,reinterpret_cast<const float*>(w),n);++sets;}
        void vsI(unsigned s,unsigned n,const uint32_t* w){d->SetVertexShaderConstantI(s,reinterpret_cast<const int*>(w),n);++sets;}
        void psI(unsigned s,unsigned n,const uint32_t* w){d->SetPixelShaderConstantI(s,reinterpret_cast<const int*>(w),n);++sets;}
        void vsB(unsigned s,unsigned n,const uint32_t* w){d->SetVertexShaderConstantB(s,reinterpret_cast<const BOOL*>(w),n);++sets;}
        void psB(unsigned s,unsigned n,const uint32_t* w){d->SetPixelShaderConstantB(s,reinterpret_cast<const BOOL*>(w),n);++sets;}
        void viewport(const uint32_t* w){D3DVIEWPORT9 v;std::memcpy(&v,w,sizeof v);d->SetViewport(&v);++sets;}
        void scissor(const uint32_t* w){RECT r;std::memcpy(&r,w,sizeof r);d->SetScissorRect(&r);++sets;}
        void clip(unsigned i,const uint32_t* w){d->SetClipPlane(i,reinterpret_cast<const float*>(w));++sets;}
        void vs(void* p){d->SetVertexShader(static_cast<IDirect3DVertexShader9*>(p));++sets;}
        void ps(void* p){d->SetPixelShader(static_cast<IDirect3DPixelShader9*>(p));++sets;}
        void decl(void* p){d->SetVertexDeclaration(static_cast<IDirect3DVertexDeclaration9*>(p));++sets;}
        void fvf(DWORD v){d->SetFVF(v);++sets;}
    };
    // The recorded state of e on the device: its delta, no compares (the record already left out everything the earlier entries of this sequence set to the
    // same value). Does not touch RT/DS or the streams and index buffer (drawEntry binds those, materialized or not).
    void applyState(const Entry& e){
        const uint32_t* w=deltaChunks_[e.dChunk].get()+e.dOff;
        DevSink sink{dev_,sets_};decodeOps(w,w+e.dLen,sink);
        if(e.ff){applyFf(*e.ff,ffPrev_);ffPrev_=e.ff;} // programmable entries leave the fixed-function state alone, so the diff base is the last fixed-function entry
    }
    // The fixed-function state of f on the device, only what differs from the previous fixed-function record (all of it without one).
    void applyFf(const FfRec& f,const FfRec* p){
        for(unsigned i=0;i<kFfTransforms;++i)if(!p||std::memcmp(&f.tf[i],&p->tf[i],sizeof f.tf[i])){dev_->SetTransform(D3DTRANSFORMSTATETYPE(kFfTransformIds[i]),&f.tf[i]);++sets_;}
        for(unsigned s=0;s<kFfStages;++s)for(unsigned k=0;k<kTssStates;++k)if(!p||f.tss[s][k]!=p->tss[s][k]){dev_->SetTextureStageState(s,D3DTEXTURESTAGESTATETYPE(kTssIds[k]),f.tss[s][k]);++sets_;}
        if(!p||std::memcmp(&f.mat,&p->mat,sizeof f.mat)){dev_->SetMaterial(&f.mat);++sets_;}
        for(unsigned i=0;i<kFfLights;++i){
            if(f.lightSet[i]){
                const bool same=p&&p->lightSet[i];
                if(!same||std::memcmp(&f.light[i],&p->light[i],sizeof f.light[i])){dev_->SetLight(i,&f.light[i]);++sets_;}
                if(!same||f.lightOn[i]!=p->lightOn[i]){dev_->LightEnable(i,f.lightOn[i]);++sets_;}
            }else if(!p||p->lightSet[i]){ // never set when the draw was made: it must not light this draw now (a Get that fails leaves a light that does not exist alone)
                BOOL on=FALSE;
                if(SUCCEEDED(dev_->GetLightEnable(i,&on))&&on){dev_->LightEnable(i,FALSE);++sets_;}
            }
        }
    }
    void bindStream(Shadow& sh,UINT s,IDirect3DVertexBuffer9* vb,UINT off,UINT stride){
        Shadow::S& x=sh.st[s];
        if(x.valid&&x.vb==vb&&x.off==off&&x.stride==stride)return;
        dev_->SetStreamSource(s,vb,off,stride);++sets_;x.vb=vb;x.off=off;x.stride=stride;x.valid=true;
    }
    void bindIndices(Shadow& sh,IDirect3DIndexBuffer9* ib){
        if(sh.ibValid&&sh.ib==ib)return;
        dev_->SetIndices(ib);++sets_;sh.ib=ib;sh.ibValid=true;
    }
    void bindFreq(Shadow& sh,UINT s){ // every replayed stream is a plain (non-instanced) one: defer() refuses anything else
        if(sh.freq>>s&1)return;
        dev_->SetStreamSourceFreq(s,1);++sets_;sh.freq|=1u<<s;
    }
    // One entry: the delta restores the game's state of that draw (it does not carry RT/DS: set before), then the streams and index buffer are bound,
    // the materialized ones from the upload buffers. The delta is applied even when the draw is skipped, so the next entry's delta stays valid.
    // Fetch rule: vertex i of a stream reads offset+(base+i)*stride. Materialized stream bytes [lo,hi) now sit at P in the upload buffer. With f=base+min
    // (Indexed) or StartVertex (Primitive): mode A keeps base and puts offset P-f*stride (needs >=0); mode B uses base=-min (StartVertex 0), offset P,
    // and moves the other streams to offset+f*stride (always >=0). Index values are never rewritten.
    bool drawEntry(const Entry& e,Shadow& sh,bool vbOk,const bool (&ibOk)[2]){
        applyState(e);
        const bool idx=e.type==DrawType::Indexed,up=e.type==DrawType::PrimitiveUP||e.type==DrawType::IndexedUP;
        bool matV=false;
        for(unsigned i=0;i<e.nStreams;++i)if(e.streams[i].blob>=0)matV=true;
        if((matV&&!vbOk)||(e.ibBlob>=0&&!ibOk[blobs_[size_t(e.ibBlob)].fmt]))return false;
        INT base=e.base;UINT start=e.first;
        if(up)bindFreq(sh,0);
        else{
            const int64_t f=idx?int64_t(e.base)+e.minIndex:int64_t(e.first);
            bool modeA=true;
            if(matV)for(unsigned i=0;i<e.nStreams;++i){
                const Stream& s=e.streams[i];if(s.blob<0)continue;
                const Blob& b=blobs_[size_t(s.blob)];
                if(int64_t(b.uploadOff)+(int64_t(s.lo)-b.base)<f*s.stride)modeA=false;
            }
            for(unsigned i=0;i<e.nStreams;++i){
                const Stream& s=e.streams[i];bindFreq(sh,s.sno);
                if(s.blob>=0){
                    const Blob& b=blobs_[size_t(s.blob)];const int64_t p=int64_t(b.uploadOff)+(int64_t(s.lo)-b.base);
                    bindStream(sh,s.sno,upVb_,UINT(modeA?p-f*s.stride:p),s.stride);
                }else bindStream(sh,s.sno,s.vb,UINT(modeA?int64_t(s.off):int64_t(s.off)+f*s.stride),s.stride);
            }
            if(matV&&!modeA){if(idx)base=-INT(e.minIndex);else start=0;}
        }
        if(idx){
            if(e.ibBlob>=0){
                const Blob& b=blobs_[size_t(e.ibBlob)];
                bindIndices(sh,upIb_[b.fmt]);
                start=UINT((int64_t(b.uploadOff)+(int64_t(e.ibLo)-b.base))/e.isz);
            }else bindIndices(sh,e.ib);
        }
        switch(e.type){
        case DrawType::Primitive:dev_->DrawPrimitive(e.prim,start,e.primCount);break;
        case DrawType::Indexed:dev_->DrawIndexedPrimitive(e.prim,base,e.minIndex,e.numVertices,start,e.primCount);break;
        case DrawType::PrimitiveUP:dev_->DrawPrimitiveUP(e.prim,e.primCount,arena_.get()+e.upVtx,e.upStride);break;
        case DrawType::IndexedUP:
            dev_->DrawIndexedPrimitiveUP(e.prim,e.minIndex,e.numVertices,e.primCount,arena_.get()+e.upIdx,e.upFmt,
                arena_.get()+e.upVtx,e.upStride);break;
        }
        if(up){ // the API side effects of the UP call: stream 0 (and the indices) are unbound afterwards
            sh.st[0].valid=false;
            if(e.type==DrawType::IndexedUP)sh.ibValid=false;
        }
        return true;
    }
    // ---- the minimal restore ----
    // Why the mirror is no source of the "before" values: every drain that comes from the integration runs in an ExtensionDevice::RawScope, whose constructor invalidates the
    // state cache (every known flag false) and whose rawDepth makes the cache inactive (MirrorView::enter() fails), so the replay's own Set* calls bypass it (nothing is
    // written through) and it holds neither the game's values before the drain nor the replay's after it; it is invalidated again when the scope ends. The one drain outside a
    // RawScope (a refused defer(), which runs inside the record's cache guard) goes through the cache like any game call and keeps it coherent by itself. So the values the
    // restore needs are read from the device, by Get calls, BEFORE the first Set of the sequence: the union of the slots the entries set is exactly what model_ marks valid after
    // the last record (plus stTouched_, ibTouched_, anyFf_ for the bindings and fixed-function state the model does not hold), so one pass over its valid bits reads everything and
    // a Get that fails is known before the device has been changed at all: the drain then uses the state block (SavedState) as before. After the last draw every slot whose
    // before value differs from the value the sequence left (the model's final value; the replay is the only writer inside the drain) is Set back; slots the sequence never set are
    // not touched, bindings and RT/DS/viewport/scissor are always restored (SetRenderTarget(0) resets viewport and scissor, a UP draw unbinds stream 0 and the indices).
    // Not restorable, as with the state block: a light the replay would have to create (captureBefore refuses a sequence whose records set a light the device does not have).
    template<class F> static void forRuns(const uint64_t* valid,unsigned n,F&& f){for(unsigned i=0;i<n;){if(!bitSet(valid,i)){++i;continue;}unsigned e=i+1;while(e<n&&bitSet(valid,e))++e;f(i,e-i);i=e;}}
    void releaseUndo(){
        if(!undo_)return;Undo& u=*undo_;
        for(auto*& t:u.tex)if(t){t->Release();t=nullptr;}
        if(u.vs){u.vs->Release();u.vs=nullptr;}if(u.ps){u.ps->Release();u.ps=nullptr;}if(u.decl){u.decl->Release();u.decl=nullptr;}
        for(auto& x:u.st)if(x.vb){x.vb->Release();x.vb=nullptr;}
        if(u.ib){u.ib->Release();u.ib=nullptr;}
        for(auto*& r:u.rt)if(r){r->Release();r=nullptr;}
        if(u.ds){u.ds->Release();u.ds=nullptr;}
    }
    // Reads the before values. False: a read failed or the sequence cannot be restored slot by slot; nothing on the device has been changed, the references taken are released.
    bool captureBefore(){
        if(!undo_){undo_.reset(new(std::nothrow) Undo);if(!undo_)return false;std::memset(undo_.get(),0,sizeof(Undo));}
        Undo& u=*undo_;const Model& m=model_;IDirect3DDevice9* d=dev_;
        const bool ok=[&]{
            if(m.rsValid)for(unsigned i=0;i<kRenderStates;++i){const unsigned id=kRenderStateIds[i];if(FAILED(d->GetRenderState(D3DRENDERSTATETYPE(id),&u.b.rs[id])))return false;}
            for(unsigned t=0;t<kMaxTextures;++t){
                if(m.ssValid>>t&1)for(unsigned k=0;k<kSamplerStates;++k)if(FAILED(d->GetSamplerState(samplerStage(t),D3DSAMPLERSTATETYPE(kSamplerFirst+k),&u.b.ss[t][k])))return false;
                if(m.texValid>>t&1){IDirect3DBaseTexture9* x=nullptr;if(FAILED(d->GetTexture(samplerStage(t),&x))){if(x)x->Release();return false;}u.tex[t]=x;}
            }
            bool good=true;
            forRuns(m.vsFV,kVsFloat,[&](unsigned s,unsigned n){good=good&&SUCCEEDED(d->GetVertexShaderConstantF(s,u.b.vsF[s],n));});
            forRuns(m.psFV,kPsFloat,[&](unsigned s,unsigned n){good=good&&SUCCEEDED(d->GetPixelShaderConstantF(s,u.b.psF[s],n));});
            forRuns(&m.vsIV,kInts,[&](unsigned s,unsigned n){good=good&&SUCCEEDED(d->GetVertexShaderConstantI(s,&u.b.vsI[s][0],n));});
            forRuns(&m.psIV,kInts,[&](unsigned s,unsigned n){good=good&&SUCCEEDED(d->GetPixelShaderConstantI(s,&u.b.psI[s][0],n));});
            forRuns(&m.vsBV,kBools,[&](unsigned s,unsigned n){good=good&&SUCCEEDED(d->GetVertexShaderConstantB(s,u.b.vsB+s,n));});
            forRuns(&m.psBV,kBools,[&](unsigned s,unsigned n){good=good&&SUCCEEDED(d->GetPixelShaderConstantB(s,u.b.psB+s,n));});
            if(!good)return false;
            if(FAILED(d->GetViewport(&u.vp))||FAILED(d->GetScissorRect(&u.sc)))return false;
            for(unsigned i=0;i<kClipPlanes;++i)if(m.clipV>>i&1&&FAILED(d->GetClipPlane(i,u.b.clip[i])))return false;
            if(m.vsV){IDirect3DVertexShader9* x=nullptr;if(FAILED(d->GetVertexShader(&x))){if(x)x->Release();return false;}u.vs=x;}
            if(m.psV){IDirect3DPixelShader9* x=nullptr;if(FAILED(d->GetPixelShader(&x))){if(x)x->Release();return false;}u.ps=x;}
            if(m.declSeen){
                IDirect3DVertexDeclaration9* x=nullptr;if(FAILED(d->GetVertexDeclaration(&x))){if(x)x->Release();return false;}u.decl=x;
                if(FAILED(d->GetFVF(&u.fvf)))return false;
            }
            for(unsigned s=0;s<16;++s)if(stTouched_>>s&1){
                IDirect3DVertexBuffer9* x=nullptr;UINT off=0,stride=0,freq=1;
                if(FAILED(d->GetStreamSource(s,&x,&off,&stride))){if(x)x->Release();return false;}
                u.st[s]=Undo::S{x,off,stride,1};
                if(FAILED(d->GetStreamSourceFreq(s,&freq)))return false;
                u.st[s].freq=freq;
            }
            if(ibTouched_){IDirect3DIndexBuffer9* x=nullptr;if(FAILED(d->GetIndices(&x))){if(x)x->Release();return false;}u.ib=x;}
            if(anyFf_){
                if(!readFf(u.ff))return false;
                for(unsigned i=0;i<kFfLights;++i)if(!u.ff.lightSet[i]&&(ffLights_>>i&1))return false;
            }
            for(unsigned i=0;i<4;++i){
                IDirect3DSurface9* x=nullptr;
                if(SUCCEEDED(d->GetRenderTarget(i,&x)))u.rt[i]=x;else if(x)x->Release();
            }
            if(!u.rt[0])return false;
            IDirect3DSurface9* z=nullptr;if(SUCCEEDED(d->GetDepthStencilSurface(&z)))u.ds=z;else if(z)z->Release();
            return true;
        }();
        if(!ok)releaseUndo();
        return ok;
    }
    // After the last draw: every touched slot whose before value differs from what the sequence left on the device goes back (see the comment above).
    void restoreBefore(){
        Undo& u=*undo_;const Model& m=model_;IDirect3DDevice9* d=dev_;unsigned n=0;
        d->SetDepthStencilSurface(nullptr);for(DWORD i=1;i<4;++i)d->SetRenderTarget(i,nullptr);
        d->SetRenderTarget(0,u.rt[0]);for(DWORD i=1;i<4;++i)d->SetRenderTarget(i,u.rt[i]);d->SetDepthStencilSurface(u.ds);n+=9;
        if(m.rsValid)for(unsigned i=0;i<kRenderStates;++i){
            const unsigned id=kRenderStateIds[i];const DWORD b=u.b.rs[id];if(b==m.rs[id])continue;
            if(vendorHackState(id)){DWORD cur=0;if(SUCCEEDED(d->GetRenderState(D3DRENDERSTATETYPE(id),&cur))&&cur==b)continue;} // a re-set of a vendor-hack state repeats its action
            d->SetRenderState(D3DRENDERSTATETYPE(id),b);++n;
        }
        for(unsigned t=0;t<kMaxTextures;++t){
            if(m.ssValid>>t&1)for(unsigned k=0;k<kSamplerStates;++k)if(u.b.ss[t][k]!=m.ss[t][k]){d->SetSamplerState(samplerStage(t),D3DSAMPLERSTATETYPE(kSamplerFirst+k),u.b.ss[t][k]);++n;}
            if((m.texValid>>t&1)&&static_cast<void*>(u.tex[t])!=m.tex[t]){d->SetTexture(samplerStage(t),u.tex[t]);++n;}
        }
        auto rows=[&](const uint64_t* valid,unsigned cnt,const void* bef,const void* fin,size_t rb,auto&& set){ // runs of valid rows whose before value differs
            const uint8_t* b=static_cast<const uint8_t*>(bef),*f=static_cast<const uint8_t*>(fin);
            for(unsigned i=0;i<cnt;){
                if(!bitSet(valid,i)||!std::memcmp(b+i*rb,f+i*rb,rb)){++i;continue;}
                unsigned e=i+1;while(e<cnt&&bitSet(valid,e)&&std::memcmp(b+e*rb,f+e*rb,rb))++e;
                set(i,e-i);++n;i=e;
            }
        };
        rows(m.vsFV,kVsFloat,u.b.vsF,m.vsF,16,[&](unsigned s,unsigned c){d->SetVertexShaderConstantF(s,u.b.vsF[s],c);});
        rows(m.psFV,kPsFloat,u.b.psF,m.psF,16,[&](unsigned s,unsigned c){d->SetPixelShaderConstantF(s,u.b.psF[s],c);});
        rows(&m.vsIV,kInts,u.b.vsI,m.vsI,16,[&](unsigned s,unsigned c){d->SetVertexShaderConstantI(s,&u.b.vsI[s][0],c);});
        rows(&m.psIV,kInts,u.b.psI,m.psI,16,[&](unsigned s,unsigned c){d->SetPixelShaderConstantI(s,&u.b.psI[s][0],c);});
        rows(&m.vsBV,kBools,u.b.vsB,m.vsB,4,[&](unsigned s,unsigned c){d->SetVertexShaderConstantB(s,u.b.vsB+s,c);});
        rows(&m.psBV,kBools,u.b.psB,m.psB,4,[&](unsigned s,unsigned c){d->SetPixelShaderConstantB(s,u.b.psB+s,c);});
        for(unsigned i=0;i<kClipPlanes;++i)if((m.clipV>>i&1)&&std::memcmp(u.b.clip[i],m.clip[i],16)){d->SetClipPlane(i,u.b.clip[i]);++n;}
        if(m.vsV&&u.vs!=static_cast<IDirect3DVertexShader9*>(m.vs)){d->SetVertexShader(u.vs);++n;}
        if(m.psV&&u.ps!=static_cast<IDirect3DPixelShader9*>(m.ps)){d->SetPixelShader(u.ps);++n;}
        if(m.declSeen&&!(m.declMode==1&&u.decl&&static_cast<void*>(u.decl)==m.decl)){ // the before declaration OBJECT goes back whenever there is one (GetFVF may be nonzero for a user declaration, and SetFVF would bind the runtime's own declaration instead); SetFVF only when none was bound
            if(u.decl)d->SetVertexDeclaration(u.decl);else if(u.fvf)d->SetFVF(u.fvf);else d->SetVertexDeclaration(nullptr);++n;
        }
        for(unsigned s=0;s<16;++s)if(stTouched_>>s&1){d->SetStreamSource(s,u.st[s].vb,u.st[s].off,u.st[s].stride);d->SetStreamSourceFreq(s,u.st[s].freq);n+=2;}
        if(ibTouched_){d->SetIndices(u.ib);++n;}
        if(anyFf_&&ffPrev_){ // the device holds the last applied record's transforms, stage states and material; lights are compared with the device itself
            const FfRec& f=*ffPrev_;
            for(unsigned i=0;i<kFfTransforms;++i)if(std::memcmp(&u.ff.tf[i],&f.tf[i],sizeof f.tf[i])){d->SetTransform(D3DTRANSFORMSTATETYPE(kFfTransformIds[i]),&u.ff.tf[i]);++n;}
            for(unsigned st=0;st<kFfStages;++st)for(unsigned k=0;k<kTssStates;++k)if(u.ff.tss[st][k]!=f.tss[st][k]){d->SetTextureStageState(st,D3DTEXTURESTAGESTATETYPE(kTssIds[k]),u.ff.tss[st][k]);++n;}
            if(std::memcmp(&u.ff.mat,&f.mat,sizeof f.mat)){d->SetMaterial(&u.ff.mat);++n;}
            for(unsigned i=0;i<kFfLights;++i){
                if(!u.ff.lightSet[i])continue; // a light the device never had: no record set it (captureBefore), it stays as it is
                D3DLIGHT9 cur;BOOL on=FALSE;
                if(FAILED(d->GetLight(i,&cur))||std::memcmp(&cur,&u.ff.light[i],sizeof cur)){d->SetLight(i,&u.ff.light[i]);++n;}
                if(FAILED(d->GetLightEnable(i,&on))||(on!=0)!=u.ff.lightOn[i]){d->LightEnable(i,u.ff.lightOn[i]);++n;}
            }
        }
        d->SetViewport(&u.vp);d->SetScissorRect(&u.sc);n+=2; // last: SetRenderTarget(0) reset both
        count([&](Counters& c){c.restoreSets+=n;});
    }
    void runReplay(){
        if(!count_)return;
        bool minimal=false;
        if(cfg_.minimalRestore){
            {Phase t(*this,timing_?&tSave_:nullptr);minimal=captureBefore();}
            if(!minimal)count([](Counters& c){++c.restoreFallbacks;});
        }
        std::optional<SavedState> scope;
        if(!minimal){
            {Phase t(*this,timing_?&tSave_:nullptr);scope.emplace(dev_,cfg_.savedPool);}
            if(!scope->ok){count([&](Counters& c){c.replaySkipped+=count_;});return;}
        }
        bool vbOk,ibOk[2];
        {Phase t(*this,timing_?&tUpload_:nullptr);vbOk=upload(false);ibOk[0]=upload(true,0);ibOk[1]=upload(true,1);}
        dev_->SetDepthStencilSurface(nullptr);
        for(DWORD i=1;i<4;++i)dev_->SetRenderTarget(i,nullptr);
        const bool bound=SUCCEEDED(dev_->SetRenderTarget(0,target_));
        dev_->SetDepthStencilSurface(ds_);
        Shadow sh;sets_=0;ffPrev_=nullptr;
        for(unsigned i=0;i<count_;++i){
            const bool ok=bound&&drawEntry(entryAt(i),sh,vbOk,ibOk);
            count([&](Counters& c){if(ok)++c.replayedDraws;else ++c.replaySkipped;});
        }
        count([&](Counters& c){c.replaySets+=sets_;});
        {Phase t(*this,timing_?&tRestore_:nullptr);if(minimal){restoreBefore();releaseUndo();}else scope.reset();}
    }

    bool ensureUpload(bool index,unsigned fmt=0){
        if(index){
            if(!upIb_[fmt]&&(FAILED(dev_->CreateIndexBuffer(UINT(cfg_.uploadIbBytes),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,fmt?D3DFMT_INDEX32:D3DFMT_INDEX16,D3DPOOL_DEFAULT,&upIb_[fmt],nullptr))||!upIb_[fmt])){upIb_[fmt]=nullptr;return false;}
        }else if(!upVb_&&(FAILED(dev_->CreateVertexBuffer(UINT(cfg_.uploadVbBytes),D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY,0,D3DPOOL_DEFAULT,&upVb_,nullptr))||!upVb_)){upVb_=nullptr;return false;}
        return true;
    }
    // Copy-on-conflict of the live ranges in hits_ (all of one buffer): merge them into byte spans, read each span once from the raw buffer into the
    // arena and give every affected entry stream/index the span's blob. False: a hazard was raised (everything pending was flushed first).
    bool copyOut(void* key){
        const GuardTable::Buf& b=guards_.buf(hits_[0].buf);const bool index=b.index;
        const unsigned fmt=index&&entryAt(guards_.range(hits_[0]).entry).isz==4?1u:0u; // every entry on one index buffer has its format
        order_.clear();for(uint32_t i=0;i<hits_.size();++i)order_.push_back(i);
        std::sort(order_.begin(),order_.end(),[&](uint32_t x,uint32_t y){return guards_.range(hits_[x]).lo<guards_.range(hits_[y]).lo;});
        segs_.clear();
        for(size_t k=0;k<order_.size();++k){
            const GuardTable::Range& r=guards_.range(hits_[order_[k]]);
            if(!segs_.empty()&&r.lo<=segs_.back().hi){segs_.back().hi=std::max(segs_.back().hi,r.hi);segs_.back().lastHit=k;}
            else segs_.push_back(Seg{r.lo,r.hi,k,k});
        }
        uint32_t& cursor=index?ibUsed_[fmt]:vbUsed_;uint32_t& base=index?ibBase_[fmt]:vbBase_;bool& fresh=index?ibFresh_[fmt]:vbFresh_;
        uint64_t arena=arenaUsed_,need=0;
        for(const Seg& s:segs_){arena+=align16(s.hi-s.lo);need+=align16(s.hi-s.lo);}
        if(base>0&&uint64_t(cursor)+need>(index?cfg_.uploadIbBytes:cfg_.uploadVbBytes)){ // the frame's earlier drains are submitted: start the buffer over (DISCARD) with this sequence's blobs at 0
            for(Blob& bl:blobs_)if(bl.index==index&&(!index||bl.fmt==fmt))bl.uploadOff-=base;
            cursor-=base;base=0;fresh=true;
        }
        const uint64_t up=uint64_t(cursor)+need;
        const bool arenaOk=ensureArena(arena);
        if(!arenaOk||up>(index?cfg_.uploadIbBytes:cfg_.uploadVbBytes)||blobs_.size()+segs_.size()>size_t(cfg_.maxEntries)*(kMaxStreams+1)){
            count([&](Counters& c){++c.capBy[arenaOk?3:2];++c.overflowFrames;c.overflowAt+=count_;});overflow_=true;
            hazard(Reason::Capacity);return false;
        }
        if(!ensureUpload(index,fmt)){hazard(Reason::Capture);return false;}
        count([](Counters& c){++c.conflicts;});
        for(const Seg& s:segs_){
            const uint32_t len=s.hi-s.lo;void* p=nullptr;
            HRESULT hr=index?static_cast<IDirect3DIndexBuffer9*>(key)->Lock(s.lo,len,&p,cfg_.readBackLockFlags):static_cast<IDirect3DVertexBuffer9*>(key)->Lock(s.lo,len,&p,cfg_.readBackLockFlags);
            if(FAILED(hr)||!p){ // a failed lock leaves nothing to unlock; a success with no pointer is unlocked again
                if(SUCCEEDED(hr)){if(index)static_cast<IDirect3DIndexBuffer9*>(key)->Unlock();else static_cast<IDirect3DVertexBuffer9*>(key)->Unlock();}
                hazard(Reason::BufWrite);return false;
            }
            std::memcpy(arena_.get()+arenaUsed_,p,len);
            if(index)static_cast<IDirect3DIndexBuffer9*>(key)->Unlock();else static_cast<IDirect3DVertexBuffer9*>(key)->Unlock();
            uint32_t& upUsed=index?ibUsed_[fmt]:vbUsed_;
            blobs_.push_back(Blob{arenaUsed_,len,s.lo,upUsed,index,uint8_t(fmt)});
            const int32_t id=int32_t(blobs_.size()-1);
            arenaUsed_+=align16(len);upUsed+=align16(len);
            count([&](Counters& c){c.conflictBytes+=len;});
            for(size_t k=s.firstHit;k<=s.lastHit;++k){
                const GuardTable::Hit h=hits_[order_[k]];const GuardTable::Range r=guards_.range(h);Entry& e=entryAt(r.entry);
                if(r.slot==0xFF)e.ibBlob=id;else e.streams[r.slot].blob=id;
                count([](Counters& c){++c.materialized;});
                if(guards_.drop(h.buf,h.range))mark(key,false);
            }
        }
        return true;
    }
    // ---- record: the delta of one draw against the replay model ----
    static bool bitSet(const uint64_t* m,unsigned i){return (m[i>>6]>>(i&63))&1;}
    static bool blockFull(const uint64_t* m,unsigned i){return ((m[i>>6]>>(i&63))&0xFFFF)==0xFFFF;} // 16 aligned registers
    static bool maskSpan(const uint64_t* mask,unsigned words,unsigned& lo,unsigned& hi){
        bool any=false;
        for(unsigned w=0;w<words;++w)if(mask[w]){
            const unsigned first=w*64+unsigned(__builtin_ctzll(mask[w])),last=w*64+63-unsigned(__builtin_clzll(mask[w]));
            if(!any)lo=first;hi=last;any=true;
        }
        return any;
    }
    // Every register in mask is known: runs of set bits are tested word-wise.
    static bool knownAll(const bool* known,const uint64_t* mask,unsigned words){
        for(unsigned w=0;w<words;++w){
            uint64_t m=mask[w];
            while(m){
                const unsigned lo=unsigned(__builtin_ctzll(m));const uint64_t shifted=m>>lo;
                const unsigned len=~shifted?unsigned(__builtin_ctzll(~shifted)):64-lo;
                if(!runKnown(known+w*64+lo,len))return false;
                m=len+lo>=64?0:m&~(((1ull<<len)-1)<<lo);
            }
        }
        return true;
    }
    void emit(uint32_t op,uint32_t a,uint32_t b=0){scratch_[n_++]=opWord(op,a,b);}
    void emit1(uint32_t op,uint32_t a,uint32_t v){scratch_[n_++]=opWord(op,a,0);scratch_[n_++]=v;}
    void emitPtr(uint32_t op,uint32_t a,const void* p){scratch_[n_++]=opWord(op,a,0);std::memcpy(&scratch_[n_],&p,sizeof p);n_+=kPtrWords;}
    // Emits, for the registers in mask whose value differs from the model (or is unknown in it), runs of rows (bridging gaps of up to two unchanged registers:
    // one call is cheaper than two). synced (optional): registers whose model value is known to equal the device (valid in the model, SyncedSlots) are not examined: they cannot
    // differ, and a bridged one is copied from the model, so the emitted delta is the one the full compare makes. Only the registers of mask & ~synced are looked at, word-wise.
    template<unsigned rowWords> void diffRows(uint32_t op,const void* curV,const void* mdlV,const uint64_t* valid,const uint64_t* mask,const uint64_t* synced,unsigned n,unsigned ovrRow=~0u,const float* ovr=nullptr){
        const uint8_t* cur=static_cast<const uint8_t*>(curV),*mdl=static_cast<const uint8_t*>(mdlV);constexpr size_t rb=size_t(rowWords)*4; // constant row size: the compare inlines
        auto isSynced=[&](unsigned i){return synced&&bitSet(synced,i);};
        auto rowOf=[&](unsigned i)->const uint8_t*{return i==ovrRow?reinterpret_cast<const uint8_t*>(ovr):cur+i*rb;}; // ovr: the value to record for one row instead of the device's (SpellFog)
        auto differs=[&](unsigned i){return !isSynced(i)&&(!bitSet(valid,i)||std::memcmp(rowOf(i),mdl+i*rb,rb)!=0);};
        unsigned consumed=0; // rows below it are inside an emitted run
        for(unsigned w=0;w<(n+63)/64;++w){
            uint64_t bits=mask[w]&~(synced?synced[w]:0ull);
            while(bits){
                const unsigned i=w*64+unsigned(__builtin_ctzll(bits));bits&=bits-1;
                if(i>=n)break;
                if(i<consumed||!differs(i))continue;
                unsigned end=i+1;
                for(unsigned k=end;k<n&&k-end<=2&&bitSet(mask,k);++k)if(differs(k))end=k+1;
                emit(op,i,end-i);
                for(unsigned r=i;r<end;++r){std::memcpy(&scratch_[n_],isSynced(r)?mdl+r*rb:rowOf(r),rb);n_+=rowWords;}
                consumed=end;
            }
        }
    }
    // The render states of the draw by id: the cache's array when every state is known (one pass over its known flags, 8 at a time), else a staging copy: the model's values (they
    // equal the device for the synced slots) with the dirty ids read by the cache or by Get (a state the device refuses is dropped for the session: neither recorded nor replayed).
    const DWORD* renderStates(const uint64_t* dirty){
        if(mv_&&!anySkip_){
            bool all=true;
            for(unsigned i=0;i<32&&all;++i){uint64_t f;std::memcpy(&f,mv_->rsKnown+i*8,8);all=(f&kRsKnownMask[i])==kRsKnownMask[i];}
            if(all)return mv_->rs;
        }
        if(model_.rsValid)std::memcpy(rsStage_,model_.rs,sizeof rsStage_);else std::memset(rsStage_,0,sizeof rsStage_);
        for(unsigned w=0;w<4;++w){
            uint64_t bits=dirty[w];
            while(bits){
                const unsigned id=w*64+unsigned(__builtin_ctzll(bits));bits&=bits-1;
                const unsigned i=kRsIndexOfId[id];
                if(i>=kRenderStates||rsSkip_[i])continue;
                if(mv_&&mv_->rsKnown[id])rsStage_[id]=mv_->rs[id];
                else if(FAILED(dev_->GetRenderState(D3DRENDERSTATETYPE(id),&rsStage_[id]))){rsSkip_[i]=true;anySkip_=true;rsStage_[id]=model_.rsValid?model_.rs[id]:0;}
            }
        }
        return rsStage_;
    }
    // SpellFog: the fit of the game's fog constant for the frame (cached per game constant and frame revision: the game's constant is the same for most of a frame's effects) and the
    // alpha sub-class's fog colour. False: the game's constant stays.
    bool fogFit(const float game[3],float fit[3]){
        fogCappedNow_=false;fogSkipNow_=false;
        const NorthlightSpellFog::Frame& f=*cfg_.fog;
        // the publish's fits first (spell_fog.h Hold::update): a flat constant has one per frame revision, a falling ramp seen before has one too; no fitting on this path
        const bool bareNow=fogProven_==2;
        if(NorthlightSpellFog::flatConstant(game)){
            const NorthlightSpellFog::Fit& pf=f.flat[bareNow?1:0];
            if(pf.valid){fogSkipNow_=pf.bareSkip;if(pf.ok){std::memcpy(fit,pf.fit,12);std::memcpy(fogLastFit_,pf.fit,12);}return pf.ok;}
        }else{
            for(const NorthlightSpellFog::Fit& pf:f.ramp)if(pf.valid&&pf.bare==bareNow&&!std::memcmp(pf.key,game,12)){fogSkipNow_=pf.bareSkip;if(pf.ok){std::memcpy(fit,pf.fit,12);std::memcpy(fogLastFit_,pf.fit,12);}return pf.ok;}
            fogNoteRamp(game,bareNow);
        }
        for(const FogFitEntry& e:fogCache_)if(e.valid&&e.rev==f.revision&&e.bare==(fogProven_==2)&&!std::memcmp(e.key,game,12)){fogSkipNow_=e.skip;if(e.ok){std::memcpy(fit,e.fit,12);std::memcpy(fogLastFit_,e.fit,12);}return e.ok;}
        if(fogFits_>=cfg_.maxFogFits){fogCappedNow_=true;count([](Counters& c){++c.fogCapped;});return false;} // a bounded cost per frame: beyond the cap the game's constant is recorded
        ++fogFits_;++fogFitCalls_;
        FogFitEntry& e=fogCache_[fogCacheNext_++&3];std::memcpy(e.key,game,12);e.rev=f.revision;e.valid=true;e.bare=fogProven_==2;
        e.ok=NorthlightSpellFog::emitFog(f,game,e.fit,e.bare,&e.skip);fogSkipNow_=e.skip;
        if(e.ok){std::memcpy(fit,e.fit,12);std::memcpy(fogLastFit_,e.fit,12);}
        fogColValid_=false;
        return e.ok;
    }
    void fogNoteRamp(const float game[3],bool bare){
        for(unsigned i=0;i<fogSeenN_;++i)if(!std::memcmp(fogSeen_[i],game,12)&&(fogSeen_[i][3]!=0)==bare)return;
        const unsigned at=fogSeenN_<NorthlightSpellFog::kRamps?fogSeenN_++:(fogSeenNext_++%NorthlightSpellFog::kRamps);
        std::memcpy(fogSeen_[at],game,12);fogSeen_[at][3]=bare?1.f:0.f;
    }
    void fogColor(const float game[3],const float fit[3],const float cF[3],float out[3]){
        float key[6];std::memcpy(key,game,12);std::memcpy(key+3,cF,12);
        if(!(fogColValid_&&fogColRev_==cfg_.fog->revision&&!std::memcmp(fogColKey_,key,24))){
            std::memcpy(fogColKey_,key,24);fogColRev_=cfg_.fog->revision;fogColValid_=true;
            NorthlightSpellFog::emitColor(*cfg_.fog,game,fit,cF,fogColOut_);
        }
        std::memcpy(out,fogColOut_,12);
    }
    void fogSample(const float* game,const float* gameColor,const float* emit,const float* emitColor,unsigned kind=0){
        if(!cfg_.fogLog||fogSampleN_>=192)return;
        const unsigned k=(fogSub_&SubAdditive?0u:fogSub_&SubAlpha?1u:fogSub_&SubModulate?2u:3u)+4*kind;
        if(fogPerSub_[k]>=16)return; // the first 16 of each sub-class and kind per session
        ++fogPerSub_[k];FogSample& x=fogSamples_[fogSampleN_++];x=FogSample{};x.sub=fogSub_;x.kind=kind;x.vsHash=fogVsHash_;
        std::memcpy(x.game,game,12);std::memcpy(x.emit,emit,12); // three floats each: (x, y, power) and (r, g, b)
        if(gameColor){x.hasColor=true;std::memcpy(x.gameColor,gameColor,12);std::memcpy(x.emitColor,emitColor,12);}
    }
    // Builds the delta of the draw into scratch_ (the model is not touched: the record may still be refused). Reason::Count: built. sy: the synced bits to use (null: examine every
    // slot of the read set, the full compare); track: fill pend_ with the stage and viewport slots examined (sy or not); pend_ receives the slots this record examined (they become synced at commit) and how many there were.
    Reason buildDelta(const Reads& rd,bool fixed,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,IDirect3DVertexDeclaration9* decl,DWORD fvf,IDirect3DBaseTexture9* const (&tex)[kMaxTextures],const SyncedSlots* sy,bool track){
        const Model& m=model_;n_=0;pend_=SyncPend{};unsigned slots=0;
        if(!m.vsV||m.vs!=static_cast<void*>(vs))emitPtr(Op::Vs,0,static_cast<void*>(vs));
        if(!m.psV||m.ps!=static_cast<void*>(ps))emitPtr(Op::Ps,0,static_cast<void*>(ps));
        if(decl){if(m.declMode!=1||m.decl!=static_cast<void*>(decl))emitPtr(Op::Decl,0,static_cast<void*>(decl));}
        else if(m.declMode!=2||m.fvf!=fvf)emit1(Op::Fvf,0,fvf);
        // render states (a synced bit counts only for a slot the model holds)
        uint64_t rsDirty[4];for(unsigned w=0;w<4;++w)rsDirty[w]=kRsIdMask[w]&~(sy&&m.rsValid?sy->rs[w]:0ull);
        const DWORD* cur=renderStates(rsDirty);rsCur_=cur;
        if(fixed&&(cur[151]||cur[167]))return Reason::Admit; // D3DRS_VERTEXBLEND / INDEXEDVERTEXBLENDENABLE: WORLD1..3 would be read
        for(unsigned w=0;w<4;++w){
            uint64_t bits=rsDirty[w];
            while(bits){
                const unsigned id=w*64+unsigned(__builtin_ctzll(bits));bits&=bits-1;
                const unsigned i=kRsIndexOfId[id];
                if(i>=kRenderStates||rsSkip_[i])continue;
                ++slots;pend_.rs[w]|=1ull<<(id&63);
                if(!m.rsValid)emit1(vendorHackState(id)?Op::RsCheck:Op::Rs,i,cur[id]);
                else if(cur[id]!=m.rs[id])emit1(Op::Rs,i,cur[id]);
            }
        }
        // texture bindings and the sampler states of the bound stages
        for(unsigned t=0;t<kMaxTextures;++t){
            if(!(rd.tex>>t&1))continue;
            void* p=static_cast<void*>(tex[t]);
            const bool tracked=track&&t<16;
            if(!(m.texValid>>t&1)||m.tex[t]!=p)emitPtr(Op::Tex,t,p);
            if(!(tracked&&sy&&(sy->tex>>t&1)&&(m.texValid>>t&1))){++slots;if(tracked)pend_.tex=uint16_t(pend_.tex|(1u<<t));}
            if(!p)continue; // a stage without a texture is not sampled
            // sampler states 1..13 (bit k = type 1+k) still to examine: all of them unless this stage's are synced
            const unsigned all=(1u<<kSamplerStates)-1,ssSynced=tracked&&sy&&(m.ssValid>>t&1)?(sy->ss[t]>>kSamplerFirst)&all:0u,dm=all&~ssSynced;
            if(!dm)continue;
            DWORD v[kSamplerStates];const DWORD* sv=v;
            bool fromMirror=mv_&&t<16;
            if(fromMirror)for(unsigned k=0;k<kSamplerStates;++k)if((dm>>k&1)&&!mv_->smpKnown[t][kSamplerFirst+k]){fromMirror=false;break;}
            if(fromMirror)sv=&mv_->smp[t][kSamplerFirst];
            else for(unsigned k=0;k<kSamplerStates;++k)if((dm>>k&1)&&FAILED(dev_->GetSamplerState(samplerStage(t),D3DSAMPLERSTATETYPE(kSamplerFirst+k),&v[k])))return Reason::Capture;
            if(!(m.ssValid>>t&1)){for(unsigned k=0;k<kSamplerStates;++k)emit1(Op::Ss,t*kSamplerStates+k,sv[k]);}
            else for(unsigned k=0;k<kSamplerStates;++k)if((dm>>k&1)&&sv[k]!=m.ss[t][k])emit1(Op::Ss,t*kSamplerStates+k,sv[k]);
            slots+=unsigned(__builtin_popcount(dm));
            if(tracked)pend_.ss[t]=uint16_t(pend_.ss[t]|(dm<<kSamplerFirst));
        }
        // shader constants (float rows: only the ones not synced are read)
        unsigned lo=0,hi=0;
        uint64_t sync4[4],dm4[4];
        // SpellFog: c30 (the fog ramp) of a programmable draw, and the alpha sub-class's fog colour register, are recorded as Northlight's fog folded into the game's value; those rows are
        // examined on every record and never marked synced (the Model holds the folded value, the device the game's).
        const bool fogUse=!fixed&&cfg_.fogOn&&fogFresh_&&cfg_.fog&&cfg_.fog->active;   // fogFresh_: the estimate was published by the last frame's render (a frozen one is not folded)
        // A record folds only with a proven vertex shader AND a proven, read PS fog colour register AND a blend with a known neutral colour: the register is substituted too (never the game's stale value).
        const FogTarget target=fogUse?fogTarget(cur[19],cur[20],cur[171]):FogTarget::None;   // 171: D3DRS_BLENDOP
        const bool psRegOk=fogReg_>=0&&fogReg_<int(kPsFloat)&&(rd.psF[fogReg_>>6]>>(fogReg_&63)&1);
        const bool vsOk=fogUse&&fogProven_!=0&&(rd.vsF[0]>>30&1);                          // a vertex shader whose fog output is proven to be the c30 ramp
        const bool fogVsRow=vsOk&&psRegOk&&target!=FogTarget::None;
        if(fogUse){if(!(rd.vsF[0]>>30&1))pend_.fogNoRead=1;else if(fogProven_==0)pend_.fogUnproven=1;else if(!psRegOk)pend_.fogNoPsReg=1;else if(target==FogTarget::None)pend_.fogOtherBlend=1;}
        const int fogReg=fogVsRow?fogReg_:-1;
        float fitV[3]={},gameV[3]={};bool fitOk=false,flatNow=false;
        for(unsigned w=0;w<4;++w){sync4[w]=sy?sy->vsF[w]&m.vsFV[w]:0ull;dm4[w]=rd.vsF[w]&~sync4[w];}
        if(fogVsRow){sync4[0]&=~(1ull<<30);dm4[0]=rd.vsF[0]&~sync4[0];pend_.fogVs=true;}
        if(maskSpan(dm4,4,lo,hi)){
            const float (*src)[4]=vsStage_;
            if(mv_&&knownAll(mv_->vsFKnown,dm4,4))src=mv_->vsF;
            else if(FAILED(dev_->GetVertexShaderConstantF(lo,vsStage_[lo],hi-lo+1)))return Reason::Capture;
            unsigned ovr=~0u;float row[4]={};
            if(fogVsRow){
                std::memcpy(gameV,src[30],12);
                if(fogFit(gameV,fitV)){fitOk=true;flatNow=NorthlightSpellFog::flatConstant(gameV);if(flatNow)pend_.fogFlat=1;row[0]=fitV[0];row[1]=fitV[1];row[2]=fitV[2];row[3]=src[30][3];ovr=30;++pend_.fogApplied;fogSample(src[30],nullptr,row,nullptr,flatNow?1u:0u);}
                else if(!fogCappedNow_){if(fogSkipNow_)pend_.fogBareSkip=1;else pend_.fogRefused=1;fogSample(src[30],nullptr,src[30],nullptr,2);}
            }
            diffRows<4>(Op::VsF,src,m.vsF,m.vsFV,rd.vsF,sync4,kVsFloat,ovr,row);
            for(unsigned w=0;w<4;++w){pend_.vsF[w]=dm4[w];slots+=unsigned(__builtin_popcountll(dm4[w]));}
            if(fogVsRow)pend_.vsF[0]&=~(1ull<<30);
        }
        for(unsigned w=0;w<4;++w){sync4[w]=sy?sy->psF[w]&m.psFV[w]:0ull;dm4[w]=rd.psF[w]&~sync4[w];}
        if(fogReg>=0){sync4[fogReg>>6]&=~(1ull<<(fogReg&63));dm4[fogReg>>6]=rd.psF[fogReg>>6]&~sync4[fogReg>>6];pend_.fogPs=fogReg;}
        if(maskSpan(dm4,4,lo,hi)){
            const float (*src)[4]=psStage_;
            if(mv_&&knownAll(mv_->psFKnown,dm4,4))src=mv_->psF;
            else if(FAILED(dev_->GetPixelShaderConstantF(lo,psStage_[lo],hi-lo+1)))return Reason::Capture;
            unsigned ovr=~0u;float row[4]={};
            if(fogReg>=0&&fitOk){
                float out[3]={0,0,0};
                if(target==FogTarget::Scatter)fogColor(gameV,fitV,src[fogReg],out);
                else if(target==FogTarget::White)out[0]=out[1]=out[2]=1.f;
                else if(target==FogTarget::Grey)out[0]=out[1]=out[2]=.5f;
                row[0]=out[0];row[1]=out[1];row[2]=out[2];row[3]=src[fogReg][3];ovr=unsigned(fogReg);++pend_.fogApplied;
                float emitV[4]={fitV[0],fitV[1],fitV[2],0};fogSample(gameV,src[fogReg],emitV,row,flatNow?1u:0u);
            }
            diffRows<4>(Op::PsF,src,m.psF,m.psFV,rd.psF,sync4,kPsFloat,ovr,row);
            for(unsigned w=0;w<4;++w){pend_.psF[w]=dm4[w];slots+=unsigned(__builtin_popcountll(dm4[w]));}
            if(fogReg>=0)pend_.psF[fogReg>>6]&=~(1ull<<(fogReg&63));
        }
        int is[kInts][4];BOOL bs[kBools];
        if(rd.vsI){
            const uint64_t mk=rd.vsI;const int (*src)[4]=is;
            if(mv_&&knownAll(mv_->vsIKnown,&mk,1))src=mv_->vsI;else if(FAILED(dev_->GetVertexShaderConstantI(0,&is[0][0],kInts)))return Reason::Capture;
            diffRows<4>(Op::VsI,src,m.vsI,&m.vsIV,&mk,nullptr,kInts);slots+=unsigned(__builtin_popcountll(mk));
        }
        if(rd.vsB){
            const uint64_t mk=rd.vsB;const BOOL* src=bs;
            if(mv_&&knownAll(mv_->vsBKnown,&mk,1))src=mv_->vsB;else if(FAILED(dev_->GetVertexShaderConstantB(0,bs,kBools)))return Reason::Capture;
            diffRows<1>(Op::VsB,src,m.vsB,&m.vsBV,&mk,nullptr,kBools);slots+=unsigned(__builtin_popcountll(mk));
        }
        if(rd.psI){const uint64_t mk=rd.psI;if(FAILED(dev_->GetPixelShaderConstantI(0,&is[0][0],kInts)))return Reason::Capture;diffRows<4>(Op::PsI,is,m.psI,&m.psIV,&mk,nullptr,kInts);slots+=unsigned(__builtin_popcountll(mk));}
        if(rd.psB){const uint64_t mk=rd.psB;if(FAILED(dev_->GetPixelShaderConstantB(0,bs,kBools)))return Reason::Capture;diffRows<1>(Op::PsB,bs,m.psB,&m.psBV,&mk,nullptr,kBools);slots+=unsigned(__builtin_popcountll(mk));}
        // viewport; the scissor rect only with the scissor test on; the clip planes only for the enabled ones (the draw reads nothing else; neither has a synced bit)
        if(!(sy&&sy->vp&&m.vpV)){
            D3DVIEWPORT9 vpl;const D3DVIEWPORT9* vp=&vpl;
            if(mv_&&*mv_->vpKnown)vp=mv_->vp;else if(FAILED(dev_->GetViewport(&vpl)))return Reason::Capture;
            if(!m.vpV||std::memcmp(vp,&m.vp,sizeof m.vp)){emit(Op::Viewport,0);std::memcpy(&scratch_[n_],vp,sizeof *vp);n_+=sizeof(D3DVIEWPORT9)/4;}
            ++slots;pend_.vp=track;
        }
        if(cur[174]){
            RECT sc;if(FAILED(dev_->GetScissorRect(&sc)))return Reason::Capture;
            if(!m.scissorV||std::memcmp(&sc,&m.scissor,sizeof sc)){emit(Op::Scissor,0);std::memcpy(&scratch_[n_],&sc,sizeof sc);n_+=4;}
            ++slots;
        }
        for(unsigned i=0;i<kClipPlanes;++i)if(cur[152]>>i&1){
            float pl[4];if(FAILED(dev_->GetClipPlane(i,pl)))return Reason::Capture;
            if(!(m.clipV>>i&1)||std::memcmp(pl,m.clip[i],sizeof pl)){emit(Op::Clip,i);std::memcpy(&scratch_[n_],pl,sizeof pl);n_+=4;}
            ++slots;
        }
        pend_.slots=slots;
        return Reason::Count;
    }
    bool readFf(FfRec& f){
        for(unsigned i=0;i<kFfTransforms;++i)if(FAILED(dev_->GetTransform(D3DTRANSFORMSTATETYPE(kFfTransformIds[i]),&f.tf[i])))return false;
        for(unsigned s=0;s<kFfStages;++s)for(unsigned k=0;k<kTssStates;++k)if(FAILED(dev_->GetTextureStageState(s,D3DTEXTURESTAGESTATETYPE(kTssIds[k]),&f.tss[s][k])))return false;
        if(FAILED(dev_->GetMaterial(&f.mat)))return false;
        for(unsigned i=0;i<kFfLights;++i){
            BOOL on=FALSE;f.lightSet[i]=SUCCEEDED(dev_->GetLight(i,&f.light[i]));
            if(f.lightSet[i]){if(FAILED(dev_->GetLightEnable(i,&on)))return false;}else std::memset(&f.light[i],0,sizeof f.light[i]);
            f.lightOn[i]=on!=0;
        }
        return true;
    }
    // The streams a declaration reads (bit per stream), cached with the declaration's held reference. False: the declaration cannot be read.
    bool usedStreams(Held& h,uint32_t& used){
        if(h.flags&kHasAux){used=h.aux;return true;}
        auto* decl=static_cast<IDirect3DVertexDeclaration9*>(h.p);
        D3DVERTEXELEMENT9 el[kMaxDecl];UINT n=0;
        if(FAILED(decl->GetDeclaration(nullptr,&n))||n>kMaxDecl||FAILED(decl->GetDeclaration(el,&n)))return false;
        used=0;for(UINT i=0;i<n&&el[i].Stream!=0xFF;++i)if(el[i].Stream<16)used|=1u<<el[i].Stream;
        h.aux=uint16_t(used);h.flags|=kHasAux;return true;
    }
    // The bytes one vertex of stream s reads, from the declaration: the largest end of its elements (D3DDECLTYPE sizes). False: no such element or an unknown type.
    static bool declStreamEnd(IDirect3DVertexDeclaration9* decl,uint32_t s,uint32_t& end){
        static const uint8_t size[17]={4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8}; // FLOAT1..4, D3DCOLOR, UBYTE4, SHORT2, SHORT4, UBYTE4N, SHORT2N, SHORT4N, USHORT2N, USHORT4N, UDEC3, DEC3N, FLOAT16_2, FLOAT16_4
        D3DVERTEXELEMENT9 el[kMaxDecl];UINT n=0;
        if(FAILED(decl->GetDeclaration(nullptr,&n))||n>kMaxDecl||FAILED(decl->GetDeclaration(el,&n)))return false;
        end=0;
        for(UINT i=0;i<n&&el[i].Stream!=0xFF;++i){
            if(el[i].Stream!=s)continue;
            if(el[i].Type>16)return false;
            end=std::max<uint32_t>(end,uint32_t(el[i].Offset)+size[el[i].Type]);
        }
        return end>0;
    }
    // Holds the state cache's guard for the reads of one record.
    struct Hold{
        Layer& l;bool on=false;
        explicit Hold(Layer& layer):l(layer){const MirrorView* v=l.cfg_.source;if(v&&v->enter&&v->enter(v->ctx)){l.mv_=v;on=true;}}
        ~Hold(){if(on){const MirrorView* v=l.mv_;l.mv_=nullptr;v->leave(v->ctx);}}
        Hold(const Hold&)=delete;Hold& operator=(const Hold&)=delete;
    };
public:
    Layer()=default;
    Layer(const Layer&)=delete;Layer& operator=(const Layer&)=delete;
    ~Layer(){reset();}

    // Not owned; the device must outlive the layer or reset()/attach(nullptr) runs first. Drops everything pending without drawing.
    void attach(IDirect3DDevice9* dev){reset();dev_=dev;}
    // Only while nothing is pending (else pending entries are dropped). Allocates the held-object table once; entries, deltas and the arena follow on demand.
    void liveOptions(unsigned classes,bool fog){if(!pending()){cfg_.classMask=classes;cfg_.fogOn=fog;}}
    void configure(const Config& c){
        reset();cfg_=c;if(cfg_.synced)cfg_.synced->clearAll();cfg_.deltaChunkWords=std::min(65535u,std::max(1024u,cfg_.deltaChunkWords));
        if(cfg_.on){
            allocHeld();scratch_.reset(new uint32_t[kScratchWords]);model_.reset();
        }
        else{entryChunks_.clear();arena_.reset();arenaCap_=0;held_=std::vector<Held>();heldUsed_=std::vector<uint32_t>();scratch_.reset();checkScratch_.reset();}
        fm_.begin(cfg_.on,false);
    }
    const Config& config()const{return cfg_;}

    // Frame boundary. Entries still pending (no B this frame, no hazard(Frame)) are dropped WITHOUT drawing and counted as discarded.
    // Memory pressure: no deferral this frame and the record storage and upload buffers are released.
    void beginFrame(bool memoryPressure){
        if(count_){count([&](Counters& c){c.discarded+=count_;});clearFrame();fm_.fallback(false);}
        releaseHeld();
        last_=frame_;frame_=Counters{};overflow_=false;resetUploadCursors();fogFits_=0;
        fm_.begin(cfg_.on,memoryPressure);
        if(!cfg_.on||memoryPressure)trim(0);
    }
    bool armed()const{return cfg_.on&&dev_&&fm_.canDefer();}
    // After a Capacity fallback the integration keeps classifying cheaply and reports each draw that would have been deferred: the frame's real demand.
    bool wantsCandidates()const{return overflow_&&cfg_.on&&!fm_.canDefer();}
    void noteCandidate(){count([](Counters& c){++c.overflowCandidates;});}
    Mode mode()const{return fm_.mode();}
    unsigned cooldownLeft()const{return fm_.cooldownLeft();}
    unsigned pending()const{return count_;}
    IDirect3DSurface9* target()const{return target_;}     // the layer target T (RT0 of the first pending entry), null when nothing is pending
    IDirect3DSurface9* depth()const{return ds_;}
    const Counters& frameCounters()const{return frame_;}
    const Counters& lastFrameCounters()const{return last_;}
    const Counters& totalCounters()const{return total_;}
    // SpellFog: the integration says, once per frame, whether the estimate was published by the last frame's world render (spell_fog.h Freshness); false: no record folds.
    void fogFresh(bool fresh){fogFresh_=fresh;}
    // SpellFog diagnostics: the last fit the layer emitted, and the first samples per sub-class since the last call (their count; the array stays valid until the next defer()).
    const float* fogLastFit()const{return fogLastFit_;}
    // SpellFog: the falling-ramp constants the record path had to look up beyond the publish's fits (x, y, power, bare), for the world renderer to precompute; and the fits made on the record path.
    unsigned fogRampsSeen(const float (*&out)[4])const{out=fogSeen_;return fogSeenN_;}
    unsigned long long fogFitCalls()const{return fogFitCalls_;}
    bool fogSampleRoom()const{if(!cfg_.fogOn||!cfg_.fogLog||fogSampleN_>=192)return false;for(unsigned k:fogPerSub_)if(k<16)return true;return false;} // a SPELLFOG sample could still be taken (the integration then looks up the vertex shader hash)
    unsigned takeFogSamples(const FogSample*& out){out=fogSamples_;const unsigned n=fogSampleN_;fogSampleN_=0;return n;}
    // The dirty cross-check on sampled frames, and its first mismatch (once per session, for a one-off log line).
    void dirtyCheck(bool on){cfg_.dirtyCheck=on;}
    bool takeDirtyMismatch(DeltaDiff& out){if(!dirtyFirst_.have||dirtyTaken_)return false;dirtyTaken_=true;out=dirtyFirst_;return true;}
    struct PhaseTicks{uint64_t save,upload,restore,check;};   // check: the time of the dirty cross-check's second (full) build, inside the record timers
      // clock() ticks the drains spent in their state save, upload and state restore since the last clearPhaseTicks()
    PhaseTicks phaseTicks()const{return PhaseTicks{tSave_,tUpload_,tRestore_,tCheck_};}
    void clearPhaseTicks(){tSave_=tUpload_=tRestore_=tCheck_=0;}
    size_t recordChunks()const{return deltaChunks_.size();} // allocated chunks of state delta words (Config::deltaChunkWords each)
    size_t ffChunks()const{return ffChunks_.size();}      // allocated chunks of kFfChunk fixed-function records
    uint32_t arenaUsed()const{return arenaUsed_;}
    // Memory the layer holds right now (bytes; the device-side upload buffers are listed apart): entries, deltas, arena, fixed-function records, held table.
    struct Memory{size_t entries,deltas,arena,fixedFunction,tables,uploads;size_t total()const{return entries+deltas+arena+fixedFunction+tables;}};
    Memory memory()const{
        Memory m{};m.entries=entryChunks_.size()*kEntryChunk*sizeof(Entry);m.deltas=deltaChunks_.size()*size_t(cfg_.deltaChunkWords)*4;m.arena=arenaCap_;
        m.fixedFunction=ffChunks_.size()*kFfChunk*sizeof(FfRec);m.tables=held_.size()*sizeof(Held)+(checkScratch_?kScratchWords*4:0)+heldUsed_.capacity()*4+sizeof(Model)+kScratchWords*4+(blobs_.capacity()*sizeof(Blob)+hits_.capacity()*sizeof(GuardTable::Hit)+segs_.capacity()*sizeof(Seg)+order_.capacity()*4);
        m.uploads=(upVb_?cfg_.uploadVbBytes:0)+(upIb_[0]?cfg_.uploadIbBytes:0)+(upIb_[1]?cfg_.uploadIbBytes:0);return m;
    }
    static constexpr unsigned entryChunkSize(){return kEntryChunk;}
    // Hazard diagnostics: eventSeq() changes on every hazard that flushed and every refused defer (the frame fell back); lastEvent() describes it.
    unsigned eventSeq()const{return eventSeq_;}
    const Event& lastEvent()const{return event_;}
    // True once per session, after the first hazard: its reason, for a one-shot log line.
    bool takeFirstHazard(Reason& r){if(!firstHazard_||firstTaken_)return false;firstTaken_=true;r=firstReason_;return true;}
    void noteInPlace(InPlaceKind k){if(k==InPlaceKind::Fade)count([&](Counters& c){++c.inPlace[unsigned(k)-1];});} // only the accepted reorder is counted

    // For a draw classify() returned Defer for. true: deferred, the caller skips the real draw and returns D3D_OK (the side effects of a skipped UP
    // call on the device are already emulated). false: nothing was queued and the device is as before except possibly a flush: when pending
    // entries existed and the defer failed (Capacity/Capture/Admit/Order) they were all replayed now (the device state restored afterwards: minimal restore or SavedState) and the frame is
    // Fallback, so the caller draws this draw in place AFTER them (order kept). When the layer is off, not armed (fallback/cooldown/pressure/after
    // B) or the draw is empty it returns false without any device call. A later Defer draw whose RT0/DS differs from the first entry's is a hazard
    // (Order); the integration normally keeps those out via DrawFacts::rt0IsTarget. DrawArgs::psMajor must carry the pixel shader model (see above).
    bool defer(const DrawArgs& a,IDirect3DSurface9* rt0,IDirect3DSurface9* ds,unsigned sub=SubOther){
        if(!cfg_.on||!dev_||!fm_.canDefer())return false;
        const uint32_t vcount=vertexCount(a.prim,a.primCount);
        if(!vcount)return false;
        if(count_>=cfg_.maxEntries)return capFail(0);
        if(!rt0||(count_&&(rt0!=target_||ds!=ds_)))return fail(count_?Reason::Order:Reason::Capture);
        if(cfg_.generation){const unsigned g=cfg_.generation(cfg_.genCtx);if(g!=verdictGen_){clearVerdicts();verdictGen_=g;}}
        if(held_.empty()&&!allocHeld())return capFail(3); // no memory for the held table: the capacity failure path (flush, fallback)
        if(!ensureEntry(count_))return capFail(0);
        const bool up=a.type==DrawType::PrimitiveUP||a.type==DrawType::IndexedUP,idx=a.type==DrawType::Indexed||a.type==DrawType::IndexedUP;
        Hold hold(*this); // the state cache's guard for all the reads below (Get calls nest in it)
        SyncedSlots* const sy=(cfg_.dirtyRecord&&cfg_.synced&&mv_)?cfg_.synced:nullptr; // only with the state cache usable: its setters keep the bits (an unusable cache means no bits are trusted)
        const uint32_t epoch0=sy?sy->epoch:0;
        Entry& e=entryAt(count_);e=Entry{};
        e.type=a.type;e.prim=a.prim;e.base=a.baseVertex;e.minIndex=a.minIndex;e.numVertices=a.numVertices;e.first=a.first;e.primCount=a.primCount;
        // admission: a programmable vertex and pixel stage, ps_2_0 or newer (the fixed-function state is then unused and not recorded), or neither
        // stage (fixed function: FfRec). A vertex shader with a fixed-function pixel stage or the reverse is refused.
        IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;bool owned=false; // owned: a Get call's reference (the cache's pointers are borrowed)
        if(mv_&&*mv_->vsKnown)vs=*mv_->vs;else if(SUCCEEDED(dev_->GetVertexShader(&vs)))owned=vs!=nullptr;else return fail(Reason::Admit);
        if(vs&&!intern(vs,owned))return capFail(3);
        owned=false;
        if(mv_&&*mv_->psKnown)ps=*mv_->ps;else if(SUCCEEDED(dev_->GetPixelShader(&ps)))owned=ps!=nullptr;else return fail(Reason::Admit);
        if(ps&&!intern(ps,owned))return capFail(3);
        const bool fixed=!vs&&!ps;
        if(!fixed&&(!vs||!ps||a.psMajor<kMinPsMajor))return fail(Reason::Admit);
        if(fixed&&(ffCount_>=cfg_.maxFixed||!ensureFf(ffCount_)))return capFail(3);
        uint32_t used=1; // FVF: stream 0
        IDirect3DVertexDeclaration9* decl=nullptr;DWORD fvf=0;
        owned=false;
        if(mv_&&*mv_->declKnown)decl=*mv_->decl;else{dev_->GetVertexDeclaration(&decl);owned=decl!=nullptr;}
        if(decl){
            Held* dh=intern(decl,owned);if(!dh)return capFail(3);
            if(!up&&!usedStreams(*dh,used))return fail(Reason::Capture);
        }else if(mv_&&*mv_->fvfKnown)fvf=*mv_->fvf;
        else if(FAILED(dev_->GetFVF(&fvf)))return fail(Reason::Capture);
        if(!up){
            const int64_t first=idx?int64_t(a.baseVertex)+a.minIndex:int64_t(a.first);
            const uint32_t verts=idx?a.numVertices:vcount;
            for(uint32_t s=0;s<16;++s){
                if(!(used>>s&1))continue;
                if(e.nStreams==kMaxStreams)return fail(Reason::Capture);
                IDirect3DVertexBuffer9* vb=nullptr;UINT off=0,stride=0,freq=1;
                const bool got=SUCCEEDED(dev_->GetStreamSource(s,&vb,&off,&stride))&&vb;
                if(!got){if(vb)vb->Release();return fail(Reason::Capture);}
                if(!intern(vb,true))return capFail(3);
                Stream& st=e.streams[e.nStreams++];st.vb=vb;st.off=off;st.stride=stride;st.sno=s;
                if(FAILED(dev_->GetStreamSourceFreq(s,&freq))||freq!=1)return fail(Reason::Capture);
                int64_t lo=int64_t(off)+first*stride,hi=lo+int64_t(verts)*stride;
                if(!stride){ // every vertex reads the same element: guard [offset, offset + the declaration's largest element end of this stream, or the FVF's vertex size); a size that cannot be determined (an unknown element type, no valid position) is refused (counted: stride0Refused)
                    uint32_t end=0;if(!(decl?declStreamEnd(decl,s,end):fvfVertexSize(fvf,end))){count([](Counters& c){++c.stride0Refused;});return fail(Reason::Capture);}
                    lo=off;hi=int64_t(off)+end;
                }
                if(lo<0||hi>int64_t(0xFFFFFFFFu))return fail(Reason::Capture);
                st.lo=uint32_t(lo);st.hi=uint32_t(hi);
            }
            if(a.type==DrawType::Indexed){
                if(FAILED(dev_->GetIndices(&e.ib))||!e.ib)return fail(Reason::Capture);
                Held* ibh=intern(e.ib,true);if(!ibh)return capFail(3);
                if(!(ibh->flags&kHasAux)){ // the format of an index buffer never changes
                    D3DINDEXBUFFER_DESC d{};
                    if(FAILED(e.ib->GetDesc(&d))||(d.Format!=D3DFMT_INDEX16&&d.Format!=D3DFMT_INDEX32))return fail(Reason::Capture);
                    ibh->aux=d.Format==D3DFMT_INDEX16?2:4;ibh->flags|=kHasAux;
                }
                e.isz=uint8_t(ibh->aux);
                const uint64_t lo=uint64_t(a.first)*e.isz,hi=lo+uint64_t(vcount)*e.isz;
                if(hi>0xFFFFFFFFu)return fail(Reason::Capture);
                e.ibLo=uint32_t(lo);e.ibHi=uint32_t(hi);
            }
        }
        uint32_t upV=0,upI=0,vOff=0;
        if(up){
            if(!a.vertexData||!a.stride||(idx&&!a.indexData))return fail(Reason::Capture);
            e.upStride=a.stride;
            if(idx){
                if(a.indexFormat!=D3DFMT_INDEX16&&a.indexFormat!=D3DFMT_INDEX32)return fail(Reason::Capture);
                e.isz=a.indexFormat==D3DFMT_INDEX16?2:4;e.upFmt=a.indexFormat;
                vOff=0;upV=(a.minIndex+a.numVertices)*a.stride;upI=vcount*e.isz; /* the driver reads [0,(min+num)*stride) of the user pointer (DXVK FillUPVertexBuffer): copy that, replay with the plain arena pointer */
            }else upV=vcount*a.stride;
            if(!ensureArena(uint64_t(arenaUsed_)+align16(upV)+align16(upI)))return capFail(2);
        }
        // What the draw can read: a fixed-function draw reads no shader constants and the texture stages 0..7; a programmable one the shaders' read sets.
        Reads rd;
        if(fixed){rd.noConstants();rd.tex=(1u<<kFfStages)-1;}
        else{   // a shader that was scanned exactly reads only its registers and declared samplers; anything else reads everything of its stage
            rd.tex=0;
            const NorthlightShaderReads::ReadSet* v=(vs&&static_cast<const void*>(vs)==a.vsFor)?a.vsReads:nullptr,*p=(ps&&static_cast<const void*>(ps)==a.psFor)?a.psReads:nullptr; // a read set of another shader than the recorded one: reads everything
            if(v&&v->exact){for(unsigned w=0;w<4;++w)rd.vsF[w]=v->f[w];rd.vsI=v->i;rd.vsB=v->b;rd.tex|=uint32_t(v->samplers&0xF)<<16;}else rd.tex|=0xF0000u;
            if(p&&p->exact){for(unsigned w=0;w<4;++w)rd.psF[w]=p->f[w];rd.psI=p->i;rd.psB=p->b;rd.tex|=p->samplers;}else rd.tex|=0xFFFFu;
        }
        IDirect3DBaseTexture9* tex[kMaxTextures]={};Held* texHeld[kMaxTextures]={};
        for(unsigned t=0;t<kMaxTextures;++t){ // pixel samplers 0..15, then the vertex texture samplers (stages 257..260): a write to any of them flushes
            if(!(rd.tex>>t&1))continue;
            if(sy&&t<16&&(sy->tex>>t&1)&&(model_.texValid>>t&1)){tex[t]=static_cast<IDirect3DBaseTexture9*>(model_.tex[t]);continue;} // synced: still what the model holds (held, verdict known and flagged when it was bound)
            IDirect3DBaseTexture9* tx=nullptr;owned=false;
            if(mv_&&t<16&&mv_->texKnown[t])tx=mv_->tex[t];
            else{dev_->GetTexture(samplerStage(t),&tx);owned=tx!=nullptr;}
            if(!tx)continue;
            Held* h=intern(tx,owned);if(!h)return capFail(3);
            if(!(h->flags&kNotRt)){ // once per object and frame (and per object and generation in the verdict cache)
                if(renderTargetTexture(tx))return fail(Reason::Capture); // the game may render into or clear it later without any call the layer sees
                h->flags|=kNotRt;
            }
            tex[t]=tx;texHeld[t]=h;
        }
        fogSub_=sub;fogReg_=a.psFogReg;fogProven_=a.vsFog;fogVsHash_=a.vsHash;
        {   Reason why=buildDelta(rd,fixed,vs,ps,decl,fvf,tex,sy,sy!=nullptr);
            if(sy&&cfg_.dirtyCheck){ const bool tm=cfg_.clock&&cfg_.timing&&cfg_.timing(cfg_.clockCtx);Phase checkTime(*this,tm?&tCheck_:nullptr); // tests and sampled frames: the full compare must give the same delta (its result, and its larger examined set, is the one used); a Get the full compare needs and the dirty path never makes may fail
                if(!checkScratch_&&!allocFails())checkScratch_.reset(new(std::nothrow) uint32_t[kScratchWords]); // allocated once, only when the check is used
                if(!checkScratch_){ // no memory to verify with: do not trust the unverified delta - the full compare is used and the dirty record is off from now on
                    count([](Counters& c){++c.checkAllocFails;});cfg_.dirtyRecord=false;why=buildDelta(rd,fixed,vs,ps,decl,fvf,tex,nullptr,false);
                }else{
                const unsigned n1=n_;const SyncPend p1=pend_;std::memcpy(checkScratch_.get(),scratch_.get(),size_t(n_)*4);
                const Reason why2=buildDelta(rd,fixed,vs,ps,decl,fvf,tex,nullptr,true);
                if(why==Reason::Count&&why2!=Reason::Count){n_=n1;pend_=p1;std::memcpy(scratch_.get(),checkScratch_.get(),size_t(n1)*4);}   // keep the dirty result
                else{
                    if(why2!=why||n_!=n1||std::memcmp(checkScratch_.get(),scratch_.get(),size_t(n1)*4)){
                        count([](Counters& c){++c.dirtyMismatches;});
                        if(!dirtyFirst_.have){
                            if(why2!=why){dirtyFirst_.have=true;std::snprintf(dirtyFirst_.kind,sizeof dirtyFirst_.kind,"reason");dirtyFirst_.index=0;dirtyFirst_.dirty=unsigned(why);dirtyFirst_.full=unsigned(why2);}
                            else dirtyFirst_=DeltaLister::diff(checkScratch_.get(),n1,scratch_.get(),n_);
                        }
                        if(cfg_.dirtyDisableOnMismatch)cfg_.dirtyRecord=false;
                    }
                    pend_.slots=p1.slots;why=why2;
                }
                }
            }
            if(why!=Reason::Count)return fail(why);}
        if(fixed){
            e.ff=ffAt(ffCount_);
            if(!readFf(*e.ff))return fail(Reason::Capture);
        }
        if(!reserveDelta(n_,e.dChunk,e.dOff))return capFail(1);
        e.dLen=uint16_t(n_);
        // Committed from here: nothing below fails.
        {   uint32_t* dst=deltaChunks_[e.dChunk].get()+e.dOff;std::memcpy(dst,scratch_.get(),size_t(n_)*4);
            ModelSink ms{model_};decodeOps(dst,dst+n_,ms);}
        if(sy){if(pend_.fogVs)sy->vsF[0]&=~(1ull<<30);if(pend_.fogPs>=0)sy->psF[pend_.fogPs>>6]&=~(1ull<<(pend_.fogPs&63));} // the fog rows hold Northlight's value in the Model, not the device's: never synced
        if(pend_.fogApplied||pend_.fogRefused||pend_.fogBareSkip||pend_.fogUnproven||pend_.fogNoRead||pend_.fogNoPsReg||pend_.fogOtherBlend)count([&](Counters& c){c.fogEntries+=pend_.fogApplied?1u:0u;c.fogFlat+=pend_.fogFlat;c.fogRefused+=pend_.fogRefused;c.fogBareSkip+=pend_.fogBareSkip;c.fogNoPsReg+=pend_.fogNoPsReg;c.fogOtherBlend+=pend_.fogOtherBlend;c.fogUnproven+=pend_.fogUnproven;c.fogNoRead+=pend_.fogNoRead;});
        if(sy&&sy->epoch==epoch0){ // nothing changed the device under this record: what it examined now equals the model
            for(unsigned w=0;w<4;++w){sy->rs[w]|=pend_.rs[w];sy->vsF[w]|=pend_.vsF[w];sy->psF[w]|=pend_.psF[w];}
            for(unsigned t=0;t<16;++t)sy->ss[t]|=pend_.ss[t];
            sy->tex|=pend_.tex;if(pend_.vp)sy->vp=true;
        }
        count([&](Counters& c){c.recordSlots+=pend_.slots;});
        if(up){
            std::memcpy(arena_.get()+arenaUsed_,static_cast<const uint8_t*>(a.vertexData)+vOff,upV);e.upVtx=arenaUsed_;arenaUsed_+=align16(upV);
            if(idx){std::memcpy(arena_.get()+arenaUsed_,a.indexData,upI);e.upIdx=arenaUsed_;arenaUsed_+=align16(upI);}
        }
        for(unsigned i=0;i<e.nStreams;++i){
            Stream& s=e.streams[i];
            if(guards_.add(static_cast<void*>(s.vb),false,s.lo,s.hi,count_,uint8_t(i)))mark(static_cast<void*>(s.vb),true);
        }
        if(e.ib&&guards_.add(static_cast<void*>(e.ib),true,e.ibLo,e.ibHi,count_,0xFF))mark(static_cast<void*>(e.ib),true);
        for(unsigned t=0;t<kMaxTextures;++t){
            Held* h=texHeld[t];
            if(h)h->flags|=kInSamples; // the frame's set of sampled textures: pendingSamples() asks the held table
        }
        if(!count_){target_=rt0;rt0->AddRef();ds_=ds;if(ds)ds->AddRef();}
        if(up){stTouched_|=1;if(idx)ibTouched_=true;}else{for(unsigned i=0;i<e.nStreams;++i)stTouched_|=1u<<e.streams[i].sno;if(a.type==DrawType::Indexed)ibTouched_=true;}
        if(fixed){anyFf_=true;for(unsigned i=0;i<kFfLights;++i)if(e.ff->lightSet[i])ffLights_|=uint8_t(1u<<i);}
        if(fixed)++ffCount_;
        ++count_;fm_.deferred();
        count([&](Counters& c){unsigned i=0;while(i<3&&!(sub>>i&1))++i;++c.deferred[i];if(fixed)++c.fixed;++c.recorded;c.deltaWords+=n_;c.recordHigh=std::max(c.recordHigh,count_);});
        if(a.type==DrawType::PrimitiveUP||a.type==DrawType::IndexedUP){ // the API side effects of the skipped call
            dev_->SetStreamSource(0,nullptr,0,0);
            if(a.type==DrawType::IndexedUP)dev_->SetIndices(nullptr);
        }
        return true;
    }

    // An unknown order/resource dependency (clear, copy, order, query, frame...). With entries pending: replay them now in order (state restored afterwards, see the
    // minimal restore), count the reason, and the frame becomes Fallback (a cooldown follows unless `cooldown` is false). Nothing pending: no effect.
    // `by`: the in-place kind (water, rain, name) whose draw raised the Order hazard. That is a split, not a fallback: the pending draws are
    // replayed now, before the caller's in-place draw (the reference order), and the frame keeps deferring, so the later draws, which the game
    // draws after it, still replay after the effects (order kept again). Counted in orderBy and flushes, not as a hazard; no cooldown and no event.
    // The game's own sort decides per frame whether water comes before or after nearby effects, so a fallback here toggled with the camera.
    void hazard(Reason r,bool cooldown=true,InPlaceKind by=InPlaceKind::None){
        if(!count_)return;
        if(by!=InPlaceKind::None&&by!=InPlaceKind::Fade&&frame_.orderBy[0]+frame_.orderBy[1]+frame_.orderBy[2]<cfg_.maxOrderSplits){count([&](Counters& c){++c.orderBy[unsigned(by)-1];++c.flushes;});drain();return;}
        noteHazard(r);noteEvent(r,by);count([](Counters& c){++c.flushes;});
        drain();fm_.fallback(cooldown);
    }
    // Buffer::Lock hook, called BEFORE the lock is forwarded, only for buffers the markGuard callback marked. READONLY and non-conflicting writes
    // return untouched. A conflicting write to a DYNAMIC buffer without outstanding locks (conflictCopy allowed) first copies the guarded bytes
    // out (the affected entries then replay from a Northlight-owned buffer); anything else, or a failed/null read-back lock, is a BufWrite hazard
    // (flush first, so the pending draws are issued before the write lands). Never leaves a guard on bytes that changed.
    void onBufferWrite(void* rawBuffer,uint32_t offset,uint32_t size,DWORD flags,DWORD usage,unsigned outstandingLocks){
        if(!count_||(flags&D3DLOCK_READONLY))return;
        hits_.clear();
        if(!guards_.collect(rawBuffer,offset,size,flags,hits_))return;
        if(!cfg_.conflictCopy||!(usage&D3DUSAGE_DYNAMIC)||outstandingLocks){hazard(Reason::BufWrite);return;}
        copyOut(rawBuffer);
    }
    // True when onBufferWrite() would copy or flush (a pending draw reads bytes this write replaces). No side effects: the integration enters its raw
    // scope only for these writes, so the common non-conflicting ring append stays cheap.
    bool bufferWriteConflicts(void* rawBuffer,uint32_t offset,uint32_t size,DWORD flags)const{return count_&&!(flags&D3DLOCK_READONLY)&&guards_.conflicts(rawBuffer,offset,size,flags);}
    void onBufferGpuWrite(void* rawBuffer){if(count_&&guards_.guarded(rawBuffer))hazard(Reason::GpuWrite);} // ProcessVertices destination
    bool pendingSamples(void* rawTexture)const{
        if(!count_||held_.empty())return false;
        IUnknown* p=static_cast<IUnknown*>(rawTexture);const size_t m=held_.size()-1;
        for(size_t i=hashPtr(p)&m;held_[i].p;i=(i+1)&m)if(held_[i].p==p)return (held_[i].flags&kInSamples)!=0;
        return false;
    }
    // A texture write (the integration calls this BEFORE the write is forwarded: LockRect at lock time, Unlock/AddDirtyRect/GenerateMipSubLevels/UpdateTexture/UpdateSurface/StretchRect/ColorFill
    // before the call). Pending draws that sample the texture are replayed now, so they read the old texels, as in the game's order; the frame keeps deferring and the draws after the write
    // sample the new texels (a split like water: counted in texSplits and flushes, no fallback, no cooldown, no event). At most Config::maxTexSplits per frame: a texture that is written between
    // every two draws would make one replay per draw, so the next write is the old TexWrite hazard (flush, fallback, cooldown).
    void onTextureWrite(void* rawTexture){
        if(!pendingSamples(rawTexture))return;
        if(frame_.texSplits>=cfg_.maxTexSplits){hazard(Reason::TexWrite);return;}
        count([](Counters& c){++c.texSplits;++c.flushes;});drain();
    }

    // At B, after the effects: draws all pending entries in their original order into T/DS and restores the device state (minimal restore, else SavedState). Idempotent;
    // closes the frame (no more deferral until beginFrame, no cooldown). Entries whose upload could not be made are skipped and counted.
    void replay(){
        fm_.close();
        if(!count_)return;
        count([](Counters& c){++c.replays;});
        drain();
    }
    // Releases entry, delta and arena storage (and the upload buffers) beyond `keep` records; never below what is pending. For idle frames and memory pressure.
    void trim(size_t keep=0){
        const size_t chunks=std::max<size_t>(keep?1:0,count_?deltaChunk_+1:0);
        while(deltaChunks_.size()>chunks)deltaChunks_.pop_back();
        const size_t entryChunks=(std::max<size_t>(keep,count_)+kEntryChunk-1)/kEntryChunk;
        while(entryChunks_.size()>entryChunks)entryChunks_.pop_back();
        if(!count_&&!keep){arena_.reset();arenaCap_=0;}
        const size_t ffChunks=(ffCount_+kFfChunk-1)/kFfChunk;while(ffChunks_.size()>ffChunks)ffChunks_.pop_back();
        if(!count_&&!keep){
            releaseUploads();
            guards_.release();                                   // nothing is guarded while nothing is pending
            std::vector<Blob>().swap(blobs_);std::vector<GuardTable::Hit>().swap(hits_);std::vector<Seg>().swap(segs_);std::vector<uint32_t>().swap(order_);
            if(heldUsed_.empty()){std::vector<Held>().swap(held_);std::vector<uint32_t>().swap(heldUsed_);} // no reference is held: the table goes too
            releaseUndo();undo_.reset();checkScratch_.reset();
        }
    }
    // Device Reset/destroy/extension fault: drops everything WITHOUT drawing, releases records, buffers and surfaces, unmarks guards.
    void reset(){
        clearFrame();deltaChunks_.clear();entryChunks_.clear();arena_.reset();arenaCap_=0;ffChunks_.clear();releaseUploads();clearVerdicts();fm_.close();
    }
};
}
