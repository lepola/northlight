#pragma once
#include <cmath>
#include <cstddef>
#include "replay_bounds_schedule.h"
#include "actor_shadow_map.h"
#include <cstdint>
#include <cstring>
#include <vector>

// Visit order of the replay bounds passes (replay_bounds_job.h, pointCalculateReplayBounds): the passes walk the draw list from a
// rotating cursor under fixed time/call budgets (replay_envelopes.inl canReady/canHeavy/canBuild). When a crowd needs more than the
// budget, the packets after the point where it ran out stay without bounds this frame, and which ones those are rotates. Casters
// that can be in the actor map must not be among them: a caster without bounds is never proven inside the actor map's inset and falls
// to the near cascade for that frame. NearList collects, in the loop that already pins every packet's inputs (no extra pass, capacity
// reused), the packets whose position (palette root, or the anchor of a remembered rigid prop) lies where a caster can be wholly inside
// the actor map, in distance rings; nearFirst visits the rings from the innermost out, then the normal rotating pass runs unchanged
// (it skips packets that already have bounds). Same budgets, same total work.
// The walk of a ring is a NorthlightReplaySchedule::pass over it, with one rotating start per phase and ring kept in the List across
// frames: a packet that spends a whole phase moves the next frame's start past it, so a tail of expensive packets cannot miss every
// frame. The whole near walk may use at most half of a phase's operation (and vertex) budget (replay_bounds_job.h), so the rotating pass
// always has the other half. Every unfinished packet participates in that rotation: an expensive near packet that does not fit
// behind the self must still get a full turn. One frame in OuterTurn, the plain turn, the near walk covers only rings 0 and 1
// in the cheap phase and nothing in the heavy/build phases, leaving those phases' entire allowances to the rotating pass.
// Which packets: a caster is in the actor map only while its bounds lie within Stay of the map centre in light-space x/y (clip units,
// the map's own matrix: the last drawn one). Its position may lie outside that square by what the caster extends beyond it (CasterYards:
// the palette root is inside the model, a model that reaches farther is simply ordered by the rotating pass) and the map centre may
// move by Leash before the packets are drawn. The depth planes are not tested (a position beyond them is rare and costs only an order).
// Rings are Chebyshev distance from the map centre in clip units, boundaries taken from the map's constants; an O(n) bucket, no sort.
// Ring 0 is the self's own packets (the selection marked them this frame: Replay::selfShadow), ahead of everything; other casters, however close to
// the self, are in the distance rings, so a stacked crowd cannot push the self out of its place.
namespace NorthlightReplayBoundsNear {
inline constexpr float CasterYards=3.f;
inline constexpr float Limit=NorthlightActorShadowMap::Stay+(NorthlightActorShadowMap::Leash+CasterYards)/NorthlightActorShadowMap::Radius; /* clip units: about 1.2 map half widths */
inline constexpr float Inner=(NorthlightActorShadowMap::Leash+CasterYards)/NorthlightActorShadowMap::Radius;  /* ring 1: the pivot's neighbourhood (leash lag plus a caster) */
inline constexpr float Middle=NorthlightActorShadowMap::Inset;                                              /* ring 2: wholly inside the inset is possible; ring 3: the edge band and beyond */
inline constexpr unsigned Rings=4,Outer=1,OuterTurn=8; /* periodically reserve more work for the rotating pass */
static_assert(Inner<Middle&&Middle<Limit,"rings are nested");
struct List {
    float matrix[2][16]={};unsigned sources=0,turn=0;bool on=false; /* turn: counts the frames (begin) */
    std::vector<std::uint32_t> ring[Rings];std::size_t cursor[3][Rings]={}; /* per phase and ring, kept across frames (begin() leaves them) */
    // matrixN/validN: the actor map matrix of source N as last drawn (null or not valid: the source has no actor map).
    void begin(const float* matrix0,bool valid0,const float* matrix1,bool valid1){
        for(auto& r:ring)r.clear();sources=0;++turn;
        if(valid0&&matrix0){std::memcpy(matrix[0],matrix0,sizeof matrix[0]);sources|=1u;}
        if(valid1&&matrix1){std::memcpy(matrix[1],matrix1,sizeof matrix[1]);sources|=2u;}
        on=sources!=0;
    }
    std::size_t size()const{return ring[0].size()+ring[1].size()+ring[2].size()+ring[3].size();}
    // Distance ring (1..3) of a position, Rings when it cannot be wholly inside the actor map.
    unsigned ringOf(const float* at)const{
        float best=INFINITY;
        for(unsigned s=0;s<2;++s)if(sources&(1u<<s)){const float* m=matrix[s];
            const float x=std::fabs(at[0]*m[0]+at[1]*m[4]+at[2]*m[8]+m[12]),y=std::fabs(at[0]*m[1]+at[1]*m[5]+at[2]*m[9]+m[13]);
            const float e=x>y?x:y;
            if(e<=Inner)return 1;
            if(e<best)best=e;} /* a non-finite position never compares: it stays out */
        return best<=Inner?1:best<=Middle?2:best<=Limit?3:Rings;
    }
    // Periodically give the outer rings the rotating pass's full allowance.
    // The self and inner ring retain their cheap walk on these frames.
    bool plainTurn()const{return turn%OuterTurn==0;}
    // self: the packet belongs to the self (ring 0, whatever its position). at: its position (null: none, listed only as the self).
    // True: an outer ring packet (middle/inner neighbourhood, excluding the edge band).
    bool consider(std::size_t i,const float* at,bool self=false){
        if(!on)return false;
        const unsigned r=self?0u:at?ringOf(at):Rings;
        if(r>=Rings)return false;
        try{ring[r].push_back(std::uint32_t(i));}catch(...){on=false;return false;}
        return r>=Outer&&r<Rings-1;
    }
};
// Visit the listed packets of a phase, ring by ring from its rotating starts, while canWork() (the phase can work and the near share is
// not spent), with pass()'s anti-pin rule and Budget handling; visit(index) returns true for a packet that ran out of budget. True: the
// walk ended because canWork() turned false, not because every listed packet was seen.
template<class CanWork,class Visit> bool nearFirst(List* list,unsigned phase,CanWork canWork,Visit visit){
    if(!list||phase>=3)return false;
    bool stopped=false;
    for(unsigned r=0,last=list->plainTurn()?(phase==0?2u:0u):Rings;r<last&&!stopped;++r){
        const auto& index=list->ring[r];if(index.empty())continue;
        list->cursor[phase][r]=NorthlightReplaySchedule::pass(index.size(),list->cursor[phase][r]%index.size(),
            [&]{const bool can=canWork();stopped=!can;return can;},[&](std::size_t k){return visit(std::size_t(index[k]));});
    }
    return stopped;
}
}
