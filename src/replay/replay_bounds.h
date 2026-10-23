#pragma once
#include "actor_deformation.h"
#include "replay_bounds_budget.h"
#include "replay_memo_policy.h"
#include <limits>
#include <memory>
#include <chrono>
#include <unordered_map>
#include <optional>
#include <string>

// All submitted snapshot vertices, after the audited original deformation.
// This helper never samples one vertex to infer a model/actor bounding box.
// Unknown/budget-limited bounds mean DRAW, never reject the caster.
namespace NorthlightReplayBounds {
struct Bounds {
    float low[3]={},high[3]={};bool valid=false;
};
enum class Status {Valid,Unsupported,Budget,Invalid};
struct Budget {
    size_t vertices=0,operations=0;
    size_t maxVertices=4096,maxOperations=262144;
    void reset(){vertices=operations=0;}
};
// Numeric error enclosure for the audited linear position subset. Each register
// component carries a conservative absolute error and an exact CPU float value.
// This covers separate MUL/ADD versus fused MAD, dot-product reassociation, and
// GPU flush-to-zero. Addressing is accepted only when the entire error interval
// maps to the exact same address. Other arithmetic fails open to unbounded replay.
namespace detail {
struct Value {float value=0;double error=0;};
using Vector=std::array<Value,4>;
inline double roundError(double magnitude){return magnitude*(2*std::numeric_limits<float>::epsilon())+std::numeric_limits<float>::min();}
inline Value add(Value a,Value b,bool subtract=false){
    const double exact=double(a.value)+(subtract?-double(b.value):double(b.value));float result=float(exact);
    const double inherited=a.error+b.error;
    // Exact representable integer/address arithmetic need not acquire error.
    const double rounding=(inherited==0&&double(result)==exact&&!(result!=0&&std::fabs(result)<std::numeric_limits<float>::min()))?0:roundError(std::fabs(a.value)+std::fabs(b.value)+inherited);
    return {result,inherited+rounding};
}
inline Value mul(Value a,Value b){
    const double exact=double(a.value)*b.value;float result=float(exact);
    const double inherited=std::fabs(a.value)*b.error+std::fabs(b.value)*a.error+a.error*b.error;
    const double rounding=(inherited==0&&double(result)==exact&&!(result!=0&&std::fabs(result)<std::numeric_limits<float>::min()))?0:roundError(std::fabs(exact)+inherited);
    return {result,inherited+rounding};
}
inline bool linear(const NorthlightActorDeformation::Program& p){
    if(p.textureCoordinates||p.major<1||p.major>3||p.positionRegister>=32||p.operations.empty()||p.operations.size()>512)return false;
    for(const auto& op:p.operations)if(op.code!=1&&op.code!=2&&op.code!=3&&op.code!=4&&op.code!=5&&op.code!=8&&op.code!=9&&op.code!=46)return false;
    return true;
}
inline bool evaluateIntervals(const NorthlightActorDeformation::Program& p,const Vector inputs[16],const float* constants,Vector& position,bool definitionsApplied=false){
    // Most rigid programs touch only a few temporaries. Construct their values
    // on first write; the lane masks still prove every subsequent read.
    using namespace NorthlightShadowShader;std::optional<Vector> temp[33];Vector address={};
    // Zeroed host storage is not proof that a GPU temporary/address component
    // was initialized. Track writes and only inspect lanes actually consumed by
    // this instruction (DP3/DP4 consume their full dot-product source lanes).
    unsigned initialized[33]={},addressInitialized=0,inputInitialized=0;
    if(p.positionRegister>=32||!inputs||!constants)return false;
    for(const auto& input:p.inputs){if(input.reg>=16)return false;inputInitialized|=1u<<input.reg;}
    auto source=[&](const NorthlightActorDeformation::Source& s,Vector& result,unsigned lanes)->bool{
        unsigned type=regType(s.token);int index=int(regIndex(s.token));Vector value={};
        if(s.token&0x2000){unsigned c=p.major>=2?((s.address>>16)&3):0;if(!(addressInitialized&(1u<<c))||address[c].error!=0||!std::isfinite(address[c].value)||std::fabs(address[c].value)>4096)return false;index+=int(address[c].value);}
        unsigned valid=15;
        if(type==0){if(index<0||index>=32||!initialized[index])return false;value=*temp[index];valid=initialized[index];}
        else if(type==1){if(index<0||index>=16||!(inputInitialized&(1u<<index)))return false;value=inputs[index];}
        else if(type==3){if(index)return false;value=address;valid=addressInitialized;}
        else if(type==2){if(index<0||index>=256)return false;for(unsigned j=0;j<4;++j)value[j].value=constants[4*index+j];if(!definitionsApplied)for(auto& d:p.definitions)if(d.reg==unsigned(index))for(unsigned j=0;j<4;++j)value[j].value=d.value[j];}
        else return false;
        for(unsigned j=0;j<4;++j){if(!(lanes&(1u<<j)))continue;const unsigned channel=(s.token>>(16+2*j))&3;if(!(valid&(1u<<channel)))return false;auto v=value[channel];unsigned modifier=(s.token>>24)&15;
            if(modifier==11||modifier==12)v.value=std::fabs(v.value);else if(modifier!=0&&modifier!=1)return false;
            if(modifier==1||modifier==12)v.value=-v.value;if(!std::isfinite(v.value)||!std::isfinite(v.error))return false;
            if(v.value!=0&&std::fabs(v.value)<std::numeric_limits<float>::min())v.error+=std::fabs(v.value);
            result[j]=v;}
        return true;
    };
    for(const auto& op:p.operations){Vector a={},b={},c={},result={};const unsigned written=(op.destination>>16)&15;if(!written)continue;
        const unsigned lanes=op.code==8?7:op.code==9?15:written;
        if(!source(op.source[0],a,lanes))return false;
        const int n=arithmeticOperands(op.code,p.major)-1;if(n>1&&!source(op.source[1],b,lanes))return false;if(n>2&&!source(op.source[2],c,lanes))return false;
        if(op.code==8||op.code==9){Value sum;double magnitude=0;for(unsigned j=0;j<(op.code==8?3u:4u);++j){Value term=mul(a[j],b[j]);magnitude+=std::fabs(term.value)+term.error;sum=add(sum,term);}
            // Dot-product implementation can reorder its additions or use FMAs.
            sum.error+=roundError(magnitude)*8;result.fill(sum);
        }else for(unsigned j=0;j<4;++j){switch(op.code){
            case 1:case 46:result[j]=a[j];break;case 2:result[j]=add(a[j],b[j]);break;case 3:result[j]=add(a[j],b[j],true);break;case 5:result[j]=mul(a[j],b[j]);break;
            case 4:{auto product=mul(a[j],b[j]);if(product.value==0&&product.error==0){result[j]=c[j];break;} // exact 0*x+c: fused and separate both preserve c
                result[j]=add(product,c[j]);result[j].error+=roundError(std::fabs(double(a[j].value)*b[j].value)+std::fabs(c[j].value));break;}
            default:return false;}}
        const unsigned type=regType(op.destination),index=regIndex(op.destination),mask=(op.destination>>16)&15;
        if((type!=0&&type!=3)||(type==0&&index>=32)||(type==3&&index))return false;
        if(type==0&&!initialized[index])temp[index].emplace();
        Vector& target=type==3?address:*temp[index];
        for(unsigned j=0;j<4;++j)if(mask&(1u<<j)){auto v=result[j];if(!std::isfinite(v.value)||!std::isfinite(v.error)||v.error<0)return false;
            if(op.destination&(1u<<20)){double lo=std::max(0.,std::min(1.,double(v.value)-v.error)),hi=std::max(0.,std::min(1.,double(v.value)+v.error));v.value=std::max(0.f,std::min(1.f,v.value));v.error=std::max(v.value-lo,hi-v.value);}
            if(type==3){double lo=double(v.value)-v.error,hi=double(v.value)+v.error;if(lo<-4096||hi>4096)return false;
                if(p.major>=2){if(std::floor(lo+.5)!=std::floor(hi+.5)||std::fabs(lo-std::floor(lo)-.5)<1e-10||std::fabs(hi-std::floor(hi)-.5)<1e-10)return false;lo=std::round(lo);hi=std::round(hi);}else{lo=std::floor(lo);hi=std::floor(hi);}if(lo!=hi)return false;v={float(lo),0};}
            target[j]=v;if(type==3)addressInitialized|=1u<<j;else initialized[index]|=1u<<j;}
    }
    if(initialized[p.positionRegister]!=15)return false;
    // Bounds consumers transform an ordinary Vec3 with implicit homogeneous
    // W=1. A near-one or uncertain W is not interchangeable at large world or
    // light translations, so it must fail open rather than enter that path.
    position=*temp[p.positionRegister];return position[3].value==1.f&&position[3].error==0;
}
inline bool evaluate(const NorthlightActorDeformation::Program& p,const NorthlightActorDeformation::Four inputs[16],const float* constants,Vector& position){
    Vector intervals[16]={};for(unsigned i=0;i<16;++i)for(unsigned j=0;j<4;++j)intervals[i][j].value=inputs[i][j];
    return evaluateIntervals(p,intervals,constants,position);
}
} // namespace detail

// No D3D calls. Input buffers/constants must be this draw's immutable snapshot.
// Budget is shared over the whole frame; exceeding it returns invalid bounds.
inline Status calculate(const NorthlightActorDeformation::Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
        const D3DVERTEXELEMENT9* elements,size_t elementCount,const float* constants,const float* inverseView,
        Budget& budget,Bounds& output){
    output=Bounds{};
    if(!detail::linear(program))return Status::Unsupported;
    if(!elements||!constants||!inverseView||!elementCount||elementCount>MAXD3DDECLLENGTH+1||!mesh.vertexCount||mesh.vertexCount>262144)return Status::Invalid;
    if((mesh.topology!=D3DPT_TRIANGLELIST&&mesh.topology!=D3DPT_TRIANGLESTRIP)||!mesh.primitiveCount||mesh.primitiveCount>87380)return Status::Invalid;
    const size_t required=mesh.topology==D3DPT_TRIANGLELIST?size_t(mesh.primitiveCount)*3:size_t(mesh.primitiveCount)+2;
    if(mesh.indexed){if(mesh.indices.size()<required)return Status::Invalid;for(size_t j=0;j<required;++j)if(mesh.indices[j]>=mesh.vertexCount)return Status::Invalid;}
    else if(required>mesh.vertexCount)return Status::Invalid;
    const size_t ops=size_t(mesh.vertexCount)*program.operations.size();
    if(budget.vertices>budget.maxVertices||mesh.vertexCount>budget.maxVertices-budget.vertices||budget.operations>budget.maxOperations||ops>budget.maxOperations-budget.operations)return Status::Budget;
    budget.vertices+=mesh.vertexCount;budget.operations+=ops;
    for(unsigned i=0;i<16;++i)if(!std::isfinite(inverseView[i]))return Status::Invalid;
    if(inverseView[3]!=0||inverseView[7]!=0||inverseView[11]!=0||inverseView[15]!=1)return Status::Invalid;
    struct Binding{unsigned reg;D3DVERTEXELEMENT9 element;};Binding binding[16];unsigned bindings=0;
    for(const auto& input:program.inputs){if(input.reg>=16||bindings>=16)return Status::Invalid;bool found=false;
        for(size_t j=0;j<elementCount;++j){auto e=elements[j];if(e.Stream==0xff)break;if(e.Usage!=input.usage||e.UsageIndex!=input.index)continue;
            if(found||e.Stream>=4||e.Method!=D3DDECLMETHOD_DEFAULT)return Status::Invalid;const auto& s=mesh.streams[e.Stream];unsigned size=NorthlightDrawSnapshot::declarationBytes(e.Type);
            if(!size||UINT(e.Offset)+size>s.stride||uint64_t(mesh.vertexCount)*s.stride>s.bytes.size())return Status::Invalid;
            binding[bindings++]={input.reg,e};found=true;}if(!found)return Status::Invalid;}
    double low[3]={INFINITY,INFINITY,INFINITY},high[3]={-INFINITY,-INFINITY,-INFINITY};
    for(UINT vertex=0;vertex<mesh.vertexCount;++vertex){NorthlightActorDeformation::Four inputs[16]={};
        for(unsigned j=0;j<bindings;++j){auto b=binding[j];const auto& s=mesh.streams[b.element.Stream];if(!NorthlightActorDeformation::decodeElement(s.bytes.data()+size_t(vertex)*s.stride+b.element.Offset,b.element.Type,inputs[b.reg]))return Status::Invalid;}
        detail::Vector position;if(!detail::evaluate(program,inputs,constants,position))return Status::Unsupported;
        for(unsigned axis=0;axis<3;++axis){detail::Value world=detail::mul(position[3],{inverseView[12+axis],0});double magnitude=std::fabs(world.value)+world.error;
            for(unsigned k=0;k<3;++k){auto term=detail::mul(position[k],{inverseView[4*k+axis],0});magnitude+=std::fabs(term.value)+term.error;world=detail::add(world,term);}
            world.error+=detail::roundError(magnitude)*8+.001; // view/world transform reassociation and matrix storage
            if(!std::isfinite(world.value)||!std::isfinite(world.error)||std::fabs(world.value)>1000000||world.error>100)return Status::Unsupported;
            low[axis]=std::min(low[axis],double(world.value)-world.error);high[axis]=std::max(high[axis],double(world.value)+world.error);}
    }
    for(unsigned axis=0;axis<3;++axis){output.low[axis]=std::nextafter(float(low[axis]),-std::numeric_limits<float>::infinity());output.high[axis]=std::nextafter(float(high[axis]),std::numeric_limits<float>::infinity());}
    output.valid=true;return Status::Valid;
}
#include "replay_skin_envelope.inl"
#include "replay_envelopes.inl"
using WorkInfo=EnvelopeCache::WorkInfo;
using WorkKind=EnvelopeCache::Kind;
using Prepared=EnvelopeCache::Prepared;

// Exact memo of the existing all-vertex interval calculation. Mesh snapshots
// are immutable shared objects; weak ownership prevents both pointer ABA and
// retaining the geometry cache. No bounds are reused for owned/mutable packets.
class Cache {
    EnvelopeCache envelopes_;
    size_t replayCursor_=0,readyCursor_=0,heavyCursor_=0,buildCursor_=0;
    unsigned envelopeValid_=0,fallbackValid_=0,deferred_=0;
    struct Entry {
        std::weak_ptr<const NorthlightDrawSnapshot::Mesh> owner;
        std::vector<std::uint8_t> key;
        Bounds bounds;std::uint64_t touched=0;size_t bytes=0;
    };
    std::unordered_multimap<std::uint64_t,Entry> entries_;
    std::vector<std::uint8_t> scratch_;
    size_t bytes_=0;std::uint64_t clock_=0,hits_=0,misses_=0,avoidedVertices_=0,lookupNanoseconds_=0;unsigned lookupCalls_=0;
    static constexpr size_t Limit=4u*1024u*1024u,MaxEntries=512;
    void word(std::uint32_t v){const auto* p=reinterpret_cast<const std::uint8_t*>(&v);scratch_.insert(scratch_.end(),p,p+4);}
    void floats(const float* values,size_t n){for(size_t i=0;i<n;++i){std::uint32_t v;std::memcpy(&v,values+i,4);word(v);}}
    bool key(const NorthlightActorDeformation::Program& p,const D3DVERTEXELEMENT9* elements,size_t n,const float* constants,const float* inverse){
        scratch_.clear();
        if(!detail::linear(p)||!elements||!n||n>MAXD3DDECLLENGTH+1||!constants||!inverse||p.inputs.size()>16||p.definitions.size()>256)return false;
        word(p.major);word(p.positionRegister);word(p.textureCoordinates);word(p.skinned);
        word(std::uint32_t(p.operations.size()));bool registers[256]={};bool relative=false;
        for(const auto& op:p.operations){word(op.code);word(op.destination);
            const unsigned count=unsigned(NorthlightShadowShader::arithmeticOperands(op.code,p.major)-1);
            for(unsigned j=0;j<3;++j){word(op.source[j].token);word(op.source[j].address);
                if(j<count&&NorthlightShadowShader::regType(op.source[j].token)==2){
                    const unsigned reg=NorthlightShadowShader::regIndex(op.source[j].token);if(reg>=256)return false;
                    registers[reg]=true;relative|=(op.source[j].token&0x2000)!=0;
                }}
        }
        word(std::uint32_t(p.inputs.size()));for(const auto& i:p.inputs){word(i.reg);word(i.usage);word(i.index);}
        word(std::uint32_t(p.definitions.size()));for(const auto& d:p.definitions){word(d.reg);floats(d.value.data(),4);}
        word(std::uint32_t(n));for(size_t i=0;i<n;++i){const auto& e=elements[i];word(e.Stream);word(e.Offset);word(e.Type);word(e.Method);word(e.Usage);word(e.UsageIndex);}
        // Relative bone addressing can reach any c-register; never infer a
        // palette from the first vertex. Direct rigid programs need only their
        // referenced registers, including address calculations and definitions.
        for(unsigned i=0;i<256;++i)if(relative||registers[i]){word(i);floats(constants+4*i,4);}
        floats(inverse,16);return true;
    }
    void evict(){auto oldest=entries_.end();for(auto it=entries_.begin();it!=entries_.end();++it)if(oldest==entries_.end()||it->second.owner.expired()||it->second.touched<oldest->second.touched){oldest=it;if(it->second.owner.expired())break;}
        if(oldest!=entries_.end()){bytes_-=oldest->second.bytes;entries_.erase(oldest);}}
public:
    void clear(){envelopeValid_=fallbackValid_=deferred_=0;replayCursor_=readyCursor_=heavyCursor_=buildCursor_=0;envelopes_.clear();entries_.clear();scratch_.clear();bytes_=0;clock_=hits_=misses_=avoidedVertices_=lookupNanoseconds_=0;lookupCalls_=0;}
    void beginFrame(bool diagnostics=true){envelopeValid_=fallbackValid_=deferred_=0;envelopes_.beginFrame(diagnostics);hits_=misses_=avoidedVertices_=lookupNanoseconds_=0;lookupCalls_=0;}
    unsigned envelopeValid()const{return envelopeValid_;}
    unsigned fallbackValid()const{return fallbackValid_;}
    unsigned deferred()const{return deferred_;}
    size_t envelopeBytes()const{return envelopes_.reservedBytes();}
    size_t envelopeEntries()const{return envelopes_.entries();}
    size_t memoBytes()const{return envelopes_.memoBytes();}
    size_t memoEntries()const{return envelopes_.memoEntries();}
    const NorthlightReplayMemo::Policy& memoPolicy()const{return envelopes_.memoPolicy();}
    EnvelopeCache::MemoTotals memoTotals()const{return envelopes_.memoTotals();}
    double envelopeMilliseconds()const{return envelopes_.workMilliseconds();}
    size_t replayStart(size_t count)const{return count?replayCursor_%count:0;}
    void replayResume(size_t index){replayCursor_=index;}
    bool canEnclose()const{return envelopes_.canWork();}
    bool canReady()const{return envelopes_.canReady();}
    bool canCheap()const{return envelopes_.canReady();}
    void finishReservedTurnsAndLend(){envelopes_.finishReservedTurnsAndLend();}
    void settleClock(){envelopes_.settleClock();}
    void chargeCheapPreparation(std::uint64_t ns)noexcept{envelopes_.chargeCheapPreparation(ns);}
    double borrowedMicroseconds()const{return envelopes_.borrowedMicroseconds();}
    bool canHeavy()const{return envelopes_.canHeavy();}
    bool canBuild()const{return envelopes_.canBuild();}
    std::uint64_t phaseNanoseconds(unsigned phase)const{return envelopes_.phaseNanoseconds(phase);}
    std::uint64_t phaseLimit(unsigned phase)const{return envelopes_.phaseLimit(phase);}
    void continuePendingBuild(Budget& budget){envelopes_.continuePendingBuild(budget);}
    bool hasPendingBuild()const{return envelopes_.hasPendingBuild();}
    size_t pendingBuildVertices()const{return envelopes_.pendingBuildVertices();}
    size_t pendingBuildIndices()const{return envelopes_.pendingBuildIndices();}
    size_t readyStart(size_t count)const{return count?readyCursor_%count:0;}
    size_t heavyStart(size_t count)const{return count?heavyCursor_%count:0;}
    size_t buildStart(size_t count)const{return count?buildCursor_%count:0;}
    void readyResume(size_t index){readyCursor_=index;}
    void heavyResume(size_t index){heavyCursor_=index;}
    void buildResume(size_t index){buildCursor_=index;}
    const EnvelopeCache::Stats& envelopeStats()const{return envelopes_.stats();}
    double envelopeReadyMilliseconds()const{return envelopes_.readyMilliseconds();}
    double envelopeHeavyMilliseconds()const{return envelopes_.heavyMilliseconds();}
    double envelopeClassifyMilliseconds()const{return envelopes_.classifyMilliseconds();}
    const EnvelopeCache::Expensive& lastExpensiveEnvelope()const{return envelopes_.lastExpensive();}
    double envelopeBuildMilliseconds()const{return envelopes_.buildMilliseconds();}
    Status calculatePhase(const NorthlightActorDeformation::Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const D3DVERTEXELEMENT9* elements,size_t count,
        const float* constants,const float* inverseView,Budget& budget,Bounds& output,EnvelopeCache::Stage stage,WorkInfo* info=nullptr){
        output={};if(info)*info={};
        // All immutable packets, including tiny rigid pieces, use source bounds.
        // Unshared/transient packets remain uncullable: no additional expensive
        // exact-cache lookup or full-vertex scan outside the three time budgets.
        if(!owner||owner.get()!=&mesh){if(info)info->kind=WorkKind::Unsupported;return Status::Unsupported;}
        const auto status=envelopes_.calculate(program,mesh,owner,elements,count,constants,inverseView,budget,output,stage,info);
        if(status==Status::Valid)++envelopeValid_;else if(status==Status::Budget)++deferred_;return status;
    }
    Status calculatePreparedPhase(const Prepared& prepared,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const float* constants,const float* inverseView,
        Budget& budget,Bounds& output,EnvelopeCache::Stage stage,WorkInfo* info=nullptr){
        const auto status=envelopes_.calculatePrepared(prepared,mesh,owner,constants,inverseView,budget,output,stage,info);
        if(status==Status::Valid)++envelopeValid_;else if(status==Status::Budget)++deferred_;return status;
    }
    Status calculateCheap(const Prepared& prepared,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const float* constants,const float* inverseView,
        Budget& budget,Bounds& output,WorkInfo& info){
        return calculatePreparedPhase(prepared,mesh,owner,constants,inverseView,budget,output,EnvelopeCache::Stage::Cheap,&info);
    }
    Status evaluateHeavy(const Prepared& prepared,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const float* constants,const float* inverseView,
        Budget& budget,Bounds& output,WorkInfo& info){
        return calculatePreparedPhase(prepared,mesh,owner,constants,inverseView,budget,output,EnvelopeCache::Stage::Heavy,&info);
    }
    Status buildEnclosed(const Prepared& prepared,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const float* constants,const float* inverseView,
        Budget& budget,Bounds& output){
        return calculatePreparedPhase(prepared,mesh,owner,constants,inverseView,budget,output,EnvelopeCache::Stage::Build);
    }
    Status calculateCheap(const NorthlightActorDeformation::Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const D3DVERTEXELEMENT9* elements,size_t count,
        const float* constants,const float* inverseView,Budget& budget,Bounds& output,WorkInfo& info){
        return calculatePhase(program,mesh,owner,elements,count,constants,inverseView,budget,output,EnvelopeCache::Stage::Cheap,&info);
    }
    Status evaluateHeavy(const NorthlightActorDeformation::Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const D3DVERTEXELEMENT9* elements,size_t count,
        const float* constants,const float* inverseView,Budget& budget,Bounds& output,WorkInfo& info){
        return calculatePhase(program,mesh,owner,elements,count,constants,inverseView,budget,output,EnvelopeCache::Stage::Heavy,&info);
    }
    Status calculateReady(const NorthlightActorDeformation::Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const D3DVERTEXELEMENT9* elements,size_t count,
        const float* constants,const float* inverseView,Budget& budget,Bounds& output){
        return calculatePhase(program,mesh,owner,elements,count,constants,inverseView,budget,output,EnvelopeCache::Stage::Ready);
    }
    Status buildEnclosed(const NorthlightActorDeformation::Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const D3DVERTEXELEMENT9* elements,size_t count,
        const float* constants,const float* inverseView,Budget& budget,Bounds& output){
        return calculatePhase(program,mesh,owner,elements,count,constants,inverseView,budget,output,EnvelopeCache::Stage::Build);
    }
    Status calculateEnclosed(const NorthlightActorDeformation::Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const D3DVERTEXELEMENT9* elements,size_t count,
        const float* constants,const float* inverseView,Budget& budget,Bounds& output){
        if(owner&&owner.get()==&mesh&&mesh.vertexCount>=64){
            const auto status=envelopes_.calculate(program,mesh,owner,elements,count,constants,inverseView,budget,output);
            if(status!=Status::Unsupported){if(status==Status::Valid)++envelopeValid_;else if(status==Status::Budget)++deferred_;return status;}
        }
        const auto status=calculate(program,mesh,owner,elements,count,constants,inverseView,budget,output);
        if(status==Status::Valid)++fallbackValid_;else if(status==Status::Budget)++deferred_;return status;
    }
    bool canLookup()const{return lookupCalls_<128&&lookupNanoseconds_<250000;}
    double lookupMilliseconds()const{return double(lookupNanoseconds_)/1000000;}
    size_t bytes()const{return bytes_;}
    size_t entries()const{return entries_.size();}
    std::uint64_t hits()const{return hits_;}
    std::uint64_t misses()const{return misses_;}
    std::uint64_t avoidedVertices()const{return avoidedVertices_;}
    Status calculate(const NorthlightActorDeformation::Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
        const std::shared_ptr<const NorthlightDrawSnapshot::Mesh>& owner,const D3DVERTEXELEMENT9* elements,size_t count,
        const float* constants,const float* inverseView,Budget& budget,Bounds& output){
        output={};
        if(!owner||owner.get()!=&mesh||!canLookup())return NorthlightReplayBounds::calculate(program,mesh,elements,count,constants,inverseView,budget,output);
        const auto lookupStart=std::chrono::steady_clock::now();++lookupCalls_;bool accounted=false;
        const auto accountLookup=[&](){if(!accounted){lookupNanoseconds_+=std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-lookupStart).count());accounted=true;}};
        try {
            if(!key(program,elements,count,constants,inverseView)){accountLookup();return NorthlightReplayBounds::calculate(program,mesh,elements,count,constants,inverseView,budget,output);}
            std::uint64_t h=1469598103934665603ull^reinterpret_cast<std::uintptr_t>(owner.get());for(auto b:scratch_){h^=b;h*=1099511628211ull;}
            auto range=entries_.equal_range(h);for(auto it=range.first;it!=range.second;++it){auto& e=it->second;
                if(e.owner.lock()==owner&&e.key==scratch_){output=e.bounds;e.touched=++clock_;++hits_;avoidedVertices_+=mesh.vertexCount;accountLookup();return Status::Valid;}}
            accountLookup();++misses_;
            const Status status=NorthlightReplayBounds::calculate(program,mesh,elements,count,constants,inverseView,budget,output);
            if(status!=Status::Valid)return status;
            // Optional cache failures cannot invalidate already computed bounds.
            try {const size_t estimate=sizeof(Entry)+scratch_.size()+128;if(estimate<=Limit){
                while(!entries_.empty()&&(entries_.size()>=MaxEntries||bytes_+estimate>Limit))evict();
                Entry next;next.owner=owner;next.key=scratch_;next.bounds=output;next.touched=++clock_;next.bytes=sizeof(Entry)+next.key.capacity()+128;
                const size_t bytes=next.bytes;if(bytes<=Limit){while(!entries_.empty()&&bytes_+bytes>Limit)evict();entries_.emplace(h,std::move(next));bytes_+=bytes;}
            }}catch(...){}
            return status;
        }catch(...){accountLookup();return NorthlightReplayBounds::calculate(program,mesh,elements,count,constants,inverseView,budget,output);}
    }
};

inline bool outsideSphere(const Bounds& b,const float* center,float radius){
    if(!b.valid||!center||!std::isfinite(radius)||radius<0)return false;double distance=0;
    for(unsigned k=0;k<3;++k){if(!std::isfinite(center[k])||!std::isfinite(b.low[k])||!std::isfinite(b.high[k])||b.low[k]>b.high[k])return false;
        double delta=std::max({double(b.low[k])-center[k],double(center[k])-b.high[k],0.});distance+=delta*delta;}
    return distance>double(radius)*radius;
}
} // namespace NorthlightReplayBounds
