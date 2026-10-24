#define NOMINMAX
#include <d3d9.h>
#include <cassert>
#include <cstdio>
#include <chrono>
#include <cfloat>
#include "legacy_actor_deformation.h"
#include "actor_deformation.h"
#include "actor_client_programs.h"
using namespace NorthlightActorDeformation;
namespace Old=LegacyActorDeformation;
static_assert(sizeof(Kernel)==sizeof(Old::Kernel),"Evaluator flags must fit existing metadata padding");
static_assert(sizeof(Program)==sizeof(Old::Program),"Program/cache accounting must retain its size");
static uint32_t randomState=12345;
static uint32_t next(){return randomState=randomState*1664525u+1013904223u;}
static float finite(){return (float(int(next()%20001))-10000.f)*.01f;}
static uint64_t comparisons=0,successes=0,failures=0,strictChecks=0,aliasChecks=0,newOutputChecks=0,preparedChecks=0,boundsChecks=0;
static Old::Program legacy(const Program& p){
    Old::Program q;q.major=p.major;q.positionRegister=p.positionRegister;q.textureCoordinates=p.textureCoordinates;q.skinned=p.skinned;q.outputMask=p.outputMask;q.paletteBase=p.paletteBase;
    for(const auto& i:p.inputs)q.inputs.push_back({i.reg,i.usage,i.index});for(const auto& d:p.definitions)q.definitions.push_back({d.reg,d.value});
    for(const auto& o:p.operations){Old::Operation a;a.code=o.code;a.destination=o.destination;for(unsigned i=0;i<3;++i){a.source[i].token=o.source[i].token;a.source[i].address=o.source[i].address;}q.operations.push_back(a);}Old::buildKernel(q);return q;
}
template<bool Strict>static void compare(const Program& p,const Old::Program& old,const Four input[16],const float* bank,bool alias){
    Four a={17,19,23,29},b=a,oracle=a;const bool ra=Old::evaluateFast<Strict>(old,input,bank,a),rb=evaluateFast<Strict>(p,input,bank,b),ro=evaluate<Strict>(p,input,bank,oracle);
    assert(ra==rb&&ra==ro);assert(p.kernel.skin==old.kernel.skin);if(ra){assert(!std::memcmp(a.data(),b.data(),16)&&!std::memcmp(a.data(),oracle.data(),16));++successes;}else ++failures;
    if(p.kernel.singleSkin()||p.kernel.commutedSkin()){assert(!std::memcmp(a.data(),b.data(),16));++newOutputChecks;}
    if constexpr(!Strict)if(p.kernel.singleSkin()){Four result={17,19,23,29};const auto control=singleSkinControl(p,bank);const bool rc=evaluateSingleSkin<true>(p,input,bank,result,&control);assert(rc==ra&&!std::memcmp(a.data(),result.data(),16));++preparedChecks;
        Four inNew[2]={input[0],input[1]},inOld[16];std::memcpy(inOld,input,sizeof inOld);const bool alias=evaluateSingleSkin<true>(p,inNew,bank,inNew[0],&control),oldAlias=Old::evaluateFast(old,inOld,bank,inOld[0]);assert(alias==oldAlias&&!std::memcmp(inNew[0].data(),inOld[0].data(),16));++preparedChecks;}
    ++comparisons;strictChecks+=Strict;
    if(alias){Four inA[16],inB[16];std::memcpy(inA,input,sizeof inA);std::memcpy(inB,input,sizeof inB);
        const bool left=Old::evaluateFast<Strict>(old,inA,bank,inA[0]),right=evaluateFast<Strict>(p,inB,bank,inB[0]);assert(left==right&&!std::memcmp(inA,inB,sizeof inA));++aliasChecks;}
}
static void batchBounds(const Program& p){
    if(!p.kernel.singleSkin())return;const auto old=legacy(p);
    for(unsigned type=0;type<=16;++type)for(unsigned indices=0;indices<=16;++indices)for(unsigned phase=0;phase<2;++phase){
        NorthlightDrawSnapshot::Mesh mesh;mesh.vertexCount=71;for(auto& stream:mesh.streams){stream.stride=64;stream.bytes.resize(size_t(mesh.vertexCount)*64);if(!phase)for(auto& b:stream.bytes)b=uint8_t(next());}
        D3DVERTEXELEMENT9 elements[]={{0,4,BYTE(type),0,0,0},{1,2,BYTE(indices),0,2,0},{0xff,0,17,0,0,0}};PreparedBindings prepared;assert(prepareBindings(p,mesh,elements,std::size(elements),prepared));
        float bank[1024];for(auto& v:bank)v=finite();bank[0]=3;bank[1]=1;
        float low[3]={FLT_MAX,FLT_MAX,FLT_MAX},high[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX},beforeLow[3],beforeHigh[3];std::memcpy(beforeLow,low,sizeof low);std::memcpy(beforeHigh,high,sizeof high);
        const bool current=singleSkinBoundsPrepared(p,mesh,prepared,bank,low,high);bool prior=true;
        for(unsigned vertex=0;vertex<mesh.vertexCount&&prior;++vertex){Four input[16]={},view;for(const auto& b:prepared.bindings){const auto& s=mesh.streams[b.element.Stream];if(!decodeElement(s.bytes.data()+size_t(vertex)*s.stride+b.element.Offset,b.element.Type,input[b.input.reg])){prior=false;break;}}
            if(!prior)break;if(!Old::evaluateFast(old,input,bank,view)){prior=false;break;}for(unsigned k=0;k<3;++k){if(!std::isfinite(view[k])){prior=false;break;}beforeLow[k]=std::min(beforeLow[k],view[k]);beforeHigh[k]=std::max(beforeHigh[k],view[k]);}}
        assert(current==prior&&!std::memcmp(low,beforeLow,sizeof low)&&!std::memcmp(high,beforeHigh,sizeof high));if(phase)assert(current);++boundsChecks;
        auto invalid=prepared;invalid.bindings[1].input.reg=invalid.bindings[0].input.reg;assert(!singleSkinBoundsPrepared(p,mesh,invalid,bank,low,high));++boundsChecks;
    }
}
static void sample(const Program& p,unsigned count){
    const auto old=legacy(p);float bank[1024];Four input[16];const uint32_t special[]={0x7fc12345,0x7fa54321,0x7f800000,0xff800000,0x80000000,0,0x00800000,1,0x80000001,0x7f7fffff,0xff7fffff,0x3f000000,0xbf000000,0x42e00000,0x42e20000};
    for(unsigned n=0;n<count;++n){for(auto& value:bank)value=finite();for(auto& v:input)for(auto& value:v)value=finite();input[0][3]=1;
        for(unsigned k=0;k<4;++k){input[p.kernel.singleSkin()?1:2][k]=float(next()%75);if(!p.kernel.singleSkin())input[1][k]=float(next()%256)/255.f;}
        if(n%4==0){const uint32_t bits=special[(n/4)%std::size(special)];std::memcpy(&input[next()%3][next()%4],&bits,4);std::memcpy(bank+31*4+next()%64,&bits,4);}
        if(n%11==0)input[p.kernel.singleSkin()?1:2][0]=float(int(next()%8300)-4150)+.5f;
        compare<false>(p,old,input,bank,n%8==0);compare<true>(p,old,input,bank,n%8==0);}
}
static void mutations(const Program& original){
    assert(original.kernel.singleSkin()||original.kernel.commutedSkin());unsigned rejected=0;
    for(size_t i=0;i<original.operations.size();++i)for(unsigned field=0;field<8;++field){Program p=original;auto& o=p.operations[i];
        if(!field)o.code^=128;else if(field==1)o.destination^=0x00100000;else{auto& source=o.source[(field-2)/2];(field&1?source.address:source.token)^=1;}
        buildKernel(p);assert(!p.kernel.singleSkin()&&!p.kernel.commutedSkin());++rejected;}
    std::printf("ACTOR_KERNEL_SIGNATURE_PASS mutations=%u single=%u commuted=%u\n",rejected,unsigned(original.kernel.singleSkin()),unsigned(original.kernel.commutedSkin()));
}
__attribute__((noinline))static bool oldCall(const Old::Program& p,const Four* in,const float* bank,Four& out){return Old::evaluateFast(p,in,bank,out);}
__attribute__((noinline))static bool newCall(const Program& p,const Four* in,const float* bank,Four& out){return evaluateFast(p,in,bank,out);}
static void benchmark(const Program& p,const char* name){
    const auto old=legacy(p);float bank[1024]={};for(unsigned r=0;r<256;++r){bank[r*4]=1;bank[r*4+1]=.5f;bank[r*4+2]=.25f;bank[r*4+3]=1;}bank[0]=3;bank[1]=1;
    std::vector<std::array<Four,16>> inputs(8192);for(unsigned i=0;i<inputs.size();++i){inputs[i][0]={float(i%19),float(i%13),float(i%7),1};inputs[i][1]={.25f,.25f,.25f,.25f};inputs[i][2]={0,1,2,3};if(p.kernel.singleSkin())inputs[i][1]={float(i%64),0,0,0};}
    volatile float checksum=0;std::vector<double> before,after;
    for(unsigned round=0;round<9;++round)for(unsigned order=0;order<2;++order){const bool optimized=(round+order)&1;const auto start=std::chrono::steady_clock::now();
        for(unsigned draw=0;draw<16;++draw){float total=0;for(const auto& input:inputs){Four out;const bool ok=optimized?newCall(p,input.data(),bank,out):oldCall(old,input.data(),bank,out);assert(ok);total+=out[0];}checksum=checksum+total;}
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/16;(optimized?after:before).push_back(ms);}
    assert(std::isfinite(checksum));std::sort(before.begin(),before.end());std::sort(after.begin(),after.end());std::printf("ACTOR_KERNEL_BENCH name=%s vertices=8192 operations=%zu oldMedianMs=%.6f newMedianMs=%.6f changePercent=%.3f oldMinMs=%.6f oldMaxMs=%.6f newMinMs=%.6f newMaxMs=%.6f\n",name,p.operations.size(),before[4],after[4],100*(after[4]/before[4]-1),before.front(),before.back(),after.front(),after.back());
}
__attribute__((noinline))static bool oldBounds(const Old::Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const PreparedBindings& prepared,const float* constants,float* low,float* high){
    for(unsigned vertex=0;vertex<mesh.vertexCount;++vertex){Four input[16]={},view;for(const auto& b:prepared.bindings){const auto& s=mesh.streams[b.element.Stream];if(!decodeElement(s.bytes.data()+size_t(vertex)*s.stride+b.element.Offset,b.element.Type,input[b.input.reg]))return false;}
        if(!Old::evaluateFast(program,input,constants,view))return false;for(unsigned k=0;k<3;++k){if(!std::isfinite(view[k]))return false;low[k]=std::min(low[k],view[k]);high[k]=std::max(high[k],view[k]);}}
    return true;
}
__attribute__((noinline))static bool newBounds(const Program& p,const NorthlightDrawSnapshot::Mesh& mesh,const PreparedBindings& bindings,const float* bank,float* low,float* high){return singleSkinBoundsPrepared(p,mesh,bindings,bank,low,high);}
static void boundsBenchmark(const Program& p,const char* name,unsigned vertices){
    const auto old=legacy(p);float bank[1024]={};for(unsigned r=0;r<256;++r){bank[r*4]=1;bank[r*4+1]=.5f;bank[r*4+2]=.25f;bank[r*4+3]=1;}bank[0]=3;bank[1]=1;
    NorthlightDrawSnapshot::Mesh mesh;mesh.vertexCount=vertices;for(unsigned stream=0;stream<2;++stream){mesh.streams[stream].stride=16;mesh.streams[stream].bytes.resize(size_t(vertices)*16);for(unsigned i=0;i<vertices;++i){const Four value=stream?Four{float(i%64),0,0,0}:Four{float(i%19),float(i%13),float(i%7),1};std::memcpy(mesh.streams[stream].bytes.data()+size_t(i)*16,value.data(),16);}}
    D3DVERTEXELEMENT9 elements[]={{0,0,3,0,0,0},{1,0,3,0,2,0},{0xff,0,17,0,0,0}};PreparedBindings bindings;assert(prepareBindings(p,mesh,elements,std::size(elements),bindings));
    std::vector<double> before,after;volatile float checksum=0;const unsigned draws=vertices<1024?1024:16;
    for(unsigned round=0;round<9;++round)for(unsigned order=0;order<2;++order){const bool optimized=(round+order)&1;const auto start=std::chrono::steady_clock::now();for(unsigned draw=0;draw<draws;++draw){float low[3]={FLT_MAX,FLT_MAX,FLT_MAX},high[3]={-FLT_MAX,-FLT_MAX,-FLT_MAX};const bool ok=optimized?newBounds(p,mesh,bindings,bank,low,high):oldBounds(old,mesh,bindings,bank,low,high);assert(ok);checksum=checksum+low[0]+high[0];}const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count()/draws;(optimized?after:before).push_back(ms);}
    assert(std::isfinite(checksum));std::sort(before.begin(),before.end());std::sort(after.begin(),after.end());std::printf("ACTOR_BOUNDS_BENCH name=%s vertices=%u oldMedianMs=%.6f newMedianMs=%.6f changePercent=%.3f oldMinMs=%.6f oldMaxMs=%.6f newMinMs=%.6f newMaxMs=%.6f\n",name,vertices,before[4],after[4],100*(after[4]/before[4]-1),before.front(),before.back(),after.front(),after.back());
}
int main(int argc,char** argv){
    assert(argc>=2);const bool bench=argc>2;std::vector<std::pair<const char*,Program>> targets;
    auto fixture=[&](const auto& words,const char* name,bool expected){Program p;assert(compile(words,std::size(words),p));assert(bool(p.kernel.singleSkin()||p.kernel.commutedSkin())==expected);sample(p,4096);batchBounds(p);if(expected)mutations(p);targets.push_back({name,std::move(p)});};
    fixture(ClientShaders::OneBoneVs3,"single-sm3-w-first",true);fixture(ClientShaders::OneBoneVs3WLast,"single-sm3-w-last",true);fixture(ClientShaders::OneBoneVs3WMid,"single-sm3-w-mid",true);fixture(ClientShaders::OneBoneVs2,"single-sm2",true);fixture(ClientShaders::NoBoneVs3,"rigid-control",false);
    // VS1 retains its distinct floor addressing. Current-pose definitions may override any palette row.
    auto sm1=targets[0].second;sm1.major=1;for(auto& o:sm1.operations){if(o.code==46)o.code=1;for(auto& s:o.source)s.address=0;}buildKernel(sm1);assert(sm1.kernel.singleSkin());sample(sm1,4096);batchBounds(sm1);
    auto overridden=targets[0].second;overridden.definitions.push_back({31,{2,3,4,5}});overridden.definitions.push_back({0,{2,1,0,0}});buildKernel(overridden);sample(overridden,4096);batchBounds(overridden);
    FILE* f=std::fopen(argv[1],"rb");assert(f);uint32_t bytes;unsigned programs=0,compiled=0,single=0,commuted=0,oldWeighted=0,majors[4]={};
    while(std::fread(&bytes,4,1,f)==1){assert(bytes%4==0&&bytes<262145);std::vector<Word> words(bytes/4);assert(std::fread(words.data(),1,bytes,f)==bytes);++programs;
        Program p;Old::Program q;const bool current=compile(words.data(),words.size(),p),prior=Old::compile(words.data(),words.size(),q);assert(current==prior);if(!current)continue;++compiled;++majors[p.major];assert(p.kernel.skin==q.kernel.skin);
        single+=bool(p.kernel.singleSkin());commuted+=p.kernel.commutedSkin();oldWeighted+=p.kernel.skin;sample(p,64);
        if(p.kernel.commutedSkin()&&std::none_of(targets.begin(),targets.end(),[](const auto& t){return t.second.kernel.commutedSkin();})){mutations(p);targets.push_back({"weighted-sm2-commuted",p});}
        if(p.kernel.skin&&std::none_of(targets.begin(),targets.end(),[](const auto& t){return t.second.kernel.skin;}))targets.push_back({"weighted-control",p});}
    std::fclose(f);assert(programs>1000&&compiled>1000&&single&&commuted&&oldWeighted);
    std::printf("ACTOR_KERNEL_CORPUS_PASS programs=%u compiled=%u SM1=%u SM2=%u SM3=%u single=%u commuted=%u oldWeighted=%u unchangedBoundsEligibility=1 metadataSizeUnchanged=1\n",programs,compiled,majors[1],majors[2],majors[3],single,commuted,oldWeighted);
    std::printf("ACTOR_KERNEL_ORACLE_PASS comparisons=%llu successes=%llu failures=%llu strict=%llu alias=%llu newlyRecognizedOutputIncludingFailure=%llu\n",(unsigned long long)comparisons,(unsigned long long)successes,(unsigned long long)failures,(unsigned long long)strictChecks,(unsigned long long)aliasChecks,(unsigned long long)newOutputChecks);
    std::printf("ACTOR_SINGLE_PREPARED_PASS vertex_and_alias=%llu bounds_and_malformed=%llu declaration_type_pairs=289 current_constants=1 failed_bounds_exact=1\n",(unsigned long long)preparedChecks,(unsigned long long)boundsChecks);
    if(bench)for(const auto& target:targets){benchmark(target.second,target.first);if(target.second.kernel.singleSkin())for(unsigned vertices:{71u,256u,8192u})boundsBenchmark(target.second,target.first,vertices);}
}
