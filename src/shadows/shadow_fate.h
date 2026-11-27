#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>

// Diagnostic "shadow fate" tracker: during a short sampled window, every model
// draw that reaches replay capture is keyed by its buffers and draw range and
// records one outcome per frame (why it was rejected, or how the actor quota
// treated it). At the end of the window the keys whose shadow flipped between
// drawn and not drawn most often are reported with their outcome histogram.
// Fixed table, no allocation; outside the window nothing runs. The renderer
// enables it only with ShadowFateDiagnostics=1.
// A shadow that is steadily missing never flips, so the window also lists every key
// near a watched point (the player) with its outcomes: capture rejection, selection
// class and how many of its frames it was selected to cast.
namespace NorthlightShadowFate {
enum Fate : unsigned char {Absent,Unknown,Cap4096,Small,CaptureBudget,Blend,AlphaFunc,Projection,Snapshot,Constants,Material,
    NotRanked,Kept,Dropped,Waiting,Rigid,Attached,Exempt,RadiusGate,DistanceCut,Fade,Remembered,FateCount}; /* Remembered: a copy the rigid memory draws while the game does not */ /* 0.3.211 (task 31): the capture admission's rejections; 0.3.215 (task 47): a faded actor's depth-only pre-pass */
inline const char* name(unsigned f){
    static const char* names[FateCount]={"absent","unknown","cap4096","small","captureBudget","blend","alphaFunc","projection","snapshot","constants","material",
        "notRanked","kept","dropped","waiting","rigid","attached","exempt","radiusGate","distanceCut","fade","remembered"};
    return f<FateCount?names[f]:"?";
}
// What became of a selected caster in one shadow map (sun near, sun far, moon near, moon far): left to the cached
// static layer by the geometric proof, drawn into the actor map instead of the near cascade, left to the cached layer
// as a resting M2 doodad, rejected by the map's bounds, or drawn.
enum Replayed : unsigned char {StaticProof,ActorMap,StaticM2,Culled,Drawn,ReplayedCount};
inline const char* replayedName(unsigned r){static const char* names[ReplayedCount]={"cached","actorMap","cachedM2","culled","drawn"};return r<ReplayedCount?names[r]:"?";}
inline const char* mapName(unsigned m){static const char* names[4]={"sunNear","sunFar","moonNear","moonFar"};return m<4?names[m]:"?";}
// cell: the instance's palette root quantized to 1 yd (identical model instances
// share every buffer and range).
struct Key {std::uint64_t vb=0,ib=0;const void* shader=nullptr;std::int32_t base=0,cell[3]={};std::uint32_t start=0,count=0,minimum=0;
    bool operator==(const Key& o)const{return vb==o.vb&&ib==o.ib&&shader==o.shader&&base==o.base&&start==o.start&&count==o.count&&minimum==o.minimum&&cell[0]==o.cell[0]&&cell[1]==o.cell[1]&&cell[2]==o.cell[2];}};
class Tracker {
public:
    static constexpr unsigned Slots=16384,WindowFrames=120,Period=1200; /* 2048 overflowed in Durotar crowds (~2.4k keys, more per instance); about 3 MiB */
    struct Slot {Key key;bool used=false,drawn=false,lastDrawn=false,skinned=false,rigid=false;unsigned lastFrame=0,lastPresent=~0u,flips=0,present=0,absentRuns=0,absentRun=0,maxAbsentRun=0;
        unsigned histogram[FateCount]={};float distance=0;unsigned triangles=0;unsigned char fate=Absent;
        unsigned drawnFrames=0;float at[3]={};bool located=false,bounded=false;std::uint16_t replayed[4][ReplayedCount]={};};
    static constexpr unsigned NearKeys=400;
    // Frame bookkeeping: a window of WindowFrames every Period frames.
    void beginFrame(bool enabled=true){++frame_;active_=enabled&&(frame_%Period)<WindowFrames;if(active_&&frame_%Period==0)reset();}
    bool active()const{return active_;}
    bool windowEnded()const{return (frame_%Period)==WindowFrames-1;}
    unsigned frame()const{return frame_;}
    int slot(const Key& key,bool skinned,unsigned triangles){
        std::uint64_t h=key.vb*0x9e3779b97f4a7c15ULL^key.ib*0xc2b2ae3d27d4eb4fULL^std::uint64_t(reinterpret_cast<std::uintptr_t>(key.shader))*0x165667b19e3779f9ULL^
            (std::uint64_t(key.start)<<32|key.count)^std::uint64_t(std::uint32_t(key.base))*0x27d4eb2f165667c5ULL^key.minimum^
            (std::uint64_t(std::uint32_t(key.cell[0]))*0x9e3779b1u)^(std::uint64_t(std::uint32_t(key.cell[1]))<<21)^(std::uint64_t(std::uint32_t(key.cell[2]))<<42);
        h^=h>>31;
        for(unsigned probe=0;probe<32;++probe){auto& s=slots_[(h+probe)%Slots];
            if(s.used&&s.key==key){s.skinned=skinned;s.triangles=triangles;return int(&s-slots_.data());}
            if(!s.used){s=Slot{};s.used=true;s.key=key;s.skinned=skinned;s.triangles=triangles;++keys_;return int(&s-slots_.data());}}
        ++overflow_;return -1;
    }
    // One outcome per key and frame; the last recorded outcome of the frame wins
    // (a key drawn twice in a frame keeps its drawn state if either draw casts).
    void record(int index,Fate fate,bool drawn,float distance=0){
        if(index<0||!active_)return;auto& s=slots_[unsigned(index)];
        if(s.lastFrame!=frame_){s.lastFrame=frame_;s.drawn=false;++s.present;}
        s.fate=fate;s.drawn=s.drawn||drawn;if(distance>0)s.distance=distance;if(fate==Rigid||fate==Attached)s.rigid=true;
    }
    // A world position for the near listing: the selection's sampled vertex. A key that never reaches the selection
    // is placed by its palette root cell when it has one.
    void locate(int index,const float* at){
        if(index<0||!active_)return;auto& s=slots_[unsigned(index)];s.at[0]=at[0];s.at[1]=at[1];s.at[2]=at[2];s.located=true;}
    // bounds: the caster's world bounds when the map's culling has them; they place a key the selection never saw
    // (a draw without bones has no sampled vertex and no palette root).
    void replayed(int index,unsigned map,Replayed outcome,const float* low=nullptr,const float* high=nullptr){
        if(index<0||!active_||map>=4)return;auto& s=slots_[unsigned(index)];auto& n=s.replayed[map][outcome];if(n<65535)++n;
        if(!s.located&&low&&high){for(unsigned k=0;k<3;++k)s.at[k]=.5f*(low[k]+high[k]);s.located=s.bounded=true;}}
    // The point the near listing is taken around (the selection's player, else the shadow pivot); null: no listing.
    void watch(const float* centre,float yards){
        watching_=centre&&yards>0;if(!watching_)return;watch_[0]=centre[0];watch_[1]=centre[1];watch_[2]=centre[2];watchYards_=yards;}
    // After selection: count this frame's outcome and drawn<->not flips.
    void endFrame(){
        if(!active_)return;
        for(auto& s:slots_){if(!s.used)continue;
            if(s.lastFrame!=frame_){++s.histogram[Absent];if(s.lastPresent==frame_-1)++s.absentRuns;s.maxAbsentRun=std::max(s.maxAbsentRun,++s.absentRun);continue;}
            s.absentRun=0;
            ++s.histogram[s.fate];s.drawnFrames+=s.drawn;
            if(s.lastPresent==frame_-1&&s.drawn!=s.lastDrawn)++s.flips;
            s.lastPresent=frame_;s.lastDrawn=s.drawn;}
    }
    template<class Log> void report(Log&& log){
        std::array<const Slot*,10> top{};
        for(const auto& s:slots_){if(!s.used||!s.flips)continue;
            for(unsigned i=0;i<top.size();++i)if(!top[i]||s.flips>top[i]->flips){for(unsigned j=top.size()-1;j>i;--j)top[j]=top[j-1];top[i]=&s;break;}}
        unsigned flipping=0;for(const auto& s:slots_)flipping+=s.used&&s.flips;
        log("SHADOW fate window frames=%u keys=%u overflow=%u flippingKeys=%u",WindowFrames,keys_,overflow_,flipping);
        for(const auto* s:top){if(!s)break;std::string h;char buffer[48];
            for(unsigned f=0;f<FateCount;++f)if(s->histogram[f]){std::snprintf(buffer,sizeof buffer,"%s%s:%u",h.empty()?"":",",name(f),s->histogram[f]);h+=buffer;}
            log("SHADOW fate key vb=%llu ib=%llu start=%u count=%u base=%d cell=%d,%d,%d flips=%u present=%u absentRuns=%u maxAbsentRun=%u triangles=%u skinned=%d rigid=%d distance=%.1f fates=%s",
                (unsigned long long)s->key.vb,(unsigned long long)s->key.ib,s->key.start,s->key.count,s->key.base,s->key.cell[0],s->key.cell[1],s->key.cell[2],s->flips,s->present,s->absentRuns,s->maxAbsentRun,s->triangles,s->skinned,s->rigid,s->distance,h.c_str());}
        if(!watching_)return;
        struct Near {const Slot* slot;float yards;};std::array<Near,NearKeys> list{};unsigned found=0,shown=0;
        for(const auto& s:slots_){if(!s.used||!s.present)continue;
            const bool cell=s.key.cell[0]||s.key.cell[1]||s.key.cell[2];if(!s.located&&!cell)continue;
            float q=0;for(unsigned k=0;k<3;++k){const float x=(s.located?s.at[k]:float(s.key.cell[k])+.5f)-watch_[k];q+=x*x;}
            const float yards=std::sqrt(q);if(yards>watchYards_)continue;++found;
            unsigned i=shown;if(shown<NearKeys)++shown;else if(yards>=list[NearKeys-1].yards)continue;else i=NearKeys-1;
            for(;i&&list[i-1].yards>yards;--i)list[i]=list[i-1];list[i]={&s,yards};}
        log("SHADOW fate near centre=(%.1f %.1f %.1f) yards=%.0f keys=%u shown=%u",watch_[0],watch_[1],watch_[2],watchYards_,found,shown);
        for(unsigned i=0;i<shown;++i){const Slot& s=*list[i].slot;std::string h;char buffer[48];
            for(unsigned f=0;f<FateCount;++f)if(s.histogram[f]){std::snprintf(buffer,sizeof buffer,"%s%s:%u",h.empty()?"":",",name(f),s.histogram[f]);h+=buffer;}
            std::string maps;
            for(unsigned m=0;m<4;++m){std::string one;
                for(unsigned r=0;r<ReplayedCount;++r)if(s.replayed[m][r]){std::snprintf(buffer,sizeof buffer,"%s%s:%u",one.empty()?"":",",replayedName(r),unsigned(s.replayed[m][r]));one+=buffer;}
                if(!one.empty()){maps+=maps.empty()?"":" ";maps+=mapName(m);maps+="(";maps+=one;maps+=")";}}
            const float* at=s.located?s.at:nullptr;
            log("SHADOW fate near key fromCentre=%.1f at=(%.1f %.1f %.1f) placed=%s present=%u cast=%u flips=%u triangles=%u skinned=%d rigid=%d start=%u count=%u fates=%s maps=%s",
                list[i].yards,at?at[0]:float(s.key.cell[0]),at?at[1]:float(s.key.cell[1]),at?at[2]:float(s.key.cell[2]),at?(s.bounded?"bounds":"vertex"):"root",s.present,s.drawnFrames,s.flips,s.triangles,s.skinned,s.rigid,s.key.start,s.key.count,h.c_str(),maps.empty()?"none":maps.c_str());}
        // Keys that never got a position (rejected at capture without a palette root) cannot be listed by place: their
        // outcomes by shape, the most seen first. A model's shapes are told apart by their primitive counts.
        struct Shape {std::uint32_t count=0;unsigned keys=0,present=0,cast=0,histogram[FateCount]={};bool skinned=false;};
        std::array<Shape,48> shapes{};unsigned used=0,others=0;
        for(const auto& s:slots_){if(!s.used||!s.present||s.located||s.key.cell[0]||s.key.cell[1]||s.key.cell[2])continue;
            Shape* shape=nullptr;for(unsigned i=0;i<used&&!shape;++i)if(shapes[i].count==s.key.count&&shapes[i].skinned==s.skinned)shape=&shapes[i];
            if(!shape){if(used==shapes.size()){++others;continue;}shape=&shapes[used++];shape->count=s.key.count;shape->skinned=s.skinned;}
            ++shape->keys;shape->present+=s.present;shape->cast+=s.drawnFrames;for(unsigned f=0;f<FateCount;++f)shape->histogram[f]+=s.histogram[f];}
        std::sort(shapes.begin(),shapes.begin()+used,[](const Shape& a,const Shape& b){return a.present>b.present;});
        log("SHADOW fate unplaced shapes=%u others=%u",used,others);
        for(unsigned i=0;i<used;++i){const Shape& s=shapes[i];std::string h;char buffer[48];
            for(unsigned f=1;f<FateCount;++f)if(s.histogram[f]){std::snprintf(buffer,sizeof buffer,"%s%s:%u",h.empty()?"":",",name(f),s.histogram[f]);h+=buffer;}
            log("SHADOW fate unplaced shape triangles=%u skinned=%d keys=%u present=%u cast=%u fates=%s",s.count,int(s.skinned),s.keys,s.present,s.cast,h.c_str());}
    }
    void reset(){for(auto& s:slots_)s=Slot{};keys_=overflow_=0;}
private:
    std::array<Slot,Slots> slots_{};unsigned frame_=0,keys_=0,overflow_=0;bool active_=false,watching_=false;float watch_[3]={},watchYards_=0;
};
}
