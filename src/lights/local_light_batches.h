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
// blended FP16 read-modify-write and the four screen fetches every pass repeats.
inline constexpr double DirectSlots=194,Direct16Slots=314,FogSlots=397,Blend=40;
using Rect=NorthlightLocalLightScissor::Rect;
inline Rect unite(const Rect& a,const Rect& b){
    if(!a.area())return b;if(!b.area())return a;
    return Rect{std::min(a.left,b.left),std::min(a.top,b.top),std::max(a.right,b.right),std::max(a.bottom,b.bottom)};}
// True when one LocalDirect16 pass over unite(a,b) costs less than the two 8-lamp passes.
inline bool wide(const Rect& a,const Rect& b){
    const double split=double(a.area())*(DirectSlots+Blend)+double(b.area())*(DirectSlots+Blend);
    return split>0&&double(unite(a,b).area())*(Direct16Slots+Blend)<split;}
// Hilbert index of (x, y) on a 2^order grid.
inline std::uint32_t hilbert(std::uint32_t x,std::uint32_t y,unsigned order=8){
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
    return directCost(rects,slot,count)+batchCost<NorthlightLocalLightSelection::FogBatchSize>(rects,slot,count,FogSlots);}
struct Result {bool arranged=false;double before=0,after=0;};
// Reorders s's slots [0, count) (position, colour, fog together). An invalid view keeps the order (every rect is the whole target).
inline Result arrange(NorthlightLocalLightSelection::Selection& s,const NorthlightLocalLightScissor::View& v){
    Result out;const unsigned count=std::min<unsigned>(s.count,NorthlightLocalLightSelection::Slots);
    if(!v.valid||count<2)return out;
    std::array<Rect,NorthlightLocalLightSelection::Slots> rects;
    std::array<std::uint64_t,NorthlightLocalLightSelection::Slots> key;
    std::array<unsigned,NorthlightLocalLightSelection::Slots> before,after;
    const long all=long(v.halfW)*long(v.halfH);
    for(unsigned i=0;i<count;++i){
        NorthlightLocalLightScissor::Bounds b;NorthlightLocalLightScissor::add(v,s.position[i].data(),b);rects[i]=NorthlightLocalLightScissor::rect(v,b);
        const long area=rects[i].area();
        // Class in the top bits (whole target, partial, unseen), then the Hilbert index of the rect centre, then the old slot.
        const std::uint64_t cls=area>=all?0u:area?1u:2u;
        const std::uint32_t cx=std::uint32_t(std::clamp((rects[i].left+rects[i].right)*128L/std::max(1L,long(v.halfW)),0L,255L));
        const std::uint32_t cy=std::uint32_t(std::clamp((rects[i].top+rects[i].bottom)*128L/std::max(1L,long(v.halfH)),0L,255L));
        key[i]=cls<<40|std::uint64_t(cls==1?hilbert(cx,cy):0u)<<8|i;
        before[i]=after[i]=i;}
    std::sort(after.begin(),after.begin()+count,[&](unsigned a,unsigned b){return key[a]<key[b];});
    out.before=cost(rects.data(),before.data(),count);out.after=cost(rects.data(),after.data(),count);
    if(!(out.after<out.before)){out.after=out.before;return out;}
    const auto position=s.position,color=s.color,fog=s.fog;const auto sources=s.sources;
    for(unsigned i=0;i<count;++i){s.position[i]=position[after[i]];s.color[i]=color[after[i]];s.fog[i]=fog[after[i]];s.sources[i]=sources[after[i]];}
    out.arranged=true;return out;}
} // namespace NorthlightLocalLightBatches
