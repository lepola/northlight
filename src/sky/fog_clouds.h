#pragma once
// 0.3.199 (fog clouds): the CPU side of the moving low fog banks. The GPU pass (a half-resolution raymarch) samples a tileable 3D
// noise volume (N^3 L8) at two world-space scales offset by the wind and weights the result near the ground; this header makes the
// volume, advances the wind, derives the per-frame constants from the settings and the weather, and mirrors one GPU sample
// (sigma/sigmaAt) so the shader maths is testable. Pure: no D3D, no game data, no platform-dependent functions.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace NorthlightFogClouds {
constexpr unsigned N=64;                            /* noise volume edge, L8 voxels, index x+N*(y+N*z) */
constexpr float LargePeriod=192.f,SmallPeriod=48.f; /* world units per noise tile (0.3.199 game tests tried 384/96..576/144; back to the original by choice) */
constexpr uint32_t Seed=0x4e4c4643u;
constexpr float kDry=0.05f,kNight=0.18f,kSigmaMax=0.03f,kBaseHeight=12,kRainHeight=8; /* bank height 12 units dry, 20 in full rain (0.3.199 game test: was 7/14, the banks should reach higher) */
constexpr float kDrySigma=0.33f; /* peak extinction share without rain: sigmaMax = kSigmaMax x (kDrySigma + (1-kDrySigma) fog) (game tests: dry banks fainter, .65 -> .45 -> .33) */
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
// periodic gradient noise, P cells per tile, evaluated at voxel (x,y,z); about -1..1
inline float perlin(unsigned P,uint32_t seed,unsigned oct,float x,float y,float z){
    static const int G[12][3]={{1,1,0},{-1,1,0},{1,-1,0},{-1,-1,0},{1,0,1},{-1,0,1},{1,0,-1},{-1,0,-1},{0,1,1},{0,-1,1},{0,1,-1},{0,-1,-1}};
    const float s=float(P)/float(N);x*=s;y*=s;z*=s;
    const int ix=int(std::floor(x)),iy=int(std::floor(y)),iz=int(std::floor(z));
    const float fx=x-float(ix),fy=y-float(iy),fz=z-float(iz);
    float v[8];
    for(int k=0;k<8;++k){
        const int dx=k&1,dy=(k>>1)&1,dz=(k>>2)&1;
        const uint32_t cx=uint32_t(ix+dx)%P,cy=uint32_t(iy+dy)%P,cz=uint32_t(iz+dz)%P;
        const int* g=G[hash(cx,cy,cz,seed+oct*0x9e37u)%12];
        v[k]=float(g[0])*(fx-float(dx))+float(g[1])*(fy-float(dy))+float(g[2])*(fz-float(dz));}
    const float u=fade(fx),vv=fade(fy),w=fade(fz);
    return lerp(lerp(lerp(v[0],v[1],u),lerp(v[2],v[3],u),vv),lerp(lerp(v[4],v[5],u),lerp(v[6],v[7],u),vv),w);}
// periodic cellular F1 distance in cell units, P cells per tile
inline float worley(unsigned P,uint32_t seed,float x,float y,float z){
    const float s=float(P)/float(N);x*=s;y*=s;z*=s;
    const int ix=int(std::floor(x)),iy=int(std::floor(y)),iz=int(std::floor(z));
    float best=4.f;
    for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
        const int cx=ix+dx,cy=iy+dy,cz=iz+dz;
        const uint32_t hx=uint32_t((cx%int(P)+int(P))%int(P)),hy=uint32_t((cy%int(P)+int(P))%int(P)),hz=uint32_t((cz%int(P)+int(P))%int(P));
        const uint32_t h=hash(hx,hy,hz,seed^0xabcdu);
        const float px=float(cx)+unit(h),py=float(cy)+unit(h*0x2545f491u+1),pz=float(cz)+unit(h*0x9e3779b1u+7);
        const float ex=px-x,ey=py-y,ez=pz-z;best=std::min(best,ex*ex+ey*ey+ez*ez);}
    return std::sqrt(best);}
}

