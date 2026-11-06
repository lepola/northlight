// Hidden-device production shader oracle. No game or installed renderer is loaded.
#define main unused_shadow_transition_replay
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
#include <chrono>
#include <functional>
#include "shadow_transition_math.h"
#include "shadow_transition_compiled_shaders.h"

using V3=std::array<float,3>;
using Constants=std::array<std::array<float,4>,120>;
static V3 point(V3 p,const Constants& c,unsigned m) {
    V3 q{};for(unsigned k=0;k<3;++k)q[k]=p[0]*c[m][k]+p[1]*c[m+1][k]+p[2]*c[m+2][k]+c[m+3][k];return q;
}
static float sat(float x){return std::max(0.f,std::min(1.f,x));}
static Image rgbaImage(unsigned w,unsigned h,const std::function<std::array<float,4>(unsigned,unsigned)>& f) {
    Image im{w,h,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(w*h*16)};
    for(unsigned y=0;y<h;++y)for(unsigned x=0;x<w;++x){auto p=f(x,y);std::memcpy(im.pixels.data()+(y*w+x)*16,p.data(),16);}return im;
}
struct Map {
    unsigned n=1024;std::vector<float> z;
    explicit Map(const std::function<float(float,float)>& f):z(n*n) {
        for(unsigned y=0;y<n;++y)for(unsigned x=0;x<n;++x)z[y*n+x]=f((x+.5f)/n,(y+.5f)/n);
    }
    float at(float u,float v)const {
        unsigned x=unsigned(std::max(0,std::min(int(n)-1,int(std::floor(u*n)))));
        unsigned y=unsigned(std::max(0,std::min(int(n)-1,int(std::floor(v*n)))));return z[y*n+x];
    }
    Image image()const {Image im{n,n,D3DFMT_R32F,4,std::vector<unsigned char>(n*n*4)};std::memcpy(im.pixels.data(),z.data(),n*n*4);return im;}
};

// Independent scalar comparison/filter oracle. Fixtures use a horizontal receiver
// and a vertical light, plus tilted normals and zero-confidence silhouettes.
static float dynamicAt(V3 q,const Map& map,const Constants& c) {
    const float u=q[0]*.5f+.5f+.5f*c[2][3],v=.5f-q[1]*.5f+.5f*c[2][3];
    return u<0||u>1||v<0||v>1||q[2]<=0||q[2]>=1?q[2]:map.at(u,v);
}
static float surfacePCF(V3 q,bool nearCascade,bool old,const Map& map,const Map& dynamic,const Constants& c,V3 gradient={0,0,0}) {
    const float t=c[2][3],u=q[0]*.5f+.5f+.5f*t,v=.5f-q[1]*.5f+.5f*t;
    if(u<.003f||u>.997f||v<.003f||v>.997f||q[2]<=0||q[2]>=1)return 1;
    const int size=nearCascade?5:4,start=nearCascade?-2:-1;
    const float pixelX=u/t-.5f,pixelY=v/t-.5f,bx=std::floor(pixelX+(nearCascade?.5f:0)),by=std::floor(pixelY+(nearCascade?.5f:0));
    const float fx=pixelX-bx,fy=pixelY-by,bias=c[24][0]*(nearCascade?1:2),receiver=q[2]-bias;
    auto stored=[&](float x,float y,float expected){
        float value=map.at(x,y);
        if(old){const unsigned m=nearCascade?90:94;auto d=point({q[0]+2*(x-u),q[1]-2*(y-v),expected+bias},c,m);
            value=std::min(value,expected+(dynamicAt(d,dynamic,c)-d[2]+bias)/c[m+2][2]);}
        return value;
    };
    float sum=0,total=0,gaps=0,blockers=0;
    for(int y=0;y<size;++y)for(int x=0;x<size;++x){
        const float tx=(bx+x+start+.5f)*t,ty=(by+y+start+.5f)*t;
        const float w=std::max(size*.5f-std::fabs(x+start-fx),0.f)*std::max(size*.5f-std::fabs(y+start-fy),0.f);
        const float expected=receiver+gradient[0]*(tx-u)+gradient[1]*(ty-v),depth=stored(tx,ty,expected);
        sum+=(expected<=depth?1.f:0.f)*w;total+=w;if(depth<expected){gaps+=expected-depth;++blockers;}
    }
    if(c[117][0]>.5f){
        for(int y=0;y<4;++y)for(int x=0;x<4;++x){
            const float tx=(std::floor(pixelX+(x-1.5f)*(4.25f/1.5f)+.5f)+.5f)*t;
            const float ty=(std::floor(pixelY+(y-1.5f)*(4.25f/1.5f)+.5f)+.5f)*t;
            const float expected=receiver+gradient[0]*(tx-u)+gradient[1]*(ty-v),depth=stored(tx,ty,expected);
            if(depth<expected){gaps+=expected-depth;++blockers;}
        }
        const float texelWorld=2*c[2][nearCascade?1:2]*t;
        const float reach=std::clamp(.04f*gaps/std::max(blockers,1.f)/(c[35][0]*texelWorld),size*.5f,4.25f);
        if(reach>size*.5f+.001f){sum=total=0;const float firstX=std::floor(pixelX-reach)+1,firstY=std::floor(pixelY-reach)+1;
            const int extent=int(std::floor(2*reach)+1);
            for(int y=0;y<extent&&y<9;++y)for(int x=0;x<extent&&x<9;++x){
                const float px=firstX+x,py=firstY+y,tx=(px+.5f)*t,ty=(py+.5f)*t;
                const float w=std::max(reach-std::fabs(px-pixelX),0.f)*std::max(reach-std::fabs(py-pixelY),0.f);
                const float expected=receiver+gradient[0]*(tx-u)+gradient[1]*(ty-v);
                sum+=(expected<=stored(tx,ty,expected)?1.f:0.f)*w;total+=w;
            }
        }
    }
    const float edge=sat((1-std::max(std::fabs(q[0]),std::fabs(q[1])))/.12f);
    return 1+(sum/std::max(total,.0001f)-1)*edge;
}
static float referenceDirect(V3 p,const Map& map,const Map& oldMap,const Map& dynamic,const Constants& c,const std::array<V3,4>* inputs=nullptr,const std::array<V3,2>* covers=nullptr) {
    const V3 n{c[116][0],c[116][1],c[116][2]};const float confidence=c[116][3];
    auto projected=[&](bool old,bool nearCascade,V3& gradient){
        const unsigned m=old?(nearCascade?80:84):(nearCascade?7:11),di=old?(nearCascade?88:98):(nearCascade?16:99);
        const float radius=c[2][nearCascade?1:2],facing=n[0]*c[di][0]+n[1]*c[di][1]+n[2]*c[di][2];
        const float lift=(1-sat(std::fabs(facing)))*confidence*2/1024;
        const float inverseSpan=old?c[100][nearCascade?2:3]:c[35][0];
        const float factor=confidence*2*inverseSpan/(facing<0?-std::max(std::fabs(facing),.15f):std::max(std::fabs(facing),.15f));
        const float limit=(nearCascade?2.688f:10.752f)*inverseSpan/c[2][3];
        gradient[0]=std::max(-limit,std::min(limit,(n[0]*c[m][0]+n[1]*c[m+1][0]+n[2]*c[m+2][0])*radius*factor*radius));
        gradient[1]=std::max(-limit,std::min(limit,-(n[0]*c[m][1]+n[1]*c[m+1][1]+n[2]*c[m+2][1])*radius*factor*radius));
        if(inputs)return (*inputs)[(old?2:0)+(nearCascade?0:1)];
        return point({p[0]+n[0]*lift*radius,p[1]+n[1]*lift*radius,p[2]+n[2]*lift*radius},c,m);
    };
    V3 g{};auto nn=projected(false,true,g),on=projected(true,true,g);
    const float oldBlend=sat((std::max(std::fabs(on[0]),std::fabs(on[1]))-.72f)/.18f),newBlend=sat((std::max(std::fabs(nn[0]),std::fabs(nn[1]))-.72f)/.18f);
    const float blend=oldBlend*(1-c[89][0])+newBlend*c[89][0];float result=0;
    for(unsigned cascade=0;cascade<2;++cascade){const bool nearCascade=cascade==0;V3 ng{},og{};auto nq=projected(false,nearCascade,ng),oq=projected(true,nearCascade,og);
        float nv=surfacePCF(nq,nearCascade,false,map,dynamic,c,ng),ov=surfacePCF(oq,nearCascade,true,oldMap,dynamic,c,og);
        auto cover=covers?(*covers)[nearCascade?0:1]:point(p,c,nearCascade?80:84);if(std::max({std::fabs(cover[0]),std::fabs(cover[1]),std::fabs(2*cover[2]-1)})>.99f)ov=nv;
        const float f=c[89][cascade];result+=(ov*(1-f)+nv*f)*(nearCascade?1-blend:blend);
    }return result;
}

