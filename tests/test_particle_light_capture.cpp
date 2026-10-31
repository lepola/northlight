#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <d3d9.h>
#include "forwarders.h"
#include "particle_light_capture.h"
#include <cassert>
#include <chrono>
#include <cstdio>

// The real SDK interfaces, backed only by CPU bytes. Any accidental device or
// driver readback reaches a nullptr forwarder instead of silently succeeding.
struct Shader final:IDirect3DVertexShader9 {
    std::vector<DWORD> code;ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs>1);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetFunction(void* data,UINT* size)override{
        if(!size)return E_POINTER;const UINT n=UINT(code.size()*4);if(data){if(*size<n)return D3DERR_INVALIDCALL;std::memcpy(data,code.data(),n);}*size=n;return D3D_OK;
    }
};
struct Pixel final:IDirect3DPixelShader9 {
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs>1);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetFunction(void*,UINT*)override{assert(false);return E_NOTIMPL;}
};
struct Texture final:IDirect3DTexture9 {
    ULONG refs=1;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs>1);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID,const void*,DWORD,DWORD)override{assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID,void*,DWORD*)override{assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID)override{assert(false);return E_NOTIMPL;}
    DWORD STDMETHODCALLTYPE SetPriority(DWORD)override{assert(false);return 0;}
    DWORD STDMETHODCALLTYPE GetPriority()override{assert(false);return 0;}
    void STDMETHODCALLTYPE PreLoad()override{assert(false);}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType()override{return D3DRTYPE_TEXTURE;}
    DWORD STDMETHODCALLTYPE SetLOD(DWORD)override{assert(false);return 0;}
    DWORD STDMETHODCALLTYPE GetLOD()override{assert(false);return 0;}
    DWORD STDMETHODCALLTYPE GetLevelCount()override{return 1;} // PARTICLELIGHT textureMiss diagnostics
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE)override{assert(false);return E_NOTIMPL;}
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType()override{assert(false);return D3DTEXF_NONE;}
    void STDMETHODCALLTYPE GenerateMipSubLevels()override{assert(false);}
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level,D3DSURFACE_DESC* out)override{assert(!level&&out);*out={};out->Width=out->Height=32;out->Format=D3DFMT_DXT5;out->Pool=D3DPOOL_MANAGED;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT,IDirect3DSurface9**)override{assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE LockRect(UINT,D3DLOCKED_RECT*,const RECT*,DWORD)override{assert(false);return E_NOTIMPL;} // never locked: CPU uploads only
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT)override{assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT*)override{assert(false);return E_NOTIMPL;}
};
struct Declaration final:IDirect3DVertexDeclaration9 {
    ULONG refs=1;
    std::vector<D3DVERTEXELEMENT9> elements{{0,0,D3DDECLTYPE_FLOAT3,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
        {0,12,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},
        {0,16,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID,void**)override{return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs>1);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9**)override{assert(false);return E_NOTIMPL;}
    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9* out,UINT* count)override{
        if(!count||*count<elements.size())return D3DERR_INVALIDCALL;std::memcpy(out,elements.data(),elements.size()*sizeof(*out));*count=UINT(elements.size());return D3D_OK;
    }
};
struct Buffer final:ForwardIDirect3DVertexBuffer9 {
    ULONG refs=1;Buffer():ForwardIDirect3DVertexBuffer9(nullptr){}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs>1);return --refs;}
};
struct Indices final:ForwardIDirect3DIndexBuffer9 {
    ULONG refs=1;Indices():ForwardIDirect3DIndexBuffer9(nullptr){}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs>1);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC* out)override{*out={D3DFMT_INDEX16,D3DRTYPE_INDEXBUFFER,0,D3DPOOL_DEFAULT,8};return D3D_OK;}
};
struct Vertex {float position[3];D3DCOLOR color;float uv[2];};
static_assert(sizeof(Vertex)==24&&offsetof(Vertex,color)==12&&offsetof(Vertex,uv)==16,"native packed stream layout");
struct Device final:ForwardIDirect3DDevice9 {
    Texture texture;Declaration declaration;Buffer vertices;Indices indices;
    float constants[1024]{};DWORD source=D3DBLEND_SRCALPHA,alphaTest=TRUE,alphaRef=1,alphaFunc=D3DCMP_GREATEREQUAL;unsigned streamOffset=0;
    Device():ForwardIDirect3DDevice9(nullptr){
        constants[8]=constants[13]=constants[18]=constants[22]=1; // c2..5, clip.w = view.z
        constants[24]=constants[29]=1; // Native UV0 matrix.
        constants[124]=constants[129]=constants[134]=1; // c31..33 identity palette.
    }
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE type,DWORD* out)override{
        switch(type){case D3DRS_ALPHABLENDENABLE:*out=TRUE;break;case D3DRS_SRCBLEND:*out=source;break;
        case D3DRS_DESTBLEND:*out=D3DBLEND_ONE;break;case D3DRS_BLENDOP:*out=D3DBLENDOP_ADD;break;
        case D3DRS_ALPHATESTENABLE:*out=alphaTest;break;case D3DRS_ALPHAREF:*out=alphaRef;break;case D3DRS_ALPHAFUNC:*out=alphaFunc;break; // the game's own particle state (traced in play)
        case D3DRS_COLORWRITEENABLE:*out=15;break;case D3DRS_ZENABLE:*out=TRUE;break;
        default:assert(false);return E_NOTIMPL;}return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD sampler,D3DSAMPLERSTATETYPE type,DWORD* out)override{
        assert(sampler==0);switch(type){case D3DSAMP_SRGBTEXTURE:*out=0;break;
        case D3DSAMP_ADDRESSU:case D3DSAMP_ADDRESSV:*out=D3DTADDRESS_CLAMP;break;
        case D3DSAMP_MAGFILTER:case D3DSAMP_MINFILTER:*out=D3DTEXF_POINT;break;default:assert(false);return E_NOTIMPL;}return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT start,float* out,UINT count)override{
        assert(start==0&&count==256);std::memcpy(out,constants,sizeof(constants));return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9** out)override{*out=&declaration;declaration.AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD sampler,IDirect3DBaseTexture9** out)override{assert(sampler==0);*out=&texture;texture.AddRef();return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT stream,IDirect3DVertexBuffer9** out,UINT* offset,UINT* stride)override{
        assert(!stream);*out=&vertices;vertices.AddRef();*offset=streamOffset;*stride=sizeof(Vertex);return D3D_OK;
    }
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT stream,UINT* out)override{assert(!stream);*out=1;return D3D_OK;}
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9** out)override{*out=&indices;indices.AddRef();return D3D_OK;}
};
static std::vector<DWORD> words(FILE* file){unsigned size;assert(std::fread(&size,4,1,file)==1);std::vector<DWORD> out(size);assert(std::fread(out.data(),4,size,file)==size);return out;}
static unsigned family(const Shader& shader){std::uint64_t hash=14695981039346656037ull;for(auto byte:std::vector<unsigned char>(reinterpret_cast<const unsigned char*>(shader.code.data()),reinterpret_cast<const unsigned char*>(shader.code.data())+shader.code.size()*4))hash=(hash^byte)*1099511628211ull;return m2ShaderFamily(hash);}
static bool close(float a,float b){return std::fabs(a-b)<.00001f;}
int main(int argc,char** argv){
    assert(argc==2);FILE* file=std::fopen(argv[1],"rb");assert(file);Shader vertex,white,mesh;vertex.code=words(file);white.code=words(file);mesh.code=words(file);const auto pixel=words(file);
    unsigned image[5];assert(std::fread(image,4,5,file)==5);std::vector<unsigned char> payload(image[4]);assert(std::fread(payload.data(),1,payload.size(),file)==payload.size());
    float nativeTint[4];assert(std::fread(nativeTint,4,4,file)==4);
    NorthlightParticleTextures::enabled=true;Device device;
    auto& textures=NorthlightParticleTextures::cache();textures.upload(&device.texture,&device.texture,image[0],image[1],0,image[2],payload.data(),payload.size(),image[3],true,true);
    NorthlightParticleTextures::View texture;assert(textures.lookup(&device.texture,texture));float texel[4];assert(texture.sample(.5f,.5f,false,3,3,texel,false)&&texel[3]>.1f);
    Pixel ps;NorthlightParticleCapture::Capture capture;capture.registerVertex(&vertex,family(vertex));capture.registerVertex(&white,family(white));capture.registerPixel(&ps,pixel.data(),pixel.size());
    assert(family(mesh)==3);capture.registerVertex(&mesh,family(mesh));
    const auto packed=[](const float* c){return D3DCOLOR_ARGB(unsigned(std::round(c[3]*255)),unsigned(std::round(c[0]*255)),unsigned(std::round(c[1]*255)),unsigned(std::round(c[2]*255)));};
    std::array<Vertex,3> vertices{{{{-.2f,-.2f,2},packed(nativeTint),{.5f,.5f}},{{.2f,-.2f,2},packed(nativeTint),{.5f,.5f}},{{0,.4f,2},packed(nativeTint),{.5f,.5f}}}};
    const std::uint16_t indices[4]{99,1,2,3};auto& uploads=NorthlightParticleUploads::cache();
    device.streamOffset=8;
    const auto upload=[&](){uploads.write(&device.vertices,1,device.streamOffset,vertices.data(),unsigned(sizeof(vertices)),false);uploads.write(&device.indices,1,0,indices,sizeof(indices),false);};upload();
    auto args=NorthlightSpellLayer::DrawArgs::indexed(D3DPT_TRIANGLELIST,-1,1,3,1,1);
    const float inverse[16]{1,0,0,0,0,1,0,0,0,0,1,0,100,200,300,1},camera[3]{100,200,300},projection[3]{1,1,1};
    NorthlightParticleLights::Frame frame;NorthlightLocalLightSelection::Tracker selection;std::vector<NorthlightLocalLights::Light> fixed;
    const auto prepare=[&](Shader& shader){return capture.prepare(&device,&shader,&ps,args,inverse,camera,projection,frame);};
    const auto publish=[&](){frame.publish(nullptr,0,fixed,camera,selection);return selection.update(fixed,camera,8,1.f/120,true,nullptr,frame.output.data(),frame.outputCount,frame.immediate.data(),frame.immediateCount);};
    auto prepared=prepare(vertex);assert(prepared.count==1&&frame.triangles==1);
    assert(!prepared.clusters[0].sample.castsShadows);
    const auto& emitted=prepared.clusters[0].sample;assert(close(emitted.position[0],100)&&close(emitted.position[1],200)&&close(emitted.position[2],302));
    assert(emitted.rgb[2]>emitted.rgb[0]&&emitted.rgb[2]>emitted.rgb[1]);
    for(unsigned k=0;k<3;++k)assert(close(emitted.rgb[k],float((vertices[0].color>>(16-8*k))&255)/255*texel[k]*float((vertices[0].color>>24)&255)/255*texel[3]));
    capture.submit(prepared,false,false,frame);assert(publish().count==0&&frame.accepted==0);
    capture.submit(prepared,true,true,frame);auto shown=publish();assert(frame.accepted==1&&frame.deferred==1&&shown.count==1&&shown.color[0][2]>.01f);
    frame.nextFrame();assert(publish().count==0&&selection.fading==0); // No selection fade/TTL after native particles retire.
    prepared=prepare(vertex);capture.submit(prepared,true,false,frame);assert(publish().count==1&&frame.accepted==1&&frame.deferred==0);
    frame.nextFrame();const auto indexed=args;args.type=NorthlightSpellLayer::DrawType::PrimitiveUP;args.first=0;args.vertexData=vertices.data();args.stride=sizeof(Vertex);
    assert(prepare(vertex).count==1);args=indexed; // UP bytes use their pointer, never the bound stream's offset.
    frame.nextFrame();for(auto& v:vertices)v.color&=0xffffff;upload();assert(!prepare(vertex).count); // SRCALPHA / ONE must respect native alpha.
    frame.nextFrame();device.source=D3DBLEND_ONE;assert(!prepare(vertex).count); // ONE / ONE: the draw's alpha test (ref 1) drops alpha 0 too.
    frame.nextFrame();device.alphaTest=FALSE;assert(prepare(vertex).count==1);device.alphaTest=TRUE; // Without the test ONE / ONE ignores alpha.
    frame.nextFrame();device.alphaRef=NorthlightParticleCapture::MaxAlphaRef+1;assert(!prepare(vertex).count&&frame.stateMiss==1&&frame.stateWhy[NorthlightParticleLights::Frame::AlphaTest]==1);
    frame.nextFrame();device.alphaRef=1;device.alphaFunc=D3DCMP_LESS;assert(!prepare(vertex).count&&frame.stateWhy[NorthlightParticleLights::Frame::AlphaTest]==1);device.alphaFunc=D3DCMP_GREATEREQUAL;
    frame.nextFrame();device.source=D3DBLEND_SRCALPHA;for(auto& v:vertices)v.color=0;upload();
    const auto constant=prepare(white);assert(constant.count==1); // Native ParticleUnlit constant white is not VB COLOR0.
    for(unsigned k=0;k<3;++k)assert(close(constant.clusters[0].sample.rgb[k],texel[k]*texel[3]));
    frame.nextFrame();uploads.forget(&device.vertices);assert(!prepare(vertex).count&&frame.bufferMiss>0); // Miss stays dark; no Lock.
    frame.nextFrame();upload();textures.invalidate(&device.texture);assert(!prepare(vertex).count&&frame.textureMiss==1);
    textures.upload(&device.texture,&device.texture,image[0],image[1],0,image[2],payload.data(),payload.size(),image[3],true,true);
    for(auto& v:vertices)v.color=packed(nativeTint);upload();
    // Native unlit model mesh: compact complete draws qualify, large or
    // partially captured meshes retain illumination without shadow intent.
    for(unsigned k=0;k<4;++k)device.constants[28*4+k]=1;
    frame.nextFrame();const auto small=prepare(mesh);assert(small.count==1&&small.clusters[0].sample.castsShadows);
    capture.submit(small,true,false,frame);publish();assert(frame.outputCount==1&&(frame.output[0].flags&NorthlightLocalLights::CastShadow));
    vertices[0].position[0]=-4;upload();frame.nextFrame();const auto large=prepare(mesh);
    assert(large.count==1&&!large.clusters[0].sample.castsShadows);
    capture.submit(large,true,false,frame);publish();assert(frame.outputCount==1&&!frame.output[0].flags);
    vertices[0].position[0]=-.2f;upload();
    args.primCount=2;frame.nextFrame();const auto partial=prepare(mesh);
    assert(partial.count==1&&!partial.clusters[0].sample.castsShadows);args.primCount=1;
    struct CloudImage {unsigned info[5];std::vector<unsigned char> bytes;};
    unsigned imageCount=0;assert(std::fread(&imageCount,4,1,file)==1&&imageCount==2);std::vector<CloudImage> cloudImages(imageCount);
    for(auto& im:cloudImages){assert(std::fread(im.info,4,5,file)==5);im.bytes.resize(im.info[4]);assert(std::fread(im.bytes.data(),1,im.bytes.size(),file)==im.bytes.size());}
    unsigned cloudCases=0;assert(std::fread(&cloudCases,4,1,file)==1);unsigned activeImage=UINT32_MAX,acceptedClouds=0,cyanClouds=0;
    for(unsigned i=0;i<cloudCases;++i){
        unsigned label[4];float tint[4],uv[2],expectedTexel[4];
        assert(std::fread(label,4,4,file)==4&&std::fread(tint,4,4,file)==4&&std::fread(uv,4,2,file)==2&&std::fread(expectedTexel,4,4,file)==4);
        assert(label[0]<cloudImages.size());const auto& im=cloudImages[label[0]];
        if(activeImage!=label[0]){textures.upload(&device.texture,&device.texture,im.info[0],im.info[1],0,im.info[2],im.bytes.data(),im.bytes.size(),im.info[3],true,true);activeImage=label[0];}
        for(auto& v:vertices){v.color=packed(tint);v.uv[0]=uv[0];v.uv[1]=uv[1];}upload();frame.reset();selection={};
        NorthlightParticleTextures::View cloudTexture;float texel[4];assert(textures.lookup(&device.texture,cloudTexture)&&cloudTexture.sample(uv[0],uv[1],false,3,3,texel,false));
        // The independent Python decoder floors RGB565 expansion; the native
        // cache replicates bits. Bound that codec rounding before checking shade.
        for(unsigned k=0;k<4;++k)assert(std::fabs(texel[k]-expectedTexel[k])<=2.f/255);
        auto cloud=prepare(vertex);float expected[3];const float alpha=float((vertices[0].color>>24)&255)/255*texel[3];
        for(unsigned k=0;k<3;++k)expected[k]=float((vertices[0].color>>(16-8*k))&255)/255*texel[k]*alpha;
        const bool visible=NorthlightParticleLights::peak(expected)>=.003f&&alpha*255>=device.alphaRef;
        assert((cloud.count!=0)==visible);
        if(visible){assert(cloud.count==1&&!cloud.clusters[0].sample.castsShadows);
            for(unsigned k=0;k<3;++k)assert(std::fabs(cloud.clusters[0].sample.rgb[k]-expected[k])<.0001f);
            capture.submit(cloud,true,false,frame);assert(publish().count==1&&frame.outputCount==1&&!frame.output[0].flags);++acceptedClouds;
            if(label[1]==286&&label[2]>=2){assert(frame.output[0].diffuse[1]>frame.output[0].diffuse[0]&&frame.output[0].diffuse[2]>frame.output[0].diffuse[0]);++cyanClouds;}
            frame.nextFrame();assert(publish().count==0&&selection.fading==0);
        }
    }
    assert(acceptedClouds&&cyanClouds);assert(std::fgetc(file)==EOF);std::fclose(file);
    std::printf("SWAMP_GAS_CAPTURE_PASS cases=%u visible=%u cyan=%u actualDisplayPalettes=1 boundedDecode=1 paletteTintAlpha=1 diffuseOnly=1 noRetirementTail=1\n",cloudCases,acceptedClouds,cyanClouds);
    textures.upload(&device.texture,&device.texture,image[0],image[1],0,image[2],payload.data(),payload.size(),image[3],true,true);
    for(auto& v:vertices){v.color=packed(nativeTint);v.uv[0]=v.uv[1]=.5f;}upload();
    std::array<std::uint16_t,49> repeated{};for(unsigned i=1;i<repeated.size();++i)repeated[i]=std::uint16_t(1+(i-1)%3);
    uploads.write(&device.indices,1,0,repeated.data(),sizeof(repeated),false);
    for(unsigned triangles:{1u,16u}){
        args.primCount=triangles;std::array<double,7> elapsed{};unsigned checksum=0;
        for(auto& time:elapsed){const auto start=std::chrono::steady_clock::now();
            for(unsigned n=0;n<1000;++n){frame.nextFrame();checksum+=prepare(vertex).count;}
            time=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/1000;}
        std::sort(elapsed.begin(),elapsed.end());assert(checksum==7000);
        std::printf("warm Capture::prepare triangles=%u: median %.3f us/draw, 32 draws %.3f ms (1000 draws x7, CPU-only SDK backend)\n",triangles,elapsed[3],elapsed[3]*.032);
    }
    assert(device.vertices.refs==1&&device.indices.refs==1&&device.texture.refs==1&&device.declaration.refs==1);
    std::puts("PASS actual SDK Capture::prepare: native Particle and white ParticleUnlit VS, native fog PS, Cold Blood texture/tint, indexed offsets, failed/deferred/immediate submits, frame expiry, additive alpha and dark upload misses");
}