// N*N*N bytes, deterministic, tileable (periodic in all axes): 3-octave Perlin FBM (4, 8, 16 cells over the tile) blended with an
// inverted Worley term (8 cells) for a billowy look, normalised min..max to 0..255.
inline std::vector<uint8_t> generate(uint32_t seed=Seed){
    std::vector<float> f(size_t(N)*N*N);float lo=1e9f,hi=-1e9f;
    for(unsigned z=0;z<N;++z)for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){
        const float fx=float(x),fy=float(y),fz=float(z);
        float p=0,a=1,sum=0;unsigned P=4;
        for(unsigned o=0;o<3;++o,P*=2,a*=.5f){p+=a*detail::perlin(P,seed,o,fx,fy,fz);sum+=a;}
        const float pn=std::clamp(.5f+.5f*(p/sum)*1.4f,0.f,1.f);
        const float w=1.f-std::min(detail::worley(8,seed,fx,fy,fz),1.f);
        const float v=.6f*pn+.4f*w;
        f[x+N*(y+size_t(N)*z)]=v;lo=std::min(lo,v);hi=std::max(hi,v);}
    std::vector<uint8_t> out(f.size());const float k=hi>lo?255.f/(hi-lo):0.f;
    for(size_t i=0;i<f.size();++i)out[i]=uint8_t(std::clamp(std::floor((f[i]-lo)*k+.5f),0.f,255.f));
    return out;}

// trilinear with WRAP, matching D3D LINEAR+WRAP: texel coordinate u*N-.5; 0..1
inline float sample(const uint8_t* volume,float u,float v,float w){
    const float c[3]={u,v,w};int i0[3],i1[3];float fr[3];
    for(int a=0;a<3;++a){
        const float x=c[a]-std::floor(c[a]);const float t=x*float(N)-.5f;const float fl=std::floor(t);
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
        speed=float(sp);dir[0]=float(std::cos(a));dir[1]=float(std::sin(a));
        large[0]+=sp*std::cos(a)*d;large[1]+=sp*std::sin(a)*d;
        small[0]+=ss*std::cos(sa)*d;small[1]+=ss*std::sin(sa)*d;small[2]+=0.21*d;
        for(int i=0;i<3;++i){large[i]=std::fmod(large[i],double(LargePeriod));if(large[i]<0)large[i]+=LargePeriod;
                             small[i]=std::fmod(small[i],double(SmallPeriod));if(small[i]<0)small[i]+=SmallPeriod;}
        clock+=d;}
};
// 257 quantiles of the mixed two-scale noise value (.65 nL + .35 nS at world points spread over the tiles as the GPU sees them: the small scale
// repeats 4x per large tile), so a coverage fraction maps to a threshold whatever the volume's value distribution is. q[i] = value at fraction i/256.
struct Quantiles{float q[257];};
inline Quantiles quantiles(const uint8_t* volume){
    constexpr unsigned count=8192;std::vector<float> v(count);
    for(unsigned i=0;i<count;++i){
        const float x=detail::unit(detail::hash(i,1,0,Seed)),y=detail::unit(detail::hash(i,2,0,Seed)),z=detail::unit(detail::hash(i,3,0,Seed));
        v[i]=.65f*sample(volume,x,y,z)+.35f*sample(volume,x*4+.37f,y*4+.61f,z*4+.13f);}
    std::sort(v.begin(),v.end());
    Quantiles t;for(unsigned i=0;i<=256;++i)t.q[i]=v[size_t(std::lround(double(i)/256.0*double(count-1)))];
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
    f.sharpness=1.f/std::max(quantile(*table,1-kDense*cov)-f.threshold,.02f);f.height=kBaseHeight+kRainHeight*fg;f.sigmaMax=kSigmaMax*(kDrySigma+(1-kDrySigma)*fg)*(1-kDryNightThin*ng*(1-fg));
    f.invLarge=1.f/LargePeriod;f.invSmall=1.f/SmallPeriod;
    for(int i=0;i<3;++i){const double c=detail::finite0(camera[i]);
        f.largeOrigin[i]=detail::origin(c,wind.large[i],1.0/double(LargePeriod));f.smallOrigin[i]=detail::origin(c,wind.small[i],1.0/double(SmallPeriod));}
    return f;}