// Enumerate one-UV-ULP coordinate uncertainty, not an output tolerance.
// The native SM3 affine/RCP paths can round an independently compiled probe
// differently. Every corner still runs the independent comparison/filter oracle.
static std::pair<float,float> queryEnvelope(V3 p,const Map& map,const Map& oldMap,const Map& dynamic,const Constants& c,const std::array<V3,4>& query,const std::array<V3,2>& covers){
    std::array<V3,4> delta{};
    for(unsigned a=0;a<4;++a){
        const float uv[]={query[a][0]*.5f+.5f+.5f*c[2][3],.5f-query[a][1]*.5f+.5f*c[2][3]};
        for(unsigned k=0;k<2;++k)delta[a][k]=2*std::max(uv[k]-std::nextafter(uv[k],-INFINITY),std::nextafter(uv[k],INFINITY)-uv[k]);
        delta[a][2]=std::max(query[a][2]-std::nextafter(query[a][2],-INFINITY),std::nextafter(query[a][2],INFINITY)-query[a][2]);
    }
    float lo=1,hi=0;
    for(unsigned bits=0;bits<(1u<<12);++bits){auto shifted=query;
        for(unsigned a=0;a<4;++a)for(unsigned k=0;k<3;++k)shifted[a][k]+=(bits&(1u<<(a*3+k))?1:-1)*delta[a][k];
        const float value=referenceDirect(p,map,oldMap,dynamic,c,&shifted,&covers);lo=std::min(lo,value);hi=std::max(hi,value);
    }
    return {lo,hi};
}

