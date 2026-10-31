#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>

namespace NorthlightNativeLocalLight {
struct Sample {
    uint16_t known=0,fromGet=0,fromWrite=0,finite=0,read=0;
    bool exactReads=false;
    float rows[11][4]={}; // Native LocalLights: colours c17..20, positions c21..24, attenuation c25..27.
};
class Probe {
    unsigned frame_=~0u,count_=0,total_=0;
    uint64_t seen_[8]={};
public:
    bool full()const{return total_>=128;}
    bool sample(bool enabled,bool contextKnown,bool skinned,bool mirrorActive,unsigned frame,uint64_t shader,
                const float (*rows)[4],const bool* known,const bool* written,uint16_t read,bool exactReads,Sample& out){
        if(!enabled||contextKnown||!skinned||!mirrorActive||!rows||!known||!written||full())return false;
        if(frame_!=frame){frame_=frame;count_=0;}
        if(count_==8)return false;
        Sample value;value.read=read;value.exactReads=exactReads;
        uint64_t hash=shader^14695981039346656037ull;
        auto word=[&](uint32_t v){hash^=v;hash*=1099511628211ull;};
        for(unsigned i=0;i<11;++i){
            if(known[17+i])value.fromGet|=uint16_t(1u<<i);
            if(written[17+i])value.fromWrite|=uint16_t(1u<<i);
            if(known[17+i]||written[17+i]){
                value.known|=uint16_t(1u<<i);bool finite=true;
                for(unsigned k=0;k<4;++k){value.rows[i][k]=rows[17+i][k];finite&=std::isfinite(value.rows[i][k]);
                    uint32_t bits;std::memcpy(&bits,&value.rows[i][k],4);word(bits);}
                if(finite)value.finite|=uint16_t(1u<<i);
            }else word(0xdead0000u+i);
        }
        word(value.fromGet);word(value.fromWrite);word(value.read);word(value.exactReads);
        for(unsigned i=0;i<count_;++i)if(seen_[i]==hash)return false;
        seen_[count_++]=hash;++total_;out=value;return true;
    }
};
} // namespace NorthlightNativeLocalLight
