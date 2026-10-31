#pragma once
namespace FrozenHeight{using namespace NorthlightMaterialHeight;
inline bool read(IDirect3DTexture9* source,Image& out,bool statistics=true,bool accents=true,bool checkUploaded=true){
    out={};if(!source)return false;NorthlightParticleTextures::View uploaded;
    if(checkUploaded&&NorthlightMaterialTextures::cache().lookup(source,uploaded)){out.width=uploaded.width;out.height=uploaded.height;out.rgba.assign(uploaded.rgba,uploaded.rgba+size_t(uploaded.width)*uploaded.height*4);if(statistics)means(out,accents);return true;}
    D3DSURFACE_DESC desc{};UINT level=0;const UINT levels=source->GetLevelCount();
    for(;level<levels;++level){if(FAILED(source->GetLevelDesc(level,&desc)))return false;if(desc.Width<=128&&desc.Height<=128)break;}
    if(level==levels||!desc.Width||!desc.Height||(desc.Usage&(D3DUSAGE_RENDERTARGET|D3DUSAGE_DEPTHSTENCIL|D3DUSAGE_DYNAMIC)))return false;
    using F=NorthlightActorTexture::Format;F format;
    switch(desc.Format){case D3DFMT_A8R8G8B8:format=F::BGRA8;break;case D3DFMT_X8R8G8B8:format=F::BGRX8;break;case D3DFMT_DXT1:format=F::BC1;break;case D3DFMT_DXT3:format=F::BC2;break;case D3DFMT_DXT5:format=F::BC3;break;case D3DFMT_R5G6B5:format=F::RGB565;break;default:return false;}
    const auto pitch=NorthlightActorTexture::rowBytes(desc.Width,format);const auto rows=NorthlightActorTexture::rowCount(desc.Height,format);std::vector<unsigned char> bytes(pitch*rows);
    D3DLOCKED_RECT lock{};if(FAILED(source->LockRect(level,&lock,nullptr,D3DLOCK_READONLY)))return false;
    const bool valid=lock.pBits&&lock.Pitch>=INT(pitch);if(valid)for(unsigned i=0;i<rows;++i)std::memcpy(bytes.data()+i*pitch,static_cast<unsigned char*>(lock.pBits)+size_t(i)*lock.Pitch,pitch);const auto hr=source->UnlockRect(level);
    if(!valid||FAILED(hr)||!NorthlightActorTexture::decode(bytes.data(),bytes.size(),desc.Width,desc.Height,pitch,format,out.rgba))return false;
    out.width=desc.Width;out.height=desc.Height;if(statistics)means(out,accents);return true;
}
inline std::array<float,4> textureProfile(IDirect3DTexture9* source,unsigned polarity,unsigned details,unsigned balance){
    Image image;NorthlightParticleTextures::View uploaded;
    const bool accents=polarity==0&&details!=0;
    if(NorthlightMaterialTextures::cache().lookup(source,uploaded)){image.width=uploaded.width;image.height=uploaded.height;means(image,uploaded.rgba,size_t(uploaded.width)*uploaded.height*4,accents);}
    else FrozenHeight::read(source,image,true,accents);
    return profile(image,polarity,details,balance);
}
}
