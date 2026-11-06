// CPU-only production caster selection; no real D3D device or game.
#define STATIC_SHADOW_GPU_TEST
#include "test_static_shadow_fake_d3d.h"
#ifndef STATIC_SHADOW_IMPLEMENTATION
#define STATIC_SHADOW_IMPLEMENTATION "static_shadow_scene.cpp"
#endif
#include STATIC_SHADOW_IMPLEMENTATION
#include "static_shadow_gpu.h"
#include <cassert>
#include <cstdio>

using namespace StaticShadow;

// Frozen bdf9053 arithmetic. This negative must remain independent of the repair.
static void oldBounds(const Placement& p,Vec3 low,Vec3 high,Vec3& outLow,Vec3& outHigh){
    Vec3 c=transformPoint(p,(low+high)*.5f),r=(high-low)*.5f;const float* m=p.matrix;
    Vec3 e={std::fabs(m[0])*r.x+std::fabs(m[1])*r.y+std::fabs(m[2])*r.z,
            std::fabs(m[3])*r.x+std::fabs(m[4])*r.y+std::fabs(m[5])*r.z,
            std::fabs(m[6])*r.x+std::fabs(m[7])*r.y+std::fabs(m[8])*r.z};
    outLow=c-e;outHigh=c+e;
}

static bool encloses(Vec3 low,Vec3 high,const double* point){
    return low.x<=point[0]&&point[0]<=high.x&&low.y<=point[1]&&point[1]<=high.y&&low.z<=point[2]&&point[2]<=high.z;
}

// Endpoint oracle: no center, extent or production bounds helper. Check exact
// real arithmetic and separately rounded/FMA float dot-product arrangements.
static void corners(const Placement& p,Vec3 low,Vec3 high,Vec3 actualLow,Vec3 actualHigh){
    for(unsigned corner=0;corner<8;++corner){
        const float xyz[]={corner&1?high.x:low.x,corner&2?high.y:low.y,corner&4?high.z:low.z};
        const float translation[]={p.translation.x,p.translation.y,p.translation.z};
        double exact[3],rounded[3],fused[3];
        for(unsigned axis=0;axis<3;++axis){
            exact[axis]=translation[axis];volatile float sum=translation[axis];float f=translation[axis];
            for(unsigned j=0;j<3;++j){
                exact[axis]+=double(p.matrix[axis*3+j])*xyz[j];
                volatile float product=p.matrix[axis*3+j]*xyz[j];sum=sum+product;
                f=std::fma(p.matrix[axis*3+j],xyz[j],f);
            }
            rounded[axis]=sum;fused[axis]=f;
        }
        assert(encloses(actualLow,actualHigh,exact));
        assert(encloses(actualLow,actualHigh,rounded));
        assert(encloses(actualLow,actualHigh,fused));
    }
}

static std::shared_ptr<Snapshot> witness(){
    auto model=std::make_shared<Model>();model->key="cancellation";model->contentRevision=1;model->index16=true;
    const float a=999999.9375f,b=1000000.f;
    model->vertices={{a,-.2f,0,0,0},{b,0,0,1,0},{a,.2f,0,0,1}};
    model->indices={0,1,2};model->batches={{0,3,0,{a,-.2f,0},{b,.2f,0}}};
    NorthlightGI::WorldMaterial material;material.width=material.height=1;material.rgba={255,255,255,255};
    model->materials={material};model->materialKeys={1};
    auto scene=std::make_shared<Snapshot>();scene->map="bounds-fixture";scene->mapGeneration=1;scene->models[model->key]=model;
    Placement p;p.uid=1;p.category=1;p.modelKey=model->key;p.translation={-a,0,0};p.low={0,-.2f,0};p.high={b-a,.2f,0};
    scene->placements.push_back(p);return scene;
}

