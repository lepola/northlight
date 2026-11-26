#pragma once
#include "world/world_adt.h"
#include <algorithm>

namespace water::world
{
// Reduce interior geometry while retaining every authored boundary vertex.
// Adjacent cells therefore share identical edges at all supported detail levels.
// Holes retain the original mesh: a coarse fan must never bridge a cave opening.
inline bool ReduceTerrainCell(TerrainCell& cell,unsigned step,unsigned alphaSize)
{
    if(cell.vertices.size()!=145 || (step!=1&&step!=4&&step!=8) ||
       (alphaSize!=64&&alphaSize!=16&&alphaSize!=4) || cell.alphaSize!=64)return false;
    for(unsigned layer=1;layer<cell.layers.size();++layer)if(cell.layers[layer].alpha.size()!=4096)return false;
    if(step>1&&!cell.holes&&!cell.indices.empty()){
        std::vector<TerrainVertex> vertices;std::vector<uint16_t> indices;std::vector<uint32_t> colours;
        const bool painted=cell.colours.size()==cell.vertices.size();
        if(painted)colours.reserve(step==4?49:33);
        vertices.reserve(step==4?49:33);indices.reserve(8/step*8/step*step*12);
        std::array<uint16_t,81> remap;remap.fill(0xffff);
        const auto point=[&](unsigned x,unsigned y){auto& index=remap[y*9+x];
            if(index==0xffff){index=static_cast<uint16_t>(vertices.size());vertices.push_back(cell.vertices[y*17+x]);if(painted)colours.push_back(cell.colours[y*17+x]);}return index;};
        for(unsigned y=0;y<8;y+=step)for(unsigned x=0;x<8;x+=step){
            const auto centre=point(x+step/2,y+step/2);std::array<uint16_t,32> edge{};unsigned n=0;
            for(unsigned k=0;k<step;++k)edge[n++]=point(x+k,y);
            for(unsigned k=0;k<step;++k)edge[n++]=point(x+step,y+k);
            for(unsigned k=0;k<step;++k)edge[n++]=point(x+step-k,y+step);
            for(unsigned k=0;k<step;++k)edge[n++]=point(x,y+step-k);
            for(unsigned k=0;k<n;++k){indices.push_back(edge[k]);indices.push_back(centre);indices.push_back(edge[(k+1)%n]);}
        }
        cell.vertices=std::move(vertices);cell.indices=std::move(indices);cell.colours=std::move(colours);
    }
    if(alphaSize<64)for(unsigned layer=1;layer<cell.layers.size();++layer){
        auto& original=cell.layers[layer].alpha;std::vector<uint8_t> samples(alphaSize*alphaSize);
        const unsigned block=64/alphaSize;
        for(unsigned y=0;y<alphaSize;++y)for(unsigned x=0;x<alphaSize;++x){unsigned total=0;
            for(unsigned j=0;j<block;++j)for(unsigned i=0;i<block;++i){
                const auto sx=std::min(x*block+i,cell.FixAlphaEdges()?62u:63u),sy=std::min(y*block+j,cell.FixAlphaEdges()?62u:63u);
                total+=original[sy*64+sx];}
            samples[y*alphaSize+x]=static_cast<uint8_t>((total+block*block/2)/(block*block));
        }
        original=std::move(samples);
    }
    cell.alphaSize=alphaSize;return true;
}
}
