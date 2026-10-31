#pragma once
// 0.3.199 (fog clouds): the CPU side of the moving low fog banks. The GPU pass (a half-resolution raymarch) samples a tileable 3D
// noise volume (N^3 L8) at two world-space scales offset by the wind and weights the result near the ground; this header makes the
// volume, advances the wind, derives the per-frame constants from the settings and the weather, and mirrors one GPU sample
// (sigma/sigmaAt) so the shader maths is testable. Pure: no D3D, no game data, no platform-dependent functions.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace NorthlightFogClouds {
constexpr unsigned N=64;                            /* noise volume edge, L8 voxels, index x+N*(y+N*z) */
constexpr float LargePeriod=192.f,SmallPeriod=48.f; /* world units per noise tile (0.3.199 game tests tried 384/96..576/144; back to the original by choice) */
constexpr uint32_t Seed=0x4e4c4643u;
constexpr float kDry=0.05f,kNight=0.18f,kSigmaMax=0.03f,kBaseHeight=12,kRainHeight=8; /* bank height 12 units dry, 20 in full rain (0.3.199 game test: was 7/14, the banks should reach higher) */
constexpr float kDrySigma=0.33f; /* peak extinction share without rain: sigmaMax = kSigmaMax x (kDrySigma + (kRainSigma-kDrySigma) fog) (game tests: dry banks fainter, .65 -> .45 -> .33) */
constexpr float kRainSigma=0.75f; /* peak extinction share in full rain (0.3.213 game tests: rain banks too opaque, 1 -> .85 -> .75); the banks' size and coverage are unchanged */
constexpr float kDryNightThin=0.12f; /* dry nights thinner still: sigmaMax x (1 - kDryNightThin night (1-fog)); .33 x .88 keeps the dry night at .29 (game test: .45 x .65 was right) */
constexpr double kWindDry=1.05,kWindRain=4.2; /* large-scale wind, units/s, dry and full rain (0.3.199 game test: was .6/2.4, read as too slow) */
constexpr float kMaxCoverage=0.7f,kDense=0.35f,kMinCoverage=0.01f; /* coverage cap (rain keeps gaps), fully dense share of the covered area, below it the pass is skipped.
    The game tests tried larger, fainter, sparser and lower banks; the original look was kept (size, density, coverage, height), only the faster wind stayed */

