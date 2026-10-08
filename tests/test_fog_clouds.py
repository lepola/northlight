#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.199 (fog clouds): NorthlightFogClouds (src/sky/fog_clouds.h), the real header compiled natively.
The 64^3 noise volume (deterministic, wide range, tileable), the wrapping trilinear sample, the shader's sigma(), derive() (off states,
coverage dry < night < rain, origins in [0,1)), the Wind integrator (dt clamp, no jump on a fog change, wrapped offsets) and the sigmaAt mirror.
Native clang++, plain -O2 and ASan/UBSan. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile

SRC=r'''
#include "fog_clouds.h"
#include <cassert>
#include <cstdio>
#include <limits>
namespace FC=NorthlightFogClouds;
static bool near(float a,float b,float e=1e-5f){return std::fabs(a-b)<=e;}
int main(){
    const unsigned N=FC::N;
    {   // dense zones: damping 1 outside, 1-kDenseDamp in Duskwood; the smoothing moves toward the target, clamps dt, never overshoots
        assert(FC::denseZoneDamp(0)==1&&near(FC::denseZoneDamp(1),1-FC::kDenseDamp)&&FC::denseZoneDamp(std::numeric_limits<float>::quiet_NaN())==1);
        float z=0;for(int i=0;i<300;++i)z=FC::smoothDense(z,1,.1f);assert(z>.99f&&z<=1);
        assert(FC::smoothDense(0,1,5)==FC::smoothDense(0,1,.1f)&&FC::smoothDense(.5f,.5f,.1f)==.5f&&FC::smoothDense(0,1,0)==0);
    }
    {   // colour: the game fog colour's hue scaled up to a luminance floor (kDayLum by day, kNightLum at night; at most kNightBoost x); bright colours unchanged;
        // no usable game colour -> the kNightGrey hue at that floor
        const auto L=[](const float* o){return .2126f*o[0]+.7152f*o[1]+.0722f*o[2];};
        const float game[3]={.3f,.05f,.4f};float o[4];
        /* 0.3.199: kSaturation of the hue kept around the luminance: channel = l + (scaled - l) * kSaturation */
        const auto sat=[&](const float* g,float target,int i){const float lum=L(g),sc=std::min(target/lum,FC::kNightBoost);return lum*sc+(g[i]*sc-lum*sc)*FC::kSaturation;};
        FC::colour(game,true,0,o);assert(near(L(o),FC::kDayLum,1e-4f)&&near(o[0],sat(game,FC::kDayLum,0),1e-4f)&&near(o[2],sat(game,FC::kDayLum,2),1e-4f)&&o[0]<o[2]&&o[3]==0); /* dark day colour raised, hue kept at half saturation */
        FC::colour(game,true,1,o);assert(near(L(o),FC::kNightLum,1e-4f)&&near(o[0],sat(game,FC::kNightLum,0),1e-4f)&&near(o[2],sat(game,FC::kNightLum,2),1e-4f));
        const float blue[3]={.01f,.02f,.06f};FC::colour(blue,true,1,o);{const float l=L(blue)*FC::kNightBoost;assert(near(o[2],l+(.06f*FC::kNightBoost-l)*FC::kSaturation)&&o[2]>o[0]);} /* capped boost, still blue, half as saturated */
        const float bright[3]={.5f,.5f,.5f};FC::colour(bright,true,0,o);assert(o[0]==.5f);FC::colour(bright,true,1,o);assert(o[0]==.5f); /* already bright: unchanged */
        FC::colour(game,false,0,o);assert(near(L(o),FC::kDayLum,1e-4f)&&o[2]>o[0]); /* never black: grey-blue at the floor */
        FC::colour(game,false,1,o);assert(near(L(o),FC::kNightLum,1e-4f));
        const float bad[3]={std::numeric_limits<float>::quiet_NaN(),-1,.2f};FC::colour(bad,true,std::numeric_limits<float>::quiet_NaN(),o);{const float l=.0722f*.2f*FC::kNightBoost;assert(near(o[0],l*(1-FC::kSaturation))&&near(o[1],l*(1-FC::kSaturation))&&near(o[2],l+(.2f*FC::kNightBoost-l)*FC::kSaturation));} /* non-finite and negative channels count as 0; the boost is capped */
    }
    const auto vol=FC::generate();
    {   // generate: size, determinism, range, mean
        assert(vol.size()==size_t(N)*N*N&&vol==FC::generate()&&vol!=FC::generate(1234u));
        unsigned lo=255,hi=0;double sum=0;for(auto b:vol){lo=std::min<unsigned>(lo,b);hi=std::max<unsigned>(hi,b);sum+=b;}
        const double mean=sum/double(vol.size())/255.0;
        assert(lo==0&&hi==255&&mean>0.3&&mean<0.7);
    }
    {   // tileable: the wrap seam (N-1 -> 0) differs like interior neighbours, per axis
        for(int axis=0;axis<3;++axis){
            double seam=0,inner=0;unsigned maxSeam=0,maxInner=0;const size_t stride[3]={1,N,size_t(N)*N};
            for(unsigned a=0;a<N;++a)for(unsigned b=0;b<N;++b){
                const size_t base=axis==0?size_t(N)*(a+size_t(N)*b):axis==1?a+size_t(N)*N*b:a+size_t(N)*b;
                for(unsigned i=0;i<N;++i){
                    const size_t i0=base+stride[axis]*i,i1=base+stride[axis]*((i+1)%N);
                    const unsigned d=unsigned(std::abs(int(vol[i0])-int(vol[i1])));
                    if(i==N-1){seam+=d;maxSeam=std::max(maxSeam,d);}else{inner+=d;maxInner=std::max(maxInner,d);}}}
            seam/=double(N)*N;inner/=double(N)*N*(N-1);
            assert(seam<inner*1.5+1&&maxSeam<=maxInner*2+8);
        }
    }
    {   // sample: texel centres are exact, wrapping on every axis, range 0..1
        for(unsigned t:{0u,1u,17u,63u}){const float c=(float(t)+.5f)/float(N);assert(near(FC::sample(vol.data(),c,c,c),float(vol[t+N*(t+N*t)])/255.f));}
        const float a=0.1234f,b=0.5f,c=0.9f;const float s=FC::sample(vol.data(),a,b,c);
        assert(near(s,FC::sample(vol.data(),a+1,b,c),1e-4f)&&near(s,FC::sample(vol.data(),a-3,b,c),1e-4f)&&near(s,FC::sample(vol.data(),a,b+1,c-2),1e-4f)&&near(s,FC::sample(vol.data(),a,b,c+5),1e-4f));
        const float mid=FC::sample(vol.data(),1.f/float(N),.5f/float(N),.5f/float(N)); /* halfway between texels 0 and 1 */
        assert(near(mid,(float(vol[0])+float(vol[1]))*.5f/255.f,1e-5f));
        for(int i=0;i<200;++i){const float v=FC::sample(vol.data(),float(i)*.137f,float(i)*.071f,float(i)*.29f);assert(v>=0&&v<=1);}
        const float seam0=FC::sample(vol.data(),0.f,.5f/float(N),.5f/float(N)); /* between texel N-1 and 0 */
        assert(near(seam0,(float(vol[N-1])+float(vol[0]))*.5f/255.f,1e-5f));
    }
    {   // sigma
        for(float n:{0.f,.4f,1.f})assert(FC::sigma(n,n,2,1,3,10,1,.03f)==0.f);
        assert(FC::sigma(1,1,-.01f,0,3,10,1,.03f)==0.f&&FC::sigma(1,1,2,0,3,10,0,.03f)==0.f&&FC::sigma(1,1,2,0,3,10,-1,.03f)==0.f);
        float prev=-1;for(int i=0;i<=20;++i){const float s=FC::sigma(.6f,.5f,2,1-float(i)/20,3,10,1,.03f);assert(s>=prev);prev=s;}
        assert(prev>0);
        assert(near(FC::sigma(1,1,0,0,3,10,1,.03f),.03f)&&FC::sigma(1,1,55.1f,0,3,10,1,.03f)==0.f&&FC::sigma(1,1,54,0,3,10,1,.03f)>0.f&&FC::sigma(.4f,1,7.1f,0,3,10,1,.03f)==0.f&&FC::sigma(.4f,1,6.9f,0,3,10,1,.03f)>0.f&&FC::sigma(.3f,1,.01f,0,3,10,1,.03f)==0.f);
        const float t1=FC::sigma(1,1,1,0,3,10,1,.03f),t5=FC::sigma(1,1,1,0,3,10,5,.03f);assert(near(t5,.7f*t1,1e-7f)&&t1>0);
        assert(FC::sigma(1,1,1,0,3,10,9,.03f)==t5); /* zone saturates */
    }
    const float cam[3]={1234.5f,-987.25f,40.f};FC::Wind w0;
    const auto Q=FC::quantiles(vol.data());
    {   // quantiles: monotonic, within [0,1], deterministic, the table interpolates
        for(unsigned i=0;i<=256;++i){assert(Q.q[i]>=0&&Q.q[i]<=1);if(i)assert(Q.q[i]>=Q.q[i-1]);}
        assert(Q.q[256]>Q.q[0]+.3f);const auto Q2=FC::quantiles(vol.data());for(unsigned i=0;i<=256;++i)assert(Q.q[i]==Q2.q[i]);
        assert(near(FC::quantile(Q,0),Q.q[0])&&near(FC::quantile(Q,1),Q.q[256])&&near(FC::quantile(Q,.5f),Q.q[128])&&near(FC::quantile(Q,-3),Q.q[0])&&near(FC::quantile(Q,9),Q.q[256]));
        assert(near(FC::quantile(Q,.5f+.5f/256),(Q.q[128]+Q.q[129])*.5f,1e-6f));
    }
    {   // derive
        for(auto f:{FC::derive(0,100,1,1,w0,cam,&Q),FC::derive(1,0,1,1,w0,cam,&Q),FC::derive(1,100,1,1,w0,cam,nullptr)}){
            assert(!f.active&&f.coverage==0&&f.height==0&&f.sigmaMax==0&&f.invLarge==0&&f.invSmall==0&&f.threshold==0&&f.sharpness==0);
            for(int i=0;i<3;++i)assert(f.largeOrigin[i]==0&&f.smallOrigin[i]==0);}
        const float nan=std::numeric_limits<float>::quiet_NaN();
        const auto dry=FC::derive(1,100,0,0,w0,cam,&Q),night=FC::derive(1,100,0,1,w0,cam,&Q),rain=FC::derive(1,100,1,0,w0,cam,&Q);
        assert(dry.active&&near(dry.coverage,.05f)&&near(night.coverage,.23f)&&near(rain.coverage,FC::kMaxCoverage));
        assert(dry.coverage<night.coverage&&night.coverage<rain.coverage&&dry.threshold>night.threshold&&night.threshold>rain.threshold&&dry.sharpness>0);
        assert(near(dry.threshold,FC::quantile(Q,.95f))&&dry.sharpness<=50.f+1e-3f);
        assert(near(dry.height,FC::kBaseHeight)&&near(rain.height,FC::kBaseHeight+FC::kRainHeight)&&near(dry.sigmaMax,FC::kSigmaMax*FC::kDrySigma)&&near(rain.sigmaMax,FC::kSigmaMax)&&near(dry.invLarge,1.f/FC::LargePeriod)&&near(dry.invSmall,1.f/FC::SmallPeriod));
        assert(near(FC::derive(1,200,0,0,w0,cam,&Q).coverage,.10f)&&near(FC::derive(1,50,0,0,w0,cam,&Q).coverage,.025f)&&near(FC::derive(1,200,1,1,w0,cam,&Q).coverage,FC::kMaxCoverage));
        assert(!FC::derive(1,10,0,0,w0,cam,&Q).active); /* .005 < kMinCoverage: the pass is skipped */
        // 0.3.199 (fog clouds): lush zones; dry thinner at night
        assert(!FC::derive(1,100,0,0,w0,cam,&Q,0.f).active&&!FC::derive(1,100,0,1,w0,cam,&Q,0.f).active); /* dry, not lush: no banks */
        assert(near(FC::derive(1,100,0,1,w0,cam,&Q,.5f).coverage,.115f)&&near(FC::derive(1,100,0,1,w0,cam,&Q,1.f).coverage,.23f)); /* the dry share scales with lush */
        assert(near(FC::derive(1,100,1,0,w0,cam,&Q,0.f).coverage,rain.coverage)&&near(FC::derive(1,100,.5f,0,w0,cam,&Q,0.f).coverage,.5f)&&near(FC::derive(1,100,.5f,0,w0,cam,&Q,1.f).coverage,.525f)); /* full rain: every zone its full coverage; half rain: the dry share x fog */
        assert(near(FC::derive(1,100,0,0,w0,cam,&Q,nan).coverage,dry.coverage)); /* nan lush: lush */
        assert(near(night.sigmaMax,FC::kSigmaMax*FC::kDrySigma*(1-FC::kDryNightThin))&&night.sigmaMax<dry.sigmaMax&&near(FC::derive(1,100,1,1,w0,cam,&Q).sigmaMax,FC::kSigmaMax)); /* dry nights thinner; rain unchanged */
        const auto bad=FC::derive(1,100,nan,nan,w0,cam,&Q);assert(bad.active&&near(bad.coverage,.05f)&&near(bad.height,FC::kBaseHeight));
        FC::Wind w;for(int i=0;i<500;++i)w.advance(.05f,.5f);
        const float far[3]={-1e5f,3.3e5f,-17.f};
        for(const float* c:{cam,far}){const auto f=FC::derive(1,100,.3f,.2f,w,c,&Q);for(int i=0;i<3;++i)assert(f.largeOrigin[i]>=0&&f.largeOrigin[i]<1&&f.smallOrigin[i]>=0&&f.smallOrigin[i]<1);}
    }
    {   // coverage: the fraction of world points with density>0 follows the setting (default wind, random points over 4 large tiles); the fully dense share ~ kDense*coverage
        struct Case{float fog,night;float want;};
        for(const Case& k:{Case{0,0,.05f},Case{0,1,.23f},Case{1,0,FC::kMaxCoverage},Case{.5f,.2f,std::min(.5f*.95f+.05f+.036f,FC::kMaxCoverage)}}){
            const auto f=FC::derive(1,100,k.fog,k.night,w0,cam,&Q);assert(f.active);
            unsigned any=0,dense=0,total=20000;uint32_t h=12345;
            auto rnd=[&]{h=h*1664525u+1013904223u;return float(h>>8)*(1.f/16777216.f);};
            for(unsigned i=0;i<total;++i){
                const float p[3]={rnd()*768.f,rnd()*768.f,rnd()*768.f};float l[3],s[3];
                for(int a=0;a<3;++a){l[a]=p[a]*f.invLarge+f.largeOrigin[a];s[a]=p[a]*f.invSmall+f.smallOrigin[a];}
                const float n=.65f*FC::sample(vol.data(),l[0],l[1],l[2])+.35f*FC::sample(vol.data(),s[0],s[1],s[2]);
                const float c=std::clamp((n-f.threshold)*f.sharpness,0.f,1.f);if(c>0)++any;if(c>=.9999f)++dense;}
            const float cover=float(any)/float(total),full=float(dense)/float(total);
            std::printf("coverage fog=%.2f night=%.2f set=%.3f measured=%.3f dense=%.3f (want ~%.3f)\n",k.fog,k.night,f.coverage,cover,full,FC::kDense*f.coverage);
            assert(std::fabs(cover-f.coverage)<.05f&&std::fabs(full-FC::kDense*f.coverage)<.05f);}
    }
    {   // wind: dt clamp, speeds, no jump on a fog change, offsets stay in range
        FC::Wind a,b;a.advance(5,0.5f);b.advance(.1f,0.5f);
        for(int i=0;i<3;++i)assert(a.large[i]==b.large[i]&&a.small[i]==b.small[i]);assert(a.clock==b.clock);
        FC::Wind c;c.advance(-1,.5f);assert(c.clock==0&&c.large[0]==0);
        c.advance(std::numeric_limits<float>::quiet_NaN(),.5f);assert(c.clock==0);
        FC::Wind d;d.advance(.1f,std::numeric_limits<float>::infinity());assert(near(d.speed,float(FC::kWindDry)));
        FC::Wind e;e.advance(.1f,0);assert(near(e.speed,float(FC::kWindDry))&&near(e.dir[0],std::cos(.35f))&&near(e.dir[1],std::sin(.35f),1e-5f));
        FC::Wind r;r.advance(.1f,3);assert(near(r.speed,float(FC::kWindRain)));
        FC::Wind j;j.advance(.1f,0);const double before[2]={j.large[0],j.large[1]};j.advance(.1f,1);
        assert(std::hypot(j.large[0]-before[0],j.large[1]-before[1])<=FC::kWindRain*.1+1e-9); /* one frame moves at most speed*dt */
        FC::Wind m;for(int i=0;i<200000;++i){m.advance(.1f,i%7<3?1.f:0.f);
            for(int k=0;k<3;++k)assert(m.large[k]>=0&&m.large[k]<FC::LargePeriod&&m.small[k]>=0&&m.small[k]<FC::SmallPeriod);}
        assert(near(float(m.clock),20000.f,1.f));
        FC::Wind z;z.advance(.1f,0);assert(z.small[2]>0); /* small scale drifts up a little */
    }
    {   // sigmaAt mirrors sigma(sample(..)) built by hand
        FC::Wind w;for(int i=0;i<300;++i)w.advance(.1f,.4f);
        const auto f=FC::derive(1,150,.6f,.1f,w,cam,&Q);assert(f.active);
        const float zero[3]={0,0,0};assert(FC::sigmaAt(vol.data(),FC::Frame{},cam,zero,0,1,1)==0.f);
        unsigned nonzero=0;
        for(int i=0;i<400;++i){
            const float p[3]={cam[0]+float(i%20-10)*7.f,cam[1]+float(i/20-10)*5.f,cam[2]+float(i%9)*.8f-1.f};
            const float ground=cam[2]-2.f,tag=float(i%6),fc=float(i%5)/4;
            float l[3],s[3];for(int k=0;k<3;++k){l[k]=(p[k]-cam[k])*f.invLarge+f.largeOrigin[k];s[k]=(p[k]-cam[k])*f.invSmall+f.smallOrigin[k];}
            const float nL=FC::sample(vol.data(),l[0],l[1],l[2]),nS=FC::sample(vol.data(),s[0],s[1],s[2]);
            const float want=FC::sigma(nL,nS,p[2]-ground,f.threshold,f.sharpness,f.height,tag,f.sigmaMax)*fc;
            const float got=FC::sigmaAt(vol.data(),f,cam,p,ground,tag,fc);assert(got==want&&got>=0&&got<=f.sigmaMax);if(got>0)++nonzero;}
        assert(nonzero>20);
        // world-fixed noise moves with the wind: advancing by dt shifts the pattern downwind (the same world point sees the old value at p - offset)
        const float p[3]={cam[0],cam[1],cam[2]},g=cam[2]-.5f;
        FC::Wind w2=w;w2.advance(.1f,.4f);const auto f2=FC::derive(1,150,.6f,.1f,w2,cam,&Q);
        const float s1=FC::sigmaAt(vol.data(),f,cam,p,g,1,1),s2=FC::sigmaAt(vol.data(),f2,cam,p,g,1,1);assert(s1>=0&&s2>=0);
    }
    {   // 0.3.199 (fog clouds, optimisation): the host-folded shader constants reproduce sigma(), and the early outs only skip samples whose sigma is 0
        auto sat=[](float x){return std::clamp(x,0.f,1.f);};
        for(const auto& k:{std::pair<float,float>{0,0},{0,1},{1,0},{.5f,.5f}}){
            const auto f=FC::derive(1,150,k.first,k.second,w0,cam,&Q);assert(f.active);
            float c[5][4]={};FC::shaderConstants(f,c);
            assert(c[1][1]==f.largeOrigin[0]&&c[2][3]==f.smallOrigin[2]&&c[3][3]==f.sigmaMax&&c[4][1]==f.invLarge&&c[4][2]==f.invSmall&&c[0][0]==0&&c[1][0]==0);
            unsigned skipped=0;
            for(int a=0;a<=40;++a)for(int b=0;b<=10;++b)for(float alt:{0.f,.5f,3.f,f.height,2*f.height,5.f*f.height,5.5f*f.height,7*f.height}){
                const float nL=a/40.f,nS=b/10.f;const float tag=1.25f;
                const float density=sat(nL*c[4][3]+(nS*c[4][0]+c[3][1]));
                float v=sat(1-alt/std::max(nL*c[3][2]+c[0][1],.001f));v*=v;
                const float shader=density*v*c[3][3]; /* zone 1 at tag 1.25 */
                const float cpu=FC::sigma(nL,nS,alt,f.threshold,f.sharpness,f.height,tag,f.sigmaMax);
                assert(std::fabs(shader-cpu)<=1e-5f*std::max(1.f,cpu*1e5f)||std::fabs(shader-cpu)<1e-6f);
                if(!(alt<c[3][0])||!(nL>c[2][0])){++skipped;assert(alt<.001f||cpu==0.f);} /* the shader's early outs: never a non-zero sample (bar the .001 bank-top floor at the ground) */
            }
            assert(skipped>0);
        }
    }
    std::printf("PASS fog clouds\n");
}
'''
with tempfile.TemporaryDirectory() as tmp:
    (Path(tmp)/'t.cpp').write_text(SRC)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/label
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(Path(tmp)/'t.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
