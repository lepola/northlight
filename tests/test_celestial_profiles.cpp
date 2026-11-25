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
        else if(entry.first==std::make_pair(std::string("Azeroth"),85u)||entry.first==std::make_pair(std::string("Azeroth"),159u)) /* Tirisfal: sun_fog_gain 3.4 since 2026-09-24 */
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
    for(const char* bad:{"[Azeroth:85]\nmoon_disc=nan,1,0", "[Azeroth:85]\nmoon_light_strength=-1", "[Azeroth:85]\nmoon_light_mix=1.1", "[Azeroth:85]\nmoon_disc=0,1,0,1", "[Azeroth:85]\nmoon_light=0,0,0", "[Azeroth:85]\nunknown=1", "[Azeroth:85]\nmoon_disc_gain=9", "[Azeroth:85]\n[default]", "[Azeroth:85]\n[Azeroth:85]", "[Azeroth:0]", "[../bad:85]", "[Azeroth:85]\nmoon_disc_gain=1\nmoon_disc_gain=2"}){
        std::istringstream in(bad);assert(!parse(in,t,error));assert(t.zones.size()==configuredCount&&near(t.at("Azeroth",85).mix[1],.8f));++rejected;
    }
    // Sun and moon recolor independently, conserve luminance until strength is
    // changed, and allocate no energy below the horizon. Default is identity.
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
    {
        Table test;std::istringstream ini("[default]\nfog_tint_strength=.8\n[Azeroth:85/159]\nfog_tint_strength=0\n[Azeroth:85]\nshadow_contrast=.6\n[Azeroth:1497@inside]\nshadow_contrast=.3\n");
        assert(parse(ini,test,error)&&test.needsLiveZone());
        assert(test.at("Azeroth",159).shadowContrast==.6f&&test.interiors.count({"Azeroth",1497}));
        edge.tiles[0].areas.fill(159);
        assert(near(sample(test,edge,"Azeroth",x,y+40).fogTintStrength[0],0));
        assert(near(sample(test,edge,"Azeroth",x,y).fogTintStrength[0],.4f));
        edge.tiles[0].areas.fill(0);
        assert(near(sample(test,edge,"Azeroth",x,y+40).fogTintStrength[0],.8f));
        const float nativeFog[]={.3f,.4f,.35f};
        auto delta=appearance(test.at("Azeroth",85),nativeFog,false);
        assert(delta[0]==0&&delta[1]==0&&delta[2]==0&&delta[3]>0);
        assert(!t.needsLiveZone());
    }
    {
        Table table;
        std::istringstream input("[Azeroth:85/159]\nnight_midtone_level=.44\n[Azeroth:85]\n"
            "fog_tint_strength=1.7\ntone_curve_strength=1\nday_shadow_level=.21\nnight_shadow_level=.27\n");
        assert(parse(input,table,error));
        auto p=table.at("Azeroth",159);
        assert(near(p.fogTintStrength[0],1.7f)&&near(p.grade[2].curves[0].levels[1],.44f));
        const auto day=toneCoefficients(p,0),night=toneCoefficients(p,1),dusk=toneCoefficients(p,.5f);
        for(unsigned i=0;i<4;++i)assert(near(dusk[i],(day[i]+night[i])*.5f));
        p.toneStrength=0;
        assert((toneCoefficients(p,.5f)==std::array<float,4>{}));
        Profile identity;identity.toneStrength=1;
        assert((toneCoefficients(identity,1)==std::array<float,4>{}));
        // Even the most compressed neighbouring points must retain their brightness order.
        for(unsigned mask=0;mask<8;++mask){
            Profile extreme;extreme.toneStrength=1;
            for(unsigned i=0;i<3;++i)extreme.grade[0].curves[0].levels[i]=.25f*(i+1)+((mask&(1u<<i))?.12f:-.12f);
            const auto coeff=toneCoefficients(extreme,0);
            float previous=-1;
            for(unsigned code=0;code<=255;++code){
                const float in=code/255.f,r=2.f/255.f;
                float out=in*(1+coeff[3]);
                for(unsigned i=0;i<3;++i){
                    const float x=in-.25f*(i+1),shoulder=std::clamp(x+r,0.f,2*r);
                    out+=coeff[i]*(std::max(x-r,0.f)+shoulder*shoulder/(4*r));
                }
                assert(out>previous);
                if(code==0||code==255)assert(near(out,in));
                previous=out;
            }
        }
        // The darkest permitted swatch at double gain must still leave nonnegative fog radiance.
        p.fogTintStrength[0]=2;p.fogTint[0][0]=0;p.fogTint[0][1]=p.fogTint[0][2]=1;
        const float white[]={1,1,1};const auto delta=appearance(p,white,true);
        assert(delta[0]>=-1.f&&std::abs(luma(delta.data()))<.00001f);
        assert(assignKey(p,"day_fog_tint_strength","2.01")&&assignKey(p,"night_midtone_level",".1"));
        assert(assignKey(p,"day_highlight_level",".95"));
    }
    {
        // Fog has a value per time of day. A look from before that sets one for all hours.
        auto read=[](const char* text){Table table;std::istringstream ini(text);std::string error;const bool ok=parse(ini,table,error);assert(ok);return table;};
        const Table old=read("[default]\nfog_tint=.2,.4,.6\nfog_tint_strength=1.5\n");
        for(unsigned time=0;time<3;++time)assert(near(old.fallback.fogTint[time][2],.6f)&&near(old.fallback.fogTintStrength[time],1.5f)&&near(old.fallback.fogBrightness[time],1));
        // A per-time value beats the all-hours one of its own section, whichever is written first.
        for(const char* text:{"[default]\nnight_fog_tint_strength=.3\nfog_tint_strength=1.5\n","[default]\nfog_tint_strength=1.5\nnight_fog_tint_strength=.3\n"}){
            const Table mixed=read(text);
            assert(near(mixed.fallback.fogTintStrength[0],1.5f)&&near(mixed.fallback.fogTintStrength[1],1.5f)&&near(mixed.fallback.fogTintStrength[2],.3f));
        }
        // The nearer section wins: a region's all-hours value replaces a per-time value set above it.
        const Table nearer=read("[default]\nnight_fog_tint_strength=.3\ndusk_fog_brightness=.5\n[Azeroth:default]\n[Azeroth:10]\nfog_tint_strength=.9\n[Azeroth:12]\n");
        assert(near(nearer.at("Azeroth",10).fogTintStrength[2],.9f)&&near(nearer.at("Azeroth",12).fogTintStrength[2],.3f));
        assert(near(nearer.at("Azeroth",10).fogBrightness[1],.5f)&&nearer.needsLiveZone());
        {Table bad;std::istringstream ini("[default]\nfog_tint_strength=2.5\n");std::string error;assert(!parse(ini,bad,error));}
        {Table bad;std::istringstream ini("[default]\nday_fog_brightness=2.5\n");std::string error;assert(!parse(ini,bad,error));}
        assert(allHoursFogKeyOf("dusk_fog_tint_strength")=="fog_tint_strength"&&allHoursFogKeyOf("day_fog_tint")=="fog_tint");
        assert(allHoursFogKeyOf("day_fog_brightness").empty()&&allHoursFogKeyOf("fog_tint").empty()&&allHoursFogKeyOf("sun_fog_tint").empty());

        const float mist[]={.4f,.5f,.6f};
        const NorthlightTone::TimeWeights day{1,0,0},dusk{0,1,0},night{0,0,1},halfway{0,.5f,.5f};
        // One value for all hours gives the same fog at every hour, and the same as before fog had times.
        const auto all=appearance(old.fallback,mist,true,day);
        assert(all==appearance(old.fallback,mist,true)&&all==appearance(old.fallback,mist,true,night));
        const auto mixedHours=appearance(old.fallback,mist,true,halfway);
        for(unsigned c=0;c<3;++c)assert(near(mixedHours[c],all[c]));
        // A night tint leaves the day alone and arrives with the clock.
        Profile nightOnly;nightOnly.fogTintStrength[2]=1.5f;nightOnly.fogTint[2][0]=.2f;nightOnly.fogTint[2][1]=.4f;nightOnly.fogTint[2][2]=.6f;
        assert(appearance(nightOnly,mist,true,day)==(std::array<float,4>{})&&appearance(nightOnly,mist,true,dusk)==(std::array<float,4>{}));
        const auto atNight=appearance(nightOnly,mist,true,night),between=appearance(nightOnly,mist,true,halfway);
        for(unsigned c=0;c<3;++c)assert(near(atNight[c],all[c])&&near(between[c],.5f*all[c]));
        // Brightness scales every channel of the fog alike, and a tint under it keeps its hue.
        Profile dim;dim.fogBrightness[0]=.7f;
        const auto dimmed=appearance(dim,mist,true,day);
        for(unsigned c=0;c<3;++c)assert(near(dimmed[c],-.3f));
        assert(appearance(dim,mist,true,night)==(std::array<float,4>{})&&appearance(dim,mist,false,day)==(std::array<float,4>{}));
        Profile both=old.fallback;both.fogBrightness[0]=.7f;
        const auto tintedDim=appearance(both,mist,true,day);
        for(unsigned c=0;c<3;++c)assert(near(1+tintedDim[c],.7f*(1+all[c])));
        Profile black;black.fogBrightness[0]=0;
        assert(near(appearance(black,mist,true,day)[1],-1));
    }
    {
        using namespace NorthlightTone;
        Table legacy;std::istringstream old("[default]\nday_red_midtone_level=.58\nnight_red_midtone_level=.44\nday_vibrance=.2\nnight_vibrance=.8\n");
        assert(parse(old,legacy,error));
        const auto& p=legacy.fallback;
        assert(!legacy.explicitFallbackKeys.count("dusk_red_midtone_level"));
        assert(near(p.grade[1].curves[1].levels[1],.58f+(.44f-.58f)*DuskNightWeight));
        for(float n:{0.f,.2f,.5f,.84375f,.9f,1.f}){
            const auto direct=sample(p.grade,n,false),evening=sample(p.grade,n,true);
            for(unsigned c=0;c<4;++c)for(unsigned i=0;i<3;++i)
                assert(near(direct.curves[c].levels[i],evening.curves[c].levels[i]));
            assert(near(direct.vibrance,evening.vibrance));
        }
        assert(near(sample(p.grade,DuskNightWeight,true).curves[1].levels[1],p.grade[1].curves[1].levels[1]));
        Table edited;std::istringstream custom("[default]\nday_red_hue_shift=-12\ndusk_red_hue_shift=23\nnight_blue_saturation=-.7\ndusk_orange_lightness=-.2\ndusk_vibrance=.6\n");
        assert(parse(custom,edited,error));
        const auto& authored=edited.fallback;
        assert(edited.explicitFallbackKeys.count("dusk_red_hue_shift"));
        assert(near(authored.grade[0].hueShift[0],-12)&&near(authored.grade[1].hueShift[0],23));
        assert(near(authored.grade[2].saturation[unsigned(NorthlightTone::HueBand::Blue)],-.7f)&&near(authored.grade[1].lightness[1],-.2f));
        const auto atDusk=gradeAt(authored,DuskNightWeight,true);
        assert(near(atDusk.hueShift[0],23)&&near(atDusk.vibrance,.6f));
        const auto dawn=gradeAt(authored,.5f,false);
        assert(near(dawn.hueShift[0],-6)&&near(dawn.hueShift[4],0));
        for(const char* bad:{"dusk_red_hue_shift=-30.1","night_green_saturation=-1.01","dusk_blue_lightness=-.251"}){
            Table invalid;std::istringstream malformed(std::string("[default]\n")+bad);assert(!parse(malformed,invalid,error));
        }
        Table inherited;std::istringstream child("[default]\nday_vibrance=.2\nnight_vibrance=.8\n[Expansion01:100]\ndusk_vibrance=.75\n[Expansion01:100/101]\nday_vibrance=.4\n");
        assert(parse(child,inherited,error));
        assert(near(inherited.at("Expansion01",101).grade[1].vibrance,.75f));
        Table sparse;std::istringstream noDusk("[default]\nday_vibrance=.2\nnight_vibrance=.8\n[Expansion01:100/101]\nday_vibrance=.4\n");
        assert(parse(noDusk,sparse,error));
        assert(!sparse.explicitKeys.at({"Expansion01",101}).count("dusk_vibrance"));
        assert(near(sparse.at("Expansion01",101).grade[1].vibrance,.4f+(.8f-.4f)*DuskNightWeight));
        std::cout<<"PASS grade timeline: legacy dusk parity, explicit 19:30 endpoint, dawn isolation, selective ranges and inheritance\n";
    }
    {
        using namespace NorthlightTone;
        Table regions;
        std::istringstream input("[Azeroth:85]\ntone_curve_strength=1\nday_red_midtone_level=.58\n"
                                 "[Azeroth:85/159]\nnight_low_input=.08\nnight_low_bend=.80\n");
        assert(parse(input,regions,error));
        const auto& timeline=regions.at("Azeroth",159).grade;
        assert(!tableKey(sample(timeline,.5f,false),0).strength);
        std::array<uint16_t,256*4> pixels{};
        for(float night:{0.f,.5f,1.f}){
            auto key=tableKey(sample(timeline,night,false),1);
            assert(key.strength);
            bake(key,pixels.data());
            for(unsigned c=0;c<4;++c){
                assert(pixels[c]==0&&pixels[255*4+c]==65535);
                for(unsigned x=1;x<256;++x)assert(pixels[x*4+c]>=pixels[(x-1)*4+c]);
            }
            assert(pixels[128*4+1]==128*257&&pixels[128*4+2]==128*257);
            if(night==0){assert(pixels[128*4]>143*257&&pixels[20*4+3]==20*257);}
            if(night==1){assert(pixels[128*4]==128*257&&pixels[20*4+3]>35*257);}
        }
        assert(!tableKey(sample(regions.at("Azeroth",85).grade,1,false),1).strength);
        Curve low;low.lowInput=.06f;low.levels[0]=.17f;
        for(float y:{.008f,.04f,.09f,.16f}){
            low.lowBend=bendForOutput(low,y);
            assert(near(lowOutput(low),y));
        }
    }
    {
        using namespace NorthlightTone;
        Profile base,day;
        assert(!assignKey(day,"day_vibrance",".65")&&assignKey(day,"night_vibrance","1.1"));
        assert(near(blend(base,day,.5f).grade[0].vibrance,.325f));
        assert(!tableKey(base.grade[0],1,true).strength);
        base.grade[0].curves[0].levels[1]=.55f;
        assert(tableKey(base.grade[0],1,true).strength&&!tableKey(base.grade[0],1,false).strength);
        const unsigned pitch=CubeSize*4+8,slice=pitch*CubeSize+8;
        std::vector<uint16_t> voxels(slice*CubeSize,0xaaaa);
        bakeColourVolume(voxels.data(),pitch*2,slice*2,vibrantColour);
        for(unsigned z=0;z<CubeSize;++z)for(unsigned y=0;y<CubeSize;++y){
            const auto* row=voxels.data()+z*slice+y*pitch;
            assert(row[CubeSize*4]==0xaaaa);
            for(unsigned x=0;x<CubeSize;++x){
                const float in[]={float(x)/(CubeSize-1),float(y)/(CubeSize-1),float(z)/(CubeSize-1)};
                float out[3];for(unsigned c=0;c<3;++c)out[c]=row[4*x+c]/65535.f;
                assert(std::abs(luma(in)-luma(out))<.00003f&&row[4*x+3]==65535);
                if(x==y&&y==z)assert(near(out[0],out[1])&&near(out[1],out[2]));
            }
        }
        const auto rose=vibrantColour(.65f,.45f,.40f),purple=vibrantColour(.75f,.02f,.9f);
        assert(rose[0]>.65f&&rose[2]<.40f&&near(purple[0],.75f)&&near(purple[1],.02f)&&near(purple[2],.9f));
    }
    {
        assert(safeMapName("Stratholme Raid")&&safeMapName("Zul'gurub"));
        assert(!safeMapName("../Zul'gurub")&&!safeMapName("Azeroth\\Kalimdor"));
        assert(!safePaletteName("../raid")&&!safePaletteName(""));
        Table instance;
        std::istringstream input(
            "[default]\nmoon_disc_gain=2\n"
            "[Stratholme Raid:default]\nsun_disc_gain=1.4\n"
            "[Stratholme Raid:329]\nsun_disc_gain=2.1\n"
            "[Stratholme Raid:329/330]\nmoon_disc_gain=3\n"
            "[Zul'gurub:1977]\nsun_disc_gain=2.7\n"
            "[Blackfathom:default]\nmoon_disc_gain=1.8\n"
            "[Blackfathom:719]\nsun_disc_gain=2.3\n"
            "[Appearance]\nPalette=Classic Raid\n");
        assert(parse(input,instance,error));
        assert(instance.palette=="Classic Raid");
        assert(instance.explicitFallbackKeys.count("moon_disc_gain"));
        assert(instance.explicitMapFallbackKeys.at("Stratholme Raid").count("sun_disc_gain"));
        const auto& raidDefault=instance.at("Stratholme Raid",999);
        assert(near(raidDefault.gain[0],1.4f)&&near(raidDefault.gain[1],2));
        const auto& raidZone=instance.at("Stratholme Raid",329);
        assert(near(raidZone.gain[0],2.1f)&&near(raidZone.gain[1],2));
        const auto& raidSubzone=instance.at("Stratholme Raid",330);
        assert(near(raidSubzone.gain[0],2.1f)&&near(raidSubzone.gain[1],3));
        assert(liveInstanceZone(instance,"Stratholme Raid",329,330)==330);
        assert(liveInstanceZone(instance,"Stratholme Raid",329,0)==329);
        assert(liveInstanceZone(instance,"Stratholme Raid",1977,330)==0);
        assert(near(instance.at("Zul'gurub",1977).gain[0],2.7f));
        assert(near(instance.at("Zul'gurub",999).gain[0],3.5f));
        // Map 48 Blackfathom, zone 719 was read from the private client at the inner pool.
        assert(liveInstanceZone(instance,"Blackfathom",719,0)==719);
        assert(near(instance.at("Blackfathom",liveInstanceZone(instance,"Blackfathom",719,0)).gain[0],2.3f));
        assert(liveInstanceZone(instance,"Stratholme Raid",1977,0)==0); // stale ID from another map
        assert(liveInstanceZone(instance,"Azeroth",719,0)==0); // continent ADT lookup remains authoritative
        NorthlightRegionalFog::Region absent;
        auto noAdt=sample(instance,absent,"Blackfathom",-151.89f,106.96f);
        assert(near(noAdt.gain[0],3.5f)&&near(noAdt.gain[1],1.8f));
        assert(instance.needsLiveZone());
        Table mapOnly;std::istringstream mapFallback("[Stratholme Raid:default]\nsun_disc_gain=1.3\n");
        assert(parse(mapFallback,mapOnly,error)&&!mapOnly.needsLiveZone());
        assert(near(mapOnly.at("Stratholme Raid",1).gain[0],1.3f));
        for(const char* bad:{"[../Azeroth:12]\nsun_disc_gain=1","[Appearance]\nPalette=../raid","[Appearance]\nmoon_disc_gain=1"}){
            Table invalid;std::istringstream malformed(bad);assert(!parse(malformed,invalid,error));
        }
    }
    Transition transition;auto first=transition.update(t.fallback,"Azeroth",0,0,1);assert(first.mix[1]==0);
    float previous=0;for(int i=1;i<=600;++i){auto p=transition.update(green,"Azeroth",0,0,1+i/60.);assert(p.mix[1]>=previous&&p.mix[1]<=.8f);previous=p.mix[1];}
    assert(previous>.799f);
    auto teleport=transition.update(t.fallback,"Azeroth",1000,0,12);assert(teleport.mix[1]==0);
    auto mapChange=transition.update(green,"Kalimdor",1000,0,13);assert(near(mapChange.mix[1],.8f));
    std::cout<<"PASS profiles: "<<rejected<<" malformed inputs, horizon haze multipliers, "<<cases<<" energy allocations, real Tirisfal asset IDs, tile-boundary continuity and temporal/map/teleport transitions\n";
}
