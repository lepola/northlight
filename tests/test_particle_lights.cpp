#include "particle_lights.h"
#include "particle_uploads.h"
#include <cassert>
#include <iostream>
using namespace NorthlightParticleLights;
int main(){
    {
        float lo[3]={0,0,0},hi[3]={1,1,1};
        assert(compactMesh(lo,hi,2,2));assert(!compactMesh(lo,hi,1,2));
        assert(!compactMesh(lo,hi,17,17));assert(!compactMesh(lo,hi,0,0));
        hi[0]=4;assert(!compactMesh(lo,hi,2,2));hi[0]=NAN;assert(!compactMesh(lo,hi,2,2));
        float camera[3]={};Frame mesh;NorthlightLocalLightSelection::Tracker selected;std::vector<Light> fixed;
        Sample herb{{0,0,2},{.2f,.4f,.8f},0,true};
        mesh.observe(herb,camera);mesh.publish(nullptr,0,fixed,camera,selected);
        assert(mesh.outputCount==1&&(mesh.output[0].flags&NorthlightLocalLights::CastShadow));
        const auto id=mesh.output[0].sourceId;
        mesh.nextFrame();herb.castsShadows=false;mesh.observe(herb,camera);mesh.publish(nullptr,0,fixed,camera,selected);
        assert(mesh.outputCount==1&&mesh.output[0].sourceId==id&&!mesh.output[0].flags);
        mesh.nextFrame();herb.castsShadows=true;mesh.observe(herb,camera);herb.castsShadows=false;mesh.observe(herb,camera);
        mesh.publish(nullptr,0,fixed,camera,selected);assert(mesh.outputCount==1&&!mesh.output[0].flags);
        mesh.nextFrame();mesh.publish(nullptr,0,fixed,camera,selected);assert(!mesh.outputCount);
    }
    std::uint64_t indices[3],v=0;
    assert(triangleIndices(4,3,indices)&&indices[0]==9&&indices[2]==11);
    assert(triangleIndices(5,3,indices)&&indices[0]==3&&indices[2]==5);
    assert(triangleIndices(6,3,indices)&&indices[0]==0&&indices[2]==5);
    assert(!triangleIndices(1,0,indices)&&!triangleIndices(4,UINT32_MAX,indices));
    Draw d{true,-10,10,4,123};
    assert(d.vertex(0,10,v)&&v==0);assert(!d.vertex(0,14,v)&&!d.vertex(0,9,v));
    d.base=-11;assert(!d.vertex(0,10,v));d={false,0,0,0,UINT32_MAX};assert(!d.vertex(1,0,v));
    float camera[3]={0,0,0};std::vector<Light> fixed;
    NorthlightLocalLightSelection::Tracker selection;Frame frame;
    Sample blue{{0,0,4},{.04f,.12f,.7f}};
    frame.observe(blue,camera);blue.position[0]=.2f;frame.observe(blue,camera);
    frame.publish(nullptr,0,fixed,camera,selection);assert(frame.outputCount==1&&frame.immediateCount==1);
    const auto id=frame.output[0].sourceId;assert(frame.output[0].diffuse[2]>frame.output[0].diffuse[0]*5);
    auto lit=selection.update(fixed,camera,32,1.f/120,true,nullptr,frame.output.data(),frame.outputCount,frame.immediate.data(),frame.immediateCount);
    assert(lit.count==1&&std::fabs(lit.color[0][2]-frame.output[0].diffuse[2])<1e-6f);
    frame.nextFrame();blue.position[0]=.3f;frame.observe(blue,camera);frame.publish(nullptr,0,fixed,camera,selection);
    assert(frame.output[0].sourceId==id);
    frame.nextFrame();frame.publish(nullptr,0,fixed,camera,selection);
    lit=selection.update(fixed,camera,32,1.f/120,true,nullptr,frame.output.data(),frame.outputCount,frame.immediate.data(),frame.immediateCount);
    assert(lit.count==0&&selection.count==0); // no 250ms TTL or 400ms fade tail
    frame.reset();frame.observe(blue,camera);Light native{};
    for(unsigned k=0;k<3;++k){native.position[k]=blue.position[k];native.diffuse[k]=blue.rgb[k];}
    native.kind=2;native.attenuationStart=.5f;native.attenuationEnd=8;native.sourceId=0xfec0000000000001ull;
    frame.publish(&native,1,fixed,camera,selection);assert(frame.outputCount==1&&frame.immediateCount==1);
    frame.reset();for(unsigned i=0;i<40;++i){blue.position[0]=float(i)*2;frame.observe(blue,camera);}
    frame.publish(nullptr,0,fixed,camera,selection);assert(frame.outputCount<=8);
    frame.reset();blue.position[0]=0;frame.observe(blue,camera);native.position[0]=0;fixed.push_back(native);
    frame.publish(nullptr,0,fixed,camera,selection);assert(frame.outputCount==0);
    // Samples of different hues at one place are one light: the average colour at the strongest sample's level.
    {fixed.clear();frame.reset();Sample white{{0,0,4},{.5f,.5f,.5f}},pale=blue;pale.rgb[0]=.3f;
     frame.observe(blue,camera);frame.observe(white,camera);frame.observe(pale,camera);frame.publish(nullptr,0,fixed,camera,selection);
     assert(frame.outputCount==1&&std::fabs(peak(frame.output[0].diffuse)-.7f*.7f)<1e-5f&&frame.output[0].diffuse[2]>frame.output[0].diffuse[0]);}
    // Continuous frames: a source shows this frame's samples only and keeps its id while it is drawn.
    const float dt=1.f/170,goal=blue.rgb[2]*.7f;fixed.clear();frame.reset();
    frame.observe(blue,camera);frame.publish(nullptr,0,fixed,camera,selection,dt);
    assert(frame.outputCount==1&&std::fabs(frame.output[0].diffuse[2]-goal)<1e-6f);const auto flash=frame.output[0].sourceId;
    for(unsigned f=0;f<20;++f){frame.nextFrame();frame.observe(blue,camera);frame.publish(nullptr,0,fixed,camera,selection,dt);}
    assert(frame.outputCount==1&&frame.output[0].sourceId==flash);
    frame.nextFrame();frame.publish(nullptr,0,fixed,camera,selection,dt);assert(frame.outputCount==0); // no release tail
    for(unsigned f=0;f<170;++f){frame.nextFrame();frame.publish(nullptr,0,fixed,camera,selection,dt);}assert(frame.outputCount==0);

    NorthlightParticleUploads::Cache uploads;int key=0;unsigned in[4]={1,2,3,4},out[4]={};
    uploads.write(&key,1,16,in,sizeof in,false);assert(uploads.read(&key,16,sizeof out,out)&&out[3]==4);
    uploads.invalidate(&key,20,4);assert(!uploads.read(&key,16,4,out));
    uploads.write(&key,1,16,in,sizeof in,true);assert(uploads.read(&key,20,4,out)&&out[0]==2);
    uploads.invalidate(&key,0,4,true);assert(!uploads.read(&key,20,4,out));
    uploads.write(&key,2,0,in,8,false);uploads.write(&key,2,8,in+2,8,false);
    assert(uploads.read(&key,0,16,out)&&out[3]==4); // adjacent known writes
    uploads.write(&key,3,0,in,4,false);assert(!uploads.read(&key,4,4,out)); // reused lifetime
    uploads.forget(&key);assert(!uploads.read(&key,0,4,out));
    assert(!uploads.read(&key,UINT32_MAX,4,out));
    std::vector<unsigned char> large(NorthlightParticleUploads::MaxChunk,42);
    std::array<int,24> keys{};uploads.clear();
    for(unsigned i=0;i<keys.size();++i)uploads.write(&keys[i],1,0,large.data(),unsigned(large.size()),false);
    assert(uploads.bytes()<=NorthlightParticleUploads::ByteBudget);
    uploads.nextFrame();for(unsigned i=unsigned(keys.size());i>0;--i)uploads.write(&keys[i-1],1,0,large.data(),unsigned(large.size()),false);
    assert(uploads.bytes()<=NorthlightParticleUploads::ByteBudget&&uploads.stats().evictions>0);
    uploads.clear();assert(uploads.bytes()==0);
    std::cout<<"PASS particle lights: actual colour, compact clustering (one light per place), indexed bounds, shared cap/dedup, instantaneous lifecycle without a frame clock, no fade with one, and bounded replay upload bytes\n";
}