struct GPU {
    HWND window{};HMODULE module{};IDirect3D9* api{};IDirect3DDevice9* d{};
    IDirect3DQuery9 *event{},*start{},*end{},*frequency{},*disjoint{};
    std::vector<IDirect3DTexture9*> owned;
    std::vector<IDirect3DPixelShader9*> shaders;
    GPU(const char* backend) {
        WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"NorthlightShadowTransition";wc.hInstance=GetModuleHandleW(nullptr);assert(RegisterClassW(&wc));
        window=CreateWindowExW(0,wc.lpszClassName,L"",WS_OVERLAPPED,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);assert(window);
        module=LoadLibraryA(backend);assert(module);auto create=reinterpret_cast<IDirect3D9*(WINAPI*)(UINT)>(GetProcAddress(module,"Direct3DCreate9"));assert(create);api=create(D3D_SDK_VERSION);assert(api);
        D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        ok(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d));
        ok(d->SetDepthStencilSurface(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));
        for(auto state:{D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ALPHABLENDENABLE,D3DRS_ALPHATESTENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_FOGENABLE})ok(d->SetRenderState(state,FALSE));ok(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE));
        for(unsigned s=0;s<16;++s){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(s,state,D3DTEXF_POINT));ok(d->SetSamplerState(s,D3DSAMP_MIPFILTER,D3DTEXF_NONE));for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(d->SetSamplerState(s,state,D3DTADDRESS_CLAMP));}
        ok(d->CreateQuery(D3DQUERYTYPE_EVENT,&event));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&start));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMP,&end));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPFREQ,&frequency));ok(d->CreateQuery(D3DQUERYTYPE_TIMESTAMPDISJOINT,&disjoint));
    }
    IDirect3DPixelShader9* shader(const std::string& path){auto code=read(path);IDirect3DPixelShader9* ps{};ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&ps));shaders.push_back(ps);return ps;}
    IDirect3DTexture9* tex(const Image& image){auto t=texture(d,image);owned.push_back(t);return t;}
    void bind(unsigned slot,IDirect3DBaseTexture9* t){ok(d->SetTexture(slot,t));}
    void draw(unsigned w,unsigned h,unsigned count=1){const Vertex q[]={{-.5f,-.5f,0,1,0,0},{float(w)-.5f,-.5f,0,1,1,0},{-.5f,float(h)-.5f,0,1,0,1},{float(w)-.5f,float(h)-.5f,0,1,1,1}};ok(d->BeginScene());for(unsigned i=0;i<count;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));ok(d->EndScene());}
    std::vector<float> capture(IDirect3DPixelShader9* ps,const Constants& c,unsigned w,unsigned h,unsigned output=0,D3DFORMAT format=D3DFMT_A32B32G32R32F){
        IDirect3DSurface9 *target{},*second{},*staging{};ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
        ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_NONE,0,FALSE,&second,nullptr));ok(d->CreateOffscreenPlainSurface(w,h,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        ok(d->SetRenderTarget(0,target));ok(d->SetRenderTarget(1,second));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));D3DVIEWPORT9 vp{0,0,w,h,0,1};ok(d->SetViewport(&vp));ok(d->SetPixelShaderConstantF(0,c[0].data(),unsigned(c.size())));ok(d->SetPixelShader(ps));draw(w,h);
        ok(d->GetRenderTargetData(output?second:target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> out(w*h*4);for(unsigned y=0;y<h;++y){if(format==D3DFMT_A16B16G16R16F){const auto* row=reinterpret_cast<const unsigned short*>(static_cast<char*>(lock.pBits)+y*lock.Pitch);for(unsigned x=0;x<w*4;++x)out[y*w*4+x]=half(row[x]);}else std::memcpy(out.data()+y*w*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,w*16);}ok(staging->UnlockRect());
        ok(d->SetRenderTarget(1,nullptr));target->Release();second->Release();staging->Release();return out;
    }
    std::vector<double> time(IDirect3DPixelShader9* ps,const Constants& c,unsigned w,unsigned h,D3DFORMAT format=D3DFMT_A16B16G16R16F){
        IDirect3DSurface9* target{};ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp{0,0,w,h,0,1};ok(d->SetViewport(&vp));ok(d->SetPixelShaderConstantF(0,c[0].data(),unsigned(c.size())));ok(d->SetPixelShader(ps));draw(w,h,3);wait(event);
        std::vector<double> times;
        for(unsigned block=0;block<5;++block){ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));draw(w,h,4);ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);UINT64 a{},b{},hz{};BOOL bad{};ok(start->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(frequency->GetData(&hz,sizeof hz,0));ok(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&b>=a&&hz);times.push_back(double(b-a)/hz*1000/4);}
        target->Release();return times;
    }
    std::vector<float> combined(IDirect3DPixelShader9* current,IDirect3DPixelShader9* previous,const Constants& c,unsigned w,unsigned h,D3DFORMAT format=D3DFMT_A32B32G32R32F){
        IDirect3DSurface9 *target{},*staging{};ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->CreateOffscreenPlainSurface(w,h,format,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        ok(d->SetRenderTarget(0,target));ok(d->SetRenderTarget(1,nullptr));D3DVIEWPORT9 vp{0,0,w,h,0,1};ok(d->SetViewport(&vp));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,7));
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetPixelShaderConstantF(0,c[0].data(),unsigned(c.size())));ok(d->SetPixelShader(current));draw(w,h);
        Constants before=c;for(unsigned row=0;row<4;++row){before[7+row]=c[80+row];before[11+row]=c[84+row];}before[16]=c[88];before[99]=c[98];
        ok(d->SetPixelShaderConstantF(0,before[0].data(),unsigned(before.size())));ok(d->SetPixelShader(previous));ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,TRUE));ok(d->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ONE));ok(d->SetRenderState(D3DRS_BLENDOP,D3DBLENDOP_ADD));draw(w,h);
        ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));
        ok(d->GetRenderTargetData(target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> out(w*h*4);for(unsigned y=0;y<h;++y){if(format==D3DFMT_A16B16G16R16F){const auto* row=reinterpret_cast<const unsigned short*>(static_cast<char*>(lock.pBits)+y*lock.Pitch);for(unsigned x=0;x<w*4;++x)out[y*w*4+x]=half(row[x]);}else std::memcpy(out.data()+y*w*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,w*16);}ok(staging->UnlockRect());target->Release();staging->Release();return out;
    }
    ~GPU(){d->SetPixelShader(nullptr);for(unsigned s=0;s<16;++s)d->SetTexture(s,nullptr);for(auto t:owned)t->Release();for(auto s:shaders)s->Release();for(auto q:{event,start,end,frequency,disjoint})q->Release();d->Release();api->Release();FreeLibrary(module);DestroyWindow(window);UnregisterClassW(L"NorthlightShadowTransition",GetModuleHandleW(nullptr));}
};

