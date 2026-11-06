#pragma once
#include <cmath>
#include <cstdint>

// Per static-shadow slot. The renderer owns both maps and their matching matrices;
// only a successful changed-direction render calls commit() to rotate those owners.
namespace NorthlightShadowTransition {
struct Direction {
    float x=0,y=0,z=0;
    bool operator==(const Direction& b)const noexcept{return x==b.x&&y==b.y&&z==b.z;}
    bool operator!=(const Direction& b)const noexcept{return !(*this==b);}
};
class Slot {
public:
    static constexpr uint32_t DurationMs=1000;
    void reset()noexcept{*this=Slot{};}
    // Content/travel/source/map invalidation: discard history and queued requests.
    // The committed identity stays paired with its map until a successful render.
    void cancel()noexcept{
        previous_={};desired_={};requested_=fading_=false;fadeAllowed_=false;
    }
    // Once per slot/frame, before matrices and draw plans are prepared. During a
    // fade only the latest request is retained; no third map is rendered or held.
    void request(Direction desired,uint32_t nowMs,bool cancelHistory=false)noexcept{
        advance(nowMs);
        if(cancelHistory)cancel();
        if(!finite(desired)){cancel();return;}
        desired_=desired;requested_=true;
    }
    bool valid()const noexcept{return valid_;}
    bool fading()const noexcept{return fading_;}
    bool canChangeDirection()const noexcept{return !fading_;}
    bool canPreserveHistory()const noexcept{return valid_&&fadeAllowed_&&!fading_;}
    bool needsRender()const noexcept{return requested_&&(!valid_||desired_!=current_);}
    Direction current()const noexcept{return current_;}
    Direction previous()const noexcept{return previous_;} // sampled only while fading()
    // Dynamic redraws use the committed basis throughout a fade. Reuse/Defer
    // keep their stored matrix and do not commit the pending candidate below.
    Direction renderDirection()const noexcept{return fading_||!requested_?current_:desired_;}
    float weight()const noexcept{return fading_?float(uint32_t(now_-start_))/float(DurationMs):1.f;}
    // Call only after successful rendering. historyReady proves the old texture
    // and matrix were preserved; optional resource failure instead switches instantly.
    // Same-direction dynamic redraws never restart a fade or consume a queued step.
    bool commit(Direction rendered,uint32_t nowMs,bool historyReady)noexcept{
        if(!finite(rendered))return false;
        const bool same=valid_&&rendered==current_;
        const bool selected=!fading_&&requested_&&rendered==desired_;
        advance(nowMs);
        if(same){fadeAllowed_=true;return true;}
        if(!selected)return false;
        fading_=valid_&&fadeAllowed_&&historyReady;
        previous_=fading_?current_:Direction{};
        current_=rendered;valid_=true;requested_=false;fadeAllowed_=true;start_=nowMs;
        return true;
    }
private:
    static bool finite(Direction v)noexcept{
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&(v.x!=0||v.y!=0||v.z!=0);
    }
    void advance(uint32_t nowMs)noexcept{
        // Unsigned subtraction accepts the ordinary DWORD wrap. A backwards
        // clock or a gap longer than the whole fade cannot resume stale history.
        if(clockValid_&&uint32_t(nowMs-now_)>DurationMs)cancel();
        now_=nowMs;clockValid_=true;
        if(fading_&&uint32_t(now_-start_)>=DurationMs)fading_=false;
    }
    Direction current_,previous_,desired_;
    uint32_t now_=0,start_=0;
    bool valid_=false,requested_=false,fading_=false,clockValid_=false,fadeAllowed_=true;
};
}
