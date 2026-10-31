// Scalar-reference equivalence for prepared point-shadow clip planes; optional ABBA microbenchmark.
#include "point_light_shadow.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <random>
#include <vector>
#include <cstring>
using namespace NorthlightPointShadow;
struct Box{float lo[3],hi[3];bool valid=true;};
int main(int argc,char**){
    std::mt19937 rng(0x30739543);std::uniform_real_distribution<float> unit(-1,1);unsigned checks=0;
    for(unsigned cube=0;cube<320;++cube){float matrices[6][16],p[3];for(float& x:p)x=unit(rng)*20000;
        // Use signed exponents explicitly; unsigned underflow must not make an invalid fixture.
        const float range=std::ldexp(1.f,int(cube%18)-8)+.2f;
        for(unsigned f=0;f<6;++f)assert(faceMatrix(p,.1f,range,f,matrices[f]));
        if(cube%11==0)matrices[cube%6][cube%16]=NAN;
        if(cube%13==0)matrices[cube%6][cube%16]=INFINITY;
        if(cube%17==0)for(auto& face:matrices)for(auto& x:face)x*=1e34f;
        const FaceClip prepared(matrices);
        for(unsigned i=0;i<512;++i){Box b;
            for(unsigned k=0;k<3;++k){const float a=p[k]+unit(rng)*range*2;const float size=std::fabs(unit(rng))*range*.3f;b.lo[k]=a-size;b.hi[k]=a+size;}
            if(i%37==0)b.lo[i%3]=NAN;if(i%41==0)b.hi[i%3]=INFINITY;if(i%43==0)b.lo[i%3]=b.hi[i%3]+1;if(i%47==0)b.valid=false;
            assert(prepared.mask(b.lo,b.hi,b.valid)==faceMask(b.lo,b.hi,b.valid,matrices));++checks;
        }
    }
    {float matrices[6][16],p[]={10000.f,-5000.f,750.f};for(unsigned f=0;f<6;++f)assert(faceMatrix(p,.1f,48,f,matrices[f]));const FaceClip prepared(matrices);
        assert(prepared.mask(nullptr,nullptr,true)==63);
        for(unsigned axis=0;axis<3;++axis)for(float delta:{-.1f,0.f,.1f,48.f,-48.f})for(int step=-16;step<=16;++step){Box b;
            for(unsigned k=0;k<3;++k)b.lo[k]=b.hi[k]=p[k];
            float x=p[axis]+delta;for(int n=0;n<std::abs(step);++n)x=std::nextafter(x,step<0?-INFINITY:INFINITY);
            b.lo[axis]=b.hi[axis]=x;assert(prepared.mask(b.lo,b.hi,true)==faceMask(b.lo,b.hi,true,matrices));++checks;
        }
    }
    // Arbitrary finite clip transforms include cancellation and overflow-adjacent inputs.
    for(unsigned n=0;n<10000;++n){float matrices[6][16];Box b;
        for(auto& face:matrices)for(float& x:face)x=std::ldexp(unit(rng),int(rng()%220)-110);
        for(unsigned k=0;k<3;++k){b.lo[k]=std::ldexp(unit(rng),int(rng()%220)-110);b.hi[k]=std::ldexp(unit(rng),int(rng()%220)-110);if(b.lo[k]>b.hi[k])std::swap(b.lo[k],b.hi[k]);}
        assert(FaceClip(matrices).mask(b.lo,b.hi,true)==faceMask(b.lo,b.hi,true,matrices));++checks;
    }
    std::printf("Prepared clip exact masks: %u cases; bytes=%zu\n",checks,sizeof(FaceClip));
    if(argc==1)return 0;
    float matrices[6][16],p[]={1345.f,-2499.f,317.f};for(unsigned f=0;f<6;++f)assert(faceMatrix(p,.1f,48,f,matrices[f]));const FaceClip prepared(matrices);
    std::vector<Box> boxes(8192);for(auto& b:boxes)for(unsigned k=0;k<3;++k){float a=p[k]+unit(rng)*64,s=std::fabs(unit(rng))*8;b.lo[k]=a-s;b.hi[k]=a+s;}
    std::vector<double> times[2];volatile unsigned checksum=0;
    for(unsigned run=0;run<16;++run){const unsigned mode=(run/2)%2?1-run%2:run%2;unsigned sum=0;auto start=std::chrono::steady_clock::now();
        for(unsigned repeat=0;repeat<20;++repeat)for(const auto& b:boxes)sum+=mode?prepared.mask(b.lo,b.hi,true):faceMask(b.lo,b.hi,true,matrices);
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();checksum+=sum;if(run>=4)times[mode].push_back(ms);
    }
    for(unsigned mode=0;mode<2;++mode){std::sort(times[mode].begin(),times[mode].end());std::printf("mode=%u medianMs=%.6f candidates=163840 repeats=6\n",mode,(times[mode][2]+times[mode][3])*.5);}
    std::printf("checksum=%u\n",unsigned(checksum));
}
