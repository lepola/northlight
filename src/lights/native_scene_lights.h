#pragma once
// Read-only 12340 scene inventory. No native calls and no draw/visibility stamp
// as an existence test. Pose follows authored tracks and current attachment state.
#include "native_light_snapshot.h"
#include "local_light_selection.h"
#include "native_light_pose.h"
#include "native_fixture_catalog.h"
#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <list>
#include <vector>

namespace NorthlightNativeLights {
namespace SceneDetail {
using Bytes=std::vector<unsigned char>;
inline constexpr std::uint32_t ScenePointer=0xcd754c;
inline constexpr unsigned ModelBytes=0x1d4,BoneBytes=0xac,LightBytes=0xd4;
inline constexpr unsigned DiscoverySteps=128,ModelLimit=512,AssetLimit=128,AssetBytes=256*1024,CacheBytes=2*1024*1024;
inline constexpr unsigned MaxBones=512,MaxLights=64,MaxSequences=512,MaxKeys=16384;
inline bool pointer(std::uint32_t p){return p>=0x10000&&p<0xffff0000&&!(p&3);}
template<class T> T at(const unsigned char* p,unsigned offset){T v;std::memcpy(&v,p+offset,sizeof v);return v;}
template<class T,std::size_t N> T at(const std::array<unsigned char,N>& p,unsigned offset){return at<T>(p.data(),offset);}
inline float distance(const float* a,const float* b){float d=0;for(unsigned k=0;k<3;++k){const float x=a[k]-b[k];d+=x*x;}return d;}
struct Keys {std::vector<std::uint32_t> times;std::vector<Value> values;};
struct Clock {unsigned animation=0;std::uint32_t time=0;};
struct CachedCurve {unsigned sequence=~0u;std::uint32_t used=0;std::size_t bytes=0;Keys keys;};
struct PoseTrack {std::vector<CachedCurve> curves;};
struct Authored {unsigned index=0,bone=0;bool fixed=false;float point[3]{},fixedPoint[3]{};std::array<unsigned char,60> tracks{};PoseTrack color,intensity,enabled;std::vector<unsigned> chain;};
struct BoneTracks {PoseTrack translation,rotation,scale;};
struct Attachment {PoseTrack enabled;};
struct Asset {
    std::uint32_t shared=0,data=0,checked=0,used=0;std::size_t bytes=0,allowance=AssetBytes;bool poseReady=false;
    std::array<unsigned char,304> header{};std::uint64_t fixtureSources=0;std::uint32_t fixtureTag=0;
    std::vector<std::array<unsigned char,88>> bones;
    std::vector<std::array<unsigned char,64>> sequences;
    std::vector<std::uint32_t> loops;std::vector<Authored> lights;std::vector<BoneTracks> poses;
    std::vector<std::array<unsigned char,40>> attachmentData;std::vector<Attachment> attachments;
};
template<class Read> bool prepareLight(Read&,Asset& a,Authored& light){
    int bone=int(light.bone);std::array<bool,MaxBones> visited{};light.fixed=true;std::copy(light.point,light.point+3,light.fixedPoint);
    while(bone>=0){if(unsigned(bone)>=a.bones.size()||visited[unsigned(bone)])return false;visited[unsigned(bone)]=true;light.chain.push_back(unsigned(bone));a.bytes+=sizeof(unsigned);
        const auto& b=a.bones[unsigned(bone)];const auto flags=at<std::uint32_t>(b,4);if(flags&0x7f)light.fixed=false;
        if(flags&0x280){if(at<std::uint32_t>(b,20))light.fixed=false;
            bool pivot=true;for(unsigned k=0;k<3;++k)pivot=pivot&&light.fixedPoint[k]==at<float>(b,76+4*k);
            if(!pivot&&(at<std::uint32_t>(b,40)||at<std::uint32_t>(b,60)))light.fixed=false;
        }bone=at<std::int16_t>(b,8);
    }return true;
}
inline bool curveRoom(Asset& a,PoseTrack& target,std::size_t bytes,std::uint32_t epoch){
    const auto extra=[&](){return target.curves.size()<target.curves.capacity()||std::any_of(target.curves.begin(),target.curves.end(),[](const auto& c){return c.sequence==~0u;})?0:sizeof(CachedCurve);};
    while(a.bytes>a.allowance||bytes+extra()>a.allowance-a.bytes){CachedCurve* oldest=nullptr;PoseTrack* owner=nullptr;
        const auto scan=[&](PoseTrack& track){for(auto& c:track.curves)if(c.sequence!=~0u&&c.used!=epoch&&(!oldest||c.used<oldest->used)){oldest=&c;owner=&track;}};
        for(auto& p:a.poses){scan(p.translation);scan(p.rotation);scan(p.scale);}for(auto& l:a.lights){scan(l.color);scan(l.intensity);scan(l.enabled);}for(auto& t:a.attachments)scan(t.enabled);
        if(!oldest){const auto before=a.bytes;const auto trim=[&](PoseTrack& track){const auto capacity=track.curves.capacity();track.curves.erase(std::remove_if(track.curves.begin(),track.curves.end(),[](const auto& c){return c.sequence==~0u;}),track.curves.end());track.curves.shrink_to_fit();a.bytes-=(capacity-track.curves.capacity())*sizeof(CachedCurve);};
            for(auto& p:a.poses){trim(p.translation);trim(p.rotation);trim(p.scale);}for(auto& l:a.lights){trim(l.color);trim(l.intensity);trim(l.enabled);}for(auto& t:a.attachments)trim(t.enabled);if(before==a.bytes)return false;continue;}
        const auto freed=oldest->bytes;a.bytes-=freed;
        if(freed){oldest->keys=Keys{};oldest->bytes=0;oldest->sequence=~0u;}
        else{const auto capacity=owner->curves.capacity();owner->curves.erase(owner->curves.begin()+(oldest-owner->curves.data()));owner->curves.shrink_to_fit();a.bytes-=(capacity-owner->curves.capacity())*sizeof(CachedCurve);}
    }return true;
}
template<class Read> Value poseSample(Read& read,Asset& a,PoseTrack& cache,const unsigned char* raw,unsigned fields,bool compressed,Clock clock,std::uint32_t now,std::uint32_t origin,std::uint32_t epoch,Value fallback,bool& ok){
    const unsigned interpolation=fields==1?0:at<std::uint16_t>(raw,0),count=at<std::uint32_t>(raw,4);const int loop=at<std::int16_t>(raw,2);
    if(count!=at<std::uint32_t>(raw,12)){ok=false;return fallback;}if(!count)return fallback;if(interpolation>1||count>MaxSequences||loop< -1||(loop>=0&&unsigned(loop)>=a.loops.size())){ok=false;return fallback;}
    if(loop>=0){const auto duration=a.loops[unsigned(loop)];clock={0,duration?(now-origin)%duration:0};}
    const unsigned sequence=clock.animation<count?clock.animation:0;auto found=std::find_if(cache.curves.begin(),cache.curves.end(),[&](const auto& c){return c.sequence==sequence;});
    if(found==cache.curves.end()){
        std::array<std::uint32_t,2> times{},values{};
        if(!read(at<std::uint32_t>(raw,8)+sequence*8,times.data(),8)||!read(at<std::uint32_t>(raw,16)+sequence*8,values.data(),8)||times[0]!=values[0]||times[0]>MaxKeys){ok=false;return fallback;}
        const unsigned n=times[0],width=compressed?8:fields==1?1:fields==0?4:fields*4;const std::size_t bytes=n*(sizeof(Value)+4);
        if(!curveRoom(a,cache,bytes,epoch)){ok=false;return fallback;}
        Keys loaded;loaded.times.resize(n);loaded.values.resize(n);Bytes valuesRaw(n*width);
        if(n&&(!read(times[1],loaded.times.data(),n*4)||!read(values[1],valuesRaw.data(),valuesRaw.size())||!std::is_sorted(loaded.times.begin(),loaded.times.end()))){ok=false;return fallback;}
        for(unsigned i=0;i<n;++i)for(unsigned k=0;k<(compressed?4:fields<=1?1:fields);++k){const float value=compressed?float(at<std::uint16_t>(valuesRaw.data()+i*width,k*2))*(2.f/65535.f)-1.f:fields==1?float(valuesRaw[i]):at<float>(valuesRaw.data()+i*width,k*4);
            if(!std::isfinite(value)||std::fabs(value)>100000||(fields==1&&value>1)||(fields==0&&value<0)){ok=false;return fallback;}loaded.values[i][k]=value;}
        found=std::find_if(cache.curves.begin(),cache.curves.end(),[](const auto& c){return c.sequence==~0u;});if(found==cache.curves.end()){const auto capacity=cache.curves.capacity();cache.curves.reserve(cache.curves.size()+1);a.bytes+=(cache.curves.capacity()-capacity)*sizeof(CachedCurve);cache.curves.emplace_back();found=cache.curves.end()-1;}
        a.bytes+=bytes;found->bytes=bytes;found->keys=std::move(loaded);found->sequence=sequence;
    }
    found->used=epoch;const auto& keys=found->keys;if(keys.times.empty())return fallback;
    const auto upper=std::upper_bound(keys.times.begin(),keys.times.end(),clock.time);const unsigned first=upper==keys.times.begin()?0:unsigned(upper-keys.times.begin()-1);
    if(!interpolation||first+1==keys.times.size()||clock.time<=keys.times[first])return keys.values[first];
    const float t=float(clock.time-keys.times[first])/float(keys.times[first+1]-keys.times[first]);if(compressed)return quaternionMix(keys.values[first],keys.values[first+1],t,false);
    Value value=keys.values[first];for(unsigned k=0;k<4;++k)value[k]+=(keys.values[first+1][k]-value[k])*t;return value;
}
inline Clock clock(const unsigned char* runtime,unsigned offset,const Asset& asset,std::uint32_t now,bool secondary,bool& ok){
    Clock c;const unsigned index=at<std::uint16_t>(runtime,offset+8);if(index==0xffff)return {at<std::uint16_t>(runtime,offset+4),at<std::uint32_t>(runtime,offset)};if(index>=asset.sequences.size()){ok=false;return c;}
    const auto& sequence=asset.sequences[index];const auto duration=at<std::uint32_t>(sequence,4),flags=at<std::uint32_t>(sequence,12);
    const auto start=at<std::uint32_t>(runtime,offset+12),end=at<std::uint32_t>(runtime,offset+16),origin=at<std::uint32_t>(runtime,offset+28);
    const float speed=at<float>(runtime,offset+20);if(!std::isfinite(speed)||std::fabs(speed)>1000){ok=false;return c;}
    const bool clamped=flags&(secondary?0x80u:1u);const bool ended=clamped&&std::int32_t(end-now)<=0;
    if(clamped&&std::int32_t(start-now)>0)now=start;
    const double elapsed=double(std::int32_t((ended?end:now)-start))*speed;
    if(elapsed<std::numeric_limits<std::int32_t>::min()||elapsed>std::numeric_limits<std::int32_t>::max()){ok=false;return c;}
    const auto value=std::uint32_t(std::int32_t(elapsed))+origin;c.animation=index;
    c.time=ended?(std::int32_t(value)<0?0:std::min(value,duration)):(duration?value%duration:0);return c;
}
} // namespace SceneDetail

class Reader {
    using Asset=SceneDetail::Asset;using Matrix=SceneDetail::Matrix;using ModelData=std::array<unsigned char,SceneDetail::ModelBytes>;
    struct Timing {SceneDetail::Clock first,second;float blend=0;bool valid=false;};
    struct Model {std::uint32_t shared=0,data=0,origin=0,visited=0;std::uint64_t firstId=0;float distance=0;};
    std::map<std::uint32_t,Model> models_;std::list<Asset> assets_;std::shared_ptr<const NorthlightNativeFixtures::Catalog> fixtures_;
    std::uint32_t scene_=0,cursor_=0,frame_=0,lastHead_=0,lastScan_=0;std::uint64_t serial_=0;bool checked_=false,proven_=false;
    unsigned loads_=0;std::size_t readBytes_=0;unsigned readCalls_=0;
    std::array<std::array<unsigned char,SceneDetail::BoneBytes>,SceneDetail::MaxBones> boneScratch_;
    std::array<unsigned char,8192> runtimeScratch_;
    std::array<std::uint32_t,SceneDetail::MaxBones> boneKnown_{};std::uint32_t boneGeneration_=0;
    std::array<Matrix,SceneDetail::MaxBones> poseScratch_;
    std::array<Timing,SceneDetail::MaxBones> timingScratch_;
    std::array<std::uint32_t,SceneDetail::MaxBones> poseKnown_{},poseBusy_{},timingKnown_{},timingBusy_{};
    struct WorldEntry {std::uint32_t address=0,frame=0;ModelData model;Asset* asset=nullptr;Matrix matrix;float alpha=0;bool visible=false,valid=false,busy=false;};
    std::array<WorldEntry,64> worlds_;unsigned nextWorld_=0;
    Matrix view_,inverse_;bool viewsRead_=false,viewsValid_=false;
    template<class Read> bool views(Read& read){if(!viewsRead_){viewsRead_=true;std::array<float,32> values{};viewsValid_=read(scene_+0x84,values.data(),128);
        if(viewsValid_){std::copy(values.begin(),values.begin()+16,view_.begin());std::copy(values.begin()+16,values.end(),inverse_.begin());viewsValid_=SceneDetail::affine(view_)&&SceneDetail::affine(inverse_);}}return viewsValid_;}
    std::size_t poseAllowance(const Asset& target)const{std::size_t used=0;for(const auto& a:assets_)if(&a!=&target)used+=a.bytes;return std::min(std::size_t(SceneDetail::AssetBytes),used>=SceneDetail::CacheBytes?0:SceneDetail::CacheBytes-used);}
    template<class Read> struct Runtime {
        Reader& owner;Read& read;Asset& asset;const ModelData& model;Matrix world;
        std::uint32_t now,origin,bones,lights,generation;std::uint64_t end=0;bool span=false,valid=false;
        Runtime(Reader& r,Read& input,Asset& a,const ModelData& m,const Matrix& w,std::uint32_t time):owner(r),read(input),asset(a),model(m),world(w),now(time){
            using namespace SceneDetail;a.allowance=r.poseAllowance(a);origin=at<std::uint32_t>(m,0x74);bones=at<std::uint32_t>(m,0x94);lights=at<std::uint32_t>(m,0x1d0);
            valid=pointer(bones);if(!valid)return;const unsigned count=at<std::uint32_t>(a.header,264);
            end=count&&pointer(lights)&&lights>=bones?std::uint64_t(lights)+count*LightBytes:std::uint64_t(bones)+a.bones.size()*BoneBytes;
            span=end>=bones&&end-bones<=r.runtimeScratch_.size()&&read(bones,r.runtimeScratch_.data(),std::size_t(end-bones));
            if(!++r.boneGeneration_){r.boneKnown_.fill(0);r.poseKnown_.fill(0);r.poseBusy_.fill(0);r.timingKnown_.fill(0);r.timingBusy_.fill(0);++r.boneGeneration_;}generation=r.boneGeneration_;
        }
        const unsigned char* bone(unsigned index){using namespace SceneDetail;if(!valid||index>=asset.bones.size())return nullptr;
            if(span&&std::size_t(index+1)*BoneBytes<=end-bones)return owner.runtimeScratch_.data()+index*BoneBytes;
            if(owner.boneKnown_[index]!=generation){if(!read(bones+index*BoneBytes+0x40,owner.boneScratch_[index].data()+0x40,BoneBytes-0x40))return nullptr;owner.boneKnown_[index]=generation;}return owner.boneScratch_[index].data();}
        Timing timing(unsigned index){using namespace SceneDetail;Timing bad;if((at<std::uint32_t>(model,16)&0x1000)&&index)return bad;if(index>=asset.bones.size()||owner.timingBusy_[index]==generation)return bad;
            if(owner.timingKnown_[index]==generation)return owner.timingScratch_[index];const auto* b=bone(index);if(!b)return bad;owner.timingBusy_[index]=generation;
            const int authoredParent=at<std::int16_t>(asset.bones[index],8);const int parent=authoredParent>=0?authoredParent:index?0:-1;
            const auto primary=at<std::uint16_t>(b,0x48),secondary=(at<std::uint32_t>(model,16)&0x1000)?std::uint16_t(0xffff):at<std::uint16_t>(b,0x6c);Timing inherited;
            if(parent>=0&&(primary==0xffff||secondary==0xffff)){inherited=timing(unsigned(parent));if(!inherited.valid)return bad;}
            bool ok=true;const auto paused=at<std::uint32_t>(model,0x64),time=paused?paused:now;Timing t;
            t.first=primary==0xffff&&parent>=0?inherited.first:clock(b,0x40,asset,time,false,ok);
            t.second=secondary==0xffff?(parent>=0?inherited.second:t.first):clock(b,0x64,asset,time,true,ok);
            if(primary==0xffff&&secondary==0xffff)t.blend=parent>=0?inherited.blend:0;
            else if(!(at<std::uint32_t>(model,16)&0x1000)){const auto finish=at<std::uint32_t>(b,0x9c);const float inverse=at<float>(b,0xa0),weight=at<float>(b,0xa4);
                if(!std::isfinite(inverse)||!std::isfinite(weight)||inverse<0||weight<0||weight>1)ok=false;
                else if(std::int32_t(finish-now)>0&&(t.first.time!=t.second.time||t.first.animation!=t.second.animation)){const float factor=std::clamp(float(finish-now)*inverse,0.f,1.f);t.blend=factor*factor*(3-2*factor)*weight;}}
            t.valid=ok;owner.timingBusy_[index]=0;owner.timingKnown_[index]=generation;owner.timingScratch_[index]=t;return t;
        }
        SceneDetail::Value poseValue(SceneDetail::PoseTrack& cache,const unsigned char* raw,unsigned fields,bool compressed,const Timing& t,SceneDetail::Value fallback,bool& ok){using namespace SceneDetail;
            Value v=poseSample(read,asset,cache,raw,fields,compressed,t.first,now,origin,owner.frame_,fallback,ok);
            if(t.blend&&fields!=1&&at<std::uint16_t>(raw,0)&&at<std::int16_t>(raw,2)<0){const auto b=poseSample(read,asset,cache,raw,fields,compressed,t.second,now,origin,owner.frame_,fallback,ok);if(compressed)return quaternionMix(v,b,t.blend,true);for(unsigned k=0;k<4;++k)v[k]+=(b[k]-v[k])*t.blend;}return v;
        }
        bool pose(unsigned index,Matrix& result){using namespace SceneDetail;if(at<std::uint32_t>(model,16)&0x1000){result=world;return index==0&&!asset.bones.empty();}if(index>=asset.bones.size()||owner.poseBusy_[index]==generation)return false;
            if(owner.poseKnown_[index]==generation){result=owner.poseScratch_[index];return true;}const auto* runtime=bone(index);if(!runtime)return false;owner.poseBusy_[index]=generation;
            const auto& authored=asset.bones[index];const int parent=at<std::int16_t>(authored,8);Matrix inherited=world;
            if(parent>=0&&!pose(unsigned(parent),inherited))return false;const unsigned flags=at<std::uint32_t>(authored,4)|at<std::uint32_t>(runtime,0x8c);
            float pivot[3];for(unsigned k=0;k<3;++k){pivot[k]=at<float>(authored,76+4*k);if(!std::isfinite(pivot[k])||std::fabs(pivot[k])>=100000)return false;}
            if(parent>=0)inherited=suppressInherited(inherited,world,pivot,flags);Matrix local=identity();bool ok=true;
            if(flags&0x280){const Timing t=timing(index);if(!t.valid)return false;auto& cache=asset.poses[index];
                const auto rotationValue=poseValue(cache.rotation,authored.data()+36,4,true,t,{0,0,0,1},ok),scale=poseValue(cache.scale,authored.data()+56,3,false,t,{1,1,1,0},ok),offset=poseValue(cache.translation,authored.data()+16,3,false,t,{},ok);
                if(!ok)return false;local=rotation(rotationValue);for(unsigned row=0;row<3;++row)for(unsigned k=0;k<3;++k)local[row*4+k]*=scale[row];
                const auto procedure=at<std::uint32_t>(runtime,0x88);if((flags&0x80)&&procedure){Matrix matrix;if(!read(procedure,matrix.data(),64)||!affine(matrix))return false;local=multiply(local,matrix);}
                float negative[3];for(unsigned k=0;k<3;++k){local[12+k]+=pivot[k]+offset[k];negative[k]=-pivot[k];}translate(local,negative);
            }
            result=multiply(local,inherited);if(flags&0x78){if(!owner.views(read))return false;result=billboard(result,local,owner.view_,owner.inverse_,pivot,flags);}
            if(!affine(result))return false;owner.poseBusy_[index]=0;owner.poseKnown_[index]=generation;owner.poseScratch_[index]=result;return true;
        }
        bool position(const SceneDetail::Authored& light,float* result){using namespace SceneDetail;bool fixed=light.fixed;if(at<std::uint32_t>(model,16)&0x1000){if(light.bone)return false;fixed=false;}
            if(fixed)for(unsigned index:light.chain){const auto* b=bone(index);if(!b)return false;const unsigned flags=at<std::uint32_t>(b,0x8c)|at<std::uint32_t>(asset.bones[index],4);
                if((flags&0x7f)||((flags&0x80)&&at<std::uint32_t>(b,0x88))||((flags&0x280)&&!(at<std::uint32_t>(asset.bones[index],4)&0x280))){fixed=false;break;}}
            Matrix matrix=world;if(!fixed&&!pose(light.bone,matrix))return false;const auto point=transform(fixed?light.fixedPoint:light.point,matrix);
            for(unsigned k=0;k<3;++k){result[k]=point[k];if(!std::isfinite(result[k])||std::fabs(result[k])>=100000)return false;}return true;
        }
        bool light(unsigned index,std::array<unsigned char,SceneDetail::LightBytes>& storage,const unsigned char*& result){using namespace SceneDetail;if(!pointer(lights))return false;
            if(span&&lights>=bones&&std::uint64_t(lights)+(index+1)*LightBytes<=end){result=owner.runtimeScratch_.data()+lights-bones+index*LightBytes;return true;}
            result=storage.data();return read(lights+index*LightBytes,storage.data(),storage.size());}
    };
    template<class Read> bool sharedData(Read& read,std::uint32_t shared,std::uint32_t& data){
        for(const auto& a:assets_)if(a.shared==shared&&a.checked==frame_){data=a.data;return true;}
        return read(shared+0x150,&data,4);
    }
    template<class Read> Asset* asset(Read& input,std::uint32_t shared,std::uint32_t data,bool needPose=false){
        using namespace SceneDetail;
        unsigned calls=0;std::size_t bytesRead=0;bool metadataLimit=false;
        const auto read=[&](std::uint32_t address,void* dst,std::size_t bytes){
            if(calls>=512||bytes>AssetBytes-bytesRead){metadataLimit=true;return false;}
            ++calls;bytesRead+=bytes;return input(address,dst,bytes);
        };
        auto found=std::find_if(assets_.begin(),assets_.end(),[&](const Asset& a){return a.shared==shared&&a.data==data;});
        if(found!=assets_.end()&&found->checked==frame_&&(!needPose||found->poseReady)){found->used=frame_;return &*found;}
        std::array<unsigned char,304> header{};if(!read(data,header.data(),header.size()))return nullptr;
        if(found!=assets_.end()&&found->header==header&&(!needPose||found->poseReady)){found->checked=found->used=frame_;return &*found;}
        if(found!=assets_.end())assets_.erase(found);
        if(loads_++>=4){++budgetLimited;return nullptr;}
        if(std::memcmp(header.data(),"MD20",4)||at<std::uint32_t>(header,4)!=264){++malformed;return nullptr;}
        Asset a;a.header=header;a.shared=shared;a.data=data;a.checked=a.used=frame_;a.bytes=sizeof a;std::vector<std::array<unsigned char,156>> records;
        const auto readFailures=failures,limited=budgetLimited;
        const unsigned lights=at<std::uint32_t>(header,264),bones=at<std::uint32_t>(header,44),sequences=at<std::uint32_t>(header,28),loops=at<std::uint32_t>(header,20);
        if(lights>MaxLights||bones>MaxBones||sequences>MaxSequences||loops>MaxSequences){++unsupported;return nullptr;}
        if(lights||needPose){a.poseReady=true;a.lights.reserve(lights);a.bytes+=lights*sizeof(Authored)+bones*sizeof(SceneDetail::BoneTracks);a.bones.resize(bones);a.poses.resize(bones);a.sequences.resize(sequences);a.loops.resize(loops);
            if((bones&&!read(at<std::uint32_t>(header,48),a.bones.data(),bones*88))||(sequences&&!read(at<std::uint32_t>(header,32),a.sequences.data(),sequences*64))||(loops&&!read(at<std::uint32_t>(header,24),a.loops.data(),loops*4)))return nullptr;
            a.bytes+=bones*88+sequences*64+loops*4;records.resize(lights);
            if(lights&&!read(at<std::uint32_t>(header,268),records.data(),lights*156))return nullptr;
            for(unsigned i=0;i<lights;++i){const auto& r=records[i];Authored l;l.index=i;l.bone=at<std::uint16_t>(r,2);
                for(unsigned k=0;k<3;++k)l.point[k]=at<float>(r,4+4*k);
                bool finite=true;for(float p:l.point)finite=finite&&std::isfinite(p)&&std::fabs(p)<100000;
                if(!finite||at<std::uint16_t>(r,0)!=1||!prepareLight(read,a,l)){++unsupported;continue;}
                std::memcpy(l.tracks.data(),r.data()+56,40);std::memcpy(l.tracks.data()+40,r.data()+136,20);a.lights.push_back(std::move(l));
            }
        }
        if(a.poseReady){const unsigned count=at<std::uint32_t>(header,240);if(count>256){++unsupported;return nullptr;}a.attachmentData.resize(count);a.attachments.resize(count);a.bytes+=count*(40+sizeof(SceneDetail::Attachment));
            if(count&&!read(at<std::uint32_t>(header,244),a.attachmentData.data(),count*40))return nullptr;}
        if(metadataLimit){++metadataLimited;a.lights.clear();} // Unsupported metadata size must not block later models.
        if(failures!=readFailures||budgetLimited!=limited)return nullptr; // A temporary read/budget failure is retried, never cached as unsupported.
        // Optional fixture proof follows all required native metadata. A stale
        // name/vertex pointer must not remove ordinary light or force retries.
        if(lights&&!metadataLimit){a.fixtureTag=std::uint32_t(NorthlightNativeFixtures::match(read,fixtures_.get(),header,a.bones,records,AssetBytes-bytesRead));if(a.fixtureTag)a.fixtureSources=fixtures_->records[a.fixtureTag-1].sources;}
        std::size_t bytes=a.bytes;for(const auto& cached:assets_)bytes+=cached.bytes;
        while(!assets_.empty()&&(assets_.size()>=AssetLimit||bytes>CacheBytes)){auto oldest=std::min_element(assets_.begin(),assets_.end(),[](const auto& x,const auto& y){return x.used<y.used;});if(oldest->used==frame_){++budgetLimited;return nullptr;}bytes-=oldest->bytes;assets_.erase(oldest);}
        if(a.bytes>CacheBytes){++budgetLimited;return nullptr;}assets_.push_back(std::move(a));return &assets_.back();
    }
    template<class Read> WorldEntry* parentWorld(Read& read,std::uint32_t address,std::uint32_t now,unsigned depth){
        using namespace SceneDetail;if(depth>=16||!pointer(address)){++malformed;return nullptr;}
        for(auto& entry:worlds_)if(entry.frame==frame_&&entry.address==address)return entry.valid&&!entry.busy?&entry:nullptr;
        auto& entry=worlds_[nextWorld_++%worlds_.size()];entry.frame=frame_;entry.address=address;entry.valid=false;entry.busy=true;
        if(!read(address,entry.model.data(),entry.model.size())||at<std::uint32_t>(entry.model,40)!=scene_||!(at<std::uint32_t>(entry.model,16)&1)){entry.busy=false;return nullptr;}
        const auto shared=at<std::uint32_t>(entry.model,44);std::uint32_t data=0;
        if(!pointer(shared)||!sharedData(read,shared,data)||!pointer(data)||(entry.asset=asset(read,shared,data,true))==nullptr){entry.busy=false;return nullptr;}
        entry.valid=world(read,entry.model,*entry.asset,now,entry.matrix,entry.alpha,entry.visible,depth+1);entry.busy=false;return entry.valid?&entry:nullptr;
    }
    template<class Read> bool world(Read& read,const ModelData& model,Asset& a,std::uint32_t now,Matrix& result,float& alpha,bool& visible,unsigned depth=0){
        using namespace SceneDetail;Matrix local;for(unsigned k=0;k<16;++k)local[k]=at<float>(model,0xb4+4*k);
        alpha=at<float>(model,0x178);visible=true;if(!affine(local)||!std::isfinite(alpha)||alpha<0||alpha>1.001f)return false;
        const auto parent=at<std::uint32_t>(model,0x48);if(!parent){result=local;return true;}
        auto* p=parentWorld(read,parent,now,depth);if(!p)return false;
        const auto flags=at<std::uint32_t>(model,16),index=at<std::uint32_t>(model,0x54);unsigned bone=0;float point[3]{};
        const unsigned char* attachment=nullptr;
        if(index==0xffff){if(!(flags&0x40000))return false;}
        else{if(index>=p->asset->attachmentData.size())return false;attachment=p->asset->attachmentData[index].data();bone=at<std::uint16_t>(attachment,4);for(unsigned k=0;k<3;++k)point[k]=at<float>(attachment,8+4*k);}
        Runtime<Read> runtime(*this,read,*p->asset,p->model,p->matrix,now);Matrix parentBone;if(!runtime.pose(bone,parentBone))return false;
        visible=p->visible;
        if(attachment){const auto timing=runtime.timing(bone);if(!timing.valid)return false;bool ok=true;
            const auto enabled=runtime.poseValue(p->asset->attachments[index].enabled,attachment+20,1,false,timing,{1,0,0,0},ok);if(!ok)return false;visible=visible&&enabled[0]>=1.f;translate(parentBone,point);}
        if(!(at<std::uint32_t>(a.header,16)&4)&&!(flags&0x100000))alpha*=p->alpha;
        result=multiply(local,parentBone);return affine(result);
    }
public:
    unsigned scanned=0,unsupported=0,malformed=0,failures=0,budgetLimited=0,metadataLimited=0;
    unsigned modelCount()const{return unsigned(models_.size());}
    unsigned assetCount()const{return unsigned(assets_.size());}
    unsigned readCalls()const{return readCalls_;}
    std::size_t readBytes()const{return readBytes_;}
    void fixtures(std::shared_ptr<const NorthlightNativeFixtures::Catalog> catalog){if(catalog!=fixtures_){fixtures_=std::move(catalog);assets_.clear();}}
    void reset(){models_.clear();assets_.clear();scene_=cursor_=lastHead_=lastScan_=0;}
    template<class Read> static bool verify(Read read){
        // Local 12340 PE: world scene, intrusive list, loaded data and light layout.
        static const unsigned char world[]={0x8b,0x35,0x4c,0x75,0xcd,0x00};
        static const unsigned char attach[]={0x8d,0x42,0x08,0x89,0x46,0x08,0x89,0x56,0x28,0x8b,0x38,0x8d,0x4e,0x0c};
        static const unsigned char data[]={0x8b,0x46,0x2c,0x8b,0x80,0x50,0x01,0x00,0x00};
        static const unsigned char light[]={0x8b,0x8e,0xd0,0x01,0x00,0x00,0x52,0x8d,0x4c,0x19,0x68};
        static const unsigned char parent[]={0x89,0x46,0x54,0x33,0xc0,0x3b,0xd3,0x0f,0x95,0xc0,0x89,0x7e,0x50,0x89,0x4e,0x48};
        static const unsigned char attachment[]={0x0f,0xb7,0x74,0xd0,0x04,0x8d,0x04,0xd0,0xc1,0xe6,0x06,0x03,0xb3,0x98,0x00,0x00,0x00};
        static const unsigned char packed[]={0x0f,0xb7,0x0c,0xd0,0x8d,0x04,0xd0,0x89,0x4d,0x0c};
        static const unsigned char procedure[]={0x69,0xc0,0xac,0x00,0x00,0x00,0x03,0x83,0x94,0x00,0x00,0x00,0x8b,0xf0,0x83,0xbe,0x88,0x00,0x00,0x00,0x00};
        static const unsigned char inverse[]={0x8d,0x8e,0xc4,0x00,0x00,0x00};
        struct Proof {std::uint32_t address;const unsigned char* bytes;unsigned size;};
        const Proof proofs[]={{0x79abdf,world,sizeof world},{0x834550,attach,sizeof attach},{0x834575,data,sizeof data},{0x834593,light,sizeof light},{0x8316b0,parent,sizeof parent},{0x82e625,attachment,sizeof attachment},{0x8286d2,packed,sizeof packed},{0x82738a,procedure,sizeof procedure},{0x821ab5,inverse,sizeof inverse}};
        for(const auto& p:proofs){unsigned char bytes[32];if(!read(p.address,bytes,p.size)||std::memcmp(bytes,p.bytes,p.size))return false;}return true;
    }
    template<class Read> void update(Read input,const float* camera,Snapshot& output){
        using namespace SceneDetail;output.clear();loads_=0;readBytes_=0;readCalls_=0;viewsRead_=viewsValid_=false;nextWorld_=0;++frame_;
        if(!camera)return;for(unsigned k=0;k<3;++k)if(!std::isfinite(camera[k])||std::fabs(camera[k])>=100000)return;
        if(!checked_){checked_=true;proven_=verify(input);}if(!proven_)return;
        const auto read=[&](std::uint32_t address,void* dst,std::size_t bytes){
            if(!pointer(address)||bytes>std::uint64_t(0xffff0000u)-address){++failures;++malformed;return false;}
            if(bytes>1024*1024-28-readBytes_||readCalls_>=4094){++budgetLimited;return false;}
            ++readCalls_;readBytes_+=bytes;if(input(address,dst,bytes))return true;++failures;return false;};
        std::uint32_t scene=0;if(!read(ScenePointer,&scene,4)||!pointer(scene)){reset();return;}
        std::array<std::uint32_t,6> header{};if(!read(scene,header.data(),sizeof header))return;
        if(scene_!=scene){reset();scene_=scene;}
        std::array<float,Capacity> distances{};
        const auto evaluate=[&](){for(auto it=models_.begin();it!=models_.end();){if(it->second.visited==frame_){++it;continue;}it->second.visited=frame_;const auto address=it->first;const auto model=it->second;std::array<unsigned char,ModelBytes> m{};std::uint32_t data=0;
            const auto limited=budgetLimited;
            if(!read(address,m.data(),m.size())||at<std::uint32_t>(m,40)!=scene||at<std::uint32_t>(m,44)!=model.shared||at<std::uint32_t>(m,0x74)!=model.origin||!(at<std::uint32_t>(m,16)&1)||!sharedData(read,model.shared,data)||data!=model.data){if(budgetLimited!=limited){++it;continue;}it=models_.erase(it);lastScan_=0;continue;}
            Asset* a=asset(read,model.shared,data);if(!a){++it;continue;}
            Matrix matrix;float alpha=0;bool visible=false;
            if(!world(read,m,*a,header[3],matrix,alpha,visible)){++unsupported;++it;continue;}
            Runtime<decltype(read)> runtime(*this,read,*a,m,matrix,header[3]);it->second.distance=std::numeric_limits<float>::infinity();
            for(auto& l:a->lights){Point p;p.id=0xfea0000000000000ull|(model.firstId+l.index);
                if(!at<std::uint32_t>(m,0x48)){const auto anchor=transform(l.point,matrix);bool finite=true;for(unsigned k=0;k<3;++k)finite=finite&&std::isfinite(anchor[k])&&std::fabs(anchor[k])<100000;if(finite){p.flags=RootAnchor|((a->fixtureSources>>l.index)&1?WorldFixture:0);if(p.flags&WorldFixture)p.reserved=a->fixtureTag;std::copy(anchor.begin(),anchor.begin()+3,p.anchor);}}
                if(!runtime.position(l,p.position)){++unsupported;continue;}const float d=distance(p.position,camera);it->second.distance=std::min(it->second.distance,d);
                const float reach=NorthlightLocalLightSelection::VisibilityEnd+12.f;if(!std::isfinite(d)||d>reach*reach||!visible||alpha==0)continue;
                const auto timing=runtime.timing(l.bone);if(!timing.valid){++malformed;continue;}
                std::array<unsigned char,LightBytes> liveStorage;const unsigned char* live=nullptr;if(!runtime.light(l.index,liveStorage,live))continue;
                bool ok=true;if(!at<std::uint32_t>(live,0x64)||runtime.poseValue(l.enabled,l.tracks.data()+40,1,false,timing,{1,0,0,0},ok)[0]<1.f||!ok)continue;
                const auto rgb=runtime.poseValue(l.color,l.tracks.data(),3,false,timing,{},ok);const float intensity=runtime.poseValue(l.intensity,l.tracks.data()+20,0,false,timing,{at<std::uint32_t>(l.tracks,24)?0.f:1.f,0,0,0},ok)[0];
                for(unsigned k=0;k<3;++k){p.color[k]=rgb[k]*intensity*alpha;p.attenuation[k]=at<float>(live,0xbc+4*k);
                    ok=ok&&std::isfinite(p.color[k])&&p.color[k]>=0&&p.color[k]<=10000&&std::isfinite(p.attenuation[k])&&p.attenuation[k]>=0&&p.attenuation[k]<=10000;}
                if(!ok||p.attenuation[0]+p.attenuation[1]+p.attenuation[2]<.001f){++malformed;continue;}if(std::max({p.color[0],p.color[1],p.color[2]})<=0)continue;
                unsigned slot=output.count;if(slot==Capacity){slot=unsigned(std::max_element(distances.begin(),distances.end())-distances.begin());if(d>=distances[slot])continue;}else ++output.count;
                output.lights[slot]=p;distances[slot]=d;
            }
            const float reach=NorthlightLocalLightSelection::VisibilityEnd+12.f;
            if(std::isfinite(it->second.distance)&&it->second.distance>reach*reach){it=models_.erase(it);continue;}++it;
        }};
        // Refresh known sources before spending the remaining budget on cold
        // metadata. New sources are evaluated once in the same update.
        evaluate();
        const auto discover=[&](){
        if(!cursor_&&(header[2]!=lastHead_||!lastScan_||std::uint32_t(header[3]-lastScan_)>=200)){cursor_=header[2];lastHead_=header[2];lastScan_=header[3];}
        std::array<std::uint32_t,DiscoverySteps> walked{};unsigned walkedCount=0;
        for(unsigned step=0;cursor_&&step<DiscoverySteps;++step){const auto address=cursor_;
            if(std::find(walked.begin(),walked.begin()+walkedCount,address)!=walked.begin()+walkedCount){cursor_=0;++malformed;break;}walked[walkedCount++]=address;
            std::array<unsigned char,ModelBytes> m{};
            const auto beforeRead=budgetLimited;
            if(!read(address,m.data(),m.size())||at<std::uint32_t>(m,40)!=scene){cursor_=budgetLimited!=beforeRead?address:0;break;}
            cursor_=at<std::uint32_t>(m,12);++scanned;if(cursor_==address){cursor_=0;++malformed;break;}
            if(models_.count(address))continue; // Its current identity/placement is validated below.
            const auto shared=at<std::uint32_t>(m,44);std::uint32_t data=0;
            if(!(at<std::uint32_t>(m,16)&1)||!pointer(shared)||!sharedData(read,shared,data)||!pointer(data))continue;
            const auto limited=budgetLimited;Asset* a=asset(read,shared,data);if(budgetLimited!=limited){cursor_=address;break;}if(!a||a->lights.empty())continue;
            const auto origin=at<std::uint32_t>(m,0x74);auto found=models_.find(address);
            if(found!=models_.end()&&(found->second.shared!=shared||found->second.data!=data||found->second.origin!=origin)){models_.erase(found);found=models_.end();}
            if(found==models_.end()){
                Matrix matrix;float alpha=0;bool visible=false;if(!world(read,m,*a,header[3],matrix,alpha,visible))continue;
                float d=std::numeric_limits<float>::infinity();Runtime<decltype(read)> runtime(*this,read,*a,m,matrix,header[3]);
                for(const auto& l:a->lights){float point[3];if(runtime.position(l,point))d=std::min(d,distance(point,camera));}
                if(budgetLimited!=limited){cursor_=address;break;}
                if(!std::isfinite(d)||d>(NorthlightLocalLightSelection::VisibilityEnd+12.f)*(NorthlightLocalLightSelection::VisibilityEnd+12.f))continue;
                if(models_.size()==ModelLimit){auto farthest=std::max_element(models_.begin(),models_.end(),[](const auto& x,const auto& y){return x.second.distance<y.second.distance;});if(d>=farthest->second.distance)continue;models_.erase(farthest);}
                const auto id=serial_;serial_+=MaxLights;models_.emplace(address,Model{shared,data,origin,0,id+1,d});
            }
        }
        };
        discover();evaluate();
        std::uint32_t again=0;std::array<std::uint32_t,6> end{};
        readCalls_+=2;readBytes_+=28; // Reserved independently of the work budget.
        if(!input(ScenePointer,&again,4)||again!=scene||!input(scene,end.data(),sizeof end)||end!=header){output.clear();return;}
        output.scene=scene;output.time=header[3];output.valid=1;
    }
};
} // namespace NorthlightNativeLights
