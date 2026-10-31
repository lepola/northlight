#pragma once
// Conservative half-resolution scissor for the additive local light passes
// (LocalDirect, LocalFog, LocalLighting). Each pass adds exactly zero unless
// the pixel's reconstructed view ray meets a light's attenuation sphere at a
// positive forward distance, so skipping pixels outside the union of the
// projected spheres leaves ONE/ONE blended targets bit-identical.
// Reconstruction mirrors the shaders: full-res texel ix=floor(uv*w),
// ndc=(2ix/w-1, 1-2iy/h), world=eye+z*(ndc.x/P0*r0+ndc.y/P1*r1+Pz*r2).
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace NorthlightLocalLightScissor {
inline constexpr bool Enabled=true;
inline constexpr long Margin=2; // half-res pixels on every side
struct View {
    double inverse[3][3]={},eye[3]={},stretch=1,minForward=0;
    double projX=1,projY=1;unsigned fullW=0,fullH=0,halfW=0,halfH=0;bool valid=false;
};
// NDC union of every light that can add a non-zero value; full wins.
struct Bounds {double x0=1e30,y0=1e30,x1=-1e30,y1=-1e30;bool full=false;
    bool empty()const{return !full&&x0>x1;}};
struct Rect {long left=0,top=0,right=0,bottom=0;
    long area()const{return right>left&&bottom>top?(right-left)*(bottom-top):0;}};
// inverseView: row-major rows r0,r1,r2 and eye in [12..14] (shader c3..c6).
// nearZ bounds surface forward distance (LocalDirect, LocalLighting); fogNear
// is FogRange.x, the glow's minimum distance along the normalized ray.
inline View view(const float* inverseView,const float* projection,float nearZ,float fogNear,unsigned w,unsigned h){
    View v;
    if(w>32768||h>32768)return v;
    v.fullW=w;v.fullH=h;v.halfW=w/2;v.halfH=h/2; // rect() fails open to these
    if(!inverseView||!projection||w<2||h<2)return v;
    for(int i=0;i<16;++i)if(!std::isfinite(inverseView[i]))return v;
    for(int i=0;i<3;++i)if(!std::isfinite(projection[i])||std::fabs(projection[i])<1e-4f)return v;
    if(!std::isfinite(nearZ)||!(nearZ>0)||!std::isfinite(fogNear)||!(fogNear>0))return v;
    double m[3][3]; // columns r0,r1,Pz*r2: world offset = m*(z*a,z*b,z)
    for(int k=0;k<3;++k){m[k][0]=inverseView[k];m[k][1]=inverseView[4+k];m[k][2]=double(projection[2])*inverseView[8+k];}
    // |m*u| >= sigmaMin*|u|; sigma^2 lies within 1 +- ||m'm-I||_F.
    double fro=0;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){double g=0;for(int k=0;k<3;++k)g+=m[k][i]*m[k][j];g-=i==j;fro+=g*g;}
    fro=std::sqrt(fro);if(!(fro<.25))return v;
    const double det=m[0][0]*(m[1][1]*m[2][2]-m[1][2]*m[2][1])-m[0][1]*(m[1][0]*m[2][2]-m[1][2]*m[2][0])+m[0][2]*(m[1][0]*m[2][1]-m[1][1]*m[2][0]);
    if(!(std::fabs(det)>.1))return v;
    for(int i=0;i<3;++i)for(int j=0;j<3;++j){
        const int i1=(j+1)%3,i2=(j+2)%3,j1=(i+1)%3,j2=(i+2)%3;
        v.inverse[i][j]=(m[i1][j1]*m[i2][j2]-m[i1][j2]*m[i2][j1])/det;
    }
    for(int i=0;i<3;++i)v.eye[i]=inverseView[12+i];
    v.stretch=1/std::sqrt(1-fro);v.projX=projection[0];v.projY=projection[1];
    // Lamp glow points are >= fogNear along the ray; the longest screen ray
    // direction (a,b,1) has |m*(a,b,1)| <= sqrt(1+fro)*sqrt(1/P0^2+1/P1^2+1).
    const double longest=std::sqrt(1+fro)*std::sqrt(1/(v.projX*v.projX)+1/(v.projY*v.projY)+1);
    v.minForward=.5*std::min(double(nearZ),double(fogNear)/longest);
    v.valid=v.halfW&&v.halfH;return v;
}
// light: xyz world position, w attenuation end (the shader's hard cutoff).
inline void add(const View& v,const float* light,Bounds& b){
    if(b.full)return;
    if(!v.valid){b.full=true;return;}
    for(int i=0;i<4;++i)if(!std::isfinite(light[i])){b.full=true;return;}
    double d[3],c[3]={},scale=1+std::fabs(light[3]);
    for(int i=0;i<3;++i){d[i]=double(light[i])-v.eye[i];scale+=std::fabs(double(light[i]))+std::fabs(v.eye[i]);}
    for(int i=0;i<3;++i)for(int k=0;k<3;++k)c[i]+=v.inverse[i][k]*d[k];
    // Relative slack for rsq/rcp, plus absolute slack for float world
    // reconstruction and the glow ray's Camera-endpoint subtraction.
    const double r=(std::fabs(double(light[3]))*(1+1./1024)+scale*(1./65536))*v.stretch;
    const double f=c[2];
    if(c[0]*c[0]+c[1]*c[1]+f*f<=r*r*1.0001){b.full=true;return;}
    if(f+r<=v.minForward)return; // entirely behind every shaded point
    // Any ball reaching the near slab keeps the full target (no clipped edges).
    if(f-r<v.minForward){b.full=true;return;}
    // Tangent planes through the eye bound x/forward over the whole ball.
    double lo[2],hi[2];const double den=f*f-r*r;
    for(int axis=0;axis<2;++axis){const double s=std::sqrt(c[axis]*c[axis]+den);
        lo[axis]=(c[axis]*f-r*s)/den;hi[axis]=(c[axis]*f+r*s)/den;}
    const double x0=v.projX*lo[0],x1=v.projX*hi[0],y0=v.projY*lo[1],y1=v.projY*hi[1];
    if(!std::isfinite(x0)||!std::isfinite(x1)||!std::isfinite(y0)||!std::isfinite(y1)){b.full=true;return;}
    b.x0=std::min(b.x0,std::min(x0,x1));b.x1=std::max(b.x1,std::max(x0,x1));
    b.y0=std::min(b.y0,std::min(y0,y1));b.y1=std::max(b.y1,std::max(y0,y1));
}
// Half-res pixel rect (right/bottom exclusive). Empty means no pixel of the
// batch can be non-zero; full is the whole target.
inline Rect rect(const View& v,const Bounds& b){
    // Unknown geometry never clips: an invalid view is the whole target.
    Rect r;const Rect all{0,0,long(v.halfW),long(v.halfH)};
    if(b.full||!v.valid)return all;
    if(b.empty())return r;
    // Pixel px samples full-res ix=floor((px+.5)*w/W2), i.e.
    // px in [ix*W2/w-.5, (ix+1)*W2/w-.5]; ndc.x=2ix/w-1, ndc.y=1-2iy/h.
    const auto clampNdc=[](double x){return std::max(-4.,std::min(4.,x));};
    const double ix0=(clampNdc(b.x0)+1)*.5*v.fullW,ix1=(clampNdc(b.x1)+1)*.5*v.fullW;
    const double iy0=(1-clampNdc(b.y1))*.5*v.fullH,iy1=(1-clampNdc(b.y0))*.5*v.fullH;
    const double sx=double(v.halfW)/v.fullW,sy=double(v.halfH)/v.fullH;
    r.left=std::max(0L,long(std::floor(ix0*sx-.5))-Margin);
    r.right=std::min(long(v.halfW),long(std::ceil((ix1+1)*sx-.5))+1+Margin);
    r.top=std::max(0L,long(std::floor(iy0*sy-.5))-Margin);
    r.bottom=std::min(long(v.halfH),long(std::ceil((iy1+1)*sy-.5))+1+Margin);
    if(r.left>=r.right||r.top>=r.bottom)return Rect{};
    return r;
}
template<std::size_t N> Rect batchRect(const View& v,const std::array<std::array<float,4>,N>& position,unsigned count){
    Bounds b;for(unsigned i=0;i<count&&i<N&&!b.full;++i)add(v,position[i].data(),b);
    return rect(v,b);
}
// Direct batches contain two consecutive fog batches. Project each sphere once,
// then merge in NDC before rounding, preserving batchRect's exact rectangles.
template<unsigned Small,std::size_t N>
void pairedBatchRects(const View& v,const std::array<std::array<float,4>,N>& position,unsigned count,Rect* small,Rect* large){
    count=std::min<unsigned>(count,N);
    for(unsigned first=0;first<count;first+=2*Small){
        Bounds pair;
        for(unsigned half=0;half<2&&first+half*Small<count;++half){
            Bounds b;
            for(unsigned i=first+half*Small;i<count&&i<first+(half+1)*Small&&!b.full;++i)add(v,position[i].data(),b);
            small[first/Small+half]=rect(v,b);
            if(b.full)pair.full=true;
            else if(!pair.full&&!b.empty()){
                pair.x0=std::min(pair.x0,b.x0);pair.y0=std::min(pair.y0,b.y0);
                pair.x1=std::max(pair.x1,b.x1);pair.y1=std::max(pair.y1,b.y1);
            }
        }
        large[first/(2*Small)]=rect(v,pair);
    }
}
} // namespace NorthlightLocalLightScissor
