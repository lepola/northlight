// 0.3.206 (task 31): Diagnostics-only report of the command stream's texture re-lock readbacks (lockImage's ONE SyncLock readback of a written level without a shadow, which drains the queue):
// which texture levels do it, whether the time is the queue wait or the lock + copy, whole vs partial rect locks, and the frames between locks. Pure data: no dependency on SubRes.
// Rows (LevelStat) exist ONLY for levels that had a readback or a pass-through in the window (cap kMaxLevels): a level's window counters live in its SubRes (LockHist, lazily reset when the
// window number changes; the previous lock's frame and epoch live next to it and survive the reset) and is copied into the row at the level's first readback / pass-through; from then on the row is the source of truth for that level's locks. Groups (kinds) count every lock.
// Gaps (frames between a level's locks) are measured ACROSS report windows: a lock at frame 599 and the next at 601 is a gap of 2 even though a report falls between them; only Diagnostics off->on (epoch) cuts a gap.
// Game thread only (StreamCore::texDiag); recorded only while StreamCore::timing (Diagnostics on), reported and cleared every window by StreamDevice::presentCommon.
#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
    // A level's lock counters since its window started (lives in SubRes; reset when the window changes). window: TexReadbackDiag::window the counters belong to (0 = never).
    // What survives a window is NOT here: SubRes::diagLastLock (frameNo+1 of the previous lock, 0 = none) and SubRes::diagEpoch (the Diagnostics on/off epoch of that lock), passed to lock().
    struct LockHist {
        std::uint32_t window=0,locks=0,wholeLocks=0,partialLocks=0,gapCount=0;double coverageSum=0,gapSum=0;
    };
    struct Sums {   // counters shared by a level and a group
        std::uint64_t locks=0,readbacks=0,readbackBytes=0,rectBytes=0,passSyncs=0,wholeLocks=0,partialLocks=0,gapCount=0,waitNs=0,lockNs=0,copyNs=0,rbWhole=0,express=0,expressWaitNs=0;
        double coverageSum=0,gapSum=0,rbCoverSum=0;   // rbWhole / rbCoverSum: over the locks that TRIGGERED a readback
        void lock(bool whole,double cover,bool hasGap,std::uint64_t gap){++locks;(whole?wholeLocks:partialLocks)++;coverageSum+=cover;if(hasGap){gapSum+=double(gap);++gapCount;}}
        void readback(std::uint64_t bytes,std::uint64_t rect,std::uint64_t wait,std::uint64_t lk,std::uint64_t cp,bool whole,double cover,bool ex){++readbacks;if(ex){++express;expressWaitNs+=wait;}readbackBytes+=bytes;rectBytes+=rect;waitNs+=wait;lockNs+=lk;copyNs+=cp;if(whole)++rbWhole;rbCoverSum+=cover;}
        bool interesting()const{return readbacks>0||passSyncs>0;}
    };
    struct LevelStat:Sums {Meta m;std::uint64_t gone[4]{};void* rbCaller=nullptr;};   // rbCaller: the caller of the last lock that triggered a readback   // gone[]: readbacks by cause (index = SubRes::Gone: none/never, fresh, relocked, skipped)
    struct GroupKey {
        std::uint32_t fmt,w,h,pool,usage;
        bool operator==(const GroupKey& o)const{return fmt==o.fmt&&w==o.w&&h==o.h&&pool==o.pool&&usage==o.usage;}
    };
    struct GroupHash {std::size_t operator()(const GroupKey& k)const{std::uint64_t x=k.fmt;x=x*1000003u^k.w;x=x*1000003u^k.h;x=x*1000003u^k.pool;x=x*1000003u^k.usage;return std::size_t(x^(x>>29));}};
    struct GroupStat:Sums {std::uint64_t levels=0,rbLevels=0;};   // levels: distinct levels locked in the window; rbLevels: distinct levels with a readback / pass-through
    // The records of one lock, kept by lockImage until its readback / pass-through is known (valid until the next report, i.e. within the lock call).
    struct Hit {LevelStat* lv=nullptr;GroupStat* gr=nullptr;LockHist* hist=nullptr;unsigned bucket=5;bool on=false,whole=false;double cover=0;std::uint32_t id=0;void* caller=nullptr;Meta m;};

    std::uint32_t nextId=1,window=1,epoch=1;   // window: the report window number (bumped at each report / restart); epoch: bumped when Diagnostics comes on again (gaps do not span it)
    std::uint64_t windowStart=0;               // frameNo at the window's start
    std::unordered_map<std::uint32_t,LevelStat> levelTab;std::unordered_map<GroupKey,GroupStat,GroupHash> groupTab;std::unordered_set<std::uint32_t> seen,rbSeen;   // seen: levels locked; rbSeen: levels with a readback / pass-through
    std::uint64_t readbacks=0,readbackBytes=0,rectBytes=0,waitNs=0,lockNs=0,copyNs=0,overflow=0,express=0,expressWaitNs=0,gapAll[kBuckets]{},gapSkip[kBuckets]{};
    std::uint64_t expressLatNs=0;std::unordered_map<std::uint16_t,std::uint64_t> expressBehind;   // express readbacks: summed time until the replay thread reached the boundary; count by the command waited behind (0xFFFF = idle)
    bool active=false;

    // One lock of level `id` at frame `frameNo`; `hist` / `lastLock` / `lastEpoch` are the level's SubRes fields (lastLock and lastEpoch persist across windows). Updates the history (or the row, if the level has one) and the group.
    Hit lock(std::uint32_t id,LockHist& hist,std::uint64_t& lastLock,std::uint32_t& lastEpoch,const Meta& m,bool whole,double cover,std::uint64_t frameNo,void* caller){
        Hit h;h.on=true;active=true;h.id=id;h.caller=caller;h.m=m;h.whole=whole;h.cover=cover;h.hist=&hist;
        if(hist.window!=window){hist=LockHist{};hist.window=window;}   // only the per-window counters restart
        const bool hasGap=lastLock!=0&&lastEpoch==epoch;const std::uint64_t gap=hasGap?frameNo-(lastLock-1):0;
        lastLock=frameNo+1;lastEpoch=epoch;h.bucket=bucketOf(hasGap,gap);
        GroupStat& g=groupTab[GroupKey{m.fmt,m.baseW,m.baseH,m.pool,m.usage}];h.gr=&g;
        if(seen.insert(id).second)++g.levels;
        g.lock(whole,cover,hasGap,gap);
        auto it=levelTab.find(id);
        if(it!=levelTab.end()){h.lv=&it->second;it->second.lock(whole,cover,hasGap,gap);}
        else{++hist.locks;(whole?hist.wholeLocks:hist.partialLocks)++;hist.coverageSum+=cover;if(hasGap){hist.gapSum+=double(gap);++hist.gapCount;}}
        return h;
    }
    // The row of a level with a readback / pass-through: made at its first one in the window from the SubRes history (no row, and one overflow, when the table is full).
    LevelStat* row(Hit& h){
        if(h.lv)return h.lv;
        if(!rbSeen.insert(h.id).second)return nullptr;   // had one before and got no row (full)
        ++h.gr->rbLevels;
        if(levelTab.size()>=kMaxLevels){++overflow;return nullptr;}
        LevelStat& r=levelTab.emplace(h.id,LevelStat{}).first->second;r.m=h.m;
        const LockHist& k=*h.hist;r.locks=k.locks;r.wholeLocks=k.wholeLocks;r.partialLocks=k.partialLocks;r.coverageSum=k.coverageSum;r.gapSum=k.gapSum;r.gapCount=k.gapCount;
        return h.lv=&r;
    }
    void readback(Hit& h,std::uint64_t bytes,std::uint64_t rect,unsigned cause,std::uint64_t wait,std::uint64_t lk,std::uint64_t cp,bool ex=false,std::uint16_t behind=0xFFFF,std::uint64_t latNs=0){
        if(!h.on)return;
        ++readbacks;if(ex){++express;expressWaitNs+=wait;expressLatNs+=latNs;++expressBehind[behind];}readbackBytes+=bytes;rectBytes+=rect;waitNs+=wait;lockNs+=lk;copyNs+=cp;++gapAll[h.bucket];if(cause==kGoneSkipped)++gapSkip[h.bucket];
        h.gr->readback(bytes,rect,wait,lk,cp,h.whole,h.cover,ex);
        if(LevelStat* l=row(h)){l->readback(bytes,rect,wait,lk,cp,h.whole,h.cover,ex);++l->gone[cause<4?cause:0];l->rbCaller=h.caller;}
    }
    void passSync(Hit& h){if(!h.on)return;++h.gr->passSyncs;if(LevelStat* l=row(h))++l->passSyncs;}

    template<class S> static bool before(const S& a,const S& b){
        if(a.readbackBytes!=b.readbackBytes)return a.readbackBytes>b.readbackBytes;
        if(a.readbacks!=b.readbacks)return a.readbacks>b.readbacks;
        return a.locks>b.locks;
    }
    static double pct(const Sums& s){return s.locks?double(s.wholeLocks)*100.0/double(s.locks):0.0;}
    static double cover(const Sums& s){return s.locks?s.coverageSum/double(s.locks):0.0;}
    static double rbPct(const Sums& s){return s.readbacks?double(s.rbWhole)*100.0/double(s.readbacks):0.0;}
    static double rbCover(const Sums& s){return s.readbacks?s.rbCoverSum/double(s.readbacks):0.0;}
    static void who(void* a,const char* (*callerModule)(void*),char* o,std::size_t n){if(callerModule&&a)std::snprintf(o,n,"%s",callerModule(a));else std::snprintf(o,n,"0x%llx",(unsigned long long)(std::uintptr_t)a);}
    static double gapFrames(const Sums& s){return s.gapCount?s.gapSum/double(s.gapCount):0.0;}
    // Writes the window's lines to `sink` (const char*), then clears everything. Nothing is written when the window recorded no lock.
    template<class Sink> void report(Sink&& sink,const char* (*callerModule)(void*),std::uint64_t frameNo,const char* (*cmdNameOf)(unsigned)=nullptr){
        if(!active){restart(frameNo);return;}
        const unsigned frames=unsigned(frameNo-windowStart);
        char b[640];
        auto hist=[](const std::uint64_t* g,char* o,std::size_t n){std::snprintf(o,n,"0-1=%llu,2-9=%llu,10-59=%llu,60-299=%llu,300+=%llu,first=%llu",(unsigned long long)g[0],(unsigned long long)g[1],(unsigned long long)g[2],(unsigned long long)g[3],(unsigned long long)g[4],(unsigned long long)g[5]);};
        char h1[200],h2[200];hist(gapAll,h1,sizeof h1);hist(gapSkip,h2,sizeof h2);
        std::snprintf(b,sizeof b,"CSTREAM TEXREADBACK window frames=%u readbacks=%llu rbMB=%.3f rectMB=%.3f express=%llu expressWaitMs=%.3f waitMs=%.3f lockMs=%.3f copyMs=%.3f gap[%s] gapFreshSkip[%s] levels=%zu overflow=%llu",
            frames,(unsigned long long)readbacks,readbackBytes/1048576.0,rectBytes/1048576.0,(unsigned long long)express,expressWaitNs/1e6,waitNs/1e6,lockNs/1e6,copyNs/1e6,h1,h2,rbSeen.size(),(unsigned long long)overflow);   // waitMs: everything still queued before the readback (e.g. a blocking Present) is in it: the real cost of the drain
        {   // what the express readbacks waited behind: the command being executed when they were posted (idle = the replay thread slept)
            std::vector<std::pair<std::uint64_t,std::uint16_t>> by;std::uint64_t idle=0;
            for(const auto& e:expressBehind){if(e.first==0xFFFF)idle=e.second;else by.emplace_back(e.second,e.first);}
            std::sort(by.begin(),by.end(),[](const auto& x,const auto& y){return x.first!=y.first?x.first>y.first:x.second<y.second;});
            std::size_t n=std::strlen(b);std::uint64_t other=0;
            n+=std::snprintf(b+n,sizeof b-n," expressLatencyMs=%.3f expressBehind[",expressLatNs/1e6);
            for(std::size_t i=0;i<by.size();++i){
                if(i<3){char nm[48];if(cmdNameOf)std::snprintf(nm,sizeof nm,"%s",cmdNameOf(by[i].second));else std::snprintf(nm,sizeof nm,"#%u",unsigned(by[i].second));
                    if(n<sizeof b)n+=std::snprintf(b+n,sizeof b-n,"%s=%llu,",nm,(unsigned long long)by[i].first);}
                else other+=by[i].first;}
            if(n<sizeof b)std::snprintf(b+n,sizeof b-n,"idle=%llu,other=%llu]",(unsigned long long)idle,(unsigned long long)other);
        }
        sink(b);
        std::vector<const LevelStat*> lv;for(const auto& e:levelTab)if(e.second.interesting())lv.push_back(&e.second);
        const std::size_t nl=std::min(kTop,lv.size());
        std::partial_sort(lv.begin(),lv.begin()+nl,lv.end(),[](const LevelStat* a,const LevelStat* c){return before(*a,*c);});
        for(std::size_t i=0;i<nl;++i){
            const LevelStat& s=*lv[i];char w1[64],w2[64];who(s.m.caller,callerModule,w1,sizeof w1);who(s.rbCaller,callerModule,w2,sizeof w2);
            std::snprintf(b,sizeof b,"CSTREAM TEXREADBACK top#%u fmt=%u size=%ux%u lvl=%u face=%u pool=%u usage=0x%x levelKB=%.1f locks=%llu rb=%llu rbMB=%.3f rectKB=%.1f whole%%=%.4g cover=%.4g rbWhole%%=%.4g rbCover=%.4g gapFrames=%.1f gone[fresh=%llu,relocked=%llu,skip=%llu,never=%llu] passSync=%llu express=%llu waitMs=%.3f caller=%s rbCaller=%s",
                unsigned(i+1),s.m.fmt,s.m.w,s.m.h,s.m.level,s.m.face,s.m.pool,s.m.usage,s.m.levelBytes/1024.0,(unsigned long long)s.locks,(unsigned long long)s.readbacks,s.readbackBytes/1048576.0,s.rectBytes/1024.0,pct(s),cover(s),rbPct(s),rbCover(s),gapFrames(s),
                (unsigned long long)s.gone[kGoneFresh],(unsigned long long)s.gone[kGoneRelocked],(unsigned long long)s.gone[kGoneSkipped],(unsigned long long)s.gone[kGoneNever],(unsigned long long)s.passSyncs,(unsigned long long)s.express,s.waitNs/1e6,w1,w2);
            sink(b);
        }
        std::vector<std::pair<const GroupKey*,const GroupStat*>> gr;for(const auto& e:groupTab)if(e.second.interesting())gr.emplace_back(&e.first,&e.second);
        const std::size_t ng=std::min(kTop,gr.size());
        std::partial_sort(gr.begin(),gr.begin()+ng,gr.end(),[](const auto& a,const auto& c){return before(*a.second,*c.second);});
        for(std::size_t i=0;i<ng;++i){
            const GroupKey& k=*gr[i].first;const GroupStat& s=*gr[i].second;
            std::snprintf(b,sizeof b,"CSTREAM TEXREADBACK kind#%u fmt=%u size=%ux%u pool=%u usage=0x%x lockedLevels=%llu rbLevels=%llu locks=%llu rb=%llu rbMB=%.3f rectKB=%.1f whole%%=%.4g cover=%.4g rbWhole%%=%.4g rbCover=%.4g gapFrames=%.1f express=%llu",
                unsigned(i+1),k.fmt,k.w,k.h,k.pool,k.usage,(unsigned long long)s.levels,(unsigned long long)s.rbLevels,(unsigned long long)s.locks,(unsigned long long)s.readbacks,s.readbackBytes/1048576.0,s.rectBytes/1024.0,pct(s),cover(s),rbPct(s),rbCover(s),gapFrames(s),(unsigned long long)s.express);
            sink(b);
        }
        restart(frameNo);
    }
    void restart(std::uint64_t frameNo){clear();++window;windowStart=frameNo;}   // a new window starts at frameNo: SubRes histories of the old one reset lazily
    void resume(std::uint64_t frameNo){restart(frameNo);++epoch;}                // Diagnostics came on again: no stale data, and no gap spans the off period
    void clear(){
        levelTab.clear();groupTab.clear();seen.clear();rbSeen.clear();expressLatNs=0;expressBehind.clear();readbacks=express=expressWaitNs=readbackBytes=rectBytes=waitNs=lockNs=copyNs=overflow=0;
        for(auto& x:gapAll)x=0;for(auto& x:gapSkip)x=0;active=false;
    }
    bool empty()const{return levelTab.empty()&&groupTab.empty()&&seen.empty()&&rbSeen.empty()&&!active;}
};
