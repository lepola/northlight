#pragma once
#include <d3d9.h>
#include <cstdint>
#include <cstdio>
#include <cstring>

// Include after the renderer's logf(const char*, ...). The device is borrowed:
// its owner must destroy/reset this profiler before releasing/resetting it.
// All methods belong on the render thread. This never calls a renderer failure
// handler, flushes the GPU, or waits for a query. GPU timestamps measure elapsed
// command-stream intervals, including stalls; they are not CPU execution times.
class NorthlightGpuProfile {
    // 0.3.149: 20 marks (was 16): room for DiagReplayProbe's SunNearLoop/ReplayProbe with both sources active.
    // 0.3.201 (task 18): 24 marks: the AOBlur pass adds one, and a folded frame the world did not composite adds AOComposite.
    static constexpr unsigned RingSize=6,MaxMarks=24,StampCount=MaxMarks+2;
    static constexpr uint64_t SamplePeriod=120;
    struct Slot {
        IDirect3DQuery9* stamps[StampCount]={};
        IDirect3DQuery9* frequency=nullptr;
        IDirect3DQuery9* disjoint=nullptr;
        char labels[MaxMarks+1][40]={};
        uint64_t frame=0;
        unsigned marks=0;
        bool pending=false,overflow=false;
    };
    IDirect3DDevice9* device_=nullptr;
    Slot slots_[RingSize];
    int active_=-1;
    unsigned next_=0;
    bool disabled_=false,hasSampleFrame_=false;
    uint64_t lastSampleFrame_=0;

    static void release(IDirect3DQuery9*& q){if(q){q->Release();q=nullptr;}}
    void releaseAll(){
        active_=-1;
        for(auto& s:slots_){
            for(auto& q:s.stamps)release(q);
            release(s.frequency);release(s.disjoint);
            s.pending=false;s.marks=0;s.overflow=false;
        }
    }
    void disable(const char* operation,HRESULT hr){
        if(!disabled_)logf("GPU profile disabled: %s HRESULT=0x%08lx (rendering unaffected)",operation,static_cast<unsigned long>(static_cast<uint32_t>(hr)));
        disabled_=true;releaseAll();
    }
    bool create(D3DQUERYTYPE type,IDirect3DQuery9** output){
        HRESULT hr=device_->CreateQuery(type,output);
        if(FAILED(hr)||!*output){disable("query creation",FAILED(hr)?hr:E_FAIL);return false;}
        return true;
    }
    bool prepare(Slot& s){
        if(s.disjoint)return true;
        // Allocate only when a free slot is first used, never allocate in poll.
        // Any partial failure releases every query and disables diagnostics only.
        for(auto& q:s.stamps)if(!create(D3DQUERYTYPE_TIMESTAMP,&q))return false;
        return create(D3DQUERYTYPE_TIMESTAMPFREQ,&s.frequency)&&create(D3DQUERYTYPE_TIMESTAMPDISJOINT,&s.disjoint);
    }
    bool issue(IDirect3DQuery9* query,DWORD flags,const char* operation){
        HRESULT hr=query->Issue(flags);if(FAILED(hr)){disable(operation,hr);return false;}return true;
    }
    static void label(char (&out)[40],const char* input){
        // Copy labels immediately: caller strings may not survive deferred poll.
        if(!input||!*input)input="stage";
        unsigned i=0;
        for(;i+1<sizeof out&&input[i];++i){
            unsigned char c=static_cast<unsigned char>(input[i]);
            out[i]=(c>=33&&c<=126&&c!='=')?char(c):'_';
        }
        out[i]=0;
    }
    void report(Slot& s,const uint64_t* ticks,uint64_t frequency){
        // Reject nonmonotonic/wrapped timestamps. Never turn them into enormous
        // unsigned durations that would incorrectly implicate a rendering pass.
        const unsigned intervals=s.marks+1;
        for(unsigned i=0;i<intervals;++i)if(ticks[i+1]<ticks[i]){
            logf("GPU profile frame=%llu discarded: nonmonotonic timestamps",static_cast<unsigned long long>(s.frame));return;
        }
        char line[1536];
        int wrote=std::snprintf(line,sizeof line,"GPU profile frame=%llu total=%.3fms",
            static_cast<unsigned long long>(s.frame),double(ticks[intervals]-ticks[0])*1000.0/double(frequency));
        size_t used=wrote>0?static_cast<size_t>(wrote):0;
        for(unsigned i=0;i<intervals&&used+1<sizeof line;++i){
            wrote=std::snprintf(line+used,sizeof line-used," %s=%.3fms",s.labels[i],double(ticks[i+1]-ticks[i])*1000.0/double(frequency));
            if(wrote<0)break;
            if(static_cast<size_t>(wrote)>=sizeof line-used){used=sizeof line-1;break;}
            used+=static_cast<size_t>(wrote);
        }
        if(s.overflow&&used+1<sizeof line)std::snprintf(line+used,sizeof line-used," marks_truncated=1");
        logf("%s",line);
    }
public:
    explicit NorthlightGpuProfile(IDirect3DDevice9* device):device_(device),disabled_(!device){}
    NorthlightGpuProfile(const NorthlightGpuProfile&)=delete;
    NorthlightGpuProfile& operator=(const NorthlightGpuProfile&)=delete;
    ~NorthlightGpuProfile(){releaseAll();}

