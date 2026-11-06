#include "native_scene_light_fixture.h"
#include "particle_lights.h"
static void calibratedEquipmentTests(){
    // Knife_1H_Utgarde_D_03's three calibrated sources. The reader must keep
    // their authored energy without a model draw, even below bridge admission.
    constexpr float energy[]={.016440100967884064f,.008057137951254845f,.00564913172274828f};
    Bytes b=modelAsset(),records(3*156);for(unsigned i=0;i<3;++i)std::memcpy(records.data()+i*156,b.data()+648,156);
    const auto offset=append(b,records.data(),unsigned(records.size()));put(b,264,3u);put(b,268,offset);
    for(unsigned i=0;i<3;++i){const auto at=offset+i*156;put(b,at+4,.1f*float(i));
        authoredTrack(b,at+56,12,{{1,.5f,.125f}},3);authoredTrack(b,at+76,4,{{energy[i]}});authoredTrack(b,at+136,1,{{1}});}
    Fixture f(b);f.add();f.update();assert(f.output.count==3);const auto first=f.output;
    NorthlightEquipmentLights::Tracker bridge;NorthlightParticleLights::Frame particles;NorthlightLocalLightSelection::Tracker selection;
    for(unsigned frame=0;frame<10;++frame){const unsigned now=1000+frame*100;put(f.memory.object(f.scene),12,now);f.update();
        assert(f.output.count==3);for(unsigned i=0;i<3;++i){assert(f.output.lights[i].id==first.lights[i].id);
            for(unsigned k=0;k<3;++k)assert(std::fabs(f.output.lights[i].color[k]-energy[i]*(k==0?1.f:k==1?.5f:.125f))<1e-7f);}
        bridge.observeScene(f.output,now,{},f.camera);bridge.publish(now);}
    // Existing native admission is .01: only the strongest source is bridged.
    assert(bridge.count==1&&bridge.outputCount==1);const auto native=bridge.output[0];
    assert(std::fabs(native.diffuse[0]-energy[0]/.73f)<1e-7f&&native.attenuationEnd==12);
    for(float distance:{2.f,6.f,8.f}){
        const float expected=energy[0]/.73f*(12.f-distance)/11.f;
        assert(std::fabs(native.diffuse[0]*NorthlightLocalLights::attenuation(native,distance)-expected)<1e-7f);}
    // The former 50-sprite gain made the weakest authored emitter dominate
    // the far field, over seventeen times the restored strongest contribution.
    auto amplified=native;amplified.diffuse[0]=energy[2]*50.f/.73f;
    assert(amplified.diffuse[0]*NorthlightLocalLights::attenuation(amplified,8)>
        17.f*native.diffuse[0]*NorthlightLocalLights::attenuation(native,8));
    particles.publish(bridge.output.data(),bridge.outputCount,{},f.camera,selection);
    assert(particles.outputCount==1&&particles.immediateCount==0&&particles.output[0].sourceId==native.sourceId);
    NorthlightParticleLights::Sample sample{};std::copy_n(native.position,3,sample.position);sample.rgb[0]=.04f;sample.rgb[1]=.02f;sample.rgb[2]=.005f;
    particles.nextFrame();particles.observe(sample,f.camera);particles.publish(bridge.output.data(),bridge.outputCount,{},f.camera,selection);
    assert(particles.outputCount==1&&particles.immediateCount==1&&std::fabs(particles.output[0].diffuse[0]-.028f)<1e-7f);
    particles.nextFrame();particles.publish(bridge.output.data(),bridge.outputCount,{},f.camera,selection);
    assert(particles.outputCount==1&&particles.immediateCount==0&&particles.output[0].sourceId==native.sourceId&&particles.output[0].diffuse[0]==native.diffuse[0]);
    // A genuine model lamp keeps the established denominator fit and range.
    Fixture lamp;lamp.add();lamp.update();NorthlightEquipmentLights::Tracker lamps;
    lamps.observeScene(lamp.output,1000,{},lamp.camera);lamps.publish(1000);lamps.publish(1100);
    assert(lamps.outputCount==1&&lamps.output[0].attenuationEnd==12);
    for(unsigned k=0;k<3;++k)assert(std::fabs(lamps.output[0].diffuse[k]-lamp.output.lights[0].color[k]/.73f)<1e-6f);
    assert(lamps.output[0].diffuse[0]*NorthlightLocalLights::attenuation(lamps.output[0],8)>.35f);
    std::puts("CALIBRATED_EQUIPMENT_PASS no-draw energy, native/particle replacement, unchanged lamp reach");
}
static void movingPoseTests(){
    {Bytes b=modelAsset();authoredTrack(b,320,12,{{0,0,0,4,0,0}},3);Fixture f(b);f.add();f.update();assert(f.output.count==1&&std::fabs(f.output.lights[0].position[0]-2)<1e-6f&&f.output.lights[0].flags==1&&f.output.lights[0].anchor[0]==0);
     put(f.memory.object(f.scene),12,1100u);f.update();assert(f.output.lights[0].position[0]==4);}
    {Bytes b=modelAsset();put(b,652,1.f);const float q=std::sqrt(.5f);quaternionTrack(b,340,{{0,0,0,1,0,0,q,q}});Fixture f(b);f.add();f.update();assert(f.output.count==1&&std::fabs(f.output.lights[0].position[0]-q)<.0001f&&std::fabs(f.output.lights[0].position[1]-q)<.0001f);}
    {Bytes b=modelAsset();authoredTrack(b,320,12,{{0,0,0},{8,0,0}},3);Fixture f(b);f.add();auto& bone=f.memory.object(f.bones[0]);put<std::uint16_t>(bone,0x6c,0);put(bone,0x70,100u);put(bone,0x74,2100u);put(bone,0x78,1.f);put(bone,0x9c,1100u);put(bone,0xa0,.001f);put(bone,0xa4,1.f);f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==4);}
    for(unsigned flags:{0u,1u,2u,4u,6u,7u}){Bytes b=modelAsset();put(b,388,0.f);put(b,660,0.f);put(b,652,1.f);authoredTrack(b,320,12,{{10,0,0}},3);authoredTrack(b,360,12,{{2,2,2}},3);const float q=std::sqrt(.5f);quaternionTrack(b,340,{{0,0,q,q}});
     Bytes bones(176);std::memcpy(bones.data(),b.data()+304,88);put(bones,88+4,flags);put<std::int16_t>(bones,88+8,0);for(unsigned offset:{104u,124u,144u})put<std::int16_t>(bones,offset+2,-1);put(b,44,2u);put(b,48,append(b,bones.data(),unsigned(bones.size())));put<std::uint16_t>(b,650,1);
     Fixture f(b);f.add();f.update();assert(f.output.count==1);const float x=flags==0||flags==2?10:flags==4?12:flags==6?11:flags==7?1:0,y=flags==0||flags==1?2:flags==2?1:0;
     assert(std::fabs(f.output.lights[0].position[0]-x)<.001f&&std::fabs(f.output.lights[0].position[1]-y)<.001f);}
    {Fixture f;f.add();Bytes procedure(64);for(unsigned i=0;i<16;i+=5)put(procedure,i*4,1.f);put(procedure,48,3.f);const auto pointer=f.memory.add(procedure);put(f.memory.object(f.bones[0]),0x8c,0x80u);put(f.memory.object(f.bones[0]),0x88,pointer);
     f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==3);put(f.memory.object(pointer),48,7.f);f.update();assert(f.output.lights[0].position[0]==7);}
    {Bytes b=modelAsset();put(b,308,8u);put(b,652,1.f);Fixture f(b);f.add();f.update();assert(f.output.count==1&&std::fabs(f.output.lights[0].position[2])<1e-6f);
     const float view[16]={0,0,-1,0,0,1,0,0,1,0,0,0,0,0,0,1},inverse[16]={0,0,1,0,0,1,0,0,-1,0,0,0,0,0,0,1};for(unsigned i=0;i<16;++i){put(f.memory.object(f.scene),0x84+i*4,view[i]);put(f.memory.object(f.scene),0xc4+i*4,inverse[i]);}
     f.update();assert(f.output.count==1&&std::fabs(f.output.lights[0].position[0]-1)<1e-6f&&std::fabs(f.output.lights[0].position[2]-1)<1e-6f);}
    {Bytes b=modelAsset();authoredTrack(b,320,12,{{0,0,0,4,0,0}},3);Fixture f(b);const auto parent=f.add(10);f.memory.set(f.lights[0],0x64,0u);const auto child=f.add(3);put(f.memory.object(child),0x48,parent);put(f.memory.object(child),0x54,0xffffu);put(f.memory.object(child),16,0x40001u);
     f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==17&&f.output.lights[0].flags==0);put(f.memory.object(f.scene),12,1100u);f.update();assert(f.output.lights[0].position[0]==21);
     put(f.memory.object(parent),0x178,.5f);f.update();assert(std::fabs(f.output.lights[0].color[0]-.97254908f*.5f)<1e-6f);put(f.memory.object(child),16,0x140001u);f.update();assert(std::fabs(f.output.lights[0].color[0]-.97254908f)<1e-6f);
     put(f.memory.object(child),0x48,0u);f.update();assert(f.output.lights[0].position[0]==7&&f.output.lights[0].flags==1);put(f.memory.object(child),0x48,parent);put(f.memory.object(parent),0xe4,20.f);f.update();assert(f.output.lights[0].position[0]==31);
     put(f.memory.object(parent),0x48,child);put(f.memory.object(parent),0x54,0xffffu);put(f.memory.object(parent),16,0x40001u);f.update();assert(!f.output.count);}
    {Bytes b=modelAsset();authoredTrack(b,320,12,{{0,0,0,4,0,0}},3);Bytes attachment(40);put<std::uint16_t>(attachment,4,0);put(attachment,8,1.f);const auto offset=append(b,attachment.data(),40);put(b,240,1u);put(b,244,offset);authoredTrack(b,offset+20,1,{{0},{1},{0,1}});
     Fixture f(b);const auto parent=f.add(10);f.memory.set(f.lights[0],0x64,0u);f.useAsset(modelAsset());const auto child=f.add(3);put(f.memory.object(child),0x48,parent);put(f.memory.object(child),0x54,0u);
     f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==16);put<std::uint16_t>(f.memory.object(f.bones[0]),0x48,2);f.update();assert(!f.output.count);put(f.memory.object(f.scene),12,1100u);f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==18);
     const auto second=f.add(30);f.memory.set(f.lights.back(),0x64,0u);put(f.memory.object(child),0x48,second);put(f.memory.object(child),0x54,0xffffu);put(f.memory.object(child),16,0x40001u);f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==33);}
    {Bytes b=modelAsset();authoredTrack(b,320,12,{{0,0,0,4,0,0}},3);Fixture f(b);auto parent=f.add(10);for(unsigned i=0;i<4;++i){f.memory.set(f.lights.back(),0x64,0u);const auto child=f.add(1);put(f.memory.object(child),0x48,parent);put(f.memory.object(child),0x54,0xffffu);put(f.memory.object(child),16,0x40001u);parent=child;}
     f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==24);put(f.memory.object(f.scene),12,1100u);f.update();assert(f.output.lights[0].position[0]==34);}
    {Bytes b=modelAsset();authoredTrack(b,320,12,{{8,0,0}},3);Fixture f(b);const auto parent=f.add(10);put(f.memory.object(parent),16,0x1001u);f.memory.set(f.lights[0],0x64,0u);f.useAsset(modelAsset());const auto child=f.add(3);put(f.memory.object(child),0x48,parent);put(f.memory.object(child),0x54,0xffffu);put(f.memory.object(child),16,0x40001u);f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==13);}
    {Bytes b=modelAsset();std::vector<std::vector<float>> colors(241,{1,.5f,.25f});authoredTrack(b,648+56,12,colors,3);Fixture f(b);f.add();const auto table=get<unsigned>(f.memory.object(f.data),648+56+16);for(unsigned i=2;i<241;++i)f.memory.set(table,i*8+4,0xfffffffcu);
     f.update();assert(f.output.count==1&&!f.reader.failures);put<std::uint16_t>(f.memory.object(f.bones[0]),0x48,3);f.update();assert(!f.output.count);float rgb[3]={.25f,.5f,1.f};Bytes loaded(sizeof rgb);std::memcpy(loaded.data(),rgb,sizeof rgb);const auto pointer=f.memory.add(std::move(loaded));f.memory.set(table,3*8+4,pointer);f.update();assert(f.output.count==1&&std::fabs(f.output.lights[0].color[2]-.75f)<1e-6f);}
    {Bytes b=modelAsset();put(b,308,0u);authoredTrack(b,320,12,{{0,0,0,4,0,0}},3);Fixture f(b);f.add();put(f.memory.object(f.bones[0]),0x8c,0x200u);f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==2);}
    {Bytes b=modelAsset();put(b,648+136+4,0u);Fixture f(b);f.add();f.update();assert(!f.output.count);}
    {Bytes b=modelAsset();put(b,652,1.f);quaternionTrack(b,340,{{0,0,0,1},{0,0,std::sqrt(.75f),.5f}});Fixture f(b);f.add();auto& bone=f.memory.object(f.bones[0]);put<std::uint16_t>(bone,0x6c,0);put(bone,0x70,100u);put(bone,0x74,2100u);put(bone,0x78,1.f);put(bone,0x9c,850u);put(bone,0xa0,.001f);put(bone,0xa4,1.f);
     f.update();const float angle=101.25f*3.14159265358979323846f/180.f;assert(f.output.count==1&&std::fabs(f.output.lights[0].position[0]-std::cos(angle))<.0001f&&std::fabs(f.output.lights[0].position[1]-std::sin(angle))<.0001f);}
    {Bytes b=modelAsset();unsigned duration=1000;put(b,20,1u);put(b,24,append(b,&duration,4));authoredTrack(b,320,12,{{0,0,0,4,0,0}},3);put<std::int16_t>(b,322,0);Fixture f(b);f.add();put(f.memory.object(f.models[0]),0x64,350u);f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==2);put(f.memory.object(f.scene),12,1100u);f.update();assert(f.output.lights[0].position[0]==0);}
    {Bytes b=modelAsset();std::vector<std::vector<float>> translations;for(unsigned i=0;i<8;++i)translations.push_back({float(i),0,0,float(i)+4,0,0});authoredTrack(b,320,12,translations,3);Bytes sequences(8*64);for(unsigned i=0;i<8;++i)put(sequences,i*64+4,2000u);put(b,28,8u);put(b,32,append(b,sequences.data(),unsigned(sequences.size())));
     Fixture f(b);std::vector<unsigned> parents;for(unsigned i=0;i<8;++i){parents.push_back(f.add(10+float(i)*3,i));f.memory.set(f.lights.back(),0x64,0u);}f.memory.watchBegin=f.data;f.memory.watchEnd=f.data+unsigned(f.memory.object(f.data).size());f.useAsset(modelAsset());
     for(unsigned i=0;i<8;++i){const auto child=f.add(1);put(f.memory.object(child),0x48,parents[i]);put(f.memory.object(child),0x54,0xffffu);put(f.memory.object(child),16,0x40001u);}f.update();assert(f.output.count==8);
#ifdef _WIN32
     f.memory.system=true;
#endif
     const auto start=std::chrono::steady_clock::now();for(unsigned frame=0;frame<200;++frame){f.memory.watchedReads=0;put(f.memory.object(f.scene),12,610+frame*6);f.update();assert(f.output.count==8&&f.memory.watchedReads==1);} // only immutable header is revalidated
     const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/200;std::printf("NATIVE_SHARED_SEQUENCES_PASS parents=8 sequences=8 warmMs=%.4f blobReads=%u\n",ms,f.memory.watchedReads);
    }
    {SceneDetail::Asset a;a.poses.resize(1);auto& cache=a.poses[0].translation;cache.curves.resize(2);for(unsigned i=0;i<2;++i){cache.curves[i].sequence=i;cache.curves[i].used=1;cache.curves[i].bytes=20;}a.bytes=a.allowance=2*sizeof(SceneDetail::CachedCurve)+40;
     assert(SceneDetail::curveRoom(a,cache,20,2));assert(cache.curves[0].sequence==~0u&&cache.curves[1].sequence==1); // one invalid slot is reused without charging metadata again
     cache.curves[0].sequence=2;cache.curves[0].bytes=20;cache.curves[0].used=cache.curves[1].used=2;a.bytes+=20;assert(!SceneDetail::curveRoom(a,cache,20,2));
     for(auto& curve:cache.curves){curve.bytes=0;curve.sequence=~0u;}a.bytes=2*sizeof(SceneDetail::CachedCurve);a.allowance=sizeof(SceneDetail::CachedCurve)+20;assert(SceneDetail::curveRoom(a,cache,20,3)&&a.bytes==0&&cache.curves.empty());}
    // Unloaded, unused animation key pointers must never reject a parent. Only
    // the two current sequences (or one global loop) are read and cached.
    {Bytes parent=modelAsset();std::vector<std::vector<float>> values(241,{0,0,0});values[1]={0,0,0,4,0,0};authoredTrack(parent,320,12,values,3);Fixture f(parent);const auto actor=f.add(10);f.memory.set(f.lights[0],0x64,0u);
     const auto table=get<unsigned>(f.memory.object(f.data),320+16);for(unsigned i=2;i<241;++i)f.memory.set(table,i*8+4,0xfffffffcu);
     f.useAsset(modelAsset());const auto child=f.add(3);put(f.memory.object(child),0x48,actor);put(f.memory.object(child),0x54,0xffffu);put(f.memory.object(child),16,0x40001u);f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==15);
     put<std::uint16_t>(f.memory.object(f.bones[0]),0x48,2);f.update();assert(!f.output.count);float translated[3]={8,0,0};Bytes loaded(sizeof translated);std::memcpy(loaded.data(),translated,sizeof translated);const auto pointer=f.memory.add(std::move(loaded));f.memory.set(table,2*8+4,pointer);f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==21);}
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);calibratedEquipmentTests();movingPoseTests();
    {Fixture f;f.add();f.update();assert(f.output.valid&&f.output.count==1);const auto first=f.output.lights[0];
     assert(std::fabs(first.color[0]-.97254908f*.75f)<1e-6f&&first.position[2]==1); // never drawn, native RGB/visible/stamp all zero
     put(f.memory.object(f.models[0]),0xe4,7.f);put(f.memory.object(f.models[0]),0x178,.5f);f.update();assert(f.output.lights[0].id==first.id&&f.output.lights[0].position[0]==7&&std::fabs(f.output.lights[0].color[0]-first.color[0]*.5f)<1e-6f);
     f.memory.set(f.lights[0],0x64,0u);f.update();assert(!f.output.count);
     f.memory.set(f.lights[0],0x64,1u);put<std::uint16_t>(f.memory.object(f.bones[0]),0x48,0);f.update();assert(!f.output.count);
     put<std::uint16_t>(f.memory.object(f.bones[0]),0x48,1);f.update();assert(f.output.lights[0].color[0]>0);
     put(f.memory.object(f.models[0]),40,0u);put(f.memory.object(f.scene),8,0u);f.update();assert(f.output.valid&&!f.output.count&&!f.reader.modelCount());
    }
    {Fixture f;f.add();put(f.memory.object(f.models[0]),0x64,350u);f.update();assert(std::fabs(f.output.lights[0].color[0]-.97254908f*.625f)<1e-6f);}
    {Fixture f(modelAsset(),0x90000000);f.add();f.update();assert(f.output.valid&&f.output.count==1);} // LAA x86 allocations above 2 GB
    // Constant translations and rotation/scale about the emitter's exact pivot
    // are fixed points. Empty sequences must contribute their default transform.
    {Bytes b=modelAsset();authoredTrack(b,320,12,{{2,3,4}},3);authoredTrack(b,360,12,{{1,1,1,2,3,4}},3);
     Fixture f(b);f.add();f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==2&&f.output.lights[0].position[2]==5);
     put(b,648+4,.01f);Fixture moving(b);moving.add();moving.update();assert(moving.output.count==1&&std::fabs(moving.output.lights[0].position[0]-2.015f)<1e-5f);}
    for(unsigned flags:{0x200u,0x80u}){Bytes b=modelAsset();put(b,308,flags);authoredTrack(b,320,12,{{},{2,3,4}},3);
     Fixture f(b);f.add();f.update();assert(f.output.count==1&&f.output.lights[0].position[0]==2);}
    {Fixture f;f.add();put(f.memory.object(f.bones[0]),0x8c,0x200u);f.update();assert(f.output.count==1);}
    // Step visibility selects the new sequence immediately even while linear
    // colour/intensity tracks blend with the previous sequence.
    {Fixture f;f.add();auto& b=f.memory.object(f.bones[0]);put<std::uint16_t>(b,0x6c,0);put(b,0x70,100u);put(b,0x74,2100u);put(b,0x78,1.f);put(b,0x9c,1100u);put(b,0xa0,.001f);put(b,0xa4,1.f);
     f.update();assert(f.output.count==1&&f.output.lights[0].color[0]>0);}
    {Bytes b=modelAsset();authoredTrack(b,648+136,1,{{1,0},{1,0}},1);put<std::uint16_t>(b,648+136,1);Fixture f(b);f.add();f.update();assert(f.output.count==1); // byte keys remain step-valued even with a linear track type
     put(f.memory.object(f.scene),12,1100u);f.update();assert(!f.output.count);}
    // Global sequences use the model's loop origin, independently of a paused
    // ordinary sequence and the current camera or native animation stamp.
    {Bytes b=modelAsset();unsigned duration=1000;put(b,20,1u);put(b,24,append(b,&duration,4));put<std::int16_t>(b,648+76+2,0);
     Fixture f(b);f.add();put(f.memory.object(f.models[0]),0x64,350u);f.update();assert(std::fabs(f.output.lights[0].color[0]-.97254908f*.75f)<1e-6f);
     put(f.memory.object(f.scene),12,1100u);f.update();assert(std::fabs(f.output.lights[0].color[0]-.97254908f*.5f)<1e-6f);}
    // The native root may have no active sequence; stored clock and global
    // loops still drive light tracks before its first native animation/draw.
    {Fixture f;f.add();auto& b=f.memory.object(f.bones[0]);put<std::uint16_t>(b,0x48,0xffff);put<std::uint16_t>(b,0x44,1);put(b,0x40,500u);
     f.update();assert(f.output.count==1&&std::fabs(f.output.lights[0].color[0]-.97254908f*.75f)<1e-6f);}
    {Bytes b=modelAsset();unsigned duration=1000;put(b,20,1u);put(b,24,append(b,&duration,4));put<std::int16_t>(b,648+76+2,0);
     Fixture f(b);f.add();put<std::uint16_t>(f.memory.object(f.bones[0]),0x48,0xffff);put<std::uint16_t>(f.memory.object(f.bones[0]),0x44,1);f.update();assert(f.output.count==1);
     const auto before=f.output.lights[0].color[0];put(f.memory.object(f.scene),12,1100u);f.update();assert(f.output.lights[0].color[0]<before);}
    // Shared identity may be reused between frames; same-frame metadata reuse
    // must not hide a new relocated asset or reuse the old source identity.
    {Fixture f;f.add();f.add(4);f.update();const auto id=f.output.lights[0].id;Bytes replacement=modelAsset();put(replacement,648+4,2.f);const auto data=f.memory.next;relocate(replacement,data);f.memory.add(std::move(replacement));put(f.memory.object(f.shared),0x150,data);
     f.update();f.update();assert(f.output.count==2&&f.output.lights[0].id!=id&&f.output.lights[0].position[0]>=2);}
    {Fixture f;f.add();put(f.memory.object(f.models[0]),0xb4,NAN);f.update();assert(!f.output.count);}
    {Bytes b=modelAsset();put<std::uint16_t>(b,648+56,2);Fixture f(b);f.add();f.update();assert(!f.output.count);}
    {Fixture f;f.add();put(f.memory.object(f.bones[0]),0x88,1u);put(f.memory.object(f.bones[0]),0x8c,0x80u);f.update();assert(f.output.valid&&!f.output.count&&f.reader.failures);}
    {Fixture f;f.add();f.memory.blocks[0x834550][0]^=1;f.update();assert(!f.output.valid&&!f.output.count);}
    {Fixture f;f.add();f.memory.fail=f.lights[0];f.update();assert(!f.output.count&&f.reader.failures);f.memory.fail=0;f.update();assert(f.output.count==1);}
    {Fixture f;for(unsigned i=0;i<SceneDetail::DiscoverySteps+8;++i)f.add(float(i)*2);for(unsigned n=0;n<3;++n)f.update();assert(f.output.valid&&f.reader.scanned>SceneDetail::DiscoverySteps);
     bool found=false;for(unsigned i=0;i<f.output.count;++i)found|=f.output.lights[i].position[0]==0;assert(found);}
    // Cold metadata and cache churn cannot starve an already known light or
    // permanently stop discovery after the first four unique assets.
    {Fixture f;f.add();f.update();const auto id=f.output.lights[0].id;const auto originalData=f.data,originalShared=f.shared;
     f.add(5);for(unsigned i=0;i<SceneDetail::AssetLimit+12;++i){Bytes b=modelAsset();f.data=f.memory.next;relocate(b,f.data);f.memory.add(std::move(b));f.shared=f.memory.add(Bytes(0x154));put(f.memory.object(f.shared),0x150,f.data);f.add(1000.f+i);}
     f.data=originalData;f.shared=originalShared;
     for(unsigned frame=0;frame<50;++frame){f.update();assert(f.output.valid);bool kept=false;for(unsigned i=0;i<f.output.count;++i)kept|=f.output.lights[i].id==id;assert(kept);assert(f.reader.readCalls()<=4096&&f.reader.readBytes()<=1024*1024);}
     assert(f.output.count==2&&f.reader.assetCount()<=SceneDetail::AssetLimit);}
    {Fixture f;f.add();f.update();const auto id=f.output.lights[0].id;f.add(5);
     Bytes b=modelAsset();std::vector<std::vector<float>> keys(512,{1,1,1});authoredTrack(b,648+56,12,keys,3);
     f.data=f.memory.next;relocate(b,f.data);f.memory.add(std::move(b));f.shared=f.memory.add(Bytes(0x154));put(f.memory.object(f.shared),0x150,f.data);f.add(10);
     for(unsigned frame=0;frame<4;++frame){f.update();assert(f.output.valid);bool kept=false;for(unsigned i=0;i<f.output.count;++i)kept|=f.output.lights[i].id==id;assert(kept);}
     assert(f.output.count==3&&!f.reader.metadataLimited&&f.reader.readCalls()<=4096);}
    // Exhaust the runtime work budget with long stationary bone chains. The
    // reserved final scene check must still succeed, and unread entries stay
    // registered instead of being mistaken for despawned objects.
    {Fixture f;f.add();f.update();const auto id=f.output.lights[0].id;
     Bytes b=modelAsset(),bones(SceneDetail::MaxBones*88);for(unsigned i=0;i<SceneDetail::MaxBones;++i){std::memcpy(bones.data()+i*88,b.data()+304,88);put<std::int16_t>(bones,i*88+8,static_cast<std::int16_t>(i?i-1:0xffff));}
     put(b,44,SceneDetail::MaxBones);put(b,48,append(b,bones.data(),unsigned(bones.size())));put<std::uint16_t>(b,650,SceneDetail::MaxBones-1);
     f.data=f.memory.next;relocate(b,f.data);f.memory.add(std::move(b));f.shared=f.memory.add(Bytes(0x154));put(f.memory.object(f.shared),0x150,f.data);for(unsigned i=0;i<12;++i)f.add(5.f+i);
     unsigned registered=0;for(unsigned frame=0;frame<3;++frame){f.update();assert(f.output.valid&&f.reader.modelCount()>=registered);registered=f.reader.modelCount();assert(registered>1);bool kept=false;for(unsigned i=0;i<f.output.count;++i)kept|=f.output.lights[i].id==id;assert(kept);assert(f.reader.readCalls()<=4096&&f.reader.readBytes()<=1024*1024);}assert(f.reader.budgetLimited);}
    {Fixture f;f.add();f.add(5);const auto malformed=f.memory.add(Bytes(304));put(f.memory.object(malformed),0,0x3032444du);put(f.memory.object(malformed),4,264u);put(f.memory.object(malformed),44,1u);put(f.memory.object(malformed),48,0xfffffffcu);put(f.memory.object(malformed),264,1u);
     const auto shared=f.memory.add(Bytes(0x154));put(f.memory.object(shared),0x150,malformed);put(f.memory.object(f.models.back()),44,shared);f.update();assert(f.output.valid&&f.output.count==1&&f.reader.malformed);}
    // Real read-only MPQ models relocated like the client's M2 loader. Stand is
    // disabled; sequence149 is enabled. No original game file is modified.
    for(int arg=1;arg<argc;++arg){const bool pe=std::strcmp(argv[arg],"--exe")==0,parent=std::strcmp(argv[arg],"--parent")==0;if(pe||parent)assert(++arg<argc);
        std::ifstream in(argv[arg],std::ios::binary|std::ios::ate);assert(in);Bytes blob(std::size_t(in.tellg()));in.seekg(0);in.read(reinterpret_cast<char*>(blob.data()),std::streamsize(blob.size()));
        if(pe){const auto nt=get<unsigned>(blob,60);assert(get<unsigned>(blob,nt)==0x4550);const auto sections=get<std::uint16_t>(blob,nt+6),optional=get<std::uint16_t>(blob,nt+20);const auto base=get<unsigned>(blob,nt+24+28);assert(base==0x400000);
            const auto read=[&](unsigned address,void* out,std::size_t n){for(unsigned i=0;i<sections;++i){const unsigned at=nt+24+optional+i*40,rva=get<unsigned>(blob,at+12),size=get<unsigned>(blob,at+16),offset=get<unsigned>(blob,at+20);
                if(address>=base+rva&&address-base-rva<=size&&n<=size-(address-base-rva)&&offset+(address-base-rva)+n<=blob.size()){std::memcpy(out,blob.data()+offset+(address-base-rva),n);return true;}}return false;};
            assert(Reader::verify(read));std::puts("ACTUAL_CLIENT_LAYOUT_PROOFS_PASS read-only PE section mapping");continue;}
        if(parent){assert(++arg<argc);std::ifstream childFile(argv[arg],std::ios::binary|std::ios::ate);assert(childFile);Bytes child(std::size_t(childFile.tellg()));childFile.seekg(0);childFile.read(reinterpret_cast<char*>(child.data()),std::streamsize(child.size()));
            Fixture f(blob);const auto actor=f.add(10,0);for(unsigned i=0;i<get<unsigned>(f.memory.object(f.data),264);++i)f.memory.set(f.lights[0],i*SceneDetail::LightBytes+0x64,0u);f.useAsset(child);const auto light=f.add(3,0);put(f.memory.object(light),0x48,actor);put(f.memory.object(light),0x54,0u);
            f.update();assert(f.output.valid&&f.output.count>0&&f.output.lights[0].flags==0);const auto first=f.output.lights[0];put(f.memory.object(f.scene),12,1200u);f.update();assert(f.output.count>0&&f.output.lights[0].id==first.id);std::puts("REAL_NATIVE_ACTOR_ATTACHMENT_PASS parent60bones/43sequences + torch, no draw");
#ifdef _WIN32
            for(unsigned count:{16u,32u}){Fixture actual(blob);const auto root=actual.add(10,0);actual.useAsset(child);for(unsigned i=0;i<count;++i){const auto attached=actual.add(float(i)*.25f,0);put(actual.memory.object(attached),0x48,root);put(actual.memory.object(attached),0x54,0u);}
                actual.memory.system=true;actual.update();auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<200;++i){put(actual.memory.object(actual.scene),12,600+i*6);actual.update();assert(actual.output.count==count);}
                const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/200;std::printf("REAL_NATIVE_ACTOR_RPM_PASS children=%u parentBones=60 warmMs=%.4f reads=%u bytes=%zu\n",count,ms,actual.reader.readCalls(),actual.reader.readBytes());}
#endif
            continue;}
        const bool dalaran=std::strstr(argv[arg],"dalaran_")!=nullptr;Fixture f(blob);f.add(0,dalaran?3:0);bool lit=false,moved=false;Snapshot first;
        for(unsigned time:{100u,600u,1100u,1600u}){put(f.memory.object(f.scene),12,time);f.update();assert(f.output.valid);if(f.output.count){if(!lit)first=f.output;else for(unsigned k=0;k<3;++k)moved=moved||std::fabs(first.lights[0].position[k]-f.output.lights[0].position[k])>1e-5f;lit=true;}}
        assert(lit);if(dalaran){put<std::uint16_t>(f.memory.object(f.bones[0]),0x48,0);f.update();assert(!f.output.count);}else if(!std::strstr(argv[arg],"torch_missile"))assert(moved);
        std::printf("REAL_NATIVE_LAMP_PASS %s moving=%d\n",argv[arg],int(moved));}
