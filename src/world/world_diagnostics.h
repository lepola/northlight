#pragma once
#include "utf8_path.h"
// Explicit F12 shadow view only. Read actual GPU products without changing the
// lighting state; errors are diagnostic failures, never renderer-disable events.
namespace NorthlightWorldDiagnostics {
// Paired camera captures need the original cached projection too: its depth
// origin and XY margin can differ from the current working shadow map.
inline bool shadowFrame(const std::string& directory,unsigned capture,int source,int cascade,
                        const float* cached,const float* working,long x,long y,float dz,
                        uint64_t geometry,uint64_t signature,const char* reason){
    char suffix[128];std::snprintf(suffix,sizeof suffix,"/capture-%u-s%d-c%d-frame.json",capture,source,cascade);
    FILE* f=NorthlightFiles::open((directory+suffix).c_str(),"wb");if(!f)return false;
    std::fprintf(f,"{\"source\":%d,\"cascade\":%d,\"geometry\":%llu,\"signature\":\"%llu\",\"reason\":\"%s\",\"offset\":[%ld,%ld,%.9g],\"cached\":[",source,cascade,(unsigned long long)geometry,(unsigned long long)signature,reason?reason:"reuse",x,y,dz);
    for(unsigned i=0;i<16;++i)std::fprintf(f,"%s%.9g",i?",":"",cached[i]);
    std::fprintf(f,"],\"upstream_retained\":true,\"working\":[");for(unsigned i=0;i<16;++i)std::fprintf(f,"%s%.9g",i?",":"",working[i]);
    std::fprintf(f,"]}\n");const bool ok=std::ferror(f)==0;return std::fclose(f)==0&&ok;
}
inline bool dump(IDirect3DDevice9* device,IDirect3DSurface9* source,
                 const std::string& directory,const char* name,unsigned capture){
    if(!source)return false;
    D3DSURFACE_DESC desc={};HRESULT hr=source->GetDesc(&desc);
    unsigned bytes=desc.Format==D3DFMT_A16B16G16R16F?8:
        (desc.Format==D3DFMT_R32F||desc.Format==D3DFMT_A8R8G8B8||desc.Format==D3DFMT_X8R8G8B8?4:0);
    if(FAILED(hr)||!bytes||!desc.Width||!desc.Height||uint64_t(desc.Width)*desc.Height*bytes>64*1024*1024)return false;
    IDirect3DSurface9* staging=nullptr;
    hr=device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&staging,nullptr);
    if(SUCCEEDED(hr))hr=device->GetRenderTargetData(source,staging);
    D3DLOCKED_RECT lock={};bool locked=false;
    if(SUCCEEDED(hr)){hr=staging->LockRect(&lock,nullptr,D3DLOCK_READONLY);locked=SUCCEEDED(hr);}
    bool ok=false;
    if(locked&&lock.pBits&&lock.Pitch>=INT(desc.Width*bytes)){
        char suffix[128];std::snprintf(suffix,sizeof suffix,"/capture-%u-%s.fgr",capture,name);
        FILE* f=NorthlightFiles::open((directory+suffix).c_str(),"wb");
        if(f){uint32_t header[]={0x31524746,desc.Width,desc.Height,unsigned(desc.Format),bytes};
            ok=std::fwrite(header,sizeof header,1,f)==1;
            for(unsigned y=0;y<desc.Height&&ok;++y)
                ok=std::fwrite(static_cast<const char*>(lock.pBits)+y*lock.Pitch,bytes,desc.Width,f)==desc.Width;
            if(std::fclose(f))ok=false;
        }
    }
    if(locked)staging->UnlockRect();if(staging)staging->Release();
    logf("WORLD GPU diagnostic capture=%u buffer=%s size=%ux%u format=%u saved=%d HRESULT=%08lx",capture,name,desc.Width,desc.Height,unsigned(desc.Format),ok,(unsigned long)hr);
    return ok;
}
}
