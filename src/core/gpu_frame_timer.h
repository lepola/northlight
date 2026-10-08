#pragma once
#include <d3d9.h>
#include <cstdint>

// 0.3.200 (gpu budget): Northlight's own GPU time on every frame (GpuBudgetMs > 0), for gpu_budget.h. Two timestamps around the effects in a
// small ring; read back several frames later. Include after the renderer's logf(const char*, ...). Render thread only. Like
// NorthlightGpuProfile it never flushes or waits: every GetData uses flags=0 and is tried once per poll; a full ring skips the frame's
// measurement, a disjoint, zero-frequency or nonmonotonic result is dropped. Any device failure disables the timer (the level then stays where
// it is; the image is never affected). The device is borrowed: reset() before the device is Reset, destroy before it is released.
class NorthlightGpuFrameTimer {
    static constexpr unsigned RingSize=4;
    struct Slot {IDirect3DQuery9 *start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;uint64_t serial=0;bool pending=false;};
    IDirect3DDevice9* device_=nullptr;
    Slot slots_[RingSize];
    int active_=-1;
    uint64_t serial_=0;
    bool disabled_=false;
    static void release(IDirect3DQuery9*& q){if(q){q->Release();q=nullptr;}}
    void releaseAll(){active_=-1;for(auto& s:slots_){release(s.start);release(s.end);release(s.frequency);release(s.disjoint);s.pending=false;}}
    void disable(const char* operation,HRESULT hr){
        if(!disabled_)logf("GPUBUDGET timer disabled: %s HRESULT=0x%08lx (rendering unaffected)",operation,static_cast<unsigned long>(static_cast<uint32_t>(hr)));
        disabled_=true;releaseAll();}
    bool create(D3DQUERYTYPE type,IDirect3DQuery9** out){
        HRESULT hr=device_->CreateQuery(type,out);
        if(FAILED(hr)||!*out){*out=nullptr;disable("query creation",FAILED(hr)?hr:E_FAIL);return false;}
        return true;}
    bool prepare(Slot& s){
        if(s.disjoint)return true;
        return create(D3DQUERYTYPE_TIMESTAMP,&s.start)&&create(D3DQUERYTYPE_TIMESTAMP,&s.end)&&create(D3DQUERYTYPE_TIMESTAMPFREQ,&s.frequency)&&create(D3DQUERYTYPE_TIMESTAMPDISJOINT,&s.disjoint);}
    bool issue(IDirect3DQuery9* q,DWORD flags,const char* operation){HRESULT hr=q->Issue(flags);if(FAILED(hr)){disable(operation,hr);return false;}return true;}
public:
    explicit NorthlightGpuFrameTimer(IDirect3DDevice9* device):device_(device),disabled_(!device){}
    NorthlightGpuFrameTimer(const NorthlightGpuFrameTimer&)=delete;
    NorthlightGpuFrameTimer& operator=(const NorthlightGpuFrameTimer&)=delete;
    ~NorthlightGpuFrameTimer(){releaseAll();}
    bool disabled()const{return disabled_;}
    // At the start of the effects. false: nothing was opened (disabled, or every slot still waits for its result).
    bool begin(){
        if(disabled_)return false;
        if(active_>=0)end();
        for(unsigned i=0;i<RingSize;++i){Slot& s=slots_[i];
            if(s.pending)continue;
            if(!prepare(s))return false;
            if(!issue(s.disjoint,D3DISSUE_BEGIN,"disjoint begin")||!issue(s.start,D3DISSUE_END,"start timestamp"))return false;
            s.serial=++serial_;active_=int(i);return true;}
        return false;
    }
    // At the end of the effects (every return path: the caller holds a scope guard). No-op without an open begin.
    void end(){
        if(active_<0||disabled_)return;
        Slot& s=slots_[active_];active_=-1;
        if(!issue(s.end,D3DISSUE_END,"end timestamp")||!issue(s.frequency,D3DISSUE_END,"frequency end")||!issue(s.disjoint,D3DISSUE_END,"disjoint end"))return;
        s.pending=true;
    }
    // Once per frame. Tries each pending slot once (flags 0, never D3DGETDATA_FLUSH, no loop until ready); true with the newest completed
    // valid interval in ms. A slot whose result is not there yet stays pending for a later frame.
    bool poll(double& ms){
        if(disabled_)return false;
        bool found=false;uint64_t newest=0;
        for(auto& s:slots_){
            if(!s.pending)continue;
            BOOL disjoint=FALSE;HRESULT hr=s.disjoint->GetData(&disjoint,sizeof disjoint,0);
            if(hr==S_FALSE)continue;
            if(FAILED(hr)){disable("disjoint read",hr);return false;}
            if(disjoint){s.pending=false;continue;}
            uint64_t frequency=0,t0=0,t1=0;
            hr=s.frequency->GetData(&frequency,sizeof frequency,0);
            if(hr==S_FALSE)continue;
            if(FAILED(hr)){disable("frequency read",hr);return false;}
            hr=s.start->GetData(&t0,sizeof t0,0);
            if(hr==S_FALSE)continue;
            if(FAILED(hr)){disable("timestamp read",hr);return false;}
            hr=s.end->GetData(&t1,sizeof t1,0);
            if(hr==S_FALSE)continue;
            if(FAILED(hr)){disable("timestamp read",hr);return false;}
            s.pending=false;
            if(!frequency||t1<t0)continue;
            if(!found||s.serial>newest){newest=s.serial;ms=double(t1-t0)*1000.0/double(frequency);found=true;}
        }
        return found;
    }
    // Before a device Reset: releases the queries; the next begin recreates them (a timer disabled by a failure may retry).
    void reset(){releaseAll();disabled_=!device_;}
};
