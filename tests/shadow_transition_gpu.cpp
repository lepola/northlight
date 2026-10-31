// Hidden-device production shader oracle. No game or installed renderer is loaded.
#define main unused_shadow_transition_replay
#include "composite_distance_gpu.cpp"
#undef main
#include <array>
#include <chrono>
#include <functional>
#include "shadow_transition_math.h"

using V3=std::array<float,3>;
using Constants=std::array<std::array<float,4>,112>;
using Clock=std::chrono::steady_clock;
static double since(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
static V3 point(V3 p,const Constants& c,unsigned m) {
    V3 q{};for(unsigned k=0;k<3;++k)q[k]=p[0]*c[m][k]+p[1]*c[m+1][k]+p[2]*c[m+2][k]+c[m+3][k];return q;
}
static float comparisonMargin=1,coordinateMargin=1;
static void sampleMargin(float u,float v,unsigned n){for(float a:{u*n,v*n})coordinateMargin=std::min(coordinateMargin,std::fabs(a-std::round(a)));}
static float sat(float x){return std::max(0.f,std::min(1.f,x));}
static void panel(const std::string& path,const std::vector<std::vector<float>>& frames,unsigned w,unsigned h,bool visibility) {
    const unsigned scale=2,width=unsigned(frames.size())*(w+1)*scale,height=h*scale;
    FILE* file=std::fopen(path.c_str(),"wb");assert(file);std::fprintf(file,"P6\n%u %u\n255\n",width,height);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
        const unsigned frame=x/((w+1)*scale),column=x%((w+1)*scale)/scale;unsigned char rgb[3]{};
        if(column<w){const unsigned i=((y/scale)*w+column)*4;for(unsigned k=0;k<3;++k){const float v=frames[frame][i+(visibility?3:k)];rgb[k]=static_cast<unsigned char>(std::lround(std::sqrt(sat(v*(visibility?1:4)))*255));}}
        assert(std::fwrite(rgb,1,3,file)==3);
    }std::fclose(file);
}
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
    const float bx=std::floor(u/t-.5f+(nearCascade?.5f:0)),by=std::floor(v/t-.5f+(nearCascade?.5f:0));
    const float fx=u/t-.5f-bx,fy=v/t-.5f-by;
    const float bias=(old?c[100][nearCascade?0:1]:c[24][0])*(nearCascade?1:2);
    const float receiver=q[2]-bias;
    float sum=0,total=0;
    for(int j=0;j<size;++j)for(int i=0;i<size;++i){
        const float x=(bx+i+start+.5f)*t,y=(by+j+start+.5f)*t;
        const float w=std::max(size*.5f-std::fabs(i+start-fx),0.f)*std::max(size*.5f-std::fabs(j+start-fy),0.f);
        const float planeDelta=gradient[0]*(x-u)+gradient[1]*(y-v);
        const float expected=receiver+planeDelta;
        sampleMargin(x,y,map.n);comparisonMargin=std::min(comparisonMargin,std::fabs(expected-map.at(x,y)));
        bool lit=expected<=map.at(x,y);
        if(old){auto d=point({q[0]+2*(x-u),q[1]-2*(y-v),q[2]+planeDelta},c,nearCascade?90:94);lit=lit&&d[2]-c[24][0]*(nearCascade?1:2)<=dynamicAt(d,dynamic,c);}
        sum+=(lit?1.f:0.f)*w;total+=w;
    }
    const float edge=sat((1-std::max(std::fabs(q[0]),std::fabs(q[1])))/.12f);
    return 1+(sum/std::max(total,.0001f)-1)*edge;
}
static float fogPCF(V3 q,bool old,const Map& map,const Map& dynamic,const Constants& c) {
    const float u=q[0]*.5f+.5f+.5f*c[2][3],v=.5f-q[1]*.5f+.5f*c[2][3];
    if(u<0||u>1||v<0||v>1||q[2]<0||q[2]>1)return 1;
    const unsigned m=old?84:11;
    float fx=(q[0]-c[m+3][0])*c[2][2]/1.5f,fy=-(q[1]-c[m+3][1])*c[2][2]/1.5f;
    fx-=std::floor(fx);fy-=std::floor(fy);
    const float wx=fx*fx*(3-2*fx),wy=fy*fy*(3-2*fy);
    float sum=0;
    for(unsigned j=0;j<2;++j)for(unsigned i=0;i<2;++i){
        const float dx=(i-fx)*1.5f/(2*c[2][2]),dy=(j-fy)*1.5f/(2*c[2][2]);
        sampleMargin(u+dx,v+dy,map.n);comparisonMargin=std::min(comparisonMargin,std::fabs(q[2]-(old?c[100][1]:c[24][0])*4-map.at(u+dx,v+dy)));
        bool lit=q[2]-(old?c[100][1]:c[24][0])*4<=map.at(u+dx,v+dy);
        if(old){auto d=point({q[0]+2*dx,q[1]-2*dy,q[2]},c,94);lit=lit&&d[2]-c[24][0]*4<=dynamicAt(d,dynamic,c);}
        sum+=(lit?1.f:0.f)*(i?wx:1-wx)*(j?wy:1-wy);
    }return sum;
}

