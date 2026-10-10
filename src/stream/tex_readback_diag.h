// 0.3.206 (task 31): Diagnostics-only report of the command stream's texture re-lock readbacks (lockImage's ONE SyncLock readback of a written level without a shadow, which drains the queue):
// which texture levels do it, whether the time is the queue wait or the lock + copy, whole vs partial rect locks, and the frames between locks. Pure data: no dependency on SubRes.
// Game thread only (StreamCore::texDiag); recorded only while StreamCore::timing (Diagnostics on), reported and cleared every window by StreamDevice::presentCommon.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct TexReadbackDiag {
    static constexpr std::size_t kMaxLevels=4096,kTop=10;   // level table cap per window; entries per top list
    static constexpr unsigned kBuckets=6;                    // gap histogram: [0-1] [2-9] [10-59] [60-299] [300+] [first = no previous lock]
    static constexpr unsigned kGoneFresh=1,kGoneRelocked=2,kGoneSkipped=3,kGoneNever=0;   // = SubRes::Gone (the cause of a readback)
    static unsigned bucketOf(bool hasGap,std::uint64_t gap){return !hasGap?5:gap<=1?0:gap<10?1:gap<60?2:gap<300?3:4;}
    // What a lock knows about its level.
    struct Meta {std::uint32_t fmt=0,w=0,h=0,d=1,level=0,face=0,pool=0,usage=0,baseW=0,baseH=0;std::uint64_t levelBytes=0;void* caller=nullptr;};
    struct Sums {   // counters shared by a level and a group
        std::uint64_t locks=0,readbacks=0,readbackBytes=0,rectBytes=0,passSyncs=0,wholeLocks=0,partialLocks=0,gapCount=0,waitNs=0,lockNs=0,copyNs=0;
        double coverageSum=0,gapSum=0;
        void lock(bool whole,double cover,bool hasGap,std::uint64_t gap){++locks;(whole?wholeLocks:partialLocks)++;coverageSum+=cover;if(hasGap){gapSum+=double(gap);++gapCount;}}
        void readback(std::uint64_t bytes,std::uint64_t rect,std::uint64_t wait,std::uint64_t lk,std::uint64_t cp){++readbacks;readbackBytes+=bytes;rectBytes+=rect;waitNs+=wait;lockNs+=lk;copyNs+=cp;}
        bool interesting()const{return readbacks>0||passSyncs>0;}
    };
    struct LevelStat:Sums {Meta m;std::uint64_t gone[4]{};};   // gone[]: readbacks by cause (index = SubRes::Gone: none/never, fresh, relocked, skipped)
    struct GroupKey {
        std::uint32_t fmt,w,h,pool,usage;
        bool operator==(const GroupKey& o)const{return fmt==o.fmt&&w==o.w&&h==o.h&&pool==o.pool&&usage==o.usage;}
    };
    struct GroupHash {std::size_t operator()(const GroupKey& k)const{std::uint64_t x=k.fmt;x=x*1000003u^k.w;x=x*1000003u^k.h;x=x*1000003u^k.pool;x=x*1000003u^k.usage;return std::size_t(x^(x>>29));}};
    struct GroupStat:Sums {std::uint64_t levels=0;};   // levels: distinct levels locked in the window
    // The records of one lock, kept by lockImage until its readback / pass-through is known (valid until the next report, i.e. within the lock call).
    struct Hit {LevelStat* lv=nullptr;GroupStat* gr=nullptr;unsigned bucket=5;bool on=false;};

    std::uint32_t nextId=1;
    std::unordered_map<std::uint32_t,LevelStat> levelTab;std::unordered_map<GroupKey,GroupStat,GroupHash> groupTab;std::unordered_set<std::uint32_t> seen;
    std::uint64_t readbacks=0,readbackBytes=0,rectBytes=0,waitNs=0,lockNs=0,copyNs=0,overflow=0,gapAll[kBuckets]{},gapSkip[kBuckets]{};
    bool active=false;

    Hit lock(std::uint32_t id,const Meta& m,bool whole,double cover,bool hasGap,std::uint64_t gap){
        Hit h;h.on=true;active=true;h.bucket=bucketOf(hasGap,gap);
        GroupStat& g=groupTab[GroupKey{m.fmt,m.baseW,m.baseH,m.pool,m.usage}];h.gr=&g;
        if(seen.insert(id).second)++g.levels;
        auto it=levelTab.find(id);
        if(it==levelTab.end()){if(levelTab.size()>=kMaxLevels)++overflow;else{it=levelTab.emplace(id,LevelStat{}).first;it->second.m=m;}}
        if(it!=levelTab.end())h.lv=&it->second;
        g.lock(whole,cover,hasGap,gap);if(h.lv)h.lv->lock(whole,cover,hasGap,gap);
        return h;
    }
    void readback(const Hit& h,std::uint64_t bytes,std::uint64_t rect,unsigned cause,std::uint64_t wait,std::uint64_t lk,std::uint64_t cp){
        if(!h.on)return;
        ++readbacks;readbackBytes+=bytes;rectBytes+=rect;waitNs+=wait;lockNs+=lk;copyNs+=cp;++gapAll[h.bucket];if(cause==kGoneSkipped)++gapSkip[h.bucket];
        h.gr->readback(bytes,rect,wait,lk,cp);if(h.lv){h.lv->readback(bytes,rect,wait,lk,cp);++h.lv->gone[cause<4?cause:0];}
    }
    void passSync(const Hit& h){if(!h.on)return;++h.gr->passSyncs;if(h.lv)++h.lv->passSyncs;}

    template<class S> static bool before(const S& a,const S& b){
        if(a.readbackBytes!=b.readbackBytes)return a.readbackBytes>b.readbackBytes;
        if(a.readbacks!=b.readbacks)return a.readbacks>b.readbacks;
        return a.locks>b.locks;
    }
    static double pct(const Sums& s){return s.locks?double(s.wholeLocks)*100.0/double(s.locks):0.0;}
    static double cover(const Sums& s){return s.locks?s.coverageSum/double(s.locks):0.0;}
    static double gapFrames(const Sums& s){return s.gapCount?s.gapSum/double(s.gapCount):0.0;}
    // Writes the window's lines to `sink` (const char*), then clears everything. Nothing is written when the window recorded no lock.
    template<class Sink> void report(Sink&& sink,const char* (*callerModule)(void*),unsigned frames){
        if(!active){clear();return;}
        char b[640];
        auto hist=[](const std::uint64_t* g,char* o,std::size_t n){std::snprintf(o,n,"0-1=%llu,2-9=%llu,10-59=%llu,60-299=%llu,300+=%llu,first=%llu",(unsigned long long)g[0],(unsigned long long)g[1],(unsigned long long)g[2],(unsigned long long)g[3],(unsigned long long)g[4],(unsigned long long)g[5]);};
        char h1[200],h2[200];hist(gapAll,h1,sizeof h1);hist(gapSkip,h2,sizeof h2);
        std::snprintf(b,sizeof b,"CSTREAM TEXREADBACK window frames=%u readbacks=%llu rbMB=%.2f rectMB=%.2f waitMs=%.3f lockMs=%.3f copyMs=%.3f gap[%s] gapFreshSkip[%s] levels=%zu overflow=%llu",
            frames,(unsigned long long)readbacks,readbackBytes/1048576.0,rectBytes/1048576.0,waitNs/1e6,lockNs/1e6,copyNs/1e6,h1,h2,seen.size(),(unsigned long long)overflow);
        sink(b);
        std::vector<const LevelStat*> lv;for(const auto& e:levelTab)if(e.second.interesting())lv.push_back(&e.second);
        const std::size_t nl=std::min(kTop,lv.size());
        std::partial_sort(lv.begin(),lv.begin()+nl,lv.end(),[](const LevelStat* a,const LevelStat* c){return before(*a,*c);});
        for(std::size_t i=0;i<nl;++i){
            const LevelStat& s=*lv[i];char who[64];
            if(callerModule&&s.m.caller)std::snprintf(who,sizeof who,"%s",callerModule(s.m.caller));else std::snprintf(who,sizeof who,"0x%llx",(unsigned long long)(std::uintptr_t)s.m.caller);
            std::snprintf(b,sizeof b,"CSTREAM TEXREADBACK top#%u fmt=%u size=%ux%u lvl=%u face=%u pool=%u usage=0x%x levelKB=%.1f locks=%llu rb=%llu rbMB=%.2f rectKB=%.1f whole%%=%.0f cover=%.2f gapFrames=%.1f gone[fresh=%llu,relocked=%llu,skip=%llu,never=%llu] passSync=%llu waitMs=%.3f caller=%s",
                unsigned(i+1),s.m.fmt,s.m.w,s.m.h,s.m.level,s.m.face,s.m.pool,s.m.usage,s.m.levelBytes/1024.0,(unsigned long long)s.locks,(unsigned long long)s.readbacks,s.readbackBytes/1048576.0,s.rectBytes/1024.0,pct(s),cover(s),gapFrames(s),
                (unsigned long long)s.gone[kGoneFresh],(unsigned long long)s.gone[kGoneRelocked],(unsigned long long)s.gone[kGoneSkipped],(unsigned long long)s.gone[kGoneNever],(unsigned long long)s.passSyncs,s.waitNs/1e6,who);
            sink(b);
        }
        std::vector<std::pair<const GroupKey*,const GroupStat*>> gr;for(const auto& e:groupTab)if(e.second.interesting())gr.emplace_back(&e.first,&e.second);
        const std::size_t ng=std::min(kTop,gr.size());
        std::partial_sort(gr.begin(),gr.begin()+ng,gr.end(),[](const auto& a,const auto& c){return before(*a.second,*c.second);});
        for(std::size_t i=0;i<ng;++i){
            const GroupKey& k=*gr[i].first;const GroupStat& s=*gr[i].second;
            std::snprintf(b,sizeof b,"CSTREAM TEXREADBACK kind#%u fmt=%u size=%ux%u pool=%u usage=0x%x levels=%llu locks=%llu rb=%llu rbMB=%.2f rectMB=%.2f whole%%=%.0f cover=%.2f gapFrames=%.1f",
                unsigned(i+1),k.fmt,k.w,k.h,k.pool,k.usage,(unsigned long long)s.levels,(unsigned long long)s.locks,(unsigned long long)s.readbacks,s.readbackBytes/1048576.0,s.rectBytes/1048576.0,pct(s),cover(s),gapFrames(s));
            sink(b);
        }
        clear();
    }
    void clear(){
        levelTab.clear();groupTab.clear();seen.clear();readbacks=readbackBytes=rectBytes=waitNs=lockNs=copyNs=overflow=0;
        for(auto& x:gapAll)x=0;for(auto& x:gapSkip)x=0;active=false;
    }
    bool empty()const{return levelTab.empty()&&groupTab.empty()&&seen.empty()&&!active;}
};
