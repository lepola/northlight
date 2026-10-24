#pragma once
#include "settings_service.h"

namespace NorthlightSettings {
// Stop-and-wait chunks: an input is acknowledged only once; retries never repeat a commit.
class Transport {
    std::string id,input,cachedResponse;unsigned accepted=0,total=0;
public:
    static constexpr unsigned Chunk=768,MaxChunks=171;
    std::string exchange(Service& service,const std::string& message){
        if(message.size()>Chunk+128)return "";auto nl=message.find('\n');if(nl==message.npos)return "";
        std::istringstream h(message.substr(0,nl));std::string version,nextId,extra;unsigned part=0,count=0,page=0;
        if(!(h>>version>>nextId>>part>>count>>page)||(h>>extra)||version!="NL1"||nextId.size()>64||!count||count>MaxChunks||!part||part>count)return "";
        auto payload=message.substr(nl+1);if(payload.size()>Chunk)return "";
        if(nextId!=id){if(part!=1)return "";id=nextId;input.clear();cachedResponse.clear();accepted=0;total=count;}
        if(count!=total)return "";
        if(part==accepted+1){if(input.size()+payload.size()>131072)return "";input+=payload;++accepted;}
        if(accepted==total&&cachedResponse.empty()){if(!service.submit(id,input))return "NL1\t"+id+"\t"+std::to_string(accepted)+"\t0\t0\nbusy";service.result(id,cachedResponse);}
        const auto& reply=cachedResponse;
        const auto pages=unsigned((reply.size()+Chunk-1)/Chunk);page=std::min(page,pages);
        return "NL1\t"+id+"\t"+std::to_string(accepted)+"\t"+std::to_string(page)+"\t"+std::to_string(pages)+"\n"+(page?reply.substr((page-1)*Chunk,Chunk):"");
    }
};
} // namespace NorthlightSettings
