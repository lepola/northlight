#undef NDEBUG
#include "settings_schema.h"
#include <cassert>
#include <fstream>
#include <iostream>

using namespace NorthlightAppearancePresets;

static Table parseText(const std::string& text){Table table;std::string error;if(!parse(text,table,error)){std::cerr<<"invalid preset fixture: "<<error<<'\n';std::abort();}return table;}
static Table parseLocalText(const std::string& text){Table table;std::string error;std::istringstream input(text);if(!NorthlightCelestialProfiles::parse(input,table,error)){std::cerr<<"invalid profile fixture: "<<error<<'\n';std::abort();}return table;}

int main(int argc,char** argv){
    assert(argc==3);
    const auto catalog=load(argv[1]);
    assert(catalog.errors.empty());
    const auto* vibrant=catalog.find("vibrant");const auto* defaults=catalog.find("DEFAULT");
    assert(vibrant&&defaults);
    {std::ofstream unused(std::filesystem::path(argv[1])/"appearance-presets"/"Unused.ini");unused<<"not a preset";}
    const auto coldDefault=loadSelected(argv[1],"Default");assert(coldDefault.entries.empty()&&coldDefault.errors.empty());
    const auto coldVibrant=loadSelected(argv[1],"vIbRaNt");assert(coldVibrant.errors.empty()&&coldVibrant.entries.size()==2);

    const auto presetsFolder=std::filesystem::path(argv[1])/"appearance-presets";
    const std::string duskBytes="[default]\nfog_tint=0.8,0.7,0.6\n";
    {std::ofstream dusk(presetsFolder/"Dusk.ini");dusk<<duskBytes;}
    {std::ofstream dusk(presetsFolder/"dusk.ini");dusk<<duskBytes;}
    const auto ambiguousMenu=load(argv[1]);
    assert(!ambiguousMenu.find("Dusk")&&ambiguousMenu.find("Vibrant")&&ambiguousMenu.find("Default"));
    assert(std::count_if(ambiguousMenu.errors.begin(),ambiguousMenu.errors.end(),[](const auto& error){return error.find("duplicate preset name")!=std::string::npos;})==2);
    const auto ambiguousStartup=loadSelected(argv[1],"dUsK");
    assert(ambiguousStartup.entries.size()==1&&ambiguousStartup.find("Default")&&!ambiguousStartup.find("Dusk"));
    assert(ambiguousStartup.errors.size()==1&&ambiguousStartup.errors.front().find("Duplicate preset name: dUsK")!=std::string::npos);
    std::filesystem::remove(presetsFolder/"dusk.ini");
    const auto recoveredMenu=load(argv[1]);
    assert(recoveredMenu.find("Dusk")&&recoveredMenu.find("Dusk")->name=="Dusk");
    assert(std::count_if(recoveredMenu.errors.begin(),recoveredMenu.errors.end(),[](const auto& error){return error.find("duplicate preset name")!=std::string::npos;})==0);
    const auto recoveredStartup=loadSelected(argv[1],"dUsK");
    assert(recoveredStartup.errors.empty()&&recoveredStartup.entries.size()==2&&recoveredStartup.find("Default")&&recoveredStartup.find("dUsK"));

    std::string nameError;assert(validName("Night Market",nameError));assert(!validName("../bad",nameError));
    assert(nameError==NameRuleText&&nameError.find('\xE2')==std::string::npos);

    Table local;std::string error;
    assert(NorthlightCelestialProfiles::load(argv[2],local,error));
    const auto stock=local.at("Expansion01",3430).fogTint[1];
    assert(std::fabs(stock[1]-.84f)<1e-5f);
    const auto defaultView=compose(local,defaults->table,defaults->table).at("Expansion01",3430).fogTint[1];
    assert(std::fabs(defaultView[0]-stock[0])<1e-5f&&std::fabs(defaultView[1]-stock[1])<1e-5f&&std::fabs(defaultView[2]-stock[2])<1e-5f);
    auto composed=compose(local,vibrant->table,defaults->table);
    const auto eversong=composed.at("Expansion01",3430).fogTint[1];
    assert(std::fabs(eversong[0]-.975f)<1e-5f&&std::fabs(eversong[1]-.675f)<1e-5f&&std::fabs(eversong[2]-.625f)<1e-5f);

    // The user's explicit value differs from the shipped baseline, so both Vibrant and Default retain it.
    Table personal=parseText("[default]\nfog_tint=1,1,1\n[Expansion01:3430]\nfog_tint=0.12,0.23,0.34\n");
    personal.palette="Vibrant";
    auto personalVibrant=compose(personal,vibrant->table,defaults->table);
    auto personalDefault=compose(personal,defaults->table,defaults->table);
    for(const auto* table:{&personalVibrant,&personalDefault}){
        const auto tint=table->at("Expansion01",3430).fogTint[1];
        assert(std::fabs(tint[0]-.12f)<1e-5f&&std::fabs(tint[1]-.23f)<1e-5f&&std::fabs(tint[2]-.34f)<1e-5f);
    }

    // A child declared only by the palette still inherits its personalized parent.
    Table localParent=parseText("[default]\nday_vibrance=0\n[Expansion01:100]\nday_vibrance=0.91\n");
    Table paletteChild=parseText("[default]\n[Expansion01:default]\n[Expansion01:100/101]\nshadow_contrast=0.3\n");
    Table baseline=parseText("[default]\nday_vibrance=0\n");
    const auto inherited=compose(localParent,paletteChild,baseline).at("Expansion01",101).grade[0].vibrance;
    assert(std::fabs(inherited-.91f)<1e-5f);

    // Palette-only child declarations keep the local parent's native sun/moon profile.
    Table localNative=parseLocalText("[default]\nfog_tint_strength=0\nsun_disc_gain=2.1\n[Expansion01:100]\nsun_disc_gain=2.7\n");
    Table paletteNative=parseText("[default]\nfog_tint_strength=0\n[Expansion01:default]\ntone_curve_strength=0.4\n[Expansion01:100/101]\nshadow_contrast=0.3\n");
    auto nativeComposed=compose(localNative,paletteNative,baseline);
    assert(std::fabs(nativeComposed.at("Expansion01",100).gain[0]-2.7f)<1e-5f);
    assert(std::fabs(nativeComposed.at("Expansion01",101).gain[0]-2.7f)<1e-5f);
    assert(nativeComposed.at("Expansion01",101).shadowContrast==.3f);

    // Dusk and selective colour share the same sparse, region-inheriting appearance keys.
    Table selective=parseText("[default]\nday_red_hue_shift=-12\nday_red_saturation=-.2\nnight_blue_lightness=.15\n"
                              "[Expansion01:3430]\ndusk_orange_hue_shift=17\ndusk_orange_saturation=.55\ndusk_vibrance=.61\n");
    assert(selective.explicitFallbackKeys.count("day_red_hue_shift"));
    assert(selective.explicitKeys.at({"Expansion01",3430}).count("dusk_orange_hue_shift"));
    assert(std::fabs(selective.at("Expansion01",3430).grade[1].hueShift[1]-17)<1e-5f);
    assert(std::fabs(selective.at("Expansion01",3430).grade[1].saturation[1]-.55f)<1e-5f);
    const auto selectiveCopy=parseText(serialize(selective));
    for(const auto& key:keys())assert(sameValue(selective.at("Expansion01",3430),selectiveCopy.at("Expansion01",3430),key));
    Table selectivePalette=parseText("[default]\ndusk_blue_saturation=.8\n[Expansion01:3430]\ndusk_red_lightness=.12\n");
    const auto selectedGrade=compose(parseLocalText("[default]\n"),selectivePalette,parseText("[default]\nfog_tint=1,1,1\n"));
    assert(std::fabs(selectedGrade.at("Expansion01",3430).grade[1].saturation[unsigned(NorthlightTone::HueBand::Blue)]-.8f)<1e-5f);
    assert(std::fabs(selectedGrade.at("Expansion01",3430).grade[1].lightness[0]-.12f)<1e-5f);
    Table legacyGrade=parseText("[default]\nday_vibrance=.2\nnight_vibrance=.8\nday_red_hue_shift=-12\nnight_red_hue_shift=10\n");
    const auto& legacyTimeline=legacyGrade.fallback.grade;
    assert(!legacyGrade.explicitFallbackKeys.count("dusk_vibrance"));
    assert(std::fabs(legacyTimeline[1].vibrance-(.2f+(.8f-.2f)*NorthlightTone::DuskNightWeight))<1e-5f);
    auto sameGrade=[](const NorthlightTone::Grade& a,const NorthlightTone::Grade& b){
        for(unsigned c=0;c<4;++c){
            if(std::fabs(a.curves[c].lowInput-b.curves[c].lowInput)>1e-5f||std::fabs(a.curves[c].lowBend-b.curves[c].lowBend)>1e-5f)return false;
            for(unsigned i=0;i<3;++i)if(std::fabs(a.curves[c].levels[i]-b.curves[c].levels[i])>1e-5f)return false;
        }
        if(std::fabs(a.vibrance-b.vibrance)>1e-5f)return false;
        for(unsigned band=0;band<NorthlightTone::HueBandCount;++band)
            if(std::fabs(a.hueShift[band]-b.hueShift[band])>1e-5f||std::fabs(a.saturation[band]-b.saturation[band])>1e-5f||std::fabs(a.lightness[band]-b.lightness[band])>1e-5f)return false;
        return true;
    };
    for(float n:{0.f,.2f,.5f,NorthlightTone::DuskNightWeight,.9f,1.f})
        assert(sameGrade(NorthlightTone::sample(legacyTimeline,n,false),NorthlightTone::sample(legacyTimeline,n,true)));
    Table inheritedDusk=parseText("[default]\nday_vibrance=.2\nnight_vibrance=.8\n[Expansion01:100]\ndusk_vibrance=.75\n[Expansion01:100/101]\nday_vibrance=.4\n");
    assert(std::fabs(inheritedDusk.at("Expansion01",101).grade[1].vibrance-.75f)<1e-5f);
    Table derivedDusk=parseText("[default]\nday_vibrance=.2\nnight_vibrance=.8\n[Expansion01:100/101]\nday_vibrance=.4\n");
    assert(!derivedDusk.explicitKeys.at({"Expansion01",101}).count("dusk_vibrance"));
    assert(std::fabs(derivedDusk.at("Expansion01",101).grade[1].vibrance-(.4f+(.8f-.4f)*NorthlightTone::DuskNightWeight))<1e-5f);

    // An explicit Off remains personal even when Default has no key at that scope; Inherit restores the palette value.
    Table vibrantOff=parseText("[default]\nfog_tint=1,1,1\nfog_tint_strength=0.25\n[Azeroth:default]\nfog_tint_strength=0.5\n[Azeroth:85]\nfog_tint_strength=0.75\n");
    Table neutralBaseline=parseText("[default]\nfog_tint=1,1,1\n");
    Table offValues=parseLocalText("[default]\nfog_tint=1,1,1\nfog_tint_strength=0\n[Azeroth:default]\nfog_tint_strength=0\n[Azeroth:85]\nfog_tint_strength=0\n");
    const auto off=compose(offValues,vibrantOff,neutralBaseline);
    assert(off.fallback.fogTintStrength[0]==0&&off.at("Azeroth",0).fogTintStrength[0]==0&&off.at("Azeroth",85).fogTintStrength[0]==0);
    Table inheritedValues=parseLocalText("[default]\nfog_tint=1,1,1\n");
    const auto inheritedOff=compose(inheritedValues,vibrantOff,neutralBaseline);
    assert(inheritedOff.fallback.fogTintStrength[0]==.25f&&inheritedOff.at("Azeroth",0).fogTintStrength[0]==.5f&&inheritedOff.at("Azeroth",85).fogTintStrength[0]==.75f);
    NorthlightAppearancePresets::Catalog offCatalog;offCatalog.entries={{"Default",neutralBaseline},{"Vibrant",vibrantOff}};
    auto offDocs=NorthlightSettings::factoryDocuments();
    offDocs[2]=NorthlightSettings::Document("[Appearance]\nPalette=Vibrant\n[default]\nfog_tint=1,1,1\nfog_tint_strength=0\n[Azeroth:default]\nfog_tint_strength=0\n[Azeroth:85]\nfog_tint_strength=0\n");
    const auto offConfig=NorthlightSettings::configOf(offDocs,"",true,&offCatalog);
    const auto offFields=NorthlightSettings::schema(offDocs,offDocs,offConfig,"","native",false,&offCatalog);
    auto offField=[&](const char* section){return std::find_if(offFields.begin(),offFields.end(),[&](const auto& f){return f.file==2&&f.section==section&&f.key=="dusk_fog_tint_strength";});};
    const auto globalOff=offField("default"),mapOff=offField("Azeroth:default"),zoneOff=offField("Azeroth:85");
    assert(globalOff!=offFields.end()&&mapOff!=offFields.end()&&zoneOff!=offFields.end());
    assert(globalOff->overrideValue=="0"&&mapOff->overrideValue=="0"&&zoneOff->overrideValue=="0");
    const auto inheritedDocs=NorthlightSettings::applyChanges(offDocs,offFields,globalOff->id()+"\t~\n"+mapOff->id()+"\t~\n"+zoneOff->id()+"\t~\n");
    const auto inheritedConfig=NorthlightSettings::configOf(inheritedDocs,"",true,&offCatalog);
    assert(inheritedConfig.celestial.fallback.fogTintStrength[1]==.25f&&inheritedConfig.celestial.at("Azeroth",0).fogTintStrength[1]==.5f&&inheritedConfig.celestial.at("Azeroth",85).fogTintStrength[1]==.75f);
    // The file set one value for all hours; letting Dusk follow the look leaves Daylight and Night switched off.
    for(unsigned time:{0u,2u})assert(inheritedConfig.celestial.fallback.fogTintStrength[time]==0&&inheritedConfig.celestial.at("Azeroth",85).fogTintStrength[time]==0);
    assert(inheritedDocs[2].text().find("\nfog_tint_strength")==std::string::npos&&inheritedDocs[2].text().find("day_fog_tint_strength=0")!=std::string::npos);

    // Serialization compares a child with its parent, so a child equal to its map base is still explicit.
    Table sparse=parseText("[default]\nfog_tint_strength=0\n[Expansion01:default]\nfog_tint_strength=1\n[Expansion01:100]\nfog_tint_strength=0.5\n[Expansion01:100/101]\nfog_tint_strength=1\n");
    const auto bytes=serialize(sparse);Table roundtrip;
    assert(parse(bytes,roundtrip,error));
    assert(std::fabs(roundtrip.at("Expansion01",101).fogTintStrength[0]-1.f)<1e-5f);
    assert(bytes.find("[Expansion01:100/101]\nday_fog_tint_strength=1")!=std::string::npos);
    assert(std::fabs(roundtrip.at("Expansion01",101).fogTintStrength[2]-1.f)<1e-5f&&bytes.find("\nfog_tint")==std::string::npos);

    // Fog per time of day: saved, read back, and taken from a look one time at a time.
    Table timed=parseText("[default]\nfog_tint_strength=0\n[Azeroth:default]\n[Azeroth:10]\nday_fog_brightness=0.7\nnight_fog_tint=0.5,0.6,1\nnight_fog_tint_strength=1.2\n");
    Table timedBack;assert(parse(serialize(timed),timedBack,error));
    const auto& duskwood=timedBack.at("Azeroth",10);
    assert(std::fabs(duskwood.fogBrightness[0]-.7f)<1e-5f&&duskwood.fogBrightness[1]==1&&duskwood.fogBrightness[2]==1);
    assert(std::fabs(duskwood.fogTintStrength[2]-1.2f)<1e-5f&&duskwood.fogTintStrength[0]==0&&std::fabs(duskwood.fogTint[2][0]-.5f)<1e-5f&&duskwood.fogTint[0][0]==1);
    // The player's own Daylight brightness stays; the look still supplies the Night tint.
    Table ownDay=parseLocalText("[default]\n[Azeroth:10]\nday_fog_brightness=0.4\n");
    const auto ownOverLook=compose(ownDay,timed,parseText("[default]\nfog_tint=1,1,1\n")).at("Azeroth",10);
    assert(std::fabs(ownOverLook.fogBrightness[0]-.4f)<1e-5f&&std::fabs(ownOverLook.fogTintStrength[2]-1.2f)<1e-5f);
    // Changing one time of an all-hours value in the player's file keeps the other two times.
    auto oldDocs=NorthlightSettings::factoryDocuments();
    oldDocs[2]=NorthlightSettings::Document("[default]\nfog_tint=1,1,1\n[Azeroth:10]\nfog_tint=0.2,0.4,0.6\nfog_tint_strength=0.8\n");
    const auto oldFields=NorthlightSettings::schema(oldDocs,oldDocs,NorthlightSettings::configOf(oldDocs,"",true),"","native",false);
    auto oldField=[&](const char* key){return std::find_if(oldFields.begin(),oldFields.end(),[&](const auto& f){return f.file==2&&f.section=="Azeroth:10"&&f.key==key;});};
    assert(oldField("night_fog_tint_strength")->overrideValue=="0.8"&&oldField("day_fog_tint")->overrideValue=="0.2,0.4,0.6"&&oldField("day_fog_brightness")->overrideValue=="~");
    const auto editedDocs=NorthlightSettings::applyChanges(oldDocs,oldFields,oldField("night_fog_tint_strength")->id()+"\t0.1\n");
    const auto editedFog=NorthlightSettings::configOf(editedDocs,"",true).celestial.at("Azeroth",10);
    assert(std::fabs(editedFog.fogTintStrength[0]-.8f)<1e-5f&&std::fabs(editedFog.fogTintStrength[1]-.8f)<1e-5f&&std::fabs(editedFog.fogTintStrength[2]-.1f)<1e-5f);
    assert(std::fabs(editedFog.fogTint[2][2]-.6f)<1e-5f&&editedDocs[2].text().find("\nfog_tint=0.2,0.4,0.6")!=std::string::npos);

    Table declarations=parseText("[default]\nfog_tint_strength=0\n[Azeroth:default]\nfog_tint_strength=0.2\n[Azeroth:100]\n[Azeroth:100/101]\n[Azeroth:1497@inside]\n");
    const auto declarationsBytes=serialize(declarations);Table declarationsRoundtrip;
    assert(declarationsBytes.find("[Azeroth:100/101]\n")!=std::string::npos&&declarationsBytes.find("[Azeroth:1497@inside]\n")!=std::string::npos);
    assert(parse(declarationsBytes,declarationsRoundtrip,error));
    assert(declarationsRoundtrip.parents.count({"Azeroth",101})&&declarationsRoundtrip.interiors.count({"Azeroth",1497}));

    const auto saved=serialize(composed);
    const auto rows=std::count(saved.begin(),saved.end(),'\n');
    assert(rows<=NorthlightCelestialProfiles::MaxProfileRows&&saved.size()<NorthlightCelestialProfiles::MaxProfileBytes);
    Table savedTable;assert(parse(saved,savedTable,error));
    for(const auto& id:composed.zones)for(const auto& key:keys())
        assert(sameValue(composed.at(id.first.first,id.first.second),savedTable.at(id.first.first,id.first.second),key));

    // Save As is a sparse portable preset: a new stock install must recompose
    // the same appearance without exporting unrelated sun/moon controls.
    Table freshStock;assert(NorthlightCelestialProfiles::load(argv[2],freshStock,error));
    const auto reloaded=compose(freshStock,savedTable,defaults->table);
    auto compareAppearance=[&](const Profile& expected,const Profile& actual){
        for(const auto& key:keys())assert(sameValue(expected,actual,key));
    };
    const std::vector<std::string> nativeKeys={
        "sun_disc","sun_disc_gain","sun_light","sun_light_mix","sun_light_strength","sun_fog_gain",
        "sun_fog_height_scale","sun_horizon_haze","sun_glow_strength","sun_glow","sun_glow_mix",
        "moon_disc","moon_disc_gain","moon_light","moon_light_mix","moon_light_strength","moon_fog_gain",
        "moon_fog_height_scale","moon_horizon_haze","moon_glow_strength","moon_glow","moon_glow_mix"
    };
    auto compareNative=[&](const Profile& expected,const Profile& actual){
        for(const auto& key:nativeKeys)assert(sameValue(expected,actual,key));
    };
    compareAppearance(composed.fallback,reloaded.fallback);
    compareNative(composed.fallback,reloaded.fallback);
    std::set<std::string> configuredMaps;
    for(const auto& row:composed.mapFallbacks)configuredMaps.insert(row.first);
    for(const auto& row:reloaded.mapFallbacks)configuredMaps.insert(row.first);
    assert(configuredMaps.size()>=85);
    for(const auto& map:configuredMaps){
        compareAppearance(composed.fallbackFor(map),reloaded.fallbackFor(map));
        compareNative(composed.fallbackFor(map),reloaded.fallbackFor(map));
    }
    std::set<std::pair<std::string,uint32_t>> configuredZones;
    for(const auto& row:composed.zones)configuredZones.insert(row.first);
    for(const auto& row:reloaded.zones)configuredZones.insert(row.first);
    assert(configuredZones.size()>=60);
    for(const auto& id:configuredZones){
        compareAppearance(composed.at(id.first,id.second),reloaded.at(id.first,id.second));
        compareNative(composed.at(id.first,id.second),reloaded.at(id.first,id.second));
    }
    assert(savedTable.parents==composed.parents);
    assert(savedTable.interiors==composed.interiors);
    assert(reloaded.parents==composed.parents);
    assert(reloaded.interiors==composed.interiors);

    // A heavily edited world file exceeds the old 8,192-row and 512-KiB limits.
    Table large;Profile fallback=large.fallback;fallback.toneStrength=.13f;large.fallback=fallback;
    for(unsigned zone=1000;zone<1200;++zone){
        Profile area=large.fallback;unsigned fieldIndex=0;
        for(const auto& key:keys()){
            unsigned count=0;float low=0,high=0;auto* target=NorthlightCelestialProfiles::keyTarget(area,key,count,low,high);assert(target);
            const float ratio=.2f+.006f*float((zone+fieldIndex)%80);
            for(unsigned i=0;i<count;++i)target[i]=low+(high-low)*ratio;
            ++fieldIndex;
        }
        large.zones.emplace(std::make_pair(std::string("Azeroth"),zone),area);
    }
    const auto largeBytes=serialize(large);const auto largeRows=std::count(largeBytes.begin(),largeBytes.end(),'\n');
    assert(largeRows>8192&&largeRows<NorthlightCelestialProfiles::MaxProfileRows);
    assert(largeBytes.size()>512*1024&&largeBytes.size()<NorthlightSettings::MaxIniBytes);
    std::cout<<"Large regional grade export: "<<largeRows<<" rows, "<<largeBytes.size()<<" bytes\n";
    NorthlightSettings::Document largeDocument(largeBytes);assert(largeDocument.text()==largeBytes);
    Table largeRoundtrip;assert(parse(largeBytes,largeRoundtrip,error));
    const auto largePath=std::filesystem::path(argv[1])/"large-regional-grade.ini";
    NorthlightSettings::writeImage(largePath,largeBytes);
    NorthlightSettings::FileImage largeImage=NorthlightSettings::readImage(largePath);assert(largeImage.raw==largeBytes);
    Table largeLoaded;assert(NorthlightCelestialProfiles::load(largePath.string(),largeLoaded,error));
    auto closeValue=[](const Profile& a,const Profile& b,const std::string& key){Profile x=a,y=b;unsigned count=0;float lo=0,hi=0;auto* left=NorthlightCelestialProfiles::keyTarget(x,key,count,lo,hi);auto* right=NorthlightCelestialProfiles::keyTarget(y,key,count,lo,hi);for(unsigned i=0;i<count;++i)if(std::fabs(left[i]-right[i])>1e-5f)return false;return true;};
    for(unsigned zone:{1000u,1100u,1199u})for(const auto& key:keys())if(!closeValue(large.at("Azeroth",zone),largeLoaded.at("Azeroth",zone),key)){
        std::cerr<<"large preset mismatch "<<zone<<" "<<key<<" expected="<<value(large.at("Azeroth",zone),key)<<" got="<<value(largeLoaded.at("Azeroth",zone),key)<<'\n';std::abort();
    }

    auto docs=NorthlightSettings::factoryDocuments();
    const std::string selected="Vibrant";docs[2].set("Appearance","Palette",&selected);
    const auto active=NorthlightSettings::configOf(docs,"",true,&catalog);
    const auto fields=NorthlightSettings::schema(docs,docs,active,"","native",true,&catalog);
    const auto palette=std::find_if(fields.begin(),fields.end(),[](const auto& f){return f.file==2&&f.section=="Appearance"&&f.key=="Palette";});
    assert(palette!=fields.end());
    std::istringstream options(palette->options);std::string option;unsigned defaultCount=0;
    while(std::getline(options,option,','))defaultCount+=NorthlightSettings::lower(option)=="default";
    assert(defaultCount==1);
    assert(std::any_of(fields.begin(),fields.end(),[](const auto& f){return f.file==2&&f.section=="Kalimdor:default"&&f.key=="tone_curve_strength";}));

    std::puts("appearance presets: shipped baseline layering, custom edits, regional inheritance, sparse round-trip and menu schema passed");
}
