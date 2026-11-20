#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include "celestial_disc.h"
#include "../core/triangle_run.h"

// Viewer-to-sky occlusion, deliberately independent of light-to-world shadows.
namespace NorthlightCelestialTerrain {
constexpr float Near=.5f,Far=16384.f; // encloses the maximum loaded 4096u terrain square
// nearDistance: the mask's near plane along the body direction (maskNear() below); Near keeps all terrain.
inline void matrix(const NorthlightCelestialDisc::Disc& disc,const float* camera,
                   float extent,unsigned size,float* out,float nearDistance=Near){
    std::fill(out,out+16,0.f);
    const float cut=std::isfinite(nearDistance)&&nearDistance>Near&&nearDistance<Far*.5f?nearDistance:Near;
    const float scale=disc.tangentRadius*extent,depth=Far/(Far-cut);
    for(unsigned row=0;row<3;++row){
        out[row*4]=disc.right[row]/scale-disc.direction[row]/size;
        out[row*4+1]=disc.up[row]/scale+disc.direction[row]/size;
        out[row*4+2]=disc.direction[row]*depth;
        out[row*4+3]=disc.direction[row];
    }
    for(unsigned col=0;col<4;++col)for(unsigned row=0;row<3;++row)
        out[12+col]-=camera[row]*out[row*4+col];
    out[14]-=cut*depth;
}
// 0.3.217: the mask spans the whole glare (20R sun, 6R moon), so it must be cheap to keep. Terrain nearer than the
// game's far clip is in the depth buffer, which already hides the glare there; the mask keeps only what lies past
// it. Its near plane is perpendicular to the body direction, and every mask ray is within ~31 deg of that direction
// (the 20R corner of the sun mask), so a plane at FarCut x far clip drops only terrain nearer than the far clip.
constexpr float FarCut=.85f;
inline float maskNear(float farClip){return std::isfinite(farClip)&&farClip>0?std::max(Near,FarCut*farClip):Near;}
// Everything left in the mask lies at least drawnCut away, so an eye move of m shifts it by at most m/drawnCut
// radians. The glare is soft: a mask drawn from an eye within ParallaxLimit (~3e-3 rad, a few screen pixels) is
// reused. A nearer cut than the drawn one (the far clip closing in) needs a redraw; a farther one does not, the
// extra terrain is in the depth buffer anyway. With the default Near the tolerance is ~1.5 mm: a moved eye redraws.
constexpr float ParallaxLimit=3e-3f;
inline bool eyeReusable(const float* drawnEye,const float* eye,float drawnCut,float cut){
    if(!drawnEye||!eye||!std::isfinite(drawnCut)||!std::isfinite(cut)||!(drawnCut>0)||drawnCut>cut*1.001f)return false;
    double move2=0;for(unsigned k=0;k<3;++k){const double d=double(eye[k])-drawnEye[k];if(!std::isfinite(d))return false;move2+=d*d;}
    const double limit=double(ParallaxLimit)*drawnCut;return move2<=limit*limit;
}
// A quarter texel of the 2048 sun mask is ~5e-5 rad, far below a screen pixel; the body drifts about a quarter
// texel per second, so a still eye redraws about every second or less instead of every frame.
constexpr float ReuseTexels=.25f;
// Largest mask displacement in texels between two matrices from matrix() for rays from the SAME eye. The x, y, w
// clip terms are t*(M3.r) for p=camera+t*r (the translation row cancels them), so uv does not depend on t.
// Translation is ignored by design: the caller compares the eye separately. Non-finite or w<=0 gives +infinity.
inline float rayShiftTexels(const float* drawn,const float* current,const NorthlightCelestialDisc::Disc& disc,float extent,unsigned size){
    const float inf=std::numeric_limits<float>::infinity();const double reach=double(disc.tangentRadius)*extent;double worst=0; // doubles: the matrix terms cancel, floats would lose ~10% of a 0.1 texel
    for(int sy=-1;sy<=1;++sy)for(int sx=-1;sx<=1;++sx){
        double r[3];for(unsigned k=0;k<3;++k)r[k]=disc.direction[k]+reach*(sx*disc.right[k]+sy*disc.up[k]);
        double uv[2][2];
        for(unsigned which=0;which<2;++which){
            const float* m=which?current:drawn;double c[3]={};
            for(unsigned col=0;col<3;++col){const unsigned i=col==2?3:col;for(unsigned k=0;k<3;++k)c[col]+=m[k*4+i]*r[k];}
            if(!std::isfinite(c[0])||!std::isfinite(c[1])||!std::isfinite(c[2])||!(c[2]>0))return inf;
            uv[which][0]=c[0]/c[2];uv[which][1]=c[1]/c[2];
        }
        const double d=std::max(std::fabs(uv[1][0]-uv[0][0]),std::fabs(uv[1][1]-uv[0][1]))*size*.5;
        if(!std::isfinite(d))return inf;worst=std::max(worst,d);
    }
    return float(worst);
}
// Homogeneous perspective planes. Never use the affine shadow-map culler here.
struct Frustum {
    double planes[6][4]={};bool valid=true;
    explicit Frustum(const float* m){
        for(unsigned k=0;k<16;++k)if(!std::isfinite(m[k]))valid=false;
        for(unsigned plane=0;plane<6;++plane)for(unsigned row=0;row<4;++row){
            const float* r=m+row*4;
            planes[plane][row]=plane<4?double(r[3])+(plane%2?-1.:1.)*r[plane/2]:
                              plane==4?double(r[2]):double(r[3])-r[2];
        }
    }
    template<class V> bool reject(V lo,V hi)const{
        if(!valid)return false;
        const float low[]={lo.x,lo.y,lo.z},high[]={hi.x,hi.y,hi.z};
        for(unsigned k=0;k<3;++k)if(!std::isfinite(low[k])||!std::isfinite(high[k])||low[k]>high[k])return false;
        for(const auto& p:planes){
            double upper=p[3],magnitude=std::fabs(upper);
            for(unsigned row=0;row<3;++row){
                const double a=low[row]*p[row],b=high[row]*p[row];
                upper+=std::max(a,b);magnitude+=std::max(std::fabs(a),std::fabs(b));
            }
            if(upper<-(.001+magnitude*16*std::numeric_limits<float>::epsilon()))return true;
        }
        return false;
    }
};
template<class V> bool reject(V lo,V hi,const float* m){return Frustum(m).reject(lo,hi);}
// 0.3.175 mask batching (S1). The mask pass writes a constant with no depth test, depth write or
// blending, so the same triangles give the same mask in any grouping. Accepted terrain batches of
// one page whose index ranges are contiguous in list order (prev.start + 3*prev.count == start)
// are one DrawIndexedPrimitive over exactly their indices, with the union vertex range. A page
// change, an index gap (a rejected or non-terrain batch between) or maxIndices ends a run.
using Run=NorthlightTriangleRun::Run;
struct RunStats {unsigned accepted=0,runs=0;std::uint64_t triangles=0;};
// list: batch indices in batch order. accept(batch): drawn at all. emit(run): false stops (failure).
template<class Batches,class List,class Accept,class Emit> bool forEachRun(const Batches& batches,const List& list,std::uint32_t maxIndices,Accept accept,Emit emit,RunStats& stats){
    Run run;bool open=false;
    for(const auto index:list){const auto& b=batches[index];if(!accept(b))continue;++stats.accepted;stats.triangles+=b.count;
        if(open&&run.append(b,maxIndices))continue;
        if(open){++stats.runs;if(!emit(run))return false;}
        run.begin(b);open=true;}
    if(open){++stats.runs;if(!emit(run))return false;}
    return true;
}
}
