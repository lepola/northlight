#include "shadow_transition_math.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
using NorthlightGI::Vec3;
namespace WM=NorthlightWorldMath;
using Point=std::array<double,4>;
static Point project(const Point& p,const float* m){
    Point q{};for(int col=0;col<4;++col)for(int row=0;row<4;++row)q[col]+=p[row]*m[row*4+col];return q;
}
static double error(const Point& a,const Point& b){
    double worst=0;for(unsigned i=0;i<4;++i){assert(std::isfinite(a[i])&&std::isfinite(b[i]));worst=std::max(worst,std::fabs(a[i]-b[i]));}return worst;
}
static Point worldPoint(const WM::ShadowFrame& f,float radius,const Point& ndc){
    const double a=ndc[0]*radius+f.cx,b=ndc[1]*radius+f.cy;
    const double c=(.5-ndc[2])*WM::ShadowDepthSpan+f.cz;
    return {a*f.x.x+b*f.y.x+c*f.z.x,a*f.x.y+b*f.y.y+c*f.z.y,a*f.x.z+b*f.y.z+c*f.z.z,1};
}
static void known(){
    WM::ShadowFrame f;f.x={1,0,0};f.y={0,1,0};f.z={0,0,1};f.cx=10;f.cy=20;f.cz=30;
    const float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};float r[16];
    ShadowTransitionMath::reproject(f,identity,48,r);
    assert(error(project({0,0,.5,1},r),{10,20,30,1})==0);
    assert(error(project({1,-1,.25,1},r),{58,-28,350,1})==0);
    assert(error(project({-1,1,1,1},r),{-38,68,-610,1})==0);
    float old[16];WM::shadowMatrixFrom(f,48,old);ShadowTransitionMath::reproject(f,old,48,r);
    for(Point q:{Point{-1,1,0,1},Point{.25,-.75,.5,1},Point{0,0,1,1}})assert(error(project(q,r),q)<1e-6);
    // In-place output is allowed and byte-identical to a separate destination.
    float inPlace[16];std::memcpy(inPlace,old,sizeof old);ShadowTransitionMath::reproject(f,inPlace,48,inPlace);
    assert(!std::memcmp(inPlace,r,sizeof r));
    // The input domain is the old 1024 working map, not its 1280 margin cache.
    float cached[16];WM::shadowMatrixFrom(f,60,cached);
    const Point p{46,8,42,1};assert(error(project(project(p,old),r),project(p,old))<1e-6);
    assert(error(project(project(p,cached),r),project(p,old))>.1);
}
static void cases(){
    const Vec3 centers[]={{0,0,0},{-10432.37f,145.61f,87.2f},{5872,672,650},{999999,-777777,333333}};
    const Vec3 directions[]={{0,0,1},{.3f,-.4f,.7f},{-.5f,.2f,.6f},{.1f,.9f,.3f},{.3003f,-.4002f,.7001f}};
    unsigned checked=0;double analyticWorst=0,worldWorst=0,roundTripWorst=0;
    for(float radius:{48.f,192.f})for(Vec3 center:centers)for(Vec3 before:directions)for(Vec3 after:directions){
        const auto old=WM::shadowFrame(center,before,radius);
        const auto current=WM::shadowFrame(center+Vec3(13.25f,-9.5f,7.75f),after,radius);
        float oldMatrix[16],currentMatrix[16],toCurrent[16],toOld[16];
        WM::shadowMatrixFrom(old,radius,oldMatrix);WM::shadowMatrixFrom(current,radius,currentMatrix);
        ShadowTransitionMath::reproject(old,currentMatrix,radius,toCurrent);
        ShadowTransitionMath::reproject(current,oldMatrix,radius,toOld);
        // A double reference avoids cancellation in independently reconstructed
        // world positions; output remains the production float4x4.
        for(double x:{-1.2,-1.,-.25,0.,.75,1.,1.2})for(double y:{-1.,0.,.3,1.})for(double z:{-.1,0.,.5,1.,1.1}){
            const Point oldNdc{x,y,z,1};const Point world=worldPoint(old,radius,oldNdc);
            const Point expected=project(world,currentMatrix),actual=project(oldNdc,toCurrent);
            const double analytic=error(actual,expected);analyticWorst=std::max(analyticWorst,analytic);
            assert(analytic<3e-6);
            // Existing frames/matrices contain rounded float bases and origins.
            // Bound world/round-trip error by float resolution at that location.
            const double scale=std::max({1.,std::fabs(world[0]),std::fabs(world[1]),std::fabs(world[2])});
            const double tolerance=4e-6+4*std::numeric_limits<float>::epsilon()*scale/radius;
            const double worldError=error(project(project(world,oldMatrix),toCurrent),expected);
            const double roundTrip=error(project(actual,toOld),oldNdc);
            worldWorst=std::max(worldWorst,worldError);roundTripWorst=std::max(roundTripWorst,roundTrip);
            assert(worldError<tolerance&&roundTrip<tolerance);
            ++checked;
        }
    }
    std::printf("PASS shadow reprojection: %u known-domain/direction/near-far/large-coordinate points; max analytic error %.9g, world error %.9g, round-trip error %.9g\n",checked,analyticWorst,worldWorst,roundTripWorst);
}
int main(){known();cases();}
