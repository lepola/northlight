#include "wmo_context.h"
#include <array>
#include <cassert>
#include <cstdio>
#include <limits>
using namespace NorthlightWmoContext;
int main(){
    std::array<unsigned char,SkyBytes> sky{};
    auto set=[&](size_t at,float f){std::memcpy(sky.data()+at,&f,4);};
    auto color=[&](size_t at,std::uint32_t n){std::memcpy(sky.data()+at,&n,4);};
    const float eye[]={120,-450,70};set(4,.75f);for(unsigned i=0;i<3;++i)set(0x18+i*4,eye[i]);
    set(0x19c,-.6f);set(0x1a0,0);set(0x1a4,-.8f);color(0x1a8,0xffa04020);color(0x1ac,0xff204060);
    Lighting light;assert(decodeGlobalLighting(sky.data(),sky.size(),eye,light));
    assert(std::fabs(light.lightDirection[0]-.6f)<1e-6f&&std::fabs(light.lightDirection[2]-.8f)<1e-6f);
    assert(light.direct[0]==160.f/255&&light.ambient[2]==96.f/255&&light.dayFraction==.75f);
    // the 18 light slots at +0xd4 (slot 10 = band 10 sunHalo at +0xfc); unproven until a read proves the code.
    assert(light.slots[0]==0&&!light.slotsProven);
    color(0xd4,0xff2e2538);color(0xfc,0xff243f32);color(0xd4+17*4,0xff123456);
    assert(decodeGlobalLighting(sky.data(),sky.size(),eye,light)&&light.slots[0]==0xff2e2538&&light.slots[10]==0xff243f32&&light.slots[17]==0xff123456&&!light.slotsProven);
    color(0x17c,0xff010203);assert(decodeGlobalLighting(sky.data(),sky.size(),eye,light)&&!light.slotsConsistent); /* slot 1 differs from its raw copy at +0x17c: layout not confirmed */
    color(0xd8,0xff6ba1c1);color(0x17c,0xff6ba1c1);assert(decodeGlobalLighting(sky.data(),sky.size(),eye,light)&&light.slotsConsistent&&!light.slotsProven);
    /* 0.3.163 Tirisfal: weather rescales the ambient (+0x1ac) and direct (+0x1a8) after the copy; the check must not care. */
    color(0x1ac,0xff0a0b0c);color(0x1a8,0xff111213);assert(decodeGlobalLighting(sky.data(),sky.size(),eye,light)&&light.slotsConsistent);
    color(0x1ac,0xff204060);color(0x1a8,0xffa04020);
    {size_t count=0;const auto* proof=lightSlotProofs(count);assert(count==4&&proof[0].address==0x7ec0b8&&proof[2].address==0x7f36f6&&proof[3].address==0x7ee887);}
    for(unsigned i=0;i<=360;++i){
        const float angle=float(i)*.01745329252f,c=std::cos(angle),s=std::sin(angle);
        float view[16]={c,-s,0,0,s,c,0,0,0,0,1,0,0,0,0,1};
        for(unsigned j=0;j<3;++j)for(unsigned k=0;k<3;++k)view[12+j]-=eye[k]*view[4*k+j];
        NorthlightWorldContext::TerrainContext decoded;assert(context(view,light,decoded));
        assert(NorthlightWorldContext::cameraAgrees(decoded,eye));
        for(unsigned j=0;j<3;++j)assert(std::fabs(decoded.lightDirection[j]-light.lightDirection[j])<1e-5f);
        // The camera remains valid when a terrain variant leaves native light
        // registers unset, negative, or nonfinite. Global lighting must win.
        float native[12]={0,0,1,0,.1f,.2f,.3f,0,.4f,.5f,.6f,0};
        for(unsigned mutation=0;mutation<4;++mutation){
            float stale[12];std::memcpy(stale,native,sizeof stale);
            if(mutation==0)stale[2]=0;
            if(mutation==1)stale[4]=-.001f;
            if(mutation==2)stale[8]=std::numeric_limits<float>::quiet_NaN();
            if(mutation==3)stale[11]=std::numeric_limits<float>::infinity();
            assert(!NorthlightWorldContext::decodeTerrain(view,stale,decoded));
            assert(terrainContext(view,stale,eye,&light,decoded));
            for(unsigned j=0;j<3;++j)assert(decoded.direct[j]==light.direct[j]&&decoded.ambient[j]==light.ambient[j]);
            assert(!terrainContext(view,stale,eye,nullptr,decoded));
        }
        assert(terrainContext(view,nullptr,eye,&light,decoded));
        assert(terrainContext(view,native,eye,nullptr,decoded));
        float wrongEye[]={eye[0]+1,eye[1],eye[2]};
        assert(!terrainContext(view,native,wrongEye,&light,decoded));
        float nonrigid[16];std::memcpy(nonrigid,view,sizeof nonrigid);nonrigid[3]=.1f;
        assert(!terrainContext(nonrigid,native,eye,&light,decoded));
        std::uint64_t lit=0,prelit=0;for(const auto& entry:kWmoShaderSignatures){if(entry.lighting)lit=entry.hash;else prelit=entry.hash;}
        assert(lit&&prelit);float constants[12]={};
        for(unsigned j=0;j<3;++j){constants[j]=light.ambient[j];constants[4+j]=light.direct[j];
            for(unsigned k=0;k<3;++k)constants[8+j]-=light.lightDirection[k]*view[k*4+j];}
        assert(decodeLitShader(lit,view,constants,decoded));assert(!decodeLitShader(prelit,view,constants,decoded));
        assert(!decodeLitShader(0,view,constants,decoded));
        for(unsigned j=0;j<3;++j){assert(std::fabs(decoded.lightDirection[j]-light.lightDirection[j])<1e-5f);assert(decoded.ambient[j]==light.ambient[j]&&decoded.direct[j]==light.direct[j]);}
    }
    const auto valid=sky;
    {sky=valid;set(0x18,eye[0]+7);Lighting trailing;assert(decodeGlobalLighting(sky.data(),sky.size(),eye,trailing));sky=valid;} /* 0.3.200: several frames of travel behind the camera are accepted */
    for(unsigned mutation=0;mutation<8;++mutation){sky=valid;
        switch(mutation){case 0:set(4,1);break;case 1:set(4,-.1f);break;case 2:set(4,std::numeric_limits<float>::quiet_NaN());break;
            case 3:set(0x18,eye[0]+50);break;case 4:set(0x19c,0);set(0x1a4,0);break;
            case 5:set(0x19c,2);break;case 6:set(0x1a0,std::numeric_limits<float>::infinity());break;
            case 7:set(0x18,100001);break;}
        Lighting output=light;assert(!decodeGlobalLighting(sky.data(),sky.size(),eye,output));assert(output.dayFraction==light.dayFraction);
    }
    sky=valid;assert(!decodeGlobalLighting(sky.data(),SkyBytes-1,eye,light));assert(!decodeGlobalLighting(nullptr,SkyBytes,eye,light));
    auto torn=sky;torn[0x1a8]^=1;assert(!decodeCoherentGlobalLighting(sky.data(),torn.data(),sky.size(),eye,light));
    assert(decodeCoherentGlobalLighting(sky.data(),sky.data(),sky.size(),eye,light));
    for(size_t i=0;i<sizeof(kWmoShaderSignatures)/sizeof(kWmoShaderSignatures[0]);++i){assert(signature(kWmoShaderSignatures[i].hash)==kWmoShaderSignatures+i);if(i)assert(kWmoShaderSignatures[i-1].hash<kWmoShaderSignatures[i].hash);}
    { /* 0.3.207: a read between the colour copy and the weather rescale keeps the last rescaled colours (Duskwood flash) */
        auto read=[](std::uint32_t direct,std::uint32_t ambient){Lighting l;l.slotsConsistent=true;l.slots[1]=0xff84bce2;l.slots[0]=0xff3a6a9a;
            rgb(direct,l.direct);rgb(ambient,l.ambient);return l;};
        const std::uint32_t scaledDirect=0xff7bafd3,scaledAmbient=0xff366290,rawDirect=0xff84bce2,rawAmbient=0xff3a6a9a;
        LightingHold hold;Lighting l=read(scaledDirect,scaledAmbient);
        assert(!hold.filter(l,"Azeroth",1000));
        l=read(rawDirect,rawAmbient);assert(hold.filter(l,"Azeroth",1016)); /* both colours back to the rescaled ones */
        {float d[3],a[3];rgb(scaledDirect,d);rgb(scaledAmbient,a);for(unsigned i=0;i<3;++i)assert(l.direct[i]==d[i]&&l.ambient[i]==a[i]);}
        l=read(scaledDirect,scaledAmbient);assert(!hold.filter(l,"Azeroth",1032)); /* the race lasts one read */
        l=read(rawDirect,scaledAmbient);assert(hold.filter(l,"Azeroth",1048)); /* direct caught alone */
        l=read(scaledDirect,rawAmbient);assert(hold.filter(l,"Azeroth",1064)); /* between the two rescales */
        {float a[3];rgb(scaledAmbient,a);assert(l.ambient[0]==a[0]&&l.ambient[2]==a[2]);}
        l=read(0xff7aaed2,0xff356190);assert(!hold.filter(l,"Azeroth",1080)); /* a new rescaled colour is taken as read */
        /* the weather ended: MaxHeld unscaled reads in a row are held, the next one is accepted and kept */
        for(unsigned i=0;i<LightingHold::MaxHeld;++i){l=read(rawDirect,rawAmbient);assert(hold.filter(l,"Azeroth",1096+16*i));}
        l=read(rawDirect,rawAmbient);assert(!hold.filter(l,"Azeroth",1128));
        {float d[3];rgb(rawDirect,d);assert(l.direct[1]==d[1]);}
        l=read(rawDirect,rawAmbient);assert(!hold.filter(l,"Azeroth",1144));
        /* a brighter rescale (a lightning flash) is never held over the band colours that follow it */
        l=read(0xff94cce8,0xff4a7aaa);assert(!hold.filter(l,"Azeroth",1160));
        l=read(rawDirect,rawAmbient);assert(!hold.filter(l,"Azeroth",1176));
        {float d[3];rgb(rawDirect,d);assert(l.direct[0]==d[0]&&l.direct[2]==d[2]);}
        /* a long gap without reads (loading screen, alt-tab): the first unscaled read after HoldMs is accepted */
        l=read(scaledDirect,scaledAmbient);assert(!hold.filter(l,"Azeroth",1192));
        l=read(rawDirect,rawAmbient);assert(!hold.filter(l,"Azeroth",1192+LightingHold::HoldMs+1));
        l=read(scaledDirect,scaledAmbient);assert(!hold.filter(l,"Azeroth",2000));
        l=read(rawDirect,rawAmbient);assert(!hold.filter(l,"Kalimdor",2016)); /* a new map forgets the hold */
        l=read(scaledDirect,scaledAmbient);assert(!hold.filter(l,"Kalimdor",2032));
        l=read(rawDirect,rawAmbient);l.slotsConsistent=false;assert(!hold.filter(l,"Kalimdor",2048)); /* unconfirmed slot layout: as read */
        assert(hold.held==3+LightingHold::MaxHeld);
        /* a copy filtered for a rejected read leaves the kept state as it was (the renderer keeps the copy only on acceptance) */
        LightingHold kept;l=read(scaledDirect,scaledAmbient);kept.filter(l,"Azeroth",3000);
        {auto copy=kept;l=read(rawDirect,rawAmbient);assert(copy.filter(l,"Azeroth",3016));}
        assert(kept.held==0&&kept.direct.run==0);
    }
    std::puts("PASS sky block reads between the colour copy and the weather rescale keep the last rescaled colours");
    std::puts("PASS 361 independent camera rotations, exact WMO ambient/direct/source sign and prelit/unknown rejection");
    std::puts("PASS engine sky RGB/255, fresh camera/day/direction checks, torn snapshot and malformed input rejection");
    std::puts("PASS all1967 exact WMO-only shader identities and sorted lookup");
    std::puts("PASS 1444 stale terrain-light cases with stable global lighting, native fallback, camera/rigidity rejection");
}
