// Production particle transfer: real stock pixel programs, controlled geometry.
// Never installs a renderer, launches WoW, or changes display HDR state.
#undef NDEBUG
#pragma clang diagnostic ignored "-Wreturn-type"
#define main northlight_composite_unused
#include "composite_distance_gpu.cpp"
#undef main
#include "hdr_frame.h"
#include "hdr_color.h"
#include "particle_light_proof.h"
#include "particle_experiment_shaders.h"
#include "spell_layer.h"
#include <functional>

using Words=std::vector<std::uint32_t>;
static IDirect3DVertexShader9* fixtureVs[2]{};
static IDirect3DVertexDeclaration9* fixtureDecl=nullptr;
static unsigned fixtureModel=0;
// Real callback and Color; the owner supplies only the composition target and
// registration tags. Fixture VS tags simulate known native particle identities.
struct Device {
    static constexpr unsigned kFamilyShift=3;
    IDirect3DDevice9* device;
    NorthlightHdr::Color hdrColor;
    bool hdrSceneReady=true,backTarget=true;
    IDirect3DSurface9* hdrPresentBack=nullptr;
    std::unordered_map<IDirect3DVertexShader9*,int> vsTags;
    bool hdrBackTarget()const{return backTarget;}
    HRESULT hdrBindBack(IDirect3DSurface9* target){return device->SetRenderTarget(0,target);}
    template<class Draw> HRESULT projectedDraw(unsigned,unsigned,Draw draw){return draw();}
#include "particle_replay_callback.h"
};
struct Target {
    IDirect3DSurface9 *surface=nullptr,*resolved=nullptr,*staging=nullptr;
    unsigned width,height;
    Target(IDirect3DDevice9* d,unsigned w,unsigned h,D3DMULTISAMPLE_TYPE ms=D3DMULTISAMPLE_NONE):width(w),height(h){
        ok(d->CreateRenderTarget(w,h,D3DFMT_A16B16G16R16F,ms,0,FALSE,&surface,nullptr));
        if(ms)ok(d->CreateRenderTarget(w,h,D3DFMT_A16B16G16R16F,D3DMULTISAMPLE_NONE,0,FALSE,&resolved,nullptr));
        ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A16B16G16R16F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    }
    ~Target(){surface->Release();if(resolved)resolved->Release();staging->Release();}
    std::vector<float> pixels(IDirect3DDevice9* d,const std::string& filename=""){
        if(resolved)ok(d->StretchRect(surface,nullptr,resolved,nullptr,D3DTEXF_NONE));
        ok(d->GetRenderTargetData(resolved?resolved:surface,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        std::vector<float> result(size_t(width)*height*4);
        std::ofstream file;if(!filename.empty())file.open(filename,std::ios::binary);
        for(unsigned y=0;y<height;++y){const auto* row=reinterpret_cast<const std::uint16_t*>(static_cast<const char*>(lock.pBits)+size_t(y)*lock.Pitch);
            for(unsigned x=0;x<width*4;++x)result[size_t(y)*width*4+x]=half(row[x]);
            if(file.is_open())file.write(reinterpret_cast<const char*>(row),width*8);
        }
        ok(staging->UnlockRect());return result;
    }
};
static double decode(double x){x=std::max(x,0.);return x<=.04045?x/12.92:std::pow((x+.055)/1.055,2.4);}
static IDirect3DPixelShader9* patched(IDirect3DDevice9* d,const std::uint32_t* code,size_t count,bool candidate,NorthlightHdrUi::Info* info=nullptr){
    const bool sm2=code[0]==0xffff0200;
    const auto* tail=candidate?(sm2?kHdrParticlePs2:kHdrParticlePs3):(sm2?kHdrUiPs2:kHdrUiPs3);
    const auto size=candidate?(sm2?sizeof kHdrParticlePs2:sizeof kHdrParticlePs3):(sm2?sizeof kHdrUiPs2:sizeof kHdrUiPs3);
    Words output;assert(NorthlightHdrUi::patch(code,count,tail,size/4,80,false,output,info,candidate));
    IDirect3DPixelShader9* shader=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(output.data()),&shader));return shader;
}
static void scalarChecks(IDirect3DDevice9* d){
    Target target(d,4,4);unsigned cases=0;double maxRelative=0,peak=0;
    for(unsigned major:{2u,3u})for(bool authoredSat:{false,true}){
        Words original={0xffff0000u|(major<<8),0x02000001,authoredSat?0x801f0800u:0x800f0800u,0xa0e40000,0xffff};
        for(bool candidate:{false,true}){
            auto* ps=patched(d,original.data(),original.size(),candidate);
            for(float rgb:{-2.f,-.02f,0.f,.02f,.5f,1.f,1.25f,2.f,4.f})for(float alpha:{-2.f,-.001f,0.f,.25f,1.f,1.001f,2.f})for(unsigned blend=0;blend<3;++blend){
                ok(d->BeginScene());ok(NorthlightHdr::quadState(d,target.surface,4,4));
                ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0));ok(d->SetPixelShader(ps));
                const float input[]={rgb,rgb*.7f,rgb*.3f,alpha};ok(d->SetPixelShaderConstantF(0,input,1));
                ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,blend!=0));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
                ok(d->SetRenderState(D3DRS_SRCBLEND,blend==2?D3DBLEND_SRCALPHA:D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));
                // Inspect bounded source alpha independently of additive alpha accumulation.
                ok(d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLENDALPHA,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLENDALPHA,D3DBLEND_ZERO));
                ok(NorthlightHdr::drawQuad(d,4,4));ok(d->EndScene());auto pixels=target.pixels(d);
                for(unsigned c=0;c<4;++c){
                    const double a=std::clamp(double(alpha),0.,1.);
                    double expected=c==3?a:decode((candidate&&!authoredSat)?input[c]:std::clamp(double(input[c]),0.,1.))*(blend==2?a:1.);
                    const double relative=std::abs(pixels[c]-expected)/std::max(1.,expected);maxRelative=std::max(maxRelative,relative);
                    if(!std::isfinite(pixels[c])||relative>.003){std::fprintf(stderr,"oracle SM%u sat=%u candidate=%u rgb=%g alpha=%g blend=%u channel=%u actual=%g expected=%g\n",major,authoredSat,candidate,rgb,alpha,blend,c,pixels[c],expected);std::abort();}
                    if(c==3)assert(pixels[c]>=0&&pixels[c]<=1);else peak=std::max(peak,double(pixels[c]));
                }
                ++cases;
            }
            ps->Release();
        }
    }
    assert(peak>19);std::printf("{\"kind\":\"scalar\",\"cases\":%u,\"max_relative_error\":%.9g,\"peak_linear_rgb\":%.9g,\"source_alpha_bounded\":true,\"authored_saturation_preserved\":true}\n",cases,maxRelative,peak);std::fflush(stdout);
}
struct SpriteVertex{float x,y,z,w;DWORD color;float u,v;};
struct Sprites {
    IDirect3DVertexBuffer9* buffer=nullptr;unsigned count;
    Sprites(IDirect3DDevice9* d,unsigned w,unsigned h,unsigned n):count(n){
        std::vector<SpriteVertex> vertices;
        const DWORD colors[]={0xffffa040,0xff70c0ff,0xffd080ff,0xffffe8b8,0xff80ffc0,0xffff8040};
        for(unsigned i=0;i<n;++i){
            const unsigned columns=n==12?4:8;const float side=h*(n==12?.38f:.20f);
            const float cx=(float(i%columns)+.5f)*w/columns,cy=(float(i/columns)+.5f)*h/((n+columns-1)/columns);
            const float x=cx-side*.5f-.5f,y=cy-side*.5f-.5f;const DWORD color=colors[i%6];
            const SpriteVertex q[]={{x,y,0,1,color,0,0},{x+side,y,0,1,color,1,0},{x,y+side,0,1,color,0,1},
                {x,y+side,0,1,color,0,1},{x+side,y,0,1,color,1,0},{x+side,y+side,0,1,color,1,1}};
            vertices.insert(vertices.end(),q,q+6);
        }
        ok(d->CreateVertexBuffer(UINT(vertices.size()*sizeof(SpriteVertex)),0,D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1,D3DPOOL_MANAGED,&buffer,nullptr));
        void* memory=nullptr;ok(buffer->Lock(0,0,&memory,0));std::memcpy(memory,vertices.data(),vertices.size()*sizeof(SpriteVertex));ok(buffer->Unlock());
    }
    ~Sprites(){buffer->Release();}
    void draw(IDirect3DDevice9* d){ok(d->SetVertexDeclaration(fixtureDecl));ok(d->SetStreamSource(0,buffer,0,sizeof(SpriteVertex)));ok(d->DrawPrimitive(D3DPT_TRIANGLELIST,0,count*2));}
};
static void render(IDirect3DDevice9* d,Target& target,Sprites& sprites,IDirect3DTexture9* texture,IDirect3DPixelShader9* ps,D3DBLEND source=D3DBLEND_SRCALPHA,float fogAmount=1,Device* owner=nullptr,unsigned family=0,bool afterEffects=true){
    ok(d->BeginScene());ok(NorthlightHdr::quadState(d,target.surface,target.width,target.height));
    ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff080b12,1,0));ok(d->SetPixelShader(ps));ok(d->SetTexture(0,texture));
    ok(d->SetVertexShader(fixtureVs[fixtureModel]));
    const float viewport[]={2.f/target.width,-2.f/target.height,0,0},fog[]={fogAmount,0,0,0},fogColor[]={0,0,0,0};
    ok(d->SetVertexShaderConstantF(0,viewport,1));ok(d->SetVertexShaderConstantF(1,fog,1));ok(d->SetPixelShaderConstantF(2,fogColor,1));
    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(0,state,D3DTEXF_LINEAR));
    ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));ok(d->SetRenderState(D3DRS_SRCBLEND,source));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));
    // Add light to an opaque scene without accumulating meaningless framebuffer alpha.
    ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));
    if(owner){
        owner->hdrPresentBack=target.surface;owner->vsTags[fixtureVs[fixtureModel]]=int(family<<Device::kFamilyShift);
        ok(d->SetVertexDeclaration(fixtureDecl));ok(d->SetStreamSource(0,sprites.buffer,0,sizeof(SpriteVertex)));
        ok(Device::spellProjectedDraw(owner,d,NorthlightSpellLayer::DrawArgs::primitive(D3DPT_TRIANGLELIST,0,sprites.count*2),afterEffects));
        IDirect3DPixelShader9* restored=nullptr;ok(d->GetPixelShader(&restored));assert(restored==ps);restored->Release();
    }else sprites.draw(d);
    ok(d->EndScene());
}
static void admissionChecks(IDirect3DDevice9* d,const NativeShader& native){
    NorthlightHdr::Color color;IDirect3DPixelShader9* original=nullptr;
    ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(native.words),&original));
    auto* particle=color.replacement(d,original,80,3);assert(particle);unsigned cases=0;
    for(unsigned family=0;family<8;++family)for(float white:{80.f,203.f})for(bool enabled:{false,true})
    for(auto source:{D3DBLEND_ONE,D3DBLEND_SRCALPHA,D3DBLEND_INVDESTCOLOR})for(auto dest:{D3DBLEND_ONE,D3DBLEND_INVSRCALPHA})
    for(auto op:{D3DBLENDOP_ADD,D3DBLENDOP_SUBTRACT,D3DBLENDOP_MAX})for(bool srgb:{false,true}){
        ok(d->SetPixelShader(original));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,enabled));
        ok(d->SetRenderState(D3DRS_SRCBLEND,source));ok(d->SetRenderState(D3DRS_DESTBLEND,dest));
        ok(d->SetRenderState(D3DRS_BLENDOP,op));ok(d->SetRenderState(D3DRS_SRGBWRITEENABLE,srgb));
        const bool accepted=(family==1||family==2)&&white==80&&enabled&&(source==D3DBLEND_ONE||source==D3DBLEND_SRCALPHA)&&dest==D3DBLEND_ONE&&op==D3DBLENDOP_ADD;
        auto* expected=accepted?particle:color.replacement(d,original,white,enabled&&source==D3DBLEND_ONE&&dest==D3DBLEND_INVSRCALPHA?1:0);
        assert(expected&&(accepted||expected!=particle));
        const HRESULT result=color.draw(d,white,[&]{
            IDirect3DPixelShader9* bound=nullptr;ok(d->GetPixelShader(&bound));assert(bound==expected);bound->Release();
            DWORD value=99;ok(d->GetRenderState(D3DRS_SRGBWRITEENABLE,&value));assert(!value);
            return E_FAIL; // Even a failed submission must restore the native draw state.
        },family);assert(result==E_FAIL);
        IDirect3DPixelShader9* bound=nullptr;ok(d->GetPixelShader(&bound));assert(bound==original);bound->Release();
        DWORD value=99;ok(d->GetRenderState(D3DRS_SRCBLEND,&value));assert(value==source);
        ok(d->GetRenderState(D3DRS_DESTBLEND,&value));assert(value==dest);
        ok(d->GetRenderState(D3DRS_BLENDOP,&value));assert(value==op);
        ok(d->GetRenderState(D3DRS_SRGBWRITEENABLE,&value));assert(value==srgb);++cases;
    }
    // A shader with RGB > 1 but without the particle proof must stay clamped.
    const DWORD unknown[]={native.words[0],0x02000001,0x800f0800,0xa0e40000,0xffff};IDirect3DPixelShader9* other=nullptr;
    ok(d->CreatePixelShader(unknown,&other));assert(!color.replacement(d,other,80,3));ok(d->SetPixelShader(other));
    ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_SRCALPHA));
    ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));
    auto* fallback=color.replacement(d,other,80,0);assert(fallback);
    ok(color.draw(d,80,[&]{IDirect3DPixelShader9* bound=nullptr;ok(d->GetPixelShader(&bound));assert(bound==fallback);bound->Release();return S_OK;},1));
    const unsigned built=color.patched;color.forget(original);assert(color.replacement(d,original,80,3)&&color.patched==built+1);
    ok(d->SetPixelShader(nullptr));other->Release();original->Release();
    std::printf("{\"kind\":\"admission\",\"major\":%u,\"cases\":%u,\"unknown_rejected\":true,\"cache_isolated\":true,\"state_restored\":true}\n",unsigned(native.words[0]>>8)&255,cases);
}
static double timed(IDirect3DDevice9* d,const std::function<void()>& frame){
    IDirect3DQuery9 *a=nullptr,*b=nullptr,*freq=nullptr,*disjoint=nullptr,*event=nullptr;
    ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&a));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&b));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&freq));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));
    for(unsigned i=0;i<4;++i)frame();wait(event);
    constexpr unsigned repeats=96;
    ok(disjoint->Issue(D3DISSUE_BEGIN));ok(a->Issue(D3DISSUE_END));for(unsigned i=0;i<repeats;++i)frame();ok(b->Issue(D3DISSUE_END));ok(freq->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
    UINT64 start=0,end=0,hz=0;BOOL bad=TRUE;ok(a->GetData(&start,sizeof start,0));ok(b->GetData(&end,sizeof end,0));ok(freq->GetData(&hz,sizeof hz,0));ok(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&hz&&end>start);
    for(auto* q:{a,b,freq,disjoint,event})q->Release();return double(end-start)*1000/hz/repeats;
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    assert(argc==3);HWND window=CreateWindowExW(0,L"STATIC",L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);assert(window);
    HMODULE module=LoadLibraryA(argv[1]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(reinterpret_cast<void*>(GetProcAddress(module,"Direct3DCreate9")));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    D3DADAPTER_IDENTIFIER9 adapter{};ok(api->GetAdapterIdentifier(0,0,&adapter));std::printf("{\"kind\":\"adapter\",\"name\":\"%s\"}\n",adapter.Description);
    scalarChecks(d);
    ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(kFixtureVs2),&fixtureVs[0]));ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(kFixtureVs3),&fixtureVs[1]));
    const D3DVERTEXELEMENT9 declaration[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},{0,20,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    ok(d->CreateVertexDeclaration(declaration,&fixtureDecl));
    auto* spriteTexture=texture(d,image(argv[2]));
    unsigned nativeCases=0;const NativeShader* selected[2]{};
    for(const auto& native:kNative){
        NorthlightParticleLightProof::Pixel proof;
        if(!NorthlightParticleLightProof::compilePixel(native.words,native.count,proof))continue;
        const unsigned major=(native.words[0]>>8)&255;
        fixtureModel=major==3;
        Device owner{d};IDirect3DPixelShader9* original=nullptr;
        ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(native.words),&original));
        Target target(d,128,72);Sprites sprites(d,128,72,12);std::vector<float> pixels[2];
        for(float fog:(proof.fog?std::vector<float>{0,.25f,.5f,1}:std::vector<float>{1}))for(auto blend:{D3DBLEND_ONE,D3DBLEND_SRCALPHA}){
        for(unsigned m=0;m<2;++m){render(d,target,sprites,spriteTexture,original,blend,fog,&owner,m);pixels[m]=target.pixels(d);}
        double maxDiff=0,maxRgb[2]={};unsigned alphaChanges=0,brighter=0;
        for(size_t i=0;i<pixels[0].size();++i){assert(std::isfinite(pixels[0][i])&&std::isfinite(pixels[1][i]));
            if(i%4==3){alphaChanges+=pixels[0][i]!=pixels[1][i];continue;}
            maxDiff=std::max(maxDiff,double(std::abs(pixels[1][i]-pixels[0][i])));brighter+=pixels[1][i]>pixels[0][i]+.001f;
            for(unsigned m=0;m<2;++m)maxRgb[m]=std::max(maxRgb[m],double(pixels[m][i]));
        }
        assert(!alphaChanges);if(proof.rgbScale*fog<=1)assert(maxDiff<.002);else{assert(brighter&&maxRgb[1]>maxRgb[0]);selected[major==3]=&native;}
        std::printf("{\"kind\":\"native\",\"name\":\"%s\",\"major\":%u,\"rgb_scale\":%g,\"fog\":%g,\"src_blend\":%u,\"max_diff\":%.9g,\"baseline_max\":%.9g,\"candidate_max\":%.9g,\"brighter_channels\":%u,\"alpha_changes\":%u}\n",native.name,major,proof.rgbScale,fog,unsigned(blend),maxDiff,maxRgb[0],maxRgb[1],brighter,alphaChanges);++nativeCases;
        }
        original->Release();
    }
    assert(selected[0]&&selected[1]);
    for(unsigned model=0;model<2;++model){
        fixtureModel=model;
        const auto& native=*selected[model];admissionChecks(d,native);Device owner{d};IDirect3DPixelShader9* original=nullptr;
        ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(native.words),&original));
        for(unsigned m=0;m<2;++m){NorthlightHdrUi::Info info;auto* shader=patched(d,native.words,native.count,m,&info);shader->Release();std::printf("{\"kind\":\"shader_cost\",\"major\":%u,\"candidate\":%u,\"native_slots\":%u,\"transfer_slots\":%u}\n",model+2,m,info.originalSlots,info.addedSlots);}
        {
            Target target(d,128,72);Sprites sprites(d,128,72,12);
            render(d,target,sprites,spriteTexture,original);const auto expected=target.pixels(d);
            for(unsigned gate=0;gate<3;++gate){
                owner.hdrSceneReady=gate!=1;owner.backTarget=gate!=2;
                render(d,target,sprites,spriteTexture,original,D3DBLEND_SRCALPHA,1,&owner,1,gate!=0);
                assert(target.pixels(d)==expected);
            }
            owner.hdrSceneReady=owner.backTarget=true;
            std::printf("{\"kind\":\"replay_gates\",\"major\":%u,\"cases\":3,\"early_and_sdr_unchanged\":true}\n",model+2);
        }
        for(unsigned samples:{0u,4u}){
            Target scene(d,960,540,D3DMULTISAMPLE_TYPE(samples)),output(d,960,540);Sprites sprites(d,960,540,12);
            for(unsigned m=0;m<2;++m){
                render(d,scene,sprites,spriteTexture,original,D3DBLEND_SRCALPHA,1,&owner,m);const std::string stem="sm"+std::to_string(model+2)+"-msaa"+std::to_string(samples)+(m?"-candidate":"-baseline");scene.pixels(d,stem+"-linear.rgba16f");
                for(unsigned bloom:{0u,1u}){NorthlightHdr::Frame post;NorthlightHdr::Settings settings;settings.autoExposure=false;settings.bloom=float(bloom);
                    ok(post.ensure(d,960,540));ok(d->BeginScene());ok(post.finish(d,scene.surface,output.surface,settings,1.f/60,true,true));ok(d->EndScene());output.pixels(d,stem+"-bloom"+std::to_string(bloom)+"-output.rgba16f");
                }
            }
        }
        for(const auto size:{std::pair<unsigned,unsigned>{1920,1080},{3840,2160}}){
            Target scene(d,size.first,size.second),output(d,size.first,size.second);Sprites sprites(d,size.first,size.second,64);
            NorthlightHdr::Frame post;ok(post.ensure(d,size.first,size.second));NorthlightHdr::Settings settings;settings.autoExposure=true;
            for(unsigned full=0;full<2;++full){std::vector<double> times[2];
                for(unsigned block=0;block<32;++block){const unsigned m=(block/2)%2?1-block%2:block%2;
                    const double ms=timed(d,[&]{render(d,scene,sprites,spriteTexture,original,D3DBLEND_SRCALPHA,1,&owner,m);if(full){ok(d->BeginScene());ok(post.finish(d,scene.surface,output.surface,settings,1.f/60,true,true));ok(d->EndScene());}});
                    if(block>=8)times[m].push_back(ms);
                }
                for(unsigned m=0;m<2;++m){std::printf("{\"kind\":\"timing\",\"major\":%u,\"width\":%u,\"height\":%u,\"sprites\":64,\"draws\":1,\"candidate\":%u,\"with_post\":%u,\"gpu_ms\":[",model+2,size.first,size.second,m,full);
                    for(size_t i=0;i<times[m].size();++i)std::printf("%s%.9f",i?",":"",times[m][i]);std::puts("]}");std::fflush(stdout);
                }
            }
        }
        original->Release();
    }
    std::printf("{\"kind\":\"pass\",\"native_cases\":%u,\"game_launched\":false,\"client_modified\":false}\n",nativeCases);
    ok(d->SetTexture(0,nullptr));ok(d->SetStreamSource(0,nullptr,0,0));spriteTexture->Release();fixtureDecl->Release();for(auto* vs:fixtureVs)vs->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);
}