int main(){
    auto scene=witness();const auto& placement=scene->placements.front();const auto& batch=scene->models.begin()->second->batches.front();
    Vec3 oldLow,oldHigh;oldBounds(placement,batch.low,batch.high,oldLow,oldHigh);
    assert(oldLow.x==.03125f); // Actual first/third vertices have world x == 0.
    // An affine orthographic plane at a 48-yard half-width lies between the
    // shrunken bound and real endpoints, beyond clipReject's existing margin.
    float light[16]={};light[0]=light[5]=1.f/48;light[10]=1.f/1280;light[12]=1.f-.02f/48;light[14]=.5f;light[15]=1;
    const double point[]={0,0,0};
    assert(!encloses(oldLow,oldHigh,point));
    assert(NorthlightShadowBounds::directionalClipReject(oldLow,oldHigh,light));
    assert(point[0]*light[0]+light[12]<1&&point[0]*light[0]+light[12]>-1);
    IDirect3DDevice9 device;device.captureTransforms=true;GpuCache::Limits limits;limits.uploadMs=1000;
    GpuCache cache(limits);assert(cache.update(&device,scene,1));assert(cache.stats().readyModels==1);
    assert(cache.draw(&device,light));
    if(cache.stats().instances!=1||device.drawnRecords.size()!=1){
        std::fputs("STATIC_CASTER_FALSE_REJECTION\n",stderr);return 2;
    }
    const auto& drawn=device.drawnRecords.front();assert(drawn.primitives==1&&drawn.firstIndex==0);
    for(unsigned axis=0;axis<3;++axis)for(unsigned j=0;j<3;++j)assert(drawn.transform[axis*4+j]==placement.matrix[axis*3+j]);
    assert(drawn.transform[3]==placement.translation.x&&drawn.transform[7]==0&&drawn.transform[11]==0);
    Vec3 low,high;transformBounds(placement,batch.low,batch.high,low,high);corners(placement,batch.low,batch.high,low,high);
    assert(!NorthlightShadowBounds::directionalClipReject(low,high,light));

    // Real dirty-record construction and rect selection must retain the same
    // corner-crossing caster after a placement publication.
    GpuCache::ContentRecord record;cache.record(light,record);assert(record.valid&&record.slices.size()==1);
    auto moved=std::make_shared<Snapshot>(*scene);moved->placements[0].translation.y=.25f;
    assert(cache.update(&device,moved,2));std::vector<CasterBounds> dirty;size_t changed=0;
    assert(cache.changedBounds(light,record,dirty,changed)&&changed==1&&dirty.size()==2);
    bool hasOld=false,hasNew=false;const double newPoint[]={0,.25,0};
    std::vector<NorthlightShadowBounds::TexelRect> footprints;
    for(const auto& box:dirty){hasOld|=encloses(box.low,box.high,point);hasNew|=encloses(box.low,box.high,newPoint);
        NorthlightShadowBounds::TexelRect footprint;assert(box.bounded&&NorthlightShadowBounds::texelFootprint(box.low,box.high,light,1024,2,footprint));footprints.push_back(footprint);}
    assert(hasOld&&hasNew);std::vector<NorthlightShadowBounds::TexelRect> rects;
    NorthlightShadowBounds::dirtyRects(footprints,1024,16,8,rects);assert(!rects.empty());
    device.drawnRecords.clear();assert(cache.draw(&device,light,&rects,1024,2));
    assert(cache.stats().instances>=1&&!device.drawnRecords.empty());
    auto invalid=std::make_shared<Snapshot>(*moved);invalid->placements[0].matrix[7]=NAN;
    assert(cache.update(&device,invalid,3));
    assert(!cache.changedBounds(light,record,dirty,changed)); // Unknown geometry requires the full redraw path.
    cache.reset();assert(FakeResource::alive==0);

    unsigned checked=0;
    const float origins[]={0,17000,-17000,999999.9375f,-999999.9375f};
    for(float origin:origins)for(unsigned variant=0;variant<48;++variant){
        Placement p;p.translation={-origin,origin*.5f,-origin*.25f};
        const float angle=float(variant)*.13f,c=std::cos(angle),s=std::sin(angle);
        const float matrix[]={c,-s,.125f,s,c,-.25f,0,0,variant&1?-2.f:.5f};
        std::copy(matrix,matrix+9,p.matrix);
        Vec3 l{origin,-3,.2f},h{std::nextafter(origin,INFINITY),7,9};
        transformBounds(p,l,h,low,high);corners(p,l,h,low,high);++checked;
    }
    // Invalid inputs fail open in all callers, including fast-depth and dirty
    // coverage; a partial valid prefix cannot escape as a usable box.
    for(unsigned failure=0;failure<7;++failure){
        Placement p;Vec3 l{0,0,0},h{1,1,1};
        if(failure==0)l.x=NAN;else if(failure==1)h.y=INFINITY;else if(failure==2)l.z=2;
        else if(failure==3)p.matrix[7]=NAN;else if(failure==4)p.translation.z=INFINITY;
        else{p.matrix[0]=failure==5?std::numeric_limits<float>::max():-std::numeric_limits<float>::max();l.x=2;h.x=3;}
        transformBounds(p,l,h,low,high);
        for(unsigned plane=0;plane<6;++plane){
            float separating[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};separating[12+plane/2]=plane&1?3.f:-3.f;
            assert(!NorthlightShadowBounds::clipReject(low,high,separating));
            assert(!NorthlightShadowBounds::directionalClipReject(low,high,separating));
            assert(!NorthlightShadowBounds::depthFullyInside(low,high,separating));
            NorthlightShadowBounds::TexelRect footprint;assert(!NorthlightShadowBounds::texelFootprint(low,high,separating,1024,2,footprint));
        }
    }
    std::printf("PASS conservative static caster bounds: production selection/dirty records/rect draw, %u endpoint boxes, invalid fail-open\n",checked);
}
