#pragma once
#include <algorithm>
#include <cstddef>
#include <utility>
#include <vector>

namespace NorthlightWorldMeshPages {
constexpr size_t MaxBufferBytes=512u<<10;
constexpr size_t CopyChunkBytes=256u<<10;
struct PageBytes {size_t vertexBytes=0,indexBytes=0,vertexStride=0;};
enum class Step {CreateVertices,CreateIndices,CopyVertices,PreloadVertices,CopyIndices,PreloadIndices};
enum class Result {Pending,Ready,Failed};

// The caller owns two GPU slots and keeps the committed slot active until this
// entire upload is Ready. This cursor never publishes or releases a GPU resource.
// A replacement or cancelled upload must reset the cursor and its pending slot
// together; a failed operation is latched until begin/reset.
class Upload {
    std::vector<PageBytes> pages_;
    size_t page_=0,offset_=0;
    Step step_=Step::CreateVertices;
    Result result_=Result::Pending;
    bool begun_=false;
public:
    void reset(){pages_.clear();page_=offset_=0;step_=Step::CreateVertices;result_=Result::Pending;begun_=false;}
    bool begin(std::vector<PageBytes> pages){
        reset();
        for(const auto& p:pages)
            if(!p.vertexBytes||!p.indexBytes||p.vertexBytes>MaxBufferBytes||p.indexBytes>MaxBufferBytes||(p.vertexStride&&p.vertexBytes%p.vertexStride)){
                result_=Result::Failed;return false;
            }
        pages_=std::move(pages);begun_=true;
        if(pages_.empty())result_=Result::Ready;
        return true;
    }
    Result status()const{return result_;}
    size_t pageIndex()const{return page_;}
    size_t offset()const{return offset_;}
    Step step()const{return step_;}

    // Exactly one backend operation per call. The outer renderer may call again
    // while its shared byte/time budget permits; each call rechecks that budget.
    // perform(step,page,offset,bytes) returns success; Create may reuse a pooled
    // buffer, and Preload uses the full page size. Budget implements available(),
    // chunk(desired), consume(bytes). Only successful copies consume byte budget.
    // The time limit remains soft: an individual driver call cannot be preempted.
    template<class Perform,class Budget>
    Result advance(Perform&& perform,Budget& budget){
        if(result_!=Result::Pending||!begun_||!budget.available())return result_;
        const auto& p=pages_[page_];
        const bool vertex=step_==Step::CreateVertices||step_==Step::CopyVertices||step_==Step::PreloadVertices;
        const bool copy=step_==Step::CopyVertices||step_==Step::CopyIndices;
        const size_t total=vertex?p.vertexBytes:p.indexBytes;
        size_t bytes=copy?budget.chunk(std::min(CopyChunkBytes,total-offset_)):total;
        if(copy&&vertex&&p.vertexStride)bytes-=bytes%p.vertexStride;
        if(!bytes)return result_;
        if(!perform(step_,page_,copy?offset_:0,bytes)){result_=Result::Failed;return result_;}
        if(copy){
            budget.consume(bytes);offset_+=bytes;
            if(offset_<total)return result_;
            offset_=0;
        }
        switch(step_){
        case Step::CreateVertices:step_=Step::CreateIndices;break;
        case Step::CreateIndices:step_=Step::CopyVertices;break;
        case Step::CopyVertices:step_=Step::PreloadVertices;break;
        case Step::PreloadVertices:step_=Step::CopyIndices;break;
        case Step::CopyIndices:step_=Step::PreloadIndices;break;
        case Step::PreloadIndices:
            ++page_;step_=Step::CreateVertices;
            if(page_==pages_.size())result_=Result::Ready;
            break;
        }
        return result_;
    }
};
} // namespace NorthlightWorldMeshPages
