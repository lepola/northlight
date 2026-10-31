#pragma once
#include <d3d9.h>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

// Sampled render-thread instrumentation. GPU spans surround actual work, never
// replay it. Results are asynchronous command intervals, including GPU stalls.
class NorthlightMaterialProfile {
public:
    enum Stage {Admission,Constants,Snapshot,BoundsPose,HeightRead,UV,Subdivide,GeometryUpload,
        NormalCache,NormalBuild,Mips,CaptureSetup,CaptureDraw,CaptureRestore,PomSetup,NativeDraw,FinalApply,SharedPOM,VisibilityAudit,DeferredRecord,DeferredFlush,PoseEnvelope,PoseFallback,StageCount};
    static const char* name(Stage s){static const char* names[]={"admission","constants","snapshot","boundsPose","heightRead","uv","subdivide","geometryUpload","normalCache","normalBuild","mips","captureSetup","captureDraw","captureRestore","pomSetup","nativeDraw","finalApply","sharedPOM","visibilityAudit","deferredRecord","deferredFlush","poseEnvelope","poseFallback"};return names[s];}
    using Clock=uint64_t(*)();
    enum Counter {Candidates,RangeRejected,NoContribution,NormalHit,NormalMiss,BoundsVertices,PoseVertices,PoseBudget,
        GeometryHit,GeometryBuilt,OcclusionDraw,OcclusionFarSkipped,CounterCount};
    struct Scope;
private:
    static constexpr unsigned Tags=20,Ring=3,MaxSpans=4096;
    struct CPU {uint64_t exclusive=0,inclusive=0,items=0,bytes=0;unsigned calls=0;};
    struct Pair {IDirect3DQuery9* first=nullptr,*last=nullptr;Stage stage=Admission;unsigned tag=0;uint64_t items=0;};
    struct Slot {std::vector<Pair> pairs;IDirect3DQuery9* frequency=nullptr,*disjoint=nullptr;uint64_t frame=0;unsigned used=0,attempted=0;bool pending=false;};
    IDirect3DDevice9* d;Clock clock;
    std::array<CPU,StageCount*Tags> cpu{};std::array<uint64_t,CounterCount> counts{};Slot slots[Ring];Scope* parent=nullptr;
    uint64_t frame=0,epoch=0,reads=0,queryNs=0;unsigned queryCreates=0;int gpu=-1;
    bool active=false,gpuDisabled=false,gpuOpen=false,gpuSample=false;
    static uint64_t now(){return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    uint64_t tick(){++reads;return clock();}
    static void dropQuery(IDirect3DQuery9*& q){if(q)q->Release();q=nullptr;}
    void releaseGPU(){gpu=-1;gpuOpen=false;for(auto& s:slots){for(auto& p:s.pairs){dropQuery(p.first);dropQuery(p.last);}s.pairs.clear();dropQuery(s.frequency);dropQuery(s.disjoint);s.pending=false;s.used=s.attempted=0;}}
    void fail(const char* operation,HRESULT hr){if(!gpuDisabled)logf("MATERIAL_GPU disabled operation=%s hr=%08lx CPU_profiling_retained=1",operation,(unsigned long)uint32_t(hr));gpuDisabled=true;releaseGPU();}
    bool create(D3DQUERYTYPE type,IDirect3DQuery9*& q){if(q)return true;HRESULT hr=d->CreateQuery(type,&q);++queryCreates;if(FAILED(hr)||!q){fail("create",FAILED(hr)?hr:E_FAIL);return false;}return true;}
    bool issue(IDirect3DQuery9* q,DWORD flags){HRESULT hr=q->Issue(flags);if(FAILED(hr)){fail("issue",hr);return false;}return true;}
    int beginGPU(Stage stage,unsigned tag){
        if(gpu<0||gpuDisabled)return -1;auto& s=slots[gpu];++s.attempted;
        if(gpuOpen||s.used==MaxSpans)return -1;
        try{if(s.pairs.size()==s.used)s.pairs.emplace_back();}catch(...){fail("allocation",E_FAIL);return -1;}
        const unsigned i=s.used;auto& p=s.pairs[i];
        if(!create(D3DQUERYTYPE_TIMESTAMP,p.first)||!create(D3DQUERYTYPE_TIMESTAMP,p.last))return -1;
        p.stage=stage;p.tag=tag;p.items=0;if(!issue(p.first,D3DISSUE_END))return -1;++s.used;gpuOpen=true;return int(i);
    }
    void endGPU(int i,uint64_t items){if(i<0||gpu<0||gpuDisabled)return;auto& p=slots[gpu].pairs[unsigned(i)];p.items=items;gpuOpen=false;issue(p.last,D3DISSUE_END);}
public:
    struct Scope {
        NorthlightMaterialProfile* owner=nullptr;Scope* previous=nullptr;Stage stage;unsigned tag=0;
        uint64_t start=0,child=0,items=0,bytes=0,epoch=0;int span=-1;
        Scope(NorthlightMaterialProfile* profile,Stage s,unsigned context=0,bool onGPU=false):stage(s){
            if(!profile||!profile->active)return;owner=profile;previous=owner->parent;tag=context?context:previous?previous->tag:0;
            if(tag>=Tags)tag=0;epoch=owner->epoch;start=owner->tick();owner->parent=this;
            if(onGPU){uint64_t began=owner->tick();span=owner->beginGPU(s,tag);owner->queryNs+=owner->tick()-began;}
        }
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
        void work(uint64_t count,uint64_t size=0){items+=count;bytes+=size;}
        void segment(uint64_t end){uint64_t elapsed=end-start;auto& c=owner->cpu[unsigned(stage)*Tags+tag];
            ++c.calls;c.inclusive+=elapsed;c.exclusive+=elapsed>=child?elapsed-child:0;c.items+=items;c.bytes+=bytes;if(previous)previous->child+=elapsed;}
        void next(Stage s){if(owner&&owner->active&&owner->epoch==epoch){segment(owner->tick());child=items=bytes=0;start=owner->tick();}stage=s;}
        ~Scope(){
            if(!owner||!owner->active||owner->epoch!=epoch)return;
            if(span>=0){uint64_t began=owner->tick();owner->endGPU(span,items);owner->queryNs+=owner->tick()-began;}
            segment(owner->tick());owner->parent=previous;
        }
    };
    explicit NorthlightMaterialProfile(IDirect3DDevice9* device,Clock source=now):d(device),clock(source),gpuDisabled(!device){}
    ~NorthlightMaterialProfile(){releaseGPU();}
    NorthlightMaterialProfile(const NorthlightMaterialProfile&)=delete;NorthlightMaterialProfile& operator=(const NorthlightMaterialProfile&)=delete;
    bool recording()const{return active;}
    uint64_t frameNumber()const{return frame;}
    void count(Counter c,uint64_t amount=1){if(active)counts[c]+=amount;}
    void beginFrame(uint64_t number,bool sample){
        endFrame();if(!sample)return;active=true;frame=number;++epoch;cpu={};counts={};parent=nullptr;reads=queryNs=queryCreates=0;gpuSample=false;
        if(gpuDisabled)return;uint64_t began=tick();
        for(unsigned i=0;i<Ring;++i){auto& s=slots[i];if(s.pending)continue;
            if(!create(D3DQUERYTYPE_TIMESTAMPFREQ,s.frequency)||!create(D3DQUERYTYPE_TIMESTAMPDISJOINT,s.disjoint))break;
            s.frame=number;s.used=s.attempted=0;if(issue(s.disjoint,D3DISSUE_BEGIN)){gpu=int(i);gpuSample=true;}break;}
        queryNs+=tick()-began;
    }
    void endFrame(){
        if(!active)return;
        if(gpu>=0){auto& s=slots[gpu];if(issue(s.frequency,D3DISSUE_END)&&issue(s.disjoint,D3DISSUE_END))s.pending=true;gpu=-1;gpuOpen=false;}
        uint64_t sum=0;for(unsigned i=0;i<cpu.size();++i){const auto& c=cpu[i];if(!c.calls)continue;sum+=c.exclusive;
            logf("MATERIAL_CPU frame=%llu stage=%s domain=%u mode=%u calls=%u exclusiveMs=%.6f inclusiveMs=%.6f items=%llu bytes=%llu",(unsigned long long)frame,name(Stage(i/Tags)),(i%Tags)/4,i%4,c.calls,c.exclusive*1e-6,c.inclusive*1e-6,(unsigned long long)c.items,(unsigned long long)c.bytes);}
        for(unsigned stage=0;stage<StageCount;++stage){CPU total{};for(unsigned tag=0;tag<Tags;++tag){const auto& c=cpu[stage*Tags+tag];total.calls+=c.calls;total.exclusive+=c.exclusive;total.items+=c.items;total.bytes+=c.bytes;}
            logf("MATERIAL_STAGE frame=%llu stage=%s calls=%u cpuMs=%.6f items=%llu bytes=%llu",(unsigned long long)frame,name(Stage(stage)),total.calls,total.exclusive*1e-6,(unsigned long long)total.items,(unsigned long long)total.bytes);}
        logf("MATERIAL_PROFILE frame=%llu cpuExclusiveMs=%.6f clockReads=%llu queryCpuMs=%.6f newQueries=%u gpuRecording=%u",(unsigned long long)frame,sum*1e-6,(unsigned long long)reads,queryNs*1e-6,queryCreates,unsigned(gpuSample&&!gpuDisabled));
        static const char* counterNames[]={"candidates","rangeRejected","noContribution","normalHit","normalMiss","boundsVertices","poseVertices","poseBudget","geometryHit","geometryBuilt","occlusionDraw","occlusionFarSkipped"};
        for(unsigned i=0;i<counts.size();++i)if(counts[i])logf("MATERIAL_WORK frame=%llu counter=%s count=%llu",(unsigned long long)frame,counterNames[i],(unsigned long long)counts[i]);active=false;parent=nullptr;
    }
    void poll(){
        if(gpuDisabled)return;
        for(auto& s:slots){if(!s.pending)continue;BOOL disjoint=FALSE;HRESULT hr=s.disjoint->GetData(&disjoint,sizeof disjoint,0);if(hr==S_FALSE)continue;if(FAILED(hr)){fail("disjoint",hr);return;}
            if(disjoint){logf("MATERIAL_GPU frame=%llu discarded=disjoint",(unsigned long long)s.frame);s.pending=false;continue;}
            uint64_t hz=0;hr=s.frequency->GetData(&hz,sizeof hz,0);if(hr==S_FALSE)continue;if(FAILED(hr)){fail("frequency",hr);return;}
            if(!hz){logf("MATERIAL_GPU frame=%llu discarded=zeroFrequency",(unsigned long long)s.frame);s.pending=false;continue;}
            struct Result{double ms=0;uint64_t items=0;unsigned calls=0;};std::array<Result,StageCount*Tags> results{};bool ready=true,valid=true;
            for(unsigned i=0;i<s.used;++i){auto& p=s.pairs[i];uint64_t a=0,b=0;
                hr=p.first->GetData(&a,sizeof a,0);if(hr==S_FALSE){ready=false;break;}if(FAILED(hr)){fail("start",hr);return;}
                hr=p.last->GetData(&b,sizeof b,0);if(hr==S_FALSE){ready=false;break;}if(FAILED(hr)){fail("end",hr);return;}
                if(b<a){valid=false;break;}auto& r=results[unsigned(p.stage)*Tags+p.tag];r.ms+=double(b-a)*1000/double(hz);r.items+=p.items;++r.calls;}
            if(!ready)continue;s.pending=false;
            if(!valid){logf("MATERIAL_GPU frame=%llu discarded=nonmonotonic",(unsigned long long)s.frame);continue;}
            double total=0;for(unsigned i=0;i<results.size();++i){auto& r=results[i];if(!r.calls)continue;total+=r.ms;
                logf("MATERIAL_GPU frame=%llu stage=%s domain=%u mode=%u spans=%u ms=%.6f items=%llu",(unsigned long long)s.frame,name(Stage(i/Tags)),(i%Tags)/4,i%4,r.calls,r.ms,(unsigned long long)r.items);}
            logf("MATERIAL_GPU_COVERAGE frame=%llu attempted=%u measured=%u complete=%u sumIntervalsMs=%.6f",(unsigned long long)s.frame,s.attempted,s.used,unsigned(s.used==s.attempted),total);
        }
    }
    void reset(){active=false;parent=nullptr;++epoch;releaseGPU();gpuDisabled=!d;}
};
