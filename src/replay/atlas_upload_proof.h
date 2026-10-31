#pragma once
// SPDX-License-Identifier: MIT
// Diagnostic metadata only. The replay thread hashes actual 64px native atlas
// uploads; raw texture bytes, COM references and game-side shadows are not kept.
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace NorthlightAtlasProof {
inline std::atomic<bool> enabled{false};
constexpr unsigned Size=512,Tile=64,Tiles=64,Capacity=32;
inline unsigned blockBytes(unsigned format){return format==0x31545844?8:(format==0x33545844||format==0x35545844?16:0);}
inline std::uint64_t hashRows(const unsigned char* p,unsigned pitch,unsigned rows,unsigned bytes){
    std::uint64_t h=14695981039346656037ull;
    for(unsigned y=0;y<rows;++y)for(unsigned x=0;x<bytes;++x){h^=p[std::size_t(y)*pitch+x];h*=1099511628211ull;}
    return h;
}
struct Entry {
    const void* raw=nullptr;const void* owner=nullptr;
    unsigned format=0,lastDraw=0,uploads=0,failedUploads=0,invalidations=0,lastFailure=0;std::uint64_t known=0,revision=0,used=0,drawOrder=0;
    std::array<std::uint64_t,Tiles> tiles{};
    std::array<std::uint32_t,14> samplers{};unsigned samplerKnown=0;
    std::uint64_t vsHash=0,psHash=0;
};
class Cache {
    std::array<Entry,Capacity> entries_{};std::uint64_t clock_=0,revision_=0,draw_=0;
public:
    Entry* find(const void* raw){if(raw)for(auto& e:entries_)if(e.raw==raw)return &e;return nullptr;}
    void erase(const void* raw,const void* owner=nullptr){for(auto& e:entries_)if((raw&&e.raw==raw)||(owner&&e.owner==owner))e={};}
    void invalidate(const void* raw){if(auto* e=find(raw)){e->known=0;++e->invalidations;e->revision=++revision_;}}
    void clear(){for(auto& e:entries_)e={};}
    void upload(const void* raw,const void* owner,unsigned format,unsigned width,unsigned height,unsigned level,
                unsigned l,unsigned t,unsigned r,unsigned b,const unsigned char* source,std::size_t sourceBytes,unsigned pitch,bool success,unsigned failure=0){
        if(!enabled.load(std::memory_order_relaxed)||!raw||!owner||width!=Size||height!=Size||level)return;
        const unsigned block=blockBytes(format);if(!block)return;
        auto* e=find(raw);
        if(e&&(e->owner!=owner||e->format!=format)){*e={};e=nullptr;}
        const bool tile=r==l+Tile&&b==t+Tile&&l%Tile==0&&t%Tile==0;
        const bool whole=l==0&&t==0&&r==Size&&b==Size;
        if(!e&&!tile)return; // Full 512px model textures do not qualify as icon atlases.
        if(!e){e=&entries_[0];for(auto& candidate:entries_)if(!candidate.raw||candidate.used<e->used)e=&candidate;*e={};e->raw=raw;e->owner=owner;e->format=format;}
        e->used=++clock_;e->revision=++revision_;++e->uploads;if(!success){++e->failedUploads;e->lastFailure=failure;}
        if(l>=r||t>=b||r>Size||b>Size||(!tile&&!whole)){e->known=0;return;}
        const unsigned rows=(b-t)/4,bytes=(r-l)/4*block;
        // Keep attempted upload hashes even when the backend rejects the write:
        // a mismatch then identifies lost content, instead of hiding the evidence.
        if(!source||pitch<bytes||!rows||sourceBytes<std::size_t(rows-1)*pitch+bytes){e->known=0;return;}
        const unsigned first=whole?0:t/Tile*8+l/Tile,last=whole?Tiles:first+1;
        for(unsigned i=first;i<last;++i){const unsigned row=whole?i/8*16:0,col=whole?i%8*16*block:0;
            e->tiles[i]=hashRows(source+std::size_t(row)*pitch+col,pitch,16,16*block);e->known|=std::uint64_t(1)<<i;}
    }
    Entry* viewed(const void* raw,unsigned frame){if(auto* e=find(raw)){e->lastDraw=frame;e->drawOrder=++draw_;return e;}return nullptr;}
    unsigned recent(std::array<Entry,4>& out,unsigned frame)const{
        unsigned count=0;std::uint64_t previous=~std::uint64_t(0);
        while(count<out.size()){
            const Entry* best=nullptr;
            for(const auto& e:entries_)if(e.raw&&e.drawOrder&&e.drawOrder<previous&&unsigned(frame-e.lastDraw)<=2&&(!best||e.drawOrder>best->drawOrder))best=&e;
            if(!best)break;out[count++]=*best;previous=best->drawOrder;
        }
        return count;
    }
};
inline Cache& cache(){static Cache value;return value;}
}
