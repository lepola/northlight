#pragma once
// SPDX-License-Identifier: MIT
// Explicit F12 only: compare uploaded tile hashes, raw managed BC and actual
// point-sampled GPU pixels. Errors restore state and never disable the renderer.
#include "atlas_upload_proof.h"
#include "actor_texture.h"
#include "bc_decode_comparison.h"
#include "saved_state.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace NorthlightAtlasDiagnostic {
struct Result {unsigned captured=0,failed=0,knownTiles=0,rawMismatches=0,pixelMismatches=0;};
struct Surfaces {IDirect3DSurface9* target=nullptr;IDirect3DSurface9* staging=nullptr;~Surfaces(){if(target)target->Release();if(staging)staging->Release();}};
inline bool save(const std::string& path,const void* bytes,std::size_t size){FILE* f=std::fopen(path.c_str(),"wb");if(!f)return false;const bool ok=std::fwrite(bytes,1,size,f)==size;return std::fclose(f)==0&&ok;}
inline bool capture(IDirect3DDevice9* d,const NorthlightAtlasProof::Entry& entry,const std::string& prefix,Result& result){
    using namespace NorthlightAtlasProof;
    auto* texture=static_cast<IDirect3DTexture9*>(const_cast<void*>(entry.raw));D3DSURFACE_DESC desc{};
    if(!texture||FAILED(texture->GetLevelDesc(0,&desc))||desc.Width!=Size||desc.Height!=Size||unsigned(desc.Format)!=entry.format||desc.Pool!=D3DPOOL_MANAGED)return false;
    const unsigned block=blockBytes(entry.format),pitch=128*block;
    std::vector<unsigned char> raw(std::size_t(pitch)*128),pixels(Size*Size*4);
    D3DLOCKED_RECT lock{};
    if(FAILED(texture->LockRect(0,&lock,nullptr,D3DLOCK_READONLY)))return false;
    const bool readable=lock.pBits&&lock.Pitch>=INT(pitch);
    if(readable)for(unsigned y=0;y<128;++y)std::memcpy(raw.data()+y*pitch,static_cast<unsigned char*>(lock.pBits)+std::size_t(y)*lock.Pitch,pitch);
    const HRESULT unlocked=texture->UnlockRect(0);if(!readable||FAILED(unlocked))return false;
    SavedState state(d);if(!state.ok)return false;
    Surfaces surfaces;
    if(FAILED(d->CreateRenderTarget(Size,Size,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&surfaces.target,nullptr))||
       FAILED(d->CreateOffscreenPlainSurface(Size,Size,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&surfaces.staging,nullptr)))return false;
    if(FAILED(d->SetDepthStencilSurface(nullptr)))return false;
    for(unsigned n=1;n<4;++n)d->SetRenderTarget(n,nullptr);
    if(FAILED(d->SetRenderTarget(0,surfaces.target)))return false;
    D3DVIEWPORT9 viewport{0,0,Size,Size,0,1};if(FAILED(d->SetViewport(&viewport)))return false;
    if(FAILED(d->SetVertexShader(nullptr))||FAILED(d->SetPixelShader(nullptr))||FAILED(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1)))return false;
    if(FAILED(d->SetStreamSourceFreq(0,1)))return false;
    for(auto pair:{std::pair<D3DRENDERSTATETYPE,DWORD>{D3DRS_ZENABLE,FALSE},{D3DRS_ZWRITEENABLE,FALSE},{D3DRS_ALPHABLENDENABLE,FALSE},{D3DRS_ALPHATESTENABLE,FALSE},{D3DRS_LIGHTING,FALSE},{D3DRS_CULLMODE,D3DCULL_NONE},{D3DRS_SCISSORTESTENABLE,FALSE},{D3DRS_STENCILENABLE,FALSE},{D3DRS_COLORWRITEENABLE,15},{D3DRS_SRGBWRITEENABLE,FALSE},{D3DRS_FOGENABLE,FALSE},{D3DRS_CLIPPLANEENABLE,0}})
        if(FAILED(d->SetRenderState(pair.first,pair.second)))return false;
    if(FAILED(d->SetTexture(0,texture)))return false;
    for(auto pair:{std::pair<D3DTEXTURESTAGESTATETYPE,DWORD>{D3DTSS_COLOROP,D3DTOP_SELECTARG1},{D3DTSS_COLORARG1,D3DTA_TEXTURE},{D3DTSS_ALPHAOP,D3DTOP_SELECTARG1},{D3DTSS_ALPHAARG1,D3DTA_TEXTURE},{D3DTSS_TEXCOORDINDEX,0},{D3DTSS_TEXTURETRANSFORMFLAGS,D3DTTFF_DISABLE}})
        if(FAILED(d->SetTextureStageState(0,pair.first,pair.second)))return false;
    if(FAILED(d->SetTextureStageState(1,D3DTSS_COLOROP,D3DTOP_DISABLE)))return false;
    for(auto pair:{std::pair<D3DSAMPLERSTATETYPE,DWORD>{D3DSAMP_MINFILTER,D3DTEXF_POINT},{D3DSAMP_MAGFILTER,D3DTEXF_POINT},{D3DSAMP_MIPFILTER,D3DTEXF_NONE},{D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP},{D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP},{D3DSAMP_SRGBTEXTURE,FALSE},{D3DSAMP_MAXMIPLEVEL,0},{D3DSAMP_MIPMAPLODBIAS,0}})
        if(FAILED(d->SetSamplerState(0,pair.first,pair.second)))return false;
    struct V {float x,y,z,rhw,u,v;};const V vertices[]={{-.5f,-.5f,0,1,0,0},{Size-.5f,-.5f,0,1,1,0},{-.5f,Size-.5f,0,1,0,1},{Size-.5f,Size-.5f,0,1,1,1}};
    if(FAILED(d->BeginScene()))return false;const HRESULT drawn=d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,vertices,sizeof(V)),ended=d->EndScene();
    if(FAILED(drawn)||FAILED(ended)||FAILED(d->GetRenderTargetData(surfaces.target,surfaces.staging)))return false;
    lock={};if(FAILED(surfaces.staging->LockRect(&lock,nullptr,D3DLOCK_READONLY)))return false;
    const bool pixelsReadable=lock.pBits&&lock.Pitch>=INT(Size*4);
    if(pixelsReadable)for(unsigned y=0;y<Size;++y)std::memcpy(pixels.data()+y*Size*4,static_cast<unsigned char*>(lock.pBits)+std::size_t(y)*lock.Pitch,Size*4);
    const HRESULT pixelUnlock=surfaces.staging->UnlockRect();if(!pixelsReadable||FAILED(pixelUnlock))return false;
    std::vector<unsigned char> tile(16*16*block),rgba(Tile*Tile*4);
    FILE* report=std::fopen((prefix+".json").c_str(),"wb");if(!report)return false;
    std::fprintf(report,"{\"format\":%u,\"size\":512,\"levels\":%u,\"tile\":64,\"revision\":%llu,\"raw\":\"%p\",\"uploads\":%u,\"failed_uploads\":%u,\"last_failure_hresult\":\"%08x\",\"invalidations\":%u,\"vs_hash\":\"%016llx\",\"ps_hash\":\"%016llx\",\"draw_sampler_known_mask\":%u,\"draw_samplers\":[",entry.format,unsigned(texture->GetLevelCount()),(unsigned long long)entry.revision,entry.raw,entry.uploads,entry.failedUploads,entry.lastFailure,entry.invalidations,(unsigned long long)entry.vsHash,(unsigned long long)entry.psHash,entry.samplerKnown);
    for(unsigned n=1;n<14;++n)std::fprintf(report,"%s%u",n>1?",":"",entry.samplers[n]);
    std::fprintf(report,"],\"tiles\":[");
    const auto format=block==8?NorthlightActorTexture::Format::BC1:entry.format==0x33545844?NorthlightActorTexture::Format::BC2:NorthlightActorTexture::Format::BC3;
    for(unsigned n=0;n<Tiles;++n){
        const unsigned tx=n%8*Tile,ty=n/8*Tile;
        for(unsigned y=0;y<16;++y)std::memcpy(tile.data()+y*16*block,raw.data()+std::size_t(ty/4+y)*pitch+tx/4*block,16*block);
        const auto hash=hashRows(tile.data(),16*block,16,16*block);const bool known=(entry.known>>n&1)!=0,match=known&&hash==entry.tiles[n];
        unsigned badPixels=0,differencePixels=0,maxError[4]{};
        if(NorthlightActorTexture::decode(tile.data(),tile.size(),Tile,Tile,16*block,format,rgba))for(unsigned y=0;y<Tile;++y)for(unsigned x=0;x<Tile;++x){
            const auto* actual=pixels.data()+(std::size_t(ty+y)*Size+tx+x)*4;
            bool different=false;for(unsigned c=0;c<4;++c){const unsigned error=unsigned(std::abs(int(actual[c<3?2-c:3])-int(rgba[(y*Tile+x)*4+c])));maxError[c]=std::max(maxError[c],error);different|=error>1;}differencePixels+=different;
            badPixels+=!NorthlightBcComparison::pixel(entry.format,tile.data()+((y/4)*16+x/4)*block,(y%4)*4+x%4,actual);}
        else badPixels=Tile*Tile;
        result.knownTiles+=known;result.rawMismatches+=known&&!match;result.pixelMismatches+=badPixels;
        std::fprintf(report,"%s{\"index\":%u,\"cpu_known\":%s,\"cpu_fnv1a64\":\"%016llx\",\"raw_fnv1a64\":\"%016llx\",\"source_matches_raw\":%s,\"raw_decode_vs_gpu_bad_pixels\":%u,\"raw_decode_vs_gpu_gt1_pixels\":%u,\"max_abs_rgba_error\":[%u,%u,%u,%u]}",n?",":"",n,known?"true":"false",(unsigned long long)entry.tiles[n],(unsigned long long)hash,match?"true":"false",badPixels,differencePixels,maxError[0],maxError[1],maxError[2],maxError[3]);
    }
    std::fprintf(report,"],\"pixel_comparison\":\"BC1/2/3 per-block D3D11.3 19.5.2 precision envelope plus half a UNORM8 render-target step; bad_pixels counts only outside this envelope. gt1_pixels and max_abs_rgba_error retain ideal CPU decoder differences.\",\"note\":\"Known source hashes prove submitted upload bytes; raw BC and independent point-sampled GPU rendering distinguish managed content from draw state/UV. Original sampler states are recorded, not used for this diagnostic render.\"}\n");
    const bool reportOk=std::fclose(report)==0;
    const bool rawOk=save(prefix+".bc",raw.data(),raw.size());
    const std::uint32_t header[]={0x31524746,Size,Size,unsigned(D3DFMT_A8R8G8B8),4};
    FILE* f=std::fopen((prefix+".fgr").c_str(),"wb");bool pixelsOk=false;if(f){pixelsOk=std::fwrite(header,sizeof header,1,f)==1&&std::fwrite(pixels.data(),1,pixels.size(),f)==pixels.size();if(std::fclose(f))pixelsOk=false;}
    return reportOk&&rawOk&&pixelsOk;
}
inline Result dump(IDirect3DDevice9* device,const std::string& prefix,unsigned frame)noexcept{
    Result result;
    try{
        std::array<NorthlightAtlasProof::Entry,4> entries{};const unsigned count=NorthlightAtlasProof::cache().recent(entries,frame);
        for(unsigned n=0;n<count;++n){if(capture(device,entries[n],prefix+"-"+std::to_string(n),result))++result.captured;else ++result.failed;}
    }catch(...){++result.failed;}
    return result;
}
}
