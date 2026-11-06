// Offline production regional field and BVH depth from verified MPQ geometry.
#include "regional_fog.h"
#include <cassert>
#include <filesystem>
#include <fstream>
using namespace NorthlightGI;
template<class T> static void read(std::ifstream& f,T& v){assert(f.read(reinterpret_cast<char*>(&v),sizeof v));}
static unsigned header(std::ifstream& f,const char* expected){char magic[4];unsigned n;assert(f.read(magic,4)&&!std::memcmp(magic,expected,4));read(f,n);assert(n&&n<100000);return n;}
static void bytes(const std::string& path,const void* data,size_t size){std::ofstream f(path,std::ios::binary);assert(f.write(static_cast<const char*>(data),size));}
static void image(const std::string& path,unsigned w,unsigned h,unsigned format,unsigned stride,const void* data){
    std::ofstream f(path,std::ios::binary);const unsigned header[]={0x31524746,w,h,format,stride};assert(f.write(reinterpret_cast<const char*>(header),sizeof header));assert(f.write(static_cast<const char*>(data),size_t(w)*h*stride));
}
struct Camera {uint64_t tick;Vec3 eye,forward;float distance;};
int main(int argc,char** argv){
    assert(argc==6);const std::string root=argv[1],metadata=argv[2],out=argv[3];const unsigned W=std::atoi(argv[4]),H=std::atoi(argv[5]);assert(W>=16&&H>=16&&W<=4096&&H<=4096);
    std::filesystem::create_directories(out);
    std::ifstream cams(root+"/camera.bin",std::ios::binary);std::vector<Camera> cameras(header(cams,"NLC1"));
    for(auto& c:cameras){read(cams,c.tick);read(cams,c.eye);read(cams,c.forward);read(cams,c.distance);}assert(cams.peek()==EOF);
    const Vec3 center=cameras.front().eye+cameras.front().forward*cameras.front().distance;
    const Vec3 low=center-Vec3(384,384,384),high=center+Vec3(384,384,384);
    std::vector<std::string> tiles;const int tx=int(std::floor((17066.666666666668-center.y)/(1600./3))),ty=int(std::floor((17066.666666666668-center.x)/(1600./3)));
    for(int y=ty-1;y<=ty+1;++y)for(int x=tx-1;x<=tx+1;++x){auto path=root+"/world-cache/Northrend/"+std::to_string(x)+"_"+std::to_string(y)+".fg3";assert(std::filesystem::exists(path));tiles.push_back(path);}
    WorldScene scene;std::string error;assert(loadInstancedScenes(tiles,root+"/world-cache/models",low,high,scene,error,6));
    // terrain.bin retains the MPQ ADT positions and holes; add it to the static instances.
    WorldMaterial terrain;terrain.terrain=true;terrain.alphaCutoff=0;const unsigned material=unsigned(scene.materials.size());scene.materials.push_back(terrain);
    std::ifstream input(root+"/terrain.bin",std::ios::binary);const unsigned draws=header(input,"NLT1");
    for(unsigned draw=0;draw<draws;++draw){unsigned n,count;read(input,n);read(input,count);assert(n&&n<=65535&&count%3==0);std::vector<Vec3> p(n);for(auto& v:p)read(input,v);
        for(unsigned i=0;i<count;i+=3){unsigned ids[3];for(auto& id:ids){read(input,id);assert(id<n);}const auto a=p[ids[0]],b=p[ids[1]],c=p[ids[2]];
            if(std::max({a.x,b.x,c.x})<low.x||std::min({a.x,b.x,c.x})>high.x||std::max({a.y,b.y,c.y})<low.y||std::min({a.y,b.y,c.y})>high.y)continue;
            const unsigned v=unsigned(scene.vertices.size());for(auto pos:{a,b,c}){WorldVertex vertex;vertex.position=pos;vertex.normal=normalized(cross(b-a,c-a));scene.vertices.push_back(vertex);}scene.triangles.push_back({v,v+1,v+2,material});
        }
    }assert(input.peek()==EOF);
    BVH bvh;assert(bvh.build(std::move(scene),error));
    for(unsigned pose=0;pose<cameras.size()*2;++pose){auto cam=cameras[pose%cameras.size()];auto region=NorthlightRegionalFog::loadRegion(metadata,"Northrend",cam.eye.x,cam.eye.y);assert(region.missing==0&&region.invalid==0&&region.tiles.size()==9);
        auto field=NorthlightRegionalFog::buildField(bvh.scene(),region,cam.eye.x,cam.eye.y);assert(field.groundCells&&field.airCells);
        const bool grounded=pose>=cameras.size();
        if(grounded){const unsigned x=unsigned((cam.eye.x-field.originX)/8),y=unsigned((cam.eye.y-field.originY)/8);assert(x<64&&y<64&&field.texels[y*64+x].height>0);cam.eye.z=field.texels[y*64+x].ground+2;cam.forward.z=-.1f;}
        const Vec3 forward=normalized(cam.forward),right=normalized(cross(forward,Vec3(0,0,1))),up=normalized(cross(right,forward));
        float c[68][4]{};const float near=.2f,far=1277,partition=.94f,fx=1.2686f,fy=1.9626f;
        c[0][0]=1.f/W;c[0][1]=1.f/H;c[0][2]=near;c[0][3]=far;c[1][0]=fx;c[1][1]=fy;c[1][2]=1;c[2][0]=1/partition;c[2][2]=192;c[2][3]=1.f/64;
        unsigned row=3;for(auto v:{right,up,forward,cam.eye}){c[row][0]=v.x;c[row][1]=v.y;c[row][2]=v.z;++row;}c[6][3]=1;
        c[15][0]=cam.eye.x;c[15][1]=cam.eye.y;c[15][2]=cam.eye.z;c[16][0]=c[16][1]=c[16][2]=.57735026f;
        c[11][0]=c[12][1]=c[13][2]=1.f/192;c[14][0]=-cam.eye.x/192;c[14][1]=-cam.eye.y/192;c[14][2]=.2f-cam.eye.z/192;c[14][3]=1;
        c[17][0]=.6f;c[17][1]=.8f;c[17][2]=.9f;c[18][0]=.2f;c[18][1]=.3f;c[18][2]=.4f;c[21][0]=c[21][1]=1;c[21][2]=.4f;c[21][3]=128;
        c[22][0]=c[22][1]=c[22][2]=1;c[22][3]=.008f;c[27][0]=1;c[31][0]=field.originX;c[31][1]=field.originY;c[31][2]=1.f/(64*8);c[31][3]=.5f;
        c[32][0]=.01f;c[32][1]=.004f;c[32][2]=64;c[32][3]=32;c[33][0]=W;c[33][1]=H;c[33][2]=W/2;c[33][3]=H/2;
        c[24][0]=.001f;c[58][0]=20;c[58][1]=1.f/15;c[59][3]=.0017f;c[59][1]=-30;c[61][0]=.25f;c[62][0]=66;c[62][1]=-3.2f;c[62][2]=96;c[62][3]=.045f;c[63][0]=2.8f;c[63][1]=1.f/48;c[63][2]=1.f/12;c[63][3]=5.2f;
        std::vector<float> depth(size_t(W)*H);unsigned hits=0;
        // Match viewPositionDistance's D3D9 half-texel subtraction after depthUV.
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x){const Vec3 ray=normalized(right*((2.f*x/W-1)/fx)+up*((1-2.f*y/H)/fy)+forward);const auto hit=bvh.trace(cam.eye,ray,.001f,128);
            float raw=1;if(hit.hit){++hits;const float z=std::max(near,hit.distance*dot(ray,forward));raw=far/(far-near)*(1-near/z)*partition;}depth[size_t(y)*W+x]=raw;
        }
        const std::string prefix=out+"/pose-"+std::to_string(pose)+"-";bytes(prefix+"constants.f32",c,sizeof c);image(prefix+"depth.fgr",W,H,114,4,depth.data());image(prefix+"regional.fgr",64,64,116,16,field.texels.data());
        std::vector<unsigned> color(size_t(W)*H,0xff808080);image(prefix+"scene.fgr",W,H,21,4,color.data());
        std::printf("FOG_MPQ_FIXTURE pose=%u camera=%s width=%u height=%u triangles=%zu hits=%u groundCells=%u airCells=%u indoorCells=%u missing=%u\n",pose,grounded?"terrain-relative-control":"recorded",W,H,bvh.triangleCount(),hits,field.groundCells,field.airCells,field.indoorCells,region.missing);std::fflush(stdout);
    }
}
