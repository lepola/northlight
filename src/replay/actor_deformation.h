#pragma once
#include "draw_snapshot.h"
#include "patch_shadow_shader.h"
#include <array>
#include <cmath>

// Audited kind-2 world shaders only. Compile the original straight-line vertex
// program through its preprojection position, retaining bone-palette addressing.
// No model-space approximation: output is the same deformed position fed to the
// game's c2..c5 projection. Caller must signature-gate the original shader and
// verify that projection belongs to the current main-world camera.
namespace NorthlightActorDeformation {
using Word=std::uint32_t;
using Four=std::array<float,4>;
struct Position {float x,y,z;};
struct Source {Word token=0,address=0;};
struct Operation {unsigned code=0;Word destination=0;Source source[3];};
struct Input {unsigned reg=0,usage=0,index=0;};
struct Definition {unsigned reg=0;Four value{};};
// Program::operations decoded once (see evaluateFast): per source its register
// kind, index, relative channel, swizzle, modifier and, for a fixed constant, the
// definition that overrides it; per operation its operand count and target.
struct Kernel {
    struct Src {std::uint8_t kind=0,relative=0,channel=0,modifier=0,swizzle[4]={};std::uint16_t index=0;std::int16_t definition=-1;};
    struct Op {unsigned code=0;int operands=0;std::uint8_t target=0,mask=0;bool address=false,saturate=false;Src source[3];};
    std::vector<Op> ops;std::vector<std::uint8_t> cleared;bool valid=false,skin=false; /* cleared: temps ever read (zero at entry, as evaluate); skin: the audited four-bone template */
};
struct Program {
    unsigned major=0,positionRegister=0;bool textureCoordinates=false,skinned=false;std::uint8_t outputMask=0;
    int paletteBase=-1; /* lowest constant register addressed through a0 (the bone palette) */
    std::vector<Operation> operations;
    std::vector<Input> inputs;
    std::vector<Definition> definitions;
    Kernel kernel; /* built by slice(); empty for hand-made programs (evaluate) */
};
inline void buildKernel(Program& p){
    using namespace NorthlightShadowShader;Kernel k;bool read[33]={};read[p.positionRegister<33?p.positionRegister:0]=true;
    for(const auto& o:p.operations){Kernel::Op op;op.code=o.code;op.operands=arithmeticOperands(o.code,p.major)-1;
        const unsigned type=regType(o.destination),index=regIndex(o.destination);op.address=type==3;if(!op.address&&index>=33)return;
        op.target=std::uint8_t(index);op.mask=std::uint8_t((o.destination>>16)&15);op.saturate=(o.destination&(1u<<20))!=0;
        for(unsigned i=0;i<3;++i){const Word t=o.source[i].token;auto& s=op.source[i];s.kind=std::uint8_t(regType(t));s.index=std::uint16_t(regIndex(t));
            s.relative=(t&0x2000)!=0;s.channel=std::uint8_t(p.major>=2?((o.source[i].address>>16)&3):0);s.modifier=std::uint8_t((t>>24)&15);
            for(unsigned j=0;j<4;++j)s.swizzle[j]=std::uint8_t((t>>(16+2*j))&3);
            if(int(i)<op.operands){if(s.kind>3)return;if(s.kind==0){if(s.index>=33)return;read[s.index]=true;}
                if(s.kind==2&&!s.relative)for(std::size_t d=0;d<p.definitions.size();++d)if(p.definitions[d].reg==s.index)s.definition=std::int16_t(d);}}
        k.ops.push_back(op);}
    for(unsigned r=0;r<33;++r)if(read[r])k.cleared.push_back(std::uint8_t(r));
    // The audited four-bone skinning template (NorthlightReplayBounds::SkinEnvelope::supports):
    // c0 = (3,1,..) defined, a0 = round(c0.x*v2), three rows of mul/mad x3 from
    // c[31+row+a0] and dp4 with v0 into r1.xyz, r1.w = c0.y.
    k.skin=[&]{if((p.major!=2&&p.major!=3)||p.positionRegister!=1||p.textureCoordinates||p.operations.size()!=18||p.inputs.size()!=3)return false;
        bool inputs[3]={};for(const auto& i:p.inputs){if(i.reg>=3||i.usage!=i.reg||i.index)return false;inputs[i.reg]=true;}if(!inputs[0]||!inputs[1]||!inputs[2])return false;
        bool def=false;for(const auto& d:p.definitions)if(d.reg==0){if(d.value[0]!=3||d.value[1]!=1)return false;def=true;}if(!def)return false;
        std::size_t at=0;
        const auto match=[&](unsigned code,std::uint32_t dst,std::uint32_t a,std::uint32_t b=0,std::uint32_t c=0,std::uint32_t aa=0,std::uint32_t ba=0){
            if(at>=p.operations.size())return false;const auto& o=p.operations[at++];
            return o.code==code&&o.destination==dst&&o.source[0].token==a&&o.source[0].address==aa&&o.source[1].token==b&&o.source[1].address==ba&&o.source[2].token==c&&o.source[2].address==0;};
        if(!match(5,0x800f0000,0xa0000000,0x90e40002)||!match(46,0xb00f0000,0x80e40000))return false;
        for(unsigned row=0;row<3;++row){const unsigned r=row?2:0;const std::uint32_t dst=0x800f0000|r,src=0x80e40000|r,matrix=0xa0e4201f+row;
            if(!match(5,dst,0x90550001,matrix,0,0,0xb0550000)||!match(4,dst,matrix,0x90000001,src,0xb0000000)||
               !match(4,dst,matrix,0x90aa0001,src,0xb0aa0000)||!match(4,dst,matrix,0x90ff0001,src,0xb0ff0000)||
               !match(9,0x80000001|(1u<<(16+row)),src,0x90e40000))return false;}
        return match(1,0x80080001,0xa0550000)&&at==p.operations.size();}();
    k.valid=p.positionRegister<33;p.kernel=std::move(k);
}
inline void slice(Program& p){
    using namespace NorthlightShadowShader;unsigned needed[34]={};needed[p.positionRegister]=p.textureCoordinates?(p.outputMask?p.outputMask:3):15;
    std::vector<Operation> reverse;
    for(auto it=p.operations.rbegin();it!=p.operations.rend();++it){auto& op=*it;unsigned destination=regType(op.destination)==3?33:regIndex(op.destination),written=(op.destination>>16)&15,observed=needed[destination]&written;
        if(!observed)continue;needed[destination]&=~written;reverse.push_back(op);unsigned channels=observed;
        if(op.code==8||op.code==36||op.code==33)channels=7;else if(op.code==9)channels=15;else if(op.code==6||op.code==7||op.code==14||op.code==15||op.code==32)channels=1;else if(op.code==16||op.code==17)channels=15;
        for(int i=0;i<NorthlightShadowShader::arithmeticOperands(op.code,p.major)-1;++i){auto source=op.source[i];unsigned type=regType(source.token),read=0;
            for(unsigned c=0;c<4;++c)if(channels&(1u<<c))read|=1u<<((source.token>>(16+2*c))&3);
            if(type==0)needed[regIndex(source.token)]|=read;else if(type==3)needed[33]|=read;
            if(source.token&0x2000)needed[33]|=1u<<(p.major>=2?((source.address>>16)&3):0);}}
    p.operations.assign(reverse.rbegin(),reverse.rend());bool used[16]={};
    for(auto& op:p.operations)for(auto& source:op.source)if(source.token&0x2000)p.skinned=true;
    for(auto& op:p.operations)for(auto& source:op.source)if((source.token&0x2000)&&regType(source.token)==2&&(p.paletteBase<0||int(regIndex(source.token))<p.paletteBase))p.paletteBase=int(regIndex(source.token));
    for(auto& op:p.operations)for(auto& source:op.source)if(regType(source.token)==1)used[regIndex(source.token)]=true;
    std::vector<Input> inputs;for(auto input:p.inputs)if(used[input.reg])inputs.push_back(input);p.inputs=std::move(inputs);
    buildKernel(p);
}
inline bool compile(const Word* words,std::size_t count,Program& output,bool textureCoordinates=false,unsigned projectionKind=2,unsigned outputUsage=5,unsigned outputIndex=0,unsigned requiredMask=0){
    using namespace NorthlightShadowShader;output={};
    if(!words||count<2||count>65536||(projectionKind!=1&&projectionKind!=2)||(textureCoordinates&&outputUsage!=5&&outputUsage!=10))return false;
    Program p;p.textureCoordinates=textureCoordinates;p.outputMask=textureCoordinates?(requiredMask?requiredMask:outputUsage==10?15:3):0;p.major=(words[0]>>8)&255;
    if(words[0]!=(p.major==1?0xfffe0101u:p.major==2?0xfffe0200u:0xfffe0300u)||p.major<1||p.major>3)return false;
    unsigned positionType=p.major<3?4:99,positionIndex=0,uvType=p.major<3&&outputUsage==10?5:6,uvIndex=p.major<3?outputIndex:99,uvWritten=0;bool inputUsed[16]={};
    auto finish=[&]()->bool{std::vector<Input> needed;for(auto i:p.inputs)if(inputUsed[i.reg]){needed.push_back(i);inputUsed[i.reg]=false;}
        for(bool missing:inputUsed)if(missing)return false;p.inputs=std::move(needed);slice(p);output=std::move(p);return true;};
    for(std::size_t at=1;at<count;){
        Word token=words[at];unsigned op=token&65535;
        if(op==65535){if(!textureCoordinates||(uvWritten&p.outputMask)!=p.outputMask||at+1!=count)return false;p.positionRegister=32;return finish();}
        if(op==65534){std::size_t n=(token>>16)&32767;if(n>count-at-1)return false;at+=n+1;continue;}
        if(token&0xf0000000u)return false;
        int formal=op==31?2:op==81?5:arithmeticOperands(op,p.major);
        if(formal<0)return false;
        unsigned n=p.major==1?unsigned(formal):(token>>24)&15;
        if(n>count-at-1)return false;
        const Word* a=words+at+1;
        if(op==31){if(n!=2)return false;unsigned type=regType(a[1]);
            if(type==1){if(regIndex(a[1])>=16)return false;p.inputs.push_back({regIndex(a[1]),a[0]&15,(a[0]>>16)&15});}
            else if(type==6&&(a[0]&15)==0&&((a[0]>>16)&15)==0){positionType=6;positionIndex=regIndex(a[1]);}
            else if(type==6&&(a[0]&15)==outputUsage&&((a[0]>>16)&15)==outputIndex)uvIndex=regIndex(a[1]);
        }else if(op==81){if(n!=5||regType(a[0])!=2||regIndex(a[0])>=256)return false;
            Definition d;d.reg=regIndex(a[0]);std::memcpy(d.value.data(),a+1,16);p.definitions.push_back(d);
        }else{
            // DetailDoodad uses the audited column projection, after its
            // affine model-view prefix. Keep that prefix for whole-mesh
            // bounds; the GPU replay still executes the original shader.
            if(!textureCoordinates&&projectionKind==1&&op==5&&n==3&&a[0]==0x800f0001u){
                const Word chain[]={0x03000005u,0x800f0001u,0xa0e40005u,0x80550000u,
                    0x04000004u,0x800f0001u,0x80000000u,0xa0e40004u,0x80e40001u,
                    0x04000004u,0x800f0001u,0x80aa0000u,0xa0e40006u,0x80e40001u,
                    0x04000004u,replaceReg(0x800f0000u,positionType,positionIndex),0x80ff0000u,0xa0e40007u,0x80e40001u};
                if(p.major<2||count-at<sizeof(chain)/sizeof(*chain))return false;
                // The first MUL is commutative in the stock VS2 permutation.
                if(words[at+2]!=chain[2]||words[at+3]!=chain[3]){
                    if(words[at+2]!=chain[3]||words[at+3]!=chain[2])return false;
                }
                for(unsigned k=0;k<sizeof(chain)/sizeof(*chain);++k)if(k!=2&&k!=3&&words[at+k]!=chain[k])return false;
                for(const auto& definition:p.definitions)if(definition.reg>=4&&definition.reg<=7)return false;
                p.positionRegister=0;return finish();
            }
            // The four original projection DP4 instructions must be consecutive
            // and read the same unmodified temp; stop before applying projection.
            if(!textureCoordinates&&projectionKind==2&&op==9&&n==3&&regType(a[0])==positionType&&regIndex(a[0])==positionIndex){
                if(regType(a[2])!=0||regIndex(a[2])>=32||(a[2]&0x0fff2000u)!=0x00e40000u)return false;
                std::size_t q=at;
                for(unsigned component=0;component<4;++component){
                    if(q+4>count||words[q]!=(p.major==1?9u:0x03000009u))return false;
                    if(words[q+1]!=replaceReg(0x80000000u|(1u<<(16+component)),positionType,positionIndex)||words[q+2]!=0xa0e40002u+component||words[q+3]!=a[2])return false;
                    q+=4;
                }
                p.positionRegister=regIndex(a[2]);
                return finish();
            }
            if(op!=1&&op!=2&&op!=3&&op!=4&&op!=5&&op!=6&&op!=7&&op!=8&&op!=9&&op!=10&&op!=11&&op!=12&&op!=13&&op!=14&&op!=15&&op!=16&&op!=17&&op!=18&&op!=19&&op!=32&&op!=33&&op!=35&&op!=36&&op!=46)return false;
            Operation o;o.code=op;o.destination=a[0];unsigned dt=regType(a[0]),di=regIndex(a[0]);
            bool discardedOutput=dt>=4&&dt<=6;
            if((dt!=0&&dt!=3&&!discardedOutput)||(dt==0&&di>=32)||(dt==3&&di!=0)||(a[0]&0x0f002000u)||((a[0]>>20)&15)>1)return false;
            unsigned cursor=1;
            for(int j=1;j<formal;++j){if(cursor>=n)return false;Word t=a[cursor++];unsigned type=regType(t),index=regIndex(t),mod=(t>>24)&15;
                if(!(t&0x80000000u)||(mod!=0&&mod!=1&&mod!=11&&mod!=12)||(type!=0&&type!=1&&type!=2&&type!=3)||(type==0&&index>=32)||(type==1&&index>=16)||(type==2&&index>=256)||(type==3&&index))return false;
                if(type==1)inputUsed[index]=true;o.source[j-1].token=t;
                if(t&0x2000){if(type!=2)return false;if(p.major>=2){if(cursor>=n)return false;Word r=a[cursor++];if(regType(r)!=3||regIndex(r)!=0||(r&0x2000))return false;o.source[j-1].address=r;}}
            }
            if(cursor!=n)return false;
            if(textureCoordinates&&dt==uvType&&di==uvIndex){uvWritten|=(o.destination>>16)&15;o.destination=replaceReg(o.destination,0,32);discardedOutput=false;}
            if(!discardedOutput)p.operations.push_back(o);if(p.operations.size()>512)return false;
        }
        at+=n+1;
    }return false;
}
inline float half(std::uint16_t bits){unsigned sign=bits>>15,exponent=(bits>>10)&31,mantissa=bits&1023;float v=exponent==0?std::ldexp(float(mantissa),-24):exponent==31?(mantissa?std::numeric_limits<float>::quiet_NaN():std::numeric_limits<float>::infinity()):std::ldexp(float(mantissa+1024),int(exponent)-25);return sign?-v:v;}
inline bool decodeElement(const std::uint8_t* bytes,unsigned type,Four& v){
    v={0,0,0,1};if(type<=3){std::memcpy(v.data(),bytes,(type+1)*4);return true;}
    if(type==4){v={bytes[2]/255.f,bytes[1]/255.f,bytes[0]/255.f,bytes[3]/255.f};return true;}
    if(type==5||type==8){for(unsigned j=0;j<4;++j)v[j]=bytes[j]*(type==8?1.f/255.f:1.f);return true;}
    if(type==6||type==7||type==9||type==10){unsigned n=(type==6||type==9)?2:4;for(unsigned j=0;j<n;++j){std::int16_t x;std::memcpy(&x,bytes+j*2,2);v[j]=type>=9?std::max(-1.f,x/32767.f):float(x);}return true;}
    if(type==11||type==12){for(unsigned j=0;j<(type==11?2u:4u);++j){std::uint16_t x;std::memcpy(&x,bytes+j*2,2);v[j]=x/65535.f;}return true;}
    if(type==13||type==14){std::uint32_t x;std::memcpy(&x,bytes,4);for(unsigned j=0;j<3;++j){unsigned u=(x>>(10*j))&1023;int s=u>=512?int(u)-1024:int(u);v[j]=type==13?float(u):std::max(-1.f,s/511.f);}return true;}
    if(type==15||type==16){for(unsigned j=0;j<(type==15?2u:4u);++j){std::uint16_t x;std::memcpy(&x,bytes+j*2,2);v[j]=half(x);}return true;}return false;
}
// No fused multiply-add in the evaluators (the x86 game build has none), so the
// written-out and decoded evaluators match evaluate() bit for bit on any target.
#ifdef __clang__
#define NO_FUSED_MULTIPLY_ADD _Pragma("clang fp contract(off)")
#else
#define NO_FUSED_MULTIPLY_ADD
#endif
template<bool Strict=false> inline bool evaluate(const Program& p,const Four inputs[16],const float* constants,Four& viewPosition){
    NO_FUSED_MULTIPLY_ADD
    using namespace NorthlightShadowShader;Four temp[33]={},address={};
    auto source=[&](const Source& s,Four& result)->bool{
        Word t=s.token;unsigned type=regType(t);int index=int(regIndex(t));Four value={};
        if(t&0x2000){unsigned channel=p.major>=2?((s.address>>16)&3):0;float a=address[channel];if(!std::isfinite(a)||a<-4096||a>4096)return false;index+=int(a);}
        if(type==0)value=temp[index];else if(type==1)value=inputs[index];else if(type==3)value=address;
        else{if(index<0||index>=256)return false;std::memcpy(value.data(),constants+4*index,16);for(auto& d:p.definitions)if(d.reg==unsigned(index))value=d.value;}
        for(unsigned j=0;j<4;++j){result[j]=value[(t>>(16+2*j))&3];unsigned modifier=(t>>24)&15;if(modifier==11||modifier==12)result[j]=std::fabs(result[j]);if(modifier==1||modifier==12)result[j]=-result[j];if constexpr(Strict)if(!std::isfinite(result[j]))return false;}return true;
    };
    for(auto& o:p.operations){Four a={},b={},c={},result={};if(!source(o.source[0],a))return false;
        int n=arithmeticOperands(o.code,p.major)-1;if(n>1&&!source(o.source[1],b))return false;if(n>2&&!source(o.source[2],c))return false;
        if(o.code==8||o.code==9){float sum=0;for(unsigned j=0;j<(o.code==8?3u:4u);++j)sum+=a[j]*b[j];result.fill(sum);}
        else if(o.code==6){result.fill(1/a[0]);}
        else if(o.code==7){float s=1/std::sqrt(std::fabs(a[0]));result.fill(s);}
        else if(o.code==36){float s=1/std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);for(unsigned j=0;j<4;++j)result[j]=a[j]*s;}
        else if(o.code==14){result.fill(std::exp2(a[0]));}
        else if(o.code==15){result.fill(std::log2(std::fabs(a[0])));}
        else if(o.code==32){result.fill(std::pow(std::fabs(a[0]),b[0]));}
        else if(o.code==16){result={1,std::max(a[0],0.f),a[0]>0?std::pow(std::max(a[1],0.f),std::max(-128.f,std::min(128.f,a[3]))):0,1};}
        else if(o.code==17){result={1,a[1]*b[1],a[2],b[3]};}
        else if(o.code==33){result={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0],0};}
        else for(unsigned j=0;j<4;++j){switch(o.code){case 1:case 46:result[j]=a[j];break;case 2:result[j]=a[j]+b[j];break;case 3:result[j]=a[j]-b[j];break;case 12:result[j]=a[j]<b[j]?1:0;break;case 13:result[j]=a[j]>=b[j]?1:0;break;case 18:result[j]=a[j]*b[j]+(1-a[j])*c[j];break;case 19:result[j]=a[j]-std::floor(a[j]);break;case 35:result[j]=std::fabs(a[j]);break;case 4:result[j]=a[j]*b[j]+c[j];break;case 5:result[j]=a[j]*b[j];break;case 10:result[j]=std::min(a[j],b[j]);break;case 11:result[j]=std::max(a[j],b[j]);break;default:return false;}}
        unsigned mask=(o.destination>>16)&15,type=regType(o.destination);Four& target=type==3?address:temp[regIndex(o.destination)];
        for(unsigned j=0;j<4;++j)if(mask&(1u<<j)){float value=result[j];if constexpr(Strict)if(!std::isfinite(value))return false;if(o.destination&(1u<<20))value=std::max(0.f,std::min(1.f,value));
            // The client's D9VK dxso_compiler::emitMov floors VS1.1 address
            // writes, rounds VS2/3 MOV/MOVA. Nonfinite addresses fail source().
            if(type==3){if(!std::isfinite(value))return false;
                // SPIR-V Round can choose either tie direction. Real integer
                // bone indices are exact; ambiguous half-indices fail closed.
                if(p.major>=2&&std::fabs(value-std::floor(value)-.5f)<1e-6f)return false;
                value=p.major==1?std::floor(value):std::round(value);}target[j]=value;}
    }
    viewPosition=temp[p.positionRegister];for(unsigned i=0;i<(p.textureCoordinates&&p.outputMask!=15?2u:4u);++i)if(!std::isfinite(viewPosition[i]))return false;
    return p.textureCoordinates||std::fabs(viewPosition[3]-1.f)<.0001f;
}
// evaluate() on the decoded kernel: the same reads, arithmetic (same expressions,
// same order), writes and failures, so results are bit-identical; without it
// (a hand-made program) evaluate() itself.
// The skinning template written out: evaluate()'s reads, expressions and order.
inline bool evaluateSkin(const Program& p,const Four inputs[16],const float* constants,Four& viewPosition){
NO_FUSED_MULTIPLY_ADD
    auto constant=[&](int index,Four& value){if(index<0||index>=256)return false;std::memcpy(value.data(),constants+4*index,16);for(auto& d:p.definitions)if(d.reg==unsigned(index))value=d.value;return true;};
    const Four& v0=inputs[0];const Four& v1=inputs[1];const Four& v2=inputs[2];Four c0,address;constant(0,c0);
    for(unsigned j=0;j<4;++j){const float value=c0[0]*v2[j];if(!std::isfinite(value)||std::fabs(value-std::floor(value)-.5f)<1e-6f)return false;address[j]=std::round(value);}
    auto row=[&](int base,unsigned channel,Four& m){const float a=address[channel];if(!std::isfinite(a)||a<-4096||a>4096)return false;return constant(base+int(a),m);};
    for(unsigned r=0;r<3;++r){const int base=31+int(r);Four t,m;
        if(!row(base,1,m))return false;for(unsigned j=0;j<4;++j)t[j]=v1[1]*m[j];
        if(!row(base,0,m))return false;for(unsigned j=0;j<4;++j)t[j]=m[j]*v1[0]+t[j];
        if(!row(base,2,m))return false;for(unsigned j=0;j<4;++j)t[j]=m[j]*v1[2]+t[j];
        if(!row(base,3,m))return false;for(unsigned j=0;j<4;++j)t[j]=m[j]*v1[3]+t[j];
        float sum=0;for(unsigned j=0;j<4;++j)sum+=t[j]*v0[j];viewPosition[r]=sum;}
    viewPosition[3]=c0[1];for(unsigned i=0;i<4;++i)if(!std::isfinite(viewPosition[i]))return false;
    return std::fabs(viewPosition[3]-1.f)<.0001f;
}
template<bool Strict=false> inline bool evaluateFast(const Program& p,const Four inputs[16],const float* constants,Four& viewPosition){
    NO_FUSED_MULTIPLY_ADD
    const Kernel& kernel=p.kernel;if(!kernel.valid||kernel.ops.size()!=p.operations.size())return evaluate<Strict>(p,inputs,constants,viewPosition);
    if(kernel.skin)return evaluateSkin(p,inputs,constants,viewPosition);
    Four temp[33],address={};for(auto r:kernel.cleared)temp[r]={};
    auto source=[&](const Kernel::Src& s,Four& result)->bool{
        int index=s.index;Four value={};
        if(s.relative){float a=address[s.channel];if(!std::isfinite(a)||a<-4096||a>4096)return false;index+=int(a);}
        if(s.kind==0)value=temp[index];else if(s.kind==1)value=inputs[index];else if(s.kind==3)value=address;
        else{if(index<0||index>=256)return false;
            if(s.definition>=0)value=p.definitions[std::size_t(s.definition)].value;
            else{std::memcpy(value.data(),constants+4*index,16);if(s.relative)for(auto& d:p.definitions)if(d.reg==unsigned(index))value=d.value;}}
        const bool absolute=s.modifier==11||s.modifier==12,negate=s.modifier==1||s.modifier==12;
        for(unsigned j=0;j<4;++j){result[j]=value[s.swizzle[j]];if(absolute)result[j]=std::fabs(result[j]);if(negate)result[j]=-result[j];if constexpr(Strict)if(!std::isfinite(result[j]))return false;}return true;
    };
    for(const auto& o:kernel.ops){Four a={},b={},c={},result={};if(!source(o.source[0],a))return false;
        const int n=o.operands;if(n>1&&!source(o.source[1],b))return false;if(n>2&&!source(o.source[2],c))return false;
        if(o.code==8||o.code==9){float sum=0;for(unsigned j=0;j<(o.code==8?3u:4u);++j)sum+=a[j]*b[j];result.fill(sum);}
        else if(o.code==6){result.fill(1/a[0]);}
        else if(o.code==7){float s=1/std::sqrt(std::fabs(a[0]));result.fill(s);}
        else if(o.code==36){float s=1/std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);for(unsigned j=0;j<4;++j)result[j]=a[j]*s;}
        else if(o.code==14){result.fill(std::exp2(a[0]));}
        else if(o.code==15){result.fill(std::log2(std::fabs(a[0])));}
        else if(o.code==32){result.fill(std::pow(std::fabs(a[0]),b[0]));}
        else if(o.code==16){result={1,std::max(a[0],0.f),a[0]>0?std::pow(std::max(a[1],0.f),std::max(-128.f,std::min(128.f,a[3]))):0,1};}
        else if(o.code==17){result={1,a[1]*b[1],a[2],b[3]};}
        else if(o.code==33){result={a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0],0};}
        else for(unsigned j=0;j<4;++j){switch(o.code){case 1:case 46:result[j]=a[j];break;case 2:result[j]=a[j]+b[j];break;case 3:result[j]=a[j]-b[j];break;case 12:result[j]=a[j]<b[j]?1:0;break;case 13:result[j]=a[j]>=b[j]?1:0;break;case 18:result[j]=a[j]*b[j]+(1-a[j])*c[j];break;case 19:result[j]=a[j]-std::floor(a[j]);break;case 35:result[j]=std::fabs(a[j]);break;case 4:result[j]=a[j]*b[j]+c[j];break;case 5:result[j]=a[j]*b[j];break;case 10:result[j]=std::min(a[j],b[j]);break;case 11:result[j]=std::max(a[j],b[j]);break;default:return false;}}
        Four& target=o.address?address:temp[o.target];
        for(unsigned j=0;j<4;++j)if(o.mask&(1u<<j)){float value=result[j];if constexpr(Strict)if(!std::isfinite(value))return false;if(o.saturate)value=std::max(0.f,std::min(1.f,value));
            if(o.address){if(!std::isfinite(value))return false;
                if(p.major>=2&&std::fabs(value-std::floor(value)-.5f)<1e-6f)return false;
                value=p.major==1?std::floor(value):std::round(value);}target[j]=value;}
    }
    viewPosition=temp[p.positionRegister];for(unsigned i=0;i<(p.textureCoordinates&&p.outputMask!=15?2u:4u);++i)if(!std::isfinite(viewPosition[i]))return false;
    return p.textureCoordinates||std::fabs(viewPosition[3]-1.f)<.0001f;
}
// Immutable declaration bindings contain only offsets/types and input metadata.
// They never cache vertex bytes, constants, bone poses, or game/D3D pointers.
struct Binding {Input input;D3DVERTEXELEMENT9 element;};
struct PreparedBindings {
    std::vector<Binding> bindings;
    std::vector<Input> inputs;
    std::vector<D3DVERTEXELEMENT9> elements;
    std::array<UINT,4> strides{};
    bool validFor(const Program& p,const NorthlightDrawSnapshot::Mesh& mesh)const {
        if(mesh.vertexCount>262144||p.inputs.size()!=inputs.size())return false;
        for(size_t i=0;i<inputs.size();++i)if(p.inputs[i].reg!=inputs[i].reg||p.inputs[i].usage!=inputs[i].usage||p.inputs[i].index!=inputs[i].index)return false;
        for(unsigned i=0;i<4;++i)if(mesh.streams[i].stride!=strides[i])return false;
        for(const auto& b:bindings){const auto& stream=mesh.streams[b.element.Stream];if(std::uint64_t(mesh.vertexCount)*stream.stride>stream.bytes.size())return false;}
        return true;
    }
    bool declarationMatches(const D3DVERTEXELEMENT9* declaration,size_t count)const {
        if(!declaration||count!=elements.size())return false;
        for(size_t i=0;i<count;++i){const auto& a=elements[i];const auto& b=declaration[i];if(a.Stream!=b.Stream||a.Offset!=b.Offset||a.Type!=b.Type||a.Method!=b.Method||a.Usage!=b.Usage||a.UsageIndex!=b.UsageIndex)return false;}return true;
    }
};
inline bool prepareBindings(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* elements,size_t count,PreparedBindings& out){
    if(!elements||!count||count>MAXD3DDECLLENGTH+1||mesh.vertexCount>262144)return false;
    PreparedBindings prepared;prepared.inputs=program.inputs;prepared.elements.assign(elements,elements+count);for(unsigned i=0;i<4;++i)prepared.strides[i]=mesh.streams[i].stride;
    for(auto input:program.inputs){if(input.reg>=16)return false;bool found=false;for(size_t j=0;j<count;++j){auto e=elements[j];if(e.Stream==0xff)break;if(e.Usage!=input.usage||e.UsageIndex!=input.index)continue;
        if(found||e.Stream>=4||e.Method!=D3DDECLMETHOD_DEFAULT)return false;const auto& stream=mesh.streams[e.Stream];const unsigned size=NorthlightDrawSnapshot::declarationBytes(e.Type);
        if(!size||UINT(e.Offset)+size>stream.stride||std::uint64_t(mesh.vertexCount)*stream.stride>stream.bytes.size())return false;
        prepared.bindings.push_back({input,e});found=true;}if(!found)return false;}
    out=std::move(prepared);return true;
}
class PreparedBindingCache {
    struct Entry {std::shared_ptr<const PreparedBindings> prepared;uint64_t key=0,touched=0;};
    std::array<Entry,64> entries_{};uint64_t stamp_=0;
public:
    uint64_t hits=0,preparations=0;
    std::shared_ptr<const PreparedBindings> acquire(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* elements,size_t count){
        if(!elements||!count||count>MAXD3DDECLLENGTH+1)return {};
        uint64_t key=14695981039346656037ull;auto mix=[&](uint64_t n){key=(key^n)*1099511628211ull;};
        for(const auto& input:program.inputs){mix(input.reg);mix(input.usage);mix(input.index);}mix(program.inputs.size());
        for(size_t i=0;i<count;++i){const auto& e=elements[i];mix(e.Stream);mix(e.Offset);mix(e.Type);mix(e.Method);mix(e.Usage);mix(e.UsageIndex);}mix(count);for(const auto& stream:mesh.streams)mix(stream.stride);
        ++stamp_;Entry* oldest=&entries_[0];
        for(auto& entry:entries_){if(entry.touched<oldest->touched)oldest=&entry;
            if(entry.prepared&&entry.key==key&&entry.prepared->declarationMatches(elements,count)&&entry.prepared->validFor(program,mesh)){entry.touched=stamp_;++hits;return entry.prepared;}}
        auto prepared=std::make_shared<PreparedBindings>();if(!prepareBindings(program,mesh,elements,count,*prepared))return {};
        ++preparations;*oldest={prepared,key,stamp_};return prepared;
    }
};
inline bool worldPositionsPrepared(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const PreparedBindings& prepared,const float* constants,const float* inverseView,std::vector<Position>& output){
    output.clear();if(!constants||!inverseView||!prepared.validFor(program,mesh))return false;
    const auto& bindings=prepared.bindings;
    std::vector<Position> positions;positions.reserve(mesh.vertexCount);
    for(UINT vertex=0;vertex<mesh.vertexCount;++vertex){Four inputs[16]={};for(auto& b:bindings){auto& s=mesh.streams[b.element.Stream];if(!decodeElement(s.bytes.data()+std::size_t(vertex)*s.stride+b.element.Offset,b.element.Type,inputs[b.input.reg]))return false;}
        Four v;if(!evaluate(program,inputs,constants,v))return false;Position out;
        out.x=v[0]*inverseView[0]+v[1]*inverseView[4]+v[2]*inverseView[8]+inverseView[12];
        out.y=v[0]*inverseView[1]+v[1]*inverseView[5]+v[2]*inverseView[9]+inverseView[13];
        out.z=v[0]*inverseView[2]+v[1]*inverseView[6]+v[2]*inverseView[10]+inverseView[14];
        if(!std::isfinite(out.x)||!std::isfinite(out.y)||!std::isfinite(out.z)||std::fabs(out.x)>1000000||std::fabs(out.y)>1000000||std::fabs(out.z)>1000000)return false;positions.push_back(out);}
    output=std::move(positions);return true;
}
inline bool textureUVsPrepared(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const PreparedBindings& prepared,const float* constants,std::vector<std::array<float,2>>& output){
    output.clear();if(!program.textureCoordinates||!constants||!prepared.validFor(program,mesh))return false;
    const auto& bindings=prepared.bindings;
    std::vector<std::array<float,2>> positions;positions.reserve(mesh.vertexCount);
    for(UINT vertex=0;vertex<mesh.vertexCount;++vertex){Four inputs[16]={};for(auto& b:bindings){auto& s=mesh.streams[b.element.Stream];if(!decodeElement(s.bytes.data()+std::size_t(vertex)*s.stride+b.element.Offset,b.element.Type,inputs[b.input.reg]))return false;}
        Four v;if(!evaluate(program,inputs,constants,v))return false;positions.push_back({v[0],v[1]});}
    output=std::move(positions);return true;
}
// Original transient-call path: avoid adding cache metadata copies for callers
// that do not reuse prepared bindings. Math and validation remain unchanged.
inline bool worldPositions(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* elements,std::size_t elementCount,const float* constants,const float* inverseView,std::vector<Position>& output){
    output.clear();if(!constants||!inverseView||!elements||!elementCount||elementCount>MAXD3DDECLLENGTH+1||mesh.vertexCount>262144)return false;
    struct Binding{Input input;D3DVERTEXELEMENT9 element;};std::vector<Binding> bindings;
    for(auto input:program.inputs){bool found=false;for(std::size_t j=0;j<elementCount;++j){auto e=elements[j];if(e.Stream==0xff)break;if(e.Usage!=input.usage||e.UsageIndex!=input.index)continue;
        if(found||e.Stream>=4||e.Method!=D3DDECLMETHOD_DEFAULT)return false;auto& s=mesh.streams[e.Stream];unsigned size=NorthlightDrawSnapshot::declarationBytes(e.Type);
        if(!size||UINT(e.Offset)+size>s.stride||std::uint64_t(mesh.vertexCount)*s.stride>s.bytes.size())return false;bindings.push_back({input,e});found=true;}if(!found)return false;}
    std::vector<Position> positions;positions.reserve(mesh.vertexCount);
    for(UINT vertex=0;vertex<mesh.vertexCount;++vertex){Four inputs[16]={};for(auto& b:bindings){auto& s=mesh.streams[b.element.Stream];if(!decodeElement(s.bytes.data()+std::size_t(vertex)*s.stride+b.element.Offset,b.element.Type,inputs[b.input.reg]))return false;}
        Four v;if(!evaluate(program,inputs,constants,v))return false;Position out;
        out.x=v[0]*inverseView[0]+v[1]*inverseView[4]+v[2]*inverseView[8]+inverseView[12];
        out.y=v[0]*inverseView[1]+v[1]*inverseView[5]+v[2]*inverseView[9]+inverseView[13];
        out.z=v[0]*inverseView[2]+v[1]*inverseView[6]+v[2]*inverseView[10]+inverseView[14];
        if(!std::isfinite(out.x)||!std::isfinite(out.y)||!std::isfinite(out.z)||std::fabs(out.x)>1000000||std::fabs(out.y)>1000000||std::fabs(out.z)>1000000)return false;positions.push_back(out);}
    output=std::move(positions);return true;
}
inline bool textureUVs(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,const D3DVERTEXELEMENT9* elements,std::size_t elementCount,const float* constants,std::vector<std::array<float,2>>& output){
    output.clear();if(!program.textureCoordinates||!constants||!elements||!elementCount||elementCount>MAXD3DDECLLENGTH+1||mesh.vertexCount>262144)return false;
    struct Binding{Input input;D3DVERTEXELEMENT9 element;};std::vector<Binding> bindings;
    for(auto input:program.inputs){bool found=false;for(std::size_t j=0;j<elementCount;++j){auto e=elements[j];if(e.Stream==0xff)break;if(e.Usage!=input.usage||e.UsageIndex!=input.index)continue;
        if(found||e.Stream>=4||e.Method!=D3DDECLMETHOD_DEFAULT)return false;auto& s=mesh.streams[e.Stream];unsigned size=NorthlightDrawSnapshot::declarationBytes(e.Type);
        if(!size||UINT(e.Offset)+size>s.stride||std::uint64_t(mesh.vertexCount)*s.stride>s.bytes.size())return false;bindings.push_back({input,e});found=true;}if(!found)return false;}
    std::vector<std::array<float,2>> positions;positions.reserve(mesh.vertexCount);
    for(UINT vertex=0;vertex<mesh.vertexCount;++vertex){Four inputs[16]={};for(auto& b:bindings){auto& s=mesh.streams[b.element.Stream];if(!decodeElement(s.bytes.data()+std::size_t(vertex)*s.stride+b.element.Offset,b.element.Type,inputs[b.input.reg]))return false;}
        Four v;if(!evaluate(program,inputs,constants,v))return false;positions.push_back({v[0],v[1]});}
    output=std::move(positions);return true;
}
// One currently referenced vertex, used ONLY for experimental distance ranking.
// No allocations, stale poses, model-origin assumptions, or buffer readbacks.
inline bool sampledVertexInputsAt(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
    const D3DVERTEXELEMENT9* elements,size_t count,unsigned vertex,Four (&inputs)[16],bool textureInputs=false){
    if((program.textureCoordinates&&!textureInputs)||!elements||!count||count>MAXD3DDECLLENGTH+1||!mesh.vertexCount)return false;
    if(vertex>=mesh.vertexCount)return false;
    for(auto& input:inputs)input={};
    for(const auto& input:program.inputs){
        if(input.reg>=16)return false;bool found=false;
        for(size_t j=0;j<count;++j){const auto& e=elements[j];if(e.Stream==0xff)break;
            if(e.Usage!=input.usage||e.UsageIndex!=input.index)continue;
            if(found||e.Stream>=4||e.Method!=D3DDECLMETHOD_DEFAULT)return false;
            const auto& stream=mesh.streams[e.Stream];const unsigned size=NorthlightDrawSnapshot::declarationBytes(e.Type);
            const std::uint64_t offset=std::uint64_t(vertex)*stream.stride+e.Offset;
            if(!size||unsigned(e.Offset)+size>stream.stride||offset+size>stream.bytes.size()||
               !decodeElement(stream.bytes.data()+size_t(offset),e.Type,inputs[input.reg]))return false;
            found=true;
        }
        if(!found)return false;
    }
    return true;
}
// world (optional) receives the same sampled world position; squared is unchanged.
inline bool sampledVertexInputs(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
    const D3DVERTEXELEMENT9* elements,size_t count,Four (&inputs)[16]){
    if(!mesh.vertexCount)return false;
    return sampledVertexInputsAt(program,mesh,elements,count,mesh.indexed?(mesh.indices.empty()?mesh.vertexCount:mesh.indices.front()):0,inputs);
}
inline bool sampledDistanceFromInputs(const Program& program,const Four (&inputs)[16],const float* constants,
    const float* inverseView,const float* camera,float& squared,float* world=nullptr){
    if(program.textureCoordinates||!constants||!inverseView||!camera)return false;
    Four v;if(!evaluateFast(program,inputs,constants,v))return false;
    float total=0,at[3];
    for(unsigned axis=0;axis<3;++axis){
        at[axis]=v[0]*inverseView[axis]+v[1]*inverseView[4+axis]+v[2]*inverseView[8+axis]+inverseView[12+axis];
        const float delta=at[axis]-camera[axis];
        if(!std::isfinite(delta)||std::fabs(delta)>1000000)return false;
        total+=delta*delta;
    }
    if(!std::isfinite(total))return false;squared=total;if(world)std::memcpy(world,at,sizeof at);return true;
}
inline bool sampledDistanceSquared(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
    const D3DVERTEXELEMENT9* elements,size_t count,const float* constants,const float* inverseView,
    const float* camera,float& squared,float* world=nullptr){
    Four inputs[16];return sampledVertexInputs(program,mesh,elements,count,inputs)&&
        sampledDistanceFromInputs(program,inputs,constants,inverseView,camera,squared,world);
}
// World position of vertex `slot` of two extra fixed vertices (middle and last
// referenced index): pose samples for the stationary-actor test only.
inline bool sampledExtraWorld(const Program& program,const NorthlightDrawSnapshot::Mesh& mesh,
    const D3DVERTEXELEMENT9* elements,size_t count,unsigned slot,const float* constants,const float* inverseView,float* world){
    if(!mesh.vertexCount||!constants||!inverseView)return false;
    const unsigned vertex=mesh.indexed?(mesh.indices.empty()?mesh.vertexCount:mesh.indices[slot?mesh.indices.size()-1:mesh.indices.size()/2]):(slot?mesh.vertexCount-1:mesh.vertexCount/2);
    Four inputs[16];if(!sampledVertexInputsAt(program,mesh,elements,count,vertex,inputs))return false;
    Four v;if(!evaluate(program,inputs,constants,v))return false;
    for(unsigned axis=0;axis<3;++axis){world[axis]=v[0]*inverseView[axis]+v[1]*inverseView[4+axis]+v[2]*inverseView[8+axis]+inverseView[12+axis];if(!std::isfinite(world[axis]))return false;}
    return true;
}
// World position of palette bone 0's origin (rows paletteBase..+2 map model to
// view space; their w components are the view-space translation). Stable for
// an object whose parts wave around a fixed root: the per-instance anchor.
inline bool rootWorld(const Program& program,const float* constants,const float* inverseView,float* world){
    if(program.paletteBase<0||program.paletteBase+2>=256||!constants||!inverseView)return false;
    const float* r=constants+4*program.paletteBase;const float v[3]={r[3],r[7],r[11]};
    for(unsigned axis=0;axis<3;++axis){world[axis]=v[0]*inverseView[axis]+v[1]*inverseView[4+axis]+v[2]*inverseView[8+axis]+inverseView[12+axis];
        if(!std::isfinite(world[axis])||std::fabs(world[axis])>1000000)return false;}
    return true;
}
} // namespace NorthlightActorDeformation
