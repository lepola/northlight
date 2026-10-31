#pragma once
#include "particle_light_proof.h"
#include "particle_texture_stats.h"
#include "particle_uploads.h"
#include "particle_lights.h"
#include "replay_copies.h"
#include "spell_layer.h"
#include <unordered_map>

// No texture/buffer Lock is allowed here. Upload-cache misses stay dark.
namespace NorthlightParticleCapture {
// Highest ALPHAREF accepted with a GREATER/GREATEREQUAL alpha test (the game uses 1).
inline constexpr DWORD MaxAlphaRef=8;
using Four=NorthlightActorDeformation::Four;
struct Prepared {
    std::array<NorthlightParticleLights::Cluster,4> clusters{};unsigned count=0;
    float camera[3]{};
    void add(const NorthlightParticleLights::Sample& sample){
        for(unsigned i=0;i<count;++i)if(NorthlightParticleLights::same(clusters[i].sample,sample)){NorthlightParticleLights::add(clusters[i],sample);return;}
        if(count<clusters.size())clusters[count++]={sample,1};
        else{unsigned worst=0;for(unsigned i=1;i<count;++i)if(
            NorthlightEquipmentLights::distance2(clusters[i].sample.position,camera)>NorthlightEquipmentLights::distance2(clusters[worst].sample.position,camera))worst=i;
            if(NorthlightEquipmentLights::distance2(sample.position,camera)<NorthlightEquipmentLights::distance2(clusters[worst].sample.position,camera))clusters[worst]={sample,1};}
    }
};
class Capture {
    struct Vertex {NorthlightActorDeformation::Program position;NorthlightParticleLightProof::Vertex material;bool valid=false;std::uint64_t used=0;};
    std::unordered_map<IDirect3DVertexShader9*,unsigned> families_;
    std::unordered_map<IDirect3DVertexShader9*,Vertex> vertices_;
    std::unordered_map<IDirect3DPixelShader9*,NorthlightParticleLightProof::Pixel> pixels_;
    std::uint64_t clock_=0;
    static bool readBuffer(const void* key,std::uint64_t offset,unsigned size,void* out){
        if(offset+size>UINT32_MAX)return false;
        if(NorthlightParticleUploads::cache().read(key,offset,size,out))return true;
        NorthlightReplayCopies::Slot* pin=nullptr;
        const auto* bytes=NorthlightReplayCopies::read(key,UINT(offset),size,pin);
        if(!bytes)return false;
        std::memcpy(out,bytes,size);NorthlightReplayCopies::unpin(*pin);return true;
    }
    Vertex* vertex(IDirect3DVertexShader9* shader){
        auto family=families_.find(shader);if(family==families_.end()||!family->second)return nullptr;
        auto found=vertices_.find(shader);if(found!=vertices_.end()){found->second.used=++clock_;return found->second.valid?&found->second:nullptr;}
        if(vertices_.size()>=64){auto old=std::min_element(vertices_.begin(),vertices_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});vertices_.erase(old);}
        Vertex v;v.used=++clock_;UINT size=0;
        if(SUCCEEDED(shader->GetFunction(nullptr,&size))&&size>=8&&size<=262144&&size%4==0){
            std::vector<std::uint32_t> code(size/4);
            if(SUCCEEDED(shader->GetFunction(code.data(),&size)))
                v.valid=NorthlightActorDeformation::compile(code.data(),code.size(),v.position,false,2)&&
                    NorthlightParticleLightProof::compileVertex(code.data(),code.size(),family->second,v.material);
        }
        auto result=vertices_.emplace(shader,std::move(v));return result.first->second.valid?&result.first->second:nullptr;
    }
public:
    bool known(IDirect3DVertexShader9* shader)const{return families_.count(shader)!=0;}
    void registerVertex(IDirect3DVertexShader9* shader,unsigned family){
        vertices_.erase(shader);
        if(family==1||family==2||family==3)families_[shader]=family;else families_.erase(shader);
    }
    void registerPixel(IDirect3DPixelShader9* shader,const DWORD* words,std::size_t count){
        NorthlightParticleLightProof::Pixel pixel;
        if(NorthlightParticleLightProof::compilePixel(reinterpret_cast<const std::uint32_t*>(words),count,pixel))pixels_[shader]=pixel;
        else pixels_.erase(shader);
    }
    Prepared prepare(IDirect3DDevice9* device,IDirect3DVertexShader9* vs,IDirect3DPixelShader9* ps,
                     const NorthlightSpellLayer::DrawArgs& args,const float* inverseView,const float* camera,
                     const float* projection,NorthlightParticleLights::Frame& frame){
        using namespace NorthlightSpellLayer;Prepared result;std::memcpy(result.camera,camera,12);
        if(!vs||!ps||!families_.count(vs)||!args.primCount||unsigned(args.prim)<4||unsigned(args.prim)>6)return result;
        DWORD blend=0,src=0,dst=0,op=0,alpha=0,mask=0,z=0;
        if(FAILED(device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))||!blend||
           FAILED(device->GetRenderState(D3DRS_SRCBLEND,&src))||(src!=D3DBLEND_ONE&&src!=D3DBLEND_SRCALPHA)||
           FAILED(device->GetRenderState(D3DRS_DESTBLEND,&dst))||dst!=D3DBLEND_ONE)return result;
        const bool particle=families_.at(vs)!=3;if(!frame.takeProbe())return result;
        using F=NorthlightParticleLights::Frame;
        if(FAILED(device->GetRenderState(D3DRS_BLENDOP,&op))||op!=D3DBLENDOP_ADD){frame.refuse(F::BlendOp);return result;}
        // The game keeps its alpha test on for additive particles (ALPHAREF 1,
        // GREATEREQUAL: only alpha 0 is dropped). A low reference is applied to
        // every sample below, the same test the draw itself makes.
        DWORD alphaFunc=D3DCMP_ALWAYS,alphaRef=0;
        if(FAILED(device->GetRenderState(D3DRS_ALPHATESTENABLE,&alpha))||(alpha&&(
           FAILED(device->GetRenderState(D3DRS_ALPHAFUNC,&alphaFunc))||FAILED(device->GetRenderState(D3DRS_ALPHAREF,&alphaRef))||
           (alphaFunc!=D3DCMP_GREATER&&alphaFunc!=D3DCMP_GREATEREQUAL&&alphaFunc!=D3DCMP_ALWAYS)||alphaRef>MaxAlphaRef))){frame.refuse(F::AlphaTest);return result;}
        if(!alpha)alphaFunc=D3DCMP_ALWAYS;
        if(FAILED(device->GetRenderState(D3DRS_COLORWRITEENABLE,&mask))||(mask&7)!=7){frame.refuse(F::ColorWrite);return result;}
        if(FAILED(device->GetRenderState(D3DRS_ZENABLE,&z))||!z){frame.refuse(F::Depth);return result;}
        auto pixel=pixels_.find(ps);Vertex* program=pixel!=pixels_.end()?vertex(vs):nullptr;
        if(pixel==pixels_.end()||!program){++frame.shaderMiss;return result;}
        const auto& material=pixel->second;
        NorthlightDrawSnapshot::Ref<IDirect3DBaseTexture9> texture;
        if(FAILED(device->GetTexture(material.sampler,texture.out()))||!texture.p){++frame.textureMiss;return result;}
        NorthlightParticleTextures::View image;
        if(!NorthlightParticleTextures::cache().lookup(texture.p,image)){++frame.textureMiss;if(!frame.wantsTextureMiss(texture.p))return result;
            NorthlightParticleLights::Frame::TextureMiss miss;miss.key=texture.p;miss.seen=NorthlightParticleTextures::cache().lastMiss();miss.type=texture.p->GetType();
            if(miss.type==D3DRTYPE_TEXTURE){D3DSURFACE_DESC d{};auto* t=static_cast<IDirect3DTexture9*>(texture.p);miss.levels=t->GetLevelCount();
                if(SUCCEEDED(t->GetLevelDesc(0,&d))){miss.width=d.Width;miss.height=d.Height;miss.format=unsigned(d.Format);miss.pool=unsigned(d.Pool);miss.usage=unsigned(d.Usage);}}
            frame.missTexture(miss);return result;}
        DWORD srgb=0,au=0,av=0,mag=0,min=0;
        if(FAILED(device->GetSamplerState(material.sampler,D3DSAMP_SRGBTEXTURE,&srgb))||
           FAILED(device->GetSamplerState(material.sampler,D3DSAMP_ADDRESSU,&au))||
           FAILED(device->GetSamplerState(material.sampler,D3DSAMP_ADDRESSV,&av))||
           FAILED(device->GetSamplerState(material.sampler,D3DSAMP_MAGFILTER,&mag))||
           FAILED(device->GetSamplerState(material.sampler,D3DSAMP_MINFILTER,&min))){frame.refuse(F::Sampler);return result;}
        float constants[1024];
        if(FAILED(device->GetVertexShaderConstantF(0,constants,256))){frame.refuse(F::Projection);return result;}
        const float* q=constants+8;
        for(unsigned i=0;i<16;++i)if(!std::isfinite(q[i])){frame.refuse(F::Projection);return result;}
        if(std::fabs(q[0]-projection[0])>.005f||std::fabs(q[5]-projection[1])>.005f||
           std::fabs(q[14]-projection[2])>.005f||std::fabs(q[15])>.001f){frame.refuse(F::Projection);return result;}
        NorthlightDrawSnapshot::Ref<IDirect3DVertexDeclaration9> declaration;
        D3DVERTEXELEMENT9 elements[65];UINT elementCount=65;
        if(FAILED(device->GetVertexDeclaration(declaration.out()))||!declaration.p||
           FAILED(declaration.p->GetDeclaration(elements,&elementCount))||elementCount>65){++frame.geometryMiss;return result;}
        struct Binding{unsigned reg;D3DVERTEXELEMENT9 element;};
        std::array<Binding,16> bindings{};unsigned bindingCount=0;bool bound[16]{};
        const auto bind=[&](const NorthlightActorDeformation::Program& p){
            for(auto input:p.inputs){
                if(input.reg>=16)return false;
                if(bound[input.reg]){
                    for(unsigned i=0;i<bindingCount;++i)if(bindings[i].reg==input.reg&&
                        (bindings[i].element.Usage!=input.usage||bindings[i].element.UsageIndex!=input.index))return false;
                    continue;
                }
                const D3DVERTEXELEMENT9* selected=nullptr;
                for(unsigned i=0;i<elementCount&&elements[i].Stream!=0xff;++i)if(elements[i].Usage==input.usage&&elements[i].UsageIndex==input.index){
                    if(selected)return false;selected=&elements[i];
                }
                if(!selected||selected->Stream>=4||selected->Method!=D3DDECLMETHOD_DEFAULT)return false;
                bindings[bindingCount++]={input.reg,*selected};bound[input.reg]=true;
            }return true;
        };
        if(!bind(program->position)||!bind(program->material.color)||!bind(program->material.uv)){++frame.geometryMiss;return result;}
        const bool up=args.type==DrawType::PrimitiveUP||args.type==DrawType::IndexedUP;
        const bool indexed=args.type==DrawType::Indexed||args.type==DrawType::IndexedUP;
        std::array<NorthlightDrawSnapshot::Ref<IDirect3DVertexBuffer9>,4> streams;
        unsigned offsets[4]{},strides[4]{};
        for(unsigned i=0;i<bindingCount;++i){
            const auto& e=bindings[i].element;const unsigned s=e.Stream;
            if(up){if(s||!args.vertexData||!args.stride||args.stride>256){++frame.geometryMiss;return result;}strides[0]=args.stride;}
            else if(!streams[s].p){
                UINT frequency=0;if(FAILED(device->GetStreamSource(s,streams[s].out(),offsets+s,strides+s))||!streams[s].p||
                    !strides[s]||strides[s]>256||FAILED(device->GetStreamSourceFreq(s,&frequency))||frequency!=1){++frame.geometryMiss;return result;}
            }
            const unsigned bytes=NorthlightDrawSnapshot::declarationBytes(e.Type);
            if(!bytes||unsigned(e.Offset)+bytes>strides[s]){++frame.geometryMiss;return result;}
        }
        NorthlightDrawSnapshot::Ref<IDirect3DIndexBuffer9> indices;
        D3DFORMAT indexFormat=args.indexFormat;
        if(indexed&&!up){D3DINDEXBUFFER_DESC description{};
            if(FAILED(device->GetIndices(indices.out()))||!indices.p||FAILED(indices.p->GetDesc(&description))){++frame.geometryMiss;return result;}
            indexFormat=description.Format;
        }
        const unsigned indexSize=indexFormat==D3DFMT_INDEX16?2:indexFormat==D3DFMT_INDEX32?4:0;
        if(indexed&&(!indexSize||(up&&!args.indexData))){++frame.geometryMiss;return result;}
        NorthlightParticleLights::Draw draw{indexed,up?0:args.baseVertex,args.minIndex,args.numVertices,up?0:args.first};
        struct Point {Four position,color;float uv[2];};
        const auto load=[&](std::uint64_t relative,Point& point){
            std::uint32_t index=0;
            if(indexed){
                const std::uint64_t at=(std::uint64_t(up?0:args.first)+relative)*indexSize;
                unsigned char bytes[4];
                if(at+indexSize>UINT32_MAX)return false;
                if(up)std::memcpy(bytes,static_cast<const unsigned char*>(args.indexData)+std::size_t(at),indexSize);
                else if(!readBuffer(indices.p,at,indexSize,bytes)){++frame.bufferMiss;return false;}
                if(indexSize==2){std::uint16_t small;std::memcpy(&small,bytes,2);index=small;}else std::memcpy(&index,bytes,4);
            }
            std::uint64_t vertexIndex=0;if(!draw.vertex(relative,index,vertexIndex))return false;
            Four inputs[16]{};std::array<std::array<unsigned char,256>,4> data;bool read[4]{};
            for(unsigned i=0;i<bindingCount;++i){const auto& b=bindings[i];const unsigned s=b.element.Stream;
                if(!read[s]){
                    const std::uint64_t at=std::uint64_t(offsets[s])+vertexIndex*strides[s];
                    if(at+strides[s]>UINT32_MAX)return false;
                    if(up)std::memcpy(data[s].data(),static_cast<const unsigned char*>(args.vertexData)+std::size_t(at),strides[s]);
                    else if(!readBuffer(streams[s].p,at,strides[s],data[s].data())){++frame.bufferMiss;return false;}
                    read[s]=true;
                }
                if(!NorthlightActorDeformation::decodeElement(data[s].data()+b.element.Offset,b.element.Type,inputs[b.reg]))return false;
            }
            return NorthlightActorDeformation::evaluateFast<true>(program->position,inputs,constants,point.position)&&
                NorthlightParticleLightProof::evaluateVertex(program->material,inputs,constants,point.color,point.uv);
        };
        const unsigned samples=std::min(args.primCount,NorthlightParticleLights::TrianglesPerDraw);
        const float bary[4][3]={{1.f/3,1.f/3,1.f/3},{.6f,.2f,.2f},{.2f,.6f,.2f},{.2f,.2f,.6f}};
        // Place the draw by its first loadable triangle before it takes a sampling slot:
        // beyond the lights' reach, or with no loadable bytes, it can add no light.
        bool placed=false;
        for(unsigned sample=0;sample<samples&&!placed;++sample){
            std::uint64_t references[3];if(!NorthlightParticleLights::triangleIndices(unsigned(args.prim),std::uint64_t(sample)*args.primCount/samples,references))continue;
            Point points[3];if(!load(references[0],points[0])||!load(references[1],points[1])||!load(references[2],points[2]))continue;
            float world[3];for(unsigned j=0;j<3;++j){world[j]=inverseView[12+j];for(unsigned k=0;k<3;++k)world[j]+=(points[0].position[k]+points[1].position[k]+points[2].position[k])/3*inverseView[k*4+j];}
            if(!(NorthlightEquipmentLights::distance2(world,camera)<=NorthlightParticleLights::MaxDistance*NorthlightParticleLights::MaxDistance)){++frame.distant;return result;}
            placed=true;
        }
        if(!placed||!frame.takeDraw(particle))return result;
        float lo[3]={INFINITY,INFINITY,INFINITY},hi[3]={-INFINITY,-INFINITY,-INFINITY};unsigned loaded=0;
        for(unsigned sample=0;sample<samples;++sample){
            std::uint64_t references[3];if(!NorthlightParticleLights::triangleIndices(unsigned(args.prim),std::uint64_t(sample)*args.primCount/samples,references))continue;
            Point points[3];if(!load(references[0],points[0])||!load(references[1],points[1])||!load(references[2],points[2]))continue;
            if(!particle){++loaded;for(const auto& point:points)for(unsigned k=0;k<3;++k){
                if(!std::isfinite(point.position[k]))return Prepared{};
                lo[k]=std::min(lo[k],point.position[k]);hi[k]=std::max(hi[k],point.position[k]);}}
            ++frame.triangles;Four center{};for(unsigned j=0;j<4;++j)center[j]=(points[0].position[j]+points[1].position[j]+points[2].position[j])/3;
            float clip[4]{};for(unsigned j=0;j<4;++j)for(unsigned k=0;k<4;++k)clip[j]+=q[j*4+k]*center[k];
            if(!std::isfinite(clip[3])||clip[3]<=0||std::fabs(clip[0])>clip[3]*1.2f||std::fabs(clip[1])>clip[3]*1.2f)continue;
            NorthlightParticleLights::Sample value;
            for(unsigned j=0;j<3;++j){value.position[j]=inverseView[12+j];for(unsigned k=0;k<3;++k)value.position[j]+=center[k]*inverseView[k*4+j];}
            bool known=true;
            for(const auto& weights:bary){
                Four colour{},texel{},emitted{};float uv[2]{};
                for(unsigned i=0;i<3;++i){for(unsigned j=0;j<4;++j)colour[j]+=weights[i]*points[i].color[j];
                    for(unsigned j=0;j<2;++j)uv[j]+=weights[i]*points[i].uv[j];}
                if(!image.sample(uv[0],uv[1],srgb!=0,au,av,texel.data(),mag!=D3DTEXF_POINT||min!=D3DTEXF_POINT)||
                   !NorthlightParticleLightProof::shade(material,colour,texel,nullptr,emitted)){known=false;break;}
                const float alphaOut=std::clamp(emitted[3],0.f,1.f)*255;
                if(alphaFunc==D3DCMP_GREATER?!(alphaOut>float(alphaRef)):alphaFunc==D3DCMP_GREATEREQUAL&&!(alphaOut>=float(alphaRef)))continue; // the draw's own alpha test drops it
                const float alphaGain=src==D3DBLEND_SRCALPHA?std::clamp(emitted[3],0.f,1.f):1.f;
                for(unsigned j=0;j<3;++j)value.rgb[j]+=std::max(0.f,emitted[j])*alphaGain*.25f;
            }
            if(known&&NorthlightParticleLights::peak(value.rgb)>=.003f)result.add(value);
        }
        if(!particle&&NorthlightParticleLights::compactMesh(lo,hi,loaded,args.primCount))
            for(unsigned i=0;i<result.count;++i)result.clusters[i].sample.castsShadows=true;
        return result;
    }
    static void submit(const Prepared& prepared,bool accepted,bool deferred,NorthlightParticleLights::Frame& frame){
        if(!accepted||!prepared.count)return;
        ++frame.accepted;if(deferred)++frame.deferred;
        for(unsigned i=0;i<prepared.count;++i)frame.observe(prepared.clusters[i].sample,prepared.camera);
    }
};
}
