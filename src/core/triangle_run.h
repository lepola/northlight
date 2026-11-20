#pragma once
#include <algorithm>
#include <cstdint>

// Adjacent triangle-list ranges in one vertex/index page. Callers must also
// keep shaders, transforms, material inputs and clipping state identical.
// No sorting or geometry copying: the emitted indices retain their order.
namespace NorthlightTriangleRun {
struct Run {
    std::uint32_t page=0,start=0,count=0,minVertex=0;
    std::uint64_t vertexEnd=0;

    template<class Batch> void begin(const Batch& b) {
        page=b.page;start=b.start;count=b.count;minVertex=b.minVertex;
        vertexEnd=std::uint64_t(b.minVertex)+b.vertexCount;
    }
    template<class Batch> bool append(const Batch& b,std::uint32_t maxIndices) {
        if(b.page!=page||std::uint64_t(start)+3ull*count!=b.start||
           3ull*(std::uint64_t(count)+b.count)>maxIndices)return false;
        count+=b.count;minVertex=std::min(minVertex,b.minVertex);
        vertexEnd=std::max(vertexEnd,std::uint64_t(b.minVertex)+b.vertexCount);
        return true;
    }
};
}
