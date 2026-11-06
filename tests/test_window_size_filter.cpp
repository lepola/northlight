#include "window_size_filter.h"
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include <thread>
using namespace NorthlightWindowSize;
static Geometry geometry(unsigned w=2560,unsigned h=1440){return {w,h,0x10,0,100,100,int(w)+116,int(h)+139,true};}
static Context context(const Publication& p,Geometry g=geometry()){
    const auto s=p.snapshot();return {s.session,s.barrier,g,s.owner==1&&s.window==2,s.ready&&s.width==g.width&&s.height==g.height};
}
int main(){
    Publication publication;DeliveredSize policy;
    assert(!publication.snapshot().ready);assert(publication.acquire(1,2));assert(!publication.acquire(3,2));
    publication.presented(3,2,2560,1440,true,true);assert(!publication.snapshot().ready);
    publication.presented(1,2,2560,1440,true,true);
    auto c=context(publication);auto first=policy.begin(0,c);assert(!first.suppress);
    // The actual game handler synchronously Reset()s from its WM_SIZE handler.
    publication.invalidate(1);assert(policy.delivered(first.token,0,c.session,context(publication)));
    publication.presented(1,2,2560,1440,true,true);
    for(unsigned i=0;i<1000;++i)assert(policy.begin(0,context(publication)).suppress);
    // Failed/occluded Present, fullscreen and destination overrides cannot arm.
    publication.presented(1,99,2560,1440,true,false);assert(!publication.snapshot().ready);
    auto d=policy.begin(0,context(publication));assert(!d.suppress);assert(policy.delivered(d.token,0,c.session,context(publication)));
    publication.presented(1,2,2560,1440,false,true);assert(!publication.snapshot().ready);
    publication.presented(1,2,2560,1440,true,true);assert(policy.begin(0,context(publication)).suppress);
    for(unsigned state:{1u,2u,3u,4u,5u,~0u}){
        auto transition=policy.begin(state,context(publication));assert(!transition.suppress);
        assert(!policy.delivered(transition.token,state,c.session,context(publication)));
        auto restored=policy.begin(0,context(publication));assert(!restored.suppress);
        publication.invalidate(1);assert(policy.delivered(restored.token,0,c.session,context(publication)));
        publication.presented(1,2,2560,1440,true,true);assert(policy.begin(0,context(publication)).suppress);
    }
    for(unsigned change=0;change<7;++change){
        auto g=geometry();switch(change){case 0:++g.width;break;case 1:++g.height;break;case 2:++g.style;break;
            case 3:++g.extendedStyle;break;case 4:++g.left;break;case 5:g.usable=false;break;case 6:g.width=0;g.usable=false;break;}
        const auto altered=context(publication,g);auto realChange=policy.begin(0,altered);assert(!realChange.suppress);
        policy.delivered(realChange.token,0,altered.session,altered);
        auto restore=policy.begin(0,context(publication));assert(!restore.suppress);
        assert(policy.delivered(restore.token,0,c.session,context(publication)));
    }
    // Focus, DPI, display, style and move barriers require a new delivered size
    // and a successful Present, even if the pixel dimensions are unchanged.
    policy.invalidate();publication.windowChanged();auto barrier=context(publication);
    auto delivered=policy.begin(0,barrier);assert(!delivered.suppress);assert(policy.delivered(delivered.token,0,barrier.session,barrier));
    assert(!policy.begin(0,context(publication)).suppress);
    auto fresh=policy.begin(0,context(publication));assert(policy.delivered(fresh.token,0,barrier.session,context(publication)));
    publication.presented(1,2,2560,1440,true,true);assert(policy.begin(0,context(publication)).suppress);
    // A nested size wins; the stale outer callback cannot overwrite its proof.
    auto outer=policy.begin(0,context(publication,geometry(1280,720)));
    auto innerContext=context(publication,geometry(1920,1080));auto inner=policy.begin(0,innerContext);
    publication.invalidate(1);assert(policy.delivered(inner.token,0,innerContext.session,context(publication,geometry(1920,1080))));
    assert(!policy.delivered(outer.token,0,innerContext.session,context(publication,geometry(1280,720))));
    publication.presented(1,2,1920,1080,true,true);assert(policy.begin(0,context(publication,geometry(1920,1080))).suppress);
    auto stale=policy.begin(0,context(publication,geometry(800,600)));const auto oldSession=context(publication).session;
    publication.release(1);assert(publication.acquire(3,4));
    auto replacement=context(publication);replacement.attached=true;
    assert(!policy.delivered(stale.token,0,oldSession,replacement));
    publication.presented(3,4,2560,1440,true,true);publication.release(1);publication.invalidate(1);
    publication.presented(1,2,2560,1440,true,false);assert(publication.snapshot().owner==3&&publication.snapshot().ready);
    // The closing reservation spans shared hook cleanup. Neither a second
    // teardown nor another device can enter before the claimant finishes.
    assert(publication.beginRelease(3));assert(publication.closing()&&!publication.owner());
    std::thread contender([&]{assert(!publication.beginRelease(3));publication.release(3);assert(!publication.acquire(5,6));});
    contender.join();assert(publication.closing());publication.finishRelease();assert(publication.acquire(3,4));
    std::uintptr_t retained=0;assert(!publication.beginMaintenance(99,retained));
    assert(publication.beginMaintenance(3,retained)&&retained==3);
    assert(!publication.acquire(5,6)&&!publication.beginWindowDestruction());
    assert(!publication.finishMaintenance(retained));assert(publication.closing()&&!publication.owner());
    publication.finishRelease();assert(!publication.owner()&&!publication.snapshot().window&&publication.acquire(3,4));
    // A publisher paused before ownership validation cannot reuse a new
    // attachment's epoch, including reuse of the same opaque owner and HWND.
    const auto delayed=publication.stamp(3,4);publication.release(3);assert(publication.acquire(3,4));
    publication.presented(3,4,2560,1440,true,true);const auto current=publication.snapshot();
    publication.complete(delayed,800,600,true,true);assert(publication.snapshot().ready&&publication.snapshot().width==current.width);
    publication.complete(delayed,800,600,true,false);assert(publication.snapshot().ready);
    // Readers never accept torn dimensions during renderer publication.
    std::atomic<bool> stop{false};std::thread writer([&]{for(unsigned i=0;i<50000;++i)publication.presented(3,4,i&1?1280:2560,i&1?720:1440,true,true);stop.store(true);});
    while(!stop.load()){const auto s=publication.snapshot();if(s.ready)assert((s.width==1280&&s.height==720)||(s.width==2560&&s.height==1440));}
    writer.join();std::puts("WINDOW_SIZE_POLICY_PASS first_delivery synchronous_Reset nested_size transitions loss fullscreen foreign_owner lifecycle coherent_publication");
}
