#pragma once
#include "../core/triangle_run.h"
#include <cstring>

// The full redraw and its cost estimate share this grouping. Partial redraws
// keep individual command bounds and scissor order. Plans and instance storage
// remain unchanged; this is a view over immutable commands, with no allocation.
namespace StaticShadow {
struct OpaqueRun {
    NorthlightTriangleRun::Run triangles;
    size_t end=0;
};
template<class Commands>
OpaqueRun opaqueRun(const Commands& commands,size_t first,bool depthVariant,bool fastVariant) {
    const auto& command=commands[first];const auto& resource=*command.resource;
    const auto& model=*resource.model;const auto& batch=model.batches[command.batch];
    struct Range {uint32_t page,start,count,minVertex,vertexCount;};
    OpaqueRun run;run.end=first+1;
    run.triangles.begin(Range{0,batch.firstIndex,batch.indexCount/3,0,resource.vertexCount});
    if(!resource.opaque[batch.material]||!(command.path?fastVariant:depthVariant))return run;
    while(run.end<commands.size()){
        const auto& next=commands[run.end];
        if(next.resource!=&resource||next.path!=command.path||next.count!=command.count)break;
        const auto& nextBatch=model.batches[next.batch];
        auto joined=run.triangles;
        // 65535 triangles is a conservative D3D9 draw limit. Check adjacency
        // before comparing transforms; most material boundaries need no copy.
        if(!resource.opaque[nextBatch.material]||
           !joined.append(Range{0,nextBatch.firstIndex,nextBatch.indexCount/3,0,resource.vertexCount},3u*65535u)||
           std::memcmp(command.data,next.data,command.count*sizeof(*command.data))!=0)break;
        run.triangles=joined;++run.end;
    }
    return run;
}
}
