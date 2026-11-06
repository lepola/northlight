#pragma once
#include "particle_texture_stats.h"
#include <cassert>
namespace TextureResetCases {
inline const void* key(unsigned n){return reinterpret_cast<const void*>(std::uintptr_t(n));}
template<class Cache> void run(){
    Cache c;NorthlightParticleTextures::View view;
    const unsigned char pixel[]={20,80,160,255},changed[]={100,40,10,255};
    const auto upload=[&](unsigned id,unsigned owner,unsigned pool,bool whole=true,bool success=true,const unsigned char* data=nullptr){c.upload(key(id),key(owner),1,1,0,21,data?data:pixel,4,4,whole,success,pool);};
    for(unsigned pool=0;pool<=3;++pool)upload(pool+1,pool+100,pool);
    upload(5,105,99);upload(6,106,1); // One MANAGED mip remains undecoded until after reset.
    assert(c.lookup(key(2),view));const auto lifetime=view.lifetime,revision=view.revision,hash=view.rgbaHash;const auto* pixels=view.rgba;
    const auto decodes=c.counts.decoded;
    for(unsigned retry=0;retry<3;++retry){c.reset();assert(c.lookup(key(2),view)&&view.lifetime==lifetime&&view.revision==revision&&view.rgbaHash==hash&&view.rgba==pixels&&c.counts.decoded==decodes);}
    assert(!c.lookup(key(1),view)&&!c.lookup(key(5),view));
    assert(c.lookup(key(3),view)&&c.lookup(key(4),view)&&c.lookup(key(6),view));
    upload(2,101,1,false);assert(!c.lookup(key(2),view));c.reset();assert(!c.lookup(key(2),view));
    upload(2,101,1,true,false);assert(!c.lookup(key(2),view));
    upload(2,101,1);c.reset();assert(c.lookup(key(2),view)&&view.lifetime==lifetime&&view.revision>revision);
    upload(2,201,1,true,true,changed);assert(c.lookup(key(2),view)&&view.lifetime!=lifetime&&view.rgba[0]==10);
    c.erase(key(2),key(101));assert(c.lookup(key(2),view));c.erase(key(2),key(201));assert(!c.lookup(key(2),view));
    upload(1,100,0);assert(c.lookup(key(1),view));const auto defaultLife=view.lifetime;c.reset();assert(!c.lookup(key(1),view));
    upload(1,300,1,true,true,changed);assert(c.lookup(key(1),view)&&view.lifetime!=defaultLife&&view.rgba[0]==10);
    c.erase(key(1),key(100));assert(c.lookup(key(1),view));
    c.invalidate(key(6));c.reset();assert(!c.lookup(key(6),view));
    c.clear();assert(!c.bytes()&&!c.size());
}
inline void run(){
    NorthlightParticleTextures::enabled.store(true);NorthlightMaterialTextures::enabled.store(true);
    run<NorthlightParticleTextures::Cache>();run<NorthlightMaterialTextures::Cache>();
    NorthlightMaterialTextures::Cache c;NorthlightParticleTextures::View view;const unsigned char pixel[]={20,80,160,255};
    c.upload(key(1),key(10),1,1,0,21,pixel,4,4,true,true,1);
    assert(c.copy(key(1),key(2),key(20),0)&&c.copy(key(1),key(3),key(30),1));
    assert(c.lookup(key(3),view));const auto lifetime=view.lifetime;
    assert(c.copy(key(1),key(3),key(31),1)&&c.lookup(key(3),view)&&view.lifetime!=lifetime);
    c.erase(key(3),key(30));c.reset();assert(c.lookup(key(1),view)&&!c.lookup(key(2),view)&&c.lookup(key(3),view));
}
}
