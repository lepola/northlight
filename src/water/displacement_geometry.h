#pragma once
#include "actor_deformation.h"
#include <unordered_map>

namespace NorthlightDisplacementGeometry {
using Mesh=NorthlightDrawSnapshot::Mesh;
using Four=NorthlightActorDeformation::Four;
inline unsigned indexElementBytes(const Mesh& mesh){return mesh.vertexCount<=65536?sizeof(uint16_t):sizeof(uint32_t);}
inline void copyIndices(const Mesh& mesh,void* destination){
    if(indexElementBytes(mesh)==sizeof(uint16_t))std::copy(mesh.indices.begin(),mesh.indices.end(),static_cast<uint16_t*>(destination));
    else std::memcpy(destination,mesh.indices.data(),mesh.indices.size()*sizeof(uint32_t));
}
struct Height {
    unsigned width=0,height=0;std::vector<float> values;
    float sample(float u,float v,bool clampU=false,bool clampV=false)const{
        if(!width||!height||values.size()!=size_t(width)*height||!std::isfinite(u)||!std::isfinite(v))return .5f;
        const float x=(clampU?std::clamp(u,0.f,1.f):u-std::floor(u))*width-.5f;
        const float y=(clampV?std::clamp(v,0.f,1.f):v-std::floor(v))*height-.5f;
        const int ix=int(std::floor(x)),iy=int(std::floor(y));
        auto at=[&](int a,int b){a=clampU?std::clamp(a,0,int(width)-1):(a%int(width)+int(width))%int(width);b=clampV?std::clamp(b,0,int(height)-1):(b%int(height)+int(height))%int(height);return values[size_t(b)*width+a];};
        return (at(ix,iy)*(1-(x-ix))+at(ix+1,iy)*(x-ix))*(1-(y-iy))+(at(ix,iy+1)*(1-(x-ix))+at(ix+1,iy+1)*(x-ix))*(y-iy);
    }
};
inline bool encode(unsigned char* p,unsigned type,const Four& v){
    if(type<=3){std::memcpy(p,v.data(),(type+1)*4);return true;}
    auto rounded=[](float x,float low,float high){return std::lround(std::clamp(x,low,high));};
    if(type==4){for(unsigned i=0;i<4;++i)p[i]=uint8_t(rounded(v[i==0?2:i==2?0:i]*255,0,255));return true;}
    if(type==5||type==8){for(unsigned i=0;i<4;++i)p[i]=uint8_t(rounded(v[i]*(type==8?255:1),0,255));return true;}
    if(type==6||type==7||type==9||type==10){for(unsigned i=0;i<((type==6||type==9)?2u:4u);++i){int16_t x=int16_t(rounded(v[i]*(type>=9?32767:1),-32767,32767));std::memcpy(p+i*2,&x,2);}return true;}
    if(type==11||type==12){for(unsigned i=0;i<(type==11?2u:4u);++i){uint16_t x=uint16_t(rounded(v[i]*65535,0,65535));std::memcpy(p+i*2,&x,2);}return true;}
    return false;
}
struct Key {std::array<uint32_t,6> v;bool operator==(const Key& b)const{return v==b.v;}};
struct Hash {size_t operator()(const Key& k)const{size_t h=2166136261u;for(auto x:k.v)h=(h^x)*16777619u;return h;}};
inline uint64_t edge(unsigned a,unsigned b){return (uint64_t(std::min(a,b))<<32)|std::max(a,b);}
// Resolve the height texture rather than stretching one fixed grid across any
// UV scale. The existing triangle/vertex budgets still bound the final mesh.
inline unsigned textureFactor(const std::vector<std::array<float,2>>& uv,const Height& height,unsigned detail){
    if(detail==1)return 2;
    if(uv.empty()||!height.width||!height.height)return 1u<<detail;
    float low[2]={uv[0][0],uv[0][1]},high[2]={low[0],low[1]};
    for(auto p:uv)for(unsigned c=0;c<2;++c){if(!std::isfinite(p[c]))return 1u<<detail;low[c]=std::min(low[c],p[c]);high[c]=std::max(high[c],p[c]);}
    const float pixels=std::max((high[0]-low[0])*height.width,(high[1]-low[1])*height.height);
    const unsigned texels=1u<<(6-std::clamp(detail,1u,5u));unsigned factor=1u<<detail;
    while(factor<128&&factor*texels<pixels)factor*=2;
    return factor;
}
// One subdivision factor per draw makes shared edges conform. Boundary vertices
// stay on the original surface, including authored UV/material/hard-normal seams.
inline bool build(const Mesh& in,const D3DVERTEXELEMENT9* elements,UINT elementCount,
    const std::vector<std::array<float,2>>& uv,const Height& first,unsigned factor,bool terrain,
    bool clampU,bool clampV,Mesh& out,const Height* second=nullptr,
    const std::vector<std::array<float,2>>* uv1=nullptr,const std::vector<float>* blend=nullptr,unsigned influences=4,bool gpuHeight=false){
    out={};if(influences>4)return false;const auto stride=in.streams[0].stride;
    if(gpuHeight&&(second||stride>232||elementCount+2>MAXD3DDECLLENGTH))return false;
    if(!in.vertexCount||in.vertexCount>32768||stride>240||uv.size()!=in.vertexCount||factor<2||factor>128||!elements||elementCount>MAXD3DDECLLENGTH)return false;
    if(in.streams[0].bytes.size()<size_t(in.vertexCount)*stride)return false;
    if((second&&(!uv1||!blend||uv1->size()!=in.vertexCount||blend->size()!=in.vertexCount)))return false;
    for(auto value:uv)if(!std::isfinite(value[0])||!std::isfinite(value[1]))return false;
    const D3DVERTEXELEMENT9 *pos=nullptr,*normal=nullptr,*weights=nullptr,*bones=nullptr;
    for(UINT i=0;i<elementCount;++i){const auto& e=elements[i];if(e.Stream==0xff)break;
        if(e.Stream||e.Method!=D3DDECLMETHOD_DEFAULT||e.Type>12||e.Offset+NorthlightDrawSnapshot::declarationBytes(e.Type)>stride)return false;
        if(e.Usage==D3DDECLUSAGE_POSITION&&e.UsageIndex==0)pos=&e;
        if(e.Usage==D3DDECLUSAGE_NORMAL&&e.UsageIndex==0)normal=&e;
        if(e.Usage==D3DDECLUSAGE_BLENDWEIGHT)weights=&e;
        if(e.Usage==D3DDECLUSAGE_BLENDINDICES)bones=&e;
        if(e.Usage==D3DDECLUSAGE_TEXCOORD&&(e.UsageIndex==7||(gpuHeight&&e.UsageIndex==6)))return false;
    }
    if(!pos||(pos->Type!=D3DDECLTYPE_FLOAT3&&pos->Type!=D3DDECLTYPE_FLOAT4))return false;
    if(weights&&!bones)return false;
    std::vector<std::array<unsigned,3>> triangles;triangles.reserve(std::min(in.primitiveCount,8192u));
    auto index=[&](unsigned i){return in.indexed?(i<in.indices.size()?in.indices[i]:UINT_MAX):i;};
    if(in.topology!=D3DPT_TRIANGLELIST&&in.topology!=D3DPT_TRIANGLESTRIP)return false;
    for(unsigned i=0;i<in.primitiveCount;++i){unsigned at=in.topology==D3DPT_TRIANGLELIST?i*3:i;std::array<unsigned,3> t={index(at),index(at+1),index(at+2)};if(in.topology==D3DPT_TRIANGLESTRIP&&(i&1))std::swap(t[0],t[1]);for(auto a:t)if(a>=in.vertexCount)return false;if(t[0]!=t[1]&&t[1]!=t[2]&&t[2]!=t[0])triangles.push_back(t);}
    if(triangles.empty())return false;
    std::vector<Four> faceNormals;if(!normal&&!terrain){faceNormals.resize(in.vertexCount);
        for(auto t:triangles){Four p[3];for(unsigned k=0;k<3;++k)if(!NorthlightActorDeformation::decodeElement(in.streams[0].bytes.data()+size_t(t[k])*stride+pos->Offset,pos->Type,p[k]))return false;
            float a[3],b[3];for(unsigned k=0;k<3;++k){a[k]=p[1][k]-p[0][k];b[k]=p[2][k]-p[0][k];}Four n={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0],0};for(auto i:t)for(unsigned k=0;k<3;++k)faceNormals[i][k]+=n[k];}}

