#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <d3d9.h>
#include "forwarders.h"
#include "particle_light_capture.h"
#include <cassert>
#ifndef NORTHLIGHT_PARTICLE_PROBE_REFERENCE
#define NORTHLIGHT_PARTICLE_PROBE_REFERENCE 0
#endif
#include <cstdio>
#include <limits>

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
    ULONG refs=1;unsigned bytes=8;Indices():ForwardIDirect3DIndexBuffer9(nullptr){}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{assert(refs>1);return --refs;}
    HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC* out)override{*out={D3DFMT_INDEX16,D3DRTYPE_INDEXBUFFER,0,D3DPOOL_DEFAULT,bytes};return D3D_OK;}
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
    assert(argc==2||argc==3);const bool influenceOnly=argc==3&&std::strcmp(argv[2],"--view-influence-only")==0;assert(argc==2||influenceOnly);
    FILE* file=std::fopen(argv[1],"rb");assert(file);Shader vertex,white,mesh;vertex.code=words(file);white.code=words(file);mesh.code=words(file);const auto pixel=words(file);
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
    if(!influenceOnly){
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
    for(unsigned scenario=0;scenario<9;++scenario)for(unsigned up=0;up<2;++up){
        if(scenario==4&&up)continue; // A raw UP pointer has no bounded upload-miss case.
        frame.reset();selection.reset();uploads.clear();
        vertices[0]={{-.2f,-.2f,2},packed(nativeTint),{.5f,.5f}};
        vertices[1]={{.2f,-.2f,2},packed(nativeTint),{.5f,.5f}};
        vertices[2]={{0,.4f,2},packed(nativeTint),{.5f,.5f}};
        if(scenario==6)for(auto& v:vertices)v.position[2]=60;
        if(scenario==7)for(auto& v:vertices)v.position[0]+=30;
        if(scenario==8)for(auto& v:vertices){v.position[2]=3;v.color=D3DCOLOR_ARGB(192,64,128,255);}
        const unsigned triangles=scenario==1?16:scenario==2||scenario==4?2:scenario==3?3:1;
        std::array<std::uint16_t,49> references{};references[0]=99;
        for(unsigned i=0;i<48;++i)references[i+1]=std::uint16_t((up?0:1)+i%3);
        if(scenario==2||scenario==3||scenario==5)references[1]=99;
        if(scenario==3)references[4]=99;
        if(scenario==4)references[1]=10;
        args=indexed;args.primCount=triangles;if(scenario==4)args.numVertices=12;
        if(up){args.type=NorthlightSpellLayer::DrawType::IndexedUP;args.baseVertex=0;args.minIndex=0;args.first=0;args.vertexData=vertices.data();args.stride=sizeof(Vertex);args.indexData=references.data()+1;}
        uploads.write(&device.vertices,1,device.streamOffset,vertices.data(),unsigned(sizeof vertices),false);
        uploads.write(&device.indices,1,0,references.data(),unsigned(sizeof references),false);
        const auto before=uploads.stats();const auto result=prepare(vertex);const auto after=uploads.stats();
        const bool empty=scenario==5||scenario==6;assert(bool(result.count)==!empty);
        const unsigned expectedTriangles=scenario==5||scenario==6?0:scenario==2||scenario==3||scenario==4?1:triangles;
        if(frame.triangles!=expectedTriangles){std::fputs("PARTICLE_PROBE_TRIANGLE_MISMATCH\n",stderr);std::abort();}
        capture.submit(result,true,false,frame);const auto shownCase=publish();assert(bool(shownCase.count)==!empty);
        uint64_t hash=1469598103934665603ull;const auto addHash=[&](const void* data,size_t size){const auto* p=static_cast<const unsigned char*>(data);for(size_t i=0;i<size;++i)hash=(hash^p[i])*1099511628211ull;};
        addHash(result.camera,sizeof result.camera);
        for(unsigned i=0;i<result.count;++i){const auto& c=result.clusters[i];addHash(c.sample.position,sizeof c.sample.position);addHash(c.sample.rgb,sizeof c.sample.rgb);addHash(&c.sample.brightest,sizeof c.sample.brightest);addHash(&c.count,sizeof c.count);}
        for(unsigned i=0;i<shownCase.count;++i){addHash(shownCase.position[i].data(),sizeof shownCase.position[i]);addHash(shownCase.color[i].data(),sizeof shownCase.color[i]);}
        std::printf("PARTICLE_PROBE_CASE scenario=%u up=%u prepared=%u selected=%u hash=%llu triangles=%u draws=%u probes=%u distant=%u bufferMiss=%u uploadHits=%llu uploadMisses=%llu\n",scenario,up,result.count,shownCase.count,(unsigned long long)hash,frame.triangles,frame.draws,frame.probes,frame.distant,frame.bufferMiss,(unsigned long long)(after.hits-before.hits),(unsigned long long)(after.misses-before.misses));
    }
    }
    // Authored geometry with stock native shaders and actual Cold Blood texels.
    // Only the camera basis changes: both the source and receiver stay in world space.
    const auto require=[](bool ok,const char* reason){if(!ok){std::fprintf(stderr,"PARTICLE_INFLUENCE_%s\n",reason);std::abort();}};
    const auto resetScene=[&](){frame.reset();selection.reset();uploads.clear();fixed.clear();device.source=D3DBLEND_SRCALPHA;device.alphaTest=TRUE;device.alphaRef=1;device.alphaFunc=D3DCMP_GREATEREQUAL;device.indices.bytes=8;args=NorthlightSpellLayer::DrawArgs::indexed(D3DPT_TRIANGLELIST,-1,1,3,1,1);};
    const auto setTriangle=[&](float x,float z){vertices={{{{x-.2f,-.2f,z},packed(nativeTint),{.5f,.5f}},{{x+.2f,-.2f,z},packed(nativeTint),{.5f,.5f}},{{x,.4f,z},packed(nativeTint),{.5f,.5f}}}};};
    const auto useUP=[&](){args.type=NorthlightSpellLayer::DrawType::PrimitiveUP;args.first=0;args.vertexData=vertices.data();args.stride=sizeof(Vertex);};
    float referenceRgb[3]{},referenceRadius=0;
    for(unsigned rotation=0;rotation<2;++rotation)for(unsigned up=0;up<2;++up){
        resetScene();setTriangle(2.6f,2.f);float rotatedInverse[16];std::copy(inverse,inverse+16,rotatedInverse);
        const float angle=rotation?.5235987756f:0.f,c=std::cos(angle),s=std::sin(angle);
        rotatedInverse[0]=c;rotatedInverse[2]=-s;rotatedInverse[8]=s;rotatedInverse[10]=c;
        for(auto& v:vertices){const float x=v.position[0],z=v.position[2];v.position[0]=c*x-s*z;v.position[2]=s*x+c*z;}
        if(up)useUP();else upload();
        const auto readsBefore=uploads.stats();
        const auto result=capture.prepare(&device,&vertex,&ps,args,rotatedInverse,camera,projection,frame);
        const auto readsAfter=uploads.stats();
        require(readsAfter.hits-readsBefore.hits==(up?0u:NORTHLIGHT_PARTICLE_PROBE_REFERENCE?12u:6u)&&readsAfter.misses==readsBefore.misses,"INPUT_READS");
        require(frame.triangles==1&&frame.draws==1,"INPUT_REUSE");
        require(result.count==1,"ROTATION_MISMATCH");const auto& source=result.clusters[0].sample;
        require(std::fabs(source.position[0]-102.6f)<.0001f&&std::fabs(source.position[1]-200.f)<.0001f&&std::fabs(source.position[2]-302.f)<.0001f,"WORLD_POSITION");
        for(unsigned k=0;k<3;++k){if(!rotation&&!up)referenceRgb[k]=source.rgb[k];require(close(source.rgb[k],referenceRgb[k]),"RADIANCE");}
        const float receiverX=c*1.9f-s*2.f,receiverZ=s*1.9f+c*2.f;
        require(receiverZ>0&&std::fabs(receiverX)<receiverZ,"VISIBLE_RECEIVER");
        const float centerX=c*2.6f-s*2.f,centerZ=s*2.6f+c*2.f;
        const bool oldCentroid=centerZ>0&&std::fabs(centerX)<=1.2f*centerZ;require(oldCentroid==bool(rotation),"OLD_GATE_REFERENCE");
        capture.submit(result,true,false,frame);frame.publish(nullptr,0,fixed,camera,selection);
        const auto view=NorthlightLocalLightScissor::view(rotatedInverse,projection,.1f,.1f,1280,720);
        const auto selected=selection.update(fixed,camera,8,1.f/120,true,&view,frame.output.data(),frame.outputCount,frame.immediate.data(),frame.immediateCount);
        require(selected.count==1,"RECEIVER_SELECTION");
        // Independent LocalDirect reference: the receiver normal faces +X,
        // directly toward the source, so Lambert=1 and reach squared is positive.
        const float reach=std::clamp((selected.position[0][3]-.7f)*selected.color[0][3],0.f,1.f);
        const float receiverEnergy=NorthlightParticleLights::peak(selected.sources[0].diffuse)*reach*reach;
        require(receiverEnergy>0,"RECEIVER_ENERGY");
        require(selected.position[0][3]<=6.f,"RADIUS_CAP");if(!rotation&&!up)referenceRadius=selected.position[0][3];
        std::printf("PARTICLE_INFLUENCE_CASE case=rotation rotation=%u up=%u prepared=%u selected=%u oldCentroid=%u receiverDistance=0.7 receiverEnergy=%.6f radius=%.6f uploadHits=%llu uploadMisses=%llu triangles=%u draws=%u\n",rotation,up,result.count,selected.count,unsigned(oldCentroid),receiverEnergy,selected.position[0][3],(unsigned long long)(readsAfter.hits-readsBefore.hits),(unsigned long long)(readsAfter.misses-readsBefore.misses),frame.triangles,frame.draws);
    }
    // A sloped triangle crosses the eye/near plane; a behind-eye centroid's
    // bounded source still reaches a visible receiver .25 yards ahead.
    resetScene();setTriangle(0,-1);vertices[0].position[2]=vertices[1].position[2]=-3;vertices[2].position[2]=3;useUP();
    auto influence=prepare(vertex);require(influence.count==1,"NEAR_CROSSING");capture.submit(influence,true,false,frame);auto influenceShown=publish();
    require(influenceShown.count==1&&NorthlightLocalLights::attenuation(influenceShown.sources[0],1.25f)>0,"NEAR_RECEIVER"); // Receiver normal faces -Z.
    std::puts("PARTICLE_INFLUENCE_CASE case=near-crossing prepared=1 selected=1 oldCentroid=0 receiverDistance=1.25");
    resetScene();setTriangle(2.6f,2);vertices[0].position[0]=-.2f;vertices[1].position[0]=.2f;vertices[2].position[0]=7.8f;useUP();
    influence=prepare(vertex);require(influence.count==1,"LARGE_TRIANGLE");
    std::puts("PARTICLE_INFLUENCE_CASE case=large-triangle prepared=1 oldCentroid=0");
    // A weak neighbour outside its OWN sphere still affects the position/hue
    // of a visible bright cluster. Culling individual samples is incorrect.
    resetScene();device.source=D3DBLEND_ONE;device.alphaTest=FALSE;
    float weakRgb[3];for(unsigned k=0;k<3;++k)weakRgb[k]=texel[k]*float(k==1?192:128)/255;
    const float brightPeak=NorthlightParticleLights::peak(texel),weakPeak=NorthlightParticleLights::peak(weakRgb);
    const float weakRadius=2+4*std::sqrt(std::min(1.f,.7f*weakPeak)),brightRadius=2+4*std::sqrt(std::min(1.f,.7f*brightPeak));
    require(weakPeak>=.003f&&weakRadius<brightRadius,"CLUSTER_INPUTS");
    const float weakX=2+std::sqrt(2.f)*(weakRadius+.25f*(brightRadius-weakRadius)),brightX=weakX-.5f;
    std::array<Vertex,6> mixed;setTriangle(weakX,2);for(auto& v:vertices)v.color=D3DCOLOR_ARGB(255,128,192,128);std::copy(vertices.begin(),vertices.end(),mixed.begin());
    setTriangle(brightX,2);for(auto& v:vertices)v.color=D3DCOLOR_ARGB(255,255,255,255);std::copy(vertices.begin(),vertices.end(),mixed.begin()+3);
    useUP();args.vertexData=mixed.data();args.primCount=2;influence=prepare(vertex);
    require(influence.count==1&&influence.clusters[0].count==2,"MIXED_CLUSTER");const auto& merged=influence.clusters[0].sample;
    for(unsigned k=0;k<3;++k)require(close(merged.rgb[k],(weakRgb[k]+texel[k])*.5f),"CLUSTER_HUE");
    require(std::fabs(merged.position[0]-(100+(weakX+brightX)*.5f))<.0001f&&close(merged.brightest,brightPeak),"CLUSTER_POSITION");
    capture.submit(influence,true,false,frame);frame.publish(nullptr,0,fixed,camera,selection);
    const float clusterX=(weakX+brightX)*.5f,receiverForward=clusterX*.5f+1;
    const float clusterDistance=std::sqrt((clusterX-(receiverForward-.01f))*(clusterX-(receiverForward-.01f))+(2-receiverForward)*(2-receiverForward));
    require(frame.outputCount==1&&NorthlightLocalLights::attenuation(frame.output[0],clusterDistance)>0,"CLUSTER_RECEIVER");
    std::printf("PARTICLE_INFLUENCE_CASE case=mixed-cluster prepared=1 samples=2 weakOutsideOwnSphere=1 receiverDistance=%.6f radius=%.6f\n",clusterDistance,frame.output[0].attenuationEnd);
    // The existing track can lag a current source by less than two yards.
    // Test the final tracked light, rather than replacing it with the sample.
    resetScene();const float currentX=2+std::sqrt(2.f)*(referenceRadius+.1f),oldX=currentX-1,dt=1.f/120;
    setTriangle(oldX,2);useUP();influence=prepare(vertex);require(influence.count==1,"TRACK_INITIAL");capture.submit(influence,true,false,frame);frame.publish(nullptr,0,fixed,camera,selection,dt);
    require(frame.outputCount==1,"TRACK_INITIAL_OUTPUT");const auto trackId=frame.output[0].sourceId;
    frame.nextFrame();setTriangle(currentX,2);influence=prepare(vertex);require(influence.count==1,"TRACK_CURRENT");capture.submit(influence,true,false,frame);frame.publish(nullptr,0,fixed,camera,selection,dt);
    const float trackedX=oldX+(1-std::exp(-dt/NorthlightParticleLights::FollowSeconds));
    require(frame.outputCount==1&&frame.output[0].sourceId==trackId&&std::fabs(frame.output[0].position[0]-(100+trackedX))<.0001f,"TRACK_FOLLOW");
    const float trackReceiverForward=trackedX*.5f+1,trackDistance=std::sqrt((trackedX-(trackReceiverForward-.01f))*(trackedX-(trackReceiverForward-.01f))+(2-trackReceiverForward)*(2-trackReceiverForward));
    require(NorthlightLocalLights::attenuation(frame.output[0],trackDistance)>0&&(currentX-2)/std::sqrt(2.f)>frame.output[0].attenuationEnd,"TRACK_RECEIVER");
    std::printf("PARTICLE_INFLUENCE_CASE case=moving-track prepared=1 sameId=1 currentOutsideOwnSphere=1 receiverDistance=%.6f radius=%.6f\n",trackDistance,frame.output[0].attenuationEnd);
    resetScene();setTriangle(0,2);for(auto& v:vertices)v.position[0]=NAN;useUP();require(!prepare(vertex).count&&frame.draws==0,"INVALID_POSITION");
    resetScene();setTriangle(2.6f,2);useUP();device.constants[8]=std::numeric_limits<float>::max();const float invalidProjection[3]{device.constants[8],1,1};
    require(!capture.prepare(&device,&vertex,&ps,args,inverse,camera,invalidProjection,frame).count,"INVALID_CLIP");device.constants[8]=1;
    std::puts("PARTICLE_INFLUENCE_CASE case=finite invalidPositionRejected=1 overflowClipRejected=1");
    for(unsigned shear=0;shear<2;++shear){
        resetScene();float scaledInverse[16];std::copy(inverse,inverse+16,scaledInverse);scaledInverse[0]=.1f;
        if(shear){scaledInverse[8]=-1;scaledInverse[10]=.1f;setTriangle(30,20);}else setTriangle(21,2);
        useUP();const auto result=capture.prepare(&device,&vertex,&ps,args,scaledInverse,camera,projection,frame);
        require(result.count==1,"WORLD_TRANSFORM");const auto& source=result.clusters[0].sample;
        require(std::fabs(source.position[0]-(shear?83.f:102.1f))<.0001f&&std::fabs(source.position[2]-302.f)<.0001f,"SCALED_WORLD_POSITION");
        const float receiverViewX=shear?19.f:1.9f,receiverViewZ=shear?20.f:2.f;
        const float receiverX=scaledInverse[12]+scaledInverse[0]*receiverViewX+scaledInverse[8]*receiverViewZ;
        const float receiverZ=scaledInverse[14]+scaledInverse[2]*receiverViewX+scaledInverse[10]*receiverViewZ;
        require(std::fabs(receiverViewX)<receiverViewZ&&std::fabs(receiverZ-source.position[2])<.0001f,"SCALED_VISIBLE_RECEIVER");
        capture.submit(result,true,false,frame);frame.publish(nullptr,0,fixed,camera,selection);
        require(frame.outputCount==1&&NorthlightLocalLights::attenuation(frame.output[0],std::fabs(source.position[0]-receiverX))>0,"SCALED_RECEIVER_ENERGY");
        std::printf("PARTICLE_INFLUENCE_CASE case=world-transform shear=%u prepared=1 receiverDistance=%.6f radius=%.6f\n",shear,std::fabs(source.position[0]-receiverX),frame.output[0].attenuationEnd);
    }
    // The original first-come budgets remain. Extra RGBA work for formerly
    // clipped samples is bounded, and is recorded rather than called a speedup.
    resetScene();setTriangle(30,2);upload();args.primCount=NorthlightParticleLights::TrianglesPerDraw;
    std::array<std::uint16_t,49> junkIndices{};junkIndices[0]=99;
    for(unsigned i=0;i<48;++i)junkIndices[i+1]=std::uint16_t(1+i%3);
    device.indices.bytes=sizeof junkIndices;uploads.write(&device.indices,1,0,junkIndices.data(),sizeof junkIndices,false);
    const auto junkBefore=uploads.stats();
    for(unsigned i=0;i<NorthlightParticleLights::ProbeLimit;++i)require(bool(prepare(vertex).count)==(i<NorthlightParticleLights::DrawLimit),"MULTI_OUTSIDE_ADMISSION");
    const auto junkAfter=uploads.stats();
    const unsigned reads=(NorthlightParticleLights::DrawLimit*NorthlightParticleLights::TrianglesPerDraw+NorthlightParticleLights::ProbeLimit-NorthlightParticleLights::DrawLimit+NORTHLIGHT_PARTICLE_PROBE_REFERENCE*NorthlightParticleLights::DrawLimit)*6;
    require(junkAfter.hits-junkBefore.hits==reads&&junkAfter.misses==junkBefore.misses,"MULTI_OUTSIDE_READS");
    require(frame.probes==128&&frame.draws==32&&frame.triangles==512&&frame.dropped==96,"MULTI_OUTSIDE_COUNTS");
    // Every triangle uses the same known texel/colour, so all four RGBA samples run.
    std::printf("PARTICLE_INFLUENCE_CASE case=multi-outside probes=%u draws=%u triangles=%u uploadHits=%u uploadMisses=0 rgbaSamples=%u dropped=%u\n",frame.probes,frame.draws,frame.triangles,reads,frame.triangles*4,frame.dropped);
    // The world-coordinate bound belongs AFTER Prepared averaging. A weak
    // member just outside it can still contribute to an admitted final cluster.
    resetScene();device.source=D3DBLEND_ONE;device.alphaTest=FALSE;
    float boundaryInverse[16];std::copy(inverse,inverse+16,boundaryInverse);boundaryInverse[12]=99999;
    const float boundaryCamera[3]{99999,200,300};
    setTriangle(1.125f,2);for(auto& v:vertices)v.color=D3DCOLOR_ARGB(255,128,192,128);std::copy(vertices.begin(),vertices.end(),mixed.begin());
    setTriangle(.25f,2);for(auto& v:vertices)v.color=D3DCOLOR_ARGB(255,255,255,255);std::copy(vertices.begin(),vertices.end(),mixed.begin()+3);
    useUP();args.vertexData=mixed.data();args.primCount=2;
    influence=capture.prepare(&device,&vertex,&ps,args,boundaryInverse,boundaryCamera,projection,frame);
    require(influence.count==1&&influence.clusters[0].count==2,"BOUNDARY_CLUSTER");
    const auto& boundary=influence.clusters[0].sample;
    require(boundary.position[0]==99999.6875f&&close(boundary.brightest,brightPeak),"BOUNDARY_POSITION");
    for(unsigned k=0;k<3;++k)require(close(boundary.rgb[k],(weakRgb[k]+texel[k])*.5f),"BOUNDARY_HUE");
    capture.submit(influence,true,false,frame);frame.publish(nullptr,0,fixed,boundaryCamera,selection);
    require(frame.points==1&&frame.outputCount==1&&frame.output[0].position[0]==99999.6875f,"BOUNDARY_ADMISSION");
    std::puts("PARTICLE_INFLUENCE_CASE case=boundary-cluster prepared=1 samples=2 weakWorldX=100000.125 finalWorldX=99999.6875 admitted=1");
    assert(device.vertices.refs==1&&device.indices.refs==1&&device.texture.refs==1&&device.declaration.refs==1);
    std::puts("PASS actual SDK Capture::prepare: native Particle and white ParticleUnlit VS, native fog PS, Cold Blood texture/tint, indexed offsets, failed/deferred/immediate submits, frame expiry, additive alpha and dark upload misses");
}
