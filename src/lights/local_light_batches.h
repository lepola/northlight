#pragma once
// Screen-coherent lamp batches. LocalDirect (8 lamps) and LocalFog (4) draw one scissored half-res quad per batch, the rect the
// union of its lamps' projected spheres (local_light_scissor.h). The selection's closest-first order spreads every batch over
// the screen; arrange() reorders the slots so each batch holds lamps that are near each other on screen: lamps whose sphere
// covers the target first (their batches are whole-target anyway), then the others along a Hilbert curve of their rect
// centres, then the lamps no pixel sees. The passes are ONE/ONE sums (the lamp fog cap runs once over the summed
// accumulator), so the order changes no pixel beyond float addition order. The new order is kept only when the cost model
// (pixels times shader slots) says it is cheaper than the old one.
// wide(): two neighbouring 8-lamp batches draw as one LocalDirect16 pass when its rect costs less than both.
#include "local_light_scissor.h"
#include "local_light_selection.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace NorthlightLocalLightBatches {
// Per-pixel cost of one pass in shader slots (shaders/world-shader-build.json static_instruction_slots) plus Blend, the
// blended FP16 read-modify-write and shared screen fetches every pass repeats.
inline constexpr double DirectSlots=288,Direct16Slots=400,FogSlots=397,Blend=40;
using Rect=NorthlightLocalLightScissor::Rect;
inline Rect unite(const Rect& a,const Rect& b){
    if(!a.area())return b;if(!b.area())return a;
    return Rect{std::min(a.left,b.left),std::min(a.top,b.top),std::max(a.right,b.right),std::max(a.bottom,b.bottom)};}
// True when one LocalDirect16 pass over unite(a,b) costs less than the two 8-lamp passes.
inline bool wide(const Rect& a,const Rect& b){
    const long areaA=a.area(),areaB=b.area();
    if(!areaA||!areaB)return false; // one 8-lamp pass already covers every non-zero pixel
    const double split=double(areaA)*(DirectSlots+Blend)+double(areaB)*(DirectSlots+Blend);
    return split>0&&double(unite(a,b).area())*(Direct16Slots+Blend)<split;}
// Hilbert index of (x, y) on a 2^order grid.
// Each nibble records its rank and the swap/flip applied to the next nibble.
// Two lookups reproduce all eight screen-grid bits; other orders keep the loop.
inline constexpr auto HilbertNibble=[](){
    std::array<std::uint16_t,256> table{};
    for(unsigned input=0;input<256;++input){unsigned x=input&15,y=input>>4,d=0,swap=0,flip=0;
        for(unsigned s=8;s;s>>=1){const unsigned rx=(x&s)?1:0,ry=(y&s)?1:0;d+=s*s*((3*rx)^ry);
            if(!ry){swap^=1;if(rx){flip^=1;x=s-1-x;y=s-1-y;}const unsigned temp=x;x=y;y=temp;}}
        table[input]=static_cast<std::uint16_t>(d|(swap<<8)|(flip<<9));
    }
    return table;
}();
inline std::uint32_t hilbert(std::uint32_t x,std::uint32_t y,unsigned order=8){
    if(order==8){const auto high=HilbertNibble[((x>>4)&15)|((y>>4)&15)<<4];
        unsigned a=x&15,b=y&15;const unsigned exchange=(a^b)&(0u-((high>>8)&1u));a^=exchange;b^=exchange;
        const unsigned flip=15u*(high>>9);a^=flip;b^=flip;
        return (unsigned(high&255)<<8)|(HilbertNibble[a|(b<<4)]&255u);
    }
    std::uint32_t d=0;
    for(std::uint32_t s=1u<<(order-1);s;s>>=1){
        const std::uint32_t rx=(x&s)?1:0,ry=(y&s)?1:0;d+=s*s*((3*rx)^ry);
        if(!ry){if(rx){x=s-1-x;y=s-1-y;}std::swap(x,y);}
    }
    return d;}
// Pass cost of `count` lamps in the order `slot` (rects per slot): every N-lamp batch's rect (the bounding box of its lamps'
// rects) times its slots, the 8-lamp passes merged where wide() says so.
template<unsigned N> double batchCost(const Rect* rects,const unsigned* slot,unsigned count,double slots){
    double total=0;
    for(unsigned first=0;first<count;first+=N){Rect r{};for(unsigned i=first;i<first+N&&i<count;++i)r=unite(r,rects[slot[i]]);total+=double(r.area())*(slots+Blend);}
    return total;}
inline double directCost(const Rect* rects,const unsigned* slot,unsigned count){
    double total=0;const unsigned N=NorthlightLocalLightSelection::DirectBatchSize;
    for(unsigned first=0;first<count;){
        Rect a{},b{};for(unsigned i=first;i<first+N&&i<count;++i)a=unite(a,rects[slot[i]]);
        if(first+N<count){for(unsigned i=first+N;i<first+2*N&&i<count;++i)b=unite(b,rects[slot[i]]);
            if(wide(a,b)){total+=double(unite(a,b).area())*(Direct16Slots+Blend);first+=2*N;continue;}}
        total+=double(a.area())*(DirectSlots+Blend);first+=N;}
    return total;}
