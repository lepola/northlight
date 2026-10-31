#pragma once
// Portable point-shadow selection and D3D cube projection. No device APIs.
#include "local_shadow_signature.h"
#include "world_local_lights.h"
#include <algorithm>
#include <array>
#include <limits>
namespace NorthlightPointShadow {
// The only temporal quality tradeoff: in a crowd, reuse a *complete* world-space
// cube until 33 ms has elapsed. Never mix faces from different update times,
// except by the explicit PointShadowFacesPerFrame<6 opt-in (FaceCycle below).
// Resource/map invalidation is supplied by the owner. Failed partial updates
// must invalidate before drawing the first face, and commit only after all six.
struct RefreshSchedule {
    bool complete=false;
    bool usable=false; /* every face holds a complete draw of `light` (complete implies usable) */
    uint32_t updatedAt=0;
    uint64_t generation=0;
    NorthlightLocalLights::Light light{};
    void invalidate(){complete=usable=false;}
    // A face cycle in progress: due on every frame until it commits, while the
    // cube (each face complete, of the same light) stays in use.
    void stale(){complete=false;}
    bool due(uint32_t now,const NorthlightLocalLights::Light& current,uint64_t meshGeneration,
             size_t replayCount,bool invalidated,uint32_t minimumMs=0)const{
        if(invalidated||!complete||generation!=meshGeneration||light.sourceId!=current.sourceId||
           light.attenuationEnd!=current.attenuationEnd||light.attenuationStart!=current.attenuationStart||light.flags!=current.flags)return true;
        for(unsigned i=0;i<3;++i)if(light.position[i]!=current.position[i])return true;
        // minimumMs (PointShadowRefreshMs) extends the crowd reuse to every scene; 0 is 0.3.136.
        if(minimumMs)return uint32_t(now-updatedAt)>=minimumMs;
        return replayCount<256||uint32_t(now-updatedAt)>=33;
    }
    void commit(uint32_t now,const NorthlightLocalLights::Light& current,uint64_t meshGeneration){
        updatedAt=now;light=current;generation=meshGeneration;complete=usable=true;
    }
};
// 0.3.151 PointShadowFacesPerFrame<6: one refresh of an unchanged light spread
// over ceil(6/n) fresh frames, each face complete when drawn. Groups are
// balanced by the previous update's draws per face (longest first onto the
// lightest group with room; ties by face and group index), since crowds sit in
// the horizontal faces. n>=6 is one group of all six faces: the 0.3.150 path.
inline unsigned partitionFaces(unsigned perFrame,const unsigned (&weights)[6],unsigned (&masks)[6]){
    perFrame=perFrame<1?1:perFrame>6?6:perFrame;const unsigned groups=(6+perFrame-1)/perFrame;
    unsigned order[6]={0,1,2,3,4,5},sizes[6]={},load[6]={};
    for(unsigned i=1;i<6;++i)for(unsigned j=i;j>0&&weights[order[j]]>weights[order[j-1]];--j)std::swap(order[j],order[j-1]);
    for(auto& m:masks)m=0;
    for(unsigned face:order){unsigned best=groups;
        for(unsigned g=0;g<groups;++g)if(sizes[g]<perFrame&&(best==groups||load[g]<load[best]))best=g;
        masks[best]|=1u<<face;++sizes[best];load[best]+=weights[face]+1;}
    return groups;
}
struct FaceCycle {
    unsigned masks[6]={},count=0,next=0;
    uint32_t startedAt=0;uint64_t generation=0;
    bool active()const{return next<count;}
    void reset(){count=next=0;}
    // The cycle commits as an update of its first frame (time and mesh generation).
    void start(unsigned perFrame,const unsigned (&weights)[6],uint32_t now,uint64_t meshGeneration){
        count=partitionFaces(perFrame,weights,masks);next=0;startedAt=now;generation=meshGeneration;}
    unsigned mask()const{return active()?masks[next]:63u;}
    // One fresh frame drew mask(); true when that completed the cube.
    bool advance(){if(active())++next;return !active();}
};
// True only when an entire world AABB is outside one homogeneous D3D clip
// plane. Perspective w is retained; no corner division or positive-w guess.
// Unknown/invalid inputs fail open. Double CPU arithmetic plus a conservative
// float dot-product cancellation bound covers the GPU's float clip transform.
inline bool clipReject(const float* lo,const float* hi,const float* matrix) {
    if(!lo||!hi||!matrix)return false;
    for(int i=0;i<3;++i)if(!std::isfinite(lo[i])||!std::isfinite(hi[i])||lo[i]>hi[i])return false;
    for(int i=0;i<16;++i)if(!std::isfinite(matrix[i]))return false;
    // Check every clip component before an early rejection, so a different
    // plane cannot hide an overflowing GPU transform component.
    for(int column=0;column<4;++column){
        double bound=std::fabs(double(matrix[12+column]));
        for(int row=0;row<3;++row)bound+=std::fabs(double(matrix[row*4+column]))*
            std::max(std::fabs(double(lo[row])),std::fabs(double(hi[row])));
        if(bound>std::numeric_limits<float>::max()/32.)return false;
    }
    for(int plane=0;plane<6;++plane){
        // x+w,w-x,y+w,w-y,z,w-z all have inside >= 0.
        const int column=plane<4?plane/2:2;
        const double sign=(plane==1||plane==3||plane==5)?-1.:1.;
        const bool addW=plane!=4;
        double maximum=0,magnitude=0;
        for(int row=0;row<4;++row){
            const double a=matrix[row*4+column],w=addW?matrix[row*4+3]:0.;
            const double coefficient=sign*a+w;
            const double coord=row==3?1.:coefficient>=0?hi[row]:lo[row];
            const double maxCoord=row==3?1.:std::max(std::fabs(double(lo[row])),std::fabs(double(hi[row])));
            maximum+=coefficient*coord;magnitude+=(std::fabs(a)+std::fabs(w))*maxCoord;
        }
        // Extreme finite inputs that could overflow GPU intermediates remain
        // uncullable, even if their ideal double expression would cancel.
        if(magnitude>std::numeric_limits<float>::max()/16.)return false;
        const double margin=32.*std::numeric_limits<float>::epsilon()*(magnitude+1.);
        if(maximum < -margin)return true;
    }
    return false;
}
inline bool faceMatrix(const float* p,float nearZ,float farZ,unsigned face,float* out,unsigned resolution=256) {
    if(!p||!out||face>=6||resolution<2||!std::isfinite(nearZ)||!std::isfinite(farZ)||nearZ<=0||farZ<=nearZ)return false;
    for(int i=0;i<3;++i)if(!std::isfinite(p[i]))return false;
    // D3DCUBEMAP_FACE_POSITIVE_X,...,NEGATIVE_Z. World can be Z up;
    // the cube convention is independent of the world's up direction.
    static const float forward[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    static const float right[6][3]={{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{-1,0,0}};
    static const float up[6][3]={{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}};
    const float a=farZ/(farZ-nearZ),b=-nearZ*a,h=1.f/resolution;
    for(int i=0;i<16;++i)out[i]=0;
    for(int i=0;i<3;++i){
        out[i*4]=right[face][i]-h*forward[face][i];
        out[i*4+1]=up[face][i]+h*forward[face][i];
        out[i*4+2]=a*forward[face][i];out[i*4+3]=forward[face][i];
        for(int j=0;j<4;++j)out[12+j]-=p[i]*out[i*4+j];
    }
    out[14]+=b;return true;
}
// The sphere broad phase is evaluated once by the caller. Invalid/unknown
// bounds retain all six faces; projection uses the same conservative predicate
// as the previous per-face draw loop.
inline unsigned faceMask(const float* lo,const float* hi,bool valid,const float (&matrices)[6][16]){
    if(!valid)return 63;
    unsigned mask=0;
    for(unsigned face=0;face<6;++face)if(!clipReject(lo,hi,matrices[face]))mask|=1u<<face;
    return mask;
}
// 0.3.151 static cube faces keyed on content (world_mesh_plan.cpp records), not
// on the mesh generation. LocalShadowPS stores no fragment beyond attenuationEnd
// (a caster out of range can never shadow a lit surface: the attenuation there is
// exactly 0), so a face holds exactly the in-range fragments of the triangles in the records
// that meet the light sphere and are not clip-rejected by the face. The digest
// sphere is 1% larger than the shader's (float reconstruction margin): an extra
// record only causes an extra redraw. Order-independent like the min-depth faces.
// Same sphere arithmetic as NorthlightReplayBounds::outsideSphere.
static constexpr float FaceContentSphereMargin=1.01f;
inline bool recordOutsideSphere(const NorthlightGI::Vec3& lo,const NorthlightGI::Vec3& hi,const NorthlightLocalLights::Light& l,float scale=1){
    const float low[3]={lo.x,lo.y,lo.z},high[3]={hi.x,hi.y,hi.z};
    const double radius=double(l.attenuationEnd)*scale;
    if(!std::isfinite(radius)||radius<0)return false;double distance=0;
    for(unsigned k=0;k<3;++k){if(!std::isfinite(l.position[k])||!std::isfinite(low[k])||!std::isfinite(high[k])||low[k]>high[k])return false;
        const double delta=std::max({double(low[k])-l.position[k],double(l.position[k])-high[k],0.});distance+=delta*delta;}
    return distance>radius*radius;
}
inline void faceContent(const NorthlightLocalShadowSignature::Records& records,const NorthlightLocalLights::Light& l,
                        const float (&matrices)[6][16],NorthlightLocalShadowSignature::Digest (&out)[6]){
    for(auto& digest:out)digest={};
    for(const auto& r:records){
        if(r.terrain||recordOutsideSphere(r.low,r.high,l,FaceContentSphereMargin))continue;
        const float lo[]={r.low.x,r.low.y,r.low.z},hi[]={r.high.x,r.high.y,r.high.z};
        const unsigned mask=faceMask(lo,hi,true,matrices);
        for(unsigned face=0;face<6;++face)if(mask&(1u<<face))out[face].add(r.content);
    }
}
// Faces whose recorded static content (digest, known, generation) differs from
// `current` (null: records unknown, so any generation change counts).
inline unsigned staleFaces(const NorthlightLocalShadowSignature::Digest (&stored)[6],const bool (&known)[6],const uint64_t (&serial)[6],
                           const NorthlightLocalShadowSignature::Digest* current,uint64_t generation){
    unsigned mask=0;
    for(unsigned face=0;face<6;++face)if(NorthlightLocalShadowSignature::needsRefresh(known[face],stored[face],serial[face],
        current!=nullptr,current?current[face]:NorthlightLocalShadowSignature::Digest{},generation))mask|=1u<<face;
    return mask;
}
inline bool intersectsSphereAABB(const NorthlightLocalLights::Light& l,const float* lo,const float* hi) {
    if(!NorthlightLocalLights::valid(l)||!lo||!hi)return false;
    double squared=0;
    for(int i=0;i<3;++i){
        if(!std::isfinite(lo[i])||!std::isfinite(hi[i])||lo[i]>hi[i])return false;
        double d=l.position[i]<lo[i]?lo[i]-l.position[i]:l.position[i]>hi[i]?l.position[i]-hi[i]:0;
        squared+=d*d;
    }
    return squared<=double(l.attenuationEnd)*l.attenuationEnd;
}
inline constexpr unsigned MaxLights=128;
struct Picks {std::array<int,MaxLights> index{};unsigned count=0;};
inline bool eligible(const NorthlightLocalLights::Light& l){
    // Legacy baked WMO lamps remain supported. Unclassified model/particle/equipment
    // observations never become shadow sources just because they happen to be bright.
    return (l.flags&NorthlightLocalLights::CastShadow)||l.kind==1;
}
inline Picks selectMany(const NorthlightLocalLights::Light* lights,size_t count,const float* camera,unsigned limit,
                        const std::array<uint64_t,MaxLights>& previous={},float maxCameraDistance=48){
    Picks out;limit=std::min(limit,MaxLights);std::array<double,MaxLights> scores{};
    if(!lights||!camera||!std::isfinite(maxCameraDistance)||maxCameraDistance<=0)return out;
    for(int i=0;i<3;++i)if(!std::isfinite(camera[i]))return out;
    for(size_t i=0;i<count;++i){
        const auto& l=lights[i];if(!eligible(l))continue;
        if(!NorthlightLocalLights::valid(l)||l.attenuationEnd<=.11f)continue;
        double distance2=0;for(int k=0;k<3;++k){double d=l.position[k]-camera[k];distance2+=d*d;}
        if(distance2>double(maxCameraDistance)*maxCameraDistance)continue;
        // Apparent influence weighted by brightness, bounded at the source.
        const double color=std::min(16.f,std::max(l.diffuse[0],std::max(l.diffuse[1],l.diffuse[2])));
        if(color<.005f)continue;
        const bool incumbent=std::find(previous.begin(),previous.end(),l.sourceId)!=previous.end();
        const double score=color*double(l.attenuationEnd)*l.attenuationEnd/(1+distance2)*(incumbent?1.25:1.);
        unsigned at=0;while(at<out.count&&(scores[at]>score||(scores[at]==score&&lights[out.index[at]].sourceId<l.sourceId)))++at;
        if(at>=limit)continue;
        for(unsigned j=std::min(out.count,limit-1);j>at;--j){out.index[j]=out.index[j-1];scores[j]=scores[j-1];}
        out.index[at]=int(i);scores[at]=score;out.count=std::min(out.count+1,limit);
    }
    return out;
}
inline int select(const std::vector<NorthlightLocalLights::Light>& lights,const float* camera,uint64_t previousSourceId=0,float maxCameraDistance=48){
    const auto p=selectMany(lights.data(),lights.size(),camera,1,{previousSourceId},maxCameraDistance);
    return p.count?p.index[0]:-1;
}
} // namespace NorthlightPointShadow
