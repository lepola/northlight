#pragma once
#include "replay_bounds.h"
#include "water_shader_patch.h"
#include "position_bounds_scan.h"

// Stock rigid or uniform single-bone M2: native clip.w is DP4(c5,view).
// Mixed bones, modified projection and unknown source/layout keep capture.
namespace NorthlightModelNormalRange {
enum Kind {None,Rigid,Single,Envelope};
inline Kind classify(const uint32_t* code,size_t count,bool allowEnvelope=false){
  try{
    using namespace NorthlightWaterShaderPatch;
    Analysis a;NorthlightActorDeformation::Program p;
    if(analyse(code,count,a)||!a.vertex||a.writes!=4||!NorthlightActorDeformation::compile(code,count,p,false,2)||
       p.positionRegister>1)return None;
    const bool single=p.kernel.singleSkin();
    using K=NorthlightActorDeformation::Kernel;
    const bool envelope=allowEnvelope&&p.kernel.evaluator==K::WeightedR0;
    if(!envelope&&p.positionRegister!=0)return None;
    if(!single&&!envelope&&(p.skinned||p.operations.size()!=4||p.inputs.size()!=1||p.inputs[0].reg||p.inputs[0].usage||p.inputs[0].index))return None;
    bool one=false;for(size_t at=1;at<count;){const unsigned op=code[at]&65535,n=op==65534?(code[at]>>16)&32767:(code[at]>>24)&15;
        if(op==65535)break;if(op==81){if(regIndex(code[at+1])!=0||code[at+2]!=((single||envelope)?0x40400000u:0x3f800000u)||((single||envelope)&&code[at+3]!=0x3f800000u))return None;one=true;}at+=n+1;}
    if(!one)return None;if(envelope)return Envelope;if(single)return Single;unsigned written=0;
    for(const auto& op:p.operations){unsigned bit=0;
        if(op.code==1&&op.destination==0x80080000u&&op.source[0].token==0xa0000000u)bit=8;
        for(unsigned axis=0;axis<3;++axis)if(op.code==9&&op.destination==(0x80000000u|(1u<<(16+axis)))&&
            op.source[0].token==0xa0e4001fu+axis&&op.source[1].token==0x90e40000u)bit=1u<<axis;
        if(!bit||(written&bit)||op.source[0].address||op.source[1].address||op.source[2].token||op.source[2].address)return None;
        if(bit==8&&(op.source[1].token||op.source[1].address))return None;written|=bit;
    }return written==15?Rigid:None;
  }catch(const std::bad_alloc&){return None;}
}
inline bool eligible(const uint32_t* code,size_t count){return classify(code,count)!=None;}
class Cache {
    using Mesh=NorthlightDrawSnapshot::Mesh;
    struct Entry {std::weak_ptr<const Mesh> owner;const Mesh* address=nullptr;UINT offset=0,indexOffset=~0u;unsigned bone=0;bool mixed=false;uint64_t used=0;
        NorthlightReplayBounds::detail::Value position[3];double magnitude[3]={};};
    Entry entries[512];uint64_t clock=0;unsigned scanVertices=0;double workMilliseconds=0;
public:
    unsigned tested=0,culled=0,singleTests=0,singleUniform=0,singleMixed=0,deferred=0;
    // Includes retained weak control blocks after a source payload dies.
    static_assert(sizeof(entries)+512*(sizeof(NorthlightSourceIdentity::Retained<Mesh>)+32)<(1u<<20),"Bounded rigid proof storage");
    void beginFrame(){scanVertices=tested=culled=singleTests=singleUniform=singleMixed=deferred=0;workMilliseconds=0;}
    void clear(){for(auto& e:entries)e={};clock=0;beginFrame();}
    bool outside(const std::shared_ptr<const Mesh>& mesh,const D3DVERTEXELEMENT9* layout,UINT count,const float* bank,Kind kind=Rigid){
        return bank&&outsideRead(mesh,layout,count,[&](unsigned first,unsigned n,float* out){std::memcpy(out,bank+first*4,n*16);return true;},kind);
    }
    // Only projection c2..c5 and the selected bone's three affine rows enter
    // this proof. A failed read declines it; accepted draws still capture all
    // shader inputs separately for replay.
    template<class Read> bool outsideRead(const std::shared_ptr<const Mesh>& mesh,const D3DVERTEXELEMENT9* layout,UINT count,Read read,Kind kind=Rigid){
        ++tested;singleTests+=kind==Single;if(!mesh||!layout||!count||count>MAXD3DDECLLENGTH||(kind!=Rigid&&kind!=Single))return false;
        if(workMilliseconds>=.25){++deferred;return false;}
        const D3DVERTEXELEMENT9* position=nullptr;for(UINT i=0;i<count;++i)if(layout[i].Stream!=0xff&&layout[i].Usage==D3DDECLUSAGE_POSITION&&!layout[i].UsageIndex){if(position)return false;position=layout+i;}
        if(!position||position->Stream||position->Type!=D3DDECLTYPE_FLOAT3||position->Method!=D3DDECLMETHOD_DEFAULT)return false;
        const auto& stream=mesh->streams[0];if(!mesh->vertexCount||!stream.stride||uint64_t(mesh->vertexCount-1)*stream.stride+position->Offset+12>stream.bytes.size())return false;
        UINT indexOffset=~0u;unsigned bone=0;
        if(kind==Single){const D3DVERTEXELEMENT9* indices=nullptr;
            for(UINT i=0;i<count;++i)if(layout[i].Stream!=0xff&&layout[i].Usage==D3DDECLUSAGE_BLENDINDICES&&!layout[i].UsageIndex){if(indices)return false;indices=layout+i;}
            if(!indices||indices->Stream||indices->Type!=D3DDECLTYPE_UBYTE4||indices->Method!=D3DDECLMETHOD_DEFAULT||uint64_t(mesh->vertexCount-1)*stream.stride+indices->Offset+4>stream.bytes.size())return false;
            indexOffset=indices->Offset;bone=stream.bytes[indexOffset];if(bone>74)return false;
        }
        const auto pointer=reinterpret_cast<uintptr_t>(mesh.get());const unsigned first=unsigned((pointer>>4)^(pointer>>13)^position->Offset)&127u;
        Entry* entry=nullptr;Entry* replacement=entries+first*4;
        for(unsigned i=0;i<4;++i){auto& e=entries[first*4+i];
            if(e.address==mesh.get()&&e.offset==position->Offset&&e.indexOffset==indexOffset&&NorthlightSourceIdentity::sameOwner(e.owner,mesh)){entry=&e;break;}
            if(e.used<replacement->used)replacement=&e;}
        // A proven mixed source cannot use this uniform-bone rejection, whatever
        // the current pose/projection. No device reads or timing are needed.
        if(entry&&entry->mixed){entry->used=++clock;++singleMixed;return false;}
        float projection[16],matrix[12];if(!read(2,4,projection))return false;
        if(projection[12]!=0||projection[13]!=0||projection[15]!=0||(projection[14]!=1&&projection[14]!=-1))return false;
        // Near origins decline this optional proof, even if local vertices are far.
        if(!read(31+3*bone,3,matrix)||!(projection[14]*matrix[11]>48.01f))return false;
        const auto start=std::chrono::steady_clock::now();
        struct Timer {double& total;std::chrono::steady_clock::time_point start;~Timer(){total+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();}} timer{workMilliseconds,start};
        if(!entry){if(mesh->vertexCount>2048-scanVertices){++deferred;return false;}scanVertices+=mesh->vertexCount;
            bool mixed=false;if(kind==Single)for(unsigned i=1;i<mesh->vertexCount;++i)if(stream.bytes[size_t(i)*stream.stride+indexOffset]!=bone){mixed=true;break;}
            if(mixed){entry=replacement;*entry={};entry->owner=mesh;entry->address=mesh.get();entry->offset=position->Offset;entry->indexOffset=indexOffset;entry->mixed=true;}
            else {
            float low[3],high[3];if(!NorthlightPositionBounds::scanPositions(stream.bytes.data()+position->Offset,mesh->vertexCount,stream.stride,low,high))return false;
            entry=replacement;entry->owner=mesh;entry->address=mesh.get();entry->offset=position->Offset;entry->indexOffset=indexOffset;entry->bone=bone;entry->mixed=false;
            // Source bounds are immutable; prepare their exact interval inputs
            // once. Pose rows, overflow checks and DP4 still run on every draw.
            for(unsigned axis=0;axis<3;++axis){const double lo=low[axis],hi=high[axis];const float mid=float((lo+hi)*.5);
                entry->position[axis]={mid,std::max(double(mid)-lo,hi-double(mid))};entry->magnitude[axis]=std::max(std::fabs(lo),std::fabs(hi));}}}
        entry->used=++clock;
        if(entry->mixed){++singleMixed;return false;}singleUniform+=kind==Single;
        // c5 multiplies all four view lanes, including its zero coefficients.
        // Do not infer clip.w from Z when another lane could be NaN/overflow.
        if(!NorthlightFloatChecks::bounded<12>(matrix))return false;
        for(unsigned row=0;row<3;++row){double scale=0;
            for(unsigned axis=0;axis<4;++axis){const float value=matrix[4*row+axis];
                scale+=std::fabs(double(value))*(axis==3?1:entry->magnitude[axis]);}
            if(!std::isfinite(scale)||scale>1000000)return false;}
        if(!NorthlightFloatChecks::bounded<16,0x49742400u>(projection))return false; // 1,000,000.f
        // The same interval DP4 operations/error allowance as replay_bounds.h.
        using namespace NorthlightReplayBounds::detail;
        Value sum;double magnitude=0;
        for(unsigned axis=0;axis<4;++axis){const Value input=axis<3?entry->position[axis]:Value{1,0};
            const float coefficient=matrix[8+axis];if(!std::isfinite(coefficient))return false;
            const auto term=mul({coefficient,0},input);magnitude+=std::fabs(term.value)+term.error;sum=add(sum,term);}
        sum.error+=roundError(magnitude)*8;
        if(!std::isfinite(sum.value)||!std::isfinite(sum.error))return false;
        const double lower=projection[14]>0?double(sum.value)-sum.error:-double(sum.value)-sum.error;
        const bool reject=std::nextafter(lower,-INFINITY)>48.01;culled+=reject;return reject;
    }
};
}
