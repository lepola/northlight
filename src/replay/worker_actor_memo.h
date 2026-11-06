#pragma once
#include <memory>

// The worker's current actor BVH/hash are the result. Remember only packet
// identity: do not retain a second decoded scene or keep expired packets alive.
template<class Job> class NorthlightWorkerActorMemo {
    std::weak_ptr<const Job> previous;
    const Job* address=nullptr;
    bool initialized=false,wasNull=false;
public:
    bool matches(const std::shared_ptr<const Job>& job)const noexcept {
        if(!initialized)return false;
        if(!job)return wasNull;
        if(wasNull)return false;
        return address==job.get()&&!previous.expired();
    }
    void remember(const std::shared_ptr<const Job>& job)noexcept {
        previous=job;address=job.get();initialized=true;wasNull=!job;
    }
};