static float referenceDirect(V3 p,const Map& map,const Map& oldMap,const Map& dynamic,const Constants& c) {
    const V3 n{c[103][0],c[103][1],c[103][2]};const float confidence=c[102][2];
    auto projected=[&](bool old,bool nearCascade,V3& gradient){
        const unsigned m=old?(nearCascade?80:84):(nearCascade?7:11),di=old?(nearCascade?88:98):(nearCascade?16:99);
        const float radius=c[2][nearCascade?1:2],facing=n[0]*c[di][0]+n[1]*c[di][1]+n[2]*c[di][2];
        const float lift=(1-sat(std::fabs(facing)))*confidence*2/1024;
        const float inverseSpan=old?c[100][nearCascade?2:3]:c[35][0];
        const float factor=confidence*2*inverseSpan/(facing<0?-std::max(std::fabs(facing),.15f):std::max(std::fabs(facing),.15f));
        const float limit=(nearCascade?2.688f:10.752f)*inverseSpan/c[2][3];
        gradient[0]=std::max(-limit,std::min(limit,(n[0]*c[m][0]+n[1]*c[m+1][0]+n[2]*c[m+2][0])*radius*factor*radius));
        gradient[1]=std::max(-limit,std::min(limit,-(n[0]*c[m][1]+n[1]*c[m+1][1]+n[2]*c[m+2][1])*radius*factor*radius));
        return point({p[0]+n[0]*lift*radius,p[1]+n[1]*lift*radius,p[2]+n[2]*lift*radius},c,m);
    };
    V3 g{};auto nn=projected(false,true,g),on=projected(true,true,g);
    const float oldBlend=sat((std::max(std::fabs(on[0]),std::fabs(on[1]))-.72f)/.18f),newBlend=sat((std::max(std::fabs(nn[0]),std::fabs(nn[1]))-.72f)/.18f);
    const float blend=oldBlend*(1-c[89][0])+newBlend*c[89][0];float result=0;
    for(unsigned cascade=0;cascade<2;++cascade){const bool nearCascade=cascade==0;V3 ng{},og{};auto nq=projected(false,nearCascade,ng),oq=projected(true,nearCascade,og);
        float nv=surfacePCF(nq,nearCascade,false,map,dynamic,c,ng),ov=surfacePCF(oq,nearCascade,true,oldMap,dynamic,c,og);
        const float u=oq[0]*.5f+.5f+.5f*c[2][3],v=.5f-oq[1]*.5f+.5f*c[2][3];if(u<.003f||u>.997f||v<.003f||v>.997f||oq[2]<=0||oq[2]>=1)ov=nv;
        const float f=c[89][cascade];result+=(ov*(1-f)+nv*f)*(nearCascade?1-blend:blend);
    }return result;
}