namespace detail {
inline uint32_t hash(uint32_t a,uint32_t b,uint32_t c,uint32_t d){ /* integer mix, same result everywhere */
    uint32_t h=a*0x9e3779b1u^(b+0x7f4a7c15u)*0x85ebca6bu^(c+0x165667b1u)*0xc2b2ae35u^(d+0x27d4eb2fu)*0x9e3779b9u;
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;return h;}
inline float unit(uint32_t h){return float(h>>8)*(1.f/16777216.f);}
inline float fade(float t){return t*t*t*(t*(t*6-15)+10);}
inline float lerp(float a,float b,float t){return a+(b-a)*t;}
// Clipping first makes the unsigned conversion the same as floor then clip.
inline uint8_t normalizedByte(float v){return uint8_t(std::clamp(v,0.f,255.f));}
// periodic gradient noise, P cells per tile, evaluated at voxel (x,y,z); about -1..1
struct PerlinAxis {int cell;float fraction,faded;};
inline PerlinAxis perlinAxis(float p,float scale){p*=scale;const int cell=int(std::floor(p));const float fraction=p-float(cell);return {cell,fraction,fade(fraction)};}
inline constexpr float G[12][3]={{1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};
template<bool Cube=false> inline float perlinPrepared(unsigned P,uint32_t seed,unsigned oct,const PerlinAxis& X,const PerlinAxis& Y,const PerlinAxis& Z,const uint8_t* gradients){

    const int ix=X.cell,iy=Y.cell,iz=Z.cell;const float fx=X.fraction,fy=Y.fraction,fz=Z.fraction;
    float v[8];const bool powerOfTwo=P&&(P&(P-1))==0;const uint8_t* cube=nullptr;
    if constexpr(Cube){const unsigned cx=uint32_t(ix)&(P-1),cy=uint32_t(iy)&(P-1),cz=uint32_t(iz)&(P-1);cube=gradients+8*((oct==0?0:oct==1?64:576)+cx+P*(cy+P*cz));}
    for(int k=0;k<8;++k){
        const int dx=k&1,dy=(k>>1)&1,dz=(k>>2)&1;
        const uint32_t cx=powerOfTwo?(uint32_t(ix+dx)&(P-1)):uint32_t(ix+dx)%P,cy=powerOfTwo?(uint32_t(iy+dy)&(P-1)):uint32_t(iy+dy)%P,cz=powerOfTwo?(uint32_t(iz+dz)&(P-1)):uint32_t(iz+dz)%P;
        const float* g=G[Cube?cube[k]:(gradients?gradients[(oct==0?0:oct==1?64:576)+cx+P*(cy+P*cz)]:hash(cx,cy,cz,seed+oct*0x9e37u)%12)];
        v[k]=float(g[0])*(fx-float(dx))+float(g[1])*(fy-float(dy))+float(g[2])*(fz-float(dz));}
    const float u=X.faded,vv=Y.faded,w=Z.faded;
    return lerp(lerp(lerp(v[0],v[1],u),lerp(v[2],v[3],u),vv),lerp(lerp(v[4],v[5],u),lerp(v[6],v[7],u),vv),w);}
#if defined(__SSE2__)
// Four aligned generator voxels share one cube at every authored octave.
inline __m128 perlinFourPrepared(unsigned P,unsigned oct,const PerlinAxis* X,const PerlinAxis& Y,const PerlinAxis& Z,const uint8_t* gradients){
    const unsigned cell=(oct==0?0:oct==1?64:576)+unsigned(X[0].cell)+P*(unsigned(Y.cell)+P*unsigned(Z.cell));
    const uint8_t* cube=gradients+8*cell;
    const __m128 fx=_mm_set_ps(X[3].fraction,X[2].fraction,X[1].fraction,X[0].fraction),u=_mm_set_ps(X[3].faded,X[2].faded,X[1].faded,X[0].faded);
    __m128 v[8];
    for(unsigned k=0;k<8;++k){const float* g=G[cube[k]];
        const __m128 dx=_mm_sub_ps(fx,_mm_set1_ps(float(k&1)));
        v[k]=_mm_add_ps(_mm_add_ps(_mm_mul_ps(_mm_set1_ps(g[0]),dx),_mm_set1_ps(g[1]*(Y.fraction-float((k>>1)&1)))),_mm_set1_ps(g[2]*(Z.fraction-float((k>>2)&1))));
    }
    const auto mix=[](__m128 a,__m128 b,__m128 f){return _mm_add_ps(a,_mm_mul_ps(_mm_sub_ps(b,a),f));};
    const __m128 y=_mm_set1_ps(Y.faded),z=_mm_set1_ps(Z.faded);
    return mix(mix(mix(v[0],v[1],u),mix(v[2],v[3],u),y),mix(mix(v[4],v[5],u),mix(v[6],v[7],u),y),z);
}
#endif
inline float perlin(unsigned P,uint32_t seed,unsigned oct,float x,float y,float z,const uint8_t* gradients=nullptr){
    const float scale=float(P)/float(N);return perlinPrepared(P,seed,oct,perlinAxis(x,scale),perlinAxis(y,scale),perlinAxis(z,scale),gradients);}
// Cache each cell's eight gradient indices while the output is scratch.
inline void preparePerlinCubes(uint8_t* out){
#if defined(__SSE2__)
    for(unsigned o=0,P=4;o<3;++o,P*=2)for(unsigned z=0;z<P;++z)for(unsigned y=0;y<P;++y)for(unsigned x=0;x<P;x+=4){
        const unsigned base=o==0?0:o==1?64:576,cell=base+x+P*(y+P*z);__m128i rows[4];
        for(unsigned row=0;row<4;++row){
            const unsigned at=base+P*(((y+(row&1))&(P-1))+P*((z+(row>>1))&(P-1)));
            uint32_t first;std::memcpy(&first,out+at+x,sizeof first);
            const uint32_t next=(first>>8)|(uint32_t(out[at+((x+4)&(P-1))])<<24);
            rows[row]=_mm_unpacklo_epi8(_mm_cvtsi32_si128(int32_t(first)),_mm_cvtsi32_si128(int32_t(next)));
        }
        const __m128i low=_mm_unpacklo_epi16(rows[0],rows[1]),high=_mm_unpacklo_epi16(rows[2],rows[3]);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out+19072+8*cell),_mm_unpacklo_epi32(low,high));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out+19072+8*cell+16),_mm_unpackhi_epi32(low,high));
    }
