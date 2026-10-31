#pragma once
#include "actor_texture.h"
#include "particle_texture_stats.h"
#include <array>
#include <algorithm>
#include <cmath>

// One inferred height convention for the CPU mesh and GPU normal/POM maps.
namespace NorthlightMaterialHeight {
inline float sat(float x){return std::clamp(x,0.f,1.f);}
inline float accent(float r,float g,float b){return std::max(sat((r-g)/std::max(r,.05f)*4-.1f)*sat((g-b)/std::max(g,.05f)*4-.1f),sat((g-r)/std::max(g,.05f)*4-.1f)*sat((g-b)/std::max(g,.05f)*4-.1f));}
struct Image {unsigned width=0,height=0;std::vector<unsigned char> rgba;float mean=.5f,accentMean=0;};
inline std::uint64_t version(IDirect3DTexture9* source){return NorthlightMaterialTextures::cache().version(source);}
inline void copiedTexture(IDirect3DBaseTexture9* source,IDirect3DBaseTexture9* destination,bool success){
    if(!destination||destination->GetType()!=D3DRTYPE_TEXTURE)return;auto& cache=NorthlightMaterialTextures::cache();
    if(!success||!source||source->GetType()!=D3DRTYPE_TEXTURE){cache.invalidate(destination);return;}
    D3DSURFACE_DESC a{},b{};auto* s=static_cast<IDirect3DTexture9*>(source);auto* t=static_cast<IDirect3DTexture9*>(destination);
    if(FAILED(s->GetLevelDesc(0,&a))||FAILED(t->GetLevelDesc(0,&b))||a.Width!=b.Width||a.Height!=b.Height||a.Format!=b.Format||(b.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL))){cache.invalidate(destination);return;}
    cache.copy(source,destination,nullptr);
}
inline void copiedSurface(IDirect3DSurface9* source,const RECT* rect,IDirect3DSurface9* destination,const POINT* point,bool success){
    if(!destination)return;IDirect3DTexture9* from=nullptr,*to=nullptr;
    if(FAILED(destination->GetContainer(IID_IDirect3DTexture9,reinterpret_cast<void**>(&to)))||!to)return;
    auto& cache=NorthlightMaterialTextures::cache();
    D3DSURFACE_DESC a{},b{},level{};
    if(success&&source&&(!point||(!point->x&&!point->y))&&SUCCEEDED(source->GetDesc(&a))&&SUCCEEDED(destination->GetDesc(&b))&&a.Width==b.Width&&a.Height==b.Height&&(!rect||(!rect->left&&!rect->top&&rect->right==LONG(a.Width)&&rect->bottom==LONG(a.Height)))&&SUCCEEDED(source->GetContainer(IID_IDirect3DTexture9,reinterpret_cast<void**>(&from)))&&from&&SUCCEEDED(from->GetLevelDesc(0,&level))&&a.Width==level.Width&&a.Height==level.Height&&SUCCEEDED(to->GetLevelDesc(0,&level))&&b.Width==level.Width&&b.Height==level.Height)copiedTexture(from,to,true);else cache.invalidate(to);
    if(from)from->Release();to->Release();
}
inline void means(Image& out,const unsigned char* rgba,size_t bytes,bool accents=true){out.mean=out.accentMean=0;for(size_t i=0;i<bytes;i+=4){float r=rgba[i]/255.f,g=rgba[i+1]/255.f,b=rgba[i+2]/255.f;out.mean+=r*.299f+g*.587f+b*.114f;if(accents)out.accentMean+=accent(r,g,b);}out.mean/=out.width*out.height;out.accentMean/=out.width*out.height;}
inline void means(Image& out,bool accents=true){means(out,out.rgba.data(),out.rgba.size(),accents);}
inline bool read(IDirect3DTexture9* source,Image& out,bool statistics=true,bool accents=true,bool checkUploaded=true){
    out={};if(!source)return false;NorthlightParticleTextures::View uploaded;
    if(checkUploaded&&NorthlightMaterialTextures::cache().lookup(source,uploaded)){out.width=uploaded.width;out.height=uploaded.height;out.rgba.assign(uploaded.rgba,uploaded.rgba+size_t(uploaded.width)*uploaded.height*4);if(statistics)means(out,accents);return true;}
    D3DSURFACE_DESC desc{};UINT level=0;const UINT levels=source->GetLevelCount();
    for(;level<levels;++level){if(FAILED(source->GetLevelDesc(level,&desc)))return false;if(desc.Width<=128&&desc.Height<=128)break;}
    if(level==levels||!desc.Width||!desc.Height||(desc.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL|D3DUSAGE_DYNAMIC)))return false;
    using F=NorthlightActorTexture::Format;F format;
    switch(desc.Format){case D3DFMT_A8R8G8B8:format=F::BGRA8;break;case D3DFMT_X8R8G8B8:format=F::BGRX8;break;case D3DFMT_DXT1:format=F::BC1;break;case D3DFMT_DXT3:format=F::BC2;break;case D3DFMT_DXT5:format=F::BC3;break;case D3DFMT_R5G6B5:format=F::RGB565;break;default:return false;}
    const auto pitch=NorthlightActorTexture::rowBytes(desc.Width,format);const auto rows=NorthlightActorTexture::rowCount(desc.Height,format);
    D3DLOCKED_RECT lock{};if(FAILED(source->LockRect(level,&lock,nullptr,D3DLOCK_READONLY)))return false;
    bool decoded=false;
    try{
        if(lock.pBits&&lock.Pitch>=INT(pitch)&&size_t(lock.Pitch)<=SIZE_MAX/rows)
            decoded=NorthlightActorTexture::decode(lock.pBits,size_t(lock.Pitch)*rows,desc.Width,desc.Height,size_t(lock.Pitch),format,out.rgba);
    }catch(...){source->UnlockRect(level);throw;}
    const auto hr=source->UnlockRect(level);
    if(!decoded||FAILED(hr)){out.rgba.clear();return false;}
    out.width=desc.Width;out.height=desc.Height;if(statistics)means(out,accents);return true;
}
inline std::array<float,4> profile(const Image& im,unsigned polarity,unsigned details,unsigned balance){return {im.width?im.mean:.5f,polarity?0.f:details*.01f*sat((.45f-im.accentMean)*4),polarity==2?-1.f:1.f,(float(balance)-50)*.005f};}
inline void uploadedMeans(Image& image,const NorthlightParticleTextures::View& view,bool accents){
    struct Statistics {const void* source=nullptr;uint64_t lifetime=0,revision=0;float mean=0,accent=0;bool accents=false;};
    static thread_local std::array<Statistics,64> cache{};
    auto& entry=cache[view.lifetime%cache.size()];
    if(entry.source!=view.rgba||entry.lifetime!=view.lifetime||entry.revision!=view.revision||(accents&&!entry.accents)){
        means(image,view.rgba,size_t(view.width)*view.height*4,accents);
        entry={view.rgba,view.lifetime,view.revision,image.mean,image.accentMean,accents};
    }else{image.mean=entry.mean;image.accentMean=accents?entry.accent:0;}
}
inline std::array<float,4> textureProfile(IDirect3DTexture9* source,unsigned polarity,unsigned details,unsigned balance){
    Image image;NorthlightParticleTextures::View uploaded;
    const bool accents=polarity==0&&details!=0;
    if(NorthlightMaterialTextures::cache().lookup(source,uploaded)){image.width=uploaded.width;image.height=uploaded.height;uploadedMeans(image,uploaded,accents);}
    else read(source,image,true,accents);
    return profile(image,polarity,details,balance);
}
inline float value(float r,float g,float b,const std::array<float,4>& p){
    const float base=.5f+(r*.299f+g*.587f+b*.114f-p[0])*.7f*p[2];
    const float a=accent(r,g,b)*p[1];return std::clamp(std::max(base,base+(.5f+.35f*a-base)*a)+p[3],.02f,.98f);
}
inline float valueWithoutAccent(float r,float g,float b,const std::array<float,4>& p){
    const float base=.5f+(r*.299f+g*.587f+b*.114f-p[0])*.7f*p[2];
    return std::clamp(base+p[3],.02f,.98f);
}
// Model albedo contains painted anatomy and skin colour. Only local detail may
// become model relief; broad lighting/colour regions must not inflate a body.
inline float modelValue(const Image& im,unsigned x,unsigned y,unsigned polarity,const unsigned char* rgba=nullptr){
    if(!rgba)rgba=im.rgba.data();
    auto luma=[&](int x,int y){x=std::clamp(x,0,int(im.width)-1);y=std::clamp(y,0,int(im.height)-1);const auto* p=rgba+(size_t(y)*im.width+x)*4;return (p[0]*.299f+p[1]*.587f+p[2]*.114f)/255;};
    const int rx=std::max(1u,im.width/32),ry=std::max(1u,im.height/32);
    const float around=(luma(int(x)-rx,y)+luma(int(x)+rx,y)+luma(x,int(y)-ry)+luma(x,int(y)+ry))*.25f;
    return .5f+std::clamp((luma(x,y)-around)*.7f*(polarity==2?-1.f:1.f),-.12f,.12f);
}
inline void modelValues(const Image& im,unsigned polarity,std::vector<float>& out,const unsigned char* rgba=nullptr){
    if(!rgba)rgba=im.rgba.data();
    out.resize(size_t(im.width)*im.height);if(out.empty())return;
    const unsigned rx=std::max(1u,im.width/32),ry=std::max(1u,im.height/32);
    std::vector<float> rows;
    try{rows.resize(size_t(ry+1)*im.width);}catch(...){
        for(unsigned y=0;y<im.height;++y)for(unsigned x=0;x<im.width;++x)out[size_t(y)*im.width+x]=modelValue(im,x,y,polarity,rgba);return;}
    // Decode each texel once. Retain overwritten luminance rows in a small
    // ring; below rows are still unmodified in the destination height field.
    for(size_t i=0;i<out.size();++i){const auto* p=rgba+i*4;out[i]=(p[0]*.299f+p[1]*.587f+p[2]*.114f)/255;}
    for(unsigned y=0;y<im.height;++y){
        auto* center=rows.data()+size_t(y%(ry+1))*im.width;
        std::copy_n(out.data()+size_t(y)*im.width,im.width,center);
        const auto* above=rows.data()+size_t((y>ry?y-ry:0)%(ry+1))*im.width;
        const auto* below=out.data()+size_t(std::min(y+ry,im.height-1))*im.width;
        for(unsigned x=0;x<im.width;++x){
            const float around=(center[x>rx?x-rx:0]+center[std::min(x+rx,im.width-1)]+above[x]+below[x])*.25f;
            out[size_t(y)*im.width+x]=.5f+std::clamp((center[x]-around)*.7f*(polarity==2?-1.f:1.f),-.12f,.12f);}}
}
}