inline double cost(const Rect* rects,const unsigned* slot,unsigned count){
    constexpr unsigned Fog=NorthlightLocalLightSelection::FogBatchSize,Direct=NorthlightLocalLightSelection::DirectBatchSize,Slots=NorthlightLocalLightSelection::Slots;
    static_assert(Direct==2*Fog,"reuse pairs of fog batches");
    if(count>Slots)return directCost(rects,slot,count)+batchCost<Fog>(rects,slot,count,FogSlots);
    // The direct passes combine pairs of these same four-light fog rectangles.
    std::array<Rect,(Slots+Fog-1)/Fog> groups;
    double direct=0,fog=0;unsigned groupCount=0;
    for(unsigned first=0;first<count;first+=Fog){Rect r{};
        for(unsigned i=first;i<first+Fog&&i<count;++i)r=unite(r,rects[slot[i]]);
        groups[groupCount++]=r;fog+=double(r.area())*(FogSlots+Blend);
    }
    for(unsigned first=0;first<groupCount;){Rect a=groups[first];if(first+1<groupCount)a=unite(a,groups[first+1]);
        if(first+2<groupCount){Rect b=groups[first+2];if(first+3<groupCount)b=unite(b,groups[first+3]);
            if(wide(a,b)){direct+=double(unite(a,b).area())*(Direct16Slots+Blend);first+=4;continue;}}
        direct+=double(a.area())*(DirectSlots+Blend);first+=2;
    }
    return direct+fog;}
struct Result {bool arranged=false;double before=0,after=0;};
// Reorders s's slots [0, count) (position, colour, fog together). An invalid view keeps the order (every rect is the whole target).
inline Result arrange(NorthlightLocalLightSelection::Selection& s,const NorthlightLocalLightScissor::View& v){
    Result out;const unsigned count=std::min<unsigned>(s.count,NorthlightLocalLightSelection::Slots);
    if(!v.valid||count<2)return out;
    // One fog group is also one direct group: permuting it cannot save a pixel.
    if(count<=NorthlightLocalLightSelection::FogBatchSize){Rect group{};
        for(unsigned i=0;i<count;++i){NorthlightLocalLightScissor::Bounds b;NorthlightLocalLightScissor::add(v,s.position[i].data(),b);
            group=unite(group,NorthlightLocalLightScissor::rect(v,b));}
        out.before=out.after=double(group.area())*(DirectSlots+Blend)+double(group.area())*(FogSlots+Blend);return out;
    }
    std::array<Rect,NorthlightLocalLightSelection::Slots> rects;
    std::array<std::uint64_t,NorthlightLocalLightSelection::Slots> key;
    std::array<unsigned,NorthlightLocalLightSelection::Slots> before,after;
    const long all=long(v.halfW)*long(v.halfH);
    for(unsigned i=0;i<count;++i){
        NorthlightLocalLightScissor::Bounds b;NorthlightLocalLightScissor::add(v,s.position[i].data(),b);rects[i]=NorthlightLocalLightScissor::rect(v,b);
        before[i]=after[i]=i;
        const long area=rects[i].area();
        // Class in the top bits (whole target, partial, unseen), then the Hilbert index of the rect centre, then the old slot.
        const std::uint64_t cls=area>=all?0u:area?1u:2u;
        std::uint32_t rank=0;
        if(cls==1){
            const auto cx=std::uint32_t(std::clamp((rects[i].left+rects[i].right)*128L/std::max(1L,long(v.halfW)),0L,255L));
            const auto cy=std::uint32_t(std::clamp((rects[i].top+rects[i].bottom)*128L/std::max(1L,long(v.halfH)),0L,255L));
            rank=hilbert(cx,cy);
        }
        key[i]=cls<<40|std::uint64_t(rank)<<8|i;}
    if(std::is_sorted(key.begin(),key.begin()+count)){out.after=out.before=cost(rects.data(),before.data(),count);return out;}
    std::sort(after.begin(),after.begin()+count,[&](unsigned a,unsigned b){return key[a]<key[b];});
    out.before=cost(rects.data(),before.data(),count);out.after=cost(rects.data(),after.data(),count);
    if(!(out.after<out.before)){out.after=out.before;return out;}
    // Apply cycles of the verified permutation without copying inactive slots.
    for(unsigned i=0;i<count;++i){
        if(after[i]==i)continue;
        const auto position=s.position[i],color=s.color[i],fog=s.fog[i];const auto source=s.sources[i];
        unsigned at=i;
        while(after[at]!=i){const unsigned from=after[at];
            s.position[at]=s.position[from];s.color[at]=s.color[from];s.fog[at]=s.fog[from];s.sources[at]=s.sources[from];
            after[at]=at;at=from;
        }
        s.position[at]=position;s.color[at]=color;s.fog[at]=fog;s.sources[at]=source;after[at]=at;
    }
    out.arranged=true;return out;}
} // namespace NorthlightLocalLightBatches
