#define NOMINMAX
#include <windows.h>
#include "regional_fog_cache.h"
#include "geometry_memory.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
using namespace NorthlightGI;
namespace F=NorthlightRegionalFog;
namespace M=NorthlightGeometryMemory;
static M::Sample memory(){
    SYSTEM_INFO info;GetSystemInfo(&info);const auto limit=uint64_t(reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress))+1;
    M::Sample s;s.valid=true;
    for(uint64_t at=0;at<limit;){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<void*>(uintptr_t(at)),&m,sizeof m)){s.valid=false;break;}
        const auto end=uint64_t(reinterpret_cast<uintptr_t>(m.BaseAddress))+m.RegionSize;if(end<=at){s.valid=false;break;}
        if(m.State==MEM_FREE){s.available+=m.RegionSize;s.largest=std::max<uint64_t>(s.largest,m.RegionSize);}at=end;}
    return s;
}
static std::vector<std::string> tiles(const std::string& root,const std::string& map,Vec3 center){
    int tx=int(std::floor((F::WorldZero-center.y)/F::TileSize)),ty=int(std::floor((F::WorldZero-center.x)/F::TileSize));std::vector<std::string> files;
    for(int y=ty-1;y<=ty+1;++y)for(int x=tx-1;x<=tx+1;++x){auto path=root+"/"+map+"/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";if(std::filesystem::is_regular_file(path))files.push_back(path);}return files;
}
static void same(const F::Field& a,const F::Field& b){
    assert(a.originX==b.originX&&a.originY==b.originY&&a.groundCells==b.groundCells&&a.fogCells==b.fogCells&&a.airCells==b.airCells&&a.indoorCells==b.indoorCells&&a.citySurfaceCells==b.citySurfaceCells);
    assert(!std::memcmp(a.texels.data(),b.texels.data(),sizeof(a.texels))&&a.lush==b.lush);
}
int main(int argc,char** argv){
    assert(argc==2);const std::string root=argv[1];std::string error;
    struct Location {const char* map;Vec3 center;};
    const Location locations[]={{"Northrend",{5947.70f,854.14f,148.87f}},{"Northrend",{5870.25f,719.85f,229.10f}},{"Northrend",{5062.95f,-240.03f,498.99f}},
                               {"Azeroth",{-8833,628,95}},{"Azeroth",{1628,-4378,70}}};
    for(const auto& location:locations){
        const auto region=F::loadRegion(root+"/fog",location.map,location.center.x,location.center.y);assert(!region.tiles.empty());
        WorldScene full;assert(loadInstancedScenes(tiles(root,location.map,location.center),root+"/models",location.center-Vec3(288,288,320),location.center+Vec3(288,288,320),full,error,15));
        const auto expected=F::buildField(full,region,location.center.x,location.center.y);
        F::Field field;assert(F::buildCachedField(root,location.map,location.center,region,field,error));same(expected,field);
        const uint64_t fullBytes=full.retainedBytes();full={};
        const auto before=memory();assert(before.valid);
        std::vector<void*> ballast;
        while(memory().available>400*M::MiB+65536){const auto s=memory();const auto bytes=std::min(s.available-400*M::MiB,s.largest/2)/65536*65536;assert(bytes);auto* block=VirtualAlloc(nullptr,size_t(bytes),MEM_RESERVE,PAGE_NOACCESS);assert(block);ballast.push_back(block);}
        uint64_t minimum=memory().available,largestRequest=0;unsigned checks=0;bool refused=false;
        const AllocationAdmission admit=[&](uint64_t bytes){if(bytes<M::MiB)return true;++checks;largestRequest=std::max(largestRequest,bytes);const auto s=memory();assert(s.valid);minimum=std::min(minimum,s.available);const bool ok=M::admits(s,M::inBuildBudget(bytes));refused|=!ok;return ok;};
        F::Field constrained;const bool built=F::buildCachedField(root,location.map,location.center,region,constrained,error,admit);
        if(built)same(expected,constrained);else assert(refused&&!error.empty());
        minimum=std::min(minimum,memory().available);assert(minimum>=M::ProcessReserve);
        for(auto* block:ballast)assert(VirtualFree(block,0,MEM_RELEASE));
        assert(F::buildCachedField(root,location.map,location.center,region,constrained,error));same(expected,constrained);
        F::Field preserved=field;
        assert(!F::buildCachedField(root,location.map,location.center,region,preserved,error,[](uint64_t){return false;}));same(field,preserved);
        const unsigned x=unsigned((location.center.x-field.originX)/F::Spacing),y=unsigned((location.center.y-field.originY)/F::Spacing);assert(x<F::N&&y<F::N);const auto& cell=field.texels[y*F::N+x];
        std::printf("INDEPENDENT_FOG map=%s x=%.2f y=%.2f z=%.2f exact=1 pressure400Built=%u minAvailableMiB=%llu maxRequestMiB=%.3f fullSceneMiB=%.3f fieldKiB=%.3f air=%u city=%u admissionChecks=%u cameraLayer=%.4f cameraAboveGround=%.3f\n",location.map,location.center.x,location.center.y,location.center.z,unsigned(built),(unsigned long long)(minimum>>20),double(largestRequest)/1048576,double(fullBytes)/1048576,double(sizeof(field))/1024,field.airCells,field.citySurfaceCells,checks,cell.height,location.center.z-cell.ground);
    }
}
