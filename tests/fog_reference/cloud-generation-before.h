#pragma once
// Frozen generator from c8206190d6b6745bcd93b6c96baeb5abb8d8ab05, MIT (repository LICENSE).
#include "fog_clouds.h"
namespace FrozenCloudGeneration {
using namespace NorthlightFogClouds;
inline std::vector<uint8_t> generate(uint32_t seed=Seed){
    // Reuse the output bytes as fixed-period gradient and feature tables until the final conversion; peak allocation is unchanged.
    std::vector<float> f(size_t(N)*N*N);std::vector<uint8_t> out(f.size());float lo=1e9f,hi=-1e9f;
#if defined(__SSE2__)
    static_assert(N%4==0,"Cloud rows fill SIMD extrema lanes");
    __m128 minima=_mm_set1_ps(lo),maxima=_mm_set1_ps(hi);
#endif
    for(unsigned o=0,P=4;o<3;++o,P*=2)for(unsigned z=0;z<P;++z)for(unsigned y=0;y<P;++y)for(unsigned x=0;x<P;++x)out[(o==0?0:o==1?64:576)+x+P*(y+P*z)]=uint8_t(detail::hash(x,y,z,seed+o*0x9e37u)%12);
    static_assert(size_t(N)*N*N>=4672+14400+8*4672,"Noise tables fit the output scratch");
    detail::prepareWorleyFeatures(out.data()+4672,seed);
    detail::preparePerlinCubes(out.data());
    detail::PerlinAxis axis[3][N];for(unsigned o=0,P=4;o<3;++o,P*=2)for(unsigned i=0;i<N;++i)axis[o][i]=detail::perlinAxis(float(i),float(P)/float(N));
    for(unsigned z=0;z<N;++z)for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){
        // Octave 1 shares Worley's 8-cell grid; its scaled coordinates are exact binary fractions.
        const auto& wx=axis[1][x];const auto& wy=axis[1][y];const auto& wz=axis[1][z];
        float p=0,a=1,sum=0;unsigned P=4;
        for(unsigned o=0;o<3;++o,P*=2,a*=.5f){p+=a*detail::perlinPrepared<true>(P,seed,o,axis[o][x],axis[o][y],axis[o][z],out.data()+19072);sum+=a;}
        const float pn=std::clamp(.5f+.5f*(p/sum)*1.4f,0.f,1.f);
        const float w=1.f-std::min(detail::worleyPrepared<true>(8,seed,float(wx.cell)+wx.fraction,float(wy.cell)+wy.fraction,float(wz.cell)+wz.fraction,wx.cell,wy.cell,wz.cell,out.data()+4672),1.f);
        const float v=.6f*pn+.4f*w;
        const size_t index=x+N*(y+size_t(N)*z);f[index]=v;
#if defined(__SSE2__)
        if((x&3)==3){const __m128 values=_mm_loadu_ps(f.data()+index-3);minima=_mm_min_ps(minima,values);maxima=_mm_max_ps(maxima,values);}
#else
        lo=std::min(lo,v);hi=std::max(hi,v);
#endif
    }
#if defined(__SSE2__)
    float lows[4],highs[4];_mm_storeu_ps(lows,minima);_mm_storeu_ps(highs,maxima);for(int i=0;i<4;++i){lo=std::min(lo,lows[i]);hi=std::max(hi,highs[i]);}
#endif
    const float k=hi>lo?255.f/(hi-lo):0.f;
    for(size_t i=0;i<f.size();++i)out[i]=detail::normalizedByte((f[i]-lo)*k+.5f);
    return out;}


}
