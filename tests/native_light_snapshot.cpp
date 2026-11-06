#include "equipment_lights.h"
#include "particle_lights.h"
#include "snapshot_archive.h"
#include "native_light_bridge.h"
#include "model_light_attachments.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <limits>
#include <memory>

using namespace NorthlightNativeLights;
using namespace NorthlightStream;
namespace A=NorthlightStream::SnapshotArchive;
static Snapshot scene(){
    Snapshot s;s.valid=1;s.scene=0x1234000;s.time=1000;s.count=1;std::strcpy(s.map,"Northrend");
    auto& p=s.lights[0];p.id=0xfea0000000000001ull;p.position[0]=4;p.position[2]=2;
    p.anchor[0]=3;p.anchor[2]=2;p.flags=RootAnchor;
    p.color[0]=.7f;p.color[1]=.4f;p.color[2]=.1f;p.attenuation[1]=.7f;p.attenuation[2]=.03f;return s;
}
static void animatedDrawIdentity(){
    auto s=scene();float camera[3]{};NorthlightEquipmentLights::Tracker tracker;
    NorthlightEquipmentLights::Native drawn;std::copy_n(s.lights[0].position,3,drawn.position);
    std::copy_n(s.lights[0].attenuation,3,drawn.attenuation);
    // A previous-colour draw bank and a current scene sample represent one
    // emitter even when its hue crosses the old eight-percent match window.
    for(unsigned frame=0;frame<40;++frame){const unsigned now=1000+frame*50;
        drawn.color[0]=frame%2?.7f:.05f;drawn.color[1]=frame%2?.05f:.8f;drawn.color[2]=.1f;
        tracker.observe(drawn,now,{},camera);
        std::copy_n(drawn.color,3,s.lights[0].color);std::swap(s.lights[0].color[0],s.lights[0].color[1]);
        tracker.observeScene(s,now,{},camera);tracker.publish(now);
        assert(tracker.count==1&&tracker.entries[0].nativeId==s.lights[0].id);
        if(frame){assert(tracker.outputCount==1);const auto expected=NorthlightEquipmentLights::light(tracker.entries[0].source,tracker.entries[0].id,tracker.entries[0].gain);
            assert(tracker.output[0].diffuse[0]==expected.diffuse[0]&&tracker.output[0].diffuse[1]==expected.diffuse[1]);}
    }
    // Scene-first observation order and a reused scene identity retain one
    // source while its position moves; old identities retire immediately.
    const auto oldId=tracker.entries[0].id;
    for(unsigned frame=0;frame<10;++frame){const unsigned now=3000+frame*50;
        s.lights[0].position[0]+=.1f;std::copy_n(s.lights[0].position,3,drawn.position);
        tracker.observeScene(s,now,{},camera);tracker.observe(drawn,now,{},camera);tracker.publish(now);
        assert(tracker.count==1&&tracker.entries[0].id==oldId);
    }
    s.count=0;tracker.observeScene(s,3500,{},camera);tracker.publish(3500);assert(!tracker.count&&!tracker.outputCount);
    s.count=1;tracker.observeScene(s,3550,{},camera);tracker.publish(3550);tracker.publish(3600);
    assert(tracker.count==1&&tracker.outputCount==1&&tracker.entries[0].id!=oldId);
    // Two real scene lights may occupy the same point. Neither can claim an
    // anonymous observation solely because the other's hue changed.
    tracker.reset();s=scene();s.count=2;s.lights[1]=s.lights[0];++s.lights[1].id;
    s.lights[0].color[0]=1;s.lights[0].color[1]=s.lights[0].color[2]=0;
    s.lights[1].color[1]=1;s.lights[1].color[0]=s.lights[1].color[2]=0;
    drawn.color[0]=drawn.color[1]=0;drawn.color[2]=1;
    tracker.observe(drawn,4000,{},camera);tracker.observeScene(s,4000,{},camera);tracker.publish(4000);tracker.publish(4050);
    assert(tracker.count==3&&tracker.outputCount==3);
    for(unsigned i=0;i<2;++i)assert(std::count_if(tracker.entries.begin(),tracker.entries.begin()+tracker.count,[&](const auto& e){return e.nativeId==s.lights[i].id;})==1);
    // A distinct attenuation bank at the same point is also inconclusive.
    tracker.reset();s=scene();drawn.attenuation[1]=.9f;
    tracker.observe(drawn,5000,{},camera);tracker.observeScene(s,5000,{},camera);tracker.publish(5000);tracker.publish(5050);
    assert(tracker.count==2&&tracker.outputCount==2);
    std::puts("NATIVE_DRAW_IDENTITY_PASS changingHue=40 observationOrders=2 moving=10 reusedSceneIdentity=1 uniquePositionDenominator=1 colocatedSceneAmbiguity=1 differentDenominator=1");
}
static void publication(){
    auto s=scene();float camera[3]{};NorthlightEquipmentLights::Tracker native;
    NorthlightParticleLights::Frame particles;NorthlightLocalLightSelection::Tracker selected;
    // No observe(draw) call, even on discovery. Keep sampling longer than the
    // old native source's lifetime; camera orientation is deliberately absent.
    for(unsigned now=1000;now<=6000;now+=50){
        native.observeScene(s,now,{},camera);native.publish(now);
        particles.publish(native.output.data(),native.outputCount,{},camera,selected);
        const auto result=selected.update({},camera,32,.05f,false,nullptr,particles.output.data(),particles.outputCount);
        if(now>=1100){assert(result.count==1);assert(result.color[0][0]>.6f);}
    }
    const auto identity=native.output[0].sourceId;
    // Large placement and hue changes retain identity; a draw reporting the
    // new hue early cannot create a second source at the same place.
    s.lights[0].position[0]=12;s.lights[0].color[0]=.05f;s.lights[0].color[1]=.8f;
    NorthlightEquipmentLights::Native drawn;std::copy_n(s.lights[0].position,3,drawn.position);
    std::copy_n(s.lights[0].color,3,drawn.color);std::copy_n(s.lights[0].attenuation,3,drawn.attenuation);
    native.observe(drawn,6050,{},camera);native.observeScene(s,6050,{},camera);native.publish(6050);
    assert(native.outputCount==1&&native.output[0].sourceId==identity&&native.output[0].position[0]==12);
    // Authored disable and detachment expire the source in this publication,
    // including the shared selector's old fade tail.
    unsigned now=6100;
    for(bool removed:{false,true}){
        auto dark=s;if(removed)dark.count=0;else std::fill_n(dark.lights[0].color,3,0.f);
        native.observeScene(dark,now,{},camera);native.publish(now);
        assert(native.outputCount==0&&native.suppressedCount==1);
        auto ids=native.suppressed;std::sort(ids.begin(),ids.begin()+native.suppressedCount);
        selected.forgetSorted(ids.data(),native.suppressedCount);
        particles.publish(nullptr,0,{},camera,selected);
        assert(selected.update({},camera,32,.016f,true,nullptr,particles.output.data(),particles.outputCount).count==0);
        native.observeScene(s,now+100,{},camera);native.publish(now+100);native.observeScene(s,now+200,{},camera);native.publish(now+200);now+=300;
        assert(native.outputCount==1);
    }
    auto broken=s;broken.count=129;assert(!wellFormed(broken));broken=s;broken.lights[0].color[0]=std::numeric_limits<float>::quiet_NaN();assert(!wellFormed(broken));
    std::puts("NATIVE_LIGHT_PUBLICATION_PASS neverDrawn=1 longOffscreen=1 movement=1 hue=1 disable=1 detach=1");
}
static void cachedAttachment(){
    auto s=scene();NorthlightEquipmentLights::Native point;
    std::copy_n(s.lights[0].position,3,point.position);std::copy_n(s.lights[0].color,3,point.color);std::copy_n(s.lights[0].attenuation,3,point.attenuation);
    auto lamp=NorthlightEquipmentLights::light(point,77,1);lamp.sourceId=77;lamp.flags=NorthlightLocalLights::CastShadow;
    std::copy_n(s.lights[0].anchor,3,lamp.position);auto unrelated=lamp;unrelated.sourceId=88;unrelated.position[0]=50;
    const std::vector<NorthlightLocalLights::Light> fixed{lamp,unrelated};std::vector<NorthlightLocalLights::Light> moved;
    auto records=std::make_shared<NorthlightLightAttachments::Records>();records->push_back({});records->back().sourceId=77;
    NorthlightLightAttachments::Tracker attachments;attachments.bind(records);attachments.observeScene(s,fixed);
    assert(!attachments.needsDrawPose()); // Current native poses must not force extra model replay capture.
    auto output=attachments.apply(fixed,moved);assert(output.size()==2&&output[0].position[0]==4&&output[1].position[0]==50);
    assert(output[0].sourceId==77&&output[0].flags==lamp.flags&&output[0].attenuationEnd==lamp.attenuationEnd);
    NorthlightEquipmentLights::Tracker native;float camera[3]{};native.observeScene(s,1000,output,camera);native.publish(1100,&output);assert(!native.outputCount);
    s.lights[0].position[0]=20;attachments.observeScene(s,fixed);assert(attachments.apply(fixed,moved)[0].position[0]==20&&fixed[0].position[0]==3);
    s.count=0;attachments.observeScene(s,fixed);output=attachments.apply(fixed,moved);assert(output.size()==1&&output[0].sourceId==88);
    s=scene();s.lights[0].id+=10;attachments.observeScene(s,fixed);output=attachments.apply(fixed,moved);assert(output.size()==2&&output[0].position[0]==4);
    s.count=0;attachments.observeScene(s,fixed);attachments.releaseScene();assert(attachments.apply(fixed,moved).size()==2&&attachments.needsDrawPose());
    attachments.reset();attachments.bind(records);s=scene();s.lights[0].flags=0;attachments.observeScene(s,fixed);
    assert(attachments.apply(fixed,moved)[0].position[0]==3); // An attached child cannot claim a root world placement.
    s=scene();const std::vector<NorthlightLocalLights::Light> reversed{unrelated,lamp};attachments.observeScene(s,reversed);
    output=attachments.apply(reversed,moved);assert(output[0].sourceId==88&&output[1].sourceId==77&&output[1].position[0]==4);
    attachments.prune({unrelated},camera);assert(attachments.nativeBindings.empty());
    // Two bones can share the same authored anchor and hue before animation.
    attachments.reset();auto pairRecords=std::make_shared<NorthlightLightAttachments::Records>(*records);
    pairRecords->push_back({});pairRecords->back().sourceId=88;attachments.bind(pairRecords);
    auto twin=lamp;twin.sourceId=88;const std::vector<NorthlightLocalLights::Light> pair{lamp,twin};
    s=scene();s.count=2;s.lights[1]=s.lights[0];++s.lights[1].id;s.lights[1].position[0]=9;
    for(unsigned i=0;i<2;++i){attachments.observeScene(s,pair);output=attachments.apply(pair,moved);
        assert(output.size()==2&&output[0].position[0]==4&&output[1].position[0]==9);}
    std::puts("NATIVE_FGA_PASS offscreenPose=1 noDuplicate=1 lifecycle=1 sourceIdentityRangeShadowPreserved=1");
}
static void attachmentCost(){
    // A broad static cache and many unrelated native effects must not require
    // per-source tree searches for every distant FGL entry.
    auto s=scene();s.count=32;std::vector<NorthlightLocalLights::Light> fixed(4096),moved;
    auto records=std::make_shared<NorthlightLightAttachments::Records>();
    for(unsigned i=0;i<fixed.size();++i){auto& lamp=fixed[i];lamp.kind=2;lamp.sourceId=i+1;lamp.position[0]=float(i*10);lamp.diffuse[0]=1;
        if(i<256){records->push_back({});records->back().sourceId=lamp.sourceId;}}
    for(unsigned i=0;i<s.count;++i){s.lights[i]=scene().lights[0];s.lights[i].id+=i;s.lights[i].anchor[0]=-float(i+1);}
    NorthlightLightAttachments::Tracker tracker;tracker.bind(records);tracker.observeScene(s,fixed);
    const auto start=std::chrono::steady_clock::now();
    for(unsigned i=0;i<100;++i){tracker.observeScene(s,fixed);assert(tracker.apply(fixed,moved).size()==fixed.size());}
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/100;
    assert(tracker.nativeBindings.empty());std::printf("NATIVE_FGA_CPU static=4096 unmatched=32 ms=%.4f\n",elapsed);
}
int main(int argc,char** argv){
    assert(argc==2);animatedDrawIdentity();publication();cachedAttachment();attachmentCost();auto source=std::make_unique<GameSnapshot>(),target=std::make_unique<GameSnapshot>();
    unsigned value=42;assert(source->record(0x123456,&value,4,true));source->triggerKind=2;source->triggerDraw=123;
    source->nativeLights=scene();std::vector<unsigned char> bytes(A::MaxBytes);std::size_t written=0;A::Error error;
    assert(A::encode(*source,bytes.data(),bytes.size(),written,error));bytes.resize(written);assert(A::u32(bytes.data()+8)==3);
    assert(A::decode(bytes.data(),bytes.size(),*target,error));
    assert(target->nativeLights.count==1&&target->nativeLights.lights[0].id==source->nativeLights.lights[0].id);
    source->nativeLights.lights[0].position[0]=999;source->nativeLights.lights[0].anchor[0]=999;
    assert(target->nativeLights.lights[0].position[0]==4&&target->nativeLights.lights[0].anchor[0]==3&&target->nativeLights.lights[0].flags==RootAnchor);
    {ScopedPlayback play(*target);assert(activeSnapshot->nativeLights.lights[0].color[0]==.7f);
#ifdef _WIN32
        Snapshot output;float camera[3]{};const auto reads=NorthlightWorldContext::SelfReadStats::calls.load();
        frame("Northrend",camera,output);assert(output.valid&&output.lights[0].position[0]==4);
        frame("Azeroth",camera,output);assert(!output.valid);
        assert(NorthlightWorldContext::SelfReadStats::calls.load()==reads);
#endif
    }
    std::ofstream file(argv[1],std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());file.close();assert(file.good());
    {source->nativeLights=target->nativeLights;auto& point=source->nativeLights.lights[0];point.id=0xfea0000000000001ull;point.flags=RootAnchor|WorldFixture;point.reserved=2;
        std::vector<unsigned char> tagged(A::MaxBytes);std::size_t size=0;assert(A::encode(*source,tagged.data(),tagged.size(),size,error)&&A::decode(tagged.data(),size,*target,error));
        assert(target->nativeLights.lights[0].reserved==2&&target->nativeLights.lights[0].flags==(RootAnchor|WorldFixture));
        std::ofstream fixture(std::string(argv[1])+".fixture",std::ios::binary);fixture.write(reinterpret_cast<const char*>(tagged.data()),size);assert(fixture);}
    // Corrupt count/map/value with a valid checksum: strict import rejects it.
    const auto extension=A::HeaderBytes+A::RecordBytes+4;
    for(auto change:std::array<std::pair<std::size_t,std::uint32_t>,5>{{{extension+8,129},{extension+12,2},{extension+80+8,0x7fc00000},{extension+80+48,0x7fc00000},{extension+80+60,2}}}){
        auto bad=bytes;A::put32(bad.data()+change.first,change.second);A::put32(bad.data()+bad.size()-4,A::checksum(bad.data(),bad.size()-4));
        assert(!A::decode(bad.data(),bad.size(),*target,error)&&!target->nativeLights.valid);
    }
    auto v2=bytes;v2.erase(v2.begin()+extension+80+48,v2.begin()+extension+80+64);
    A::put32(v2.data()+8,2);A::put32(v2.data()+12,unsigned(v2.size()));A::put32(v2.data()+v2.size()-4,A::checksum(v2.data(),v2.size()-4));
    assert(A::decode(v2.data(),v2.size(),*target,error)&&target->nativeLights.count==1&&!target->nativeLights.lights[0].flags);
    target->clear();assert(!target->nativeLights.valid&&!target->nativeLights.count);
    source->nativeLights.clear();bytes.resize(A::MaxBytes);assert(A::encode(*source,bytes.data(),bytes.size(),written,error));
    assert(A::u32(bytes.data()+8)==1&&A::decode(bytes.data(),written,*target,error)&&!target->nativeLights.valid);
    std::puts("NATIVE_LIGHT_SNAPSHOT_PASS ownedValues=1 v1Compatible=1 v2Compatible=1 v3RoundTrip=1 invalidRejected=1");
}
