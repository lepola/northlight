#include "native_direct_ownership.h"
#include <cassert>
#include <cstdio>
int main(){
    using namespace NorthlightNativeDirect;
    NorthlightNativeLights::Snapshot scene;scene.valid=1;scene.scene=1;scene.count=1;auto& point=scene.lights[0];point.id=1;
    point.position[0]=-8564.9423828125f;point.position[1]=1011.7713012695312f;point.position[2]=99.7315444946289f;
    point.color[0]=4;point.color[1]=2.2f;point.color[2]=.8f;point.attenuation[1]=.7f;point.attenuation[2]=.03f;
    NorthlightLocalLights::Light raw{};std::copy_n(point.position,3,raw.position);std::copy_n(point.color,3,raw.diffuse);raw.kind=2;raw.flags=2;raw.sourceId=14933062424609180460ull;
    const auto light=NorthlightLocalLights::nativeTransport(raw,point.attenuation);
    assert(source(light,scene)==&point);
    NorthlightEquipmentLights::Native value;std::copy_n(point.position,3,value.position);std::copy_n(point.color,3,value.color);std::copy_n(point.attenuation,3,value.attenuation);
    assert(slot(value,scene)==&point);
    auto changed=light;changed.kind=3;assert(!source(changed,scene));changed=light;changed.position[0]+=.02f;assert(!source(changed,scene));
    changed=light;changed.diffuse[0]*=.9f;assert(!source(changed,scene));changed=light;changed.attenuationEnd=24;assert(!source(changed,scene));
    auto moved=value;moved.attenuation[2]=.04f;assert(!slot(moved,scene));moved=value;moved.color[0]+=.01f;assert(!slot(moved,scene));moved=value;moved.position[2]+=.02f;assert(!slot(moved,scene));
    scene.lights[1]=point;scene.lights[1].id=2;scene.count=2;assert(!source(light,scene)&&!slot(value,scene));
    scene.count=0;assert(!source(light,scene)&&!slot(value,scene));scene.count=1;scene.valid=0;assert(!source(light,scene)&&!slot(value,scene));
    // A missing slot on one receiver does not remove scene-level presence.
    scene.valid=1;moved=value;moved.position[0]+=10;assert(!slot(moved,scene)&&source(light,scene)==&point);
    std::puts("NATIVE_DIRECT_OWNERSHIP_PASS exact_current_scene unique_slot duplicate_rejected absent_receiver_slot_preserves_presence");
}
