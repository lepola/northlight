#include "world_scene.h"
#include <chrono>
#include "world_asset_name.h"
#include "world_terrain_lod.h"
#include "owned_world_region.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstring>
#include <cstdio>

namespace water::world
{
namespace
{
constexpr unsigned MaxModels=MaxSceneModels, MaxModelSources=1024, MaxTextures=MaxSceneTextures, MaxInstances=MaxSceneInstances, MaxSources=4096, MaxWater=8192;
constexpr const char* Tiles[]={"world\\maps\\azeroth\\azeroth_32_49.adt","world\\maps\\azeroth\\azeroth_32_50.adt"};
constexpr const char* Required[]={
    "world\\azeroth\\elwynn\\passivedoodads\\trees\\elwynntreecanopy01.m2",
    "world\\azeroth\\elwynn\\passivedoodads\\trees\\elwynntreecanopy03.m2",
    "world\\azeroth\\elwynn\\passivedoodads\\bush\\elwynnbush09.m2",
    "world\\azeroth\\elwynn\\passivedoodads\\cliffrocks\\elwynncliffrock02.m2"};
std::size_t NameBytes(const std::string& s) { return s.capacity()+1; }
uint32_t SourcePolicy(const SceneOptions& o)
{return unsigned(o.buildings)|(unsigned(o.texturePrefix)<<1)|(unsigned(o.lake)<<2)|
        (unsigned(o.snapshotReads)<<3)|(unsigned(o.streaming)<<4)|(o.textureMipBias<<5)|(unsigned(o.distance)<<6);}
uint32_t U32(Bytes b,std::size_t at) { uint32_t v=0; b.U32(at,v); return v; }
uint16_t U16(Bytes b,std::size_t at) { uint16_t v=0; b.U16(at,v); return v; }
bool Float(Bytes b,std::size_t at,float& v) { return b.F32(at,v) && std::isfinite(v); }
template<class T> uint32_t Find(const std::vector<T>& values,const std::string& name)
{
    for (unsigned i=0;i<values.size();++i) if (values[i].name==name) return i;
    return MissingSceneAsset;
}
std::size_t TerrainPayloadBytes(const SceneTerrain& terrain)
{
    return terrain.tile.Bytes()-sizeof(AdtTile)+terrain.textures.capacity()*sizeof(uint32_t);
}
class Builder
{
public:
    ArchiveReader& archives;
    const std::atomic<bool>& cancelled;
    const std::size_t sceneLimit,workspaceLimit;
    WorldScene scene;
    SceneResult result;
    std::size_t inputBytes=0,pendingBytes=0;
    std::array<TextureError,MaxTextures> textureErrors{};
    std::atomic<uint64_t>* progress = nullptr;
    SceneOptions options;
    AdtAlphaFormat alpha=AdtAlphaFormat::Packed4;
    // Completed tiles retain their storage through material remapping. The current
    // tile can still shrink during LOD selection and must be counted live.
    std::size_t finishedTiles=0, finishedTerrainBytes=0;
    void FinishTiles()
    {
        while (finishedTiles<scene.region->terrain.size())
            finishedTerrainBytes+=TerrainPayloadBytes(scene.region->terrain[finishedTiles++]);
    }
    std::size_t RetainedBytes() const
    {
        auto bytes=scene.BytesExcludingTerrainPayload()+finishedTerrainBytes;
        for (auto i=finishedTiles;i<scene.region->terrain.size();++i)
            bytes+=TerrainPayloadBytes(scene.region->terrain[i]);
        return bytes;
    }
    bool Fail(SceneError e,const std::string& asset={}) { result.error=e; result.asset=asset; return false; }
    bool Stop() { if(!cancelled.load(std::memory_order_relaxed))return false;Fail(SceneError::Cancelled);return true; }
    bool Fits(std::size_t scratch=0,std::size_t decoded=0)
    {
        scratch=std::max(scratch,inputBytes)+furtherWater.capacity()*sizeof(FurtherWater); decoded=std::max(decoded,pendingBytes);
        const auto bytes=RetainedBytes();
        if (bytes>sceneLimit || bytes>workspaceLimit || decoded>sceneLimit-bytes ||
            scratch>workspaceLimit-bytes || decoded>workspaceLimit-bytes-scratch) return Fail(SceneError::Budget);
        result.peakWorkspaceBytes=std::max(result.peakWorkspaceBytes,bytes+scratch+decoded); return true;
    }
    std::size_t DecodeLimit(std::size_t scratch)
    {
        if (!Fits(scratch)) return 0;
        const auto retained=RetainedBytes();
        return std::min(sceneLimit-retained,workspaceLimit-retained-scratch-furtherWater.capacity()*sizeof(FurtherWater));
    }
    bool Read(const std::string& name,ArchiveEntry& entry,std::size_t scratch=0)
    {
        inputBytes=scratch;
        if (Stop() || !Fits()) return false;
        const auto available=workspaceLimit-RetainedBytes()-scratch-furtherWater.capacity()*sizeof(FurtherWater);
        result.archiveError=options.snapshotReads?archives.ReadSnapshot(name,cancelled,entry,available):
            archives.Read(name,cancelled,entry,available);
        if (result.archiveError!=ArchiveError::None)
            return Fail(result.archiveError==ArchiveError::Cancelled ? SceneError::Cancelled :
                        result.archiveError==ArchiveError::Budget ? SceneError::Budget : SceneError::Archive,name);
        result.sourceBytes+=entry.bytes.size();
        if (progress) progress->store(result.sourceBytes, std::memory_order_release);
        inputBytes+=entry.bytes.capacity();
        return Fits();
    }
    bool Source(ArchiveEntry& entry)
    {
        if(Find(scene.sources,entry.name)!=MissingSceneAsset)return Fits(entry.bytes.capacity());
        if (scene.sources.size()==MaxSources) return Fail(SceneError::Budget,entry.name);
        scene.sources.push_back({std::move(entry.name),std::move(entry.source)});
        return Fits(entry.bytes.capacity());
    }
    // The client draws a terrain layer from its specular variant (`<name>_s.blp`) when one
    // exists: the same colour with the highlight mask in alpha. The plain tileset's alpha is
    // 1 and would give every surface the full terrain highlight. Absent variant: the plain name.
    bool TerrainTexture(const std::string& name,std::string& chosen)
    {
        chosen=name;
        if (Stop()) return false;
        if (name.size()<4) return true;
        std::string variant=name.substr(0,name.size()-4);
        std::string extension=name.substr(name.size()-4);
        for (auto& c : extension) c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (extension!=".blp") return true;
        variant+="_s.blp";
        const auto found=archives.Contains(variant,cancelled,!options.snapshotReads);
        if (found==ArchiveError::None) { chosen=std::move(variant); return true; }
        if (found==ArchiveError::Missing) return true;
        result.archiveError=found;
        return Fail(found==ArchiveError::Cancelled ? SceneError::Cancelled :
                    found==ArchiveError::Budget ? SceneError::Budget : SceneError::Archive,variant);
    }
    uint32_t Texture(const std::string& name,unsigned maxDimension=0)
    {
        const auto found=Find(scene.textures,name);
        if(found!=MissingSceneAsset){auto& retained=scene.textures[found].maxDimension;
            retained=retained&&maxDimension?std::max(retained,maxDimension):0;return found;}
        if (scene.textures.size()==MaxTextures) { Fail(SceneError::Budget,name); return MissingSceneAsset; }
        scene.textures.push_back({name,{},maxDimension});
        if (!Fits()) return MissingSceneAsset;
        return static_cast<uint32_t>(scene.textures.size()-1);
    }
    // Water past the reach always prepared, nearest first while the budget lasts (OwnedWorldRegion::WaterFarReach).
    struct FurtherWater { double distance; reflection::WaterSurface surface; decltype(scene.region->waterTypes)::value_type type; };
    static constexpr std::size_t MaxFurtherWater=65536;
    std::vector<FurtherWater> furtherWater{};
    void WithheldWater(const reflection::WaterSurface& surface)
    {
        double distance=0;
        for(unsigned c=0;c<2;++c)distance=std::max(distance,std::max({0.,
            double(surface.bounds[c])-options.region.Centre(c),double(options.region.Centre(c))-surface.bounds[c+3]}));
        scene.waterCompleteReach=std::min(scene.waterCompleteReach,float(distance));
    }
    bool AdmitFurtherWater()
    {
        const std::size_t room=MaxWater-std::min<std::size_t>(scene.region->water.size(),MaxWater);
        const auto nearer=[](const FurtherWater& a,const FurtherWater& b){return a.distance!=b.distance?a.distance<b.distance:a.surface.key<b.surface.key;};
        if(furtherWater.size()>room){
            std::nth_element(furtherWater.begin(),furtherWater.begin()+room,furtherWater.end(),nearer);
            for(std::size_t i=room;i<furtherWater.size();++i)WithheldWater(furtherWater[i].surface);
            furtherWater.resize(room);
        }
        std::sort(furtherWater.begin(),furtherWater.end(),nearer);
        for(const auto& w:furtherWater){scene.region.Edit().water.push_back(w.surface);scene.region.Edit().waterTypes.push_back(w.type);}
        std::vector<FurtherWater>().swap(furtherWater);
        return true;
    }
    // One square of liquid, with its corners. False only on a fault in water near enough to matter.
    bool WaterQuad(const std::array<std::array<double,3>,4>& points,uint64_t key,uint16_t type)
    {
        // Admission precedes fitting: a remote waterfall must not
        // reject an otherwise valid local ocean window.
        std::array<float,3> low3={float(points[0][0]),float(points[0][1]),0},high0=low3;
        for(const auto& p:points)for(unsigned c=0;c<2;++c){low3[c]=std::min(low3[c],float(p[c]));high0[c]=std::max(high0[c],float(p[c]));}
        if(options.streaming&&!options.region.Overlaps(low3,high0,options.WaterReachLimit()))return true;
        const bool further=options.streaming&&!options.region.Overlaps(low3,high0,OwnedWorldRegion::WaterReach);
        reflection::WaterSurface surface;
        surface.key=key;
        if (!reflection::FitWaterTile(points,surface,0)) {
            if(further)return true;
            return Fail(SceneError::Water);
        }
        // A valid steep spillway is unsupported as a receiver, not
        // a corrupt scene. Keep the surrounding pools and scenery;
        // the coverage mask leaves this surface on its fallback.
        if (surface.normal[2]<.25) { if(!further)++scene.omittedWaterSurfaces; return true; }
        // All of a region's water takes its place by distance, so water that fills the budget within reach (Northrend's seas lie in
        // layers, twice 8,192 squares) loses its furthest, not whichever tile was read last. Only further water may be left out unlisted.
        if(options.streaming){
            if(furtherWater.size()==MaxFurtherWater){
                WithheldWater(surface);return further?true:Fail(SceneError::Budget);
            }
            if(furtherWater.size()==furtherWater.capacity()){
                const auto capacity=std::min(MaxFurtherWater,std::max<std::size_t>(256,furtherWater.capacity()*2));
                if(!Fits(inputBytes+(capacity-furtherWater.capacity())*sizeof(FurtherWater)))return false;
                furtherWater.reserve(capacity);
            }
            furtherWater.push_back({std::max(std::abs(points[0][0]-options.region.Centre(0)),std::abs(points[0][1]-options.region.Centre(1))),surface,type});
            return true;
        }
        if(scene.region->water.size()==MaxWater)return Fail(SceneError::Budget);
        scene.region.Edit().water.push_back(surface); scene.region.Edit().waterTypes.push_back(type);
        return true;
    }
    // A rectangle of level, contiguous terrain water has the same plane, footprint
    // and authored edges as its constituent quads. Keep holes and slopes exact.
    bool TerrainWater(const std::array<float,3>& origin,uint64_t key,uint16_t type,
                      uint64_t wet,const std::array<float,81>& heights)
    {
        constexpr double unit=1600.0/3/128;
        const auto bit=[](unsigned x,unsigned y){return uint64_t(1)<<(y*8+x);};
        for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
            if(!(wet&bit(x,y)))continue;
            unsigned w=1,h=1;
            const float level=heights[y*9+x];
            const auto levelTile=[&](unsigned col,unsigned row){
                const unsigned k=row*9+col;
                return (wet&bit(col,row)) && heights[k]==level && heights[k+1]==level &&
                       heights[k+9]==level && heights[k+10]==level;
            };
            if(options.streaming && levelTile(x,y)){
                while(x+w<8 && levelTile(x+w,y))++w;
                while(y+h<8){
                    unsigned col=0;
                    while(col<w && levelTile(x+col,y+h))++col;
                    if(col!=w)break;
                    ++h;
                }
            }
            const uint64_t rowMask=(uint64_t(1)<<w)-1;
            for(unsigned row=y;row<y+h;++row)wet&=~(rowMask<<(row*8+x));
            std::array<std::array<double,3>,4> points{};
            for(unsigned corner=0;corner<4;++corner){
                const unsigned col=x+((corner&1)?w:0),row=y+((corner&2)?h:0);
                points[corner]={double(origin[0])-row*unit,double(origin[1])-col*unit,heights[row*9+col]};
            }
            if(!WaterQuad(points,key|(y*8+x),type))return false;
        }
        return true;
    }
    bool LegacyWater(Bytes cell,const AdtTile& tile,unsigned ordinal,unsigned index)
    {
        std::vector<LegacyLiquid> liquids;
        if(!DecodeLegacyLiquid(cell,liquids))return Fail(SceneError::Water);
        const auto& origin=tile.cells[index].vertices[0].position;
        for(unsigned layer=0;layer<liquids.size();++layer){
            const auto& liquid=liquids[layer];
            uint64_t wet=0;
            for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x){
                if(!liquid.Drawn(x,y))continue;
                const unsigned base=y*9+x;
                bool known=true;
                for(unsigned c:{base,base+1,base+9,base+10})
                    known=known && std::isfinite(liquid.heights[c]) && std::abs(liquid.heights[c])<1e6f;
                if(known)wet|=uint64_t(1)<<(y*8+x);
            }
            const uint64_t key=(uint64_t(ordinal+1)<<32)|(uint64_t(index)<<12)|(layer<<6);
            if(!TerrainWater(origin,key,liquid.type,wet,liquid.heights))return false;
        }
        return true;
    }
    bool Water(Bytes input,const AdtTile& tile,unsigned ordinal,std::size_t scratch)
    {
        Bytes water; std::array<bool,256> legacy{}; std::array<Bytes,256> cells{};
        for (std::size_t at=0;at<input.size;) {
            if (!input.Contains(at,8)) return Fail(SceneError::Water);
            const auto size=U32(input,at+4);
            if (!input.Contains(at+8,size)) return Fail(SceneError::Water);
            if (!std::memcmp(input.data+at,"KNCM",4)) {
                Bytes cell{input.data+at+8,size};
                const auto x=U32(cell,4),y=U32(cell,8);
                if (size<128 || x>=16 || y>=16) return Fail(SceneError::Water);
                legacy[y*16+x]=U32(cell,96)!=0 && U32(cell,100)>8;
                cells[y*16+x]=cell;
            }
            if (!std::memcmp(input.data+at,"O2HM",4)) {
                if (water.data) return Fail(SceneError::Water);
                water={input.data+at+8,size};
            }
            at+=8+std::size_t(size);
        }
        if (!water.data && options.streaming) {
            for(unsigned cell=0;cell<256;++cell){
                if(Stop())return false;
                if(legacy[cell]&&!LegacyWater(cells[cell],tile,ordinal,cell))return false;
            }
            return Fits(scratch);
        }
        if (!water.Contains(0,3072)) return Fail(SceneError::Water);
        for (unsigned cell=0;cell<256;++cell) {
            if (Stop()) return false;
            const auto layers=U32(water,cell*12+4), offset=U32(water,cell*12);
            if (!layers) { if (legacy[cell] && !LegacyWater(cells[cell],tile,ordinal,cell)) return false; continue; }
            if (layers>64 || offset<3072 || !water.Contains(offset,std::size_t(layers)*24)) return Fail(SceneError::Water);
            const auto& origin=tile.cells[cell].vertices[0].position;
            for (unsigned layer=0;layer<layers;++layer) {
                const auto at=std::size_t(offset)+layer*24;
                const auto type=U16(water,at), format=U16(water,at+2);
                const unsigned ox=water.data[at+12],oy=water.data[at+13],w=water.data[at+14],h=water.data[at+15];
                const auto mask=U32(water,at+16), vertices=U32(water,at+20);
                float low=0,high=0;
                if (!w || !h || ox+w>8 || oy+h>8 || format>2 || !Float(water,at+4,low) ||
                    !Float(water,at+8,high) || low>high ||
                    (mask && (mask<3072 || !water.Contains(mask,(w*h+7)/8))) ||
                    (vertices && format!=2 && (vertices<3072 || !water.Contains(vertices,(w+1)*(h+1)*4))))
                    return Fail(SceneError::Water);
                uint64_t wet=0;
                std::array<float,81> heights{};
                heights.fill(low);
                for (unsigned y=0;y<h;++y) for (unsigned x=0;x<w;++x) {
                    const unsigned bit=y*w+x;
                    if (mask && !(water.data[mask+bit/8]&(1u<<(bit%8)))) continue;
                    wet|=uint64_t(1)<<((oy+y)*8+ox+x);
                    if(vertices && format!=2){
                        for(unsigned c=0;c<4;++c){
                            const unsigned row=y+(c>>1),col=x+(c&1);
                            if(!Float(water,vertices+(row*(w+1)+col)*4,heights[(oy+row)*9+ox+col]))return Fail(SceneError::Water);
                        }
                    }
                }
                const uint64_t key=(uint64_t(ordinal+1)<<32)|(uint64_t(cell)<<12)|(layer<<6);
                if(!TerrainWater(origin,key,type,wet,heights))return false;
            }
        }
        return Fits(scratch);
    }
    bool Building(const AdtWmoPlacement& p)const
    {return options.streaming?options.region.Overlaps(p.minimum,p.maximum,options.buildingReach>0?options.buildingReach:options.SourceReachLimit()):OwnedRiverRegion::Building(p.minimum,p.maximum,options.lake);}
    bool Model(const AdtModelPlacement& p)const
    {
        if(!options.streaming)return true;
        std::array<float,16> transform;
        return PlacementMatrix(p.pose,transform)&&options.region.Contains(transform.data()+12,options.modelReach>0?options.modelReach:options.SourceReachLimit()+64);
    }
    unsigned ModelDetail(const StaticModel& mesh,const PlacementPose& pose)const
    {
        if(!options.streaming)return 0;
        std::array<float,16> transform;std::array<float,3> low,high;
        if(!PlacementMatrix(pose,transform)||!TransformBounds(mesh.minimum,mesh.maximum,transform.data(),low,high))return MissingSceneAsset;
        if(!options.region.Overlaps(low,high,options.SourceReachLimit()))return MissingSceneAsset;
        if(options.region.Overlaps(low,high,OwnedWorldRegion::NearReach))return 0;
        const float distance=std::max(std::abs(transform[12]-options.region.Centre(0)),std::abs(transform[13]-options.region.Centre(1)));
        float diameter=0;for(unsigned axis=0;axis<3;++axis)diameter=std::max(diameter,high[axis]-low[axis]);
        // Subpixel ground clutter is not a distant silhouette. Large props and
        // trees retain their geometry; authored lower mips reduce material cost.
        if(diameter<distance*.005f)return MissingSceneAsset;
        return distance<=OwnedWorldRegion::MiddleReach?128:64;
    }
    unsigned ModelDetail(const StaticModel& mesh,const std::string& name)const
    {
        if(!options.streaming)return 0;
        unsigned detail=MissingSceneAsset;
        for(const auto& terrain:scene.region->terrain)for(const auto& p:terrain.tile.placements)
            if(terrain.tile.models[p.model]==name && Model(p)){
                const auto selected=ModelDetail(mesh,p.pose);
                if(selected==0)return 0;
                if(selected!=MissingSceneAsset)detail=detail==MissingSceneAsset?selected:std::max(detail,selected);
            }
        return detail;
    }
    unsigned BuildingDetail(const std::string& name,const StaticModel& mesh)const
    {
        if(!options.streaming)return 0;
        unsigned detail=MissingSceneAsset;
        for(const auto& terrain:scene.region->terrain)for(const auto& p:terrain.tile.wmoInstances)
            if(terrain.tile.wmoModels[p.model]==name && Building(p)){
                std::array<float,16> transform;std::array<float,3> low,high;
                if(!PlacementMatrix(p.pose,transform)||!TransformBounds(mesh.minimum,mesh.maximum,transform.data(),low,high))continue;
                if(options.region.Overlaps(low,high,OwnedWorldRegion::NearReach))return 0;
                if(options.region.Overlaps(low,high,options.SourceReachLimit())){
                    const unsigned selected=options.region.Overlaps(low,high,OwnedWorldRegion::MiddleReach)?128:64;
                    detail=detail==MissingSceneAsset?selected:std::max(detail,selected);
                }
            }
        return detail;
    }
    bool MapMetadata()
    {
        const std::string map=OwnedWorldRegion::Directory(options.region.map);
        ArchiveEntry entry;if(!Read("world\\maps\\"+map+"\\"+map+".wdt",entry))return false;
        Bytes bytes{entry.bytes.data(),entry.bytes.size()};bool seen=false,version=false;
        for(std::size_t at=0;at<bytes.size;) {
            if(!bytes.Contains(at,8)) {
                if(bytes.size-at>3)return Fail(SceneError::Descriptor,entry.name);
                for(;at<bytes.size;++at)if(bytes.data[at])return Fail(SceneError::Descriptor,entry.name);
                break;
            }
            const auto size=U32(bytes,at+4);if(!bytes.Contains(at+8,size))return Fail(SceneError::Descriptor,entry.name);
            if(!std::memcmp(bytes.data+at,"REVM",4))version=size==4&&U32(bytes,at+8)==18;
            if(!std::memcmp(bytes.data+at,"DHPM",4)) {
                if(seen||size<32)return Fail(SceneError::Descriptor,entry.name);
                seen=true;const auto flags=U32(bytes,at+8);
                if(flags&1)return Fail(SceneError::Descriptor,entry.name); // Global-WMO maps have no terrain window.
                alpha=flags&4?AdtAlphaFormat::Byte8:AdtAlphaFormat::Packed4;
            }
            at+=8+std::size_t(size);
        }
        if(!seen||!version)return Fail(SceneError::Descriptor,entry.name);
        return Source(entry);
    }
    bool Tiles()
    {
        unsigned ordinal=0;
        if(options.streaming&&!MapMetadata())return false;
        std::vector<std::string> names;
        if(options.streaming)names=options.region.Tiles(options.SourceReachLimit());else for(const auto* name : ::water::world::Tiles)names.emplace_back(name);
        if(names.empty()||names.size()>options.TileLimit())return Fail(SceneError::Descriptor);
        for (const auto& name : names) {
            ArchiveEntry entry;
            if (!Read(name,entry)) {
                if(options.streaming&&result.archiveError==ArchiveError::Missing){result.error=SceneError::None;result.archiveError=ArchiveError::None;result.asset.clear();continue;}
                return false;
            }
            SceneTerrain terrain;
            const auto limit=DecodeLimit(entry.bytes.capacity());
            const auto error=DecodeAdt({entry.bytes.data(),entry.bytes.size()},alpha,limit,terrain.tile);
            if (error!=AdtError::None) return Fail(error==AdtError::Budget ? SceneError::Budget : SceneError::Terrain,name);
            if (!Fits(entry.bytes.capacity(),terrain.tile.Bytes())) return false;
            // Install immutable geometry before fitting the source water; its
            // residency is then included in every subsequent admission check.
            scene.region.Edit().terrain.push_back(std::move(terrain));
            if (!Water({entry.bytes.data(),entry.bytes.size()},scene.region.Edit().terrain.back().tile,ordinal++,entry.bytes.capacity())) {
                if(result.asset.empty())result.asset=entry.name;return false;
            }
            if(!Source(entry))return false;
            auto& target=scene.region.Edit().terrain.back();
            if(options.streaming){
                auto& cells=target.tile.cells;
                cells.erase(std::remove_if(cells.begin(),cells.end(),[&](const auto& c){return c.indices.empty()||!options.region.Overlaps(c.minimum,c.maximum,options.SourceReachLimit());}),cells.end());
                for(auto& cell:cells){
                    const bool detailed=options.region.Overlaps(cell.minimum,cell.maximum,OwnedWorldRegion::NearReach);
                    const bool middle=options.region.Overlaps(cell.minimum,cell.maximum,OwnedWorldRegion::MiddleReach);
                    if(!Fits(inputBytes,8192))return false;
                    if(!ReduceTerrainCell(cell,detailed?1:middle?4:8,detailed?64:middle?16:4))return Fail(SceneError::Terrain,name);
                }
                cells.shrink_to_fit();
            }

            if (!Fits(0,target.tile.textures.size()*sizeof(uint32_t))) return false;
            target.textures.resize(target.tile.textures.size(),MissingSceneAsset);
            std::vector<std::string> layerTextures(target.tile.textures.size());
            for (const auto& cell : target.tile.cells) if(!cell.indices.empty())for (const auto& layer : cell.layers) {
                auto& chosen=layerTextures[layer.texture];
                if (chosen.empty() && !TerrainTexture(target.tile.textures[layer.texture],chosen)) return false;
                auto& texture=target.textures[layer.texture];
                texture=Texture(chosen,cell.alphaSize==64?0:cell.alphaSize==16?128:64);
                if (texture==MissingSceneAsset) return false;
            }
            scene.omittedWmoReferences+=target.tile.wmoPlacements;
            if(options.buildings)for(const auto& placement:target.tile.wmoInstances) {
                if(!Building(placement))continue;
                const auto& name=target.tile.wmoModels[placement.model];
                if(Find(scene.wmoSources,name)==MissingSceneAsset) {
                    if(scene.wmoSources.size()==MaxSceneBuildings)return Fail(SceneError::Budget,name);
                    if(!Fits(0,NameBytes(name)))return false;
                    scene.wmoSources.push_back({name,{},WmoError::None,0,{}});
                }
            }
            for (const auto& placement : target.tile.placements) {
                if(!Model(placement))continue;
                const auto& model=target.tile.models[placement.model];
                if (Find(scene.modelSources,model)!=MissingSceneAsset) continue;
                if (scene.modelSources.size()==MaxModelSources) return Fail(SceneError::Budget,model);
                scene.modelSources.push_back({model,MissingSceneAsset,ModelError::None});
            }
            FinishTiles();
            if (!Fits()) return false;
        }
        inputBytes=0;return true;
    }
    bool LiveAssets()
    {
        if(std::none_of(options.liveDisplays.begin(),options.liveDisplays.end(),[](auto id){return id!=0;}))return true;
        ArchiveEntry entry;if(!Read("dbfilesclient\\gameobjectdisplayinfo.dbc",entry))return false;
        const Bytes dbc{entry.bytes.data(),entry.bytes.size()};
        if(!dbc.Contains(0,20)||std::memcmp(dbc.data,"WDBC",4))return Fail(SceneError::Descriptor,entry.name);
        const auto rows=U32(dbc,4),fields=U32(dbc,8),stride=U32(dbc,12),strings=U32(dbc,16);
        if(rows>100000 || fields<2 || stride<8 || stride>4096 || !dbc.Contains(20,std::size_t(rows)*stride) ||
           !dbc.Contains(20+std::size_t(rows)*stride,strings) || 20+std::size_t(rows)*stride+strings!=dbc.size)return Fail(SceneError::Descriptor,entry.name);
        const auto table=20+std::size_t(rows)*stride;
        for(auto display:options.liveDisplays)if(display){
            if(std::any_of(scene.liveAssets.begin(),scene.liveAssets.end(),[&](const auto& a){return a.display==display;}))continue;
            std::string name;
            for(unsigned i=0;i<rows;++i)if(U32(dbc,20+std::size_t(i)*stride)==display){
                const auto offset=U32(dbc,20+std::size_t(i)*stride+4);if(offset>=strings)break;
                const char* begin=reinterpret_cast<const char*>(dbc.data+table+offset);
                const auto* end=static_cast<const char*>(std::memchr(begin,0,strings-offset));
                if(end)CanonicalAssetName(std::string(begin,end),name);break;
            }
            // Unsupported live displays do not discard otherwise valid static scenery.
            if(name.size()<4 || name.substr(name.size()-4)!=".wmo")continue;
            auto source=Find(scene.wmoSources,name);
            if(source==MissingSceneAsset){
                if(scene.wmoSources.size()==MaxSceneBuildings)return Fail(SceneError::Budget,name);
                source=unsigned(scene.wmoSources.size());scene.wmoSources.push_back({name,{},WmoError::None,0,{}});
            }
            scene.liveAssets.push_back({display,source});
        }
        return Source(entry);
    }
    bool Models(bool attachments=false)
    {
        for (std::size_t i=0;i<scene.modelSources.size();++i) {
            auto& source=scene.modelSources[i];
            if(attachments){
                bool referenced=false;
                for(const auto& building:scene.wmoSources)for(const auto& attachment:building.attachments)
                    referenced=referenced||attachment.source==i;
                if(!referenced)continue;
                if(source.model!=MissingSceneAsset){
                    // A prop can serve both a distant placement and a nearby
                    // live ship. Its closest consumer owns material detail.
                    auto& existing=scene.models[source.model];
                    for(const auto& draw:existing.geometry->mesh.draws)
                        if(Texture(existing.geometry->mesh.textures[draw.texture].name)==MissingSceneAsset)return false;
                    continue;
                }
            }
            ArchiveEntry model,skin;
            if (!Read(source.name,model) || !Read(source.name.substr(0,source.name.size()-3)+"00.skin",skin,model.bytes.capacity()))
                return false;
            const auto scratch=model.bytes.capacity()+skin.bytes.capacity();
            SceneModel decoded;
            auto error=attachments?DecodeStaticModel({model.bytes.data(),model.bytes.size()},{skin.bytes.data(),skin.bytes.size()},
                                        DecodeLimit(scratch),decoded.geometry.Edit().mesh):
                DecodeRestModel({model.bytes.data(),model.bytes.size()},{skin.bytes.data(),skin.bytes.size()},
                                        DecodeLimit(scratch),decoded.geometry.Edit().mesh);
            if(attachments && error==ModelError::Pose)
                error=DecodeLoopingModel({model.bytes.data(),model.bytes.size()},{skin.bytes.data(),skin.bytes.size()},
                                        DecodeLimit(scratch),decoded.geometry.Edit().mesh,decoded.geometry.Edit().motion);
            if (error==ModelError::Budget) return Fail(SceneError::Budget,source.name);
            source.unsupported=error;
            const auto detail=error!=ModelError::None?MissingSceneAsset:attachments?0:ModelDetail(decoded.geometry->mesh,source.name);
            if (error==ModelError::None && detail!=MissingSceneAsset) {
                if(scene.models.size()==MaxModels)return Fail(SceneError::Budget,source.name);
                pendingBytes=decoded.geometry->mesh.Bytes()+decoded.geometry->motion.Bytes()+decoded.geometry->mesh.textures.size()*sizeof(uint32_t);
                if (!Fits(scratch)) return false;
                source.model=static_cast<uint32_t>(scene.models.size());
                decoded.textures.resize(decoded.geometry->mesh.textures.size(),MissingSceneAsset);
                pendingBytes=decoded.geometry->mesh.Bytes()+decoded.geometry->motion.Bytes()+decoded.textures.capacity()*sizeof(uint32_t);
                for (const auto& draw : decoded.geometry->mesh.draws) {
                    auto& texture=decoded.textures[draw.texture];
                    if (texture==MissingSceneAsset) texture=Texture(decoded.geometry->mesh.textures[draw.texture].name,detail);
                    if (texture==MissingSceneAsset) return false;
                }
                scene.models.push_back(std::move(decoded)); pendingBytes=0;
            }
            if (!Source(model) || !Source(skin)) return false;
        }
        inputBytes=0;
        if(!options.streaming)for (const auto* name : Required) {
            const auto found=Find(scene.modelSources,name);
            if (found==MissingSceneAsset || scene.modelSources[found].model==MissingSceneAsset) return Fail(SceneError::Required,name);
        }
        return true;
    }
    bool BuildingWater(Bytes input,const std::string& name,unsigned group)
    {
        WmoLiquid liquid;
        if(DecodeWmoLiquid(input,liquid)!=WmoError::None)return Fail(SceneError::Water,name);
        if(!liquid.width)return true;
        // ADT borders repeat a building placement. Its water is admitted once,
        // in the same world transform as its scenery, before receiver selection.
        std::vector<const AdtWmoPlacement*> seen;
        for(const auto& terrain:scene.region->terrain)for(const auto& placement:terrain.tile.wmoInstances){
            if((placement.flags&~4u)||!Building(placement)||terrain.tile.wmoModels[placement.model]!=name)continue;
            const auto previous=std::find_if(seen.begin(),seen.end(),[&](const auto* p){return p->uid==placement.uid;});
            if(previous!=seen.end()){
                const auto& pose=(*previous)->pose;
                if(pose.position!=placement.pose.position||pose.rotation!=placement.pose.rotation||pose.scale!=placement.pose.scale)
                    return Fail(SceneError::Placement,name);
                continue;
            }
            if(seen.size()==MaxInstances||!Fits(inputBytes+(seen.size()+1)*2*sizeof(seen[0])))return Fail(SceneError::Budget,name);
            seen.push_back(&placement);
            std::array<float,16> transform;
            if(!PlacementMatrix(placement.pose,transform))return Fail(SceneError::Placement,name);
            for(unsigned y=0;y<liquid.height;++y){
                if(Stop())return false;
                for(unsigned x=0;x<liquid.width;++x){
                    std::array<std::array<double,3>,4> local,world;
                    if(!liquid.Quad(x,y,local))continue;
                    for(unsigned v=0;v<4;++v)for(unsigned c=0;c<3;++c)
                        world[v][c]=transform[12+c]+local[v][0]*transform[c]+local[v][1]*transform[4+c]+local[v][2]*transform[8+c];
                    const uint64_t key=(uint64_t(1)<<63)|(uint64_t(placement.uid)<<25)|(uint64_t(group)<<16)|(y*liquid.width+x);
                    if(!WaterQuad(world,key,liquid.type))return false;
                }
            }
        }
        return true;
    }
    bool Buildings(unsigned firstSource=0)
    {
        // Admit every supported group through the shared model, scene and
        // workspace budgets. A root's group count alone is not its cost.
        for(std::size_t sourceIndex=firstSource;sourceIndex<scene.wmoSources.size();++sourceIndex) {
            auto& source=scene.wmoSources[sourceIndex];
            const bool live=std::any_of(scene.liveAssets.begin(),scene.liveAssets.end(),[&](const auto& a){return a.source==sourceIndex;});
            ArchiveEntry rootEntry;if(!Read(source.name,rootEntry))return false;
            WmoRoot root;
            auto error=DecodeWmoRoot({rootEntry.bytes.data(),rootEntry.bytes.size()},DecodeLimit(rootEntry.bytes.capacity()),root);
            if(error==WmoError::Budget)return Fail(SceneError::Budget,source.name);
            WmoAttachments attachments;
            if(live&&error==WmoError::None){
                const auto cap=DecodeLimit(rootEntry.bytes.capacity());
                source.attachmentError=DecodeWmoAttachments({rootEntry.bytes.data(),rootEntry.bytes.size()},cap>root.Bytes()?cap-root.Bytes():0,attachments);
                if(source.attachmentError==WmoError::Budget)return Fail(SceneError::Budget,source.name);
            }
            pendingBytes=root.Bytes()+attachments.Bytes();
            if(!Source(rootEntry))return false;
            std::vector<uint8_t>().swap(rootEntry.bytes);inputBytes=0;
            if(error!=WmoError::None){source.unsupported=error;pendingBytes=0;continue;}
            source.omittedDoodads=root.doodads;
            if(!Fits(0,root.Bytes()+attachments.Bytes()+attachments.models.size()))return false;
            std::vector<uint8_t> referenced(attachments.models.size());
            const auto rootBytes=root.Bytes()+attachments.Bytes()+referenced.capacity();
            if(root.groups>MaxModels-scene.models.size())return Fail(SceneError::Budget,source.name);
            pendingBytes=rootBytes;
            if(!Fits(0,rootBytes+root.groups*sizeof(uint32_t)))return false;
            source.models.reserve(root.groups);
            const auto first=scene.models.size();
            for(unsigned i=0;i<root.groups;++i) {
                if(Stop())return false;
                char suffix[16];std::snprintf(suffix,sizeof(suffix),"_%03u.wmo",i);
                const auto name=source.name.substr(0,source.name.size()-4)+suffix;
                ArchiveEntry entry;if(!Read(name,entry))return false;
                if(!BuildingWater({entry.bytes.data(),entry.bytes.size()},source.name,i))return false;
                // Unsupported scenery does not discard valid liquid in later groups.
                if(source.unsupported!=WmoError::None){if(!Source(entry))return false;continue;}
                SceneModel group;
                const auto cap=DecodeLimit(entry.bytes.capacity());
                std::vector<uint16_t> references;
                error=DecodeWmoGroup({entry.bytes.data(),entry.bytes.size()},root,cap>rootBytes?cap-rootBytes:0,group.geometry.Edit().mesh);
                if(error==WmoError::Budget)return Fail(SceneError::Budget,name);
                if(live&&source.attachmentError==WmoError::None){
                    const auto occupied=rootBytes+group.geometry->mesh.Bytes();
                    source.attachmentError=DecodeWmoReferences({entry.bytes.data(),entry.bytes.size()},static_cast<uint32_t>(attachments.models.size()),cap>occupied?cap-occupied:0,references);
                    if(source.attachmentError==WmoError::Budget)return Fail(SceneError::Budget,name);
                }
                if(error==WmoError::None) {
                    auto detail=live?0:BuildingDetail(source.name,group.geometry->mesh);
                    // Inside a building beyond the near reach, nothing shows in a reflection: most of Dalaran's bytes are rooms.
                    if(detail!=0&&WmoGroupOnlyIndoors({entry.bytes.data(),entry.bytes.size()}))detail=MissingSceneAsset;
                    if(detail==MissingSceneAsset){
                        pendingBytes=rootBytes+references.capacity()*sizeof(uint16_t);
                        if(!Source(entry))return false;
                        continue;
                    }
                    pendingBytes=rootBytes+group.geometry->mesh.Bytes()+group.geometry->mesh.textures.size()*sizeof(uint32_t)+references.capacity()*sizeof(uint16_t);
                    if(!Fits(entry.bytes.capacity()))return false;
                    group.textures.resize(group.geometry->mesh.textures.size(),MissingSceneAsset);
                    for(const auto& draw:group.geometry->mesh.draws) {
                        auto& texture=group.textures[draw.texture];
                        if(texture==MissingSceneAsset)texture=Texture(group.geometry->mesh.textures[draw.texture].name,detail);
                        if(texture==MissingSceneAsset)return false;
                    }
                    for(auto index:references)referenced[index]=1;
                    source.models.push_back(static_cast<uint32_t>(scene.models.size()));scene.models.push_back(std::move(group));
                }
                pendingBytes=rootBytes+references.capacity()*sizeof(uint16_t);
                if(!Source(entry))return false;
                if(error!=WmoError::None){scene.models.resize(first);source.models.clear();source.unsupported=error;}
            }
            if(error!=WmoError::None){scene.models.resize(first);source.models.clear();source.unsupported=error;}
            if(error==WmoError::None&&live&&source.attachmentError==WmoError::None){
                const auto count=std::count(referenced.begin(),referenced.end(),uint8_t(1));
                if(!Fits(0,rootBytes+count*sizeof(SceneWmoSource::Attachment)))return false;
                source.attachments.reserve(count);
                for(unsigned i=0;i<attachments.models.size();++i){
                    const auto& a=attachments.models[i];if(!referenced[i]||a.set!=0)continue;
                    auto model=Find(scene.modelSources,a.name);
                    if(model==MissingSceneAsset){
                        if(scene.modelSources.size()==MaxModelSources)return Fail(SceneError::Budget,a.name);
                        model=static_cast<uint32_t>(scene.modelSources.size());
                        scene.modelSources.push_back({a.name,MissingSceneAsset,ModelError::None});
                    }
                    source.attachments.push_back({model,a.transform});
                    if(!Fits())return false;
                }
            }
            pendingBytes=0;inputBytes=0;
            if(!Fits())return false;
        }
        if(!Models(true))return false;
        for(unsigned i=firstSource;i<scene.wmoSources.size();++i){auto& source=scene.wmoSources[i];
            auto& attachments=source.attachments;
            attachments.erase(std::remove_if(attachments.begin(),attachments.end(),[&](const auto& a){return scene.modelSources[a.source].model==MissingSceneAsset;}),attachments.end());
            source.omittedDoodads-=static_cast<uint32_t>(attachments.size());
        }
        return true;
    }
    bool LoadTexture(SceneTexture& texture,TextureCache& cache,ArchiveEntry& entry,world::Texture& decoded,TextureError& error)
    {
        using Clock=std::chrono::steady_clock;
        const auto elapsed=[](Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();};
        const auto policy=options.texturePrefix ? TextureMips::RenderPrefix : TextureMips::StoredPrefix;
        if(Stop())return false;
        inputBytes=0;
        if(cache.Bytes()) {
            const auto start=Clock::now();TexturePackReadStats stats;
            const auto resident=RetainedBytes();
            const auto available=resident<=workspaceLimit && cache.Bytes()<=workspaceLimit-resident ?
                std::min(sceneLimit-resident,workspaceLimit-resident-cache.Bytes()) : 0;
            const auto loaded=cache.Read({texture.name,texture.maxDimension,options.textureMipBias,policy},cancelled,available,decoded,entry.source,stats);
            result.textureCacheMilliseconds+=elapsed(start);result.textureCacheBytes+=stats.inputBytes;
            result.peakWorkspaceBytes=std::max(result.peakWorkspaceBytes,resident+cache.Bytes()+stats.peakWorkspaceBytes);
            if(loaded==TexturePackError::None) {
                ++result.textureCacheHits;result.textureCacheBuckets|=uint64_t(1)<<TextureStore::Bucket(texture.name);entry.name=texture.name;error=TextureError::None;
                return Fits(cache.Bytes(),decoded.Bytes());
            }
            result.textureCacheLast=loaded;
            if(loaded==TexturePackError::Cancelled)return Fail(SceneError::Cancelled);
            if(loaded!=TexturePackError::Missing)++result.textureCacheFailures;
            // Release optional cache overhead before source fallback. Missing keys
            // keep the index for later hits, unless source admission needs its bytes.
            if(loaded!=TexturePackError::Missing)cache.Close();
        }
        if(!options.texturePack.empty() || !options.textureCacheDirectory.empty())++result.textureCacheMisses;
        auto start=Clock::now();
        bool read=Read(texture.name,entry,cache.Bytes());
        if(!read && result.error==SceneError::Budget && cache.Bytes()) {
            cache.Close();entry=ArchiveEntry{};
            ++result.textureCacheFailures;result.textureCacheLast=TexturePackError::Budget;
            result.error=SceneError::None;result.archiveError=ArchiveError::None;result.asset.clear();
            read=Read(texture.name,entry);
        }
        result.textureSourceMilliseconds+=elapsed(start);
        if(!read)return false;
        start=Clock::now();
        error=DecodeBlp({entry.bytes.data(),entry.bytes.size()},DecodeLimit(entry.bytes.capacity()+cache.Bytes()),decoded,
                        policy,texture.maxDimension,options.textureMipBias);
        if(error==TextureError::Budget && cache.Bytes()) {
            cache.Close();inputBytes=entry.bytes.capacity();
            ++result.textureCacheFailures;result.textureCacheLast=TexturePackError::Budget;
            result.error=SceneError::None;result.asset.clear();
            error=DecodeBlp({entry.bytes.data(),entry.bytes.size()},DecodeLimit(entry.bytes.capacity()),decoded,
                            policy,texture.maxDimension,options.textureMipBias);
        }
        result.textureDecodeMilliseconds+=elapsed(start);
        if(error==TextureError::Budget)return Fail(SceneError::Budget,texture.name);
        return Fits(entry.bytes.capacity()+cache.Bytes(),decoded.Bytes());
    }
    bool Textures(const std::vector<unsigned>* previousDimensions=nullptr)
    {
        TextureCache cache;inputBytes=0;
        if(!options.texturePack.empty() || !options.textureCacheDirectory.empty()) {
            const auto start=std::chrono::steady_clock::now();
            result.textureCacheOpen=options.texturePack.empty() ?
                cache.OpenDirectory(archives,options.textureCacheDirectory,cancelled,workspaceLimit-RetainedBytes()) :
                cache.Open(archives,options.texturePack,cancelled,workspaceLimit-RetainedBytes());
            result.textureCacheMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            if(result.textureCacheOpen==TexturePackError::Cancelled)return Fail(SceneError::Cancelled);
            if(workspaceLimit-RetainedBytes()>=TextureCache::OpenReservation)
                result.peakWorkspaceBytes=std::max(result.peakWorkspaceBytes,RetainedBytes()+TextureCache::OpenReservation);
        }
        for (unsigned index=0;index<scene.textures.size();++index) {
            auto& texture=scene.textures[index];ArchiveEntry entry;world::Texture decoded;TextureError error;
            if(previousDimensions && index<previousDimensions->size() &&
               texture.maxDimension==(*previousDimensions)[index])continue;
            const auto hits=result.textureCacheHits;
            if(!LoadTexture(texture,cache,entry,decoded,error))return false;
            // A promoted shared material must not retire/reindex an already
            // prepared static model. Retry the complete import if it is refused.
            if(previousDimensions && index<previousDimensions->size() && error!=TextureError::None)
                return Fail(SceneError::Texture,texture.name);
            if(options.populateTextures && error==TextureError::None && result.textureCacheHits==hits)
                result.textureBakeCandidates.push_back(index);
            textureErrors[index]=error;
            if (error!=TextureError::None) scene.textureIssues.push_back({texture.name,error});
            else if(decoded.rejectedMipLevel) scene.textureIssues.push_back({texture.name,TextureError::None,
                static_cast<unsigned>(decoded.mips.size()),decoded.rejectedMipLevel});
            inputBytes=entry.bytes.capacity()+cache.Bytes();
            texture.pixels.Replace(std::move(decoded));texture.contentKey={};
            if (!Source(entry)) {
                if(result.error!=SceneError::Budget || !cache.Bytes())return false;
                cache.Close();inputBytes=entry.bytes.capacity();
                ++result.textureCacheFailures;result.textureCacheLast=TexturePackError::Budget;
                result.error=SceneError::None;result.asset.clear();
                if(!Fits())return false;
            }
        }
        cache.Close();inputBytes=0;
        // Terrain and the named comparison landmarks are mandatory. Optional
        // models with unsupported material inputs are classified like unsupported
        // poses; no empty texture or altered material reaches the GPU scene.
        for (const auto& terrain : scene.region->terrain) for (const auto& cell : terrain.tile.cells) if(!cell.indices.empty())for (const auto& layer : cell.layers) {
            const auto texture=terrain.textures[layer.texture];
            if (textureErrors[texture]!=TextureError::None) return Fail(SceneError::Texture,scene.textures[texture].name);
        }
        // A refused optional WMO material retires its whole root, including
        // groups that happen to use other textures. Never publish half a house.
        std::array<bool,MaxModels> omittedBuildings{};
        for(auto& source:scene.wmoSources) {
            bool bad=false;
            for(auto model:source.models)for(auto texture:scene.models[model].textures)
                if(texture!=MissingSceneAsset&&textureErrors[texture]!=TextureError::None)bad=true;
            if(bad){for(auto model:source.models)omittedBuildings[model]=true;source.models.clear();source.omittedDoodads+=static_cast<uint32_t>(source.attachments.size());source.attachments.clear();source.unsupported=WmoError::Material;}
        }
        for (std::size_t i=scene.models.size();i--;) {
            uint32_t bad=MissingSceneAsset;
            for (const auto texture : scene.models[i].textures)
                if (texture!=MissingSceneAsset && textureErrors[texture]!=TextureError::None) { bad=texture;break; }
            if (bad==MissingSceneAsset && !omittedBuildings[i]) continue;
            for (auto& source : scene.modelSources) if (source.model==i) {
                if(!options.streaming)for (const auto* required : Required) if (source.name==required) return Fail(SceneError::Required,scene.textures[bad].name);
                source.model=MissingSceneAsset;source.unsupported=ModelError::Material;
            }
            scene.models.erase(scene.models.begin()+i);
            for (auto& source : scene.modelSources)
                if (source.model!=MissingSceneAsset && source.model>i) --source.model;
            for(auto& source:scene.wmoSources)for(auto& model:source.models)if(model>i)--model;
        }
        for(auto& source:scene.wmoSources){auto& attachments=source.attachments;const auto before=attachments.size();
            attachments.erase(std::remove_if(attachments.begin(),attachments.end(),[&](const auto& a){return scene.modelSources[a.source].model==MissingSceneAsset;}),attachments.end());
            source.omittedDoodads+=static_cast<uint32_t>(before-attachments.size());}
        std::array<bool,MaxTextures> used{};std::array<uint32_t,MaxTextures> remap{};
        remap.fill(MissingSceneAsset);
        for (const auto& terrain : scene.region->terrain) for (auto texture : terrain.textures)
            if (texture!=MissingSceneAsset) used[texture]=true;
        for (const auto& model : scene.models) for (auto texture : model.textures)
            if (texture!=MissingSceneAsset) used[texture]=true;
        unsigned count=0;
        for (unsigned i=0;i<scene.textures.size();++i) if (used[i]) {
            remap[i]=count;if (count!=i) scene.textures[count]=std::move(scene.textures[i]);++count;
        }
        for(auto& index:result.textureBakeCandidates)index=remap[index];
        auto& candidates=result.textureBakeCandidates;
        candidates.erase(std::remove(candidates.begin(),candidates.end(),MissingSceneAsset),candidates.end());
        scene.textures.resize(count); // retire orphaned optional geometry/textures on the CPU worker
        if (previousDimensions) for (unsigned i=0;i<previousDimensions->size();++i)
            if (remap[i]!=i) return Fail(SceneError::Descriptor);
        if (!previousDimensions) for (auto& terrain : scene.region.Edit().terrain) for (auto& texture : terrain.textures)
            if (texture!=MissingSceneAsset) texture=remap[texture];
        for (auto& model : scene.models) for (auto& texture : model.textures)
            if (texture!=MissingSceneAsset) texture=remap[texture];
        return Fits();
    }
    // The instance vector owns ordering and conflict policy. Slots hold its one-based
    // indices only while placing instances; zero is empty even for client UID zero.
    static constexpr unsigned PlacementSlots = 2 * MaxInstances;
    static_assert((PlacementSlots & (PlacementSlots-1)) == 0 && MaxInstances <= UINT16_MAX);
    std::unique_ptr<uint16_t[]> placementSlots{};
    unsigned PlacementSlot(uint32_t uid,uint16_t group) const
    {
        unsigned hash = (uid ^ (uid >> 16)) * 0x9e3779b1u + unsigned(group) * 0x85ebca6bu;
        unsigned slot = (hash ^ (hash >> 16)) & (PlacementSlots-1);
        while (placementSlots[slot]) {
            const auto& instance=scene.region->instances[placementSlots[slot]-1];
            if (instance.uid==uid && instance.group==group) break;
            slot=(slot+1) & (PlacementSlots-1);
        }
        return slot;
    }
    bool Place(uint32_t model,uint32_t uid,const PlacementPose& pose,uint16_t group=0xffff)
    {
        const auto& instances=scene.region->instances;
        const unsigned slot=placementSlots ? PlacementSlot(uid,group) : 0;
        const SceneInstance* duplicate=nullptr;
        if (placementSlots) {
            if (placementSlots[slot]) duplicate=&instances[placementSlots[slot]-1];
        } else {
            // Preserve admission when a caller has room for the scene alone.
            const auto found=std::find_if(instances.begin(),instances.end(),[&](const auto& p){return p.uid==uid && p.group==group;});
            if (found!=instances.end()) duplicate=&*found;
        }
        if(duplicate){
            if(duplicate->model==model&&duplicate->pose.position==pose.position&&duplicate->pose.rotation==pose.rotation&&duplicate->pose.scale==pose.scale)return true;
            // The client's own files reuse some model IDs between different props (Kalimdor, around (40,-30)). The first stands;
            // one reused ID should not cost the whole scene.
            if(group==0xffff){++scene.omittedModelReferences;return true;}
            return Fail(SceneError::Placement);
        }
        SceneInstance instance;instance.model=model;instance.uid=uid;instance.group=group;instance.pose=pose;
        if(!PlacementMatrix(pose,instance.transform))return Fail(SceneError::Placement);
        const auto& mesh=scene.models[model].geometry->mesh;
        if(!TransformBounds(mesh.minimum,mesh.maximum,instance.transform.data(),instance.minimum,instance.maximum))return Fail(SceneError::Placement);
        // A shared group can be retained for another placement. Test this
        // placement too, so a crossing root does not admit remote instances.
        if(options.streaming&&!options.region.Overlaps(instance.minimum,instance.maximum,options.SourceReachLimit()))return true;
        if(scene.region->instances.size()==MaxInstances)return Fail(SceneError::Budget);
        scene.region.Edit().instances.push_back(instance);
        if (placementSlots) placementSlots[slot]=static_cast<uint16_t>(scene.region->instances.size());
        return true;
    }
    bool Instances()
    {
        if (!Fits()) return false;
        const auto scratch=inputBytes+furtherWater.capacity()*sizeof(FurtherWater);
        const auto retained=RetainedBytes();
        constexpr auto indexBytes=PlacementSlots*sizeof(uint16_t);
        if (workspaceLimit-retained-scratch>=indexBytes) {
            placementSlots.reset(new (std::nothrow) uint16_t[PlacementSlots]{});
            if (placementSlots) result.peakWorkspaceBytes=std::max(result.peakWorkspaceBytes,retained+scratch+indexBytes);
        }
        for (const auto& terrain : scene.region->terrain) for (const auto& placement : terrain.tile.placements) {
            if (Stop()) return false;
            if(!Model(placement))continue;
            if (placement.flags) { ++scene.omittedFlagReferences; continue; }
            const auto source=Find(scene.modelSources,terrain.tile.models[placement.model]);
            if (source==MissingSceneAsset) return Fail(SceneError::Placement);
            const auto model=scene.modelSources[source].model;
            if (model==MissingSceneAsset) { ++scene.omittedModelReferences; continue; }
            if(ModelDetail(scene.models[model].geometry->mesh,placement.pose)==MissingSceneAsset){++scene.omittedModelReferences;continue;}
            if(!Place(model,placement.uid,placement.pose))return false;
        }
        if(options.buildings)for(const auto& terrain:scene.region->terrain)for(const auto& placement:terrain.tile.wmoInstances) {
            if(Stop())return false;
            if(placement.flags&~4u){++scene.omittedFlagReferences;continue;}
            if(!Building(placement))continue;
            const auto source=Find(scene.wmoSources,terrain.tile.wmoModels[placement.model]);
            if(source==MissingSceneAsset)return Fail(SceneError::Placement);
            const auto& groups=scene.wmoSources[source].models;if(groups.empty())continue;
            if(scene.omittedWmoReferences)--scene.omittedWmoReferences;
            for(unsigned group=0;group<groups.size();++group)
                if(!Place(groups[group],placement.uid,placement.pose,static_cast<uint16_t>(group)))return false;
        }
        if(!options.streaming)for (const auto* name : Required) {
            const auto model=scene.modelSources[Find(scene.modelSources,name)].model;
            if (std::none_of(scene.region->instances.begin(),scene.region->instances.end(),[&](const auto& p){return p.model==model;})) return Fail(SceneError::Required,name);
        }
        return Fits();
    }
};
}

