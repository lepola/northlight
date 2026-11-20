#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include "effects_buckets.h"

// Diagnostics only. Query spans overlap frame phases; they are not extra CPU time.
namespace NorthlightFrameStalls {
struct Queries {
    unsigned calls=0,flush=0,pending=0,errors=0,events=0,occlusion=0,completed=0;
    int64_t wallNs=0,maxCallNs=0,pendingNs=0;
};
struct QuerySpan {
    int64_t first=0;bool waiting=false;
    void reset(){first=0;waiting=false;}
    void record(Queries& out,unsigned type,unsigned flags,long result,int64_t start,int64_t end){
        if(end<start){reset();return;}
        ++out.calls;out.flush+=bool(flags&1);out.events+=type==8;out.occlusion+=type==9;
        out.wallNs+=end-start;out.maxCallNs=std::max(out.maxCallNs,end-start);
        if(result==1){++out.pending;if(!waiting){first=start;waiting=true;}}
        else {out.errors+=result<0;if(result==0&&waiting){++out.completed;out.pendingNs+=std::max(int64_t(0),end-first);}reset();}
    }
};
struct Frame {
    unsigned id=0,kind=0;
    bool applied=false,sampled=false,logged=false,focus=false,phases=false;
    double totalMs=0,sceneMs=-1,effectsMs=0,tailMs=-1,finishMs=0,presentMs=0;
    double sceneIdleMs=-1,frameIdleMs=-1;   // Queue waiting overlaps the phases; it is not extra time.
    Queries queries;
    NorthlightEffectsBuckets::Snapshot effects;
};
struct Report {unsigned frames=0,slow=0,count=0;std::array<Frame,4> worst{};};
class Tracker {
    int64_t previousDone=0;unsigned long long previousLogs=0;bool previousLogged=true,primed=false;
    std::uint64_t previousIdle=0;bool idlePrimed=false;
    Report window;
public:
    void reset(){previousDone=0;previousLogs=0;previousLogged=true;primed=false;previousIdle=0;idlePrimed=false;window={};}
    // Previous Present done -> current Present done, with non-overlapping phases.
    bool presented(Frame frame,int64_t entry,int64_t finished,int64_t done,int64_t frequency,
                   int64_t scene,unsigned long long logs,bool idleKnown=false,std::uint64_t sceneIdle=0,std::uint64_t doneIdle=0){
        if(frequency<=0||finished<entry||done<finished){reset();return false;}
        const bool logged=logs!=previousLogs;previousLogs=logs;
        frame.logged=logged||previousLogged;previousLogged=logged;
        const int64_t old=previousDone;const bool had=primed;previousDone=done;primed=true;
        const auto oldIdle=previousIdle;const bool hadIdle=idlePrimed;previousIdle=doneIdle;idlePrimed=idleKnown;
        if(!had)return false;
        if(entry<old){window={};return false;}
        const double ms=1000.0/double(frequency);
        frame.totalMs=double(done-old)*ms;frame.finishMs=double(finished-entry)*ms;frame.presentMs=double(done-finished)*ms;
        if(scene>=old&&scene<=entry&&frame.effectsMs>=0&&frame.effectsMs<=double(entry-scene)*ms+.001){
            frame.phases=true;frame.sceneMs=double(scene-old)*ms;
            frame.tailMs=std::max(0.0,double(entry-scene)*ms-frame.effectsMs);
        }
        if(idleKnown&&hadIdle&&doneIdle>=oldIdle){
            const double idleMs=double(doneIdle-oldIdle)*1e-6;
            if(idleMs<=frame.totalMs+.05)frame.frameIdleMs=std::min(frame.totalMs,idleMs);
            if(frame.phases&&sceneIdle>=oldIdle&&sceneIdle<=doneIdle){
                const double sceneMs=double(sceneIdle-oldIdle)*1e-6;
                if(sceneMs<=frame.sceneMs+.05)frame.sceneIdleMs=std::min(frame.sceneMs,sceneMs);
            }
        }
        if(frame.totalMs>=20){
            ++window.slow;
            if(window.count<window.worst.size())window.worst[window.count++]=frame;
            else {
                auto least=std::min_element(window.worst.begin(),window.worst.end(),[](const Frame& a,const Frame& b){return a.totalMs<b.totalMs;});
                if(frame.totalMs>least->totalMs)*least=frame;
            }
        }
        return ++window.frames>=120;
    }
    Report take(){auto report=window;window={};return report;}
};
}
