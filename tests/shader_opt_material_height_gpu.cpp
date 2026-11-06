// Compare actual AutoNormal bytecodes; this standalone oracle never starts the game.
#define main distance_oracle_main
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
#include <chrono>
#include <cstdlib>

static void heightWait(IDirect3DQuery9* query){
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    HRESULT hr;while((hr=query->GetData(nullptr,0,D3DGETDATA_FLUSH))==S_FALSE){assert(std::chrono::steady_clock::now()<deadline);SwitchToThread();}ok(hr);
}
static std::vector<Image> heightMips(Image base,const std::string& fixture={}){
    assert((base.format==D3DFMT_A8R8G8B8&&base.bytes==4)||(base.format==D3DFMT_A32B32G32R32F&&base.bytes==16));
    std::vector<Image> result{std::move(base)};
    while(result.back().width>1||result.back().height>1){
        const auto& source=result.back();const auto path=fixture+"."+std::to_string(result.size());
        Image next{std::max(1u,source.width/2),std::max(1u,source.height/2),source.format,source.bytes,{}};
        if(!fixture.empty()&&std::ifstream(path).good()){
            next=image(path);assert(next.width==std::max(1u,source.width/2)&&next.height==std::max(1u,source.height/2)&&next.format==source.format&&next.bytes==source.bytes);
        }else{
            next.pixels.resize(std::size_t(next.width)*next.height*next.bytes);
            for(unsigned y=0;y<next.height;++y)for(unsigned x=0;x<next.width;++x)for(unsigned c=0;c<4;++c){
                float sum=0;for(unsigned dy=0;dy<2;++dy)for(unsigned dx=0;dx<2;++dx){
                    const auto at=(std::size_t(std::min(y*2+dy,source.height-1))*source.width+std::min(x*2+dx,source.width-1))*source.bytes;
                    if(source.bytes==4)sum+=source.pixels[at+c];else{float value;std::memcpy(&value,source.pixels.data()+at+c*4,4);sum+=value;}
                }
                const auto at=(std::size_t(y)*next.width+x)*next.bytes;
                if(next.bytes==4)next.pixels[at+c]=static_cast<unsigned char>((unsigned(sum)+2)/4);else{const float value=sum*.25f;std::memcpy(next.pixels.data()+at+c*4,&value,4);}
            }
        }
        result.push_back(std::move(next));
    }
    return result;
}
static IDirect3DTexture9* heightSource(IDirect3DDevice9* d,const std::vector<Image>& levels){
    IDirect3DTexture9* result=nullptr;const auto& base=levels.front();ok(d->CreateTexture(base.width,base.height,UINT(levels.size()),0,D3DFORMAT(base.format),D3DPOOL_MANAGED,&result,nullptr));
    for(unsigned level=0;level<levels.size();++level){const auto& im=levels[level];D3DLOCKED_RECT lock{};ok(result->LockRect(level,&lock,nullptr,0));for(unsigned y=0;y<im.height;++y)std::memcpy(static_cast<char*>(lock.pBits)+y*lock.Pitch,im.pixels.data()+std::size_t(y)*im.width*im.bytes,im.width*im.bytes);ok(result->UnlockRect(level));}
    return result;
}
static void heightSourceUnchanged(IDirect3DTexture9* texture,const std::vector<Image>& levels){
    // Include source alpha: generated A is cavity, so source alpha must stay an input.
    for(unsigned level=0;level<levels.size();++level){const auto& im=levels[level];D3DLOCKED_RECT lock{};ok(texture->LockRect(level,&lock,nullptr,D3DLOCK_READONLY));for(unsigned y=0;y<im.height;++y)assert(!std::memcmp(static_cast<char*>(lock.pBits)+y*lock.Pitch,im.pixels.data()+std::size_t(y)*im.width*im.bytes,im.width*im.bytes));ok(texture->UnlockRect(level));}
}
static Image heightBytes(bool grid){
    const unsigned size=grid?256:64;Image result{size,size,D3DFMT_A8R8G8B8,4,std::vector<unsigned char>(size*size*4)};
    const unsigned char palette[]={0,1,12,13,14,24,25,26,127,128,129,230,254,255};
    for(unsigned y=0;y<size;++y)for(unsigned x=0;x<size;++x){auto* p=result.pixels.data()+(y*size+x)*4;p[0]=grid?static_cast<unsigned char>(x*29+y*17):palette[y%14];p[1]=grid?static_cast<unsigned char>(y):palette[(x/14+y)%14];p[2]=grid?static_cast<unsigned char>(x):palette[x%14];p[3]=static_cast<unsigned char>(x*3+y*5);}
    return result;
}
static Image heightBoundaries(){
    Image result{64,64,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(64*64*16)};
    const float values[]={0,.001f,std::nextafter(.05f,0.f),.05f,std::nextafter(.05f,1.f),.125f,.5f,1};
    const float edges[]={0,std::nextafter(.025f,0.f),.025f,std::nextafter(.025f,1.f),std::nextafter(.275f,0.f),.275f,std::nextafter(.275f,1.f),1};
    for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x){const float high=values[y%8],edge=edges[x%8];float c[]={high,high*(1-edge),values[(x/8+y/8)%8],float((x*3+y*5)%64)/63};if((x/8)&1)std::swap(c[0],c[1]);std::memcpy(result.pixels.data()+(y*64+x)*16,c,16);}
    return result;
}
struct HeightTarget {
    IDirect3DSurface9 *target=nullptr,*stage=nullptr;unsigned width,height;
    HeightTarget(IDirect3DDevice9* d,unsigned w,unsigned h):width(w),height(h){ok(d->CreateRenderTarget(w,h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&stage,nullptr));}
    ~HeightTarget(){stage->Release();target->Release();}
    void bind(IDirect3DDevice9* d){ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp{0,0,width,height,0,1};ok(d->SetViewport(&vp));const float texel[]={1.f/width,1.f/height,5,0};ok(d->SetPixelShaderConstantF(0,texel,1));}
    std::vector<unsigned char> pixels(IDirect3DDevice9* d){ok(d->GetRenderTargetData(target,stage));D3DLOCKED_RECT lock{};ok(stage->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<unsigned char> data(std::size_t(width)*height*4);for(unsigned y=0;y<height;++y)std::memcpy(data.data()+std::size_t(y)*width*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,width*4);ok(stage->UnlockRect());return data;}
};
static void heightDraw(IDirect3DDevice9* d,unsigned w,unsigned h,unsigned count=1,float offset=0,float scale=1){
    const Vertex quad[]={{-.5f,-.5f,0,1,offset,offset},{float(w)-.5f,-.5f,0,1,offset+scale,offset},{-.5f,float(h)-.5f,0,1,offset,offset+scale},{float(w)-.5f,float(h)-.5f,0,1,offset+scale,offset+scale}};
    ok(d->BeginScene());for(unsigned i=0;i<count;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());
}
struct HeightSampler {const char* name;D3DTEXTUREFILTERTYPE filter,mip;unsigned level;D3DTEXTUREADDRESS address;float offset,scale;};
static void heightSampler(IDirect3DDevice9* d,const HeightSampler& s){
    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(0,state,s.filter));ok(d->SetSamplerState(0,D3DSAMP_MIPFILTER,s.mip));ok(d->SetSamplerState(0,D3DSAMP_MAXMIPLEVEL,s.level));ok(d->SetSamplerState(0,D3DSAMP_ADDRESSU,s.address));ok(d->SetSamplerState(0,D3DSAMP_ADDRESSV,s.address));
}
int main(int argc,char** argv){
    if(argc<4||argc>5){std::fprintf(stderr,"usage: shader-opt-material-height-gpu old.bin new.bin backend.dll [source.fgr]\n");return 1;}
    std::setvbuf(stdout,nullptr,_IONBF,0);
    HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    const auto module=LoadLibraryA(argv[3]);assert(module);const auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));ok(d->SetDepthStencilSurface(nullptr));
    for(auto key:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_STENCILENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE})ok(d->SetRenderState(key,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetSamplerState(0,D3DSAMP_SRGBTEXTURE,FALSE));ok(d->SetSamplerState(0,D3DSAMP_MIPMAPLODBIAS,0));
    IDirect3DPixelShader9* shaders[2]={};for(unsigned i=0;i<2;++i){const auto code=read(argv[1+i]);assert(code.size()>=8&&code.size()%4==0);ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&shaders[i]));}
    const float profiles[][4]={{.5f,.75f,1,0},{.45f,0,1,0},{.5f,0,-1,0},{.2f,1,1,.25f},{.8f,.75f,-1,-.25f},{.5f,.01f,1,.005f}};
    const HeightSampler samplers[]={{"linear-wrap",D3DTEXF_LINEAR,D3DTEXF_NONE,0,D3DTADDRESS_WRAP,0,1},{"linear-clamp",D3DTEXF_LINEAR,D3DTEXF_NONE,0,D3DTADDRESS_CLAMP,0,1},{"point-edges",D3DTEXF_POINT,D3DTEXF_NONE,0,D3DTADDRESS_WRAP,-.01f,1.02f},{"linear-mip1",D3DTEXF_LINEAR,D3DTEXF_POINT,1,D3DTADDRESS_WRAP,.0039f,1},{"point-mip2",D3DTEXF_POINT,D3DTEXF_POINT,2,D3DTADDRESS_CLAMP,-.01f,1.02f},{"linear-mipblend",D3DTEXF_LINEAR,D3DTEXF_LINEAR,1,D3DTADDRESS_WRAP,.99999f,.7f}};
    std::vector<std::vector<Image>> sources{heightMips(heightBytes(true)),heightMips(heightBytes(false)),heightMips(heightBoundaries())};
    const char* names[]={"byte-grid","byte-boundaries","float-boundaries","fixture"};if(argc==5)sources.push_back(heightMips(image(argv[4]),argv[4]));
    std::vector<IDirect3DTexture9*> textures;for(const auto& levels:sources)textures.push_back(heightSource(d,levels));unsigned cases=0;
    {
        HeightTarget output(d,128,96);output.bind(d);
        for(unsigned source=0;source<sources.size();++source){ok(d->SetTexture(0,textures[source]));
            for(unsigned profile=0;profile<6;++profile){ok(d->SetPixelShaderConstantF(1,profiles[profile],1));for(const auto& sampler:samplers){heightSampler(d,sampler);std::vector<unsigned char> reference;
                for(unsigned mode=0;mode<2;++mode){ok(d->SetPixelShader(shaders[mode]));heightDraw(d,output.width,output.height,1,sampler.offset,sampler.scale);auto actual=output.pixels(d);
                    if(!mode)reference=std::move(actual);else if(reference!=actual){std::size_t first=0,changed=0,alpha=0;for(std::size_t i=0;i<actual.size();++i)if(actual[i]!=reference[i]){if(!changed)first=i;++changed;alpha+=i%4==3;}const char* channels[]={"height","normal_y","normal_x","cavity"};std::fprintf(stderr,"BYTE_MISMATCH source=%s profile=%u sampler=%s changed=%zu cavity_changes=%zu first=%zu,%zu channel=%s old=%u new=%u\n",names[source],profile,sampler.name,changed,alpha,(first/4)%output.width,(first/4)/output.width,channels[first%4],unsigned(reference[first]),unsigned(actual[first]));return 2;}
                }++cases;
            }}heightSourceUnchanged(textures[source],sources[source]);std::printf("HEIGHT_BYTE_IDENTITY source=%s cases=36 source_bytes_and_alpha_unchanged=1\n",names[source]);
        }
    }
    IDirect3DQuery9 *event=nullptr,*start=nullptr,*end=nullptr,*frequency=nullptr,*disjoint=nullptr;ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    const unsigned timedSource=argc==5?3:0;ok(d->SetTexture(0,textures[timedSource]));heightSampler(d,samplers[0]);ok(d->SetPixelShaderConstantF(1,profiles[0],1));
    // Warm the actual shader and source; timestamps cover only the generation kernel.
    // 1024 is an explicit stress size, above the production cache's 512 cap.
    for(unsigned size:{256u,1024u}){
        HeightTarget output(d,size,size);output.bind(d);const unsigned draws=size==256?32:8;std::vector<double> times[2];
        for(unsigned mode=0;mode<2;++mode){ok(d->SetPixelShader(shaders[mode]));heightDraw(d,size,size,8);}ok(event->Issue(D3DISSUE_END));heightWait(event);
        for(unsigned block=0;block<12;++block)for(unsigned turn=0;turn<2;++turn){const unsigned mode=(block+turn)&1;ok(d->SetPixelShader(shaders[mode]));ok(disjoint->Issue(D3DISSUE_BEGIN));ok(frequency->Issue(D3DISSUE_END));ok(start->Issue(D3DISSUE_END));heightDraw(d,size,size,draws);ok(end->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));ok(event->Issue(D3DISSUE_END));heightWait(event);
            UINT64 a=0,b=0,hz=0;BOOL bad=FALSE;ok(start->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(frequency->GetData(&hz,sizeof hz,0));ok(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&b>=a&&hz);if(block>=4)times[mode].push_back(double(b-a)/double(hz)*1000/draws);
        }
        double median[2];for(unsigned mode=0;mode<2;++mode){assert(times[mode].size()==8);std::sort(times[mode].begin(),times[mode].end());median[mode]=(times[mode][3]+times[mode][4])*.5;}
        std::vector<unsigned char> reference;for(unsigned mode=0;mode<2;++mode){ok(d->SetPixelShader(shaders[mode]));heightDraw(d,size,size);auto actual=output.pixels(d);if(!mode)reference=std::move(actual);else assert(reference==actual);}
        std::printf("{\"test\":\"AutoNormal\",\"width\":%u,\"height\":%u,\"source\":\"%s\",\"reference_ms\":%.9f,\"optimized_ms\":%.9f,\"ratio\":%.6f,\"byte_identity\":true,\"paired_samples\":8,\"warmed_kernel_only\":true,\"steady_state_capture_benchmark\":false}\n",size,size,names[timedSource],median[0],median[1],median[0]>0?median[1]/median[0]:0);
    }
    heightSourceUnchanged(textures[timedSource],sources[timedSource]);std::printf("HEIGHT_BYTE_IDENTITY_PASS cases=%u timed_sizes=256,1024 source_alpha_preserved=1 game_launched=0\n",cases+2);
    ok(d->SetTexture(0,nullptr));for(auto* texture:textures)texture->Release();for(auto* shader:shaders)shader->Release();for(auto* query:{event,start,end,frequency,disjoint})query->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);return 0;
}
