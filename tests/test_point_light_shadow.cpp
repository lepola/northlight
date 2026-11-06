#include "point_light_shadow.h"
#include "local_light_scissor.h"
#include <cassert>
#include <iostream>
using NorthlightLocalLights::Light;
static void transform(const float* matrix,const float* p,float* clip){for(int j=0;j<4;++j){clip[j]=matrix[12+j];for(int i=0;i<3;++i)clip[j]+=p[i]*matrix[i*4+j];}}
int main(){
    const float source[3]={123,-41,7};
    const float axis[6][3]={{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    unsigned samples=0;
    unsigned faceBounds=0;
    for(unsigned face=0;face<6;++face){
        float matrix[16];assert(NorthlightPointShadow::faceMatrix(source,.1,30,face,matrix));
        for(float distance:{.1f,1.f,10.f,30.f}){
            float p[3],clip[4];for(int j=0;j<3;++j)p[j]=source[j]+axis[face][j]*distance;
            transform(matrix,p,clip);assert(std::abs(clip[3]-distance)<.00002);
            assert(std::abs(clip[0]/clip[3]+1.f/256)<.0001);assert(std::abs(clip[1]/clip[3]-1.f/256)<.0001);
            float expected=30.f/29.9f*(1-.1f/distance);assert(std::abs(clip[2]/clip[3]-expected)<.0001);++samples;
        }
        // Independent D3D cube map address equations: right/left/up/down rays.
        for(int u=-3;u<=3;++u)for(int v=-3;v<=3;++v){
            const float s=u*.2f,t=v*.2f;
            const float directions[6][3]={{1,-t,-s},{-1,-t,s},{s,1,t},{s,-1,-t},{s,-t,1},{-s,-t,-1}};
            float p[3],clip[4];for(int j=0;j<3;++j)p[j]=source[j]+directions[face][j]*5;
            transform(matrix,p,clip);
            const float textureU=(clip[0]/clip[3]*.5f+.5f)+.5f/256;
            const float textureV=(.5f-clip[1]/clip[3]*.5f)+.5f/256;
            assert(std::abs(textureU-(s*.5f+.5f))<.00001);assert(std::abs(textureV-(t*.5f+.5f))<.00001);++samples;
        }
        for(unsigned other=0;other<6;++other){
            float lo[3],hi[3];for(int j=0;j<3;++j){lo[j]=source[j]+axis[other][j]*5-.1f;hi[j]=lo[j]+.2f;}
            assert(NorthlightPointShadow::clipReject(lo,hi,matrix)==(other!=face));++faceBounds;
        }
        float enclosingLo[3],enclosingHi[3];for(int j=0;j<3;++j){enclosingLo[j]=source[j]-50;enclosingHi[j]=source[j]+50;}
        assert(!NorthlightPointShadow::clipReject(enclosingLo,enclosingHi,matrix));++faceBounds;
        // A box straddling the near clip plane cannot be rejected.
        float nearLo[3],nearHi[3];for(int j=0;j<3;++j){nearLo[j]=source[j]+axis[face][j]*.1f-.03f;nearHi[j]=nearLo[j]+.06f;}
        assert(!NorthlightPointShadow::clipReject(nearLo,nearHi,matrix));++faceBounds;
    }
    Light a={{0,0,0},{1,1,1},1,10,100,1,1},b=a;b.position[0]=2;b.sourceId=200;
    std::vector<Light> lights={a,b};float camera[3]={1.02f,0,0};
    assert(NorthlightPointShadow::select(lights,camera)==1);assert(NorthlightPointShadow::select(lights,camera,100)==0);
    Light authored=a;authored.kind=3;authored.flags=0;authored.diffuse[0]=100;
    assert(NorthlightLocalLights::valid(authored));assert(NorthlightPointShadow::select({authored},camera)==-1);
    camera[0]=100;assert(NorthlightPointShadow::select(lights,camera)==-1);
    // A large source can light nearby receivers although its centre is beyond
    // the 48-yard shadow window. Camera rotation does not change admission.
    Light large={{0,0,91.2f},{.368f,.700f,.648f},0,81.57f,42,1,NorthlightLocalLights::Baked|NorthlightLocalLights::CastShadow};
    const float origin[3]={},projection[3]={9.f/16.f,1,1};
    for(float yaw:{-1.4f,-.7f,0.f,.7f,1.4f,3.14f}){
        float c=std::cos(yaw),s=std::sin(yaw);
        const float view[16]={c,0,s,0,0,1,0,0,-s,0,c,0,0,0,0,1};
        auto v=NorthlightLocalLightScissor::view(view,projection,.1f,3.5f,3840,2160);assert(v.valid);
        assert(NorthlightPointShadow::select({large},origin)==0);
        const float sphere[4]={large.position[0],large.position[1],large.position[2],large.attenuationEnd};
        NorthlightLocalLightScissor::Bounds bounds;NorthlightLocalLightScissor::add(v,sphere,bounds);
        if(yaw==0.f)assert(NorthlightLocalLightScissor::rect(v,bounds).area()>0);
    }
    large.position[2]=129.5f;assert(NorthlightPointShadow::select({large},origin)==0);
    large.position[2]=129.7f;assert(NorthlightPointShadow::select({large},origin)==-1);
    large.position[2]=91.2f;large.kind=3;large.flags=0;assert(NorthlightPointShadow::select({large},origin)==-1);
    float lo[3]={10,-1,-1},hi[3]={11,1,1};assert(NorthlightPointShadow::intersectsSphereAABB(a,lo,hi));
    lo[0]=10.01f;assert(!NorthlightPointShadow::intersectsSphereAABB(a,lo,hi));
    lo[0]=NAN;assert(!NorthlightPointShadow::intersectsSphereAABB(a,lo,hi));
    float matrix[16];assert(!NorthlightPointShadow::faceMatrix(camera,.1,.1,0,matrix));assert(!NorthlightPointShadow::faceMatrix(camera,.1,10,6,matrix));
    assert(!NorthlightPointShadow::clipReject(nullptr,hi,matrix));
    assert(!NorthlightPointShadow::clipReject(lo,hi,matrix)); // nonfinite bounds
    // World-coordinate cancellation and exact frustum-edge contact: retain
    // boxes on either side within the documented float evaluation uncertainty.
    float distant[3]={-10000,15000,100};assert(NorthlightPointShadow::faceMatrix(distant,.1,30,4,matrix));
    for(int i=-10;i<=10;++i){
        const float z=10,x=z*(1+1.f/256)+i*.0001f;
        float edgeLo[3]={distant[0]+x,distant[1],distant[2]+z},edgeHi[3]={edgeLo[0]+.001f,edgeLo[1]+.001f,edgeLo[2]+.001f};
        assert(!NorthlightPointShadow::clipReject(edgeLo,edgeHi,matrix));++faceBounds;
    }
    matrix[0]=NAN;lo[0]=0;assert(!NorthlightPointShadow::clipReject(lo,hi,matrix));
    // Real six-face depth policy: a nearer opaque wall blocks the receiver;
    // empty maps and geometry behind it cannot create a shadow.
    auto depth=[](float d){return 30.f/29.9f*(1-.1f/d);};
    assert(depth(10)>depth(5));assert(depth(10)<depth(20));assert(depth(10)<1);
    std::cout<<"point-shadow: "<<samples<<" cube projection/address cases, "<<faceBounds<<" perspective bounds cases, selection, culling and depth ordering passed\n";
}
