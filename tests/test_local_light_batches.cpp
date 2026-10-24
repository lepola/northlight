// local_light_batches.h (screen-coherent lamp batches, LocalDirect16 merge) and the Tracker's off-screen preference
// (local_light_selection.h inView, OffscreenPenalty).
#include "local_light_batches.h"
#include <cassert>
#include <cstdio>
#include <random>
#include <set>
#include <vector>
using namespace NorthlightLocalLightSelection;
namespace S=NorthlightLocalLightScissor;
namespace B=NorthlightLocalLightBatches;
using Light=NorthlightLocalLights::Light;
// Camera at the origin looking down +z (rows x, y, z; Pz=1), 90 degrees vertical, 16:9, 1280x720.
static S::View view(){
    float iv[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};const float proj[3]={1.f/(16.f/9.f),1.f,1.f};
    return S::view(iv,proj,.1f,3.5f,1280,720);
}
static Light lamp(unsigned id,float x,float y,float z,float end=6){
    Light l{};l.position[0]=x;l.position[1]=y;l.position[2]=z;l.diffuse[0]=1;l.diffuse[1]=.5f;l.diffuse[2]=.25f;
    l.attenuationStart=1;l.attenuationEnd=end;l.sourceId=id;l.kind=2;return l;
}
static void inViewTests(){
    const auto v=view();assert(v.valid);
    assert(inView(v,lamp(1,0,0,40),ViewMarginSin));               // ahead
    assert(!inView(v,lamp(2,0,0,-40),ViewMarginSin));             // behind
    assert(!inView(v,lamp(3,0,0,-40),IncumbentMarginSin));
    assert(!inView(v,lamp(7,-100,0,1),ViewMarginSin));            // 90 degrees to the side, far outside the 60 degree half-width
    assert(inView(v,lamp(4,0,0,-3,6),ViewMarginSin));             // the camera inside the sphere
    assert(inView(v,lamp(8,80,0,40,4),ViewMarginSin));            // just outside the screen edge: lit before it enters
    // About 25 degrees past the right edge (60.6 degrees): the wider incumbent margin keeps it, the newcomer's does not.
    const Light side=lamp(5,99.7f,0,7.7f,4);
    assert(!inView(v,side,ViewMarginSin)&&inView(v,side,IncumbentMarginSin));
    S::View invalid;assert(inView(invalid,lamp(6,0,0,-40),ViewMarginSin)); // no view: every light in view
}
static void trackerTests(){
    const auto v=view();const float camera[3]={0,0,0};
    // Four lamps behind (closer) and four ahead (farther): over the cap (4) the slots go ahead; under it (8) all, as without a view.
    std::vector<Light> lights;
    for(unsigned i=0;i<4;++i)lights.push_back(lamp(1+i,float(i)*3-4.5f,0,-20));
    for(unsigned i=0;i<4;++i)lights.push_back(lamp(5+i,float(i)*3-4.5f,0,60));
    Tracker t;Selection out;for(unsigned f=0;f<60;++f)out=t.update(lights,camera,4,1.f/60,true,&v);
    assert(t.offscreen==4);
    unsigned ahead=0;for(unsigned i=0;i<out.count;++i)if(out.color[i][0]>0&&out.position[i][2]>0)++ahead;
    assert(out.count-t.fading==4&&ahead==4);
    for(unsigned i=0;i<out.count;++i)if(out.position[i][2]<0)assert(i>=4); // behind lamps only in the fading tail
    Tracker a,b;Selection sa,sb;
    for(unsigned f=0;f<10;++f){sa=a.update(lights,camera,8,1.f/60,true,&v);sb=b.update(lights,camera,8,1.f/60,true);}
    assert(sa.count==8&&sa.count==sb.count&&sa.position==sb.position&&sa.color==sb.color&&sa.fogDistance==sb.fogDistance);
    // Debug views pass no view: the plain distance order even over the cap.
    Tracker c;Selection sc;for(unsigned f=0;f<60;++f)sc=c.update(lights,camera,4,1.f/60,true);
    for(unsigned i=0;i<4;++i)assert(sc.position[i][2]<0);
}
static std::multiset<std::array<float,12>> content(const Selection& s){
    std::multiset<std::array<float,12>> m;
    for(unsigned i=0;i<s.count;++i){std::array<float,12> k{};for(unsigned j=0;j<4;++j){k[j]=s.position[i][j];k[4+j]=s.color[i][j];k[8+j]=s.fog[i][j];}m.insert(k);}
    return m;
}
static Selection fill(const std::vector<Light>& lights){
    Selection s;s.count=unsigned(lights.size());
    for(unsigned i=0;i<s.count;++i){const auto& l=lights[i];s.position[i]={l.position[0],l.position[1],l.position[2],l.attenuationEnd};
        s.color[i]={float(i+1),0,0,1};s.fog[i]={float(i)*.5f,0,0,0};s.sources[i]=l;
        for(unsigned k=0;k<3;++k)s.sources[i].diffuse[k]=s.color[i][k];}
    return s;
}
static void sourcesAligned(const Selection& s){
    for(unsigned i=0;i<s.count;++i){const auto& source=s.sources[i];
        for(unsigned k=0;k<3;++k)assert(source.position[k]==s.position[i][k]&&source.diffuse[k]==s.color[i][k]);
        assert(source.attenuationEnd==s.position[i][3]&&source.sourceId==std::uint64_t(s.color[i][0])&&source.kind==2);
    }
}
static void arrangeTests(){
    const auto v=view();
    // Closest-first order alternating left and right: every 8-lamp batch spans the screen. Arranged: left and right batches.
    std::vector<Light> lights;for(unsigned i=0;i<32;++i)lights.push_back(lamp(i+1,(i%2?1:-1)*(30.f+float(i/2)),float(i/2%4)*4-6,40+float(i),3));
    Selection s=fill(lights);const auto before=content(s);
    const auto r=B::arrange(s,v);
    assert(r.arranged&&r.after<r.before*.75&&content(s)==before);
    sourcesAligned(s); // source IDs and point-shadow metadata follow the same permutation
    // Each slot keeps its own colour and fog with its position (fill() tags them by slot).
    for(unsigned i=0;i<s.count;++i){unsigned k=0;while(lights[k].position[0]!=s.position[i][0]||lights[k].position[2]!=s.position[i][2])++k;assert(s.color[i][0]==float(k+1)&&s.fog[i][0]==float(k)*.5f);}
    // Invalid view and a single lamp: unchanged.
    Selection u=fill(lights);S::View invalid;assert(!B::arrange(u,invalid).arranged&&u.position==fill(lights).position);
    Selection one=fill({lights[0]});assert(!B::arrange(one,v).arranged);
    // Never worse than the old order, random scenes (behind, around and in front of the camera).
    std::mt19937 rng(0x1a3b);std::uniform_real_distribution<float> x(-120,120),z(-60,160),e(1,14);
    for(unsigned trial=0;trial<400;++trial){
        std::vector<Light> l;const unsigned n=1+rng()%72;for(unsigned i=0;i<n;++i)l.push_back(lamp(i+1,x(rng),x(rng)*.3f,z(rng),e(rng)));
        Selection t=fill(l);const auto c=content(t);const auto res=B::arrange(t,v);
        assert(res.after<=res.before&&content(t)==c);
        sourcesAligned(t);
        if(!res.arranged)assert(t.position==fill(l).position);
    }
}
static void wideTests(){
    const B::Rect all{0,0,640,360},left{0,0,40,40},right{600,320,640,360},none{};
    assert(B::wide(all,all));                  // two whole-target passes: one 16-lamp pass is cheaper
    // Current shader slots prefer the joint rect; the stale 244/364 model did not.
    const B::Rect edgeA{0,0,100,10},edgeB{41,0,141,10},pastEdge{42,0,142,10};
    assert(B::wide(edgeA,edgeB)&&!B::wide(edgeA,pastEdge));
    assert(!B::wide(left,right));              // far apart: the joint rect is the whole target
    assert(!B::wide(none,none)&&!B::wide(left,none)); // one 8-lamp pass already covers it
    assert(B::unite(none,left).area()==left.area()&&B::unite(left,right).area()==all.area());
}
static void hilbertTests(){
    std::set<std::uint32_t> seen;std::vector<std::pair<unsigned,unsigned>> at(256);
    for(unsigned x=0;x<16;++x)for(unsigned y=0;y<16;++y){const auto d=B::hilbert(x,y,4);assert(d<256&&seen.insert(d).second);at[d]={x,y};}
    for(unsigned d=1;d<256;++d){const int dx=int(at[d].first)-int(at[d-1].first),dy=int(at[d].second)-int(at[d-1].second);assert(std::abs(dx)+std::abs(dy)==1);}
}
int main(){
    inViewTests();trackerTests();arrangeTests();wideTests();hilbertTests();
    std::puts("local light batches: ok");
}
