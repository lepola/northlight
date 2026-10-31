#pragma once
#include "world_mesh_plan.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace NorthlightWorldStreaming {
// Start the next bounded build while the current 96-unit coverage still has
// 64 units of travel left. Coverage and GI solve radii are not enlarged.
inline constexpr float GeometryRefreshDistance=32.f;
inline constexpr float GeometryCoverageDistance=96.f;
inline bool within(NorthlightGI::Vec3 center,NorthlightGI::Vec3 camera,double radius){
    const double x=double(center.x)-camera.x,y=double(center.y)-camera.y,z=double(center.z)-camera.z;
    return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&x*x+y*y+z*z<=radius*radius;
}
inline bool needsGeometry(const std::string& map,NorthlightGI::Vec3 center,
                          const std::string& currentMap,NorthlightGI::Vec3 camera){
    return map!=currentMap||!within(center,camera,GeometryRefreshDistance);
}
struct Retry {
    uint32_t failedAt=0;bool waiting=false;
    bool ready(uint32_t now)const{return !waiting||uint32_t(now-failedAt)>=1000u;}
    void fail(uint32_t now){failedAt=now;waiting=true;}
    void clear(){waiting=false;}
};
inline bool applicable(const std::string& map,NorthlightGI::Vec3 center,
                       const std::string& currentMap,NorthlightGI::Vec3 camera) {
    return map==currentMap&&within(center,camera,GeometryCoverageDistance);
}
// 0.3.169 coverage hold: the render thread keeps a snapshot past the 96-unit coverage
// until a replacement applies, up to this hard limit. The near cascade (pivot <= eye+80,
// radius 48) stays inside the +-288 local box up to 288-128=160.
inline constexpr float GeometryRetainDistance=160.f;
inline bool retained(const std::string& map,NorthlightGI::Vec3 center,
                     const std::string& currentMap,NorthlightGI::Vec3 camera) {
    return map==currentMap&&within(center,camera,GeometryRetainDistance);
}
// Render-thread adoption of a publication. The same hard limit as the hold, so a held
// snapshot and an unadoptable build can never fill both geometry generations; an error
// snapshot (no BVH) never replaces a drawable one.
inline bool adopts(const std::string& map,NorthlightGI::Vec3 center,bool drawable,bool activeDrawable,
                   const std::string& currentMap,NorthlightGI::Vec3 camera) {
    return (drawable||!activeDrawable)&&retained(map,center,currentMap,camera);
}
// 0.3.169 geometry lead: at speed the build is centred ahead of the eye by the travel
// expected during one build, capped so that while the lead point is within the 32-unit
// refresh of a region the eye stays within its 96-unit coverage (60+32 < 96).
// Builds still start per 32 units of lead-point travel: no extra builds.
inline constexpr float GeometryLeadMax=60.f,GeometryLeadMinSpeed=10.f,GeometryLeadMoveStep=8.f;
inline NorthlightGI::Vec3 leadCenter(NorthlightGI::Vec3 camera,NorthlightGI::Vec3 velocity,float expectedSeconds){
    const float speed=std::sqrt(velocity.x*velocity.x+velocity.y*velocity.y+velocity.z*velocity.z);
    if(!std::isfinite(speed)||!std::isfinite(expectedSeconds)||speed<GeometryLeadMinSpeed||expectedSeconds<=0)return camera;
    const float scale=std::min(speed*expectedSeconds,GeometryLeadMax)/speed;
    return NorthlightGI::Vec3(camera.x+velocity.x*scale,camera.y+velocity.y*scale,camera.z+velocity.z*scale);
}
// A camera orbit rotates the eye around a nearly stationary pivot. Keep the
// geometry there, then cap the TOTAL eye offset by the original lead budget.
// Thus the unchanged 32-unit refresh still keeps the eye within 60+32 < 96.
inline NorthlightGI::Vec3 anchoredCenter(NorthlightGI::Vec3 eye,NorthlightGI::Vec3 pivot,NorthlightGI::Vec3 velocity,float expectedSeconds){
    const auto wanted=leadCenter(pivot,velocity,expectedSeconds);
    const NorthlightGI::Vec3 delta(wanted.x-eye.x,wanted.y-eye.y,wanted.z-eye.z);const float squared=delta.x*delta.x+delta.y*delta.y+delta.z*delta.z;
    if(!std::isfinite(squared))return eye;
    if(squared<=GeometryLeadMax*GeometryLeadMax)return wanted;
    const float scale=GeometryLeadMax/std::sqrt(squared);
    return NorthlightGI::Vec3(eye.x+delta.x*scale,eye.y+delta.y*scale,eye.z+delta.z*scale);
}
// Travel velocity for the lead: smoothed eye and pivot (eye + forward*pivotDistance)
// velocities, the slower of the two. A third-person flick moves the eye but not the
// pivot; a first-person turn with a wrong pivot distance moves the pivot but not the eye.
// The lead arms only after ArmMs of consistent travel (both speeds >= GeometryLeadMinSpeed,
// ratio within [0.75, 1.33]): a flick with a wrong pivot distance, a zoom, a camera-collision
// snap or a blink never arms it. Once armed a flick during travel keeps it; it disarms
// when the slower speed drops below GeometryLeadMinSpeed.
struct Motion {
    static constexpr float TimeConstantMs=500.f,JumpDistance=40.f,MinRatio=.75f,MaxRatio=1.33f;static constexpr uint32_t GapMs=500,ArmMs=1000;
    NorthlightGI::Vec3 eye,pivot,eyeVelocity,pivotVelocity;std::string map;uint32_t at=0,consistentSince=0;bool valid=false,consistent=false,armed=false;
    static float length(NorthlightGI::Vec3 v){return std::sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
    static void step(NorthlightGI::Vec3& last,NorthlightGI::Vec3& velocity,NorthlightGI::Vec3 now,float ms){
        const NorthlightGI::Vec3 d(now.x-last.x,now.y-last.y,now.z-last.z);last=now;
        if(!(length(d)<=JumpDistance)){velocity=NorthlightGI::Vec3();return;} /* teleport, or non-finite */
        const float a=std::min(1.f,ms/TimeConstantMs),k=1000.f/ms;
        velocity=NorthlightGI::Vec3(velocity.x+(d.x*k-velocity.x)*a,velocity.y+(d.y*k-velocity.y)*a,velocity.z+(d.z*k-velocity.z)*a);
    }
    void update(const std::string& currentMap,NorthlightGI::Vec3 eyeNow,NorthlightGI::Vec3 pivotNow,uint32_t now){
        const uint32_t ms=now-at;
        if(!valid||map!=currentMap||ms>GapMs){eye=eyeNow;pivot=pivotNow;eyeVelocity=pivotVelocity=NorthlightGI::Vec3();map=currentMap;at=now;valid=true;consistent=armed=false;return;}
        if(!ms)return; /* same tick: the displacement accumulates into the next step */
        step(eye,eyeVelocity,eyeNow,float(ms));step(pivot,pivotVelocity,pivotNow,float(ms));at=now;
        const float e=length(eyeVelocity),p=length(pivotVelocity);
        if(armed){if(!(std::min(e,p)>=GeometryLeadMinSpeed))armed=consistent=false;return;}
        const bool steady=e>=GeometryLeadMinSpeed&&p>=GeometryLeadMinSpeed&&e>=MinRatio*p&&e<=MaxRatio*p;
        if(!steady){consistent=false;return;}
        if(!consistent){consistent=true;consistentSince=now;}
        armed=now-consistentSince>=ArmMs;
    }
    NorthlightGI::Vec3 velocity()const{if(!armed)return NorthlightGI::Vec3();return length(eyeVelocity)<=length(pivotVelocity)?eyeVelocity:pivotVelocity;}
};
// Request a moved geometry target (reason 128) when it differs from the eye.
// Eye-centred motion is already covered by the probe-centre trigger. Test
// both offsets so stopping a lead or changing camera mode still requests it.
inline bool leadMoved(NorthlightGI::Vec3 lastCamera,NorthlightGI::Vec3 lastCenter,NorthlightGI::Vec3 camera,NorthlightGI::Vec3 center){
    auto apart=[](NorthlightGI::Vec3 a,NorthlightGI::Vec3 b,float e){const float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;return x*x+y*y+z*z>e*e;};
    return (apart(lastCenter,lastCamera,0)||apart(center,camera,0))&&apart(lastCenter,center,GeometryLeadMoveStep);
}
// Source and plan are immutable and retained together by the caller. Validate
// their identity and all upload sizes BEFORE making any resource allocation.
// Per-index validity is established by buildUploadPlan on the worker.
inline const char* validate(const NorthlightWorldMesh::WorldMeshUploadPlan& p,const NorthlightGI::WorldScene& s) {
    if(p.source!=&s)return "plan/source identity mismatch";
    if(!p.vertexCount||!p.triangleCount||p.batches.empty()||p.materials.empty())return "empty upload plan";
    if(p.vertexCount!=s.vertices.size()||p.triangleCount!=s.triangles.size()||p.materials.size()!=s.materials.size())return "plan/source count mismatch";
    if(p.vertexBytes!=uint64_t(s.vertices.size())*sizeof(NorthlightGI::WorldVertex)||p.vertexBytes>UINT32_MAX||
       p.indexBytes!=uint64_t(p.indices.size())*sizeof(uint32_t)||p.indexBytes>UINT32_MAX||
       uint64_t(p.triangleCount)*3!=p.indices.size())return "invalid buffer sizes";
    uint64_t expected=0;
    for(const auto& b:p.batches){
        if(!b.count||b.material>=p.materials.size()||b.start!=expected)return "invalid batch range";
        expected+=uint64_t(b.count)*3;if(expected>p.indices.size())return "batch exceeds index buffer";
    }
    if(expected!=p.indices.size())return "incomplete batch coverage";
    for(const auto& m:p.materials)
        if(!m.width||!m.height||m.width>4096||m.height>4096||uint64_t(m.width)*m.height*4!=m.bgra.size())return "invalid texture size";
    return nullptr;
}
enum class BeginResult { Ready, InvalidPlan, VertexFailure, IndexFailure };
// Allocation transaction shared by runtime and fake-resource tests. The rollback
// callback releases only new pending resources; the last committed mesh is never
// handed to this function and therefore cannot be destroyed by a failed attempt.
template<class Vertex,class Index,class Rollback>
BeginResult begin(const NorthlightWorldMesh::WorldMeshUploadPlan& p,const NorthlightGI::WorldScene& s,
                  Vertex vertex,Index index,Rollback rollback,const char*& reason) {
    reason=validate(p,s);
    if(reason){rollback();return BeginResult::InvalidPlan;}
    if(!vertex(uint32_t(p.vertexBytes))){rollback();return BeginResult::VertexFailure;}
    if(!index(uint32_t(p.indexBytes))){rollback();return BeginResult::IndexFailure;}
    return BeginResult::Ready;
}
} // namespace NorthlightWorldStreaming
