#pragma once
#include "world_gi.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace NorthlightShadowBounds {
// Conservative culling for the renderer's affine world->light clip matrices:
// row-vector convention, X/Y in [-1,1], Z in [0,1], W identically one.
// Never camera-frustum cull shadow casters: offscreen geometry still casts light.
// Returns false (draw) for invalid bounds/matrices or unsupported perspective.
// Finite affine light matrix: the precondition of every test below. Callers
// testing many boxes against one matrix check it once and use the *Affine forms.
inline bool affineLightMatrix(const float* matrix){
    if(!matrix)return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(matrix[i]))return false;
    return matrix[3]==0&&matrix[7]==0&&matrix[11]==0&&matrix[15]==1;
}
inline bool clipRejectAffine(NorthlightGI::Vec3 low,NorthlightGI::Vec3 high,
                             const float* matrix,float epsilon=1e-4f,bool retainUpstream=false) {
    if(!std::isfinite(epsilon)||epsilon<0)return false;
    const float lo[]={low.x,low.y,low.z},hi[]={high.x,high.y,high.z};
    double extent[3];
    for(unsigned axis=0;axis<3;++axis){
        if(!std::isfinite(lo[axis])||!std::isfinite(hi[axis])||lo[axis]>hi[axis])return false;
        extent[axis]=std::max(std::fabs(double(lo[axis])),std::fabs(double(hi[axis])));
    }
    for(unsigned axis=0;axis<3;++axis) {
        double lower=matrix[12+axis],upper=lower,magnitude=std::fabs(lower);
        for(unsigned row=0;row<3;++row){
            const double a=double(lo[row])*matrix[row*4+axis],b=double(hi[row])*matrix[row*4+axis];
            lower+=std::min(a,b);upper+=std::max(a,b);
            magnitude+=extent[row]*std::fabs(double(matrix[row*4+axis]));
        }
        // Interval extrema replace eight corner projections. The maximum
        // magnitude bounds EVERY corner's old roundoff expansion (including
        // world-coordinate cancellation), so this can only retain more casters.
        const double margin=double(epsilon)+magnitude*(8.0*std::numeric_limits<float>::epsilon());
        const double minimum=lower-margin,maximum=upper+margin;
        // A separating light plane is sufficient. Do not project the other
        // axes once this box is proven outside; invalid boxes were checked above.
        if(axis<2?(maximum<-1||minimum>1):((!retainUpstream&&maximum<0)||minimum>1))return true;
    }
    return false;
}
inline bool clipReject(NorthlightGI::Vec3 low,NorthlightGI::Vec3 high,
                       const float* matrix,float epsilon=1e-4f,bool retainUpstream=false) {
    return affineLightMatrix(matrix)&&clipRejectAffine(low,high,matrix,epsilon,retainUpstream);
}
// Fast depth output is permitted only when every corner stays strictly inside
// BOTH depth planes, including conservative CPU/GPU float roundoff. Unknown or
// perspective inputs use the raw-depth fragment path. No XY/selection change.
inline bool depthFullyInsideAffine(NorthlightGI::Vec3 low,NorthlightGI::Vec3 high,
                                   const float* matrix,float epsilon=1e-4f){
    if(!std::isfinite(epsilon)||epsilon<0)return false;
    const float lo[]={low.x,low.y,low.z},hi[]={high.x,high.y,high.z};
    for(unsigned axis=0;axis<3;++axis)if(!std::isfinite(lo[axis])||!std::isfinite(hi[axis])||lo[axis]>hi[axis])return false;
    double minimum=matrix[14],maximum=minimum,magnitude=std::fabs(minimum);
    for(unsigned axis=0;axis<3;++axis){
        const double coefficient=matrix[axis*4+2],a=double(lo[axis])*coefficient,b=double(hi[axis])*coefficient;
        minimum+=std::min(a,b);maximum+=std::max(a,b);magnitude+=std::max(std::fabs(a),std::fabs(b));
    }
    // 32 eps also covers the preceding local->world float transform and
    // its AABB center/extent computation, including large-coordinate
    // cancellation. Conservative rejection costs speed, never coverage.
    const double margin=double(epsilon)+magnitude*(32.0*std::numeric_limits<float>::epsilon());
    if(!(minimum-margin>0&&maximum+margin<1))return false;
    return true;
}
inline bool depthFullyInside(NorthlightGI::Vec3 low,NorthlightGI::Vec3 high,
                             const float* matrix,float epsilon=1e-4f){
    return affineLightMatrix(matrix)&&depthFullyInsideAffine(low,high,matrix,epsilon);
}
// Integer texel rectangle [left,right)x[top,bottom) of a size x size viewport.
struct TexelRect {
    long left=0,top=0,right=0,bottom=0;
    bool empty()const{return left>=right||top>=bottom;}
    long long area()const{return empty()?0:(long long)(right-left)*(bottom-top);}
};
inline TexelRect unite(const TexelRect& a,const TexelRect& b){if(a.empty())return b;if(b.empty())return a;return {std::min(a.left,b.left),std::min(a.top,b.top),std::max(a.right,b.right),std::max(a.bottom,b.bottom)};}
inline bool intersects(const TexelRect& a,const TexelRect& b){return !a.empty()&&!b.empty()&&a.left<b.right&&b.left<a.right&&a.top<b.bottom&&b.top<a.bottom;}
// Conservative raster footprint of an AABB under an affine light matrix in a
// size x size viewport: the clip-space interval (with clipReject's roundoff
// margin) plus `margin` whole texels on every side, covering either pixel-center
// convention and GPU float transform error. Depth is ignored (only widens).
// Returns false when no footprint can be proven; callers must then draw.
inline bool texelFootprint(NorthlightGI::Vec3 low,NorthlightGI::Vec3 high,const float* matrix,long size,long margin,TexelRect& out){
    if(!matrix||size<=0||margin<0)return false;
    const float lo[]={low.x,low.y,low.z},hi[]={high.x,high.y,high.z};
    for(unsigned axis=0;axis<3;++axis)if(!std::isfinite(lo[axis])||!std::isfinite(hi[axis])||lo[axis]>hi[axis])return false;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(matrix[i]))return false;
    if(matrix[3]!=0||matrix[7]!=0||matrix[11]!=0||matrix[15]!=1)return false;
    double minimum[2],maximum[2];
    for(unsigned axis=0;axis<2;++axis){
        double lower=matrix[12+axis],upper=lower,magnitude=std::fabs(lower);
        for(unsigned row=0;row<3;++row){const double a=double(lo[row])*matrix[row*4+axis],b=double(hi[row])*matrix[row*4+axis];lower+=std::min(a,b);upper+=std::max(a,b);magnitude+=std::max(std::fabs(a),std::fabs(b));}
        const double m=1e-4+magnitude*(8.0*std::numeric_limits<float>::epsilon());minimum[axis]=lower-m;maximum[axis]=upper+m;
    }
    const double half=.5*double(size),limit=double(size+margin+1);
    auto clampTexel=[&](double v){return long(std::max(-limit,std::min(limit,v)));};
    // Viewport: x=(ndc+1)*size/2, y=(1-ndc)*size/2.
    const double x0=std::floor((minimum[0]+1)*half),x1=std::ceil((maximum[0]+1)*half),y0=std::floor((1-maximum[1])*half),y1=std::ceil((1-minimum[1])*half);
    if(!std::isfinite(x0)||!std::isfinite(x1)||!std::isfinite(y0)||!std::isfinite(y1))return false;
    out.left=std::max(0L,clampTexel(x0)-margin);out.top=std::max(0L,clampTexel(y0)-margin);
    out.right=std::min(size,clampTexel(x1)+margin+1);out.bottom=std::min(size,clampTexel(y1)+margin+1);
    if(out.empty())out=TexelRect{};
    return true;
}
// Disjoint cover of footprints: tile x tile cells, horizontal runs merged down
// identical spans, then cheapest-growth pairs merged (absorbing any rect the
// merge overlaps) until at most maxRects remain; over 64 runs or a mostly dirty
// map collapse to one bounding rect. Disjointness lets one ordered draw walk
// scissor each draw per rect without changing any pixel's fragment order.
inline void dirtyRects(const std::vector<TexelRect>& footprints,long size,long tile,size_t maxRects,std::vector<TexelRect>& out){
    out.clear();if(size<=0||tile<=0)return;const long n=(size+tile-1)/tile;std::vector<unsigned char> mask(size_t(n*n),0);TexelRect all;
    for(const auto& f:footprints){if(f.empty())continue;all=unite(all,f);
        for(long y=f.top/tile;y<=(f.bottom-1)/tile&&y<n;++y)for(long x=f.left/tile;x<=(f.right-1)/tile&&x<n;++x)mask[size_t(y*n+x)]=1;}
    if(all.empty())return;
    std::vector<size_t> open;
    for(long y=0;y<n;++y){std::vector<size_t> next;
        for(long x=0;x<n;){if(!mask[size_t(y*n+x)]){++x;continue;}long e=x;while(e<n&&mask[size_t(y*n+e)])++e;
            const TexelRect run={x*tile,y*tile,std::min(size,e*tile),std::min(size,(y+1)*tile)};bool merged=false;
            for(size_t i:open)if(out[i].left==run.left&&out[i].right==run.right&&out[i].bottom==run.top){out[i].bottom=run.bottom;next.push_back(i);merged=true;break;}
            if(!merged){next.push_back(out.size());out.push_back(run);}x=e;}
        open.swap(next);}
    if(out.size()>64||!maxRects){out.clear();out.push_back(all);return;}
    while(out.size()>maxRects){size_t bi=0,bj=1;long long best=-1;
        for(size_t i=0;i<out.size();++i)for(size_t j=i+1;j<out.size();++j){const long long growth=unite(out[i],out[j]).area()-out[i].area()-out[j].area();if(best<0||growth<best){best=growth;bi=i;bj=j;}}
        out[bi]=unite(out[bi],out[bj]);out.erase(out.begin()+long(bj));if(bj<bi)--bi;
        for(bool grown=true;grown;){grown=false;for(size_t k=0;k<out.size();++k)if(k!=bi&&intersects(out[bi],out[k])){out[bi]=unite(out[bi],out[k]);out.erase(out.begin()+long(k));if(k<bi)--bi;grown=true;break;}}
    }
    long long dirty=0;for(const auto& r:out)dirty+=r.area();
    if(out.size()>1&&dirty*10>(long long)size*size*7){out.clear();out.push_back(all);}
}
// Only for the persistent directional pass whose shader clamps upstream
// depth. Receivers inside the volume can still be occluded from before z=0.
inline bool directionalClipReject(NorthlightGI::Vec3 low,NorthlightGI::Vec3 high,const float* matrix){
    return clipReject(low,high,matrix,1e-4f,true);
}
inline bool directionalClipRejectAffine(NorthlightGI::Vec3 low,NorthlightGI::Vec3 high,const float* matrix){
    return clipRejectAffine(low,high,matrix,1e-4f,true);
}
} // namespace NorthlightShadowBounds
