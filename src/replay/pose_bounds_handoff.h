#pragma once
#include "replay_bounds.h"
#include "captured_constant_epoch.h"

namespace NorthlightPoseBounds {
// Ephemeral metadata; the containing CaptureSource owns the raw mesh/declaration.
// No pose cache: only an unchanged current bank can consume this draw's result.
struct Handoff {
    NorthlightReplayBounds::Bounds bounds;
    // Exactly the words compared by ConstantEpoch::exact; block dirtiness is
    // unnecessary because this consumer never accepts a partially changed bank.
    std::uint64_t reset=0,floats=0,booleans=0,integers=0;
    const void *source=nullptr,*shader=nullptr,*declaration=nullptr;
    float clipSign=0;bool outsideRange=false;
    bool bind(const NorthlightConstantEpoch::Stamp& current,const void* mesh,const void* vs,const void* decl){
        if(!current.valid||!current.held||!mesh||!vs||!decl)return false;
        reset=current.reset;floats=current.floats;booleans=current.booleans;integers=current.integers;
        source=mesh;shader=vs;declaration=decl;return true;
    }
    void publishFar(const NorthlightConstantEpoch::Stamp& current,const void* mesh,const void* vs,const void* decl){
        *this={};outsideRange=bind(current,mesh,vs,decl);
    }
    void publish(const NorthlightReplayBounds::Bounds& value,const float* bank,
                 const NorthlightConstantEpoch::Stamp& current,const void* mesh,const void* vs,const void* decl){
        *this={};if(!value.valid||!bank||!current.valid||!current.held||!mesh||!vs||!decl)return;
        if(bank[20]!=0||bank[21]!=0||bank[23]!=0||(bank[22]!=1&&bank[22]!=-1))return;
        for(unsigned i=8;i<24;++i)if(!std::isfinite(bank[i])||std::fabs(bank[i])>1000000)return;
        for(unsigned i=0;i<3;++i)if(!std::isfinite(value.low[i])||!std::isfinite(value.high[i])||value.low[i]>value.high[i]||
            std::max(std::fabs(value.low[i]),std::fabs(value.high[i]))>1000000)return;
        bounds=value;bind(current,mesh,vs,decl);clipSign=bank[22];
    }
    bool outside(const void* mesh,const void* vs,const void* decl,const NorthlightConstantEpoch::Stamp& current)const{
        if(!current.valid||!current.held||source!=mesh||shader!=vs||declaration!=decl||
           reset!=current.reset||floats!=current.floats||booleans!=current.booleans||integers!=current.integers)return false;
        if(outsideRange)return true;
        if(!bounds.valid)return false;
        const double lower=clipSign==1?double(bounds.low[2]):clipSign==-1?-double(bounds.high[2]):-INFINITY;
        return std::nextafter(lower,-INFINITY)>48.01;
    }
};
}
