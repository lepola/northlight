#include "native_scene_light_fixture.h"
#include "model_light_attachments.h"
#include "particle_lights.h"
#include "point_light_shadow.h"
#include <cstdlib>
#include <new>

static std::size_t allocations=0;
void* operator new(std::size_t size){if(void* p=std::malloc(size?size:1)){++allocations;return p;}throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}

static Bytes readBytes(const char* path){std::ifstream file(path,std::ios::binary|std::ios::ate);assert(file);Bytes b(std::size_t(file.tellg()));file.seekg(0);file.read(reinterpret_cast<char*>(b.data()),std::streamsize(b.size()));assert(file);return b;}
static bool eligible(const NorthlightLocalLights::Light& light){return NorthlightPointShadow::eligible(light);}
static void translatedProof(){
    using namespace NorthlightLightAttachments;
    auto records=std::make_shared<Records>();Attachment proof{};proof.sourceId=42;proof.flags=NativeSource;proof.point[2]=.2f;proof.offset[2]=1;records->push_back(proof);
    NorthlightLocalLights::Light reference{{2,3,5},{1,.5f,.25f},1,12,42,2,NorthlightLocalLights::CastShadow|NorthlightLocalLights::RequiresNative};
    std::vector<NorthlightLocalLights::Light> fixed{reference},output;Tracker tracker;tracker.bind(records);
    Snapshot scene;scene.valid=1;scene.scene=1;scene.count=1;auto& point=scene.lights[0];point.id=0xfea0000000000001ull;point.flags=RootAnchor;point.anchor[2]=1;
    point.position[0]=2;point.position[1]=3;point.position[2]=5;point.color[0]=.5f;point.color[1]=.25f;point.color[2]=.125f;point.attenuation[1]=.7f;point.attenuation[2]=.03f;
    tracker.observeScene(scene,fixed);const auto& applied=tracker.apply(fixed,output);assert(applied.size()==1&&applied[0].sourceId==42&&eligible(applied[0])&&std::fabs(applied[0].position[2]-5.2f)<1e-5f&&tracker.claimedNativeCount==1);
    // Coincident sources retain the authored native record index. An unknown
    // duplicate placement cannot be assigned by hue/proximity alone.
    point.id++;tracker.releaseScene();tracker.observeScene(scene,fixed);assert(tracker.apply(fixed,output).empty());point.id--;
    auto duplicate=proof;duplicate.sourceId=43;records->push_back(duplicate);auto second=reference;second.sourceId=43;fixed.push_back(second);tracker.reset();tracker.bind(records);tracker.observeScene(scene,fixed);assert(tracker.apply(fixed,output).empty());
}
int main(int argc,char** argv){
    if(argc==3&&!std::strcmp(argv[1],"--invalid-catalog")){assert(!NorthlightNativeFixtures::load(argv[2]));return 0;}
    translatedProof();
    assert(argc==6);const Bytes blob=readBytes(argv[1]);const bool dalaran=std::strstr(argv[1],"dalaran")!=nullptr;
    const auto catalog=NorthlightNativeFixtures::load(argv[2]);assert(catalog);
    std::vector<NorthlightLocalLights::Light> fixed,effective;
    if(std::strcmp(argv[3],"-"))assert(NorthlightLocalLights::readFile(argv[3],fixed));
    auto records=std::make_shared<NorthlightLightAttachments::Records>();
    if(std::strcmp(argv[4],"-"))assert(NorthlightLightAttachments::readFile(argv[4],*records));
    Fixture f(blob);f.reader.fixtures(catalog);f.add(0,dalaran?3:0);
    if(!dalaran){assert(!records->empty());const auto& m=records->front().placement;
        for(unsigned row=0;row<3;++row)for(unsigned k=0;k<3;++k)put(f.memory.object(f.models[0]),0xb4+row*16+k*4,m[row*3+k]);
        for(unsigned k=0;k<3;++k){put(f.memory.object(f.models[0]),0xe4+k*4,m[9+k]);f.camera[k]=m[9+k];}}
    const auto vertexAt=get<unsigned>(f.memory.object(f.data),64);f.memory.watchBegin=vertexAt;f.memory.watchEnd=vertexAt+get<unsigned>(blob,60)*48;
    f.update();assert(f.output.valid&&f.output.count==1);const auto original=f.output.lights[0];
    if(dalaran){assert(original.flags==(RootAnchor|WorldFixture)&&fixed.empty());assert(f.memory.watchedReads==1);}
    NorthlightLightAttachments::Tracker attachments;attachments.bind(records);
    NorthlightEquipmentLights::Tracker bridge;NorthlightParticleLights::Frame particles;NorthlightLocalLightSelection::Tracker selection;
    NorthlightLocalLightSelection::Selection selected;
    for(unsigned frame=0;frame<30;++frame){const unsigned now=1000+frame*20;put(f.memory.object(f.scene),12,now);f.update();
        attachments.observeScene(f.output,fixed);const auto& lights=attachments.apply(fixed,effective);attachments.forgetInactive(selection);
        bridge.observeScene(f.output,now,lights,f.camera,attachments.claimedNative.data(),attachments.claimedNativeCount);bridge.publish(now,&lights);
        selection.forgetSorted(bridge.suppressed.data(),bridge.suppressedCount);
        particles.publish(bridge.output.data(),bridge.outputCount,lights,f.camera,selection);
        selected=selection.update(lights,f.camera,64,.02f,true,nullptr,particles.output.data(),particles.outputCount);
        if(frame)assert(selected.count==1&&eligible(selected.sources[0]));
    }
    const auto light=selected.sources[0];assert(light.flags==NorthlightLocalLights::CastShadow&&light.attenuationEnd==12);
    {const auto path=std::filesystem::path(argv[5]).parent_path()/"native-snapshot.bin";std::ofstream snapshot(path,std::ios::binary);snapshot.write(reinterpret_cast<const char*>(&f.output),sizeof f.output);assert(snapshot);}
    const auto warmReads=f.memory.watchedReads;
    for(unsigned i=0;i<100;++i)f.update();assert(f.memory.watchedReads==warmReads);
    if(dalaran){
        // No FGL placement, palette draw or visibility stamp participates.
        assert(bridge.outputCount==1&&attachments.claimedNativeCount==0);
        assert(bridge.fixture(light.sourceId)&&bridge.fixture(light.sourceId)->fixtureTag==original.reserved);
        {NorthlightEquipmentLights::Native draw;std::copy_n(original.position,3,draw.position);std::copy_n(original.color,3,draw.color);std::copy_n(original.attenuation,3,draw.attenuation);bridge.observe(draw,1580,{});assert(bridge.fixture(light.sourceId));}
        const auto id=bridge.output[0].sourceId;put(f.memory.object(f.scene),12,1600u);f.update();bridge.observeScene(f.output,1600,{},f.camera);bridge.publish(1600);
        assert(bridge.outputCount==1&&bridge.output[0].sourceId==id&&bridge.output[0].flags==light.flags&&bridge.output[0].attenuationEnd==light.attenuationEnd);
        put(f.memory.object(f.models[0]),0x178,.25f);f.update();bridge.observeScene(f.output,1605,{},f.camera);bridge.publish(1605);assert(bridge.output[0].sourceId==id&&bridge.output[0].flags==light.flags&&bridge.output[0].attenuationEnd==light.attenuationEnd&&bridge.output[0].diffuse[0]<light.diffuse[0]);
        put<std::uint16_t>(f.memory.object(f.bones[0]),0x48,0);f.update();assert(!f.output.count);bridge.observeScene(f.output,1620,{},f.camera);bridge.publish(1620);assert(!bridge.outputCount);
        // Geometry, internal name and authored structure are independent proof
        // inputs. Native light survives a stale catalog without shadow intent.
        const unsigned name=get<unsigned>(blob,12),vertices=get<unsigned>(blob,64),bones=get<unsigned>(blob,48);
        for(unsigned at:{name,vertices,bones+76}){Bytes changed=blob;changed[at]^=1;Fixture negative(changed);negative.reader.fixtures(catalog);negative.add(0,3);negative.update();assert(negative.output.count==1&&negative.output.lights[0].flags==RootAnchor);}
        {Fixture negative(blob);negative.add(0,3);negative.update();assert(negative.output.count==1&&negative.output.lights[0].flags==RootAnchor);}
        {Fixture changed(blob);changed.reader.fixtures(catalog);changed.add(0,3);changed.update();NorthlightEquipmentLights::Tracker tracker;tracker.observeScene(changed.output,1000,{},changed.camera);tracker.publish(1000);tracker.observeScene(changed.output,1100,{},changed.camera);tracker.publish(1100);assert(tracker.outputCount==1&&eligible(tracker.output[0]));const auto identity=tracker.output[0].sourceId;
            changed.reader.fixtures({});changed.update();tracker.observeScene(changed.output,1200,{},changed.camera);tracker.publish(1200);assert(tracker.outputCount==1&&tracker.output[0].sourceId==identity&&!eligible(tracker.output[0])&&!tracker.fixture(identity));}
        for(unsigned pointerAt:{12u,64u}){Fixture negative(blob);negative.reader.fixtures(catalog);negative.add(0,3);put(negative.memory.object(negative.data),pointerAt,0x60000000u);negative.update();assert(negative.output.count==1&&negative.output.lights[0].flags==RootAnchor&&negative.reader.failures);
            const auto failed=negative.reader.failures;negative.update();assert(negative.output.count==1&&negative.reader.failures==failed);}
        {Fixture negative(blob);negative.reader.fixtures(catalog);const auto parent=negative.add(0,3);negative.memory.set(negative.lights[0],0x64,0u);const auto child=negative.add(3,3);put(negative.memory.object(child),0x48,parent);put(negative.memory.object(child),0x54,0xffffu);put(negative.memory.object(child),16,0x40001u);negative.update();assert(negative.output.count==1&&negative.output.lights[0].flags==0);}
        {Fixture negative(blob);negative.reader.fixtures(catalog);negative.add(NorthlightEquipmentLights::MaxDistance+20,3);negative.update();assert(!negative.output.count);}
        {Bytes changed=blob;const auto at=get<unsigned>(changed,268);put<std::uint16_t>(changed,at+76,2);Fixture negative(changed);negative.reader.fixtures(catalog);negative.add(0,3);negative.update();assert(!negative.output.count);}
#ifdef _WIN32
        if(std::strstr(argv[1],"lamppost")){
            Fixture baseline(blob),qualified(blob);qualified.reader.fixtures(catalog);
            for(unsigned i=0;i<128;++i){baseline.add(float(i%16)*2,3);qualified.add(float(i%16)*2,3);}
            baseline.memory.system=qualified.memory.system=true;for(unsigned i=0;i<4;++i){baseline.update();qualified.update();}
            assert(baseline.output.count==128&&qualified.output.count==128&&baseline.reader.readCalls()==qualified.reader.readCalls()&&baseline.reader.readBytes()==qualified.reader.readBytes());
            NorthlightEquipmentLights::Tracker population,oldPopulation;for(unsigned i=0;i<3;++i){population.observeScene(qualified.output,1000+i*100,{},qualified.camera);population.publish(1000+i*100);oldPopulation.observeScene(baseline.output,1000+i*100,{},baseline.camera);oldPopulation.publish(1000+i*100);}assert(population.outputCount==128&&oldPopulation.outputCount==128);
            const auto before=allocations;auto start=std::chrono::steady_clock::now();
            for(unsigned i=0;i<200;++i){baseline.update();oldPopulation.observeScene(baseline.output,1400+i*10,{},baseline.camera);oldPopulation.publish(1400+i*10);}
            const auto baselineMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/200;
            start=std::chrono::steady_clock::now();for(unsigned i=0;i<200;++i){qualified.update();population.observeScene(qualified.output,1400+i*10,{},qualified.camera);population.publish(1400+i*10);}
            const auto qualifiedMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/200;
            assert(allocations==before&&qualified.reader.readCalls()<=4096&&qualified.reader.readBytes()<=1024*1024);
            std::printf("NATIVE_WORLD_LAMP_WARM_PASS sources=128 baselineReaderAndBridgeMs=%.4f qualifiedReaderAndBridgeMs=%.4f reads=%u bytes=%zu allocations=0\n",baselineMs,qualifiedMs,qualified.reader.readCalls(),qualified.reader.readBytes());
        }
#endif
    }else{
        assert(attachments.claimedNativeCount==1&&bridge.outputCount==0&&light.sourceId==fixed[0].sourceId);
        Snapshot absent;absent.valid=1;absent.scene=1;attachments.observeScene(absent,fixed);const auto& fallback=attachments.apply(fixed,effective);
        assert(fallback.size()==1&&fallback[0].sourceId==light.sourceId&&eligible(fallback[0]));
    }
    std::ofstream out(argv[5],std::ios::binary);const std::uint32_t header[]={2,sizeof(light),1};out.write("FGL1",4);out.write(reinterpret_cast<const char*>(header),12);out.write(reinterpret_cast<const char*>(&light),sizeof light);assert(out);
    std::printf("NATIVE_WORLD_LAMP_PASS model=%s noPlacement=%u nativeFlags=%u source=%llu flags=%u range=%.3f rgb=%.6f,%.6f,%.6f position=%.6f,%.6f,%.6f coldVertexReads=%u warmVertexReads=0\n",argv[1],unsigned(dalaran),original.flags,(unsigned long long)light.sourceId,light.flags,light.attenuationEnd,light.diffuse[0],light.diffuse[1],light.diffuse[2],light.position[0],light.position[1],light.position[2],warmReads);
}
