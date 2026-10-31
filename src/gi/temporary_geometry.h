#pragma once
#include "memory_footprint.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <type_traits>

namespace NorthlightGI {
// Private per-candidate spill storage. tmpfile closes and deletes the file on
// destruction; no file is mapped into the 32-bit client's address space.
class TemporaryGeometryError:public std::runtime_error {public:using std::runtime_error::runtime_error;};
class TemporaryGeometryFile {
    struct Close {void operator()(FILE* file)const{if(file)std::fclose(file);}};
    std::unique_ptr<FILE,Close> file_;mutable std::mutex mutex_;uint64_t size_=0;
    void seek(uint64_t offset)const{
        if(offset>uint64_t(std::numeric_limits<long>::max())||std::fseek(file_.get(),long(offset),SEEK_SET))throw TemporaryGeometryError("Temporary geometry seek failed");
    }
public:
    TemporaryGeometryFile():file_(std::tmpfile()){if(!file_)throw TemporaryGeometryError("Temporary geometry file creation failed");}
    uint64_t append(const void* data,size_t bytes){
        std::lock_guard<std::mutex> lock(mutex_);const auto offset=size_;
        if(bytes>uint64_t(std::numeric_limits<long>::max())-size_)throw TemporaryGeometryError("Temporary geometry size exceeded");
        seek(size_);if(bytes&&std::fwrite(data,1,bytes,file_.get())!=bytes)throw TemporaryGeometryError("Temporary geometry write failed");size_+=bytes;return offset;
    }
    void write(uint64_t offset,const void* data,size_t bytes){
        std::lock_guard<std::mutex> lock(mutex_);if(offset>size_||bytes>size_-offset)throw TemporaryGeometryError("Temporary geometry write range invalid");
        seek(offset);if(bytes&&std::fwrite(data,1,bytes,file_.get())!=bytes)throw TemporaryGeometryError("Temporary geometry write failed");
    }
    void read(uint64_t offset,void* data,size_t bytes)const{
        std::lock_guard<std::mutex> lock(mutex_);if(offset>size_||bytes>size_-offset)throw TemporaryGeometryError("Temporary geometry read range invalid");
        seek(offset);if(bytes&&std::fread(data,1,bytes,file_.get())!=bytes)throw TemporaryGeometryError("Temporary geometry read failed");
    }
    void flush(){std::lock_guard<std::mutex> lock(mutex_);if(std::fflush(file_.get()))throw TemporaryGeometryError("Temporary geometry flush failed");}
    uint64_t size()const{return size_;}
};
// Exclusive scratch arrays only: references survive four different block
// visits. Scene preparation reads at most three vertices together. Published
// BVHs never use this storage. Each array owns at most 256 KiB of cache.
template<class T> class TemporaryGeometryArray {
    static_assert(std::is_trivially_copyable_v<T>);
    static constexpr size_t Elements=65536/sizeof(T),Bytes=Elements*sizeof(T);
    struct Slot {std::unique_ptr<T[]> values;size_t block=SIZE_MAX;uint64_t age=0;bool dirty=false;};
    mutable TemporaryGeometryFile file_;mutable std::array<Slot,4> slots_;mutable uint64_t age_=0;size_t size_=0,blocks_=0;
    void flush(Slot& slot)const{if(slot.block!=SIZE_MAX&&slot.dirty){file_.write(slot.block*Bytes,slot.values.get(),Bytes);slot.dirty=false;}}
    Slot& visit(size_t block,bool dirty)const{
        for(auto& slot:slots_)if(slot.block==block){slot.age=++age_;slot.dirty|=dirty;return slot;}
        auto* victim=&slots_[0];for(auto& slot:slots_)if(slot.age<victim->age)victim=&slot;
        if(!victim->values)victim->values=std::make_unique<T[]>(Elements);
        flush(*victim);file_.read(block*Bytes,victim->values.get(),Bytes);victim->block=block;victim->age=++age_;victim->dirty=dirty;return *victim;
    }
public:
    size_t size()const{return size_;}size_t capacity()const{return blocks_*Elements;}
    const T& operator[](size_t i)const{return visit(i/Elements,false).values[i%Elements];}
    T& operator[](size_t i){return visit(i/Elements,true).values[i%Elements];}
    void push_back(const T& value){
        const T copy=value;
        if(size_==blocks_*Elements){std::array<unsigned char,Bytes> zero{};file_.append(zero.data(),Bytes);++blocks_;}
        (*this)[size_++]=copy;
    }
    void resize(size_t size){while(size_<size)push_back(T{});size_=size;}
    void clear(){size_=0;}
    void flush(){for(auto& slot:slots_)flush(slot);file_.flush();}
    uint64_t bytes()const{uint64_t bytes=sizeof(*this);for(const auto& slot:slots_)if(slot.values)bytes+=Bytes;return bytes;}
    void accountMemory(NorthlightMemoryFootprint::Counter& memory,NorthlightMemoryFootprint::Kind kind)const{
        memory.allocation(NorthlightMemoryFootprint::Metadata,this,sizeof(*this));for(const auto& slot:slots_)memory.allocation(kind,slot.values.get(),Bytes);
    }
};
}
