#include "celestial_context.h"
#include <cassert>
#include <cstdio>
#include <limits>
#include <vector>
using namespace NorthlightCelestial;
static Snapshot scene(float sunZ,float moonZ) {
    Snapshot s;s.dayFraction=.5f;
    for(int i=0;i<3;++i) {
        float z=i==0?sunZ:moonZ;
        s.bodies[i].center[0]=12*std::sqrt(1-z*z);
        s.bodies[i].center[2]=12*z;
        s.bodies[i].color=0xffffffff;s.bodies[i].texture=1;s.bodies[i].size=1;
    }
    return s;
}
int main() {
    const float camera[3]={},direct[3]={.7f,.6f,.4f};Context c;
    auto s=scene(.8f,-.8f);assert(decode(s,camera,direct,c));
    assert(c.sunWeight==1&&c.moonWeight==0&&c.sunColor[0]==direct[0]);
    s=scene(-.8f,.8f);assert(decode(s,camera,direct,c));
    assert(c.sunWeight==0&&c.moonWeight==1&&c.moonColor[0]==direct[0]);
    float last=-1;int energyCases=0;
    for(int i=0;i<=2000;++i) {
        const float z=-.1f+i*.0001f;s=scene(z,.8f);assert(decode(s,camera,direct,c));
        assert(c.sunWeight>=last);if(last>=0)assert(c.sunWeight-last<.002f);last=c.sunWeight;
        for(int j=0;j<3;++j)assert(std::fabs(c.sunColor[j]+c.moonColor[j]-direct[j])<1e-6f);
        ++energyCases;
    }
    s=scene(-.8f,-.8f);assert(decode(s,camera,direct,c));assert(c.sunWeight==0&&c.moonWeight==0);
    s=scene(.04f,.8f);Context origin;assert(decode(s,camera,direct,origin));
    const float translated[3]={-12000,2300,500};
    for(int j=0;j<3;++j){s.skyCamera[j]=translated[j];for(auto& b:s.bodies)b.center[j]+=translated[j];}
    assert(decode(s,translated,direct,c));
    for(int j=0;j<3;++j)assert(std::fabs(c.sun.direction[j]-origin.sun.direction[j])<1e-4f);
    assert(std::fabs(c.sunWeight-origin.sunWeight)<1e-4f);
    {const float trailing[3]={translated[0]+7,translated[1],translated[2]};Context t;assert(decode(s,trailing,direct,t));} /* 0.3.200: several frames of travel are accepted */
    Context sentinel;c.dayFraction=-123;sentinel=c;
    assert(!decode(s,camera,direct,c));assert(c.dayFraction==sentinel.dayFraction);
    s=scene(.8f,-.8f);s.dayFraction=std::numeric_limits<float>::quiet_NaN();assert(!decode(s,camera,direct,c));
    s=scene(.8f,-.8f);s.bodies[0].center[0]+=5;assert(!decode(s,camera,direct,c));
    s=scene(.8f,-.8f);s.bodies[1].texture=0;assert(!decode(s,camera,direct,c));
    s=scene(.8f,.8f);s.bodies[0].color=0x00ffffff;assert(decode(s,camera,direct,c));assert(c.sunWeight==0&&c.moonWeight==1);
    s=scene(.8f,-.8f);s.dayFraction=0;assert(decode(s,camera,direct,c));
    s.dayFraction=std::nextafter(1.f,0.f);assert(decode(s,camera,direct,c));
    // Logged midnight failure: native billboard alpha varies by zone, although
    // the moon is at z=.81 and the authored direct-light budget is unchanged.
    // The extension must keep a solid disc and stable lighting in all cases.
    unsigned appearanceCases=0;
    for(unsigned alpha=0;alpha<256;++alpha){
        s=scene(-.1714f,.81f);s.dayFraction=13.f/(24*60);
        s.bodies[0].color=s.bodies[1].color=(alpha<<24)|((255-alpha)*0x010101u);
        assert(decode(s,camera,direct,c));
        assert(c.moon.alpha==float(alpha)/255);
        applyRendererPolicy(c,direct);
        assert(c.moon.alpha==1&&c.sunWeight==0&&c.moonWeight==1);
        for(unsigned k=0;k<3;++k)assert(c.moonColor[k]==direct[k]&&c.sunColor[k]==0);
        assert(c.moon.tint[0]==.94f&&c.moon.tint[1]==.97f&&c.moon.tint[2]==1);
        // Daytime allocation, below-horizon rejection and energy conservation
        // use the displayed orbit, including a source raised by the renderer-owned orbit.
        for(float z:{-.1f,0.f,.02f,.04f,.08f,.8f}){
            c.sun.direction[2]=z;applyRendererPolicy(c,direct);
            assert(c.sunWeight>=0&&c.moonWeight>=0&&c.sunWeight+c.moonWeight==1);
            for(unsigned k=0;k<3;++k)assert(std::fabs(c.sunColor[k]+c.moonColor[k]-direct[k])<1e-6f);
            ++appearanceCases;
        }
    }
    s=scene(-.1f,-.1f);assert(decode(s,camera,direct,c));applyRendererPolicy(c,direct);
    assert(c.sunWeight==0&&c.moonWeight==0);
    const float black[3]={};c.moon.direction[2]=.81f;applyRendererPolicy(c,black);
    for(float v:c.moonColor)assert(v==0); // no invented light when zone budget is zero
    // Sky-only frames use current native snapshots and the SAME clock-based orbit as
    // world lighting. Missing world passes do not resurrect native appearance
    // or freeze the body. Intermittent world reads must agree with sky reads.
    float previousMoonZ=NorthlightCelestialOrbit::evaluate(2./24).moon.direction[2];
    for(unsigned frame=0;frame<6000;++frame){
        const double seconds=frame/60.;
        const float moonZ=.81f-float(seconds)*.00002f;
        s=scene(-.1714f,moonZ);s.dayFraction=float((2*3600+seconds)/86400.);s.bodies[1].color=((frame%3==0?0u:frame%3==1?14u:255u)<<24)|0x525252u;
        Context sky;assert(decode(s,camera,direct,sky));
        resolveRendererSky(sky,direct);
        assert(sky.moon.alpha==1&&sky.moonWeight==1);
        assert(std::fabs(sky.moon.direction[2]-previousMoonZ)<.001f);
        previousMoonZ=sky.moon.direction[2];
        if(frame%7==0){
            Context world;assert(decode(s,camera,direct,world));
            resolveRendererSky(world,direct);
            assert(world.moon.alpha==sky.moon.alpha&&world.moonWeight==sky.moonWeight);
            for(unsigned k=0;k<3;++k)assert(std::fabs(world.moon.direction[k]-sky.moon.direction[k])<1e-6f);
        }
    }
    // Region artwork/native positions cannot alter the clock-based direction.
    // First-frame sky and world agree at all times, including dawn/dusk.
    for(unsigned minute=0;minute<1440;++minute){
        Context sky,world;
        s=scene(-.17f,.8f);s.dayFraction=float(minute/1440.);
        assert(decode(s,camera,direct,sky));
        s=scene(.5f,-.17f);s.dayFraction=float(minute/1440.);
        s.bodies[0].color=0;s.bodies[1].color=0;
        assert(decode(s,camera,direct,world));
        const auto result=resolveRendererSky(sky,direct);assert(result.valid);
        resolveRendererSky(world,direct);
        for(unsigned k=0;k<3;++k){
            assert(sky.sun.direction[k]==world.sun.direction[k]);
            assert(sky.moon.direction[k]==world.moon.direction[k]);
            assert(sky.sunColor[k]==world.sunColor[k]);
            assert(sky.moonColor[k]==world.moonColor[k]);
            assert(sky.sunColor[k]+sky.moonColor[k]<=direct[k]+1e-6f);
        }
        const double seconds=minute*60.;
        if(seconds>NorthlightCelestialOrbit::kSunriseSeconds&&seconds<NorthlightCelestialOrbit::kSunsetSeconds)assert(sky.sunWeight>0&&sky.moonWeight==0);
        else assert(sky.moonWeight>0&&sky.sunWeight==0);
        assert(std::fabs(std::tan(sky.moon.angularRadius)*24/1.75-.46176318)<1e-6);
        assert(std::fabs(std::tan(sky.sun.angularRadius)*24-.506736)<1e-6);
        const float radius=sky.moon.angularRadius,sunRadius=sky.sun.angularRadius;
        resolveRendererSky(sky,direct);assert(sky.moon.angularRadius==radius&&sky.sun.angularRadius==sunRadius);
        // Native horizon enlargement (including zero/stale artwork size) must
        // not affect the owned disc. Exercise the full orbit, both bodies and
        // all renderer-policy outputs while keeping decode faithful to native data.
        for(float nativeSize:{0.f,.5f,1.f,1.75f,3.5f,12.f}){
            s.bodies[0].size=nativeSize;s.bodies[1].size=12.f-nativeSize;
            Context animated;assert(decode(s,camera,direct,animated));
            assert(animated.sun.nativeAngularRadius==std::atan(nativeSize/24.f));
            assert(animated.moon.nativeAngularRadius==std::atan((12.f-nativeSize)/24.f));
            resolveRendererSky(animated,direct);
            assert(animated.sun.angularRadius==sunRadius&&animated.moon.angularRadius==radius);
            for(unsigned k=0;k<3;++k){
                assert(animated.sun.direction[k]==sky.sun.direction[k]);
                assert(animated.moon.direction[k]==sky.moon.direction[k]);
                assert(animated.sunColor[k]==sky.sunColor[k]);
                assert(animated.moonColor[k]==sky.moonColor[k]);
            }
        }
    }
    c.dayFraction=-1;const auto before=c;
    assert(!resolveRendererSky(c,direct).valid);
    assert(std::memcmp(&before,&c,sizeof c)==0);
    // Read protocol and partial/changed data fail closed; caller value unchanged.
    std::vector<unsigned char> memory(0x388);s=scene(.8f,-.8f);
    std::memcpy(memory.data()+4,&s.dayFraction,4);
    std::memcpy(memory.data()+0x18,s.skyCamera,12);std::memcpy(memory.data()+0x328,s.bodies,96);
    Snapshot copied;int reads=0;
    auto reader=[&](uintptr_t address,void* dst,size_t size){assert(address==0xd38b00&&size==memory.size());++reads;std::memcpy(dst,memory.data(),size);return true;};
    assert(readSnapshot(reader,copied));assert(reads==2&&decode(copied,camera,direct,c));
    auto torn=[&](uintptr_t address,void* dst,size_t size){reader(address,dst,size);memory[4]^=1;return true;};
    copied.dayFraction=-123;assert(!readSnapshot(torn,copied));assert(copied.dayFraction==-123);
    assert(!readSnapshot([](uintptr_t,void*,size_t){return false;},copied));
    size_t count=0;auto table=signatures(count);
    auto codeReader=[&](uintptr_t address,void* dst,size_t size){for(size_t i=0;i<count;++i)if(table[i].address==address){assert(size==table[i].size);std::memcpy(dst,table[i].bytes,size);return true;}return false;};
    assert(verifyCode(codeReader));
    for(size_t bad=0;bad<count;++bad) {
        auto changed=[&](uintptr_t address,void* dst,size_t size){bool ok=codeReader(address,dst,size);if(address==table[bad].address)static_cast<unsigned char*>(dst)[0]^=1;return ok;};
        assert(!verifyCode(changed));
    }
    std::printf("{\"energy_continuity_cases\":%d,\"appearance_alpha_cases\":%u,\"sky_only_frames\":6000,\"clock_region_cases\":1440,\"constant_size_cases\":8640,\"signature_mutations_rejected\":%zu,\"translation_invariance\":true,\"malformed_and_torn_reads_rejected\":true}\n",energyCases,appearanceCases,count);
}