static void matrices(Constants& c,float angle,float shift){
    for(unsigned m:{7u,11u,80u,84u,90u,94u})for(unsigned j=0;j<4;++j)c[m+j]={0,0,0,0};
    const float cs=std::cos(angle),sn=std::sin(angle);
    for(unsigned pair=0;pair<2;++pair){const unsigned cur=pair?11:7,old=pair?84:80,trans=pair?94:90;const float radius=c[2][pair?2:1];
        c[cur][0]=1/radius;c[cur+1][1]=1/radius;c[cur+2][2]=1.f/1280;c[cur+3][2]=.5f;c[cur+3][3]=1;
        c[old][0]=cs/radius;c[old][1]=sn/radius;c[old+1][0]=-sn/radius;c[old+1][1]=cs/radius;c[old+2][2]=1.f/1280;c[old+3][0]=shift;c[old+3][2]=.5f;c[old+3][3]=1;
        c[trans][0]=cs;c[trans][1]=-sn;c[trans+1][0]=sn;c[trans+1][1]=cs;c[trans+2][2]=1;c[trans+3][0]=-shift*cs;c[trans+3][1]=shift*sn;c[trans+3][3]=1;
    }
}
static Constants constants(unsigned fw,unsigned fh){
    Constants c{};c[0]={1.f/fw,1.f/fh,.2f,1000};c[1]={1.3f,1.9f,1,0};c[2]={1,48,192,1.f/1024};
    c[3][0]=c[4][2]=c[5][1]=c[6][3]=1;c[6][2]=10;c[15][2]=10;
    c[16]={0,0,1,0};c[17]={.8f,.7f,.6f,0};c[18]={.2f,.23f,.3f,1};c[20][2]=1;
    c[21]={1,1,.5f,128};c[22]={1,1,1,.002f};c[24][0]=.00004f;c[25]={0,1,1,1};c[26]={.3f,.4f,.5f,1};c[27]={1,1,0,1};c[28][2]=1;c[29]={.8f,.7f,.6f,0};
    c[31]={-256,-256,1.f/512,.7f};c[32]={.01f,.004f,30,18};c[33]={float(fw),float(fh),float(fw/2),float(fh/2)};c[35][0]=1.f/1280;
    c[58][1]=1.f/12;c[59]={0,-10,0,.0017f};c[60]={0,.37f,.23f,.13f};c[61]={.3125f,.52f,.27f,.64f};c[62]={22,-1.25f,32,.06f};c[63]={.875f,1.f/64,1.f/16,1.625f};
    c[88]=c[98]=c[99]=c[16];c[100]={c[24][0],c[24][0],c[35][0],c[35][0]};c[102]={73.13f,0,1,0};c[103]={0,0,1,0};matrices(c,0,0);return c;
}
static void productionMatrices(Constants& c,unsigned scene){
    namespace WM=NorthlightWorldMath;using NorthlightGI::Vec3;
    const float pi=3.14159265358979323846f;
    const float elevation=(scene==0||scene==4)?21.7f*pi/180:scene==1?2.f*pi/180:scene==2?31.f*pi/180:std::asin(.95f)-.00025f;
    const float azimuth=scene==2?-1.4f:.7f;
    auto direction=[](float e,float a){return Vec3(std::cos(e)*std::cos(a),std::cos(e)*std::sin(a),std::sin(e));};
    const Vec3 oldDirection=direction(elevation,azimuth),newDirection=direction(elevation+.0005f,azimuth+.00015f);
    const Vec3 oldCenter{12.25f,-9.75f,7.5f},newCenter=oldCenter+Vec3(.21f,-.17f,.09f);
    auto matrix=[&](unsigned dst,const float* m){for(unsigned row=0;row<4;++row)for(unsigned k=0;k<4;++k)c[dst+row][k]=m[row*4+k];};
    for(unsigned cascade=0;cascade<2;++cascade){const float radius=cascade?192.f:48.f;
        const auto old=WM::shadowFrame(oldCenter,scene==4&&cascade?direction(elevation+.00035f,azimuth+.00011f):oldDirection,radius),current=WM::shadowFrame(newCenter,newDirection,radius);
        float om[16],cm[16],reproject[16];WM::shadowMatrixFrom(old,radius,om);WM::shadowMatrixFrom(current,radius,cm);ShadowTransitionMath::reproject(old,cm,radius,reproject);
        matrix(cascade?84:80,om);matrix(cascade?11:7,cm);matrix(cascade?94:90,reproject);
        if(cascade){float inverse[16];ShadowTransitionMath::reproject(current,om,radius,inverse);matrix(101,inverse);}
        c[cascade?98:88]={old.z.x,old.z.y,old.z.z,0};c[cascade?99:16]={current.z.x,current.z.y,current.z.z,scene==2?1.f:0.f};
    }
    c[24][0]=WM::ShadowBiasWorld*WM::InverseShadowDepth;c[35][0]=WM::InverseShadowDepth;
    c[100]={c[24][0],c[24][0],c[35][0],c[35][0]};
}