// 0.3.199 (fog clouds): the banks' environment colour, uploaded as c26 (with c25.w=1) for the cloud pass only: the game's validated fog
// colour. At night (nightFactor 1), and by day in the storm bands and some zones, it is near black, and the banks read as dark smears or black balls
// (game tests); a neutral grey floor fixed that but read as dust or dirt against a blue night. So the game colour keeps its hue and is
// scaled up to a luminance of at least kDayLum by day and kNightLum at night (at most kNightBoost times); only without a usable game colour
// (not validated, or black) does the neutral kNightGrey hue apply, at the same luminance. The shader still takes the brighter of this and its own ambient*.35 air radiance.
constexpr float kNightGrey[3]={.15f,.16f,.18f};
constexpr float kNightLum=.24f,kNightBoost=10.f; /* game test: .16 / 6 still read dark next to the moonlit, Northlight-lit ground at night */
constexpr float kDayLum=.30f; /* the same floor by day (game test: the storm bands' and some zones' daytime fog colours are near black, the dense cores read as black balls) */
inline void colour(const float game[3],bool gameValid,float night,float out[4]){
    const float n=std::isfinite(night)?std::clamp(night,0.f,1.f):0.f;
    float g[3];for(int i=0;i<3;++i)g[i]=gameValid&&std::isfinite(game[i])?std::max(game[i],0.f):0.f;
    const float lum=.2126f*g[0]+.7152f*g[1]+.0722f*g[2],target=kDayLum+(kNightLum-kDayLum)*n;
    if(lum>1e-4f){const float scale=lum<target?std::min(target/lum,kNightBoost):1.f;for(int i=0;i<3;++i)out[i]=g[i]*scale;}
    else{const float grey=.2126f*kNightGrey[0]+.7152f*kNightGrey[1]+.0722f*kNightGrey[2];for(int i=0;i<3;++i)out[i]=kNightGrey[i]*target/grey;}
    out[3]=0;}
// 0.3.199 (fog clouds): dense-zone damping (game test: Duskwood in night rain was all fog). profile: the regional field's dense-zone tag at
// the camera (0 ordinary air, 1 Duskwood), smoothed by smoothDense over kDenseSeconds. The rain's extra air extinction and the cloud density
// setting are multiplied by denseZoneDamp: 1 - kDenseDamp at full profile, exactly 1 outside dense zones.
constexpr float kDenseDamp=0.6f,kDenseSeconds=3.f;
inline float denseZoneDamp(float profile){return std::isfinite(profile)?1-kDenseDamp*std::clamp(profile,0.f,1.f):1.f;}
inline float smoothDense(float current,float target,float dt){
    if(!std::isfinite(current))current=0;if(!std::isfinite(target))target=0;
    const float d=std::isfinite(dt)?std::clamp(dt,0.f,.1f):0.f;return current+(target-current)*(1-std::exp(-d/kDenseSeconds));}
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
    float l[3],s[3];
    for(int i=0;i<3;++i){const float d=p[i]-camera[i];l[i]=d*f.invLarge+f.largeOrigin[i];s[i]=d*f.invSmall+f.smallOrigin[i];}
    const float nL=sample(volume,l[0],l[1],l[2]),nS=sample(volume,s[0],s[1],s[2]);
    return sigma(nL,nS,p[2]-ground,f.threshold,f.sharpness,f.height,tag,f.sigmaMax)*fieldCoverage;}
}
