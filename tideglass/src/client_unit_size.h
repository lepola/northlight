#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace water::client12340
{
struct UnitModelSize { float width=0, height=0; };
// Build 12340's loaded DBC index: bounded min/max IDs and a pointer per row.
// Verified against its 0x65c290 lookup and live rows. Never call client code or
// open archives from the frame capture thread. These tables include active art.
template<class Read> uint32_t UnitSizeRow(Read read,uint32_t table,uint32_t vtable,uint32_t id)
{
    uint32_t low=0,high=0,index=0,tag=0,row=0,actual=0;
    if (!id || id>1000000 || !read(table,&tag,4) || tag!=vtable ||
        !read(table-8,&low,4) || !read(table-12,&high,4) ||
        high<low || high-low>1000000 || id<low || id>high ||
        !read(table+8,&index,4) || index<0x10000 || index>0x7fffffff-4*(id-low) ||
        !read(index+4*(id-low),&row,4) || row<0x10000 || row>0x7fffff80 ||
        !read(row,&actual,4) || actual!=id) return 0;
    return row;
}
template<class Read> bool ReadUnitModelSize(Read read,uint32_t display,UnitModelSize& output)
{
    const auto d=UnitSizeRow(read,0xad34d0,0xa27560,display);
    uint32_t model=0;float displayScale=0,modelScale=0,shape[2]={},box[6]={};
    if (!d || !read(d+4,&model,4) || !read(d+16,&displayScale,4)) return false;
    const auto m=UnitSizeRow(read,0xad3518,0xa275b0,model);
    if (!m || !read(m+16,&modelScale,4) || !read(m+56,shape,sizeof(shape)) || !read(m+68,box,sizeof(box))) return false;
    const float scale=displayScale*modelScale;
    if (!std::isfinite(scale) || scale<=0 || scale>100 || !std::isfinite(shape[0]) || !std::isfinite(shape[1]) ||
        shape[0]<=0 || shape[1]<=0) return false;
    float height=shape[1];
    if (std::isfinite(box[2]) && std::isfinite(box[5]) && box[5]>box[2]) height=std::max(height,box[5]-box[2]);
    const UnitModelSize size{shape[0]*scale,height*scale};
    if (!std::isfinite(size.width) || !std::isfinite(size.height) || size.width>100 || size.height>100) return false;
    output=size;return true;
}
struct UnitRippleSize {float radius=.25f,strength=1,lead=.125f;};
inline UnitRippleSize RippleSize(const UnitModelSize& model,float objectScale)
{
    if (!std::isfinite(objectScale) || objectScale<=0 || objectScale>100) objectScale=1;
    const float height=model.height*objectScale,width=model.width*objectScale;
    // The 0.15-yard grid cannot resolve an arbitrarily small disturbance. Bound
    // radius at half a cell; height limits squat models' broad collision widths.
    const float radius=std::clamp(std::min(width*.4f,height*.12f),.075f,3.f);
    return {radius,std::clamp(height/2.4f,.15f,3.f),radius*.5f};
}
}
