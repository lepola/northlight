// Rebuild static probe residency, then replay the GI delta against captured buffers.
// No game, dynamic-probe history, or stencil-mask reconstruction.
#define main northlight_unused_composite_replay
#include "composite_distance_gpu.cpp"
#undef main
#include "world_probe_progress.h"
#include "probe_texture_upload.h"
#include <filesystem>
#include <chrono>

static Image floats(unsigned w,unsigned h,const std::vector<float>& values){
    Image im{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(values.size()*4)};
    assert(values.size()==size_t(w)*h*4);std::memcpy(im.pixels.data(),values.data(),im.pixels.size());return im;
}
static Image solid(float x,float y=0,float z=0,float w=0){return floats(1,1,{x,y,z,w});}
static std::vector<float> unpack(const Image& im){
    assert(im.format==D3DFMT_A16B16G16R16F);std::vector<float> out(size_t(im.width)*im.height*4);
    for(size_t i=0;i<out.size();++i){uint16_t h;std::memcpy(&h,im.pixels.data()+i*2,2);out[i]=half(h);}return out;
}
static void save(const std::string& path,const Image& im){
    const uint32_t header[]={0x31524746,im.width,im.height,im.format,im.bytes};
    std::ofstream f(path,std::ios::binary);f.write(reinterpret_cast<const char*>(header),sizeof header);f.write(reinterpret_cast<const char*>(im.pixels.data()),im.pixels.size());assert(f.good());
}
int main(int argc,char** argv){
    assert(argc==7);const std::string capture=argv[1],cache=argv[2],oldShaders=argv[3],newShaders=argv[4],output=argv[6];
    auto bank=constantBank(read(capture+"constants.f32"));auto& c=bank.values;
    const unsigned W=unsigned(c[33][0]),H=unsigned(c[33][1]),w=W/2,h=H/2;
    assert(bank.count==79&&c[73][2]==1&&c[16][3]==0&&c[30][0]==0&&c[77][0]==0&&c[77][1]==0&&c[77][2]==0);
    // Same grid/solver as production, settled static geometry. The capture did
    // not export the original probe atlas, so this is an explicit reconstruction.
    using namespace NorthlightGI;
    assert(configureProbeLayout(probeLayoutFor(unsigned(c[20][0])-2)));
    const Vec3 camera(c[15][0],c[15][1],c[15][2]);
    const int tx=int(std::floor((17066.6666667-camera.y)/533.3333333)),ty=int(std::floor((17066.6666667-camera.x)/533.3333333));
    std::vector<std::string> tiles;std::ofstream tileList(output+"/tiles.txt");
    for(int y=ty-1;y<=ty+1;++y)for(int x=tx-1;x<=tx+1;++x){const auto path=cache+"/Kalimdor/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";if(std::filesystem::exists(path)){tiles.push_back(path);tileList<<path<<'\n';}}
    assert(!tiles.empty());BVH bvh;LocalSceneCache sceneCache;std::string error;
    if(!sceneCache.build(tiles,cache+"/models","Kalimdor",camera-Vec3(288,288,320),camera+Vec3(288,288,320),bvh,error)){std::fprintf(stderr,"%s\n",error.c_str());return 1;}
    Lighting light;light.sunDirection={c[16][0],c[16][1],c[16][2]};light.sunIrradiance=Vec3(c[76][0],c[76][1],c[76][2])*3.14159265f;light.skyRadiance={c[18][0],c[18][1],c[18][2]};light.maxBounces=3;
    const auto prepared=prepareLighting(light);assert(prepared.valid());const auto origin=probeWindowOrigin(camera);const auto& layout=probeLayout();
    std::vector<ProbeAtlasEntry> atlas(layout.atlasSize());unsigned valid=0;
    auto started=std::chrono::steady_clock::now();
    for(unsigned i=0;i<layout.count();++i){const auto position=probeWindowPosition(origin,i);ProbeGridKey key;assert(probeGridKey(position,key));auto probe=solveProbePrepared(bvh,position,prepared,64,probeSeed(key));valid+=probe.valid;atlas[probeAtlasIndex(key)]={key,probe,true};}
    std::printf("STATIC_PROBES count=%u valid=%u triangles=%zu solveSeconds=%.3f dynamicActors=0 retainedHistory=0\n",layout.count(),valid,bvh.triangleCount(),std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count());std::fflush(stdout);
    NorthlightProbeBlend::Mirror blend;NorthlightProbeActivation activation;blend.begin("Kalimdor");activation.begin("Kalimdor");
    std::vector<Image> probes;for(unsigned channel=0;channel<5;++channel){const unsigned height=layout.atlas*(channel==3?6:1);auto im=floats(layout.atlas*layout.atlas,height,std::vector<float>(size_t(layout.atlas)*layout.atlas*height*4));NorthlightProbeUpload::write(channel,im.pixels.data(),size_t(im.width)*16,layout.atlas,atlas,blend,activation,0,false,true);save(output+"/probe-"+std::to_string(channel)+".fgr",im);probes.push_back(std::move(im));}
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightCapturedGI";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
    auto window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,4,4,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
    auto module=LoadLibraryA(argv[5]);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);auto* api=create(D3D_SDK_VERSION);assert(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=4;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;
    IDirect3DDevice9* d=nullptr;ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
    D3DADAPTER_IDENTIFIER9 adapter{};ok(api->GetAdapterIdentifier(0,0,&adapter));std::printf("GPU %s backend=%s\n",adapter.Description,argv[5]);
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
    for(auto k:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHATESTENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_FOGENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_SCISSORTESTENABLE})ok(d->SetRenderState(k,FALSE));
    ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
    IDirect3DTexture9* textures[16]{};std::vector<IDirect3DPixelShader9*> shaders;
    auto bind=[&](unsigned slot,const Image& im){ok(d->SetTexture(slot,nullptr));if(textures[slot])textures[slot]->Release();textures[slot]=texture(d,im);ok(d->SetTexture(slot,textures[slot]));};
    auto captured=[&](const char* name){return image(capture+name+".fgr");};
    auto shader=[&](const std::string& path){auto bytes=read(path);IDirect3DPixelShader9* ps=nullptr;ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(bytes.data()),&ps));shaders.push_back(ps);return ps;};
    auto* oldGI=shader(oldShaders+"/WorldGI.bin");auto* newGI=shader(newShaders+"/WorldGI.bin");auto* smooth=shader(newShaders+"/RemovalSmooth.bin");auto* composite=shader(newShaders+"/WorldComposite.bin");auto* material=shader(newShaders+"/MaterialComposite.bin");
    for(unsigned s=0;s<16;++s){for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto k:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,k,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(s,D3DSAMP_SRGBTEXTURE,FALSE));}
    auto render=[&](IDirect3DPixelShader9* ps,unsigned width,unsigned height,D3DFORMAT format=D3DFMT_A16B16G16R16F){
        const unsigned bytes=format==D3DFMT_A16B16G16R16F?8:4;
        IDirect3DSurface9 *target=nullptr,*staging=nullptr;ok(d->CreateRenderTarget(width,height,format,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(width,height,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));ok(d->SetRenderTarget(0,target));
        const Vertex quad[]={{-.5f,-.5f,0,1,0,0},{float(width)-.5f,-.5f,0,1,1,0},{-.5f,float(height)-.5f,0,1,0,1},{float(width)-.5f,float(height)-.5f,0,1,1,1}};
        ok(d->SetPixelShaderConstantF(0,c[0],bank.count));ok(d->SetPixelShader(ps));ok(d->BeginScene());ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,quad,sizeof(Vertex)));ok(d->EndScene());ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));Image im{width,height,format,bytes,std::vector<unsigned char>(size_t(width)*height*bytes)};for(unsigned y=0;y<height;++y)std::memcpy(im.pixels.data()+size_t(y)*width*bytes,static_cast<char*>(lock.pBits)+y*lock.Pitch,width*bytes);ok(staging->UnlockRect());staging->Release();target->Release();return im;
    };
    bind(1,captured("depth"));bind(14,captured("normals"));bind(11,solid(0));bind(8,solid(0));
    auto baseline=captured("baseline");auto base=unpack(baseline);auto distances=captured("distance");
    // Old capture encoded unknown visibility as zero; upgrade only those texels
    // proven outside the captured first-source far matrix to the new -1 marker.
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){const size_t i=size_t(y)*w+x;float z;std::memcpy(&z,distances.pixels.data()+i*4,4);const float view[]={((float(2*x+1)/W)*2-1)*z/c[1][0],(1-(float(2*y+1)/H)*2)*z/c[1][1],c[1][2]*z};float world[3],q[3];for(unsigned k=0;k<3;++k)world[k]=c[6][k]+view[0]*c[3][k]+view[1]*c[4][k]+view[2]*c[5][k];for(unsigned k=0;k<3;++k)q[k]=c[14][k]+world[0]*c[11][k]+world[1]*c[12][k]+world[2]*c[13][k];if(std::max(std::fabs(q[0]),std::fabs(q[1]))>1||q[2]<0||q[2]>1)base[i*4+3]=-1;}
    bind(12,floats(w,h,base));for(unsigned i=0;i<5;++i)bind(i==4?10:4+i,probes[i]);
    const auto before=unpack(render(oldGI,w,h)),after=unpack(render(newGI,w,h));
    auto raw=unpack(captured("light")),changed=raw;size_t changedTexels=0;
    for(size_t i=0;i<raw.size();i+=4){bool different=false;for(unsigned k=0;k<3;++k){const float delta=after[i+k]-before[i+k];assert(std::isfinite(delta)&&delta<=.001f);changed[i+k]+=delta;different|=delta!=0;}changedTexels+=different;}
    bind(0,captured("scene"));bind(10,captured("lighting-input-g3-s10"));bind(15,distances);
    for(unsigned s:{0u,10u})for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_LINEAR));
    bind(8,floats(w,h,raw));const auto oldSmooth=unpack(render(smooth,w,h));bind(8,floats(w,h,changed));const auto newSmooth=unpack(render(smooth,w,h));
    auto temporal=unpack(captured("temporal-light")),newTemporal=temporal;
    for(size_t i=0;i<temporal.size();i+=4)for(unsigned k=0;k<3;++k)newTemporal[i+k]+=newSmooth[i+k]-oldSmooth[i+k];
    // Preserve captured history; apply the current-frame filtered delta equally
    // to both comparison inputs. No animation/history convergence is claimed.
    const auto colorFormat=D3DFORMAT(captured("scene").format);assert(colorFormat==D3DFMT_X8R8G8B8||colorFormat==D3DFMT_A8R8G8B8);
    for(unsigned mode=0;mode<2;++mode){
        bind(0,captured("scene"));bind(2,solid(0));bind(3,captured("current-local-light"));bind(8,floats(w,h,mode?newTemporal:temporal));bind(9,captured("lighting-input-g3-s4"));bind(13,solid(0));bind(14,captured("normals"));
        for(unsigned s:{0u,9u,10u})for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,k,D3DTEXF_LINEAR));
        for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(12,k,D3DTEXF_POINT));
        const auto color=render(composite,W,H,colorFormat);bind(0,color);bind(4,captured("lighting-input-g3-s4"));bind(5,captured("scene"));bind(6,captured("material-occlusion"));bind(9,captured("material-height"));
        for(auto k:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER}){ok(d->SetSamplerState(4,k,D3DTEXF_LINEAR));ok(d->SetSamplerState(5,k,D3DTEXF_LINEAR));ok(d->SetSamplerState(9,k,D3DTEXF_POINT));ok(d->SetSamplerState(12,k,D3DTEXF_LINEAR));}
        auto result=render(material,W,H,colorFormat);save(output+(mode?"/new.fgr":"/old.fgr"),result);
    }
    std::printf("GI_CAPTURE_REPLAY_PASS changedHalfTexels=%zu fullWidth=%u fullHeight=%u staticProbeReconstruction=1 capturedHistoryPlusCurrentDelta=1 stencilExported=0\n",changedTexels,W,H);
    for(unsigned s=0;s<16;++s){d->SetTexture(s,nullptr);if(textures[s])textures[s]->Release();}for(auto* ps:shaders)ps->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(wc.lpszClassName,wc.hInstance);
}