unsigned SceneTextureMipBias(const WorldScene& scene) { return (scene.sourcePolicy>>5)&1u; }
std::size_t WorldScene::Bytes() const
{
    auto bytes=BytesExcludingTerrainPayload();
    for (const auto& tile : region->terrain) bytes+=TerrainPayloadBytes(tile);
    return bytes;
}
std::size_t WorldScene::BytesExcludingTerrainPayload() const
{
    std::size_t n=sizeof(*this)+NameBytes(sourceIdentity)+region->terrain.capacity()*sizeof(SceneTerrain)+models.capacity()*sizeof(SceneModel)+
        textures.capacity()*sizeof(SceneTexture)+textureIssues.capacity()*sizeof(SceneTextureIssue)+
        modelSources.capacity()*sizeof(SceneModelSource)+wmoSources.capacity()*sizeof(SceneWmoSource)+sources.capacity()*sizeof(SceneSource)+
        region->instances.capacity()*sizeof(SceneInstance)+liveAssets.capacity()*sizeof(SceneLiveAsset)+region->water.capacity()*sizeof(reflection::WaterSurface)+region->waterTypes.capacity()*sizeof(uint16_t);
    n+=sizeof(RegionGeometry)+region->horizon.Bytes()-sizeof(StaticModel);
    for (const auto& m : models) n+=m.geometry->mesh.Bytes()+m.geometry->motion.Bytes()+m.textures.capacity()*sizeof(uint32_t);
    for (const auto& t : textures) n+=t.pixels->Bytes()+NameBytes(t.name);
    for (const auto& m : modelSources) n+=NameBytes(m.name);
    for (const auto& t : textureIssues) n+=NameBytes(t.name);
    for(const auto& source:wmoSources)n+=NameBytes(source.name)+source.models.capacity()*sizeof(uint32_t)+source.attachments.capacity()*sizeof(SceneWmoSource::Attachment);
    for (const auto& s : sources) n+=NameBytes(s.name)+(s.archive.path.capacity()+1)*sizeof(wchar_t);
    return n;
}
SceneResult LoadRiverScene(ArchiveReader& archives,const std::atomic<bool>& cancelled,WorldScene& output,
                          std::size_t sceneLimit,std::size_t workspaceLimit,std::atomic<uint64_t>* sourceProgress,SceneOptions options)
{
    Builder b{archives,cancelled,sceneLimit,workspaceLimit,{},{},0,0,{},sourceProgress,options};
    if (sourceProgress) sourceProgress->store(0, std::memory_order_release);
    if (b.Stop()) return b.result;
    if(options.textureMipBias>1 || (!options.texturePack.empty() && !options.textureCacheDirectory.empty())){b.Fail(SceneError::Descriptor);return b.result;}
    if(options.streaming&&!options.region.Valid()){b.Fail(SceneError::Descriptor);return b.result;}
    // Bounded metadata reservations are admitted before allocating. Runtime
    // limits cannot raise the fixed prototype's descriptor/content caps.
    const std::size_t metadata=sizeof(WorldScene)+sizeof(RegionGeometry)+(options.streaming?options.TileLimit():2)*sizeof(SceneTerrain)+MaxModels*sizeof(SceneModel)+MaxModelSources*sizeof(SceneModelSource)+
        MaxTextures*(sizeof(SceneTexture)+sizeof(SceneTextureIssue))+MaxSources*sizeof(SceneSource)+MaxInstances*sizeof(SceneInstance)+
        MaxWater*(sizeof(reflection::WaterSurface)+sizeof(uint16_t))+(options.buildings?MaxSceneBuildings*sizeof(SceneWmoSource):0);
    if (metadata>sceneLimit || metadata>workspaceLimit) { b.Fail(SceneError::Budget);return b.result; }
    b.result.archiveError=archives.Validate();
    if(b.result.archiveError!=ArchiveError::None){b.Fail(SceneError::Archive);return b.result;}
    b.scene.sourceIdentity=TextureCacheIdentity(archives,TextureRecipe());
    b.scene.sourceRegion=options.region;b.scene.sourcePolicy=SourcePolicy(options);
    b.scene.waterCompleteReach=options.streaming?options.WaterReachLimit():0;
    if(options.buildings || std::any_of(options.liveDisplays.begin(),options.liveDisplays.end(),[](auto id){return id!=0;}))b.scene.wmoSources.reserve(MaxSceneBuildings);
    b.scene.region.Edit().terrain.reserve(options.streaming?options.TileLimit():2);b.scene.models.reserve(MaxModels);b.scene.modelSources.reserve(MaxModelSources);
    b.scene.textures.reserve(MaxTextures);b.scene.textureIssues.reserve(MaxTextures);
    b.scene.sources.reserve(MaxSources);b.scene.region.Edit().instances.reserve(MaxInstances);
    b.scene.region.Edit().water.reserve(MaxWater);b.scene.region.Edit().waterTypes.reserve(MaxWater);
    auto stage=std::chrono::steady_clock::now();
    const auto timed=[&](unsigned index,bool ok){const auto now=std::chrono::steady_clock::now();
        b.result.stageMilliseconds[index]=std::chrono::duration<double,std::milli>(now-stage).count();stage=now;return ok;};
    // City/dungeon water belongs to WMO groups, not terrain. Admit both sources
    // through the same nearest-water budget before choosing a receiving body.
    const auto hasWater=[&]{if(options.streaming&&b.scene.region->water.empty()){b.Fail(SceneError::NoWater);return false;}return true;};
    const auto potentialWater=[&]{
        if(options.streaming&&b.furtherWater.empty()&&b.scene.wmoSources.empty()){b.Fail(SceneError::NoWater);return false;}
        return true;
    };
    if (b.Fits() && timed(0,b.Tiles() && potentialWater()) && timed(1,b.LiveAssets()) && timed(2,b.Models()) &&
        timed(3,b.Buildings() && b.AdmitFurtherWater() && hasWater()) && timed(4,b.Textures()) && timed(5,b.Instances()) && !b.Stop()) {
        b.scene.region.Edit().terrain.erase(std::remove_if(b.scene.region.Edit().terrain.begin(),b.scene.region.Edit().terrain.end(),[](const auto& t){return t.tile.cells.empty();}),b.scene.region->terrain.end());
        b.result.archiveError=archives.Validate();
        if (b.result.archiveError!=ArchiveError::None) b.Fail(SceneError::Archive);
        else output=std::move(b.scene);
    }
    return b.result;
}
SceneResult ExtendLiveScene(ArchiveReader& archives,const std::atomic<bool>& cancelled,WorldScene& candidate,
                           std::size_t sceneLimit,std::size_t workspaceLimit,std::atomic<uint64_t>* progress,SceneOptions options)
{
    Builder b{archives,cancelled,sceneLimit,workspaceLimit,{},{},0,0,{},progress,options};
    if(progress)progress->store(0,std::memory_order_release);
    if(b.Stop())return b.result;
    const auto& region=candidate.sourceRegion;
    if(!options.streaming || candidate.sourcePolicy!=SourcePolicy(options) ||
       region.map!=options.region.map || region.x!=options.region.x || region.y!=options.region.y ||
       candidate.sourceIdentity.empty() || candidate.sourceIdentity!=TextureCacheIdentity(archives,TextureRecipe())){
        b.Fail(SceneError::Descriptor);return b.result;
    }
    b.result.archiveError=archives.Validate();
    if(b.result.archiveError!=ArchiveError::None){b.Fail(SceneError::Archive);return b.result;}
    std::swap(b.scene,candidate);
    b.FinishTiles();
    const auto firstSource=unsigned(b.scene.wmoSources.size());
    const auto previousLive=unsigned(b.scene.liveAssets.size());
    std::vector<unsigned> dimensions;dimensions.reserve(b.scene.textures.size());
    for(const auto& texture:b.scene.textures)dimensions.push_back(texture.maxDimension);
    if(!b.Fits() || !b.LiveAssets())return b.result;
    if(b.scene.liveAssets.size()>MaxLiveAssets){b.Fail(SceneError::Budget);return b.result;}
    for(unsigned i=previousLive;i<b.scene.liveAssets.size();++i)
        if(b.scene.liveAssets[i].source<firstSource){b.Fail(SceneError::Descriptor);return b.result;}
    if(b.Buildings(firstSource) && b.Textures(&dimensions) && !b.Stop()){
        b.result.archiveError=archives.Validate();
        if(b.result.archiveError!=ArchiveError::None)b.Fail(SceneError::Archive);
        else {
            for(const auto& texture:b.scene.textures)
                b.result.textureCacheBuckets|=uint64_t(1)<<TextureStore::Bucket(texture.name);
            candidate=std::move(b.scene);b.result.reusedScenery=true;
        }
    }
    return b.result;
}
}
