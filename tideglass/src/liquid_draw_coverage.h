#pragma once
#include <array>
#include <cmath>
#include <vector>

namespace water
{
// Current native liquid draws, copied from the bounds already read for material
// classification, with an explicit world-space guarantee from the caller. No GPU
// access or asset residency. Unverified coordinate space, missing/invalid bounds or
// overflow make the whole set incomplete: callers must retain their conservative
// fallback, never treat a partial list as proof that a height is invisible.
// This describes observations so far, not a closed liquid pass. The session
// invalidates a prepared reflection whenever a later observation changes them.
class LiquidDrawCoverage
{
public:
    using Bounds=std::array<float,6>;
    static constexpr unsigned MaxDraws=1024;
    void Clear(){m_bounds.clear();m_seen=false;m_complete=true;}
    bool Observe(const float* bounds,bool worldSpace)
    {
        if(!m_complete)return false;
        m_seen=true;
        if(!worldSpace || !bounds){m_complete=false;return true;}
        Bounds copy{};
        for(unsigned c=0;c<3;++c){
            if(!std::isfinite(bounds[c]) || !std::isfinite(bounds[c+3]) || bounds[c]>bounds[c+3]){
                m_complete=false;return true;
            }
            copy[c]=bounds[c];copy[c+3]=bounds[c+3];
        }
        // A host may classify one draw twice before submission.
        if(!m_bounds.empty() && m_bounds.back()==copy)return false;
        if(m_bounds.size()==MaxDraws){m_complete=false;return true;}
        m_bounds.push_back(copy);return true;
    }
    bool Complete()const{return m_seen&&m_complete;}
    const std::vector<Bounds>& Draws()const{return m_bounds;}
private:
    std::vector<Bounds> m_bounds;
    bool m_seen=false,m_complete=true;
};
}
