// Frozen Northlight MIT baseline: ca3846bd, src/gi/probe_texture_upload.h.
#pragma once
#include "probe_activation.h"
#include "probe_blend.h"
#include <cstddef>
#include <cstring>
#include <limits>

namespace FrozenProbeUpload {
// Match the shader's x+z*n, y atlas layout. Specialize outside the texel loop
// so every texel writes just its own channel, with no runtime channel tests.
template<unsigned Channel,bool KnownValid=false,int PreviousMode=-1,bool BlendValid=false,bool Contiguous=false>
void write(void* bits,std::size_t pitch,unsigned n,
           const std::vector<NorthlightGI::ProbeAtlasEntry>& atlas,
           const NorthlightProbeBlend::Mirror& blend,NorthlightProbeActivation& activation,
           float now,bool hasPrevious){
    static_assert(Channel<5);
    // Independent 16-cell textures and the validated activation path read atlas entries contiguously.
    // The checked fallback keeps its original order if it throws partway through.
    for(unsigned major=0;major<n;++major)for(unsigned minor=0;minor<n;++minor){
        const unsigned y=((Channel<3&&Contiguous)||(Channel==4&&KnownValid))?minor:major;
        const unsigned z=((Channel<3&&Contiguous)||(Channel==4&&KnownValid))?major:minor;
        auto* row=reinterpret_cast<float*>(static_cast<char*>(bits)+y*pitch)+z*n*4;
        const std::size_t base=(std::size_t(z)*n+y)*n;
        for(unsigned x=0;x<n;++x){
            const auto& entry=atlas[base+x];const auto& p=entry.probe;float* dst=row+x*4;
            if constexpr(Channel<3){
                for(unsigned k=0;k<4;++k){
                    if constexpr(Channel==0)dst[k]=p.sh[k].x;
                    else if constexpr(Channel==1)dst[k]=p.sh[k].y;
                    else dst[k]=p.sh[k].z;
                }
            }else if constexpr(Channel==3){
                const float valid=entry.occupied&&p.valid?1.f:0.f;
                const float start=(PreviousMode<0?hasPrevious:PreviousMode!=0)?(BlendValid?blend.startKnownValid(base+x):blend.start(base+x)):NorthlightProbeBlend::None;
                for(unsigned axis=0;axis<6;++axis){
                    float* moment=reinterpret_cast<float*>(reinterpret_cast<char*>(dst)+axis*n*pitch);
                    moment[0]=p.moments[axis].mean;moment[1]=p.moments[axis].meanSquare;
                    moment[2]=valid;moment[3]=start;
                }
            }else{
                dst[0]=float(entry.key.x);dst[1]=float(entry.key.y);dst[2]=float(entry.key.z);
                dst[3]=KnownValid?activation.updateKnownValid(base+x,entry,now):activation.update(base+x,entry,now);
            }
        }
    }
}
inline void write(unsigned channel,void* bits,std::size_t pitch,unsigned n,
                  const std::vector<NorthlightGI::ProbeAtlasEntry>& atlas,
                  const NorthlightProbeBlend::Mirror& blend,NorthlightProbeActivation& activation,
                  float now,bool hasPrevious,bool independentTarget=false){
    switch(channel){
    case 0:if(independentTarget&&n==16)write<0,false,-1,false,true>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);else write<0>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);break;
    case 1:if(independentTarget&&n==16)write<1,false,-1,false,true>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);else write<1>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);break;
    case 2:if(independentTarget&&n==16)write<2,false,-1,false,true>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);else write<2>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);break;
    case 3:
        if(hasPrevious&&n&&std::size_t(n)<=std::numeric_limits<std::size_t>::max()/n/n&&blend.covers(std::size_t(n)*n*n))
            write<3,false,1,true>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);
        else if(hasPrevious)write<3,false,1>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);
        else write<3,false,0>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);
        break;
    case 4:if(n&&std::size_t(n)<=std::numeric_limits<std::size_t>::max()/n/n&&activation.covers(std::size_t(n)*n*n))write<4,true>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);else write<4,false>(bits,pitch,n,atlas,blend,activation,now,hasPrevious);break;
    }
}
template<bool KnownValid> void previousImpl(void* bits,std::size_t pitch,unsigned n,const NorthlightProbeBlend::Mirror& blend){
    for(unsigned y=0;y<n;++y)for(unsigned z=0;z<n;++z){
        auto* row=reinterpret_cast<float*>(static_cast<char*>(bits)+y*pitch)+z*n*4;
        const std::size_t base=(std::size_t(z)*n+y)*n;
        for(unsigned x=0;x<n;++x){
            const float* source=KnownValid?blend.previousKnownValid(base+x):blend.previous(base+x);
            for(unsigned c=0;c<3;++c)std::memcpy(row+x*4+c*n*n*4,source+c*4,4*sizeof(float));
        }
    }
}
inline void previous(void* bits,std::size_t pitch,unsigned n,const NorthlightProbeBlend::Mirror& blend){
    if(n&&std::size_t(n)<=std::numeric_limits<std::size_t>::max()/n/n&&blend.covers(std::size_t(n)*n*n))previousImpl<true>(bits,pitch,n,blend);
    else previousImpl<false>(bits,pitch,n,blend);
}
} // namespace NorthlightProbeUpload