#else
    for(unsigned o=0,P=4;o<3;++o,P*=2)for(unsigned z=0;z<P;++z)for(unsigned y=0;y<P;++y)for(unsigned x=0;x<P;++x){
        const unsigned base=o==0?0:o==1?64:576,cell=base+x+P*(y+P*z);
        for(unsigned k=0;k<8;++k){const unsigned xx=(x+(k&1))&(P-1),yy=(y+((k>>1)&1))&(P-1),zz=(z+((k>>2)&1))&(P-1);out[19072+8*cell+k]=out[base+xx+P*(yy+P*zz)];}}
#endif
}
// The generator visits cells -1..8 around the periodic grid; cache the same
// cell-plus-feature floats in three padded planes in the existing output scratch.
inline void prepareWorleyFeatures(uint8_t* out,uint32_t seed){
    for(int z=-1;z<=8;++z)for(int y=-1;y<=8;++y){
        for(int x=-1;x<=8;++x){
            const uint32_t h=hash(uint32_t(x)&7,uint32_t(y)&7,uint32_t(z)&7,seed^0xabcdu);
            const float v[3]={float(x)+unit(h),float(y)+unit(h*0x2545f491u+1),float(z)+unit(h*0x9e3779b1u+7)};
            const int at=(x+1)+12*((y+1)+10*(z+1));for(int i=0;i<3;++i)std::memcpy(out+4*(at+i*1200),v+i,4);
        }
        // Features overwrite the first ten cells; zero only SIMD padding.
        const uint64_t zero=0;const int padding=10+12*((y+1)+10*(z+1));
        for(int i=0;i<3;++i)std::memcpy(out+4*(padding+i*1200),&zero,sizeof zero);
    }
}
// periodic cellular F1 distance in cell units, P cells per tile
// Absolute expects the generator's eight-cell grid and its padded feature table.
template<bool Absolute=false> inline float worleyPrepared(unsigned P,uint32_t seed,float x,float y,float z,int ix,int iy,int iz,const uint8_t* features){
    float best=4.f;
#if defined(__SSE2__)
    if constexpr(Absolute){const __m128 vx=_mm_set1_ps(x),vy=_mm_set1_ps(y),vz=_mm_set1_ps(z);__m128 minima=_mm_set1_ps(4.f);for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy){
        const int at=ix+12*((iy+dy+1)+10*(iz+dz+1));__m128 X,Y,Z;std::memcpy(&X,features+4*at,16);std::memcpy(&Y,features+4*(at+1200),16);std::memcpy(&Z,features+4*(at+2400),16);
        X=_mm_sub_ps(X,vx);Y=_mm_sub_ps(Y,vy);Z=_mm_sub_ps(Z,vz);const __m128 d=_mm_add_ps(_mm_add_ps(_mm_mul_ps(X,X),_mm_mul_ps(Y,Y)),_mm_mul_ps(Z,Z));
        minima=_mm_min_ps(minima,d);
    }
    float values[4];_mm_storeu_ps(values,minima);best=std::min(best,values[0]);best=std::min(best,values[1]);best=std::min(best,values[2]);return std::sqrt(best);}
