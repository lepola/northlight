#pragma once
// 0.3.203 (task 17): fallback effect boundary for clients whose UI shaders were replaced by another module (font mods). Pure logic, no D3D/Windows.
// The stock boundary is found by shader hash; when it never fires although the world draws, this learns the (VS,PS) pair that draws the screen-space
// UI after the world (ZWRITE off, full-target viewport, ortho rows with c3=(0,0,0,1)) and uses it as the boundary. Identities are shader object pointers.
#include <cstddef>
#include <cstdint>
#include <cmath>
namespace NorthlightUiBoundary {
inline bool clipWOne(const float c3[4]){return std::fabs(c3[0])+std::fabs(c3[1])+std::fabs(c3[2])<.001f&&std::fabs(c3[3]-1.f)<.001f;}
// c = VS constants c0..c3 (16 floats): finite, c3 = (0,0,0,1), and the x/y rows carry a scale (not all zero).
inline bool screenSpaceRows(const float c[16]){
    for(int i=0;i<16;++i)if(!std::isfinite(c[i]))return false;
    if(!clipWOne(c+12))return false;
    bool any=false;for(int i=0;i<8;++i)if(c[i]!=0.f)any=true;
    return any;
}
struct Candidate{bool afterWorld,worldVs,waterVs,zWrite,fullTarget;const float* c;};
inline bool accepts(const Candidate& d){return d.afterWorld&&!d.worldVs&&!d.waterVs&&!d.zWrite&&d.fullTarget&&d.c&&screenSpaceRows(d.c);}
class Arming{
public:
    static constexpr unsigned kWarmFrames=2,kArmFrames=120,kLostFrames=120,kMinDrawsPerFrame=8,kSlots=32,kMaxPs=4,kMinSharePercent=10;
    enum class FrameKind{NoWorld,Neutral,WorldMissed,WorldHash,WorldFallback};
    enum class Result{None,Armed,Lost};
    struct Pair{std::uintptr_t vs,ps;unsigned count;};
    void onHashBoundary(){disarmedForever=true;armedNow=false;clearCounts();streak=lostStreak=0;}
    bool disarmed()const{return disarmedForever;}
    bool armed()const{return armedNow;}
    bool learning()const{return !disarmedForever&&!armedNow;}
    // The renderer reads draw state only while this holds: armed, or learning after kWarmFrames consecutive world frames without a hash boundary.
    // A stock client sees the hash boundary in its first world frame, so its draws never reach the fallback's state reads (or fullViewport's log).
    bool collecting()const{return armedNow||(!disarmedForever&&streak>=kWarmFrames);}
    void noteCandidate(std::uintptr_t vs,std::uintptr_t ps){
        if(!learning())return;
        for(unsigned i=0;i<used;++i)if(slots[i].vs==vs&&slots[i].ps==ps){++slots[i].count;return;}
        if(used>=kSlots){++dropped;return;}
        slots[used++]=Pair{vs,ps,1};
    }
    Result endFrame(FrameKind k){
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
    bool isBoundary(std::uintptr_t vs,std::uintptr_t ps)const{
        if(!armedNow||vs!=lVs)return false;
        for(unsigned i=0;i<lPsCount;++i)if(lPs[i]==ps)return true;
        return false;
    }
    void forget(std::uintptr_t obj){
        if(disarmedForever)return;
        if(armedNow){
            bool hit=obj==lVs;for(unsigned i=0;i<lPsCount;++i)if(lPs[i]==obj)hit=true;
            if(hit){armedNow=false;streak=lostStreak=0;clearCounts();}
            return;
        }
        unsigned w=0;for(unsigned i=0;i<used;++i)if(slots[i].vs!=obj&&slots[i].ps!=obj)slots[w++]=slots[i];
        used=w;
    }
    std::uintptr_t learnedVs()const{return lVs;}
    unsigned learnedPsCount()const{return lPsCount;}
    std::uintptr_t learnedPs(unsigned i)const{return i<lPsCount?lPs[i]:0;}
    float drawsPerFrame()const{return perFrame;}
    unsigned missedWorldFrames()const{return missed;}
    unsigned droppedPairs()const{return dropped;}
    unsigned candidateStreak()const{return streak;}
    unsigned topCandidates(Pair* out,unsigned max)const{
        Pair tmp[kSlots];for(unsigned i=0;i<used;++i)tmp[i]=slots[i];
        unsigned n=0;
        for(;n<max&&n<used;++n){unsigned b=n;for(unsigned j=n+1;j<used;++j)if(tmp[j].count>tmp[b].count)b=j;const Pair t=tmp[n];tmp[n]=tmp[b];tmp[b]=t;out[n]=tmp[n];}
        return n;
    }
private:
    void clearCounts(){used=0;}
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
    Pair slots[kSlots]={};unsigned used=0,dropped=0,streak=0,lostStreak=0,missed=0;
    bool disarmedForever=false,armedNow=false;
    std::uintptr_t lVs=0,lPs[kMaxPs]={};unsigned lPsCount=0;float perFrame=0.f;
};
}
