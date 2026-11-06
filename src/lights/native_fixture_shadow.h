#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

// Catalog ownership is already exact. A replay must additionally prove both
// its original local triangles and their rigid transform to this light.
namespace NorthlightNativeFixtureShadow {
using Point=std::array<double,3>;
inline Point sub(Point a,Point b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
inline double dot(Point a,Point b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
inline Point scale(Point p,double s){return {p[0]*s,p[1]*s,p[2]*s};}
inline Point cross(Point a,Point b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
inline bool finite(Point p){return std::isfinite(p[0])&&std::isfinite(p[1])&&std::isfinite(p[2]);}
inline bool coincides(Point a,Point b,double tolerance){return finite(a)&&finite(b)&&std::fabs(a[0]-b[0])<=tolerance&&std::fabs(a[1]-b[1])<=tolerance&&std::fabs(a[2]-b[2])<=tolerance;}
template<class P> Point point(const P& p){return {p.x,p.y,p.z};}
struct Transform {
    Point local{},world{},a{},b{},c{},x{},y{},z{};double factor=0;
    Point apply(Point p)const{const auto d=sub(p,local);Point out=world;for(unsigned k=0;k<3;++k)out[k]+=factor*(dot(d,a)*x[k]+dot(d,b)*y[k]+dot(d,c)*z[k]);return out;}
    template<class Triangle> bool fit(const Triangle& raw,const Triangle& placed){
        local=point(raw[0]);world=point(placed[0]);auto u=sub(point(raw[1]),local),v=sub(point(raw[2]),local);
        auto r=sub(point(placed[1]),world),s=sub(point(placed[2]),world);
        const double length=std::sqrt(dot(u,u)),other=std::sqrt(dot(r,r));
        if(!finite(local)||!finite(world)||length<1e-5||other<1e-6)return false;
        factor=other/length;if(!std::isfinite(factor)||factor<.0001||factor>10000)return false;
        a=scale(u,1/length);x=scale(r,1/other);v=sub(v,scale(a,dot(v,a)));s=sub(s,scale(x,dot(s,x)));
        const double height=std::sqrt(dot(v,v)),placedHeight=std::sqrt(dot(s,s));if(height<1e-5||placedHeight<1e-6)return false;
        b=scale(v,1/height);y=scale(s,1/placedHeight);c=cross(a,b);z=cross(x,y);
        for(unsigned i=0;i<3;++i)if(!coincides(apply(point(raw[i])),point(placed[i]),.0025))return false;
        return true;
    }
};
struct Prepared {
    std::vector<bool> excluded;std::vector<std::size_t> indices;Point anchor{};std::size_t best=0;
    bool valid()const{return indices.size()>=4&&best<excluded.size();}
};
inline constexpr std::size_t CompactBytes=1024*1024;
// Cold copy only: every owned corner and its original shader stream inputs are
// retained. The original replay and its surviving GPU runs remain unchanged.
template<class Mesh,class Triangles>
bool compact(const Mesh& input,bool strip,unsigned streams,Prepared& prepared,Triangles& local,Mesh& output){
    if(!prepared.valid()||!streams||(streams&~15u)||!input.vertexCount||input.vertexCount>12288||local.size()!=input.primitiveCount||local.size()!=prepared.excluded.size()||local.size()>4096||!prepared.excluded[prepared.best])return false;
    const auto required=strip?local.size()+2:local.size()*3;
    if(input.indexed?input.indices.size()<required:input.vertexCount<required)return false;
    if(input.indexed)for(std::size_t i=0;i<required;++i)if(input.indices[i]>=input.vertexCount)return false;
    std::size_t selected=0;for(std::size_t i=0;i<local.size();++i)if(prepared.excluded[i]){if(selected>=prepared.indices.size()||prepared.indices[selected++]!=i)return false;}
    if(selected!=prepared.indices.size())return false;
    Mesh mesh;Triangles triangles;Prepared reduced;reduced.anchor=prepared.anchor;reduced.excluded.assign(selected,true);reduced.indices.reserve(selected);triangles.reserve(selected);mesh.indices.reserve(selected*3);
    std::vector<std::uint32_t> remap(input.vertexCount,~0u),vertices;vertices.reserve(std::min<std::size_t>(input.vertexCount,selected*3));
    for(std::size_t i=0;i<selected;++i){const auto triangle=prepared.indices[i];triangles.push_back(local[triangle]);reduced.indices.push_back(i);if(triangle==prepared.best)reduced.best=i;
        for(unsigned corner=0;corner<3;++corner){const auto at=strip?triangle+corner:triangle*3+corner;const auto vertex=input.indexed?input.indices[at]:std::uint32_t(at);
            if(remap[vertex]==~0u){remap[vertex]=std::uint32_t(vertices.size());vertices.push_back(vertex);}mesh.indices.push_back(remap[vertex]);}}
    std::size_t bytes=0;
    for(unsigned i=0;i<4;++i)if(streams&(1u<<i)){const auto& source=input.streams[i];const std::uint64_t size=std::uint64_t(vertices.size())*source.stride;
        if(!source.stride||std::uint64_t(input.vertexCount)*source.stride>source.bytes.size()||size>CompactBytes-bytes)return false;bytes+=std::size_t(size);}
    for(unsigned i=0;i<4;++i)if(streams&(1u<<i)){const auto& source=input.streams[i];auto& target=mesh.streams[i];target.stride=source.stride;target.bytes.resize(vertices.size()*source.stride);
        for(std::size_t j=0;j<vertices.size();++j)std::memcpy(target.bytes.data()+j*source.stride,source.bytes.data()+std::size_t(vertices[j])*source.stride,source.stride);}
    mesh.vertexCount=unsigned(vertices.size());mesh.primitiveCount=unsigned(selected);mesh.indexed=true;
    output=std::move(mesh);local=std::move(triangles);prepared=std::move(reduced);return true;
}
template<class Shells,class Triangles>
Prepared prepare(const Shells& shells,std::uint32_t source,const Triangles& local){
    if(shells.empty()||shells.size()>4096||local.empty()||local.size()>4096)return {};
    constexpr double localTolerance=.00001;
    std::vector<std::size_t> owned;Point anchor{};bool anchored=false;
    for(std::size_t i=0;i<shells.size();++i)if(shells[i].source==source){
        const auto& shell=shells[i];Point p={shell.point[0],shell.point[1],shell.point[2]};
        if(!finite(p)||(anchored&&!coincides(p,anchor,localTolerance)))return {};
        anchor=p;anchored=true;owned.push_back(i);
        for(float value:shell.triangle)if(!std::isfinite(value))return {};
    }
    if(owned.size()<4)return {};
    std::vector<bool> excluded(local.size()),used(owned.size());std::size_t work=0,matches=0;
    for(std::size_t t=0;t<local.size();++t){bool duplicate=false;
        for(std::size_t i=0;i<owned.size();++i){if(++work>(1u<<20))return {};const auto& flat=shells[owned[i]].triangle;
            const auto p=[&](unsigned k){return Point{flat[k*3],flat[k*3+1],flat[k*3+2]};};bool same=false;
            for(unsigned k=0;k<3&&!same;++k)if(coincides(point(local[t][0]),p(k),localTolerance))
                same=(coincides(point(local[t][1]),p((k+1)%3),localTolerance)&&coincides(point(local[t][2]),p((k+2)%3),localTolerance))||
                     (coincides(point(local[t][2]),p((k+1)%3),localTolerance)&&coincides(point(local[t][1]),p((k+2)%3),localTolerance));
            if(same){if(used[i])duplicate=true;else{used[i]=true;excluded[t]=true;++matches;break;}}
        }
        if(!excluded[t]&&duplicate)return {};
    }
    if(matches<4)return {};
    // The largest triangle avoids amplifying native float round-off through a
    // tiny ornament when the fixture is far from the map origin.
    std::size_t best=local.size();double area=0;
    for(std::size_t i=0;i<local.size();++i)if(excluded[i]){const auto normal=cross(sub(point(local[i][1]),point(local[i][0])),sub(point(local[i][2]),point(local[i][0])));const double candidate=dot(normal,normal);
        if(candidate>area){area=candidate;best=i;}}
    if(best==local.size())return {};
    Prepared result;result.excluded=std::move(excluded);result.anchor=anchor;result.best=best;
    for(std::size_t i=0;i<result.excluded.size();++i)if(result.excluded[i])result.indices.push_back(i);
    return result;
}
template<class Triangles>
bool verify(const Prepared& prepared,const Triangles& local,const Triangles& world,const float* selectedPoint){
    constexpr double worldTolerance=.0025;
    if(!selectedPoint||!prepared.valid()||local.size()!=prepared.excluded.size()||local.size()!=world.size()||local.size()>4096)return false;
    const Point selected={selectedPoint[0],selectedPoint[1],selectedPoint[2]};
    Transform transform;if(!finite(selected)||!transform.fit(local[prepared.best],world[prepared.best])||
        !coincides(transform.apply(prepared.anchor),selected,worldTolerance))return false;
    for(std::size_t i:prepared.indices){if(i>=local.size())return false;for(unsigned k=0;k<3;++k)
        if(!coincides(transform.apply(point(local[i][k])),point(world[i][k]),worldTolerance))return false;}
    return true;
}
template<class Shells,class Triangles>
std::vector<bool> matching(const Shells& shells,std::uint32_t source,const Triangles& local,const Triangles& world,const float* selectedPoint){
    auto prepared=prepare(shells,source,local);
    return verify(prepared,local,world,selectedPoint)?std::move(prepared.excluded):std::vector<bool>{};
}
}
