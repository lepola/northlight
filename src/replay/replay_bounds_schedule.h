#pragma once
#include <cstddef>
namespace NorthlightReplaySchedule {
// A pending packet may be cold or merely too expensive for the remaining
// budget. Keep visiting affordable packets until the phase's actual budget is
// exhausted. Rotate past a first packet that consumes an entire phase so it
// cannot pin every following draw indefinitely.
template<class CanWork,class Visit>
size_t pass(size_t count,size_t start,CanWork canWork,Visit visit){
    if(!count)return 0;
    start%=count;
    size_t index=start;
    for(size_t offset=0;offset<count;++offset){
        const size_t next=index+1==count?0:index+1;
        if(!canWork())return index==start?next:index;
        const bool pending=visit(index);
        if(!canWork())return pending&&index!=start?index:next;
        index=next;
    }
    return start+1==count?0:start+1;
}
}
