#include <windows.h>
#include "native_fixture_shadow.h"
#include <cassert>
#include <cstdio>
#include <fstream>

struct P {float x,y,z;};
using Triangle=std::array<P,3>;
struct Shell {std::uint32_t source;float point[3];std::array<float,9> triangle;};
static_assert(sizeof(Shell)==52,"shell test format");
struct Stream {unsigned stride=0;std::vector<unsigned char> bytes;};
struct Mesh {unsigned vertexCount=0,primitiveCount=0;bool indexed=false;Stream streams[4];std::vector<std::uint32_t> indices;};
P placed(P p,float scale=1.375f){
    const float c=std::cos(.63f),s=std::sin(.63f),a=std::cos(.2f),b=std::sin(.2f);
    const float x=p.x*c-p.y*s,y=p.x*s+p.y*c;
    return {-8555.f+scale*x,1000.f+scale*(y*a-p.z*b),100.f+scale*(y*b+p.z*a)};
}
void compaction(const std::vector<Shell>& shell,const std::vector<Triangle>& original,const float* position){
    auto local=original;std::rotate(local.begin(),local.end()-1,local.end()); // Frame first: owned indices are not a prefix.
    const auto full=NorthlightNativeFixtureShadow::prepare(shell,shell[0].source,local);assert(full.valid());
    Mesh input;input.vertexCount=unsigned(local.size()*3);input.primitiveCount=unsigned(local.size());input.indexed=true;
    input.streams[0].stride=16;input.streams[1].stride=8;
    for(unsigned s=0;s<2;++s)input.streams[s].bytes.resize(input.vertexCount*input.streams[s].stride);
    input.streams[3].stride=~0u;input.streams[3].bytes={1}; // Unused stream is neither copied nor charged.
    for(unsigned i=0;i<input.vertexCount;++i){const auto vertex=input.vertexCount-1-i;input.indices.push_back(vertex);
        std::memcpy(input.streams[0].bytes.data()+vertex*16,&local[i/3][i%3],sizeof(P));
        const std::uint32_t values[]={vertex^0x12345678u,vertex^0x98765432u};std::memcpy(input.streams[1].bytes.data()+vertex*8,values,8);}
    const auto before=input;auto prepared=full;auto reduced=local;Mesh compact;
    assert(NorthlightNativeFixtureShadow::compact(input,false,3,prepared,reduced,compact));
    assert(input.indices==before.indices&&input.streams[0].bytes==before.streams[0].bytes&&input.streams[1].bytes==before.streams[1].bytes);
    assert(compact.vertexCount==shell.size()*3&&compact.primitiveCount==shell.size()&&compact.streams[3].bytes.empty()&&!compact.streams[3].stride);
    assert(reduced.size()==shell.size()&&prepared.indices.size()==shell.size()&&reduced[prepared.best][0].x==local[full.best][0].x);
    for(unsigned i=0;i<full.indices.size();++i)for(unsigned k=0;k<3;++k){const auto source=input.indices[full.indices[i]*3+k],target=compact.indices[i*3+k];
        for(unsigned s=0;s<2;++s)assert(!std::memcmp(input.streams[s].bytes.data()+source*input.streams[s].stride,compact.streams[s].bytes.data()+target*compact.streams[s].stride,input.streams[s].stride));}
    std::vector<Triangle> world=reduced;for(auto& triangle:world)for(auto& p:triangle)p=placed(p);
    assert(NorthlightNativeFixtureShadow::verify(prepared,reduced,world,position));
    const auto other=prepared.best?0:world.size()-1;world[other][2].z+=.03f;
    assert(!NorthlightNativeFixtureShadow::verify(prepared,reduced,world,position)); // Every owned vertex, not just the fit triangle.
    const auto reject=[&](const Mesh& mesh,unsigned streams=3){auto p=full;auto triangles=local;Mesh output;output.vertexCount=99;output.indices={17};
        const bool rejected=!NorthlightNativeFixtureShadow::compact(mesh,false,streams,p,triangles,output);
        assert(output.vertexCount==99&&(output.indices==std::vector<std::uint32_t>{17})&&p.indices==full.indices&&p.best==full.best&&triangles.size()==local.size()&&!std::memcmp(triangles.data(),local.data(),local.size()*sizeof(Triangle)));return rejected;};
    auto bad=input;bad.indices[0]=input.vertexCount;assert(reject(bad));bad=input;bad.streams[0].bytes.pop_back();assert(reject(bad));
    bad=input;bad.streams[0].stride=0;assert(reject(bad));assert(reject(input,7)&&reject(input,0)&&reject(input,16));
    bad=input;bad.streams[0].stride=unsigned(NorthlightNativeFixtureShadow::CompactBytes/compact.vertexCount+1);bad.streams[0].bytes.resize(bad.vertexCount*bad.streams[0].stride);assert(reject(bad));
    prepared=full;prepared.indices.pop_back();reduced=local;assert(!NorthlightNativeFixtureShadow::compact(input,false,3,prepared,reduced,compact));
    // Nonindexed strip corners share their original vertex IDs and flatten to a list.
    Mesh strip;strip.vertexCount=7;strip.primitiveCount=5;strip.streams[0].stride=12;strip.streams[0].bytes.resize(7*12);
    std::vector<Triangle> triangles(5);for(unsigned i=0;i<5;++i)for(unsigned k=0;k<3;++k)triangles[i][k]={float(i+k),float((i+k)%2),0};
    prepared={};prepared.excluded={true,false,true,true,true};prepared.indices={0,2,3,4};prepared.best=2;
    assert(NorthlightNativeFixtureShadow::compact(strip,true,1,prepared,triangles,compact));
    assert(compact.vertexCount==7&&compact.primitiveCount==4&&prepared.best==1&&(compact.indices==std::vector<std::uint32_t>{0,1,2,2,3,4,3,4,5,4,5,6}));
    Mesh expanded;expanded.vertexCount=12288;expanded.primitiveCount=4096;expanded.streams[0].stride=48;expanded.streams[0].bytes.resize(12288*48);
    triangles.resize(4096);prepared={};prepared.excluded.assign(4096,true);for(unsigned i=0;i<4096;++i)prepared.indices.push_back(i);
    assert(NorthlightNativeFixtureShadow::compact(expanded,false,1,prepared,triangles,compact)&&compact.vertexCount==12288&&compact.streams[0].bytes.size()==12288*48);
    std::printf("PASS compact owned mesh triangles=%zu requiredStreams=2 byteCap=%zu allOwnedVertices=1\n",shell.size(),NorthlightNativeFixtureShadow::CompactBytes);
}
void check(const std::vector<Shell>& shell){
    assert(shell.size()>=4);std::vector<Triangle> local,world;
    for(const auto& s:shell){Triangle t;for(unsigned k=0;k<3;++k)t[k]={s.triangle[k*3],s.triangle[k*3+1],s.triangle[k*3+2]};local.push_back(t);}
    // A frame/wall in the same draw remains opaque, including a disconnected
    // triangle inside the bulb's bounds.
    local.push_back({P{.01f,.02f,.03f},P{.04f,.03f,.02f},P{.02f,.04f,.01f}});
    for(const auto& t:local){Triangle q;for(unsigned k=0;k<3;++k)q[k]=placed(t[k]);world.push_back(q);}
    const auto anchor=placed({shell[0].point[0],shell[0].point[1],shell[0].point[2]});const float position[]={anchor.x,anchor.y,anchor.z};
    const auto match=[&](const auto& a,const auto& b,const float* p= nullptr){return NorthlightNativeFixtureShadow::matching(shell,shell[0].source,a,b,p?p:position);};
    const auto result=match(local,world);assert(result.size()==local.size()&&!result.back());
    assert(std::count(result.begin(),result.end(),true)==std::ptrdiff_t(shell.size()));
    const auto prepared=NorthlightNativeFixtureShadow::prepare(shell,shell[0].source,local);assert(prepared.valid());
    compaction(shell,local,position);
    assert(NorthlightNativeFixtureShadow::verify(prepared,local,world,position));
    float foreign[]={anchor.x+1,anchor.y,anchor.z};assert(match(local,world,foreign).empty());
    auto moved=world;for(auto& t:moved)for(auto& p:t)p.x+=1;assert(match(local,moved).empty());
    assert(NorthlightNativeFixtureShadow::verify(prepared,local,moved,foreign));
    assert(!NorthlightNativeFixtureShadow::verify(prepared,local,moved,position));
    auto warped=world;warped[0][0].z+=.03f;assert(match(local,warped).empty());
    auto raw=local;raw[0][0].z+=.01f;const auto changed=match(raw,world);assert(changed.empty()||!changed[0]);
    raw=local;raw.push_back(raw[0]);auto duplicate=world;duplicate.push_back(duplicate[0]);assert(match(raw,duplicate).empty());
    assert(NorthlightNativeFixtureShadow::matching(shell,shell[0].source+1,local,world,position).empty());
    assert(match(local,std::vector<Triangle>{}).empty());
    auto nonfinite=world;nonfinite[0][0].x=NAN;assert(match(local,nonfinite).empty());
    auto sheared=world;for(auto& t:sheared)for(auto& p:t)p.x+=.1f*(p.y-1000);assert(match(local,sheared).empty());
    std::vector<Triangle> insufficient(local.begin(),local.begin()+3),few(world.begin(),world.begin()+3);assert(match(insufficient,few).empty());
    // Reversed winding and submission order preserve exact triangle identity.
    std::reverse(local.begin(),local.end());std::reverse(world.begin(),world.end());for(auto& t:local)std::swap(t[0],t[2]);for(auto& t:world)std::swap(t[0],t[2]);
    const auto reversed=match(local,world);assert(reversed.size()==local.size()&&!reversed.front());
}
int main(int argc,char** argv){
    for(int i=1;i<argc;++i){std::ifstream in(argv[i],std::ios::binary);std::uint32_t count=0;in.read(reinterpret_cast<char*>(&count),4);assert(count>=4&&count<=4096);
        std::vector<Shell> shell(count);in.read(reinterpret_cast<char*>(shell.data()),shell.size()*sizeof(Shell));assert(in&&in.peek()==EOF);check(shell);std::printf("PASS native fixture replay shell %s triangles=%u\n",argv[i],count);}
    assert(argc>1);return 0;
}
