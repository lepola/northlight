#pragma once
// Portable disc basis. World direction is copied from the audited native sky
// orbit; this module never invents a clock, orbit or below-horizon source.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <vector>
#include <string>
namespace NorthlightCelestialDisc {
// Window-space depth for an XYZRHW sky sprite. The world reserves the lower
// depth band; never let a reset/full-range sky viewport put a disc inside it.
// Keep the existing sky-band placement ahead of the outer sky dome.
inline bool skyDepth(float skyMin,float skyMax,float worldMax,float& output){
    if(!std::isfinite(skyMin)||!std::isfinite(skyMax)||!std::isfinite(worldMax)||
       skyMin<0||skyMax>1||skyMin>=skyMax||worldMax<0||worldMax>=skyMax)return false;
    const float lower=std::max(skyMin,worldMax);
    const float depth=lower+(skyMax-lower)*.92f; // skyMin/skyMax are the client's own sky viewport (0.9990234375..1), so this lands at ~0.99992, inside the sky band
    if(depth<=lower||depth>=skyMax)return false;
    output=depth;return true;
}
struct Texture {unsigned width=0,height=0;std::vector<std::uint8_t> rgba;};
inline bool loadTexture(const std::string& path,Texture& out){
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return false;auto length=f.tellg();if(length<16)return false;f.seekg(0);
    std::uint32_t h[4];f.read(reinterpret_cast<char*>(h),16);
    if(!f||h[0]!=0x31544346||h[1]!=1||!h[2]||!h[3]||h[2]>512||h[3]>512||std::uint64_t(length)!=16+std::uint64_t(h[2])*h[3]*4)return false;
    Texture t;t.width=h[2];t.height=h[3];t.rgba.resize(t.width*t.height*4);f.read(reinterpret_cast<char*>(t.rgba.data()),t.rgba.size());if(!f)return false;out=std::move(t);return true;
}
// The next mip level (half size, at least 1). Colour is averaged by alpha, so the transparent black exterior
// never darkens the rim of a smaller level; alpha is the plain average.
inline Texture halve(const Texture& texture){
    Texture out;out.width=std::max(1u,texture.width/2);out.height=std::max(1u,texture.height/2);out.rgba.resize(std::size_t(out.width)*out.height*4);
    for(unsigned y=0;y<out.height;++y)for(unsigned x=0;x<out.width;++x){
        unsigned sum[3]={},alpha=0,count=0;
        for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){
            const unsigned sx=std::min(texture.width-1,x*2+dx),sy=std::min(texture.height-1,y*2+dy);
            const auto* p=texture.rgba.data()+(std::size_t(sy)*texture.width+sx)*4;
            for(unsigned c=0;c<3;++c)sum[c]+=p[c]*p[3];alpha+=p[3];++count;
        }
        auto* q=out.rgba.data()+(std::size_t(y)*out.width+x)*4;
        for(unsigned c=0;c<3;++c)q[c]=alpha?std::uint8_t((sum[c]+alpha/2)/alpha):0;
        q[3]=std::uint8_t((alpha+count/2)/count);
    }
    return out;
}
// Texture LOD for a disc of `radiusPixels` on screen: the texture spans the diameter.
inline float discLod(unsigned textureWidth,float radiusPixels){
    if(!(radiusPixels>0)||!textureWidth)return 0;
    return std::max(0.f,std::log2(float(textureWidth)/(2*radiusPixels)));
}
// Inward 6-texel feather of the authored lunar silhouette. Preserve RGB and
// existing partial coverage; transparent exterior texels never gain opacity.
inline void softenMoonRim(Texture& texture){
    const auto original=texture.rgba;
    const int radius=7;
    for(int y=0;y<int(texture.height);++y)for(int x=0;x<int(texture.width);++x){
        const auto index=(std::size_t(y)*texture.width+x)*4+3;
        if(!original[index])continue;
        float distance2=float(radius*radius);
        for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx){
            const int px=x+dx,py=y+dy;
            if(px<0||py<0||px>=int(texture.width)||py>=int(texture.height)||
               original[(std::size_t(py)*texture.width+px)*4+3]==0)
                distance2=std::min(distance2,float(dx*dx+dy*dy));
        }
        float coverage=std::min(1.f,std::max(0.f,(std::sqrt(distance2)-.5f)/6.f));
        coverage=coverage*coverage*(3-2*coverage);
        texture.rgba[index]=std::uint8_t(std::lround(original[index]*coverage));
    }
}
// Native sky viewports may reserve a much narrower depth band than the world.
// Retain that measured band even if the warped body was outside the screen;
// fallback and halo must use the same raw-depth test as an early sky sprite.
struct SkyBand {
    float low=0,high=1;bool valid=false;
    bool observe(float minZ,float maxZ,float worldMax){
        float depth;if(!skyDepth(minZ,maxZ,worldMax,depth))return false;
        low=minZ;high=maxZ;valid=true;return true;
    }
    float depth(float worldMax)const{
        float value;
        if(valid&&skyDepth(low,high,worldMax,value))return value;
        // Unknown sky band: every nearer sky surface remains an occluder.
        return skyDepth(worldMax,1,worldMax,value)?std::nextafter(1.f,0.f):-1.f;
    }
};
struct Disc {float direction[3],right[3],up[3],tint[3],tangentRadius,opacity;};
// Late color repair repeats the early disc depth test against resolved depth.
// Foreground world and nearer sky geometry must remain occluders; a later
// sky pass clearing/replacing farther depth must not leave holes in the disc.
constexpr float RepairDepthTolerance=1.5f/16777215.f;
inline bool repairVisible(float rawDepth,float discDepth){
    return std::isfinite(rawDepth)&&std::isfinite(discDepth)&&
        rawDepth>=discDepth-RepairDepthTolerance;
}
struct Placement {
    Disc disc{};float projection[3]={},inverseView[16]={},depth=0,emission=0;
    unsigned width=0,height=0;bool valid=false;
};
inline bool prepare(const float* direction,const float* tint,float alpha,float angularRadius,Disc& output,float horizonSine=0) {
    if(!direction||!tint||!std::isfinite(alpha)||alpha<=0||alpha>1||
       !std::isfinite(angularRadius)||angularRadius<.000001f||angularRadius>.5f)return false;
    Disc d={};float length2=0;
    for(int i=0;i<3;++i){
        if(!std::isfinite(direction[i])||!std::isfinite(tint[i])||tint[i]<0||tint[i]>16)return false;
        length2+=direction[i]*direction[i];d.tint[i]=tint[i];
    }
    if(length2<.99f||length2>1.01f)return false;
    for(int i=0;i<3;++i)d.direction[i]=direction[i]/std::sqrt(length2);
    // The shader retains the native per-pixel horizon fade. Stop submitting the
    // disc and its glare as soon as the top limb has completely set.
    if(!std::isfinite(horizonSine))horizonSine=0;
    horizonSine=std::clamp(horizonSine,-1.f,0.f);
    const float limb=horizonSine*std::cos(angularRadius)-std::sqrt(1-horizonSine*horizonSine)*std::sin(angularRadius);
    if(d.direction[2]<=limb)return false;
    // Continuous tangent basis across the visible upper hemisphere, including
    // zenith. No arbitrary axis-switch jump rotates the moon near the pole.
    // Native billboard roll is not inferred by the exact center/size proof.
    const float a=1/(1+d.direction[2]),b=-d.direction[0]*d.direction[1]*a;
    d.right[0]=1-d.direction[0]*d.direction[0]*a;d.right[1]=b;d.right[2]=-d.direction[0];
    d.up[0]=b;d.up[1]=1-d.direction[1]*d.direction[1]*a;d.up[2]=-d.direction[1];
    d.opacity=alpha;d.tangentRadius=std::tan(angularRadius);output=d;return true;
}
// Conservative full-resolution scissor [left,top,right,bottom], with two-pixel
// padding. Projects the four actual billboard-plane corners; if the plane
// straddles the camera, retain the full viewport rather than clipping wrongly.
inline bool screenBounds(const Disc& d,const float* inverseView,float projectionX,float projectionY,float zSign,
                         unsigned width,unsigned height,int* bounds){
    if(!inverseView||!bounds||!width||!height||width>32768||height>32768||
       !std::isfinite(projectionX)||!std::isfinite(projectionY)||std::fabs(projectionX)<.00001f||std::fabs(projectionY)<.00001f||
       (zSign!=1&&zSign!=-1))return false;
    for(int i=0;i<12;++i)if(!std::isfinite(inverseView[i]))return false;
    double minX=1e30,minY=1e30,maxX=-1e30,maxY=-1e30;unsigned front=0;
    for(int u=-1;u<=1;u+=2)for(int v=-1;v<=1;v+=2){
        double ray[3],view[3]={};for(int i=0;i<3;++i)ray[i]=d.direction[i]+d.tangentRadius*(u*d.right[i]+v*d.up[i]);
        for(int row=0;row<3;++row)for(int i=0;i<3;++i)view[row]+=ray[i]*inverseView[row*4+i];
        double forward=view[2]*zSign;if(forward<=.000001)continue;++front;
        double x=(view[0]*projectionX/forward*.5+.5)*width;
        double y=(.5-view[1]*projectionY/forward*.5)*height;
        minX=std::min(minX,x);maxX=std::max(maxX,x);minY=std::min(minY,y);maxY=std::max(maxY,y);
    }
    if(!front)return false;
    if(front<4){bounds[0]=bounds[1]=0;bounds[2]=int(width);bounds[3]=int(height);return true;}
    bounds[0]=int(std::max(0.,std::min(double(width),std::floor(minX)-2)));
    bounds[1]=int(std::max(0.,std::min(double(height),std::floor(minY)-2)));
    bounds[2]=int(std::max(0.,std::min(double(width),std::ceil(maxX)+2)));
    bounds[3]=int(std::max(0.,std::min(double(height),std::ceil(maxY)+2)));
    return bounds[0]<bounds[2]&&bounds[1]<bounds[3];
}
} // namespace NorthlightCelestialDisc
