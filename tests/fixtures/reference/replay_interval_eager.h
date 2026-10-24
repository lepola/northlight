// Frozen interval register initialization before lazy temporaries; exact arithmetic/status oracle.
#pragma once
namespace NorthlightReplayBounds {namespace detail {
inline bool legacyEvaluateIntervals(const NorthlightActorDeformation::Program& p,const Vector inputs[16],const float* constants,Vector& position,bool definitionsApplied=false){
    using namespace NorthlightShadowShader;Vector temp[33]={},address={};
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
        if(type==0){if(index<0||index>=32)return false;value=temp[index];valid=initialized[index];}
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
        if((type!=0&&type!=3)||(type==0&&index>=32)||(type==3&&index))return false;Vector& target=type==3?address:temp[index];
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
    position=temp[p.positionRegister];return position[3].value==1.f&&position[3].error==0;
}
}}
