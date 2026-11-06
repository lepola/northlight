// Authored fixture and optional client shader run on a private hidden device.
#define main northlight_unused_composite
#include "composite_distance_gpu.cpp"
#undef main
#include "hdr_color.h"
#include "water_shader_patch.h"
#include "forwarders.h"
static void checkWater(HRESULT hr,const char* operation,unsigned line){if(FAILED(hr)){std::fprintf(stderr,"line=%u operation=%s HRESULT=%08lx\n",line,operation,(unsigned long)hr);std::abort();}}
#define ok(operation) checkWater((operation),#operation,__LINE__)

struct Meter:ForwardIDirect3DDevice9 {
    unsigned clears=0,copies=0,draws=0;
    explicit Meter(IDirect3DDevice9* d):ForwardIDirect3DDevice9(d){}
    void reset(){clears=copies=draws=0;}
    HRESULT STDMETHODCALLTYPE Clear(DWORD n,const D3DRECT* r,DWORD flags,D3DCOLOR c,float z,DWORD s)override{++clears;return real->Clear(n,r,flags,c,z,s);}
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9* a,const RECT* ar,IDirect3DSurface9* b,const RECT* br,D3DTEXTUREFILTERTYPE f)override{++copies;return real->StretchRect(a,ar,b,br,f);}
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE t,UINT n,const void* v,UINT stride)override{++draws;return real->DrawPrimitiveUP(t,n,v,stride);}
};
static std::vector<DWORD> program(const char* path){auto bytes=read(path);assert(bytes.size()%4==0);std::vector<DWORD> words(bytes.size()/4);std::memcpy(words.data(),bytes.data(),bytes.size());return words;}
static float decodeWater(float x){return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f);}
struct Target {
    IDirect3DSurface9 *surface=nullptr,*resolved=nullptr,*staging=nullptr;unsigned width,height,bytes;D3DFORMAT format;
    Target(IDirect3DDevice9* d,unsigned w,unsigned h,D3DFORMAT f,D3DMULTISAMPLE_TYPE ms):width(w),height(h),bytes(f==D3DFMT_A8R8G8B8?4:8),format(f){
        ok(d->CreateRenderTarget(w,h,f,ms,0,FALSE,&surface,nullptr));
        if(ms)ok(d->CreateRenderTarget(w,h,f,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
        ok(d->CreateOffscreenPlainSurface(w,h,f,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    }
    ~Target(){surface->Release();if(resolved)resolved->Release();staging->Release();}
    std::vector<unsigned char> pixels(IDirect3DDevice9* d){
        if(resolved)ok(d->StretchRect(surface,nullptr,resolved,nullptr,D3DTEXF_NONE));ok(d->GetRenderTargetData(resolved?resolved:surface,staging));
        D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> result(std::size_t(width)*height*bytes);
        for(unsigned y=0;y<height;++y)std::memcpy(result.data()+std::size_t(y)*width*bytes,static_cast<const char*>(lock.pBits)+y*lock.Pitch,width*bytes);ok(staging->UnlockRect());return result;
    }
};
int main(int argc,char** argv){
    assert(argc==9||argc==10);const bool benchmark=argc==10,nativeBackend=std::atoi(argv[8])!=0;const auto ms=D3DMULTISAMPLE_TYPE(std::atoi(argv[2]));
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,32,16,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    auto library=LoadLibraryA(argv[1]);assert(library);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(library,"Direct3DCreate9"));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=32;pp.BackBufferHeight=16;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));Meter meter(d);NorthlightStateBlockPool pool(&meter);
    auto vsCode=program(argv[3]);IDirect3DVertexShader9* vs=nullptr;ok(d->CreateVertexShader(vsCode.data(),&vs));
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    IDirect3DVertexDeclaration9* declaration=nullptr;ok(d->CreateVertexDeclaration(elements,&declaration));
    auto fixture=program(argv[4]),client=program(argv[5]);const bool baseline=std::atoi(argv[6])!=0;
    IDirect3DTexture9* tex=nullptr;ok(d->CreateTexture(1,1,1,0,D3DFMT_A32B32G32R32F,D3DPOOL_MANAGED,&tex,nullptr));
    auto draw=[&](unsigned w,unsigned h,unsigned ordinal=0,bool dense=false){const float x=dense?-.5f:2.25f+float(ordinal%(w>128?w-128:1)),y=dense?-.5f:1.25f;const float r=dense?float(w)-.5f:std::min(float(w)-2.25f,x+96),b=dense?float(h)-.5f:float(h)-2.25f;
        const Vertex q[]={{x,y,.3f,1,0,0},{r,y,.3f,1,1,0},{x,b,.3f,1,0,1},{r,b,.3f,1,1,1}};return meter.DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex));};
    auto setup=[&](Target& target,IDirect3DPixelShader9* shader,unsigned blend,float fog,float alpha){
        for(unsigned n=0;n<16;++n)ok(d->SetTexture(n,nullptr));ok(NorthlightHdr::quadState(d,target.surface,target.width,target.height));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
        ok(d->SetVertexDeclaration(declaration));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(shader));
        const float viewport[]={2.f/target.width,-2.f/target.height,0,0},tint[]={.8f,.6f,.4f,alpha},environment[]={fog,12.5f,0,0};
        ok(d->SetVertexShaderConstantF(0,viewport,1));ok(d->SetVertexShaderConstantF(1,tint,1));ok(d->SetVertexShaderConstantF(2,environment,1));
        float constants[14][4];for(auto& row:constants)for(float& value:row)value=.2f;ok(d->SetPixelShaderConstantF(0,constants[0],14));
        D3DLOCKED_RECT lock{};ok(tex->LockRect(0,&lock,nullptr,0));const float color[]={.7f,.5f,.3f,alpha};std::memcpy(lock.pBits,color,sizeof color);ok(tex->UnlockRect(0));
        for(unsigned slot:{0u,1u}){ok(d->SetTexture(slot,tex));ok(d->SetSamplerState(slot,D3DSAMP_MINFILTER,D3DTEXF_POINT));ok(d->SetSamplerState(slot,D3DSAMP_MAGFILTER,D3DTEXF_POINT));}
        ok(d->SetRenderState(D3DRS_FOGENABLE,fog!=0));ok(d->SetRenderState(D3DRS_FOGCOLOR,0xff102030));ok(d->SetRenderState(D3DRS_FOGTABLEMODE,D3DFOG_NONE));ok(d->SetRenderState(D3DRS_FOGVERTEXMODE,D3DFOG_NONE));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,blend!=0));ok(d->SetRenderState(D3DRS_SRCBLEND,blend==2?D3DBLEND_ONE:D3DBLEND_SRCALPHA));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_INVSRCALPHA));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
    };
    if(!benchmark){
        Target native(d,32,16,D3DFMT_A8R8G8B8,ms),hdr(d,32,16,D3DFMT_A16B16G16R16F,ms),water(d,32,16,D3DFMT_G32R32F,ms),extraA(d,32,16,D3DFMT_A16B16G16R16F,ms),extraB(d,32,16,D3DFMT_A16B16G16R16F,ms);
        unsigned cases=0,rejected=0;float worst=0;
        for(const auto* original:{&fixture,&client}){
            std::vector<std::uint32_t> derivative;assert(!NorthlightWaterShaderPatch::patchMrt(reinterpret_cast<const std::uint32_t*>(original->data()),original->size(),derivative));
            if(original==&client)assert(NorthlightWaterShaderPatch::fnv1a64(derivative.data(),derivative.size())==0xe79938975a92b584ULL);
            bool authoredNative=false;IDirect3DPixelShader9 *nativeShader=nullptr,*mrt=nullptr;ok(d->CreatePixelShader(original->data(),&nativeShader));const HRESULT admission=d->CreatePixelShader(reinterpret_cast<const DWORD*>(derivative.data()),&mrt);
            if(FAILED(admission)){
                assert(nativeBackend&&admission==D3DERR_INVALIDCALL);++rejected;
                std::printf("HDR_WATER_NATIVE_ADMISSION_REJECTED exact_client=%u HRESULT=%08lx before_hdr_patch=1\n",unsigned(original==&client),(unsigned long)admission);
                if(original==&client){nativeShader->Release();continue;}
                const auto authoredMrt=program(argv[7]);ok(d->CreatePixelShader(authoredMrt.data(),&mrt));nativeShader->Release();nativeShader=mrt;nativeShader->AddRef();authoredNative=true;
            }
            if(nativeBackend&&ms){
                setup(hdr,mrt,0,0,1);ok(d->SetRenderTarget(1,water.surface));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->BeginScene());ok(draw(32,16));ok(d->EndScene());const auto p=hdr.pixels(d);std::uint16_t value;std::memcpy(&value,p.data()+(8*32+12)*8,2);
                std::printf("HDR_WATER_NATIVE_MSAA_BINDING_CONTROL samples=%u unmodified_color=%g expected_source=.56 hdr_patch_exercised=0 admitted_hdr_cases=0\n",unsigned(ms),half(value));return 0;
            }
            for(float fog:{0.f,.35f,1.f})for(float alpha:{0.f,.002f,.01f,.25f,.75f,1.f})for(unsigned blend=0;blend<3;++blend)for(bool occupied:{false,true}){
                setup(native,nativeShader,blend,fog,alpha);if(authoredNative){ok(d->SetRenderTarget(1,water.surface));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));}ok(d->BeginScene());ok(draw(32,16));ok(d->EndScene());const auto nativeReference=native.pixels(d);
                setup(native,mrt,blend,fog,alpha);ok(d->SetRenderTarget(1,water.surface));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));
                ok(d->BeginScene());ok(draw(32,16));ok(d->EndScene());const auto waterReference=water.pixels(d);assert(native.pixels(d)==nativeReference);
                // The unblended native output supplies an independent UNORM
                // quantization interval for the direct linear source.
                setup(native,nativeShader,0,fog,alpha);ok(d->BeginScene());ok(draw(32,16));ok(d->EndScene());const auto source=native.pixels(d);
                float mask[2];std::memcpy(mask,waterReference.data()+(8*32+12)*8,sizeof mask);
                if(source[(8*32+12)*4+3]>3)assert(mask[0]==12.5f&&mask[1]==1.f);
                if(source[(8*32+12)*4+3]==0)assert(mask[0]==0.f&&mask[1]==0.f);
                for(unsigned n=0;n<32*8;++n)assert(waterReference[n]==0); // The first row is outside the geometry.
                setup(hdr,mrt,blend,fog,alpha);ok(d->SetRenderTarget(1,water.surface));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));
                if(occupied){ok(d->SetRenderTarget(2,extraA.surface));ok(d->SetRenderTarget(3,extraB.surface));}
                ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));NorthlightHdr::Color transfer;meter.reset();unsigned submitted=0;
                ok(d->BeginScene());ok(transfer.draw(&meter,80,[&]{++submitted;return draw(32,16);},0,&pool));ok(d->EndScene());
                assert(submitted==1);if(baseline){assert(transfer.layers==1);std::puts("HDR_WATER_OLD_REJECTION_REPRODUCED layers=1");return 0;}
                assert(transfer.layers==0&&transfer.patched==1&&meter.clears==0&&meter.copies==0&&meter.draws==1);assert(water.pixels(d)==waterReference);
                for(unsigned slot=0;slot<4;++slot){IDirect3DSurface9* bound=nullptr;const auto hr=d->GetRenderTarget(slot,&bound);IDirect3DSurface9* wanted=slot==0?hdr.surface:slot==1?water.surface:occupied?(slot==2?extraA.surface:extraB.surface):nullptr;assert((hr==D3DERR_NOTFOUND&&!wanted)||(SUCCEEDED(hr)&&bound==wanted));if(bound)bound->Release();}
                IDirect3DPixelShader9* restored=nullptr;ok(d->GetPixelShader(&restored));assert(restored==mrt);restored->Release();
                const auto pixels=hdr.pixels(d);const unsigned point=(8*32+12)*4;const auto* result=reinterpret_cast<const std::uint16_t*>(pixels.data());const float a=source[point+3]/255.f;
                for(unsigned c=0;c<3;++c){const float s=source[point+2-c]/255.f;auto value=[&](float encoded,float opacity){return blend==2?decodeWater(std::min(1.f,encoded/std::max(.000001f,opacity)))*opacity:decodeWater(encoded)*(blend==1?opacity:1);};const float expected=value(s,a),actual=half(result[point+c]);
                    // A8 source capture rounds by at most half an UNORM step;
                    // FP16 output adds at most one half-precision ulp here.
                    const float sl=std::max(0.f,s-.5f/255),sh=std::min(1.f,s+.5f/255),al=std::max(0.f,a-.5f/255),ah=std::min(1.f,a+.5f/255);
                    const float lo=std::min({value(sl,al),value(sl,ah),value(sl,std::clamp(sl,al,ah))}),hi=std::max({value(sh,al),value(sh,ah),value(sh,std::clamp(sh,al,ah))});
                    if(!(std::isfinite(actual)&&actual>=lo-.001f&&actual<=hi+.001f))std::fprintf(stderr,"HDR_ORACLE msaa=%u native=%u fog=%g alpha=%g source=%g,%g blend=%u occupied=%u channel=%u actual=%g lo=%g hi=%g expected=%g\n",unsigned(ms),unsigned(authoredNative),fog,alpha,s,a,blend,unsigned(occupied),c,actual,lo,hi,expected);
                    assert(std::isfinite(actual)&&actual>=lo-.001f&&actual<=hi+.001f);worst=std::max(worst,std::fabs(actual-expected));
                }
                const float al=std::max(0.f,a-.5f/255),ah=std::min(1.f,a+.5f/255),actualAlpha=half(result[point+3]);
                assert(actualAlpha>=(blend==1?al*al:al)-.001f&&actualAlpha<=(blend==1?ah*ah:ah)+.001f);
                for(std::size_t i=0;i<pixels.size()/2;++i)assert(std::isfinite(half(result[i])));
                ++cases;
            }
            nativeShader->Release();mrt->Release();
        }
        std::printf("HDR_WATER_TRANSFER_PASS cases=%u samples=%u rejected_before_hdr=%u exact_client_native_rgba_parity=%u native_authored_auxiliary_oracle=%u water_mask_exact=1 alpha_quantization_bounds=1 occupied_targets=4 single_source_draw=1 full_layers=0 max_center_error=%g\n",cases,unsigned(ms),rejected,unsigned(!rejected),unsigned(rejected!=0),worst);
    }else{
        const unsigned W=3840,H=2160,count=332;const bool dense=std::strcmp(argv[9],"dense")==0;Target hdr(d,W,H,D3DFMT_A16B16G16R16F,ms);
        std::vector<std::uint32_t> derivative;assert(!NorthlightWaterShaderPatch::patchMrt(reinterpret_cast<const std::uint32_t*>(client.data()),client.size(),derivative));IDirect3DPixelShader9* shader=nullptr;if(nativeBackend){auto authoredMrt=program(argv[7]);ok(d->CreatePixelShader(authoredMrt.data(),&shader));}else ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(derivative.data()),&shader));NorthlightHdr::Color transfer;
        auto frame=[&]{setup(hdr,shader,1,.35f,.5f);meter.reset();ok(d->BeginScene());for(unsigned n=0;n<count;++n)ok(transfer.draw(&meter,80,[&]{return draw(W,H,n,dense);},0,&pool));ok(d->EndScene());assert(meter.clears==(baseline?count:0)&&meter.copies==(baseline?count:0)&&meter.draws==count*(baseline?2:1));};
        IDirect3DQuery9 *first=nullptr,*last=nullptr,*frequency=nullptr,*event=nullptr,*disjoint=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&first));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&last));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
        for(unsigned n=0;n<3;++n)frame();wait(event);
        for(unsigned sample=0;sample<8;++sample){ok(disjoint->Issue(D3DISSUE_BEGIN));ok(first->Issue(D3DISSUE_END));LARGE_INTEGER a{},b{},hz{};QueryPerformanceFrequency(&hz);QueryPerformanceCounter(&a);frame();QueryPerformanceCounter(&b);ok(last->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);UINT64 start=0,end=0,rate=0;BOOL bad=FALSE;ok(first->GetData(&start,sizeof start,0));ok(last->GetData(&end,sizeof end,0));ok(frequency->GetData(&rate,sizeof rate,0));ok(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&rate&&end>=start);
            std::printf("HDR_WATER_TIMING baseline=%u dense=%u sample=%u width=%u height=%u msaa=%u draws=%u gpu_ms=%.6f cpu_submit_ms=%.6f clears=%u resolves=%u submitted=%u\n",unsigned(baseline),unsigned(dense),sample,W,H,unsigned(ms),count,double(end-start)*1000/rate,double(b.QuadPart-a.QuadPart)*1000/hz.QuadPart,meter.clears,meter.copies,meter.draws);}
        first->Release();last->Release();frequency->Release();event->Release();disjoint->Release();shader->Release();
    }
    return 0;
}