#endif
    const bool powerOfTwo=P&&(P&(P-1))==0;
    for(int dz=-1;dz<=1;++dz){const int cz=iz+dz;const uint32_t hz=powerOfTwo?(uint32_t(cz)&(P-1)):uint32_t((cz%int(P)+int(P))%int(P));
      for(int dy=-1;dy<=1;++dy){const int cy=iy+dy;const uint32_t hy=powerOfTwo?(uint32_t(cy)&(P-1)):uint32_t((cy%int(P)+int(P))%int(P));
       for(int dx=-1;dx<=1;++dx){const int cx=ix+dx;const uint32_t hx=powerOfTwo?(uint32_t(cx)&(P-1)):uint32_t((cx%int(P)+int(P))%int(P));
        float px,py,pz;
        if constexpr(Absolute){const int at=(cx+1)+12*((cy+1)+10*(cz+1));std::memcpy(&px,features+4*at,4);std::memcpy(&py,features+4*(at+1200),4);std::memcpy(&pz,features+4*(at+2400),4);}
        else{float u[3];if(features)std::memcpy(u,features+12*(hx+8*(hy+8*hz)),12);
          else{const uint32_t h=hash(hx,hy,hz,seed^0xabcdu);u[0]=unit(h);u[1]=unit(h*0x2545f491u+1);u[2]=unit(h*0x9e3779b1u+7);}
          px=float(cx)+u[0];py=float(cy)+u[1];pz=float(cz)+u[2];}
        const float ex=px-x,ey=py-y,ez=pz-z;best=std::min(best,ex*ex+ey*ey+ez*ez);}}}
    return std::sqrt(best);}
inline float worley(unsigned P,uint32_t seed,float x,float y,float z,const uint8_t* features=nullptr){
    const float s=float(P)/float(N);x*=s;y*=s;z*=s;
    const int ix=int(std::floor(x)),iy=int(std::floor(y)),iz=int(std::floor(z));
    return worleyPrepared(P,seed,x,y,z,ix,iy,iz,features);}
}

// N*N*N bytes, deterministic, tileable (periodic in all axes): 3-octave Perlin FBM (4, 8, 16 cells over the tile) blended with an
// inverted Worley term (8 cells) for a billowy look, normalised min..max to 0..255.
inline std::vector<uint8_t> generate(uint32_t seed=Seed){
    // Reuse the output bytes as fixed-period gradient and feature tables until the final conversion; peak allocation is unchanged.
    std::vector<float> f(size_t(N)*N*N);std::vector<uint8_t> out(f.size());float lo=1e9f,hi=-1e9f;
#if defined(__SSE2__)
    static_assert(N%64==0,"Four-voxel groups stay inside every authored octave cube");
    __m128 minima=_mm_set1_ps(lo),maxima=_mm_set1_ps(hi);
#endif
    for(unsigned o=0,P=4;o<3;++o,P*=2)for(unsigned z=0;z<P;++z)for(unsigned y=0;y<P;++y)for(unsigned x=0;x<P;++x)out[(o==0?0:o==1?64:576)+x+P*(y+P*z)]=uint8_t(detail::hash(x,y,z,seed+o*0x9e37u)%12);
    static_assert(size_t(N)*N*N>=4672+14400+8*4672,"Noise tables fit the output scratch");
    detail::prepareWorleyFeatures(out.data()+4672,seed);
    detail::preparePerlinCubes(out.data());
    static_assert(N&&(N&(N-1))==0,"Noise-axis fractions are exact binary values");
    detail::PerlinAxis axis[3][N];for(unsigned o=0,P=4;o<3;++o,P*=2)for(unsigned i=0;i<N;++i){
        const unsigned scaled=i*P;const float fraction=float(scaled%N)*(1.f/float(N));
        axis[o][i]={int(scaled/N),fraction,detail::fade(fraction)};
    }
#if defined(__SSE2__)
    float perlinFour[4];
#endif
    for(unsigned z=0;z<N;++z)for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){
        // Octave 1 shares Worley's 8-cell grid; its scaled coordinates are exact binary fractions.
        const auto& wx=axis[1][x];const auto& wy=axis[1][y];const auto& wz=axis[1][z];
#if defined(__SSE2__)
        if((x&3)==0){__m128 values=_mm_setzero_ps();float a=1;unsigned P=4;
            for(unsigned o=0;o<3;++o,P*=2,a*=.5f)values=_mm_add_ps(values,_mm_mul_ps(_mm_set1_ps(a),detail::perlinFourPrepared(P,o,&axis[o][x],axis[o][y],axis[o][z],out.data()+19072)));
            _mm_storeu_ps(perlinFour,values);
        }
        const float p=perlinFour[x&3],sum=1.75f;
#else
        float p=0,a=1,sum=0;unsigned P=4;
        for(unsigned o=0;o<3;++o,P*=2,a*=.5f){p+=a*detail::perlinPrepared<true>(P,seed,o,axis[o][x],axis[o][y],axis[o][z],out.data()+19072);sum+=a;}
#endif
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
#if defined(__SSE2__)
    static_assert((size_t(N)*N*N)%4==0,"Four normalized noise bytes per store");
    const __m128 low=_mm_set1_ps(lo),scale=_mm_set1_ps(k),half=_mm_set1_ps(.5f),zero=_mm_setzero_ps(),cap=_mm_set1_ps(255.f);
    for(size_t i=0;i<f.size();i+=4){
        const __m128 value=_mm_add_ps(_mm_mul_ps(_mm_sub_ps(_mm_loadu_ps(f.data()+i),low),scale),half);
        const __m128i integer=_mm_cvttps_epi32(_mm_min_ps(_mm_max_ps(value,zero),cap));
        const __m128i words=_mm_packs_epi32(integer,integer),bytes=_mm_packus_epi16(words,words);
        const uint32_t packed=uint32_t(_mm_cvtsi128_si32(bytes));std::memcpy(out.data()+i,&packed,sizeof packed);
    }
#else
    for(size_t i=0;i<f.size();++i)out[i]=detail::normalizedByte((f[i]-lo)*k+.5f);
#endif
    return out;}