#ifdef _WIN32
    for(unsigned count:{2u,16u,32u,128u,512u}){Fixture f;for(unsigned i=0;i<count;++i)f.add(float(i%16)*2);f.memory.system=true;
     auto start=std::chrono::steady_clock::now();f.update();const auto cold=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();const auto coldCalls=f.reader.readCalls();
     for(unsigned i=0;i<4;++i)f.update();start=std::chrono::steady_clock::now();for(unsigned i=0;i<200;++i){put(f.memory.object(f.scene),12,600+6*i);f.update();}const auto warm=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/200;
     assert(f.output.count==std::min(count,Capacity)&&f.reader.readCalls()<=4096&&f.reader.readBytes()<=1024*1024);
     std::printf("NATIVE_SCENE_RPM_PASS models=%u coldMs=%.4f coldReads=%u warmMs=%.4f warmReads=%u warmBytes=%zu\n",count,cold,coldCalls,warm,f.reader.readCalls(),f.reader.readBytes());}
    for(unsigned children:{1u,16u,32u}){Bytes b=modelAsset();authoredTrack(b,320,12,{{0,0,0,4,0,0}},3);Fixture f(b);const auto parent=f.add(10);f.memory.set(f.lights[0],0x64,0u);for(unsigned i=0;i<children;++i){const auto child=f.add(float(i));put(f.memory.object(child),0x48,parent);put(f.memory.object(child),0x54,0xffffu);put(f.memory.object(child),16,0x40001u);}
     f.memory.system=true;f.update();auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<200;++i){put(f.memory.object(f.scene),12,600+6*i);f.update();assert(f.output.count==children);}
     const auto warm=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/200;std::printf("NATIVE_SCENE_RPM_ATTACHED_PASS children=%u sharedParent=1 warmMs=%.4f warmReads=%u warmBytes=%zu\n",children,warm,f.reader.readCalls(),f.reader.readBytes());}
    for(bool animated:{false,true}){Bytes blob=modelAsset();if(animated)authoredTrack(blob,320,12,{{0,0,0,4,0,0}},3);Fixture f(blob);for(unsigned i=0;i<2048;++i)f.add(i<16?float(i)*2:1000.f+float(i));f.memory.system=true;double coldMax=0;unsigned coldMaxCalls=0;
     for(unsigned i=0;i<16;++i){auto start=std::chrono::steady_clock::now();f.update();coldMax=std::max(coldMax,std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());coldMaxCalls=std::max(coldMaxCalls,f.reader.readCalls());}
     assert(f.output.count==16&&f.reader.modelCount()==16);auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<200;++i){put(f.memory.object(f.scene),12,700+6*i);f.update();}
     const auto warm=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/200;
     std::printf("NATIVE_SCENE_RPM_DISTRIBUTION_PASS models=2048 nearby=16 animated=%d coldBatchMaxMs=%.4f coldMaxReads=%u warmMs=%.4f\n",int(animated),coldMax,coldMaxCalls,warm);}
#endif
    std::puts("NATIVE_SCENE_LIGHTS_PASS offscreen first discovery, clocks, current placement/alpha, enable transitions, detach, proof failure, unsafe bones, discovery continuation");
}
