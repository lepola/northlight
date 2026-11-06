#include "celestial_profiles.h"
#ifdef near
#undef near
#endif
#include <cassert>
#include <iostream>
using namespace NorthlightCelestialProfiles;
static bool near(float a,float b,float eps=1e-5f){return std::fabs(a-b)<eps;}
static float luma(const float* c){return c[0]*.2126f+c[1]*.7152f+c[2]*.0722f;}
int main(int argc,char** argv){
    assert(argc==2);Table t;std::string error;
    assert(load(std::string(argv[1])+"/celestial-profiles.ini",t,error));assert(t.zones.count({"Azeroth",85})==1);
    const auto configuredCount=t.zones.size();
    const auto& prairie=t.at("Kalimdor",215);
    assert(near(prairie.fogGain[0],4.3f)&&near(prairie.fogGain[1],2.5f));
    assert(near(fogHeightScale(prairie,0),2.f)&&near(fogHeightScale(prairie,1),1));
    assert(near(fogHeightScale(prairie,.5f),1.5f));
    assert(near(t.fallback.fogGain[0],4.3f)&&t.fallback.fogHeightScale[0]==2);
    assert(near(t.fallback.fogGain[1],2.5f));
    for(const auto& entry:t.zones){
        assert(near(entry.second.fogGain[1],2.5f)&&entry.second.fogHeightScale[1]==1);
        if(entry.first==std::make_pair(std::string("Azeroth"),10u))
            assert(near(entry.second.fogGain[0],4.3f)&&entry.second.fogHeightScale[0]==1);
        else if(entry.first==std::make_pair(std::string("Azeroth"),85u)) /* Tirisfal: sun_fog_gain 3.4 since 2026-09-24 */
            assert(near(entry.second.fogGain[0],3.4f)&&entry.second.fogHeightScale[0]==2);
        else if(entry.first==std::make_pair(std::string("Azeroth"),33u)) /* Stranglethorn: sun_fog_height_scale 3 (2026-10-17 tuning) */
            assert(near(entry.second.fogGain[0],4.3f)&&entry.second.fogHeightScale[0]==3);
        else if(entry.first!=std::make_pair(std::string("Kalimdor"),215u))
            assert(near(entry.second.fogGain[0],4.3f)&&entry.second.fogHeightScale[0]==2);
    }
    // glow_strength was split from light_strength: the zones that lowered light_strength before the split keep that glow.
    assert(t.fallback.glowStrength[0]==t.fallback.strength[0]&&t.fallback.glowStrength[1]==t.fallback.strength[1]);
    assert(near(t.at("Azeroth",10).glowStrength[0],.90f)&&near(t.at("Kalimdor",148).glowStrength[0],.95f));
    // All volume knobs inherit and interpolate without changing surface light.
    auto middle=blend(t.fallback,prairie,.5f);
    assert(near(middle.fogGain[0],4.3f)&&near(middle.fogHeightScale[0],2.f));
    for(const char* bad:{"sun_fog_gain=-1","sun_fog_gain=8.1","sun_fog_height_scale=0","sun_fog_height_scale=nan","moon_fog_height_scale=4.1"}){
        Table check;std::istringstream input(std::string("[Kalimdor:215]\n")+bad);
        assert(!parse(input,check,error));
    }
    {
        Table check;std::istringstream input("[default]\nsun_fog_gain=2\nsun_fog_height_scale=1.25\n[Kalimdor:215]\nmoon_fog_gain=0\n");
        assert(parse(input,check,error));const auto& p=check.at("Kalimdor",215);
        assert(p.fogGain[0]==2&&p.fogGain[1]==0&&p.fogHeightScale[0]==1.25f);
        NorthlightCelestial::Context a,b;a.sunWeight=b.sunWeight=1;float direct[3]={1,.7f,.4f};
        apply(p,direct,a);apply(Profile{},direct,b);
        for(unsigned c=0;c<3;++c)assert(a.sunColor[c]==b.sunColor[c]);
    }
    // Horizon haze multipliers: identity in [default] and every shipped zone but Crystalsong, day/night
    // blend like fog_height_scale, 0..4, atomic reject, never surface light.
    assert(t.fallback.horizonHaze[0]==1&&t.fallback.horizonHaze[1]==1&&near(horizonHaze(t.fallback,.3f),1));
    for(const auto& entry:t.zones)if(entry.first!=std::make_pair(std::string("Northrend"),2817u))assert(entry.second.horizonHaze[0]==1&&entry.second.horizonHaze[1]==1);
    assert(near(t.at("Northrend",2817).horizonHaze[0],2.46f)&&t.at("Northrend",2817).horizonHaze[1]==1); /* Crystalsong (2026-10-17 tuning) */
    for(const char* bad:{"sun_horizon_haze=-1","sun_horizon_haze=4.1","moon_horizon_haze=nan","sun_horizon_haze=1,1","sun_horizon_haze=1\nsun_horizon_haze=2"}){
        Table check;std::istringstream input(std::string("[Kalimdor:215]\n")+bad);
        assert(!parse(input,check,error));
    }
    {
        Table check;std::istringstream input("[default]\nsun_horizon_haze=2\n[Kalimdor:215]\nmoon_horizon_haze=0\n[Kalimdor:14]\nsun_horizon_haze=4\n");
        assert(parse(input,check,error));const auto& p=check.at("Kalimdor",215);
        assert(p.horizonHaze[0]==2&&p.horizonHaze[1]==0&&check.at("Kalimdor",14).horizonHaze[1]==1&&check.at("Kalimdor",14).horizonHaze[0]==4);
        assert(near(horizonHaze(p,0),2)&&near(horizonHaze(p,1),0)&&near(horizonHaze(p,.25f),1.5f)&&near(horizonHaze(p,-1),2)&&near(horizonHaze(p,2),0));
        assert(near(blend(Profile{},p,.5f).horizonHaze[0],1.5f)&&near(blend(Profile{},p,.5f).horizonHaze[1],.5f));
        NorthlightCelestial::Context a,b;a.sunWeight=b.sunWeight=1;float direct[3]={1,.7f,.4f};
        apply(p,direct,a);apply(Profile{},direct,b);
        for(unsigned c=0;c<3;++c)assert(a.sunColor[c]==b.sunColor[c]&&a.sun.tint[c]==b.sun.tint[c]);
    }
    const auto& green=t.at("Azeroth",85);assert(green.disc[1][1]>green.disc[1][0]);
    assert(t.at("Azeroth",999999).mix[1]==0&&t.at("Kalimdor",85).mix[1]==0);
    unsigned rejected=0;
    for(const char* bad:{"[Azeroth:85]\nmoon_disc=nan,1,0", "[Azeroth:85]\nmoon_light_strength=-1", "[Azeroth:85]\nmoon_light_mix=1.1", "[Azeroth:85]\nmoon_disc=0,1,0,1", "[Azeroth:85]\nmoon_light=0,0,0", "[Azeroth:85]\nunknown=1", "[Azeroth:85]\nmoon_disc_gain=9", "[Azeroth:85]\n[default]", "[Azeroth:85]\n[Azeroth:85]", "[Azeroth:0]", "[bad:85]", "[Azeroth:85]\nmoon_disc_gain=1\nmoon_disc_gain=2"}){
        std::istringstream in(bad);assert(!parse(in,t,error));assert(t.zones.size()==configuredCount&&near(t.at("Azeroth",85).mix[1],.8f));++rejected;
    }
    // Sun and moon recolor independently, conserve luminance until strength is
    // changed, and allocate no energy below the horizon. Default is identity.
    for(unsigned v=0;v<256;++v){
        NorthlightCelestial::Context sky;sky.sun.nativeAppearance=true;sky.sun.nativeTint[0]=float(v)/255;sky.sun.nativeTint[1]=.25f;sky.sun.nativeTint[2]=.75f;
        sky.sun.angularRadius=sky.sun.nativeAngularRadius=.023456f;sky.sun.direction[2]=1;sky.moon.direction[2]=-1;
        const float light[]={.7f,.6f,.4f};NorthlightCelestial::applyRendererPolicy(sky,light);auto custom=green;
        custom.disc[0][0]=0;custom.disc[0][1]=1;custom.disc[0][2]=0;apply(custom,light,sky);
        for(unsigned k=0;k<3;++k)assert(sky.sun.tint[k]==sky.sun.nativeTint[k]);
        assert(sky.sun.angularRadius==.023456f&&sky.sun.nativeAngularRadius==.023456f&&sky.sun.emission==custom.gain[0]);
    }
    unsigned cases=0;float native[]={.32157f,.75294f,.98824f};
    for(unsigned i=0;i<=1000;++i){
        NorthlightCelestial::Context sky;sky.sunWeight=float(i)/1000;sky.moonWeight=1-sky.sunWeight;
        auto p=green;p.mix[0]=1;p.light[0][0]=1;p.light[0][1]=.5f;p.light[0][2]=.2f;
        apply(p,native,sky);
        assert(near(luma(sky.sunColor)+luma(sky.moonColor),luma(native)));
        assert(near(sky.sun.tint[0],p.disc[0][0])&&near(sky.moon.tint[0],p.disc[1][0]));
        p.strength[1]=0;apply(p,native,sky);assert(luma(sky.moonColor)==0);
        apply(t.fallback,native,sky);for(unsigned c=0;c<3;++c){assert(near(sky.sunColor[c],native[c]*sky.sunWeight));assert(near(sky.moonColor[c],native[c]*sky.moonWeight));}
        ++cases;
    }
    // Real asset zone lookup at the Tirisfal test locations.
    auto region=NorthlightRegionalFog::loadRegion(std::string(argv[1])+"/world-cache/fog","Azeroth",2251.6284f,311.0387f);
    assert(!region.tiles.empty());assert(NorthlightRegionalFog::zoneAt(region,2251.6284f,311.0387f)==85);
    auto actual=sample(t,region,"Azeroth",2251.6284f,311.0387f);assert(actual.mix[1]>.79f);
    // Synthetic tile edge: left Tirisfal, right default. One-millimeter steps
    // across the actual ADT boundary must never switch the palette abruptly.
    NorthlightRegionalFog::Region edge;NorthlightRegionalFog::Tile a,b;a.x=31;a.y=31;b.x=32;b.y=31;a.zones.fill(85);b.zones.fill(999999);edge.tiles={a,b};
    float x=float(NorthlightRegionalFog::WorldZero-31.5*NorthlightRegionalFog::TileSize),y=float(NorthlightRegionalFog::WorldZero-32*NorthlightRegionalFog::TileSize);
    auto center=sample(t,edge,"Azeroth",x,y);assert(near(center.mix[1],.4f,.0001f));
    auto left=sample(t,edge,"Azeroth",x,y-.001f),right=sample(t,edge,"Azeroth",x,y+.001f);assert(std::fabs(left.mix[1]-right.mix[1])<.001f);
    for(int n=-400;n<=400;++n){float yy=y+n*.1f;auto p=sample(t,edge,"Azeroth",x,yy),q=sample(t,edge,"Azeroth",x,yy+.001f);assert(std::fabs(p.mix[1]-q.mix[1])<.001f);}
    Transition transition;auto first=transition.update(t.fallback,"Azeroth",0,0,1);assert(first.mix[1]==0);
    float previous=0;for(int i=1;i<=600;++i){auto p=transition.update(green,"Azeroth",0,0,1+i/60.);assert(p.mix[1]>=previous&&p.mix[1]<=.8f);previous=p.mix[1];}
    assert(previous>.799f);
    auto teleport=transition.update(t.fallback,"Azeroth",1000,0,12);assert(teleport.mix[1]==0);
    auto mapChange=transition.update(green,"Kalimdor",1000,0,13);assert(near(mapChange.mix[1],.8f));
    std::cout<<"PASS profiles: "<<rejected<<" malformed inputs, horizon haze multipliers, "<<cases<<" energy allocations, real Tirisfal asset IDs, tile-boundary continuity and temporal/map/teleport transitions\n";
}
