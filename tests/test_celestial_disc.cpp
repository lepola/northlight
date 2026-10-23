#include "celestial_disc.h"
#include "celestial_disc_native.h"
#include "celestial_context.h"
#include <cassert>
#include <iostream>
#include <limits>
#include <unordered_map>
using namespace NorthlightCelestialDisc;
int main(int argc,char** argv){
    assert(argc==2);float white[3]={1,1,1};Disc d;unsigned cases=0;
    // Duskwood regression: .92 is in front of most of a 0.. .94 world.
    // Exercise the actual placement helper with both the native sky viewport
    // and a full-range viewport; all world distances must win the depth test,
    // regardless of whether the world is drawn before or after the sprite.
    auto d24=[](float z){return std::lround(double(z)*16777215.0);};
    unsigned skyCases=0;float sprite=0;
    for(float worldMax:{.7f,.9f,.94f,.99f})for(float skyMin:{0.f,worldMax}){
        assert(skyDepth(skyMin,1,worldMax,sprite));
        assert(d24(sprite)>d24(worldMax)&&d24(sprite)<d24(1.f));
        for(float nearZ:{.1f,.2f,1.f})for(float farZ:{100.f,947.f,2000.f})
        for(float fraction:{0.f,.01f,.1f,.5f,1.f}){
            const float distance=nearZ+(farZ-nearZ)*fraction;
            const float z=worldMax*(farZ/(farZ-nearZ)-nearZ*farZ/((farZ-nearZ)*distance));
            assert(d24(z)<=d24(sprite)); // world after sky: passes LESSEQUAL
            assert(!(d24(sprite)<=d24(z))); // sky after world: rejected
            ++skyCases;
        }
    }
    assert(skyDepth(.94f,1,.94f,sprite)&&std::fabs(sprite-.9952f)<.000001f);
    assert(.92f<.94f*(947.f/(947.f-.2f)-.2f*947.f/((947.f-.2f)*100.f)));
    assert(!skyDepth(0,1,1,sprite)); // no reserved sky band: keep native draw
    assert(!skyDepth(.94f,.94f,.94f,sprite));
    assert(!skyDepth(0,.9f,.94f,sprite));
    assert(!skyDepth(-.1f,1,.94f,sprite));
    assert(!skyDepth(0,1.1f,.94f,sprite));
    assert(!skyDepth(0,1,-.1f,sprite));
    assert(!skyDepth(0,1,std::nextafter(1.f,0.f),sprite));
    for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}){
        assert(!skyDepth(invalid,1,.94f,sprite));
        assert(!skyDepth(0,invalid,.94f,sprite));
        assert(!skyDepth(0,1,invalid,sprite));
    }
    std::cout<<"celestial sky-depth: "<<skyCases<<" D24 world occlusion/order cases and invalid bands passed\n";
    std::unordered_map<std::uintptr_t,unsigned char> memory;
    auto put=[&](std::uintptr_t address,std::uint32_t value){for(unsigned i=0;i<4;++i)memory[address+i]=static_cast<unsigned char>(value>>(8*i));};
    auto read=[&](std::uintptr_t address,void* output,std::size_t count){for(std::size_t i=0;i<count;++i){auto it=memory.find(address+i);if(it==memory.end())return false;static_cast<unsigned char*>(output)[i]=it->second;}return true;};
    for(unsigned i=0;i<0x40;++i)memory[0x20020+i]=0;
    put(0xd38e38,0x20000);put(0xd38e58,0);put(0x20044,0x30000);put(0x30038,0x40000);
    auto ids=readIdentities(read);assert(ids.valid==3&&ids.texture[0]==0x40000&&!ids.texture[1]);
    memory[0x20028]=4;put(0x2005c,0x50000);put(0x50018,0x60000);put(0x60038,0x70000);
    ids=readIdentities(read);assert(ids.valid==3&&ids.texture[0]==0x70000);
    put(0x50018,0);ids=readIdentities(read);assert(ids.valid==3&&ids.texture[0]==0); // Lazy variant GX.
    memory[0x20028]=0;put(0x20044,0);ids=readIdentities(read);assert(ids.valid==3&&!ids.texture[0]); // Lazy ordinary GX.
    put(0x20044,0x30000);put(0x30038,0);ids=readIdentities(read);assert(ids.valid==3&&!ids.texture[0]); // Lazy D3D allocation.
    put(0x20044,0xfffffff0);ids=readIdentities(read);assert(ids.valid==2); // Invalid overflow pointer.
    put(0x20044,0x30000);put(0x30038,0x40000);unsigned reads=0;
    auto torn=[&](std::uintptr_t address,void* output,std::size_t count){if(address==0x30038&&++reads==2)put(address,0x40004);return read(address,output,count);};
    assert(readIdentities(torn).valid==2);
    // Unrelated particle churn cannot consume a texture-discovery budget or
    // suppress either disc. Mid-frame streaming suppresses only changed body.
    Identities initial;initial.valid=3;initial.texture[0]=0x40000;initial.texture[1]=0x70000;
    IdentityFrame frame;frame.begin(initial);for(unsigned i=0;i<10000;++i)frame.observe(0x80000+i*4);frame.finish(initial);
    assert(frame.canFallback(0)&&frame.canFallback(1));
    frame.reset();frame.begin(initial);frame.observe(0x40000);frame.finish(initial);assert(!frame.canFallback(0)&&frame.canFallback(1));
    frame.reset();frame.begin(initial);auto changed=initial;changed.texture[1]=0x90000;frame.observe(0x90000);frame.finish(changed);
    assert(frame.canFallback(0)&&!frame.canFallback(1)&&frame.uncertainMask()==2);
    frame.reset();frame.begin(changed);frame.observe(0x90000);frame.finish(changed);assert(frame.mask()==2&&frame.canFallback(0));
    initial.texture[0]=initial.texture[1]=0;frame.reset();frame.begin(initial);frame.finish(initial);assert(frame.canFallback(0)&&frame.canFallback(1));
    // 0.3.162: the secondary moon (moon02, third record) is read and claimed like the
    // sun and primary moon, but never observed, never uncertain and never fallback-drawn.
    assert(BodyRecords[2]==BodyRecords[1]+32&&BodyRecords[1]==BodyRecords[0]+32);
    for(unsigned i=0;i<0x40;++i)memory[0x120020+i]=0;
    put(0x30038,0x40000);put(0xd38e78,0x120000);put(0x120044,0x130000);put(0x130038,0x140000);
    ids=readIdentities(read);assert(ids.valid==7&&ids.texture[0]==0x40000&&!ids.texture[1]&&ids.texture[2]==0x140000);
    put(0x120044,0xfffffff0);assert(readIdentities(read).valid==3);put(0x120044,0x130000); // invalid moon02 chain: no claim
    Identities three;three.valid=7;three.texture[0]=0x40000;three.texture[1]=0x70000;three.texture[2]=0x140000;
    frame.reset();frame.begin(three);assert(frame.claimIndex(0x40000)==0&&frame.claimIndex(0x70000)==1&&frame.claimIndex(0x140000)==2&&frame.claimIndex(0x150000)<0);
    frame.observe(0x140000);auto moved=three;moved.texture[2]=0x150000;frame.finish(moved);
    assert(frame.mask()==0&&frame.uncertainMask()==0&&frame.canFallback(0)&&frame.canFallback(1));
    auto noMoon2=three;noMoon2.valid=3;frame.reset();frame.begin(noMoon2);assert(frame.claimIndex(0x140000)<0&&frame.claimIndex(0x70000)==1);
    frame.reset();assert(frame.claimIndex(0x140000)<0); // not begun
    // Every fixed-build identity signature must pass exactly and fail mutation.
    std::size_t sigCount=0;const auto* signatures=identitySignatures(sigCount);
    for(std::size_t i=0;i<sigCount;++i)for(std::size_t j=0;j<signatures[i].size;++j)memory[signatures[i].address+j]=signatures[i].bytes[j];
    assert(verifyIdentityCode(read));for(std::size_t i=0;i<sigCount;++i){memory[signatures[i].address]^=1;assert(!verifyIdentityCode(read));memory[signatures[i].address]^=1;}
    for(float size:{0.f,.5f,1.f,12.f}){
        NorthlightCelestial::BodyRecord record={};record.center[2]=12;record.color=0xffffffff;record.texture=1;record.size=size;
        NorthlightCelestial::Body body;float camera[3]={};assert(NorthlightCelestial::decodeBody(record,camera,body));
        assert(std::fabs(body.angularRadius-std::atan(size/24.f))<1e-7);
    }
    for(int degrees=1;degrees<=90;++degrees){
        float angle=degrees*3.14159265358979323846f/180,dir[3]={std::cos(angle),0,std::sin(angle)};
        assert(prepare(dir,white,1,.02,d));float a=0,b=0,c=0,r=0,u=0;
        for(int i=0;i<3;++i){a+=d.direction[i]*d.right[i];b+=d.direction[i]*d.up[i];c+=d.right[i]*d.up[i];r+=d.right[i]*d.right[i];u+=d.up[i]*d.up[i];}
        assert(std::fabs(a)<1e-5&&std::fabs(b)<1e-5&&std::fabs(c)<1e-5&&std::fabs(r-1)<1e-5&&std::fabs(u-1)<1e-5);
        assert(d.opacity>0&&d.opacity<=1);++cases;
    }
    // A partially set disc keeps its brightness; it is gone when its top limb sets.
    float horizon[3]={1,0,0};assert(prepare(horizon,white,1,.02,d)&&d.opacity==1);
    for(float radius:{.02f,.11f,.46f})for(float edge:{0.f,-.03f,-.0523f}){
        const float angle=std::asin(edge);
        for(float elevation:{angle,angle-radius*.99f}){
            float limb[3]={std::cos(elevation),0,std::sin(elevation)};
            assert(prepare(limb,white,.7f,radius,d,edge)&&d.opacity==.7f);
        }
        const float below=angle-radius-.0001f;float dir[3]={std::cos(below),0,std::sin(below)};
        assert(!prepare(dir,white,1,radius,d,edge));
    }
    {const float angle=-9.86f*.01745329252f;float parked[3]={std::cos(angle),0,std::sin(angle)};
     assert(!prepare(parked,white,1,.11f,d,-.0523f));} // reported enlarged moon below the high-camera horizon
    {const float z=-.01f;float dir[3]={std::sqrt(1-z*z),0,z};
     assert(prepare(dir,white,1,.02f,d,.1f)&&d.opacity==1);
     assert(prepare(dir,white,1,.02f,d,NAN)&&d.opacity==1);}
    horizon[2]=NAN;assert(!prepare(horizon,white,1,.02,d));
    float zenith[3]={0,0,1};assert(prepare(zenith,white,1,.02,d));float inverse[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};int bounds[4];
    assert(screenBounds(d,inverse,1,1,1,1920,1080,bounds));assert(bounds[0]<960&&bounds[2]>960&&bounds[1]<540&&bounds[3]>540);
    assert(bounds[2]-bounds[0]<50&&bounds[3]-bounds[1]<30);assert(!screenBounds(d,inverse,1,1,-1,1920,1080,bounds));
    // Bounds contain the projected disc, including signed projections.
    for(float px:{-1.7f,1.7f})for(float py:{-2.1f,2.1f}){
        assert(screenBounds(d,inverse,px,py,1,1920,1080,bounds));
        for(int angle=0;angle<360;++angle){float a=angle*.01745329252f;
            float x=d.direction[0]+d.tangentRadius*(std::cos(a)*d.right[0]+std::sin(a)*d.up[0]);
            float y=d.direction[1]+d.tangentRadius*(std::cos(a)*d.right[1]+std::sin(a)*d.up[1]);
            float pixelX=(x*px*.5f+.5f)*1920,pixelY=(.5f-y*py*.5f)*1080;
            assert(pixelX>=bounds[0]&&pixelX<bounds[2]&&pixelY>=bounds[1]&&pixelY<bounds[3]);++cases;
        }
    }
    for(const char* body:{"sun","moon"}){
        std::string root=argv[1];Texture t;assert(loadTexture(root+"/"+body+".fct",t));assert(t.width&&t.height&&t.rgba.size()==t.width*t.height*4);
        std::vector<MatchMip> refs;assert(loadMatch(root+"/"+body+".fcm",refs));assert(!refs.empty());
        MatchMip exact=refs.front();assert(matches(exact,refs));
        MatchMip expanded;expanded.width=exact.width;expanded.height=exact.height;expanded.format=21;
        for(size_t i=0;i<exact.rgba.size();i+=4){expanded.bytes.push_back(exact.rgba[i+2]);expanded.bytes.push_back(exact.rgba[i+1]);expanded.bytes.push_back(exact.rgba[i]);expanded.bytes.push_back(exact.rgba[i+3]);}
        assert(matches(expanded,refs));expanded.bytes[3]^=255;expanded.rgba.clear();assert(!matches(expanded,refs));
        std::vector<MatchMip> other;assert(loadMatch(root+"/"+(std::string(body)=="sun"?"moon":"sun")+".fcm",other));assert(!matches(exact,other));
    }
    std::cout<<"celestial-disc: "<<cases<<" basis/scissor cases, original asset checks and audited pointer identity/streaming passed\n";
}
