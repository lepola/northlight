// northlight-test: requires=zig
#include "equipment_lights.h"
#include <cassert>
#include <chrono>
#include <cstdio>
using namespace NorthlightLocalLights;
int main(int argc,char** argv){
    assert(argc==3);std::vector<Light> legacy,current;assert(readFile(argv[1],legacy)&&readFile(argv[2],current));
    assert(legacy.size()==current.size()&&legacy.size()==4);
    for(unsigned i=0;i<legacy.size();++i){const auto& a=legacy[i];const auto& b=current[i];
        assert(a.sourceId==b.sourceId&&a.kind==b.kind&&a.flags==b.flags);
        assert(!std::memcmp(a.position,b.position,sizeof a.position));
        for(unsigned k=0;k<3;++k)assert(std::fabs(a.diffuse[k]-b.diffuse[k])<1e-6f);
        assert(a.attenuationStart==b.attenuationStart&&a.attenuationEnd==b.attenuationEnd);
    }
    // Actual Orgrimmar patch-D bonfire values at placement scale 5.36797.
    const auto& bonfire=current[0];assert(bonfire.kind==2&&bonfire.diffuse[0]==1&&bonfire.attenuationEnd==12);
    assert(std::fabs(bonfire.diffuse[1]-.470588237f)<1e-6f&&std::fabs(bonfire.diffuse[2]-.156862751f)<1e-6f);
    NorthlightEquipmentLights::Native n;std::copy_n(bonfire.position,3,n.position);
    n.color[0]=4;n.color[1]=1.882352948f;n.color[2]=.627451003f;n.attenuation[1]=.7f;n.attenuation[2]=.03f;
    const auto live=NorthlightEquipmentLights::light(n,1,1);
    assert(live.attenuationStart==bonfire.attenuationStart&&live.attenuationEnd==bonfire.attenuationEnd);
    for(unsigned k=0;k<3;++k)assert(live.diffuse[k]==bonfire.diffuse[k]);
    float previous=INFINITY;
    for(unsigned step=0;step<=1600;++step){const float d=step*.01f,fade=attenuation(bonfire,d),energy=bonfire.diffuse[0]*fade*fade;
        assert(energy<=previous&&energy>=0);previous=energy;if(d>=12)assert(energy==0);
    }
    assert(attenuation(bonfire,100)==0&&attenuation(bonfire,214.718948f)==0);
    assert(std::fabs(attenuation(bonfire,6)-6.f/11)<1e-6f);
    // WMO and curated emissive fixtures keep their original units and energy.
    assert(current[2].kind==1&&current[2].diffuse[0]==4&&current[2].attenuationEnd==214.718948f);
    assert(current[3].kind==3&&current[3].diffuse[0]==4&&current[3].attenuationEnd==214.718948f);
    std::vector<Light> reread;assert(readFile(argv[2],reread));assert(!std::memcmp(reread.data(),current.data(),current.size()*sizeof(Light)));
    NorthlightEquipmentLights::Tracker tracked;float camera[3]{};tracked.observe(n,1000,current,camera);tracked.publish(1100,&current);assert(!tracked.count&&!tracked.outputCount);
    NorthlightNativeLights::Snapshot scene;scene.valid=1;scene.scene=1;scene.count=NorthlightNativeLights::Capacity;
    for(unsigned i=0;i<scene.count;++i){auto& p=scene.lights[i];p.id=i+1;p.position[0]=float(i);p.color[0]=1;p.attenuation[1]=.7f;p.attenuation[2]=.03f;}
    const auto start=std::chrono::steady_clock::now();
    for(unsigned frame=0;frame<300;++frame){const unsigned now=2000+frame*50;tracked.reset();
        for(unsigned i=0;i<scene.count;++i){NorthlightEquipmentLights::Native drawn;std::copy_n(scene.lights[i].position,3,drawn.position);
            drawn.color[2]=1;drawn.attenuation[1]=.7f;drawn.attenuation[2]=.03f;tracked.observe(drawn,now,{},camera);}
        tracked.observeScene(scene,now,{},camera);tracked.publish(now);assert(tracked.count==scene.count);
    }
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/300;
    std::printf("NATIVE_TRANSPORT_PASS legacyMigration=1 version2Idempotent=1 bonfireRadius=12 bonfirePeak=1 liveEqualsStatic=1 cutoff=12 untouchedWmoCurated=1 maxSceneAnonymous=128 updateMs=%.4f\n",elapsed);
}