    while(factor>2&&triangles.size()*factor*factor>32768)factor/=2;
    if(triangles.size()*factor*factor>32768)return false;
    // At the minimum budget, edge-only subdivision leaves isolated UV-seam
    // triangles completely flat. A centroid fan has a real interior sample.
    const bool centroid=factor==2;if(centroid)factor=3;
    std::unordered_map<uint64_t,unsigned> edges;edges.reserve(triangles.size()*3);for(auto t:triangles)for(unsigned i=0;i<3;++i)++edges[edge(t[i],t[(i+1)%3])];
    std::vector<bool> rigid(triangles.size(),true);
    if(influences&&bones)for(size_t i=0;i<triangles.size();++i){const auto& t=triangles[i];
        for(auto* e:{weights,bones})if(e)for(unsigned k=1;k<3;++k)if(std::memcmp(in.streams[0].bytes.data()+size_t(t[0])*stride+e->Offset,in.streams[0].bytes.data()+size_t(t[k])*stride+e->Offset,NorthlightDrawSnapshot::declarationBytes(e->Type)))rigid[i]=false;
        // Interpolating both position and varying skin weights is quadratic;
        // it changes the native animated triangle even with a zero height map.
        if(!rigid[i])for(unsigned k=0;k<3;++k)edges[edge(t[k],t[(k+1)%3])]=1;
    }
    std::vector<bool> boundary(in.vertexCount,false);for(auto e:edges)if(e.second!=2){boundary[unsigned(e.first>>32)]=true;boundary[unsigned(e.first)]=true;}
    const unsigned outputStride=stride+(gpuHeight?20:12);
    out.streams[0].stride=outputStride;out.topology=D3DPT_TRIANGLELIST;out.indexed=true;
    const auto capacity=std::min(size_t(32768),triangles.size()*(factor+1)*(factor+2)/2);
    out.streams[0].bytes.reserve(capacity*outputStride);out.indices.reserve(triangles.size()*factor*factor*3);
    std::vector<Four> decoded(size_t(elementCount)*in.vertexCount);
    for(UINT e=0;e<elementCount&&elements[e].Stream!=0xff;++e){if(&elements[e]==weights||&elements[e]==bones)continue;
        for(unsigned i=0;i<in.vertexCount;++i)if(!NorthlightActorDeformation::decodeElement(in.streams[0].bytes.data()+size_t(i)*stride+elements[e].Offset,elements[e].Type,decoded[size_t(e)*in.vertexCount+i]))return false;}
    std::unordered_map<Key,unsigned,Hash> vertices;vertices.reserve(capacity);
    auto vertex=[&](const std::array<unsigned,3>& t,unsigned i,unsigned j)->unsigned{
        const unsigned w[3]={factor-i-j,i,j};std::array<std::pair<unsigned,unsigned>,3> sorted;
        for(unsigned k=0;k<3;++k)sorted[k]={w[k]?t[k]:UINT_MAX,w[k]};std::sort(sorted.begin(),sorted.end());
        Key key;for(unsigned k=0;k<3;++k){key.v[k*2]=sorted[k].first;key.v[k*2+1]=sorted[k].second;}
        auto found=vertices.find(key);if(found!=vertices.end())return found->second;
        if(out.vertexCount>=32768)return UINT_MAX;
        const unsigned n=out.vertexCount++;vertices.emplace(key,n);auto& bytes=out.streams[0].bytes;bytes.resize(size_t(out.vertexCount)*outputStride);auto* dst=bytes.data()+size_t(n)*outputStride;
        Four direction={0,0,1,0};
        for(UINT e=0;e<elementCount&&elements[e].Stream!=0xff;++e){const auto& element=elements[e];if(&element==weights||&element==bones)continue;Four value={};
            for(unsigned k=0;k<3;++k){const auto& a=decoded[size_t(e)*in.vertexCount+t[k]];for(unsigned c=0;c<4;++c)value[c]+=a[c]*(float(w[k])/factor);}
            for(float v:value)if(!std::isfinite(v))return UINT_MAX;
            if(!encode(dst+element.Offset,element.Type,value))return UINT_MAX;
            if(&element==normal&&!terrain)direction=value;
        }
        if(!normal&&!terrain){direction={};for(unsigned k=0;k<3;++k)for(unsigned c=0;c<3;++c)direction[c]+=faceNormals[t[k]][c]*(float(w[k])/factor);}
        if(bones&&!weights){unsigned k=w[1]>w[0]?1:0;if(w[2]>w[k])k=2;Four ids;if(!NorthlightActorDeformation::decodeElement(in.streams[0].bytes.data()+size_t(t[k])*stride+bones->Offset,bones->Type,ids)||!encode(dst+bones->Offset,bones->Type,ids))return UINT_MAX;}
        if(!influences){unsigned k=w[1]>w[0]?1:0;if(w[2]>w[k])k=2;for(auto* e:{weights,bones})if(e)std::memcpy(dst+e->Offset,in.streams[0].bytes.data()+size_t(t[k])*stride+e->Offset,NorthlightDrawSnapshot::declarationBytes(e->Type));}
        if(influences&&weights&&bones){unsigned ids[12]={},used=0;float merged[12]={};
            for(unsigned k=0;k<3;++k){Four ws,bs;if(!NorthlightActorDeformation::decodeElement(in.streams[0].bytes.data()+size_t(t[k])*stride+weights->Offset,weights->Type,ws)||!NorthlightActorDeformation::decodeElement(in.streams[0].bytes.data()+size_t(t[k])*stride+bones->Offset,bones->Type,bs))return UINT_MAX;
                for(unsigned c=0;c<influences;++c){if(!std::isfinite(ws[c])||!std::isfinite(bs[c])||bs[c]<0||bs[c]>255||std::fabs(bs[c]-std::round(bs[c]))>.001f||ws[c]<0)return UINT_MAX;const unsigned id=unsigned(std::lround(bs[c]));unsigned j=0;while(j<used&&ids[j]!=id)++j;if(j==used)ids[used++]=id;merged[j]+=ws[c]*(float(w[k])/factor);}}
            for(unsigned a=0;a<std::min(used,influences);++a)for(unsigned b=a+1;b<used;++b)if(merged[b]>merged[a]){std::swap(merged[a],merged[b]);std::swap(ids[a],ids[b]);}
            Four ws={},bs={};float total=0;for(unsigned a=0;a<std::min(used,influences);++a)total+=merged[a];if(total<=1e-8f)return UINT_MAX;
            for(unsigned a=0;a<std::min(used,influences);++a){ws[a]=merged[a]/total;bs[a]=float(ids[a]);}if(!encode(dst+weights->Offset,weights->Type,ws)||!encode(dst+bones->Offset,bones->Type,bs))return UINT_MAX;
            if(weights->Type==D3DDECLTYPE_UBYTE4N){unsigned sum=0;float fraction[4];for(unsigned a=0;a<4;++a){unsigned n=unsigned(std::floor(ws[a]*255));dst[weights->Offset+a]=uint8_t(n);sum+=n;fraction[a]=ws[a]*255-n;}
                while(sum<255){unsigned a=unsigned(std::max_element(fraction,fraction+4)-fraction);++dst[weights->Offset+a];fraction[a]=-1;++sum;}}
        }
        bool pinned=false;for(unsigned k=0;k<3;++k){if(w[k]==factor&&(boundary[t[k]]||influences))pinned=true;if(!w[k]&&edges[edge(t[(k+1)%3],t[(k+2)%3])]!=2)pinned=true;
            if(w[k]==factor)std::memcpy(dst,in.streams[0].bytes.data()+size_t(t[k])*stride,stride);}
        float u=0,v=0,u1=0,v1=0,b=0;for(unsigned k=0;k<3;++k){float weight=float(w[k])/factor;u+=uv[t[k]][0]*weight;v+=uv[t[k]][1]*weight;if(uv1){u1+=(*uv1)[t[k]][0]*weight;v1+=(*uv1)[t[k]][1]*weight;}if(blend)b+=(*blend)[t[k]]*weight;}
        float h=.5f;if(!gpuHeight&&!pinned){h=first.sample(u,v,clampU,clampV);if(second&&uv1&&blend)h=second->sample(u1,v1,clampU,clampV)*(1-std::clamp(b,0.f,1.f))+h*std::clamp(b,0.f,1.f);}
        const float length=std::sqrt(direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2]);if(!std::isfinite(length)||length<1e-5f)return UINT_MAX;
        // Skinned eye shells and facial UV seams must keep their native shape.
        // Taper through the first triangle ring; an all-boundary triangle has
        // no safe interior to inflate. Normal-map detail remains independent.
        float relief=1;if(influences&&bones){relief=0;for(unsigned k=0;k<3;++k)if(!boundary[t[k]])relief+=float(w[k])/factor;}
        float delta[3];for(unsigned k=0;k<3;++k)delta[k]=pinned?0:relief*(gpuHeight?1.f:h-.5f)*direction[k]/length;std::memcpy(dst+stride,delta,12);
        if(gpuHeight){const float coordinates[]={u,v};std::memcpy(dst+stride+12,coordinates,8);}return n;
    };
    std::array<std::array<unsigned,129>,129> rows;
    for(size_t ti=0;ti<triangles.size();++ti){auto t=triangles[ti];
        const bool seamTriangle=influences&&bones&&boundary[t[0]]&&boundary[t[1]]&&boundary[t[2]];
        if(centroid||!rigid[ti]||seamTriangle){unsigned a=vertex(t,0,0),b=vertex(t,factor,0),c=vertex(t,0,factor);if(a==UINT_MAX||b==UINT_MAX||c==UINT_MAX){out={};return false;}
            if(!rigid[ti]||seamTriangle)out.indices.insert(out.indices.end(),{a,b,c});
            else{unsigned center=vertex(t,1,1);if(center==UINT_MAX){out={};return false;}out.indices.insert(out.indices.end(),{a,b,center,b,c,center,c,a,center});}continue;}
        for(unsigned i=0;i<=factor;++i)for(unsigned j=0;j<=factor-i;++j){unsigned v=vertex(t,i,j);if(v==UINT_MAX){out={};return false;}rows[i][j]=v;}
        for(unsigned i=0;i<factor;++i)for(unsigned j=0;j<factor-i;++j){out.indices.insert(out.indices.end(),{rows[i][j],rows[i+1][j],rows[i][j+1]});if(j+1<factor-i)out.indices.insert(out.indices.end(),{rows[i+1][j],rows[i+1][j+1],rows[i][j+1]});}}
    out.primitiveCount=unsigned(out.indices.size()/3);return true;
}
} // namespace NorthlightDisplacementGeometry