// trilinear with WRAP, matching D3D LINEAR+WRAP: texel coordinate u*N-.5; 0..1
template<bool QuantileInput=false> inline float sample(const uint8_t* volume,float u,float v,float w){
    const float c[3]={u,v,w};int i0[3],i1[3];float fr[3];
    for(int a=0;a<3;++a){
        // Quantile coordinates are finite and in [0, 5), so truncation gives the same floor.
        float base;bool finite=true;
        if constexpr(QuantileInput)base=float(int(c[a]));
        else if(std::fabs(c[a])<8388608.f){const int i=int(c[a]);base=float(i-(c[a]<float(i)));}
        else if(std::isfinite(c[a]))base=c[a]; // Every finite float at this magnitude is integral.
        else{base=std::floor(c[a]);finite=false;}
        const float x=c[a]-base;const float t=x*float(N)-.5f;const float fl=finite?float(int(t)-(t<0)):std::floor(t);
        fr[a]=t-fl;const int i=int(fl);i0[a]=((i%int(N))+int(N))%int(N);i1[a]=(i0[a]+1)%int(N);}
    auto at=[&](int x,int y,int z){return float(volume[x+N*(y+N*z)]);};
    auto l=[](float a,float b,float t){return a+(b-a)*t;};
    const float r=l(l(l(at(i0[0],i0[1],i0[2]),at(i1[0],i0[1],i0[2]),fr[0]),l(at(i0[0],i1[1],i0[2]),at(i1[0],i1[1],i0[2]),fr[0]),fr[1]),
                    l(l(at(i0[0],i0[1],i1[2]),at(i1[0],i0[1],i1[2]),fr[0]),l(at(i0[0],i1[1],i1[2]),at(i1[0],i1[1],i1[2]),fr[0]),fr[1]),fr[2]);
    return r*(1.f/255.f);}

