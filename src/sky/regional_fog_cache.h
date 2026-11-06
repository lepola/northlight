#pragma once
#include "regional_fog.h"
#include <filesystem>

// Fog needs terrain heights and, in cities, opaque WMO street/roof heights.
// M2 geometry, BVHs, shadow meshes and GPU upload pages cannot affect this field.
// Build it independently so a deferred GI/shadow generation cannot remove fog.
namespace NorthlightRegionalFog {
constexpr float RefreshDistance=64.f;
inline bool cityInField(const Region& region,float x,float y){
    if(region.tiles.empty())return false;
    const float originX=std::floor(x/Spacing)*Spacing-float(N/2)*Spacing;
    const float originY=std::floor(y/Spacing)*Spacing-float(N/2)*Spacing;
    if(cityZone(zoneAt(region,originX,originY)))return true;
    int previousRow=INT_MIN;int columns[N];unsigned count=0;bool columnsReady=false;
    for(unsigned row=0;row<N;++row){
        const float py=originY+row*Spacing;if(!std::isfinite(py)||std::fabs(py)>100000)continue;
        const int rowChunk=chunkIndex(py);if(rowChunk==previousRow)continue;previousRow=rowChunk;
        if(columnsReady){for(unsigned i=0;i<count;++i)if(cityZone(zoneAtChunk(region,rowChunk,columns[i])))return true;}
        else{int previousColumn=INT_MIN;
            for(unsigned column=0;column<N;++column){
                const float px=originX+column*Spacing;if(!std::isfinite(px)||std::fabs(px)>100000)continue;
                const int chunk=chunkIndex(px);if(chunk==previousColumn)continue;previousColumn=chunk;
                columns[count++]=chunk;if(cityZone(zoneAtChunk(region,rowChunk,chunk)))return true;
            }
            columnsReady=true;
        }
    }
    return false;
}
inline bool buildCachedField(const std::string& cache,const std::string& map,NorthlightGI::Vec3 center,const Region& region,
                             Field& output,std::string& error,const NorthlightGI::AllocationAdmission& admit={},
                             const NorthlightGI::SceneSourceProvider& provider={},const ZoneScale* scale=nullptr){
    using namespace NorthlightGI;
    if(map!="Azeroth"&&map!="Kalimdor"&&map!="Expansion01"&&map!="Northrend"){error="Unsupported regional fog map";return false;}
    if(!std::isfinite(center.x)||!std::isfinite(center.y)||!std::isfinite(center.z)||std::fabs(center.x)>100000||std::fabs(center.y)>100000||std::fabs(center.z)>100000){error="Invalid regional fog center";return false;}
    if(admit&&!admit(sizeof(Field))){error="Regional fog field allocation deferred";return false;}
    const int tx=int(std::floor((WorldZero-center.y)/TileSize)),ty=int(std::floor((WorldZero-center.x)/TileSize));
    std::vector<std::string> files;
    for(int y=ty-1;y<=ty+1;++y)for(int x=tx-1;x<=tx+1;++x){
        auto path=cache+"/"+map+"/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";
        std::error_code ec;if(std::filesystem::is_regular_file(path,ec))files.push_back(std::move(path));
    }
    if(files.empty()){error="No cached regional fog geometry for "+map;return false;}
    WorldScene ground;
    const uint32_t categories=cityInField(region,center.x,center.y)?5u:1u;
    if(!loadInstancedScenes(files,cache+"/models",center-Vec3(288,288,320),center+Vec3(288,288,320),ground,error,categories,admit,{}, {},nullptr,provider))return false;
    auto field=buildField(ground,region,center.x,center.y,scale);
    // Empty support at high altitude is valid. Publishing an empty field also
    // prevents a previous ground region from leaking into the new location.
    output=std::move(field);return true;
}
}
