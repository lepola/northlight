#pragma once
// Redirects a fixed set of the client's call instructions to a function of ours, and puts them back exactly.
// Nothing changes unless every site still calls the expected native function. A site that something else has
// rewritten since is left as found: another mod's hook is never chained or overwritten.
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace water::client12340
{
template<std::size_t Count> class NativeCalls
{
    const std::array<uintptr_t,Count> sites;
    const uint32_t native;
    unsigned char original[Count][5]={},replacement[Count][5]={};
    bool owned[Count]={};
public:
    NativeCalls(std::array<uintptr_t,Count> addresses,uint32_t target):sites(addresses),native(target){}
    bool AnyOwned()const{for(bool value:owned)if(value)return true;return false;}
    template<class Read,class Write> bool Restore(Read read,Write write)
    {
        bool ok=true;
        for(unsigned i=0;i<Count;++i)if(owned[i]){
            unsigned char current[5];
            if(!read(sites[i],current,5)){ok=false;continue;}
            if(!std::memcmp(current,original[i],5)){owned[i]=false;continue;}
            if(std::memcmp(current,replacement[i],5)){ok=false;continue;}
            if(!write(sites[i],original[i],5)||!read(sites[i],current,5)||std::memcmp(current,original[i],5))ok=false;
            else owned[i]=false;
        }
        return ok&&!AnyOwned();
    }
    template<class Read,class Write> bool Install(Read read,Write write,uintptr_t thunk)
    {
        if(AnyOwned()){
            for(unsigned i=0;i<Count;++i){
                unsigned char current[5];
                if(!owned[i]||!read(sites[i],current,5)||std::memcmp(current,replacement[i],5))return false;
            }
            return true;
        }
        // Validate all original calls before changing any. Never chain an unknown owner.
        for(unsigned i=0;i<Count;++i){
            uint32_t relative=0;
            if(!read(sites[i],original[i],5)||original[i][0]!=0xe8)return false;
            std::memcpy(&relative,original[i]+1,4);
            if(uint32_t(sites[i]+5+relative)!=native)return false;
            replacement[i][0]=0xe8;relative=uint32_t(thunk-(sites[i]+5));
            std::memcpy(replacement[i]+1,&relative,4);
        }
        for(unsigned i=0;i<Count;++i){
            unsigned char current[5];owned[i]=true;
            if(!write(sites[i],replacement[i],5)||!read(sites[i],current,5)||std::memcmp(current,replacement[i],5)){
                Restore(read,write);return false;
            }
        }
        return true;
    }
};

}