struct Wind {
    double large[3]={},small[3]={}; /* accumulated wind offsets in world units, wrapped modulo the periods to stay small */
    double clock=0;                 /* seconds of advanced time (drives the slow veer) */
    float speed=0;                  /* current large-scale speed, units/s (for the log) */
    float dir[2]={1,0};             /* current large-scale direction (for the log) */
    void advance(float dt,float fog){
        const double d=std::isfinite(dt)?std::clamp(double(dt),0.0,0.1):0.0;
        const double f=std::isfinite(fog)?std::clamp(double(fog),0.0,1.0):0.0;
        const double sp=kWindDry+(kWindRain-kWindDry)*f,a=0.35+0.436*std::sin(2*3.14159265358979323846*clock/600.0); /* slow +-25 degree veer over ~10 min */
        const double sa=a+0.35,ss=sp*1.6;
#if defined(_WIN32) && defined(__SSE2__) && (defined(__clang__) || defined(__GNUC__))
        double ca,sa0,caSmall,saSmall;__builtin_sincos(a,&sa0,&ca);__builtin_sincos(sa,&saSmall,&caSmall);
        speed=float(sp);dir[0]=float(ca);dir[1]=float(sa0);
        large[0]+=sp*ca*d;large[1]+=sp*sa0*d;
        small[0]+=ss*caSmall*d;small[1]+=ss*saSmall*d;small[2]+=0.21*d;
#else
        const double ca=std::cos(a),sa0=std::sin(a);
        speed=float(sp);dir[0]=float(ca);dir[1]=float(sa0);
        large[0]+=sp*ca*d;large[1]+=sp*sa0*d;
        small[0]+=ss*std::cos(sa)*d;small[1]+=ss*std::sin(sa)*d;small[2]+=0.21*d;
#endif
        for(int i=0;i<3;++i){if(!(large[i]>=0&&large[i]<LargePeriod))large[i]=std::fmod(large[i],double(LargePeriod));if(large[i]<0)large[i]+=LargePeriod;
                             if(!(small[i]>=0&&small[i]<SmallPeriod))small[i]=std::fmod(small[i],double(SmallPeriod));if(small[i]<0)small[i]+=SmallPeriod;}
        clock+=d;}
};
// 257 quantiles of the mixed two-scale noise value (.65 nL + .35 nS at world points spread over the tiles as the GPU sees them: the small scale
// repeats 4x per large tile), so a coverage fraction maps to a threshold whatever the volume's value distribution is. q[i] = value at fraction i/256.
struct Quantiles{float q[257];};
inline Quantiles quantiles(const uint8_t* volume){
    constexpr unsigned count=8192;std::vector<float> v(count);
    for(unsigned i=0;i<count;++i){
        const float x=detail::unit(detail::hash(i,1,0,Seed)),y=detail::unit(detail::hash(i,2,0,Seed)),z=detail::unit(detail::hash(i,3,0,Seed));
        v[i]=.65f*sample<true>(volume,x,y,z)+.35f*sample<true>(volume,x*4+.37f,y*4+.61f,z*4+.13f);}
    std::sort(v.begin(),v.end());
    Quantiles t;for(unsigned i=0;i<=256;++i)t.q[i]=v[(size_t(i)*(count-1)+128)/256];
    return t;}
inline float quantile(const Quantiles& t,float x){
    const float f=std::clamp(x,0.f,1.f)*256.f;const unsigned i=std::min(unsigned(f),255u);const float k=f-float(i);return t.q[i]+(t.q[i+1]-t.q[i])*k;}
