#include <windows.h>
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include "draw_snapshot.h"
#include "actor_deformation.h"
#include "displacement_geometry.h"
int main(){
    using namespace NorthlightDisplacementGeometry;
    // Packing preserves index values/order at the 16-bit boundary; larger meshes retain 32-bit indices.
    {Mesh indices;indices.vertexCount=65536;indices.indices={0,65535,12345};
     std::array<uint16_t,4> narrow={0,0,0,0xCAFE};assert(indexElementBytes(indices)==2);copyIndices(indices,narrow.data());
     assert(narrow[0]==0&&narrow[1]==65535&&narrow[2]==12345&&narrow[3]==0xCAFE);
     indices.vertexCount=65537;indices.indices={65536,65535,0};
     std::array<uint32_t,4> wide={0,0,0,0xCAFE};assert(indexElementBytes(indices)==4);copyIndices(indices,wide.data());
     assert(wide[0]==65536&&wide[1]==65535&&wide[2]==0&&wide[3]==0xCAFE);}
    struct V {float x,y,z,nx,ny,nz,u,v;};
    const V data[]={{0,0,0,0,0,1,0,0},{1,0,0,0,0,1,1,0},{0,1,0,0,0,1,0,1},{1,1,0,0,0,1,1,1}};
    const D3DVERTEXELEMENT9 decl[]={{0,0,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_POSITION,0},{0,12,D3DDECLTYPE_FLOAT3,0,D3DDECLUSAGE_NORMAL,0},{0,24,D3DDECLTYPE_FLOAT2,0,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    Mesh source;source.vertexCount=4;source.primitiveCount=2;source.indexed=true;source.indices={0,1,2,2,1,3};source.streams[0].stride=sizeof(V);source.streams[0].bytes.resize(sizeof(data));std::memcpy(source.streams[0].bytes.data(),data,sizeof(data));
    const std::vector<std::array<float,2>> uv={{{0,0}},{{1,0}},{{0,1}},{{1,1}}};Height h;h.width=h.height=8;h.values.resize(64,.8f);Mesh out;
    assert(build(source,decl,4,uv,h,8,false,false,false,out));assert(out.vertexCount==81&&out.primitiveCount==128);
    unsigned moved=0,boundary=0;std::vector<unsigned> use(out.vertexCount);
    for(auto i:out.indices){assert(i<out.vertexCount);++use[i];}
    for(unsigned i=0;i<out.vertexCount;++i){auto* bytes=out.streams[0].bytes.data()+i*out.streams[0].stride;V v;std::memcpy(&v,bytes,sizeof v);float d[3];std::memcpy(d,bytes+sizeof(V),12);
        assert(v.z==0&&d[0]==0&&d[1]==0);assert(use[i]);if(v.x==0||v.y==0||v.x==1||v.y==1){assert(d[2]==0);++boundary;}else{assert(std::fabs(d[2]-.3f)<1e-5f);++moved;}}
    assert(boundary==32&&moved==49); // shared diagonal welded, no internal cracks
    Mesh strip=source;strip.indexed=false;strip.topology=D3DPT_TRIANGLESTRIP;strip.indices.clear();assert(build(strip,decl,4,uv,h,8,true,false,false,out));assert(out.vertexCount==81&&out.primitiveCount==128);
    Height black=h;std::fill(black.values.begin(),black.values.end(),.2f);std::vector<float> blend(4,.5f);
    assert(build(source,decl,4,uv,h,4,false,false,false,out,&black,&uv,&blend));for(unsigned i=0;i<out.vertexCount;++i){float d[3];std::memcpy(d,out.streams[0].bytes.data()+i*out.streams[0].stride+sizeof(V),12);assert(std::fabs(d[2])<1e-6f);}
    auto bad=source;bad.indices[0]=100;assert(!build(bad,decl,4,uv,h,8,false,false,false,out));
    auto badUV=uv;badUV[0][0]=std::numeric_limits<float>::quiet_NaN();assert(!build(source,decl,4,badUV,h,8,false,false,false,out));
    assert(build(source,decl,4,uv,h,64,false,false,false,out));assert(out.primitiveCount==8192);
    assert(build(source,decl,4,uv,h,128,false,false,false,out));assert(out.primitiveCount==32768&&out.vertexCount==16641);
    assert(!build(source,decl,4,uv,h,256,false,false,false,out));assert(!build(source,decl,4,uv,h,8,false,false,false,out,&black));
    Height dense=h;dense.width=dense.height=128;dense.values.assign(128*128,.8f);
    assert(textureFactor(uv,dense,1)==2&&textureFactor(uv,dense,4)==32&&textureFactor(uv,dense,5)==64);
    auto tiled=uv;for(auto& p:tiled)for(auto& c:p)c*=4;assert(textureFactor(tiled,dense,4)==128);
    h.values={0,1,1,0};h.width=h.height=2;assert(std::fabs(h.sample(.5f,.5f)-.5f)<1e-6f);assert(h.sample(0,0,true,true)==0);assert(std::fabs(h.sample(-.25f,.25f)-1)<1e-6f);
    // Varying skin transforms keep the original triangle: interpolating both
    // weights and positions changes its animated shape even at zero height.
    struct Skin {V v;unsigned char weights[4],bones[4];};Skin skin[4];
    for(unsigned i=0;i<4;++i){skin[i]={data[i],{255,0,0,0},{static_cast<unsigned char>(i&1?74:0),0,0,0}};}
    auto skinned=source;skinned.streams[0].stride=sizeof(Skin);skinned.streams[0].bytes.resize(sizeof skin);std::memcpy(skinned.streams[0].bytes.data(),skin,sizeof skin);
    D3DVERTEXELEMENT9 skinDecl[6];std::memcpy(skinDecl,decl,3*sizeof(*decl));skinDecl[3]={0,32,D3DDECLTYPE_UBYTE4N,0,D3DDECLUSAGE_BLENDWEIGHT,0};skinDecl[4]={0,36,D3DDECLTYPE_UBYTE4,0,D3DDECLUSAGE_BLENDINDICES,0};skinDecl[5]=D3DDECL_END();
    assert(build(skinned,skinDecl,6,uv,h,8,false,false,false,out));assert(out.primitiveCount==2&&out.vertexCount==4);
    for(unsigned i=0;i<out.vertexCount;++i){Skin s;std::memcpy(&s,out.streams[0].bytes.data()+i*out.streams[0].stride,sizeof s);unsigned sum=0;for(unsigned k=0;k<4;++k){sum+=s.weights[k];if(s.weights[k]){assert(s.bones[k]==0||s.bones[k]==74);}}assert(sum==255);}
    // Even an isolated UV-seam triangle gets an interior relief sample at the
    // lowest geometry budget; splitting boundary edges alone was all flat.
    auto isolated=source;isolated.vertexCount=3;isolated.primitiveCount=1;isolated.indices={0,1,2};auto triUV=uv;triUV.resize(3);h.width=h.height=8;h.values.assign(64,.8f);
    assert(build(isolated,decl,4,triUV,h,2,false,false,false,out));assert(out.primitiveCount==3);unsigned raised=0;
    for(unsigned i=0;i<out.vertexCount;++i){float delta[3];std::memcpy(delta,out.streams[0].bytes.data()+size_t(i)*out.streams[0].stride+sizeof(V),12);raised+=delta[2]>.2f;}assert(raised==1);
    // A rigid shader may share a declaration with unused, all-zero skin weights.
    // Those fields must not reject an otherwise valid rigid surface.
    for(auto& v:skin)for(auto& w:v.weights)w=0;std::memcpy(skinned.streams[0].bytes.data(),skin,sizeof skin);
    assert(build(skinned,skinDecl,6,uv,h,8,false,false,false,out,nullptr,nullptr,nullptr,0));
    // Unlit rigid props have no NORMAL attribute: use welded geometric normals.
    D3DVERTEXELEMENT9 noNormal[]={decl[0],decl[2],D3DDECL_END()};assert(build(source,noNormal,3,uv,h,8,false,false,false,out));
    std::puts("DISPLACEMENT_GEOMETRY_PASS welded_edges boundary_pins list_strip two_layers finite_validation budgets bilinear_wrap_clamp");
}
