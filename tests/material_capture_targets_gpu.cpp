// Actual production flush, unchanged two-output shader, MRT bindings and near-only writes.
#include "material_target_gpu_support.h"
#include <chrono>
#include <numeric>
struct TargetGpuMeter:ForwardIDirect3DDevice9 {
    unsigned binds=0,writes=0;explicit TargetGpuMeter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD n,IDirect3DSurface9* value)override{binds+=n==1;return real->SetRenderTarget(n,value);}
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE key,DWORD value)override{writes+=key==D3DRS_COLORWRITEENABLE1;return real->SetRenderState(key,value);}
};
struct MaterialCaptureStateTestAccess {
    struct Result {std::vector<unsigned char> color,occlusion;double gpu=0,cpu=0;unsigned binds=0;};
    static std::vector<unsigned char> pixels(IDirect3DDevice9* d,IDirect3DSurface9* target,unsigned width,unsigned height,D3DFORMAT format,unsigned bpp){
        IDirect3DSurface9* staging=nullptr;ok(d->CreateOffscreenPlainSurface(width,height,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<unsigned char> data(size_t(width)*height*bpp);for(unsigned y=0;y<height;++y)std::memcpy(data.data()+size_t(y)*width*bpp,static_cast<char*>(lock.pBits)+size_t(y)*lock.Pitch,width*bpp);ok(staging->UnlockRect());drop(staging);return data;
    }
    static Result run(TargetGpuMeter& d,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,IDirect3DVertexDeclaration9* declaration,unsigned width,unsigned height,unsigned pattern,bool partial,bool fallback,bool timing){
        NorthlightAutoBump b(&d);b.width=width;b.heightPixels=height;
        D3DCAPS9 caps{};ok(d.GetDeviceCaps(&caps));assert(caps.PrimitiveMiscCaps&D3DPMISCCAPS_INDEPENDENTWRITEMASKS);b.independentCaptureWrites=!fallback;
        ok(d.CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A16B16G16R16F,D3DPOOL_DEFAULT,&b.height,nullptr));ok(b.height->GetSurfaceLevel(0,&b.target));
        ok(d.CreateTexture(width,height,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&b.occlusionMap,nullptr));ok(b.occlusionMap->GetSurfaceLevel(0,&b.occlusionTarget));
        ok(d.CreateDepthStencilSurface(width,height,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,FALSE,&b.captureDepth,nullptr));
        {SavedState restore(&d);assert(restore.ok);ok(d.SetDepthStencilSurface(nullptr));ok(d.SetRenderTarget(0,b.occlusionTarget));ok(d.Clear(0,nullptr,D3DCLEAR_TARGET,0xa0b0c0d0,1,0));}
        using Mesh=NorthlightDrawSnapshot::Mesh;std::vector<std::shared_ptr<const Mesh>> meshes;
        for(unsigned i=0;i<192;++i){auto mesh=std::make_shared<Mesh>();mesh->vertexCount=6;mesh->primitiveCount=2;mesh->streams[0].stride=16;
            const float x=-1.f+float(i%16)/8.f,y=-1.f+float(i/16)/6.f,r=x+.125f,t=y+1.f/6.f,z=.25f+float(i%4)*.1f;
            const float v[][4]={{x,y,z,1},{r,y,z,1},{x,t,z,1},{x,t,z,1},{r,y,z,1},{r,t,z,1}};mesh->streams[0].bytes.resize(sizeof v);std::memcpy(mesh->streams[0].bytes.data(),v,sizeof v);meshes.push_back(std::move(mesh));}
        Result result;std::vector<double> gpu,cpu;
        IDirect3DQuery9 *frequency=nullptr,*start=nullptr,*finish=nullptr,*disjoint=nullptr;
        if(timing){ok(d.CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d.CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d.CreateQuery(D3DQUERYTYPE_TIMESTAMP,&finish));ok(d.CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));}
        const unsigned count=timing?27:16;
        for(unsigned frame=0;frame<count;++frame){
            b.cleared=b.occlusionCleared=false;
            for(unsigned i=0;i<meshes.size();++i){auto packet=std::make_unique<NorthlightAutoBump::Deferred>();packet->mesh=meshes[i];packet->tag=19;packet->vertex=vs;vs->AddRef();packet->pixel=ps;ps->AddRef();packet->declaration=declaration;declaration->AddRef();packet->vertexCount=0;packet->vertexFloatOnly=true;
                packet->first=20;packet->pixelConstants={float(i%7)/8.f,.5f,.25f,1,.65f,.72f,.8f,1};packet->occlusion=pattern==1||(pattern==2&&(i&1));
                packet->viewport=partial?D3DVIEWPORT9{width/8,height/8,width*3/4,height*3/4,.15f,.85f}:D3DVIEWPORT9{0,0,width,height,0,1};
                NorthlightAutoBump::ReplayState states(&d,nullptr,{});for(unsigned n=0;n<14;++n)packet->states[n]=states.states[n].replay;packet->states[0]=TRUE;packet->states[5]=FALSE;
                std::fill(packet->raster,packet->raster+NorthlightAutoBump::DeferredRasterCount,0);packet->raster[0]=D3DCULL_NONE;packet->raster[1]=TRUE;packet->raster[2]=D3DCMP_LESSEQUAL;packet->raster[6]=TRUE;packet->raster[7]=~DWORD(0);packet->raster[8]=TRUE;packet->raster[9]=TRUE;packet->raster[12]=D3DSHADE_GOURAUD;packet->raster[13]=D3DFILL_SOLID;packet->raster[14]=TRUE;
                std::fill(packet->pixelInt,packet->pixelInt+64,0);std::fill(packet->pixelBool,packet->pixelBool+16,FALSE);b.deferred.push_back(std::move(packet));}
            const auto binds=d.binds;ok(d.BeginScene());const bool measure=timing&&frame>=16;
            if(measure){ok(disjoint->Issue(D3DISSUE_BEGIN));ok(frequency->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));}
            const auto begin=std::chrono::steady_clock::now();assert(b.finishCaptures());const auto submit=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
            if(measure){ok(finish->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));}ok(d.EndScene());
            if(measure){auto get=[](IDirect3DQuery9* query,void* value,DWORD bytes){const auto deadline=GetTickCount()+10000;HRESULT hr;while((hr=query->GetData(value,bytes,D3DGETDATA_FLUSH))==S_FALSE){assert(GetTickCount()<deadline);Sleep(0);}ok(hr);};
                UINT64 hz=0,a=0,z=0;BOOL invalid=TRUE;get(finish,&z,8);get(start,&a,8);get(frequency,&hz,8);get(disjoint,&invalid,sizeof invalid);assert(!invalid&&hz&&z>=a);gpu.push_back(double(z-a)*1000/double(hz));cpu.push_back(submit);result.binds=d.binds-binds;}
            b.endFrame();
        }
        if(timing){std::sort(gpu.begin(),gpu.end());std::sort(cpu.begin(),cpu.end());result.gpu=gpu[gpu.size()/2];result.cpu=cpu[cpu.size()/2];}
        else{result.color=pixels(&d,b.target,width,height,D3DFMT_A16B16G16R16F,8);result.occlusion=pixels(&d,b.occlusionTarget,width,height,D3DFMT_A8R8G8B8,4);assert(std::count_if(result.color.begin(),result.color.end(),[](auto v){return v!=0;})>width*height/2);if(pattern)assert(std::count_if(result.occlusion.begin(),result.occlusion.end(),[](auto v){return v!=0;})>width*height/2);}
        drop(frequency);drop(start);drop(finish);drop(disjoint);return result;
    }
};
int main(int argc,char** argv){
    assert(argc>=4);std::setvbuf(stdout,nullptr,_IONBF,0);const std::string folder=argv[2];const bool timing=std::string(argv[3])=="timing";
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightCaptureTargets";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(library,"Direct3DCreate9"));assert(create);auto api=create(D3D_SDK_VERSION);assert(api);
    for(unsigned samples:{0u,4u,8u}){
        if(timing&&samples)continue;
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=64;pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.MultiSampleType=D3DMULTISAMPLE_TYPE(samples);pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        IDirect3DDevice9* raw=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&raw));{
            TargetGpuMeter d(raw);IDirect3DVertexShader9* vs=nullptr;IDirect3DPixelShader9* ps=nullptr;auto v=read(folder+"vs.bin"),p=read(folder+"ps.bin");ok(d.CreateVertexShader(reinterpret_cast<DWORD*>(v.data()),&vs));ok(d.CreatePixelShader(reinterpret_cast<DWORD*>(p.data()),&ps));
            const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};IDirect3DVertexDeclaration9* decl=nullptr;ok(d.CreateVertexDeclaration(elements,&decl));
            for(unsigned fallback=0;fallback<2;++fallback)for(unsigned partial=0;partial<2;++partial)for(unsigned pattern=0;pattern<3;++pattern){if(timing&&(fallback||partial))continue;
                const auto result=MaterialCaptureStateTestAccess::run(d,vs,ps,decl,timing?1920:256,timing?1080:128,pattern,partial,fallback,timing);
                const auto stem=std::to_string(samples)+"-"+std::to_string(fallback)+"-"+std::to_string(partial)+"-"+std::to_string(pattern);
                if(timing)std::printf("CAPTURE_TARGET_TIMING pattern=%u gpuMs=%.6f cpuMs=%.6f targetBinds=%u\n",pattern,result.gpu,result.cpu,result.binds);
                else{for(const auto& item:{std::make_pair("normal",&result.color),std::make_pair("occlusion",&result.occlusion)}){std::ofstream f(folder+stem+"-"+item.first+".pixels",std::ios::binary);f.write(reinterpret_cast<const char*>(item.second->data()),item.second->size());}}
            }drop(vs);drop(ps);drop(decl);
        }raw->Release();
    }api->Release();DestroyWindow(window);std::puts("CAPTURE_TARGET_GPU_PASS");return 0;
}