struct Frame {
    bool active=false;float coverage=0,height=0,sigmaMax=0,threshold=0,sharpness=0;
    float largeOrigin[3]={},smallOrigin[3]={};float invLarge=0,invSmall=0;
};
namespace detail {
inline float finite0(float x){return std::isfinite(x)?x:0.f;}
inline float origin(double cam,double wind,double inv){double v=(cam-wind)*inv;v-=std::floor(v);const float f=float(v);return f>=1.f?0.f:f;}
}
// fogClouds: setting 0/1; density: setting percent 0..200; fog: NorthlightWeatherEffects::Frame::fog (0 dry .. ~2); night: regional
// nightFactor 0..1; camera: world eye position. The shader computes noise coords as (p-camera)*inv+origin, so world-fixed noise moves with +wind.
// lush: 0..1, the camera's zone is a forest or grass zone (lushZone, smoothed by smoothDense). Without rain only lush zones get banks: the dry
// share of the coverage (kDry + kNight night) is scaled by max(lush, fog), so rain gives every zone its full coverage (game test: no dry banks
// in Tanaris or Durotar).
inline Frame derive(unsigned fogClouds,unsigned density,float fog,float night,const Wind& wind,const float camera[3],const Quantiles* table,float lush=1.f){
    Frame f;
    const float fg=std::clamp(detail::finite0(fog),0.f,1.f),ng=std::clamp(detail::finite0(night),0.f,1.f);
    const float lz=std::clamp(std::isfinite(lush)?lush:1.f,0.f,1.f);
    const float cov=std::clamp((float(std::min(density,100000u))/100.f)*((kDry+kNight*ng)*std::max(lz,fg)+(1-kDry)*fg),0.f,kMaxCoverage);
    if(!fogClouds||!table||!(cov>=kMinCoverage))return f;
    f.active=true;f.coverage=cov;f.threshold=quantile(*table,1-cov);
    f.sharpness=1.f/std::max(quantile(*table,1-kDense*cov)-f.threshold,.02f);f.height=kBaseHeight+kRainHeight*fg;f.sigmaMax=kSigmaMax*(kDrySigma+(kRainSigma-kDrySigma)*fg)*(1-kDryNightThin*ng*(1-fg));
    f.invLarge=1.f/LargePeriod;f.invSmall=1.f/SmallPeriod;
    for(int i=0;i<3;++i){const double c=detail::finite0(camera[i]);
        f.largeOrigin[i]=detail::origin(c,wind.large[i],1.0/double(LargePeriod));f.smallOrigin[i]=detail::origin(c,wind.small[i],1.0/double(SmallPeriod));}
    return f;}
// 0.3.199 (fog clouds, optimisation): the FogClouds pass constants, c59..c63 (rows 0..4), host-folded so the shader saves slots for its
// early outs. Only the components below are written (the rest stay as the bank has them): c59.y -2.5 height; c60.yzw / c61.yzw the large /
// small origins; c61.x the large noise below which a sample can hold no bank (the bank top is 0 below nL .3125, the density is 0 while
// .65 nL + .35 <= threshold); c62.x the tallest bank top 5.5 height; c62.y -threshold x sharpness; c62.z 8 height; c62.w sigmaMax;
// c63.x .35 sharpness; c63.y/z the inverse periods; c63.w .65 sharpness. The shader's density is then
// saturate(nL c63.w + nS c63.x + c62.y) and its bank top nL c62.z + c59.y: sigma() below, folded.
inline void shaderConstants(const Frame& f,float c[5][4]){
    c[0][1]=-2.5f*f.height;
    for(int i=0;i<3;++i){c[1][1+i]=f.largeOrigin[i];c[2][1+i]=f.smallOrigin[i];}
    c[2][0]=std::max(.3125f,(f.threshold-.35f)/.65f);
    c[3][0]=5.5f*f.height;c[3][1]=-f.threshold*f.sharpness;c[3][2]=8.f*f.height;c[3][3]=f.sigmaMax;
    c[4][0]=.35f*f.sharpness;c[4][1]=f.invLarge;c[4][2]=f.invSmall;c[4][3]=.65f*f.sharpness;}
// 0.3.199 (fog clouds): the banks' environment colour, uploaded as c26 (with c25.w=1) for the cloud pass only: the game's validated fog
// colour. At night (nightFactor 1), and by day in the storm bands and some zones, it is near black, and the banks read as dark smears or black balls
// (game tests); a neutral grey floor fixed that but read as dust or dirt against a blue night. So the game colour keeps its hue and is
// scaled up to a luminance of at least kDayLum by day and kNightLum at night (at most kNightBoost times); only without a usable game colour
// (not validated, or black) does the neutral kNightGrey hue apply, at the same luminance. The shader still takes the brighter of this and its own ambient*.35 air radiance.
constexpr float kNightGrey[3]={.15f,.16f,.18f};
constexpr float kNightLum=.24f,kNightBoost=10.f; /* game test: .16 / 6 still read dark next to the moonlit, Northlight-lit ground at night */
constexpr float kSaturation=.5f; /* the game hue's saturation kept (luminance unchanged): the boosted night-rain colour read too blue in Elwynn (game test) */
constexpr float kDayLum=.30f; /* the same floor by day (game test: the storm bands' and some zones' daytime fog colours are near black, the dense cores read as black balls) */
inline void colour(const float game[3],bool gameValid,float night,float out[4]){
    const float n=std::isfinite(night)?std::clamp(night,0.f,1.f):0.f;
    float g[3];for(int i=0;i<3;++i)g[i]=gameValid&&std::isfinite(game[i])?std::max(game[i],0.f):0.f;
    const float lum=.2126f*g[0]+.7152f*g[1]+.0722f*g[2],target=kDayLum+(kNightLum-kDayLum)*n;
    if(lum>1e-4f){const float scale=lum<target?std::min(target/lum,kNightBoost):1.f,l=lum*scale;for(int i=0;i<3;++i)out[i]=l+(g[i]*scale-l)*kSaturation;}
    else{const float grey=.2126f*kNightGrey[0]+.7152f*kNightGrey[1]+.0722f*kNightGrey[2];for(int i=0;i<3;++i)out[i]=kNightGrey[i]*target/grey;}
    out[3]=0;}
