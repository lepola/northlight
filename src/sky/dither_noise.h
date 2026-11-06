#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

// WorldComposite dither: CPU only, no D3D object. The composite writes an 8-bit target, so smooth gradients that
// depend only on view elevation (horizon haze, dark night air) round into horizontal bands. A static tileable
// 64x64 blue-noise tile, added before that rounding, breaks the bands into an invisible grain. Values are signed
// and already scaled, so the shader spends one add (and one texld) on it.
namespace NorthlightDitherNoise {
inline constexpr unsigned N=64;
inline constexpr float Amplitude=1.f/255; // triangular, +-1 LSB: the rounding error stops depending on the gradient
inline constexpr float Sigma=1.5f;        // void-and-cluster filter width in texels
inline constexpr float InitialFill=.1f;

// Void and cluster (Ulichney 1993) on the torus: each texel's rank 0..N*N-1, deterministic.
inline std::vector<uint32_t> ranks(){
    constexpr unsigned count=N*N;
    std::vector<float> kernel(count);
    for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){
        if(y>N/2){kernel[y*N+x]=kernel[(N-y)*N+x];continue;}
        if(x>N/2){kernel[y*N+x]=kernel[y*N+N-x];continue;}
        const float dx=float(std::min(x,N-x)),dy=float(std::min(y,N-y));
        kernel[y*N+x]=std::exp(-(dx*dx+dy*dy)/(2*Sigma*Sigma));
    }
    std::vector<float> energy(count,0.f);
    static_assert(count%32==0);
    std::array<uint32_t,count/32> occupied{};
    auto toggle=[&](unsigned i,bool set){
        if(set)occupied[i/32]|=1u<<(i%32);else occupied[i/32]&=~(1u<<(i%32));
        const unsigned ix=i%N,iy=i/N,shift=(N-ix)%N,split=N-shift;
        for(unsigned y=0;y<N;++y){const float* row=&kernel[((y+N-iy)%N)*N];float* out=&energy[y*N];
            if(set){for(unsigned x=0;x<split;++x)out[x]+=row[x+shift];for(unsigned x=split;x<N;++x)out[x]+=row[x-split];}
            else{for(unsigned x=0;x<split;++x)out[x]-=row[x+shift];for(unsigned x=split;x<N;++x)out[x]-=row[x-split];}}
    };
    // Tightest cluster: the set texel with the most energy. Largest void: the unset texel with the least. With a
    // constant kernel sum the void is also the tightest cluster of the unset texels, so one search serves both
    // the second and the third phase.
    auto cluster=[&]{unsigned best=0;float e=-1;
        for(unsigned word=0;word<occupied.size();++word)for(uint32_t bits=occupied[word];bits;bits&=bits-1){
            const unsigned i=word*32+unsigned(__builtin_ctz(bits));if(energy[i]>e){e=energy[i];best=i;}}
        return best;};
    auto largestVoid=[&]{unsigned best=0;float e=std::numeric_limits<float>::infinity();
        for(unsigned word=0;word<occupied.size();++word)for(uint32_t bits=~occupied[word];bits;bits&=bits-1){
            const unsigned i=word*32+unsigned(__builtin_ctz(bits));if(energy[i]<e){e=energy[i];best=i;}}
        return best;};
    // Initial pattern: a fixed LCG, then swap the tightest cluster into the largest void until stable.
    const unsigned ones=unsigned(count*InitialFill);uint32_t seed=0x2545F491u;
    for(unsigned placed=0;placed<ones;){seed=seed*1664525u+1013904223u;const unsigned i=(seed>>8)%count;if(!(occupied[i/32]&(1u<<(i%32)))){toggle(i,true);++placed;}}
    for(unsigned guard=0;guard<4*count;++guard){
        const unsigned c=cluster();toggle(c,false);const unsigned v=largestVoid();
        toggle(v,true);if(v==c)break;
    }
    const std::vector<float> prototypeEnergy=energy;const auto prototypeOccupied=occupied;
    std::vector<uint32_t> rank(count,0);
    for(unsigned r=ones;r-->0;){const unsigned c=cluster();toggle(c,false);rank[c]=r;}
    energy=prototypeEnergy;occupied=prototypeOccupied;
    for(unsigned r=ones;r<count;++r){const unsigned v=largestVoid();toggle(v,true);rank[v]=r;}
    return rank;
}

// The tile as signed offsets: ranks spread uniformly over (0,1), then remapped to a triangular distribution of
// +-Amplitude (it keeps the rank order, so the tile stays blue).
inline std::vector<float> offsets(){
    static const std::vector<float> cached=[](){
    const std::vector<uint32_t> r=ranks();std::vector<float> out(r.size());
    for(size_t i=0;i<r.size();++i){
        const float u=(float(r[i])+.5f)/float(r.size());
        const float t=u<.5f?std::sqrt(2*u)-1:1-std::sqrt(2-2*u);
        out[i]=t*Amplitude;
    }
    return out;
    }();
    return cached;
}
}
