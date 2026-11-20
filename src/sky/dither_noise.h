#pragma once
#include <algorithm>
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
inline constexpr float Amplitude=.5f/255; // uniform, +-half LSB: unbiased rounding without texturing exact colours
inline constexpr float Sigma=1.5f;        // void-and-cluster filter width in texels
inline constexpr float InitialFill=.1f;

// Void and cluster (Ulichney 1993) on the torus: each texel's rank 0..N*N-1, deterministic.
inline std::vector<uint32_t> ranks(){
    constexpr unsigned count=N*N;
    std::vector<float> kernel(count);
    for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){
        const float dx=float(std::min(x,N-x)),dy=float(std::min(y,N-y));
        kernel[y*N+x]=std::exp(-(dx*dx+dy*dy)/(2*Sigma*Sigma));
    }
    std::vector<uint8_t> on(count,0);std::vector<float> energy(count,0.f);
    auto toggle=[&](unsigned i,bool set){
        on[i]=set;const unsigned ix=i%N,iy=i/N;const float s=set?1.f:-1.f;
        for(unsigned y=0;y<N;++y){const float* row=&kernel[((y+N-iy)%N)*N];float* out=&energy[y*N];
            for(unsigned x=0;x<N;++x)out[x]+=s*row[(x+N-ix)%N];}
    };
    // Tightest cluster: the set texel with the most energy. Largest void: the unset texel with the least. With a
    // constant kernel sum the void is also the tightest cluster of the unset texels, so one search serves both
    // the second and the third phase.
    auto cluster=[&]{unsigned best=0;float e=-1;for(unsigned i=0;i<count;++i)if(on[i]&&energy[i]>e){e=energy[i];best=i;}return best;};
    auto largestVoid=[&]{unsigned best=0;float e=std::numeric_limits<float>::infinity();for(unsigned i=0;i<count;++i)if(!on[i]&&energy[i]<e){e=energy[i];best=i;}return best;};
    // Initial pattern: a fixed LCG, then swap the tightest cluster into the largest void until stable.
    const unsigned ones=unsigned(count*InitialFill);uint32_t seed=0x2545F491u;
    for(unsigned placed=0;placed<ones;){seed=seed*1664525u+1013904223u;const unsigned i=(seed>>8)%count;if(!on[i]){toggle(i,true);++placed;}}
    for(unsigned guard=0;guard<4*count;++guard){
        const unsigned c=cluster();toggle(c,false);const unsigned v=largestVoid();
        toggle(v,true);if(v==c)break;
    }
    const std::vector<uint8_t> prototypeOn=on;const std::vector<float> prototypeEnergy=energy;
    std::vector<uint32_t> rank(count,0);
    for(unsigned r=ones;r-->0;){const unsigned c=cluster();toggle(c,false);rank[c]=r;}
    on=prototypeOn;energy=prototypeEnergy;
    for(unsigned r=ones;r<count;++r){const unsigned v=largestVoid();toggle(v,true);rank[v]=r;}
    return rank;
}

// Uniform half-step offsets turn fractional 8-bit colours into unbiased rounding.
// Exact 8-bit colours stay exact. The previous triangular full-step distribution
// changed 25% of those pixels, adding visible grain to already composited water.
// Keep the rank order and static blue-noise tile for smooth sky/fog gradients.
inline std::vector<float> offsets(){
    const std::vector<uint32_t> r=ranks();std::vector<float> out(r.size());
    for(size_t i=0;i<r.size();++i){
        const float u=(float(r[i])+.5f)/float(r.size());
        out[i]=(2*u-1)*Amplitude;
    }
    return out;
}
}