// 0.3.199 (fog clouds): dense-zone damping (game test: Duskwood in night rain was all fog). profile: the regional field's dense-zone tag at
// the camera (0 ordinary air, 1 Duskwood), smoothed by smoothDense over kDenseSeconds. The rain's extra air extinction and the cloud density
// setting are multiplied by denseZoneDamp: 1 - kDenseDamp at full profile, exactly 1 outside dense zones.
constexpr float kDenseDamp=0.6f,kDenseSeconds=3.f;
inline float denseZoneDamp(float profile){return std::isfinite(profile)?1-kDenseDamp*std::clamp(profile,0.f,1.f):1.f;}
namespace detail {
inline float denseWeight(float dt){const float d=std::isfinite(dt)?std::clamp(dt,0.f,.1f):0.f;return 1-std::exp(-d/kDenseSeconds);}
inline float denseBlend(float current,float target,float weight){
    if(!std::isfinite(current))current=0;if(!std::isfinite(target))target=0;
    return current+(target-current)*weight;
}
}
inline float smoothDense(float current,float target,float dt){return detail::denseBlend(current,target,detail::denseWeight(dt));}
inline void smoothDensePair(float& first,float firstTarget,float& second,float secondTarget,float dt){
    const float weight=detail::denseWeight(dt);
    first=detail::denseBlend(first,firstTarget,weight);second=detail::denseBlend(second,secondTarget,weight);
}
// the per-sample density, identical to the shader: nL, nS noise values 0..1; tag = regional field .w (0 indoors/unknown)
inline float sigma(float nL,float nS,float altitude,float threshold,float sharpness,float height,float tag,float sigmaMax){
    auto sat=[](float x){return std::clamp(x,0.f,1.f);};
    const float n=.65f*nL+.35f*nS,c=sat((n-threshold)*sharpness);
    float v=sat(1-altitude/std::max(height*(8.f*nL-2.5f),.001f));v*=v; /* the bank top follows the large noise, as in the shader (0 below nL .31, 5.5x at 1) */
    const float zone=1+(.7f-1)*sat((tag-1.25f)/3.75f);
    return altitude>=0&&tag>0?c*v*zone*sigmaMax:0.f;}
// CPU mirror of one GPU sample at world point p (ground = regional field ground height at p, tag = field .w, fieldCoverage = 0..1 valid weight)
inline float sigmaAt(const uint8_t* volume,const Frame& f,const float camera[3],const float p[3],float ground,float tag,float fieldCoverage){
    if(!f.active)return 0.f;
    if(!(p[2]-ground>=0&&tag>0))return 0.f*fieldCoverage;
    float l[3],s[3];
    for(int i=0;i<3;++i){const float d=p[i]-camera[i];l[i]=d*f.invLarge+f.largeOrigin[i];s[i]=d*f.invSmall+f.smallOrigin[i];}
    const float nL=sample(volume,l[0],l[1],l[2]),nS=sample(volume,s[0],s[1],s[2]);
    return sigma(nL,nS,p[2]-ground,f.threshold,f.sharpness,f.height,tag,f.sigmaMax)*fieldCoverage;}
}
