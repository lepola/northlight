#pragma once
// 0.3.203 (task 17): fallback effect boundary for clients whose UI shaders were replaced by another module (font mods). Pure logic, no D3D/Windows.
// The stock boundary is found by shader hash; otherwise frequent screen-space draws establish the UI VS. Once learned, rare font/background PS
// variants must trigger the same boundary too. The host still checks ZWRITE, projection, depth function and the full back-buffer target.
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <atomic>
namespace NorthlightUiBoundary {
// Only a previously learned shader may bypass the next device's learning period.
inline std::atomic<std::uint64_t> rememberedVsHash{0};
inline bool clipWOne(const float c3[4]){return std::fabs(c3[0])+std::fabs(c3[1])+std::fabs(c3[2])<.001f&&std::fabs(c3[3]-1.f)<.001f;}
// c = VS constants c0..c3 (16 floats): finite, c3 = (0,0,0,1), and the x/y rows carry a scale (not all zero).
inline bool screenSpaceRows(const float c[16]){
    for(int i=0;i<16;++i)if(!std::isfinite(c[i]))return false;
    if(!clipWOne(c+12))return false;
    bool any=false;for(int i=0;i<8;++i)if(c[i]!=0.f)any=true;
    return any;
}
// The draw-state part of the fallback predicate: Z writes off and screen-space rows in c0..c3. The renderer tests the rest around it: before, a
// non-world, non-water, non-WMO VS after the world; after, a full back-buffer viewport (fullViewport, the only D3D query, last).
// UI disables depth testing either with ZENABLE off or ZFUNC ALWAYS. A stale
// comparison function is irrelevant while Z is disabled.
inline bool accepts(bool zWrite,const float* c,bool zFuncAlways=true,bool zEnabled=true){return !zWrite&&(!zEnabled||zFuncAlways)&&c&&screenSpaceRows(c);}
// Native pixel-space text (including Ctrl+R) has a distinct VS from the
// scalable UI. Its full-window orthographic transform is sufficient proof;
// world names retain the world depth band and cannot satisfy this predicate.
inline bool fullWindowOrthoRows(const float c[16],unsigned width,unsigned height,float minZ,float maxZ){
    if(!width||!height||minZ!=0||maxZ!=1||!screenSpaceRows(c))return false;
    const float sx=2.f/width,sy=2.f/height;
    // Scalable UI uses logical units (observed scales 2.29469/4.07946 at
    // 3840x2160), while Ctrl+R uses physical pixels. Both preserve aspect and
    // the full-window origin, including the native half-pixel adjustment.
    const float aspect=float(height)/width;
    return c[0]>0&&std::fabs(c[5])>0&&std::fabs(c[0]/std::fabs(c[5])-aspect)<aspect*.001f&&
        std::fabs(std::fabs(c[3])-1)<=2*sx&&std::fabs(std::fabs(c[7])-1)<=2*sy&&
        c[1]==0&&c[2]==0&&c[4]==0&&c[6]==0&&c[8]==0&&c[9]==0;
}
// The former physical-pixel gate remains the regression counterfactual.
inline bool pixelSpaceRows(const float c[16],unsigned width,unsigned height,float minZ,float maxZ){
    return fullWindowOrthoRows(c,width,height,minZ,maxZ)&&std::fabs(c[0]-2.f/width)<2.f/width*.001f&&
        std::fabs(std::fabs(c[5])-2.f/height)<2.f/height*.001f;
}
class Arming{
public:
    static constexpr unsigned kWarmFrames=2,kArmFrames=120,kLostFrames=120,kMinDrawsPerFrame=8,kSlots=32,kMaxPs=4,kMinSharePercent=10;
    enum class FrameKind{NoWorld,Neutral,WorldMissed,WorldHash,WorldFallback};
    enum class Result{None,Armed,Lost};
    struct Pair{std::uintptr_t vs,ps;unsigned count;};
    // Candidates are noted into the frame's counts (frame) and only kept (count) at the end of a WorldMissed frame, so the arming density is
    // draws per counted frame: a Neutral frame (effects off) can neither advance the streak nor add draws.
    void onHashBoundary(){disarmedForever=true;armedNow=false;clearCounts();streak=lostStreak=0;}
    bool disarmed()const{return disarmedForever;}
    bool armed()const{return armedNow;}
    bool learning()const{return !disarmedForever&&!armedNow;}
    // The renderer reads draw state only while this holds: armed, or learning after kWarmFrames consecutive world frames without a hash boundary.
    // A stock client sees the hash boundary in its first world frame, so its draws never reach the fallback's state reads (or fullViewport's log).
    bool collecting()const{return armedNow||(!disarmedForever&&streak>=kWarmFrames);}
    void noteCandidate(std::uintptr_t vs,std::uintptr_t ps){
        if(!vs||!learning())return;
        for(unsigned i=0;i<used;++i)if(slots[i].vs==vs&&slots[i].ps==ps){++frame[i];return;}
        if(used>=kSlots){++dropped;return;}
        frame[used]=1;slots[used++]=Pair{vs,ps,0};
    }
    Result endFrame(FrameKind k){
        settleFrame(k==FrameKind::WorldMissed&&learning());
        switch(k){
        case FrameKind::NoWorld:streak=0;clearCounts();return Result::None;
        case FrameKind::Neutral:return Result::None;
        case FrameKind::WorldHash:onHashBoundary();return Result::None;
        case FrameKind::WorldFallback:lostStreak=0;return Result::None;
        case FrameKind::WorldMissed:break;
        }
        if(disarmedForever)return Result::None;
        if(armedNow){
            if(++lostStreak>=kLostFrames){armedNow=false;lostStreak=0;streak=0;clearCounts();return Result::Lost;}
            return Result::None;
        }
        ++missed;
        if(++streak<kArmFrames)return Result::None;
        return tryArm()?Result::Armed:Result::None;
    }
    bool isBoundary(std::uintptr_t vs,std::uintptr_t /*ps*/)const{return armedNow&&vs==lVs;}
    bool restore(std::uintptr_t vs,std::uintptr_t ps){
        if(disarmedForever||armedNow||!vs)return false;
        lVs=vs;lPsCount=ps?1:0;if(ps)lPs[0]=ps;armedNow=true;streak=lostStreak=0;clearCounts();return true;
    }
    void forget(std::uintptr_t obj){
        if(disarmedForever)return;
        if(armedNow){
            if(obj==lVs){armedNow=false;streak=lostStreak=0;clearCounts();}
            else{unsigned w=0;for(unsigned i=0;i<lPsCount;++i)if(lPs[i]!=obj)lPs[w++]=lPs[i];lPsCount=w;}
            return;
        }
        unsigned w=0;for(unsigned i=0;i<used;++i)if(slots[i].vs!=obj&&slots[i].ps!=obj){frame[w]=frame[i];slots[w++]=slots[i];}
        used=w;
    }
    std::uintptr_t learnedVs()const{return lVs;}
    unsigned learnedPsCount()const{return lPsCount;} // tests only
    std::uintptr_t learnedPs(unsigned i)const{return i<lPsCount?lPs[i]:0;}
    float drawsPerFrame()const{return perFrame;}
    unsigned missedWorldFrames()const{return missed;}
    unsigned droppedPairs()const{return dropped;}
    unsigned candidateStreak()const{return streak;} // tests only
    unsigned topCandidates(Pair* out,unsigned max)const{
        Pair tmp[kSlots];for(unsigned i=0;i<used;++i)tmp[i]=slots[i];
        unsigned n=0;
        for(;n<max&&n<used;++n){unsigned b=n;for(unsigned j=n+1;j<used;++j)if(tmp[j].count>tmp[b].count)b=j;const Pair t=tmp[n];tmp[n]=tmp[b];tmp[b]=t;out[n]=tmp[n];}
        return n;
    }
private:
    void clearCounts(){used=0;}
    // Keeps (WorldMissed while learning) or drops this frame's notes; a pair with no kept count gives its slot back.
    void settleFrame(bool keep){
        unsigned w=0;
        for(unsigned i=0;i<used;++i){if(keep)slots[i].count+=frame[i];frame[i]=0;if(slots[i].count){frame[w]=0;slots[w++]=slots[i];}}
        used=w;
    }
    bool tryArm(){
        std::uintptr_t bestVs=0;unsigned bestTotal=0;
        for(unsigned i=0;i<used;++i){
            unsigned total=0;for(unsigned j=0;j<used;++j)if(slots[j].vs==slots[i].vs)total+=slots[j].count;
            if(total>bestTotal){bestTotal=total;bestVs=slots[i].vs;}
        }
        const float density=streak?float(bestTotal)/float(streak):0.f;
        if(bestTotal==0||density<float(kMinDrawsPerFrame)){clearCounts();streak=0;return false;}
        Pair mine[kSlots];unsigned n=0;
        for(unsigned i=0;i<used;++i)if(slots[i].vs==bestVs&&slots[i].count*100u>=bestTotal*kMinSharePercent)mine[n++]=slots[i];
        for(unsigned i=0;i<n;++i){unsigned b=i;for(unsigned j=i+1;j<n;++j)if(mine[j].count>mine[b].count)b=j;const Pair t=mine[i];mine[i]=mine[b];mine[b]=t;}
        lVs=bestVs;lPsCount=n<kMaxPs?n:kMaxPs;for(unsigned i=0;i<lPsCount;++i)lPs[i]=mine[i].ps;
        perFrame=density;armedNow=true;lostStreak=0;streak=0;clearCounts();return true;
    }
    Pair slots[kSlots]={};unsigned frame[kSlots]={};unsigned used=0,dropped=0,streak=0,lostStreak=0,missed=0;
    bool disarmedForever=false,armedNow=false;
    std::uintptr_t lVs=0,lPs[kMaxPs]={};unsigned lPsCount=0;float perFrame=0.f;
};
}