struct GPU {
    HWND window{};HMODULE module{};IDirect3D9* api{};IDirect3DDevice9* d{};
    IDirect3DQuery9 *event{},*start{},*end{},*frequency{},*disjoint{};
    std::vector<IDirect3DTexture9*> owned;
    std::vector<IDirect3DPixelShader9*> shaders;
    double lastCreateCpuMs=0;
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
    IDirect3DPixelShader9* shader(const std::string& path){auto code=read(path);IDirect3DPixelShader9* ps{};const auto begin=Clock::now();ok(d->CreatePixelShader(reinterpret_cast<const DWORD*>(code.data()),&ps));lastCreateCpuMs=since(begin);shaders.push_back(ps);return ps;}
    IDirect3DTexture9* tex(const Image& image){auto t=texture(d,image);owned.push_back(t);return t;}
    void bind(unsigned slot,IDirect3DBaseTexture9* t){ok(d->SetTexture(slot,t));}
    void draw(unsigned w,unsigned h,unsigned count=1){const Vertex q[]={{-.5f,-.5f,0,1,0,0},{float(w)-.5f,-.5f,0,1,1,0},{-.5f,float(h)-.5f,0,1,0,1},{float(w)-.5f,float(h)-.5f,0,1,1,1}};ok(d->BeginScene());for(unsigned i=0;i<count;++i)ok(d->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP,2,q,sizeof(Vertex)));ok(d->EndScene());}
    std::array<double,3> firstDraw(IDirect3DPixelShader9* ps,const Constants& c,unsigned w,unsigned h,bool mrt,D3DFORMAT format=D3DFMT_A16B16G16R16F){
        IDirect3DSurface9 *target{},*second{};ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->SetRenderTarget(0,target));
        if(mrt){ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_NONE,0,FALSE,&second,nullptr));ok(d->SetRenderTarget(1,second));}
        D3DVIEWPORT9 vp{0,0,w,h,0,1};ok(d->SetViewport(&vp));ok(d->SetPixelShaderConstantF(0,c[0].data(),unsigned(c.size())));
        ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));const auto begin=Clock::now();ok(d->SetPixelShader(ps));draw(w,h);const double submit=since(begin);
        ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);const double wall=since(begin);
        UINT64 a{},b{},hz{};BOOL bad{};ok(start->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(frequency->GetData(&hz,sizeof hz,0));ok(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&b>=a&&hz);
        ok(d->SetRenderTarget(1,nullptr));if(second)second->Release();target->Release();return {submit,wall,double(b-a)/hz*1000};
    }
    std::array<double,2> transitionTargets(){
        IDirect3DSurface9* surfaces[2]{};const auto begin=Clock::now();
        for(unsigned i=0;i<2;++i){IDirect3DTexture9* t{};ok(d->CreateTexture(1024,1024,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&t,nullptr));owned.push_back(t);ok(t->GetSurfaceLevel(0,&surfaces[i]));}
        const double cpu=since(begin);ok(d->SetRenderTarget(0,surfaces[0]));D3DVIEWPORT9 vp{0,0,1024,1024,0,1};ok(d->SetViewport(&vp));
        ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));
        for(auto s:surfaces){ok(d->SetRenderTarget(0,s));ok(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0));}
        ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);
        UINT64 a{},b{},hz{};BOOL bad{};ok(start->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(frequency->GetData(&hz,sizeof hz,0));ok(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&b>=a&&hz);
        for(auto s:surfaces)s->Release();return {cpu,double(b-a)/hz*1000};
    }
    std::vector<float> capture(IDirect3DPixelShader9* ps,const Constants& c,unsigned w,unsigned h,unsigned output=0){
        IDirect3DSurface9 *target{},*second{},*staging{};ok(d->CreateRenderTarget(w,h,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));
        ok(d->CreateRenderTarget(w,h,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&second,nullptr));ok(d->CreateOffscreenPlainSurface(w,h,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr));
        ok(d->SetRenderTarget(0,target));ok(d->SetRenderTarget(1,second));ok(d->SetRenderState(D3DRS_COLORWRITEENABLE1,15));D3DVIEWPORT9 vp{0,0,w,h,0,1};ok(d->SetViewport(&vp));ok(d->SetPixelShaderConstantF(0,c[0].data(),unsigned(c.size())));ok(d->SetPixelShader(ps));draw(w,h);
        ok(d->GetRenderTargetData(output?second:target,staging));D3DLOCKED_RECT lock{};ok(staging->LockRect(&lock,nullptr,D3DLOCK_READONLY));std::vector<float> out(w*h*4);for(unsigned y=0;y<h;++y)std::memcpy(out.data()+y*w*4,static_cast<char*>(lock.pBits)+y*lock.Pitch,w*16);ok(staging->UnlockRect());
        ok(d->SetRenderTarget(1,nullptr));target->Release();second->Release();staging->Release();return out;
    }
    std::vector<double> time(IDirect3DPixelShader9* ps,const Constants& c,unsigned w,unsigned h,D3DFORMAT format=D3DFMT_A16B16G16R16F){
        IDirect3DSurface9* target{};ok(d->CreateRenderTarget(w,h,format,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr));ok(d->SetRenderTarget(0,target));D3DVIEWPORT9 vp{0,0,w,h,0,1};ok(d->SetViewport(&vp));ok(d->SetPixelShaderConstantF(0,c[0].data(),unsigned(c.size())));ok(d->SetPixelShader(ps));draw(w,h,3);wait(event);
        std::vector<double> times;
        for(unsigned block=0;block<5;++block){ok(disjoint->Issue(D3DISSUE_BEGIN));ok(start->Issue(D3DISSUE_END));draw(w,h,8);ok(end->Issue(D3DISSUE_END));ok(frequency->Issue(D3DISSUE_END));ok(disjoint->Issue(D3DISSUE_END));wait(event);UINT64 a{},b{},hz{};BOOL bad{};ok(start->GetData(&a,sizeof a,0));ok(end->GetData(&b,sizeof b,0));ok(frequency->GetData(&hz,sizeof hz,0));ok(disjoint->GetData(&bad,sizeof bad,0));assert(!bad&&b>=a&&hz);times.push_back(double(b-a)/hz*1000/8);}
        target->Release();return times;
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
    const float elevation=scene==0?21.7f*pi/180:scene==1?2.f*pi/180:scene==2?31.f*pi/180:std::asin(.95f)-.00025f;
    const float azimuth=scene==2?-1.4f:.7f;
    auto direction=[](float e,float a){return Vec3(std::cos(e)*std::cos(a),std::cos(e)*std::sin(a),std::sin(e));};
    const Vec3 oldDirection=direction(elevation,azimuth),newDirection=direction(elevation+.0005f,azimuth+.00015f);
    const Vec3 oldCenter{12.25f,-9.75f,7.5f},newCenter=oldCenter+Vec3(.21f,-.17f,.09f);
    auto matrix=[&](unsigned dst,const float* m){for(unsigned row=0;row<4;++row)for(unsigned k=0;k<4;++k)c[dst+row][k]=m[row*4+k];};
    for(unsigned cascade=0;cascade<2;++cascade){const float radius=cascade?192.f:48.f;
        const auto old=WM::shadowFrame(oldCenter,oldDirection,radius),current=WM::shadowFrame(newCenter,newDirection,radius);
        float om[16],cm[16],reproject[16];WM::shadowMatrixFrom(old,radius,om);WM::shadowMatrixFrom(current,radius,cm);ShadowTransitionMath::reproject(old,cm,radius,reproject);
        matrix(cascade?84:80,om);matrix(cascade?11:7,cm);matrix(cascade?94:90,reproject);
        c[cascade?98:88]={old.z.x,old.z.y,old.z.z,0};c[cascade?99:16]={current.z.x,current.z.y,current.z.z,scene==2?1.f:0.f};
    }
    c[104]={oldCenter.x,oldCenter.y,oldCenter.z,0};c[24][0]=WM::ShadowBiasWorld*WM::InverseShadowDepth;c[35][0]=WM::InverseShadowDepth;
    c[100]={c[24][0],c[24][0],c[35][0],c[35][0]};
}
int main(int argc,char** argv){
    assert(argc==4||argc==5);const std::string dir=argv[1];GPU gpu(argv[2]);FILE* report=std::fopen(argv[3],"w");assert(report);
    std::array<double,2> resourceCold[4]{};if(argc==5)for(auto& slot:resourceCold)slot=gpu.transitionTargets();
    constexpr unsigned W=64,H=48;auto c=constants(W*2,H*2);
    auto solid=[&](float x,float y,float z,float w){return gpu.tex(rgbaImage(1,1,[&](unsigned,unsigned){return std::array<float,4>{x,y,z,w};}));};
    auto white=solid(1,1,1,1),zero=solid(0,0,0,0);for(unsigned s=0;s<16;++s)gpu.bind(s,white);gpu.bind(11,zero);gpu.bind(12,white);
    auto currentStatic=Map([](float u,float v){const int x=int(u*1024),y=int(v*1024);return (x%17==8||y%43<3)?1.f:(x/17%3==1?.49f:.4f);});
    auto oldStatic=Map([](float u,float v){const int x=int(u*1024),y=int(v*1024);return ((x+1)%17==8||(y+1)%43<3)?1.f:(x/17%3==1?.49f:.4f);});
    auto dynamic=Map([](float u,float v){return u>.52f&&u<.70f&&v>.02f&&v<.55f?.35f:1.f;});
    auto clear=Map([](float,float){return 1.f;});
    auto unionMap=Map([&](float u,float v){return std::min(currentStatic.at(u,v),dynamic.at(u,v));});
    auto oldUnion=Map([&](float u,float v){return std::min(oldStatic.at(u,v),dynamic.at(u,v));});
    auto oldUnionTex=gpu.tex(oldUnion.image());
    auto oldTex=gpu.tex(oldStatic.image()),unionTex=gpu.tex(unionMap.image()),dynamicTex=gpu.tex(dynamic.image()),clearTex=gpu.tex(clear.image());
    gpu.bind(2,unionTex);gpu.bind(3,unionTex);gpu.bind(6,oldTex);gpu.bind(7,oldTex);gpu.bind(8,dynamicTex);gpu.bind(9,dynamicTex);
    auto direct=gpu.shader(dir+"/DirectVisibility.bin"),fog=gpu.shader(dir+"/FogVisibility.bin");
    double referenceError=0;float minimumReferenceMargin=1;unsigned checks=0,failures=0;float contrast=0;
    for(unsigned receiver=0;receiver<3;++receiver){c[103]=receiver?std::array<float,4>{.3f,-.4f,.8660254f,0}:std::array<float,4>{0,0,1,0};c[102][2]=receiver==2?0:1;
    for(unsigned rotated=0;rotated<2;++rotated){matrices(c,rotated?.00053f:0,rotated?2.f/1024:0);
        for(float span:{73.13f,167.31f,380.23f}){c[102][0]=span;
            for(auto weights:std::array<std::array<float,2>,8>{{{0,0},{.25f,.25f},{.5f,.5f},{.75f,.75f},{1,1},{0,1},{1,0},{.25f,.75f}}}){const float f=weights[0],farFade=weights[1];c[89]={f,farFade,0,0};
                for(unsigned pass=0;pass<2;++pass){const auto pixels=gpu.capture(pass?fog:direct,c,W,H);
                    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){V3 p{((x+.5f)/W-.5f)*span,((y+.5f)/H-.5f)*span,0};float expected;comparisonMargin=coordinateMargin=1;
                        if(pass){const auto nq=point(p,c,11),oq=point(p,c,84);float nv=fogPCF(nq,false,unionMap,dynamic,c);float ov=fogPCF(oq,true,oldStatic,dynamic,c);
                            float ou=oq[0]*.5f+.5f+.5f*c[2][3],ovv=.5f-oq[1]*.5f+.5f*c[2][3];if(ou<0||ou>1||ovv<0||ovv>1||oq[2]<0||oq[2]>1)ov=nv;expected=ov*(1-farFade)+nv*farFade;
                        }else expected=referenceDirect(p,unionMap,oldStatic,dynamic,c);
                        minimumReferenceMargin=std::min(minimumReferenceMargin,comparisonMargin);
                        const float actual=pixels[(y*W+x)*4];const double error=std::fabs(double(actual)-expected);referenceError=std::max(referenceError,error);if(error>3e-4&&failures<24)std::printf("reference mismatch receiver=%u rotated=%u span=%g nearFade=%g farFade=%g pass=%u x=%u y=%u expected=%.9g actual=%.9g margin=%.9g coordinate=%.9g\n",receiver,rotated,span,f,farFade,pass,x,y,expected,actual,comparisonMargin,coordinateMargin);failures+=!std::isfinite(actual)||error>3e-4;++checks;contrast=std::max(contrast,std::fabs(actual-.5f));
                    }
                }
            }
        }
    }
    }
    std::printf("scalar oracle checks=%u maxError=%g failures=%u\n",checks,referenceError,failures);std::fflush(stdout);
    failures+=minimumReferenceMargin<.001f;
    auto originalVisibility=gpu.shader(dir+"/BaselineVisibility.bin");
    auto geometricCurrent=Map([](float u,float v){return (int(u*1024)%17==8||int(v*1024)%43<3)?1.f:.125f;});
    auto geometricOld=Map([](float u,float v){return ((int(u*1024)+1)%17==8||(int(v*1024)+1)%43<3)?1.f:.125f;});
    auto geometricDynamic=Map([](float u,float v){return u>.52f&&u<.70f&&v>.02f&&v<.55f?.1f:1.f;});
    auto geometricUnion=Map([&](float u,float v){return std::min(geometricCurrent.at(u,v),geometricDynamic.at(u,v));});
    auto gc=gpu.tex(geometricCurrent.image()),go=gpu.tex(geometricOld.image()),gd=gpu.tex(geometricDynamic.image()),gu=gpu.tex(geometricUnion.image());
    unsigned geometryChecks=0,geometryEndpoints=0,negativeDepthChecks=0,filteredGeometry=0;double geometryError=0,geometryEndpointError=0;float geometryMargin=1;
    for(unsigned scene=0;scene<4;++scene)for(unsigned receiver=0;receiver<2;++receiver){
        c=constants(W*2,H*2);productionMatrices(c,scene);c[103]=receiver?std::array<float,4>{.3f,-.4f,.8660254f,0}:std::array<float,4>{0,0,1,0};c[102][0]=24;
        gpu.bind(2,gc);gpu.bind(3,gc);gpu.bind(6,go);gpu.bind(7,go);gpu.bind(8,clearTex);gpu.bind(9,clearTex);
        const auto currentExpected=gpu.capture(originalVisibility,c,W,H);c[89]={1,1,0,0};const auto currentActual=gpu.capture(direct,c,W,H);
        auto oldConstants=c;for(unsigned row=0;row<4;++row){oldConstants[7+row]=c[80+row];oldConstants[11+row]=c[84+row];}oldConstants[16]=c[88];oldConstants[99]=c[98];gpu.bind(2,go);gpu.bind(3,go);
        const auto oldExpected=gpu.capture(originalVisibility,oldConstants,W,H);gpu.bind(2,gc);gpu.bind(3,gc);c[89]={0,0,0,0};const auto oldActual=gpu.capture(direct,c,W,H);
        for(unsigned i=0;i<oldActual.size();i+=4)for(unsigned endpoint=0;endpoint<2;++endpoint){const double e=std::fabs(double(endpoint?currentActual[i]:oldActual[i])-(endpoint?currentExpected[i]:oldExpected[i]));geometryEndpointError=std::max(geometryEndpointError,e);failures+=e>3e-5;++geometryEndpoints;}
        gpu.bind(2,gu);gpu.bind(3,gu);gpu.bind(6,go);gpu.bind(7,go);gpu.bind(8,gd);gpu.bind(9,gd);
        const auto center=c[104];
        for(float depthShift:{0.f,-1280.f,1280.f})for(float span:{24.f,300.f}){
            c[102][0]=span;for(unsigned k=0;k<3;++k)c[104][k]=center[k]+c[16][k]*depthShift;
            for(auto weights:std::array<std::array<float,2>,3>{{{0,0},{.25f,.75f},{1,1}}}){c[89]={weights[0],weights[1],0,0};
                for(unsigned pass=0;pass<2;++pass){const auto actual=gpu.capture(pass?fog:direct,c,W,H);
                    for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const V3 p{((x+.5f)/W-.5f)*span+c[104][0],((y+.5f)/H-.5f)*span+c[104][1],c[104][2]};comparisonMargin=coordinateMargin=1;float expected;
                        if(pass){const auto nq=point(p,c,11),oq=point(p,c,84);const float nv=fogPCF(nq,false,geometricUnion,geometricDynamic,c);float ov=fogPCF(oq,true,geometricOld,geometricDynamic,c);const float u=oq[0]*.5f+.5f+.5f*c[2][3],v=.5f-oq[1]*.5f+.5f*c[2][3];if(u<0||u>1||v<0||v>1||oq[2]<0||oq[2]>1)ov=nv;expected=ov*(1-weights[1])+nv*weights[1];}
                        else expected=referenceDirect(p,geometricUnion,geometricOld,geometricDynamic,c);
                        const auto q=point(p,c,11);negativeDepthChecks+=q[2]<0||q[2]>1;geometryMargin=std::min(geometryMargin,comparisonMargin);const float value=actual[(y*W+x)*4];filteredGeometry+=value>.01f&&value<.99f;const double e=std::fabs(double(value)-expected);geometryError=std::max(geometryError,e);
                        if(e>3e-4&&failures<24)std::printf("3D mismatch scene=%u receiver=%u depth=%g span=%g pass=%u pixel=%u error=%g margin=%g\n",scene,receiver,depthShift,span,pass,y*W+x,e,comparisonMargin);failures+=!std::isfinite(value)||e>3e-4;++geometryChecks;
                    }
                }
            }
        }
    }
    failures+=geometryMargin<.001f||negativeDepthChecks==0||filteredGeometry<100;
    std::printf("3D production geometry checks=%u endpoints=%u error=%g endpoint=%g margin=%g clipped=%u partial=%u failures=%u\n",geometryChecks,geometryEndpoints,geometryError,geometryEndpointError,geometryMargin,negativeDepthChecks,filteredGeometry,failures);std::fflush(stdout);
    gpu.bind(8,dynamicTex);gpu.bind(9,dynamicTex);
    // Keep one-ULP comparison ties in a separate original-bytecode GPU oracle.
    // Scalar fixtures above have a proved margin; tolerances are unchanged.
    auto tieStatic=Map([](float u,float v){const int x=int(u*1024),y=int(v*1024);return (x%17==8||y%43<3)?1.f:(x/17%3==1?.49995f:.4f);});
    auto tieUnion=Map([&](float u,float v){return std::min(tieStatic.at(u,v),dynamic.at(u,v));});
    auto tieStaticTex=gpu.tex(tieStatic.image()),tieUnionTex=gpu.tex(tieUnion.image());
    double tieError=0;unsigned tieChecks=0;
    c=constants(W*2,H*2);c[103]={.3f,-.4f,.8660254f,0};gpu.bind(2,tieUnionTex);gpu.bind(3,tieUnionTex);gpu.bind(6,tieStaticTex);gpu.bind(7,tieStaticTex);
    for(float span:{73.13f,167.31f,380.23f}){c[102][0]=span;const auto expected=gpu.capture(originalVisibility,c,W,H);
        for(float f:{0.f,.5f,1.f}){c[89]={f,f,0,0};const auto actual=gpu.capture(direct,c,W,H);for(unsigned i=0;i<actual.size();i+=4){double e=std::fabs(double(actual[i])-expected[i]);tieError=std::max(tieError,e);if(e>3e-5&&failures<24)std::printf("GPU tie mismatch span=%g fade=%g pixel=%u expected=%.9g actual=%.9g\n",span,f,i/4,expected[i],actual[i]);failures+=e>3e-5;++tieChecks;}}
    }
    std::printf("GPU original tie endpoints checks=%u maxError=%g failures=%u scalarMargin=%g\n",tieChecks,tieError,failures,minimumReferenceMargin);std::fflush(stdout);
    // Whole entry endpoints, monotonic thin-gap change, ambient and extinction invariants.
    const char* names[]={"WorldLighting","WorldFog","FogClouds"};IDirect3DPixelShader9* baseline[3]{},*candidate[3]{};
    double createCpu[3][2]{};std::array<double,3> coldDraw[3][2]{};
    for(unsigned i=0;i<3;++i){baseline[i]=gpu.shader(dir+"/"+names[i]+".bin");createCpu[i][0]=gpu.lastCreateCpuMs;candidate[i]=gpu.shader(dir+"/"+names[i]+"Fade.bin");createCpu[i][1]=gpu.lastCreateCpuMs;}
    gpu.bind(1,solid(1000.f/999.8f*(1-.2f/160),0,0,1));gpu.bind(14,solid(0,0,1,1));
    auto field=gpu.tex(rgbaImage(64,64,[](unsigned x,unsigned y){return std::array<float,4>{-.2f,.008f,.004f,(x/5+y/7)%5?5.f:0.f};}));gpu.bind(13,field);
    IDirect3DVolumeTexture9* noise{};ok(gpu.d->CreateVolumeTexture(16,16,16,1,0,D3DFMT_L8,D3DPOOL_MANAGED,&noise,nullptr));D3DLOCKED_BOX box{};ok(noise->LockBox(0,&box,nullptr,0));for(unsigned z=0;z<16;++z)for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x)*((unsigned char*)box.pBits+z*box.SlicePitch+y*box.RowPitch+x)=static_cast<unsigned char>((x*37+y*17+z*53)%256);ok(noise->UnlockBox(0));
    double endpointError=0,alphaError=0,ambientError=0,ghostError=0,monotonicError=0,thinGapChange=0,movingShadowContrast=0,movingContrast[3]{};unsigned wholeChecks=0;
    for(unsigned pass=0;pass<3;++pass){
        if(pass==2){gpu.bind(14,noise);for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(gpu.d->SetSamplerState(14,state,D3DTEXF_LINEAR));for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,D3DSAMP_ADDRESSW})ok(gpu.d->SetSamplerState(14,state,D3DTADDRESS_WRAP));}
        c=constants(W*2,H*2);gpu.bind(2,unionTex);gpu.bind(3,unionTex);gpu.bind(6,oldTex);gpu.bind(7,oldTex);gpu.bind(8,dynamicTex);gpu.bind(9,dynamicTex);
        if(argc==5){auto native=constants(1728,1117);native[89]={.5f,.5f,0,0};for(unsigned mode=0;mode<2;++mode)coldDraw[pass][mode]=gpu.firstDraw(mode?candidate[pass]:baseline[pass],native,864,558,pass==0);}
        const auto current=gpu.capture(baseline[pass],c,W,H);c[89]={1,1,0,0};const auto newEnd=gpu.capture(candidate[pass],c,W,H);
        for(unsigned i=0;i<current.size();++i){double e=std::fabs(double(current[i])-newEnd[i]);endpointError=std::max(endpointError,e);failures+=!std::isfinite(e)||e>3e-5;++wholeChecks;}
        gpu.bind(2,oldUnionTex);gpu.bind(3,oldUnionTex);const auto previousReference=gpu.capture(baseline[pass],c,W,H);
        gpu.bind(2,unionTex);gpu.bind(3,unionTex);c[89]={0,0,0,0};const auto oldEnd=gpu.capture(candidate[pass],c,W,H);
        for(unsigned i=0;i<oldEnd.size();++i){thinGapChange=std::max(thinGapChange,std::fabs(double(oldEnd[i])-newEnd[i]));double e=std::fabs(double(oldEnd[i])-previousReference[i]);endpointError=std::max(endpointError,e);failures+=e>3e-5;}
        std::vector<std::vector<float>> frames{previousReference};
        std::vector<float> previous;
        for(float f:{0.f,.25f,.5f,.75f,1.f}){c[89]={f,f,0,0};const auto pixels=gpu.capture(candidate[pass],c,W,H);for(unsigned i=0;i<pixels.size();++i){if(i%4==3&&pass){double e=std::fabs(double(current[i])-pixels[i]);alphaError=std::max(alphaError,e);failures+=e>2e-6;}if(!std::isfinite(pixels[i]))++failures;
                if(!previous.empty()){const double sign=newEnd[i]>=oldEnd[i]?1:-1;double e=std::max(0.,sign*(previous[i]-pixels[i]));monotonicError=std::max(monotonicError,e);failures+=e>3e-5;}
            }previous=pixels;frames.push_back(pixels);}
        frames.push_back(current);
        c[17]={0,0,0,0};const auto ambient=gpu.capture(baseline[pass],c,W,H);
        for(float f:{0.f,.5f,1.f}){c[89]={f,f,0,0};const auto pixels=gpu.capture(candidate[pass],c,W,H);for(unsigned i=0;i<pixels.size();++i){double e=std::fabs(double(ambient[i])-pixels[i]);ambientError=std::max(ambientError,e);failures+=e>3e-5;}}
        // Histories contain static geometry only. A caster absent from today's
        // dynamic map cannot leave a shadow at its previous location at any fade.
        c=constants(W*2,H*2);gpu.bind(2,dynamicTex);gpu.bind(3,dynamicTex);gpu.bind(6,clearTex);gpu.bind(7,clearTex);
        const auto movingCurrent=gpu.capture(baseline[pass],c,W,H);
        gpu.bind(2,clearTex);gpu.bind(3,clearTex);const auto noCaster=gpu.capture(baseline[pass],c,W,H);gpu.bind(2,dynamicTex);gpu.bind(3,dynamicTex);
        for(unsigned i=0;i<movingCurrent.size();++i)movingContrast[pass]=std::max(movingContrast[pass],std::fabs(double(movingCurrent[i])-noCaster[i]));
        movingShadowContrast=std::max(movingShadowContrast,movingContrast[pass]);failures+=movingContrast[pass]<(pass?1e-4:.01);
        frames.push_back(movingCurrent);panel(dir+"/"+names[pass]+".panel.ppm",frames,W,H,pass==0);
        for(float f:{0.f,.25f,.5f,.75f,1.f}){c[89]={f,f,0,0};const auto pixels=gpu.capture(candidate[pass],c,W,H);for(unsigned i=0;i<pixels.size();++i){double e=std::fabs(double(movingCurrent[i])-pixels[i]);ghostError=std::max(ghostError,e);failures+=e>3e-5;}}
        if(pass==0){const auto baseOld=gpu.capture(baseline[pass],c,W,H,1);c[89]={.5f,.5f,0,0};const auto baseNew=gpu.capture(candidate[pass],c,W,H,1);for(unsigned i=0;i<baseOld.size();i+=4)for(unsigned k=0;k<3;++k){double e=std::fabs(double(baseOld[i+k])-baseNew[i+k]);ambientError=std::max(ambientError,e);failures+=e>3e-5;}}
    }
    failures+=thinGapChange<.01||movingShadowContrast<.01;
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
    double snapshotMedian=0,snapshotP95=0;
    if(argc==5){Constants cc{};cc[0]={128,128,0,1280};cc[1]={1.f/1024,1.f/1280,0,0};auto samples=gpu.time(snapshot,cc,1024,1024,D3DFMT_R32F);std::sort(samples.begin(),samples.end());snapshotMedian=samples[samples.size()/2];snapshotP95=samples.back();}
    gpu.bind(1,solid(1000.f/999.8f*(1-.2f/160),0,0,1));
    std::fprintf(report,"{\"reference_checks\":%u,\"reference_max_error\":%.9g,\"reference_min_comparison_margin\":%.9g,\"geometry_3d_checks\":%u,\"geometry_3d_max_error\":%.9g,\"geometry_3d_min_margin\":%.9g,\"geometry_3d_endpoints\":%u,\"geometry_3d_endpoint_max_error\":%.9g,\"geometry_3d_clipped_depth_checks\":%u,\"geometry_3d_partial_visibility_checks\":%u,\"gpu_tie_checks\":%u,\"gpu_tie_max_error\":%.9g,\"whole_checks\":%u,\"endpoint_max_error\":%.9g,\"transmittance_max_error\":%.9g,\"ambient_max_error\":%.9g,\"moving_caster_max_error\":%.9g,\"monotonic_max_error\":%.9g,\"thin_gap_max_change\":%.9g,\"moving_shadow_contrast\":%.9g,\"crop_checks\":%u,\"crop_max_error\":%.9g,\"snapshot_median_ms\":%.6f,\"snapshot_p95_ms\":%.6f,\"failures\":%u,\"cold_resources\":[",checks,referenceError,minimumReferenceMargin,geometryChecks,geometryError,geometryMargin,geometryEndpoints,geometryEndpointError,negativeDepthChecks,filteredGeometry,tieChecks,tieError,wholeChecks,endpointError,alphaError,ambientError,ghostError,monotonicError,thinGapChange,movingShadowContrast,cropChecks,cropError,snapshotMedian,snapshotP95,failures);std::fflush(report);
    if(argc==5)for(unsigned slot=0;slot<4;++slot)std::fprintf(report,"%s{\"slot\":%u,\"create_8MiB_cpu_ms\":%.6f,\"first_clear_gpu_ms\":%.6f}",slot?",":"",slot,resourceCold[slot][0],resourceCold[slot][1]);
    std::fprintf(report,"],\"cold_first_draw\":[");
    if(argc==5)for(unsigned pass=0;pass<3;++pass)std::fprintf(report,"%s{\"pass\":\"%s\",\"baseline_create_ps_cpu_ms\":%.6f,\"fade_create_ps_cpu_ms\":%.6f,\"baseline_first_submit_cpu_ms\":%.6f,\"fade_first_submit_cpu_ms\":%.6f,\"baseline_first_sync_wall_ms\":%.6f,\"fade_first_sync_wall_ms\":%.6f,\"baseline_first_gpu_span_ms\":%.6f,\"fade_first_gpu_span_ms\":%.6f}",pass?",":"",names[pass],createCpu[pass][0],createCpu[pass][1],coldDraw[pass][0][0],coldDraw[pass][1][0],coldDraw[pass][0][1],coldDraw[pass][1][1],coldDraw[pass][0][2],coldDraw[pass][1][2]);
    std::fprintf(report,"],\"timings\":[");
    double transitionIncrement=0;
    if(argc==5){unsigned rows=0;for(unsigned pass=0;pass<3;++pass){
        constexpr unsigned FW=1728,FH=1117,HW=FW/2,HH=FH/2;c=constants(FW,FH);c[89]={.5f,.5f,0,0};gpu.bind(2,unionTex);gpu.bind(3,unionTex);gpu.bind(6,oldTex);gpu.bind(7,oldTex);gpu.bind(8,dynamicTex);gpu.bind(9,dynamicTex);
        if(pass<2){gpu.bind(14,solid(0,0,1,1));}else gpu.bind(14,noise);
        std::vector<double> times[2];for(unsigned round=0;round<4;++round)for(unsigned j=0;j<2;++j){unsigned mode=(round&1)?1-j:j;auto samples=gpu.time(mode?candidate[pass]:baseline[pass],c,HW,HH);times[mode].insert(times[mode].end(),samples.begin(),samples.end());}
        double med[2],p95[2];for(unsigned mode=0;mode<2;++mode){std::sort(times[mode].begin(),times[mode].end());med[mode]=times[mode][times[mode].size()/2];p95[mode]=times[mode][times[mode].size()*95/100];}
        transitionIncrement+=med[1]-med[0];
        std::fprintf(report,"%s{\"pass\":\"%s\",\"width\":%u,\"height\":%u,\"baseline_ms\":%.6f,\"fade_ms\":%.6f,\"baseline_p95_ms\":%.6f,\"fade_p95_ms\":%.6f,\"duty_1s_per_9s_delta_ms\":%.6f}",rows++?",":"",names[pass],HW,HH,med[0],med[1],p95[0],p95[1],(med[1]-med[0])/9);std::fflush(report);
        std::printf("%s baseline=%.6f fade=%.6f increment=%.6f dutyAvg=%.6f\n",names[pass],med[0],med[1],med[1]-med[0],(med[1]-med[0])/9);std::fflush(stdout);
    }}
    // Seven one-second fades at t=0,9,...54 in sixty seconds. Two simultaneous
    // sources are a conservative upper bound; clouds normally draw once.
    const double scheduleDuty=7./60.;
    std::fprintf(report,"],\"schedule_seconds\":60,\"schedule_fades_per_source\":7,\"schedule_two_source_average_delta_ms\":%.6f,\"schedule_four_slot_snapshot_average_ms\":%.6f,\"schedule_two_source_total_average_delta_ms\":%.6f}\n",transitionIncrement*2*scheduleDuty,snapshotMedian*4*7/60,transitionIncrement*2*scheduleDuty+snapshotMedian*4*7/60);std::fclose(report);gpu.bind(14,nullptr);noise->Release();std::printf("whole endpoint=%g alpha=%g ambient=%g moving=%g failures=%u\n",endpointError,alphaError,ambientError,ghostError,failures);return failures?1:0;
}
