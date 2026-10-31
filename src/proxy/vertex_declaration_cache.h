#pragma once
#include <array>
#include <cstdint>

// Declarations are immutable COM objects. Hold a reference while caching the
// decoded elements so pointer reuse can never associate a new layout with an
// old one. Render-thread only; bounded and explicitly reset with the device.
namespace NorthlightVertexDeclarations {
class Cache {
    struct Entry {
        IDirect3DVertexDeclaration9* object=nullptr;
        std::array<D3DVERTEXELEMENT9,MAXD3DDECLLENGTH+1> elements{};
        UINT count=0;uint64_t touched=0;
        std::array<UINT,4> captureExtent{};bool captureChecked=false,captureValid=false;
    };
    std::array<Entry,128> entries_{};
    unsigned used_=0;
    uint64_t clock_=0;Entry* recent_=nullptr;
    Entry* acquire(IDirect3DVertexDeclaration9* object){
        if(!object)return nullptr;
        if(recent_&&recent_->object==object){recent_->touched=++clock_;return recent_;}
        Entry* slot=&entries_[0];
        for(unsigned i=0;i<used_;++i){auto& e=entries_[i];
            if(e.object==object){e.touched=++clock_;recent_=&e;return &e;}
            if(slot->object&&(!e.object||e.touched<slot->touched))slot=&e;
        }
        std::array<D3DVERTEXELEMENT9,MAXD3DDECLLENGTH+1> decoded{};UINT n=UINT(decoded.size());
        if(FAILED(object->GetDeclaration(decoded.data(),&n))||!n||n>decoded.size()||decoded[n-1].Stream!=0xff)return nullptr;
        if(used_<entries_.size())slot=&entries_[used_++];
        object->AddRef();if(slot->object)slot->object->Release();
        *slot=Entry{};slot->object=object;slot->elements=decoded;slot->count=n;slot->touched=++clock_;recent_=slot;return slot;
    }
public:
    Cache()=default;
    Cache(const Cache&)=delete;Cache& operator=(const Cache&)=delete;
    ~Cache(){clear();}
    void clear(){recent_=nullptr;for(unsigned i=0;i<used_;++i){auto& e=entries_[i];if(e.object)e.object->Release();e=Entry{};}used_=0;clock_=0;}
    bool get(IDirect3DVertexDeclaration9* object,const D3DVERTEXELEMENT9*& elements,UINT& count){
        elements=nullptr;count=0;auto* entry=acquire(object);if(!entry)return false;
        elements=entry->elements.data();count=entry->count;return true;
    }
    bool captureLayout(IDirect3DVertexDeclaration9* object,UINT extent[4]){
        if(!extent)return false;for(unsigned s=0;s<4;++s)extent[s]=0;
        auto* entry=acquire(object);if(!entry)return false;
        if(!entry->captureChecked){
            // Same stream/type/method/end validation as Frame::layout. Keep
            // its result separate: get() also serves non-capture consumers.
            constexpr unsigned sizes[]={4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8};
            bool any=false,end=false,valid=true;
            for(UINT i=0;i<entry->count;++i){const auto& e=entry->elements[i];if(e.Stream==0xff){end=true;break;}
                const unsigned size=e.Type<sizeof(sizes)/sizeof(*sizes)?sizes[e.Type]:0;
                if(e.Stream>=4||!size||e.Method!=D3DDECLMETHOD_DEFAULT){valid=false;break;}
                const UINT finish=UINT(e.Offset)+size;auto& value=entry->captureExtent[e.Stream];if(finish>value)value=finish;any=true;
            }
            entry->captureValid=valid&&any&&end;entry->captureChecked=true;
        }
        for(unsigned s=0;s<4;++s)extent[s]=entry->captureExtent[s];return entry->captureValid;
    }
};
}
