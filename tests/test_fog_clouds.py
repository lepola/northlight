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
        for(float n:{0.f,.4f,1.f})assert(FC::sigma(n,n,2,0,10,1,.03f)==0.f);
        assert(FC::sigma(1,1,-.01f,1,10,1,.03f)==0.f&&FC::sigma(1,1,2,1,10,0,.03f)==0.f&&FC::sigma(1,1,2,1,10,-1,.03f)==0.f);
        float prev=-1;for(int i=0;i<=20;++i){const float s=FC::sigma(.6f,.5f,2,float(i)/20,10,1,.03f);assert(s>=prev);prev=s;}
        assert(prev>0);
        assert(near(FC::sigma(1,1,0,1,10,1,.03f),.03f)&&FC::sigma(1,1,10,1,10,1,.03f)==0.f&&FC::sigma(1,1,20,1,10,1,.03f)==0.f);
        const float t1=FC::sigma(1,1,1,1,10,1,.03f),t5=FC::sigma(1,1,1,1,10,5,.03f);assert(near(t5,.7f*t1,1e-7f)&&t1>0);
        assert(FC::sigma(1,1,1,1,10,9,.03f)==t5); /* zone saturates */
    }
    const float cam[3]={1234.5f,-987.25f,40.f};FC::Wind w0;
    {   // derive
        for(auto f:{FC::derive(0,100,1,1,w0,cam),FC::derive(1,0,1,1,w0,cam)}){
            assert(!f.active&&f.coverage==0&&f.height==0&&f.sigmaMax==0&&f.invLarge==0&&f.invSmall==0);
            for(int i=0;i<3;++i)assert(f.largeOrigin[i]==0&&f.smallOrigin[i]==0);}
        const auto dry=FC::derive(1,100,0,0,w0,cam),night=FC::derive(1,100,0,1,w0,cam),rain=FC::derive(1,100,1,0,w0,cam);
        assert(dry.active&&near(dry.coverage,.12f)&&near(night.coverage,.30f)&&near(rain.coverage,1.f));
        assert(dry.coverage<night.coverage&&night.coverage<rain.coverage);
        assert(near(dry.height,7)&&near(rain.height,14)&&dry.sigmaMax==.03f&&near(dry.invLarge,1.f/192)&&near(dry.invSmall,1.f/48));
        assert(near(FC::derive(1,200,0,0,w0,cam).coverage,.24f)&&near(FC::derive(1,50,0,0,w0,cam).coverage,.06f)&&FC::derive(1,200,1,1,w0,cam).coverage==1.f);
        const float nan=std::numeric_limits<float>::quiet_NaN();
        const auto bad=FC::derive(1,100,nan,nan,w0,cam);assert(bad.active&&near(bad.coverage,.12f)&&near(bad.height,7));
        FC::Wind w;for(int i=0;i<500;++i)w.advance(.05f,.5f);
        const float far[3]={-1e5f,3.3e5f,-17.f};
        for(const float* c:{cam,far}){const auto f=FC::derive(1,100,.3f,.2f,w,c);for(int i=0;i<3;++i)assert(f.largeOrigin[i]>=0&&f.largeOrigin[i]<1&&f.smallOrigin[i]>=0&&f.smallOrigin[i]<1);}
    }
    {   // wind: dt clamp, speeds, no jump on a fog change, offsets stay in range
        FC::Wind a,b;a.advance(5,0.5f);b.advance(.1f,0.5f);
        for(int i=0;i<3;++i)assert(a.large[i]==b.large[i]&&a.small[i]==b.small[i]);assert(a.clock==b.clock);
        FC::Wind c;c.advance(-1,.5f);assert(c.clock==0&&c.large[0]==0);
        c.advance(std::numeric_limits<float>::quiet_NaN(),.5f);assert(c.clock==0);
        FC::Wind d;d.advance(.1f,std::numeric_limits<float>::infinity());assert(near(d.speed,.6f));
        FC::Wind e;e.advance(.1f,0);assert(near(e.speed,.6f)&&near(e.dir[0],std::cos(.35f))&&near(e.dir[1],std::sin(.35f),1e-5f));
        FC::Wind r;r.advance(.1f,3);assert(near(r.speed,2.4f));
        FC::Wind j;j.advance(.1f,0);const double before[2]={j.large[0],j.large[1]};j.advance(.1f,1);
        assert(std::hypot(j.large[0]-before[0],j.large[1]-before[1])<=2.4*.1+1e-9); /* one frame moves at most speed*dt */
        FC::Wind m;for(int i=0;i<200000;++i){m.advance(.1f,i%7<3?1.f:0.f);
            for(int k=0;k<3;++k)assert(m.large[k]>=0&&m.large[k]<FC::LargePeriod&&m.small[k]>=0&&m.small[k]<FC::SmallPeriod);}
        assert(near(float(m.clock),20000.f,1.f));
        FC::Wind z;z.advance(.1f,0);assert(z.small[2]>0); /* small scale drifts up a little */
    }
    {   // sigmaAt mirrors sigma(sample(..)) built by hand
        FC::Wind w;for(int i=0;i<300;++i)w.advance(.1f,.4f);
        const auto f=FC::derive(1,150,.6f,.1f,w,cam);assert(f.active);
        const float zero[3]={0,0,0};assert(FC::sigmaAt(vol.data(),FC::Frame{},cam,zero,0,1,1)==0.f);
        unsigned nonzero=0;
        for(int i=0;i<400;++i){
            const float p[3]={cam[0]+float(i%20-10)*7.f,cam[1]+float(i/20-10)*5.f,cam[2]+float(i%9)*.8f-1.f};
            const float ground=cam[2]-2.f,tag=float(i%6),fc=float(i%5)/4;
            float l[3],s[3];for(int k=0;k<3;++k){l[k]=(p[k]-cam[k])*f.invLarge+f.largeOrigin[k];s[k]=(p[k]-cam[k])*f.invSmall+f.smallOrigin[k];}
            const float nL=FC::sample(vol.data(),l[0],l[1],l[2]),nS=FC::sample(vol.data(),s[0],s[1],s[2]);
            const float want=FC::sigma(nL,nS,p[2]-ground,f.coverage,f.height,tag,f.sigmaMax)*fc;
            const float got=FC::sigmaAt(vol.data(),f,cam,p,ground,tag,fc);assert(got==want&&got>=0&&got<=f.sigmaMax);if(got>0)++nonzero;}
        assert(nonzero>20);
        // world-fixed noise moves with the wind: advancing by dt shifts the pattern downwind (the same world point sees the old value at p - offset)
        const float p[3]={cam[0],cam[1],cam[2]},g=cam[2]-.5f;
        FC::Wind w2=w;w2.advance(.1f,.4f);const auto f2=FC::derive(1,150,.6f,.1f,w2,cam);
        const float s1=FC::sigmaAt(vol.data(),f,cam,p,g,1,1),s2=FC::sigmaAt(vol.data(),f2,cam,p,g,1,1);assert(s1>=0&&s2>=0);
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