    // Returns true only when a sampled interval was actually opened. Unsampled
    // mark/end calls are cheap no-ops. A full ring drops the sample, never waits.
    bool beginFrame(uint64_t frameNumber){return beginFrame(frameNumber,frameNumber%SamplePeriod==0);}
    // 0.3.149: the caller chooses the sample frame (the renderer's rotating CPU sample).
    bool beginFrame(uint64_t frameNumber,bool sample){
        if(disabled_)return false;
        // Balance a forgotten end before a subsequent begin; normal integration
        // should still end at the last desired boundary or before Present.
        if(active_>=0)endFrame();
        if(disabled_||!sample||(hasSampleFrame_&&frameNumber==lastSampleFrame_))return false;
        hasSampleFrame_=true;lastSampleFrame_=frameNumber;
        for(unsigned offset=0;offset<RingSize;++offset){
            unsigned index=(next_+offset)%RingSize;Slot& s=slots_[index];
            if(s.pending)continue;
            if(!prepare(s))return false;
            s.frame=frameNumber;s.marks=0;s.overflow=false;
            active_=int(index);next_=(index+1)%RingSize;
            if(!issue(s.disjoint,D3DISSUE_BEGIN,"disjoint begin"))return false;
            return issue(s.stamps[0],D3DISSUE_END,"start timestamp");
        }
        return false;
    }
    uint64_t sampledFrame()const {return active_>=0&&!disabled_?slots_[active_].frame:0;}
    // label names the interval ending here (e.g. mark("AO") after the AO draw).
    void mark(const char* name){
        if(active_<0||disabled_)return;
        Slot& s=slots_[active_];
        if(s.marks>=MaxMarks){s.overflow=true;return;}
        label(s.labels[s.marks],name);
        if(issue(s.stamps[s.marks+1],D3DISSUE_END,"stage timestamp"))++s.marks;
    }
    void endFrame(){
        if(active_<0||disabled_)return;
        Slot& s=slots_[active_];label(s.labels[s.marks],"tail");
        if(!issue(s.stamps[s.marks+1],D3DISSUE_END,"end timestamp")||
           !issue(s.frequency,D3DISSUE_END,"frequency end")||
           !issue(s.disjoint,D3DISSUE_END,"disjoint end"))return;
        s.pending=true;active_=-1;
    }
    // Call once per frame (including effect-disabled frames). S_FALSE simply
    // leaves a slot pending until a later frame. Every GetData uses flags=0;
    // no D3DGETDATA_FLUSH, spin loop, sleep, Present or event wait is introduced.
    void poll(){
        if(disabled_)return;
        for(auto& s:slots_){
            if(!s.pending)continue;
            BOOL disjoint=FALSE;
            HRESULT hr=s.disjoint->GetData(&disjoint,sizeof disjoint,0);
            if(hr==S_FALSE)continue;
            if(FAILED(hr)){disable("disjoint read",hr);return;}
            if(disjoint){logf("GPU profile frame=%llu discarded: disjoint timestamp interval",static_cast<unsigned long long>(s.frame));s.pending=false;continue;}
            uint64_t frequency=0;
            hr=s.frequency->GetData(&frequency,sizeof frequency,0);
            if(hr==S_FALSE)continue;
            if(FAILED(hr)){disable("frequency read",hr);return;}
            if(!frequency){logf("GPU profile frame=%llu discarded: zero timestamp frequency",static_cast<unsigned long long>(s.frame));s.pending=false;continue;}
            uint64_t ticks[StampCount]={};bool complete=true;
            for(unsigned i=0;i<s.marks+2;++i){
                hr=s.stamps[i]->GetData(&ticks[i],sizeof ticks[i],0);
                if(hr==S_FALSE){complete=false;break;}
                if(FAILED(hr)){disable("timestamp read",hr);return;}
            }
            if(!complete)continue;
            report(s,ticks,frequency);s.pending=false;
        }
    }
    // Call before device Reset to release queries; next sample lazily recreates
    // them. A previously unsupported profiler may retry after a device reset.
    void reset(){releaseAll();disabled_=!device_;next_=0;hasSampleFrame_=false;lastSampleFrame_=0;}
};
