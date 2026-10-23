#pragma once
// SPDX-License-Identifier: MIT
// F12-armed, synchronous observation immediately before the real game draw.
// No setters, rendering, retained COM objects, or periodic buffer readbacks.
#include "device_mirror.h"
#include "spell_layer.h"
#include "replay_copies.h"
#include "upload_lock.h"
#include "actor_deformation.h"
#include "shader_tags.h"
#include "atlas_gpu_diagnostic.h"
#include <array>
#include <limits>
#include <climits>

namespace NorthlightUiDrawDiagnostic {
constexpr unsigned MaxDraws=4,MaxProbes=8192,MaxVertices=4096,MaxIndices=12288,MaxBytes=256u<<10,MaxShaderBytes=64u<<10;
struct Window {
    std::string prefix;unsigned frame=0,left=0,captured=0,probes=0;std::array<const void*,MaxDraws> seen{};
    void arm(const std::string& p,unsigned f){prefix=p;frame=f;left=MaxDraws;captured=0;probes=0;seen.fill(nullptr);}
    bool active(unsigned f){if(left&&unsigned(f-frame)>2)left=0;return left!=0;}
    bool probe(unsigned f){if(!active(f))return false;if(probes++>=MaxProbes){left=0;return false;}return true;}
    bool claim(const void* atlas,unsigned f){if(!atlas||!active(f))return false;for(auto* p:seen)if(p==atlas)return false;seen[captured++]=atlas;--left;return true;}
};
template<class T> struct Ref {T* p=nullptr;~Ref(){if(p)p->Release();}T** out(){return &p;}};
struct File {FILE* p=nullptr;explicit File(const std::string& path):p(std::fopen(path.c_str(),"wb")){}~File(){if(p)std::fclose(p);}bool close(){const bool good=std::ferror(p)==0;const int closed=std::fclose(p);p=nullptr;return good&&closed==0;}};
struct Bytes {std::vector<unsigned char> data;const char* source="not-read";HRESULT hr=D3DERR_INVALIDCALL,unlockHr=S_OK;DWORD flags=0;};
inline bool copyUp(const void* data,std::uint64_t offset,unsigned size,Bytes& out){
    if(!data||!size||size>MaxBytes||offset>std::numeric_limits<std::uintptr_t>::max()-reinterpret_cast<std::uintptr_t>(data))return false;
    out.data.resize(size);SIZE_T copied=0;
    const bool ok=ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(reinterpret_cast<std::uintptr_t>(data)+std::uintptr_t(offset)),out.data.data(),size,&copied)&&copied==size;
    out.source="UP-payload";out.hr=ok?S_OK:E_FAIL;if(!ok)out.data.clear();return ok;
}
template<class Buffer> bool read(Buffer* buffer,unsigned offset,unsigned size,Bytes& out){
    if(!buffer||!size||size>MaxBytes)return false;
    out.data.resize(size);NorthlightReplayCopies::Slot* slot=nullptr;
    if(const auto* bytes=NorthlightReplayCopies::read(buffer,offset,size,slot)){
        struct Pin {NorthlightReplayCopies::Slot* slot;~Pin(){NorthlightReplayCopies::unpin(*slot);}} pin{slot};
        std::memcpy(out.data.data(),bytes,size);out.source="valid-replay-CPU-copy";out.hr=S_OK;return true;
    }
    void* bytes=nullptr;out.flags=NorthlightUpload::readBackLock();out.source="F12-backend-read-lock";out.hr=buffer->Lock(offset,size,&bytes,out.flags);
    if(FAILED(out.hr)){out.data.clear();return false;}
    struct Unlock {Buffer* buffer;Bytes& out;~Unlock(){out.unlockHr=buffer->Unlock();}} unlock{buffer,out};
    if(!bytes){out.hr=E_POINTER;out.data.clear();return false;}
    std::memcpy(out.data.data(),bytes,size);return true;
}
inline std::uint64_t vertexCount(unsigned primitive,unsigned count){
    switch(primitive){case 1:return count;case 2:return std::uint64_t(count)*2;case 3:return std::uint64_t(count)+1;case 4:return std::uint64_t(count)*3;case 5:case 6:return std::uint64_t(count)+2;default:return 0;}
}
inline unsigned elementBytes(unsigned type){constexpr unsigned sizes[]={4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8};return type<17?sizes[type]:0;}
inline bool range(unsigned first,unsigned last,unsigned stride,unsigned extent,unsigned offset,unsigned length,unsigned budget,unsigned& begin,unsigned& size){
    if(last<first||std::uint64_t(last)-first>=MaxVertices||!stride||stride>4096||!extent||extent>stride)return false;
    const std::uint64_t start=std::uint64_t(offset)+std::uint64_t(first)*stride,bytes=std::uint64_t(last-first)*stride+extent;
    if(start>length||bytes>length-start||bytes>budget)return false;begin=unsigned(start);size=unsigned(bytes);return true;
}
inline bool save(const std::string& path,const void* data,std::size_t size){return size&&NorthlightAtlasDiagnostic::save(path,data,size);}
inline void bytesReport(FILE* f,const char* file,const Bytes& bytes,bool saved){std::fprintf(f,"\"file\":\"%s\",\"saved\":%s,\"source\":\"%s\",\"size\":%u,\"lock_flags\":%u,\"read_hresult\":\"%08x\",\"unlock_hresult\":\"%08x\"",file,saved?"true":"false",bytes.source,unsigned(bytes.data.size()),unsigned(bytes.flags),unsigned(bytes.hr),unsigned(bytes.unlockHr));}
template<class Shader> void shader(FILE* f,IDirect3DDevice9*,Shader* actual,const void* mirrored,bool known,HRESULT hr,const std::string& prefix,const char* kind){
    UINT size=0;HRESULT codeHr=actual?actual->GetFunction(nullptr,&size):D3DERR_NOTFOUND;std::vector<unsigned char> bytes;
    if(SUCCEEDED(codeHr)&&size&&size<=MaxShaderBytes){bytes.resize(size);codeHr=actual->GetFunction(bytes.data(),&size);if(FAILED(codeHr)||size>bytes.size())bytes.clear();else bytes.resize(size);}
    else if(size>MaxShaderBytes)codeHr=E_OUTOFMEMORY;
    const auto hash=bytes.empty()?0:NorthlightShaderTags::fnv1a(bytes.data(),bytes.size());
    const std::string suffix=std::string(".")+kind+".bin";const bool saved=save(prefix+suffix,bytes.data(),bytes.size());
    std::fprintf(f,"{\"actual\":\"%p\",\"get_hresult\":\"%08x\",\"mirror_known\":%s,\"mirror\":\"%p\",\"matches_mirror\":%s,\"code_hresult\":\"%08x\",\"code_bytes\":%u,\"fnv1a64\":\"%016llx\",\"file_suffix\":\"%s\",\"saved\":%s}",actual,unsigned(hr),known?"true":"false",mirrored,known&&SUCCEEDED(hr)&&actual==mirrored?"true":"false",unsigned(codeHr),unsigned(bytes.size()),(unsigned long long)hash,suffix.c_str(),saved?"true":"false");
}
inline void constants(FILE* f,IDirect3DDevice9* d,const DeviceMirror& mirror,const std::string& prefix,bool vertex){
    const unsigned count=vertex?DeviceMirror::VsFloat:DeviceMirror::PsFloat;std::array<std::array<float,4>,DeviceMirror::VsFloat> values{};
    const HRESULT hr=vertex?d->GetVertexShaderConstantF(0,values[0].data(),count):d->GetPixelShaderConstantF(0,values[0].data(),count);
    const char* suffix=vertex?".vs-float.bin":".ps-float.bin";const bool saved=SUCCEEDED(hr)&&save(prefix+suffix,values.data(),count*16);
    std::fprintf(f,"{\"get_hresult\":\"%08x\",\"register_count\":%u,\"file_suffix\":\"%s\",\"saved\":%s,\"mirror_known\":[",unsigned(hr),count,suffix,saved?"true":"false");
    bool comma=false;for(unsigned n=0;n<count;++n)if(vertex?mirror.vsFloatKnown[n]:mirror.psFloatKnown[n]){std::fprintf(f,"%s%u",comma?",":"",n);comma=true;}
    std::fprintf(f,"],\"mirror_differences\":[");comma=false;
    if(SUCCEEDED(hr))for(unsigned n=0;n<count;++n){const bool known=vertex?mirror.vsFloatKnown[n]:mirror.psFloatKnown[n];const auto* expected=vertex?mirror.vsFloat[n]:mirror.psFloat[n];if(known&&std::memcmp(expected,values[n].data(),16)){
        std::uint32_t bits[4];std::memcpy(bits,expected,16);std::fprintf(f,"%s{\"register\":%u,\"mirror_bits\":[%u,%u,%u,%u]}",comma?",":"",n,bits[0],bits[1],bits[2],bits[3]);comma=true;}}
    std::fprintf(f,"]}");
}
inline bool capture(IDirect3DDevice9* d,const DeviceMirror& mirror,const NorthlightSpellLayer::DrawArgs& args,const std::string& prefix,unsigned frame,const void* atlas){
    using NorthlightSpellLayer::DrawType;
    File report(prefix+".draw.json");if(!report.p)return false;FILE* f=report.p;
    const bool indexed=args.type==DrawType::Indexed||args.type==DrawType::IndexedUP,up=args.type==DrawType::PrimitiveUP||args.type==DrawType::IndexedUP;
    std::fprintf(f,"{\"frame\":%u,\"atlas\":\"%p\",\"phase\":\"immediately before real game draw submission after extension preparation\",\"mirror_active\":%s,\"draw\":{\"kind\":%u,\"primitive\":%u,\"primitive_count\":%u,\"base_vertex\":%d,\"min_index\":%u,\"num_vertices\":%u,\"first\":%u,\"UP_stride\":%u},",frame,atlas,mirror.active()?"true":"false",unsigned(args.type),unsigned(args.prim),args.primCount,args.baseVertex,args.minIndex,args.numVertices,args.first,args.stride);
    Ref<IDirect3DVertexShader9> vs;Ref<IDirect3DPixelShader9> ps;
    const HRESULT vsHr=d->GetVertexShader(vs.out()),psHr=d->GetPixelShader(ps.out());
    std::fprintf(f,"\"vertex_shader\":");shader(f,d,vs.p,mirror.vertexShader,mirror.vertexShaderKnown,vsHr,prefix,"vs");
    std::fprintf(f,",\"pixel_shader\":");shader(f,d,ps.p,mirror.pixelShader,mirror.pixelShaderKnown,psHr,prefix,"ps");
    std::fprintf(f,",\"vs_float_constants\":");constants(f,d,mirror,prefix,true);std::fprintf(f,",\"ps_float_constants\":");constants(f,d,mirror,prefix,false);
    DWORD fvf=0;const HRESULT fvfHr=d->GetFVF(&fvf);Ref<IDirect3DVertexDeclaration9> declaration;const HRESULT declarationHr=d->GetVertexDeclaration(declaration.out());
    std::array<D3DVERTEXELEMENT9,65> elements{};UINT elementCount=unsigned(elements.size());HRESULT elementsHr=declaration.p?declaration.p->GetDeclaration(elements.data(),&elementCount):D3DERR_NOTFOUND;
    if(FAILED(elementsHr)||elementCount>elements.size()){elementCount=0;elementsHr=D3DERR_INVALIDCALL;}
    std::fprintf(f,",\"fvf\":{\"actual\":%u,\"get_hresult\":\"%08x\",\"mirror_known\":%s,\"mirror\":%u,\"matches_mirror\":%s},\"declaration\":{\"actual\":\"%p\",\"get_hresult\":\"%08x\",\"mirror_known\":%s,\"mirror\":\"%p\",\"matches_mirror\":%s,\"elements_hresult\":\"%08x\",\"elements\":[",unsigned(fvf),unsigned(fvfHr),mirror.fvfKnown?"true":"false",unsigned(mirror.fvf),mirror.fvfKnown&&SUCCEEDED(fvfHr)&&mirror.fvf==fvf?"true":"false",declaration.p,unsigned(declarationHr),mirror.declarationKnown?"true":"false",mirror.declaration,mirror.declarationKnown&&SUCCEEDED(declarationHr)&&mirror.declaration==declaration.p?"true":"false",unsigned(elementsHr));
    std::array<unsigned,16> extents{};
    for(unsigned n=0;n<elementCount;++n){const auto& e=elements[n];std::fprintf(f,"%s[%u,%u,%u,%u,%u,%u]",n?",":"",unsigned(e.Stream),unsigned(e.Offset),unsigned(e.Type),unsigned(e.Method),unsigned(e.Usage),unsigned(e.UsageIndex));if(e.Stream<16&&elementBytes(e.Type))extents[e.Stream]=std::max(extents[e.Stream],unsigned(e.Offset)+elementBytes(e.Type));}
    std::fprintf(f,"]},\"textures\":[");
    for(unsigned stage=0;stage<16;++stage){Ref<IDirect3DBaseTexture9> texture;const HRESULT hr=d->GetTexture(stage,texture.out());D3DSURFACE_DESC desc{};HRESULT descHr=D3DERR_NOTFOUND;unsigned levels=0;
        if(texture.p&&texture.p->GetType()==D3DRTYPE_TEXTURE){auto* t=static_cast<IDirect3DTexture9*>(texture.p);descHr=t->GetLevelDesc(0,&desc);levels=t->GetLevelCount();}
        std::fprintf(f,"%s{\"stage\":%u,\"actual\":\"%p\",\"get_hresult\":\"%08x\",\"mirror_known\":%s,\"mirror\":\"%p\",\"matches_mirror\":%s,\"desc_hresult\":\"%08x\",\"width\":%u,\"height\":%u,\"format\":%u,\"levels\":%u,\"samplers\":[",stage?",":"",stage,texture.p,unsigned(hr),mirror.textureKnown[stage]?"true":"false",mirror.textures[stage],mirror.textureKnown[stage]&&SUCCEEDED(hr)&&texture.p==mirror.textures[stage]?"true":"false",unsigned(descHr),desc.Width,desc.Height,unsigned(desc.Format),levels);
        for(unsigned state=1;state<14;++state){DWORD value=0;const HRESULT readHr=d->GetSamplerState(stage,D3DSAMPLERSTATETYPE(state),&value);const bool known=mirror.samplerKnown[stage][state];std::fprintf(f,"%s{\"type\":%u,\"actual\":%u,\"get_hresult\":\"%08x\",\"mirror_known\":%s,\"mirror\":%u,\"matches_mirror\":%s}",state>1?",":"",state,unsigned(value),unsigned(readHr),known?"true":"false",unsigned(mirror.samplers[stage][state]),known&&SUCCEEDED(readHr)&&value==mirror.samplers[stage][state]?"true":"false");}
        std::fprintf(f,"]}");}
    std::fprintf(f,"],\"render_states\":[");
    const D3DRENDERSTATETYPE states[]={D3DRS_ZENABLE,D3DRS_ZWRITEENABLE,D3DRS_ZFUNC,D3DRS_ALPHATESTENABLE,D3DRS_ALPHAREF,D3DRS_ALPHAFUNC,D3DRS_ALPHABLENDENABLE,D3DRS_SRCBLEND,D3DRS_DESTBLEND,D3DRS_BLENDOP,D3DRS_SEPARATEALPHABLENDENABLE,D3DRS_SRCBLENDALPHA,D3DRS_DESTBLENDALPHA,D3DRS_BLENDOPALPHA,D3DRS_COLORWRITEENABLE,D3DRS_SRGBWRITEENABLE,D3DRS_DITHERENABLE,D3DRS_SCISSORTESTENABLE,D3DRS_CULLMODE,D3DRS_LIGHTING,D3DRS_FOGENABLE,D3DRS_CLIPPLANEENABLE};
    for(unsigned n=0;n<sizeof(states)/sizeof(states[0]);++n){DWORD value=0;const unsigned type=states[n];const HRESULT hr=d->GetRenderState(states[n],&value);std::fprintf(f,"%s{\"type\":%u,\"actual\":%u,\"get_hresult\":\"%08x\",\"mirror_known\":%s,\"mirror\":%u,\"matches_mirror\":%s}",n?",":"",type,unsigned(value),unsigned(hr),mirror.renderStateKnown[type]?"true":"false",unsigned(mirror.renderStates[type]),mirror.renderStateKnown[type]&&SUCCEEDED(hr)&&value==mirror.renderStates[type]?"true":"false");}
    D3DVIEWPORT9 viewport{};RECT scissor{};const HRESULT viewportHr=d->GetViewport(&viewport),scissorHr=d->GetScissorRect(&scissor);
    std::fprintf(f,"],\"viewport\":{\"get_hresult\":\"%08x\",\"actual\":[%u,%u,%u,%u],\"mirror_known\":%s,\"matches_mirror\":%s},\"scissor\":{\"get_hresult\":\"%08x\",\"actual\":[%ld,%ld,%ld,%ld]}",unsigned(viewportHr),unsigned(viewport.X),unsigned(viewport.Y),unsigned(viewport.Width),unsigned(viewport.Height),mirror.viewportKnown?"true":"false",mirror.viewportKnown&&SUCCEEDED(viewportHr)&&!std::memcmp(&viewport,&mirror.viewport,sizeof viewport)?"true":"false",unsigned(scissorHr),long(scissor.left),long(scissor.top),long(scissor.right),long(scissor.bottom));
    std::uint32_t depthRange[2]{};std::memcpy(depthRange,&viewport.MinZ,sizeof depthRange);std::fprintf(f,",\"viewport_depth_range_bits\":[%u,%u],\"surfaces\":[",depthRange[0],depthRange[1]);
    for(unsigned n=0;n<2;++n){Ref<IDirect3DSurface9> surface;const HRESULT hr=n?d->GetDepthStencilSurface(surface.out()):d->GetRenderTarget(0,surface.out());D3DSURFACE_DESC desc{};const HRESULT descHr=surface.p?surface.p->GetDesc(&desc):D3DERR_NOTFOUND;const bool known=n?mirror.depthKnown:mirror.targetKnown[0];const void* expected=n?mirror.depth:mirror.targets[0];
        std::fprintf(f,"%s{\"kind\":\"%s\",\"actual\":\"%p\",\"get_hresult\":\"%08x\",\"desc_hresult\":\"%08x\",\"width\":%u,\"height\":%u,\"format\":%u,\"multisample\":%u,\"mirror_known\":%s,\"mirror\":\"%p\",\"matches_mirror\":%s}",n?",":"",n?"depth":"target0",surface.p,unsigned(hr),unsigned(descHr),desc.Width,desc.Height,unsigned(desc.Format),unsigned(desc.MultiSampleType),known?"true":"false",expected,known&&SUCCEEDED(hr)&&surface.p==expected?"true":"false");}
    std::fprintf(f,"]");
    const std::uint64_t count=vertexCount(unsigned(args.prim),args.primCount);std::vector<unsigned> indices;unsigned budget=MaxBytes,first=0,last=0;bool geometry=count&&count<=(indexed?MaxIndices:MaxVertices);
    Ref<IDirect3DIndexBuffer9> ib;D3DINDEXBUFFER_DESC ibDesc{};const HRESULT ibHr=d->GetIndices(ib.out());const HRESULT ibDescHr=ib.p?ib.p->GetDesc(&ibDesc):D3DERR_NOTFOUND;
    Bytes indexBytes;unsigned indexSize=0;D3DFORMAT indexFormat=up?args.indexFormat:ibDesc.Format;
    if(indexed&&geometry){indexSize=indexFormat==D3DFMT_INDEX16?2:indexFormat==D3DFMT_INDEX32?4:0;const std::uint64_t start=std::uint64_t(args.first)*indexSize,size=count*indexSize;
        if(!indexSize||size>budget)geometry=false;
        else if(up)geometry=copyUp(args.indexData,0,unsigned(size),indexBytes);
        else geometry=SUCCEEDED(ibHr)&&SUCCEEDED(ibDescHr)&&start<=ibDesc.Size&&size<=ibDesc.Size-start&&read(ib.p,unsigned(start),unsigned(size),indexBytes);
        if(geometry&&FAILED(indexBytes.unlockHr))geometry=false;
        if(geometry){budget-=unsigned(size);for(unsigned n=0;n<count;++n){unsigned value=0;std::memcpy(&value,indexBytes.data.data()+n*indexSize,indexSize);indices.push_back(value);}}
    }else if(geometry){if(std::uint64_t(up?0:args.first)+count-1>UINT_MAX)geometry=false;else for(unsigned n=0;n<count;++n)indices.push_back((up?0:args.first)+n);}
    if(geometry){first=*std::min_element(indices.begin(),indices.end());last=*std::max_element(indices.begin(),indices.end());if(std::uint64_t(last)-first>=MaxVertices)geometry=false;
        if(indexed&&up&&(first<args.minIndex||std::uint64_t(last)>=std::uint64_t(args.minIndex)+args.numVertices))geometry=false;}
    std::fprintf(f,",\"indices\":{\"actual\":\"%p\",\"get_hresult\":\"%08x\",\"desc_hresult\":\"%08x\",\"mirror_known\":%s,\"mirror\":\"%p\",\"matches_mirror\":%s,\"format\":%u,\"start_index\":%u,",ib.p,unsigned(ibHr),unsigned(ibDescHr),mirror.indicesKnown?"true":"false",mirror.indices,mirror.indicesKnown&&SUCCEEDED(ibHr)&&mirror.indices==ib.p?"true":"false",unsigned(indexFormat),args.first);
    bytesReport(f,".ib",indexBytes,save(prefix+".ib",indexBytes.data.data(),indexBytes.data.size()));std::fprintf(f,",\"buffer_usage\":%u,\"buffer_pool\":%u},\"referenced_indices\":[",unsigned(ibDesc.Usage),unsigned(ibDesc.Pool));
    for(unsigned n=0;n<indices.size();++n)std::fprintf(f,"%s%u",n?",":"",indices[n]);std::fprintf(f,"],\"geometry_range_valid\":%s,\"streams\":[",geometry?"true":"false");
    std::vector<unsigned> unique=indices;std::sort(unique.begin(),unique.end());unique.erase(std::unique(unique.begin(),unique.end()),unique.end());
    for(unsigned stream=0;stream<16;++stream){Ref<IDirect3DVertexBuffer9> vb;UINT offset=0,stride=0,frequency=0;const HRESULT hr=d->GetStreamSource(stream,vb.out(),&offset,&stride),freqHr=d->GetStreamSourceFreq(stream,&frequency);D3DVERTEXBUFFER_DESC desc{};const HRESULT descHr=vb.p?vb.p->GetDesc(&desc):D3DERR_NOTFOUND;
        const auto& mirrored=mirror.streams[stream];const bool same=mirrored.known&&SUCCEEDED(hr)&&vb.p==mirrored.buffer&&offset==mirrored.offset&&stride==mirrored.stride;
        const std::int64_t beginVertex=std::int64_t(first)+(indexed&&!up?args.baseVertex:0),endVertex=std::int64_t(last)+(indexed&&!up?args.baseVertex:0);
        unsigned begin=0,size=0;Bytes bytes;bool got=false;const bool readable=geometry&&extents[stream]&&beginVertex>=0&&endVertex>=beginVertex&&endVertex<=UINT_MAX;
        const unsigned dataStride=up?args.stride:stride;
        if(up&&stream==0&&readable&&range(unsigned(beginVertex),unsigned(endVertex),args.stride,extents[stream],0,UINT_MAX,budget,begin,size)){got=copyUp(args.vertexData,begin,size,bytes);}
        else if(!up&&readable&&SUCCEEDED(hr)&&SUCCEEDED(descHr)&&SUCCEEDED(freqHr)&&frequency==1&&range(unsigned(beginVertex),unsigned(endVertex),stride,extents[stream],offset,desc.Size,budget,begin,size))got=read(vb.p,begin,size,bytes);
        if(FAILED(bytes.unlockHr))got=false;if(got)budget-=size;
        const std::string suffix=".s"+std::to_string(stream)+".vb";const bool saved=got&&save(prefix+suffix,bytes.data.data(),bytes.data.size());
        std::fprintf(f,"%s{\"stream\":%u,\"actual\":\"%p\",\"get_hresult\":\"%08x\",\"desc_hresult\":\"%08x\",\"buffer_size\":%u,\"offset\":%u,\"stride\":%u,\"frequency\":%u,\"frequency_hresult\":\"%08x\",\"mirror_known\":%s,\"mirror\":\"%p\",\"mirror_offset\":%u,\"mirror_stride\":%u,\"matches_mirror\":%s,\"mirror_frequency_known\":%s,\"mirror_frequency\":%u,\"frequency_matches_mirror\":%s,\"read_offset\":%u,\"first_buffer_vertex\":%lld,\"vertex_range_count\":%u,",stream?",":"",stream,vb.p,unsigned(hr),unsigned(descHr),desc.Size,offset,stride,frequency,unsigned(freqHr),mirrored.known?"true":"false",mirrored.buffer,mirrored.offset,mirrored.stride,same?"true":"false",mirror.frequencyKnown[stream]?"true":"false",mirror.frequency[stream],mirror.frequencyKnown[stream]&&SUCCEEDED(freqHr)&&frequency==mirror.frequency[stream]?"true":"false",begin,(long long)beginVertex,geometry?last-first+1:0);
        const char* reason=got?"captured":!geometry?"geometry-range-rejected":!extents[stream]?"no-declaration-elements":up&&stream?"UP-only-stream0":!readable?"base-vertex-range":!up&&frequency!=1?"unsupported-frequency":size?"read-failed":"bounds-or-budget";
        bytesReport(f,suffix.c_str(),bytes,saved);std::fprintf(f,",\"buffer_usage\":%u,\"buffer_pool\":%u,\"capture_reason\":\"%s\",\"data_stride\":%u,\"UV_samples\":[",unsigned(desc.Usage),unsigned(desc.Pool),reason,dataStride);bool comma=false;
        if(got)for(unsigned e=0;e<elementCount;++e){const auto& element=elements[e];if(element.Stream!=stream||element.Method!=D3DDECLMETHOD_DEFAULT||element.Usage!=D3DDECLUSAGE_TEXCOORD||!elementBytes(element.Type))continue;
            for(unsigned n=0;n<std::min(unsigned(unique.size()),64u);++n){const unsigned vertex=unique[n];const std::uint64_t at=std::uint64_t(vertex-first)*dataStride+element.Offset;if(at+elementBytes(element.Type)>bytes.data.size())continue;NorthlightActorDeformation::Four value{};
                if(NorthlightActorDeformation::decodeElement(bytes.data.data()+at,element.Type,value)&&std::isfinite(value[0])&&std::isfinite(value[1])){std::fprintf(f,"%s{\"index\":%u,\"usage_index\":%u,\"uv\":[%.9g,%.9g]}",comma?",":"",vertex,unsigned(element.UsageIndex),double(value[0]),double(value[1]));comma=true;}}}
        std::fprintf(f,"]}");}
    std::fprintf(f,"],\"note\":\"Capture observes draw submission, not visible pixels. Byte bounds: 4096 vertex range, 12288 indices, 256KiB total VB/IB; 64 UV samples per TEXCOORD element. Unknown/unsupported or failed reads are explicit. Shader floats are binary; int/bool constants and vertex textures are not captured. No setters or rendering are performed.\"}\n");
    return report.close();
}
}