static void retainCurrent(Constants& c){for(unsigned row=0;row<4;++row){c[105+row]=c[7+row];c[109+row]=c[11+row];}c[113]=c[16];c[114]=c[99];}
static unsigned redirectedDynamic(GPU& gpu,const std::string& dir,IDirect3DTexture9* white){
    auto* d=gpu.d;constexpr unsigned N=1024;
    IDirect3DTexture9 *dynamic=nullptr,*united=nullptr;IDirect3DSurface9 *dynamicSurface=nullptr,*unionSurface=nullptr,*depth=nullptr,*staging=nullptr,*saved=nullptr;
    ok(d->GetRenderTarget(0,&saved));
    for(auto entry:{std::make_pair(&dynamic,&dynamicSurface),std::make_pair(&united,&unionSurface)}){
        ok(d->CreateTexture(N,N,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,entry.first,nullptr));ok((*entry.first)->GetSurfaceLevel(0,entry.second));
    }
    ok(d->CreateDepthStencilSurface(N,N,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&depth,nullptr));
    ok(d->CreateOffscreenPlainSurface(N,N,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
    auto code=read(dir+"/ShadowVS.bin");IDirect3DVertexShader9* vs=nullptr;ok(d->CreateVertexShader(reinterpret_cast<const DWORD*>(code.data()),&vs));
    auto* caster=gpu.shader(dir+"/ShadowPS.bin");auto* combine=gpu.shader(dir+"/ShadowUnion.bin");
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(1,nullptr));ok(d->SetRenderTarget(0,dynamicSurface));ok(d->SetDepthStencilSurface(depth));
    D3DVIEWPORT9 vp{0,0,N,N,0,1};ok(d->SetViewport(&vp));ok(d->SetRenderState(D3DRS_ZENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,TRUE));ok(d->SetRenderState(D3DRS_ZFUNC,D3DCMP_LESS));
    ok(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE,15));ok(d->SetVertexShader(vs));ok(d->SetPixelShader(caster));ok(d->SetFVF(D3DFVF_XYZ|D3DFVF_NORMAL|D3DFVF_TEX1));ok(d->SetTexture(0,white));
    const float identity[]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1},material[]={1,1,1,-1};ok(d->SetVertexShaderConstantF(0,identity,4));ok(d->SetPixelShaderConstantF(0,material,1));
    struct Caster{float x,y,z,nx,ny,nz,u,v;};const Caster q[]={{-.6f,.8f,.35f,0,0,1,0,0},{-.1f,.8f,.35f,0,0,1,1,0},{-.6f,-.8f,.35f,0,0,1,0,1},{-.1f,-.8f,.35f,0,0,1,1,1}};
    // Match the game path: existing caster draws target the selected R32F
    // dynamic map, then the union samples it inside the SAME BeginScene.
    ok(d->BeginScene());ok(d->Clear(0,nullptr,D3DCLEAR_TARGET|D3DCLEAR_ZBUFFER,0xffffffff,1,0));ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Caster)));
    ok(d->SetDepthStencilSurface(nullptr));ok(d->SetRenderTarget(0,unionSurface));ok(d->SetVertexShader(nullptr));ok(d->SetFVF(D3DFVF_XYZRHW|D3DFVF_TEX1));ok(d->SetRenderState(D3DRS_ZENABLE,FALSE));ok(d->SetRenderState(D3DRS_ZWRITEENABLE,FALSE));
    ok(d->SetTexture(0,dynamic));ok(d->SetTexture(1,white));ok(d->SetPixelShader(combine));const float crop[2][4]={{0,0,0,float(N)},{1.f/N,1.f/N,0,0}};ok(d->SetPixelShaderConstantF(0,crop[0],2));
    const Vertex full[]={{-.5f,-.5f,0,1,0,0},{N-.5f,-.5f,0,1,1,0},{-.5f,N-.5f,0,1,0,1},{N-.5f,N-.5f,0,1,1,1}};
    ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,full,sizeof(Vertex)));ok(d->EndScene());
    unsigned checks=0;
    for(auto* surface:{dynamicSurface,unionSurface}){
        ok(d->GetRenderTargetData(surface,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));
        for(unsigned y=0;y<N;++y)for(unsigned x=0;x<N;++x){float value;std::memcpy(&value,static_cast<const char*>(lock.pBits)+y*lock.Pitch+x*4,4);
            const bool covered=x>=205&&x<=460&&y>=103&&y<=921;assert(std::isfinite(value)&&std::fabs(value-(covered?.35f:1.f))<1e-7);++checks;}
        ok(staging->UnlockRect());
    }
    ok(d->SetTexture(0,nullptr));ok(d->SetTexture(1,nullptr));ok(d->SetRenderTarget(0,saved));saved->Release();vs->Release();staging->Release();depth->Release();unionSurface->Release();dynamicSurface->Release();united->Release();dynamic->Release();
    return checks;
}
static V3 receiver(unsigned x,unsigned y,unsigned w,unsigned h,const Constants& c,float raw){
    const float u=(std::floor((x+.5f)/w*c[33][0])+.5f)*c[0][0],v=(std::floor((y+.5f)/h*c[33][1])+.5f)*c[0][1];
    const float depth=sat((raw-c[1][3])*c[2][0]),z=c[0][2]*c[0][3]/std::max(c[0][3]-depth*(c[0][3]-c[0][2]),.00001f);
    return point({((u-.5f*c[0][0])*2-1)/c[1][0]*z,((v-.5f*c[0][1])*-2+1)/c[1][1]*z,c[1][2]*z},c,3);
}
int main(int argc,char** argv){
    assert(argc==5||argc==6);const std::string dir=argv[1];GPU gpu(argv[2]);D3DCAPS9 caps{};ok(gpu.d->GetDeviceCaps(&caps));const unsigned required=std::max({kDirectionalShadowTransitionCurrentShaderSlots,kDirectionalShadowTransitionPreviousShaderSlots,kWorldFogTransitionShaderSlots,kWorldFogActorTransitionShaderSlots,kWorldFogActorTiledTransitionShaderSlots,kFogCloudsTransitionShaderSlots,kFogCloudsActorTransitionShaderSlots,kFogCloudsActorTiledTransitionShaderSlots});assert(caps.MaxPixelShader30InstructionSlots>=required);
    FILE* report=std::fopen(argv[3],"w");assert(report);FILE* numeric=std::fopen((std::string(argv[3])+".numeric.jsonl").c_str(),"w");assert(numeric);double receiverMaxError=0,originalScalarMaxError=0,productionQueryInputError=0,singleEndpointError=0,zeroBlendError=0,productionEndpointError=0;unsigned numericInputs=0,uncertaintyChecks=0,borderControls=0,skipChecks=0;constexpr unsigned W=32,H=24;unsigned failures=0,cases=0,checks=0;double maxError=0,endpointError=0,ambientError=0,alphaError=0,ghostError=0;unsigned changed=0;
    auto solid=[&](float x,float y,float z,float w){return gpu.tex(rgbaImage(1,1,[&](unsigned,unsigned){return std::array<float,4>{x,y,z,w};}));};
    auto white=solid(1,1,1,1),zero=solid(0,0,0,0);for(unsigned s=0;s<16;++s)gpu.bind(s,white);gpu.bind(11,zero);gpu.bind(12,white);
    const float rawDepth=1000.f/999.8f*(1-.2f/160);auto depth=solid(rawDepth,0,0,1);gpu.bind(1,depth);
    Map newStatic([](float u,float v){return (int(u*1024)%17<8&&int(v*1024)%31>4)?.4f:.6f;}),oldStatic([](float u,float v){return (int(u*1024+1)%17<8&&int(v*1024+2)%31>4)?.4f:.6f;}),dynamic([](float u,float v){return u>.52f&&u<.70f&&v>.02f&&v<.55f?.35f:1.f;}),clear([](float,float){return 1.f;});
    Map unionMap([&](float u,float v){return std::min(newStatic.at(u,v),dynamic.at(u,v));}),oldUnion([&](float u,float v){return std::min(oldStatic.at(u,v),dynamic.at(u,v));});
    auto currentTex=gpu.tex(unionMap.image()),oldTex=gpu.tex(oldStatic.image()),dynamicTex=gpu.tex(dynamic.image()),clearTex=gpu.tex(clear.image()),oldUnionTex=gpu.tex(oldUnion.image());
    auto inputProbe=gpu.shader(std::string(argv[4])+"/TransitionWorldPoint.bin");
    auto currentProbe=gpu.shader(std::string(argv[4])+"/TransitionCurrentPoints.bin"),previousProbe=gpu.shader(std::string(argv[4])+"/TransitionPreviousPoints.bin"),coverProbe=gpu.shader(std::string(argv[4])+"/TransitionPreviousCoverage.bin");
    auto queryCurrent=gpu.shader(std::string(argv[4])+"/TransitionCurrentQuery.bin"),queryPrevious=gpu.shader(std::string(argv[4])+"/TransitionPreviousQuery.bin");
    auto current=gpu.shader(dir+"/DirectionalShadowTransitionCurrent.bin"),previous=gpu.shader(dir+"/DirectionalShadowTransitionPrevious.bin"),baseline=gpu.shader(dir+"/DirectionalShadow.bin");
    for(unsigned scene=0;scene<5;++scene)for(unsigned mode=0;mode<3;++mode)for(unsigned soft=0;soft<2;++soft){
        Constants c=constants(W*2+1,H*2+1);productionMatrices(c,scene);retainCurrent(c);c[116]=mode==2?std::array<float,4>{.3f,-.4f,.8660254f,1}:std::array<float,4>{0,0,1,float(mode)};c[117][0]=float(soft);
        gpu.bind(14,solid(c[116][0],c[116][1],c[116][2],c[116][3]));gpu.bind(2,currentTex);gpu.bind(3,currentTex);gpu.bind(6,oldTex);gpu.bind(7,oldTex);gpu.bind(8,dynamicTex);gpu.bind(9,dynamicTex);
        const auto inputPoints=gpu.capture(inputProbe,c,W,H);
        const std::array<std::vector<float>,4> projections{{gpu.capture(currentProbe,c,W,H),gpu.capture(currentProbe,c,W,H,1),gpu.capture(previousProbe,c,W,H),gpu.capture(previousProbe,c,W,H,1)}};
        const std::array<std::vector<float>,2> coverPoints{{gpu.capture(coverProbe,c,W,H),gpu.capture(coverProbe,c,W,H,1)}};
        const std::array<std::vector<float>,6> uploaded{{projections[0],projections[1],projections[2],projections[3],coverPoints[0],coverPoints[1]}};std::array<IDirect3DTexture9*,6> queryTextures{};
        for(unsigned a=0;a<6;++a)queryTextures[a]=gpu.tex(rgbaImage(W,H,[&](unsigned x,unsigned y){const unsigned i=(y*W+x)*4;return std::array<float,4>{uploaded[a][i],uploaded[a][i+1],uploaded[a][i+2],uploaded[a][i+3]};}));
        auto bindQueries=[&]{const unsigned slots[]={0,1,4,5,10,11};for(unsigned a=0;a<6;++a)gpu.bind(slots[a],queryTextures[a]);};
        for(auto weights:{std::array<float,2>{0,0},{.25f,.25f},{.5f,.5f},{.75f,.75f},{1,1},{.5f,1},{1,.5f}}){
            c[89]={weights[0],weights[1],float(soft),0};gpu.bind(1,depth);gpu.bind(11,zero);const auto productionPixels=gpu.combined(current,previous,c,W,H);bindQueries();const auto pixels=gpu.combined(queryCurrent,queryPrevious,c,W,H);
            for(unsigned i=0;i<pixels.size();i+=4){productionQueryInputError=std::max(productionQueryInputError,std::fabs(double(productionPixels[i])-pixels[i]));failures+=!std::isfinite(productionPixels[i])||productionPixels[i]<-1e-6f||productionPixels[i]>1.000001f;}
            for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){V3 p{};for(unsigned k=0;k<3;++k)p[k]=inputPoints[(y*W+x)*4+k];
                auto cpu=receiver(x,y,W,H,c,rawDepth);for(unsigned k=0;k<3;++k){cpu[k]+=c[116][k]*.08f*c[116][3];receiverMaxError=std::max(receiverMaxError,std::fabs(double(cpu[k])-p[k]));}
                const float originalReference=referenceDirect(cpu,unionMap,oldStatic,dynamic,c);
                std::array<V3,4> query{};std::array<V3,2> cover{};for(unsigned a=0;a<4;++a)for(unsigned k=0;k<3;++k)query[a][k]=projections[a][(y*W+x)*4+k];for(unsigned a=0;a<2;++a)for(unsigned k=0;k<3;++k)cover[a][k]=coverPoints[a][(y*W+x)*4+k];
                const float expected=referenceDirect(p,unionMap,oldStatic,dynamic,c,&query,&cover);const float actual=pixels[(y*W+x)*4];const double originalError=std::fabs(double(actual)-originalReference);originalScalarMaxError=std::max(originalScalarMaxError,originalError);
                if(originalError>3e-5)++numericInputs;if(originalError>3e-5&&numericInputs<=64){std::fprintf(numeric,"{\"scene\":%u,\"normal_mode\":%u,\"soft\":%u,\"weights\":[%.9g,%.9g],\"pixel\":[%u,%u],\"receiver_cpu\":[%.9g,%.9g,%.9g],\"receiver_gpu\":[%.9g,%.9g,%.9g],\"cpu_reconstructed_expected\":%.9g,\"actual_receiver_expected\":%.9g,\"actual\":%.9g,\"constants\":[",scene,mode,soft,weights[0],weights[1],x,y,cpu[0],cpu[1],cpu[2],p[0],p[1],p[2],originalReference,expected,actual);for(unsigned row=0;row<c.size();++row)std::fprintf(numeric,"%s[%.9g,%.9g,%.9g,%.9g]",row?",":"",c[row][0],c[row][1],c[row][2],c[row][3]);std::fprintf(numeric,"]}\n");}
                const float observed=productionPixels[(y*W+x)*4];
                const std::array<V3,2> liftedCovers{{query[2],query[3]}};const float wrongBorder=referenceDirect(p,unionMap,oldStatic,dynamic,c,&query,&liftedCovers);
                const bool needsEnvelope=std::fabs(double(observed)-expected)>3e-5,checkBorder=borderControls<16&&std::fabs(double(wrongBorder)-expected)>.001;
                if(needsEnvelope||checkBorder){const auto range=queryEnvelope(p,unionMap,oldStatic,dynamic,c,query,cover);
                    if(needsEnvelope){++uncertaintyChecks;if(observed<range.first-3e-5f||observed>range.second+3e-5f){++failures;std::printf("FAIL production ULP envelope scene=%u mode=%u soft=%u xy=%u,%u got=%g range=[%g,%g]\n",scene,mode,soft,x,y,observed,range.first,range.second);}}
                    if(checkBorder){assert(wrongBorder<range.first-3e-5f||wrongBorder>range.second+3e-5f);++borderControls;}
                }
                double error=std::fabs(double(actual)-expected);maxError=std::max(maxError,error);
                if(!std::isfinite(actual)||error>3e-5){if(failures++<8)std::printf("FAIL surface scene=%u mode=%u soft=%u weights=%g,%g xy=%u,%u actual=%g expected=%g error=%g\n",scene,mode,soft,weights[0],weights[1],x,y,actual,expected,error);}++checks;changed+=actual<.99f;
            }++cases;
        }
        gpu.bind(1,depth);gpu.bind(11,zero);c[89]={1,1,1,0};auto original=gpu.capture(baseline,c,W,H),endpoint=gpu.combined(current,previous,c,W,H),single=gpu.capture(current,c,W,H);
        auto original16=gpu.capture(baseline,c,W,H,0,D3DFMT_A16B16G16R16F),endpoint16=gpu.combined(current,previous,c,W,H,D3DFMT_A16B16G16R16F);
        for(unsigned i=0;i<original.size();i+=4){productionEndpointError=std::max(productionEndpointError,std::fabs(double(endpoint16[i])-original16[i]));singleEndpointError=std::max(singleEndpointError,std::fabs(double(single[i])-original[i]));zeroBlendError=std::max(zeroBlendError,std::fabs(double(endpoint[i])-single[i]));failures+=endpoint16[i]!=original16[i];double error=std::fabs(double(endpoint[i])-original[i]);endpointError=std::max(endpointError,error);failures+=error>3e-5;}
    }
    std::printf("NUMERIC singleEndpoint=%g zeroAddR32Blend=%g productionFP16Endpoint=%g\n",singleEndpointError,zeroBlendError,productionEndpointError);std::printf("surface cases=%u scalarChecks=%u maxError=%g endpointError=%g failures=%u\n",cases,checks,maxError,endpointError,failures);std::fflush(stdout);
    // Identical epochs prove current-pose occlusion at every fade, including
    // PCSS: the stored history is empty and cannot retain a departed actor.
    for(unsigned soft=0;soft<2;++soft){Constants c=constants(W*2+1,H*2+1);productionMatrices(c,0);for(unsigned row=0;row<4;++row){c[80+row]=c[7+row];c[84+row]=c[11+row];c[90+row]={0,0,0,0};c[94+row]={0,0,0,0};c[90+row][row]=c[94+row][row]=1;}c[88]=c[16];c[98]=c[99];retainCurrent(c);c[89]={1,1,float(soft),0};
        gpu.bind(14,solid(0,0,1,1));gpu.bind(2,dynamicTex);gpu.bind(3,dynamicTex);gpu.bind(6,clearTex);gpu.bind(7,clearTex);gpu.bind(8,dynamicTex);gpu.bind(9,dynamicTex);
        const auto expected=gpu.combined(current,previous,c,W,H);
        for(float f:{0.f,.25f,.5f,.75f,1.f}){c[89]={f,f,float(soft),0};const auto actual=gpu.combined(current,previous,c,W,H);for(unsigned i=0;i<actual.size();i+=4){double error=std::fabs(double(actual[i])-expected[i]);ghostError=std::max(ghostError,error);failures+=error>3e-5;}}
    }
    for(bool water:{false,true})for(unsigned soft=0;soft<2;++soft){auto c=constants(W*2+1,H*2+1);productionMatrices(c,0);retainCurrent(c);c[89]={.5f,.25f,float(soft),0};c[30][0]=water?1.f:0.f;
        gpu.bind(1,water?depth:white);gpu.bind(11,water?solid(80,1,0,0):zero);gpu.bind(14,solid(0,0,1,1));
        const auto a=gpu.capture(current,c,W,H),b=gpu.capture(previous,c,W,H),sum=gpu.combined(current,previous,c,W,H,D3DFMT_A16B16G16R16F);
        for(unsigned i=0;i<a.size();i+=4){assert(a[i]==1&&b[i]==0&&sum[i]==1);++skipChecks;}
    }
    gpu.bind(1,depth);gpu.bind(11,zero);
    auto field=gpu.tex(rgbaImage(64,64,[](unsigned x,unsigned y){return std::array<float,4>{-.2f,.008f,.004f,(x/5+y/7)%5?5.f:0.f};}));gpu.bind(13,field);
    IDirect3DVolumeTexture9* noise{};ok(gpu.d->CreateVolumeTexture(16,16,16,1,0,D3DFMT_L8,D3DPOOL_MANAGED,&noise,nullptr));D3DLOCKED_BOX box{};ok(noise->LockBox(0,&box,nullptr,0));for(unsigned z=0;z<16;++z)for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)*((unsigned char*)box.pBits+z*box.SlicePitch+y*box.RowPitch+x)=static_cast<unsigned char>((x*37+y*17+z*53)%256);ok(noise->UnlockBox(0));
    const char* names[]={"WorldFog","WorldFogActor","WorldFogActorTiled","FogClouds","FogCloudsActor","FogCloudsActorTiled"};unsigned volumeChecks=0;
    for(const auto* name:names){auto native=gpu.shader(dir+"/"+name+".bin"),fade=gpu.shader(dir+"/"+name+"Transition.bin");const bool cloud=std::string(name).find("Clouds")!=std::string::npos;
        Constants c=constants(W*2+1,H*2+1);productionMatrices(c,0);for(unsigned row=0;row<4;++row){c[80+row]=c[7+row];c[84+row]=c[11+row];c[90+row]={0,0,0,0};c[94+row]={0,0,0,0};c[101+row]={0,0,0,0};c[90+row][row]=c[94+row][row]=c[101+row][row]=1;}c[88]=c[16];c[98]=c[99];retainCurrent(c);
        gpu.bind(1,depth);gpu.bind(2,currentTex);gpu.bind(3,currentTex);gpu.bind(6,oldTex);gpu.bind(7,oldTex);gpu.bind(8,dynamicTex);gpu.bind(9,dynamicTex);gpu.bind(14,cloud?static_cast<IDirect3DBaseTexture9*>(noise):white);
        if(cloud){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(gpu.d->SetSamplerState(14,state,D3DTEXF_LINEAR));for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})ok(gpu.d->SetSamplerState(14,state,D3DTADDRESS_WRAP));}
        for(unsigned actors=0;actors<2;++actors){for(unsigned row=0;row<4;++row)c[68+row]=c[11+row];c[72][0]=float(actors);c[66]={-1,-1,1,1};gpu.bind(4,dynamicTex);gpu.bind(5,zero);
            auto expected=gpu.capture(native,c,W,H);c[89]={1,1,0,0};auto endpoint=gpu.capture(fade,c,W,H);
            for(unsigned i=0;i<expected.size();++i){double error=std::fabs(double(endpoint[i])-expected[i]);endpointError=std::max(endpointError,error);failures+=!std::isfinite(error)||error>3e-5;++volumeChecks;}
            gpu.bind(2,oldUnionTex);gpu.bind(3,oldUnionTex);auto oldExpected=gpu.capture(native,c,W,H);gpu.bind(2,currentTex);gpu.bind(3,currentTex);c[89]={0,0,0,0};auto oldEndpoint=gpu.capture(fade,c,W,H);
            for(unsigned i=0;i<expected.size();++i){double error=std::fabs(double(oldEndpoint[i])-oldExpected[i]);endpointError=std::max(endpointError,error);failures+=error>3e-5;}
            for(float f:{0.f,.25f,.5f,.75f,1.f}){c[89]={f,f,0,0};auto pixels=gpu.capture(fade,c,W,H);for(unsigned i=0;i<pixels.size();++i){if(i%4==3){double error=std::fabs(double(pixels[i])-expected[i]);alphaError=std::max(alphaError,error);failures+=error>2e-6;}failures+=!std::isfinite(pixels[i]);}}
            c[17]={0,0,0,0};auto ambient=gpu.capture(native,c,W,H);for(float f:{0.f,.5f,1.f}){c[89]={f,f,0,0};auto pixels=gpu.capture(fade,c,W,H);for(unsigned i=0;i<pixels.size();++i){double error=std::fabs(double(pixels[i])-ambient[i]);ambientError=std::max(ambientError,error);failures+=error>3e-5;}}c[17]={.8f,.7f,.6f,0};
        }
    }
    // Snapshot exactly the working-grid static contribution, including nonzero
    // cache offsets and depth shifts. The neutral dynamic source is one.
    Image raw{1280,1280,D3DFMT_R32F,4,std::vector<unsigned char>(1280*1280*4)};std::vector<float> rawValues(1280*1280);
    for(unsigned y=0;y<1280;++y)for(unsigned x=0;x<1280;++x)rawValues[y*1280+x]=float((x*17+y*31)%129)/128;
    std::memcpy(raw.pixels.data(),rawValues.data(),rawValues.size()*4);auto rawTex=gpu.tex(raw);auto snapshot=gpu.shader(dir+"/ShadowUnion.bin");
    unsigned cropChecks=0;double cropError=0;
    const std::array<std::array<float,3>,7> crops{{{-32,17,0},{128,128,0},{256,256,0},{600,-32,.015625f},{0,0,-.015625f},{128,128,-.75f},{128,128,.75f}}};
    gpu.bind(0,white);gpu.bind(1,rawTex);
    for(auto crop:crops){Constants cc{};cc[0]={crop[0],crop[1],crop[2],1280};cc[1]={1.f/1024,1.f/1280,0,0};const auto pixels=gpu.capture(snapshot,cc,1024,1024);
        for(unsigned y=0;y<1024;++y)for(unsigned x=0;x<1024;++x){const int xx=int(x)+int(crop[0]),yy=int(y)+int(crop[1]);float expected=1;
            if(xx>=0&&xx<1280&&yy>=0&&yy<1280){const float value=rawValues[yy*1280+xx];if(value<1){const float shifted=value+crop[2];expected=shifted<=1?std::max(shifted,0.f):1;}}
            const float actual=pixels[(y*1024+x)*4];double e=std::fabs(double(actual)-expected);cropError=std::max(cropError,e);failures+=!std::isfinite(actual)||e!=0;++cropChecks;
        }
    }
    const unsigned redirectedChecks=redirectedDynamic(gpu,dir,white);
    std::fclose(numeric);std::fprintf(report,"{\"timings\":[");
    if(argc==6){unsigned row=0;for(auto size:{std::pair<unsigned,unsigned>{1920,1080},{3840,2160}}){
        auto c=constants(size.first,size.second);productionMatrices(c,0);retainCurrent(c);c[89]={.5f,.5f,1,0};
        gpu.bind(1,depth);gpu.bind(2,currentTex);gpu.bind(3,currentTex);gpu.bind(6,oldTex);gpu.bind(7,oldTex);gpu.bind(8,dynamicTex);gpu.bind(9,dynamicTex);gpu.bind(11,zero);gpu.bind(14,solid(0,0,1,1));
        for(unsigned slot:{1u,2u,3u,6u,7u,8u,9u,11u,14u}){for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(gpu.d->SetSamplerState(slot,state,D3DTEXF_POINT));for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})ok(gpu.d->SetSamplerState(slot,state,D3DTADDRESS_CLAMP));}
        auto median=[](std::vector<double> values){std::sort(values.begin(),values.end());return values[values.size()/2];};
        const double base=median(gpu.time(baseline,c,size.first/2,size.second/2));
        const double now=median(gpu.time(current,c,size.first/2,size.second/2));
        for(unsigned r=0;r<4;++r){c[7+r]=c[80+r];c[11+r]=c[84+r];}c[16]=c[88];c[99]=c[98];
        const double before=median(gpu.time(previous,c,size.first/2,size.second/2));
        std::fprintf(report,"%s{\"full_width\":%u,\"full_height\":%u,\"baseline_pcss_gpu_ms\":%.6f,\"fade_current_gpu_ms\":%.6f,\"fade_previous_gpu_ms\":%.6f,\"fade_resolve_total_gpu_ms\":%.6f,\"active_delta_gpu_ms\":%.6f}",row++?",":"",size.first,size.second,base,now,before,now+before,now+before-base);std::fflush(report);
        std::printf("TIMING %ux%u PCSS base=%.6f fadeCurrent=%.6f fadePrevious=%.6f fadeTotal=%.6f activeDelta=%.6f ms\n",size.first,size.second,base,now,before,now+before,now+before-base);std::fflush(stdout);
    }}
    std::fprintf(report,"],\"dynamic_redirect_checks\":%u,\"surface_cases\":%u,\"unequal_near_far_epoch_cases\":42,\"scalar_checks\":%u,\"shadowed_pixels\":%u,\"scalar_max_error\":%.9g,\"receiver_max_error_world_units\":%.9g,\"production_query_input_max_error\":%.9g,\"one_uv_ulp_envelope_checks\":%u,\"lifted_border_negative_controls\":%u,\"sky_water_skip_checks\":%u,\"production_fp16_visibility_endpoint_error\":%.9g,\"cpu_reconstruction_failures\":%u,\"cpu_reconstruction_scalar_max_error\":%.9g,\"endpoint_max_error\":%.9g,\"current_pose_max_error\":%.9g,\"ambient_max_error\":%.9g,\"transmittance_max_error\":%.9g,\"volume_checks\":%u,\"crop_checks\":%u,\"crop_max_error\":%.9g,\"device_slots\":%lu,\"failures\":%u}\n",redirectedChecks,cases,checks,changed,maxError,receiverMaxError,productionQueryInputError,uncertaintyChecks,borderControls,skipChecks,productionEndpointError,numericInputs,originalScalarMaxError,endpointError,ghostError,ambientError,alphaError,volumeChecks,cropChecks,cropError,caps.MaxPixelShader30InstructionSlots,failures);std::fclose(report);gpu.bind(14,nullptr);noise->Release();assert(changed&&borderControls&&skipChecks);std::printf("transition GPU surface=%u volume=%u endpoint=%g ambient=%g alpha=%g ghost=%g failures=%u\n",checks,volumeChecks,endpointError,ambientError,alphaError,ghostError,failures);return failures?1:0;
}
