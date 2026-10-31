#include "shadow_transition.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <type_traits>
using namespace NorthlightShadowTransition;
constexpr Direction A{1,0,0},B{0,1,0},C{0,0,1},D{-1,0,0};
static_assert(std::is_trivially_copyable<Slot>::value,"fixed value state, no heap ownership");
static_assert(sizeof(Slot)<=64,"four slots require at most 256 bytes");
static void initial(Slot& s,Direction d,uint32_t now){
    assert(!s.valid()&&!s.fading()&&s.canChangeDirection()&&!s.needsRender());
    assert(!s.canPreserveHistory());
    s.request(d,now);assert(s.needsRender()&&s.renderDirection()==d);
    assert(s.commit(d,now,true));assert(s.valid()&&!s.fading()&&s.current()==d&&s.weight()==1);
    assert(s.canPreserveHistory());
}
static void coalescing(){
    Slot s;initial(s,A,0);
    s.request(B,100);assert(s.commit(B,100,true));
    assert(s.fading()&&s.previous()==A&&s.current()==B&&s.weight()==0);
    assert(!s.canPreserveHistory());
    for(uint32_t t=101;t<1100;++t){
        const Direction latest=(t&1)?C:D;s.request(latest,t);
        assert(!s.canChangeDirection()&&s.needsRender()&&s.renderDirection()==B);
        assert(s.current()==B&&s.previous()==A);
        const float expected=float(t-100)/1000;assert(std::fabs(s.weight()-expected)<1e-7f);
        assert(!s.commit(latest,t,true)); // forbidden third-map publication
        assert(s.commit(B,t,false)); // ordinary dynamic redraw: old history stays valid
        assert(s.fading()&&s.previous()==A&&s.needsRender()&&s.weight()==expected);
    }
    s.request(C,1100);assert(!s.fading()&&s.canChangeDirection()&&s.weight()==1);
    assert(s.canPreserveHistory());
    assert(s.renderDirection()==C&&s.current()==B&&s.previous()==A);
    // Reuse, Defer and GPU failure omit commit: the candidate does not become current.
    for(uint32_t t=1101;t<1130;++t){s.request(C,t);assert(s.current()==B&&s.previous()==A&&s.needsRender());}
    assert(!s.commit(D,1130,true));assert(s.current()==B&&s.previous()==A);
    assert(s.commit(C,1130,true));assert(s.current()==C&&s.previous()==B&&s.weight()==0);
    s.request(C,1630);assert(s.commit(C,1630,true));assert(s.weight()==.5f&&s.previous()==B);
    // A request which returned to the committed direction cancels the queued change.
    s.request(D,1700);s.request(C,1800);assert(!s.needsRender());
    s.request(C,2130);assert(!s.fading()&&!s.needsRender()&&s.current()==C);
}
static void invalidation(){
    Slot s;initial(s,A,500);
    s.request(B,600);assert(s.commit(B,600,false)); // no preserved GPU history
    assert(!s.fading()&&s.weight()==1&&s.current()==B);
    s.request(C,700);assert(s.commit(C,700,true)&&s.fading());
    s.request(D,800);s.cancel();
    assert(!s.fading()&&!s.needsRender()&&s.current()==C&&s.renderDirection()==C);
    assert(!s.canPreserveHistory());
    assert(!s.commit(D,800,true)); // cancellation discarded the pending request
    s.request(D,801);assert(s.commit(D,801,true)&&!s.fading());
    // A completed same-direction replacement re-establishes a usable current map.
    s.request(A,900);assert(s.commit(A,900,true)&&s.fading());
    s.request(B,950,true);assert(!s.fading()&&s.needsRender()&&s.current()==A);
    assert(s.commit(B,950,true)&&!s.fading());
    s.request(B,1000,true);assert(s.commit(B,1000,false));
    s.request(C,1100);assert(s.commit(C,1100,true)&&s.fading());
    const float nan=std::numeric_limits<float>::quiet_NaN();
    for(Direction bad:{Direction{nan,0,1},Direction{0,INFINITY,1},Direction{0,0,0}}){
        s.request(bad,1200);assert(!s.fading()&&!s.needsRender()&&s.current()==C);
        assert(!s.commit(bad,1200,true));
    }
    s.reset();initial(s,D,0);
}
static void clocks(){
    Slot s;initial(s,A,1000);s.request(B,1100);assert(s.commit(B,1100,true));
    s.request(C,1600);assert(s.weight()==.5f);
    s.request(D,1500); // backwards: instant replacement, no old pending C
    assert(!s.fading()&&s.current()==B&&s.renderDirection()==D);
    assert(s.commit(D,1500,true)&&!s.fading());
    assert(s.canPreserveHistory());
    s.request(A,1600);assert(s.commit(A,1600,true)&&s.fading());
    s.request(B,2601);assert(!s.fading()&&s.current()==A);
    assert(!s.canPreserveHistory());
    assert(s.commit(B,2601,true)&&!s.fading());
    // Timer starts at successful commit, not at request/Defer preparation.
    s.request(C,2700);assert(s.current()==B);
    assert(s.commit(C,2800,true)&&s.weight()==0);
    s.request(C,3300);assert(s.weight()==.5f);
    s.reset();initial(s,A,UINT32_MAX-700);
    s.request(B,UINT32_MAX-500);assert(s.commit(B,UINT32_MAX-500,true));
    s.request(C,UINT32_MAX);assert(s.fading()&&s.weight()==.5f);
    s.request(C,499);assert(!s.fading()&&s.current()==B&&s.renderDirection()==C);
    assert(s.commit(C,499,true)&&s.previous()==B&&s.weight()==0);
    // A clock discontinuity during a successful GPU render still commits instantly.
    s.request(C,500);assert(s.commit(C,UINT32_MAX,true));
    assert(!s.fading()&&s.current()==C);
    s.request(D,10);assert(s.commit(D,10,true)&&s.fading()); // ordinary wrap
    s.reset();initial(s,A,100);
    s.request(B,200);assert(s.commit(B,1201,true)); // long successful GPU render
    assert(s.current()==B&&!s.fading()&&!s.needsRender());
    s.request(C,1300);assert(s.commit(C,1200,true)); // backwards during render
    assert(s.current()==C&&!s.fading());
}
static void independentSlots(){
    Slot near,far;initial(near,A,0);initial(far,A,0);
    near.request(B,10);far.request(B,10);assert(near.commit(B,10,true));
    assert(near.current()==B&&far.current()==A); // deferred far slot keeps its basis
    near.request(C,510);far.request(C,510);
    assert(near.renderDirection()==B&&far.renderDirection()==C);
    assert(far.commit(C,510,true));
    near.request(D,1010);far.request(D,1010);
    assert(near.canChangeDirection()&&!far.canChangeDirection());
    assert(near.commit(D,1010,true)&&near.previous()==B&&far.previous()==A);
    far.cancel();assert(near.fading()&&!far.fading());
}
static void randomized(){
    // Independent integer-time model: each slot owns exactly a current and a
    // previous identity; queued steps never rotate either, even across failures.
    const Direction dirs[]={A,B,C,D};std::mt19937 rng(107);
    for(unsigned trial=0;trial<100;++trial){
        Slot s;int current=-1,previous=-1,pending=-1;bool fade=false,allow=true;
        uint64_t now=0,start=0;unsigned rendered=0,requests=0;
        for(unsigned frame=0;frame<2000;++frame){
            now+=1+rng()%34;const int desired=int(rng()%4);const bool cancel=rng()%89==0;
            if(fade&&now-start>=1000)fade=false;
            if(cancel){fade=false;previous=-1;allow=false;}
            pending=desired;s.request(dirs[desired],uint32_t(now),cancel);++requests;
            assert(s.fading()==fade&&s.canChangeDirection()==!fade);
            assert(s.needsRender()==(current<0||pending!=current));
            assert(s.renderDirection()==dirs[fade?current:pending]);
            const float expected=fade?float(now-start)/1000:1;
            assert(std::fabs(s.weight()-expected)<1e-7f);
            const unsigned action=rng()%5; // omitted commits model Reuse/Defer/failure
            if(action<2){
                const int target=fade?current:pending;const bool history=rng()%7!=0;
                assert(s.commit(dirs[target],uint32_t(now),history));++rendered;
                if(target==current){allow=true;}
                else{fade=current>=0&&allow&&history;previous=fade?current:-1;
                    current=target;pending=-1;allow=true;start=now;}
            }
            assert(s.valid()==(current>=0));
            if(current>=0)assert(s.current()==dirs[current]);
            if(fade)assert(s.previous()==dirs[previous]);
            assert(s.fading()==fade);
        }
        assert(rendered<=requests);
    }
}
int main(){coalescing();invalidation();clocks();independentSlots();randomized();
    std::printf("PASS shadow transition: coalescing, commit/failure/defer, cancellation, optional history, independent slots, clock wrap/jumps; 200000 randomized frames; %zu bytes/slot\n",sizeof(Slot));}
