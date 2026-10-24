#undef NDEBUG
#include "settings_transport.h"
#include <cassert>
#include <iostream>
#include <chrono>
using namespace NorthlightSettings;
template<class F> void fails(F f){bool failed=false;try{f();}catch(...){failed=true;}assert(failed);}
int main(int argc,char** argv){
    assert(argc==2);auto root=std::filesystem::path(argv[1]);std::filesystem::create_directories(root);
    std::string raw="\xef\xbb\xbf; note\r\n[Quality]\r\n  Weather = 1  ; keep\r\nUnknown=future\r\n";Document d(raw);assert(d.bytes()==raw);
    std::string zero="0";d.set("Quality","Weather",&zero);assert(d.bytes().find("  Weather = 0  ; keep")!=std::string::npos);d.set("Quality","Weather",nullptr);assert(d.bytes().find("; keep")!=std::string::npos&&d.bytes().find("Unknown=future")!=std::string::npos);
    Document u;u.encoding=Document::Utf16LE;u.lines={{"# caf\xc3\xa9","\r\n"},{"[Quality]","\r\n"},{"Weather=1","\r\n"}};auto unicode=u.bytes();Document u2(unicode);assert(u2.bytes()==unicode);u2.set("Quality","Weather",&zero);assert(u2.bytes().substr(0,2)=="\xff\xfe"&&u2.text().find("# caf")!=std::string::npos);
    Document encodedProfile("[default]\nsun_fog_gain=2.5\n");encodedProfile.encoding=Document::Utf16LE;writeImage(root/"profile-encoding.ini",encodedProfile.bytes());NorthlightCelestialProfiles::Table palette;std::string error;
    assert(NorthlightCelestialProfiles::load((root/"profile-encoding.ini").string(),palette,error)&&palette.fallback.fogGain[0]==2.5f);
    Document encodedRange("[Azeroth:12]\nterrain_radius=2048\n");encodedRange.encoding=Document::Utf16LE;writeImage(root/"range-encoding.ini",encodedRange.bytes());NorthlightRegionalShadow::Table range;
    assert(NorthlightRegionalShadow::load((root/"range-encoding.ini").string(),range,error)&&range.at("Azeroth",12)==2048);
    Store store(root);auto before=store.read(),after=before;after[0]={true,"[Quality]\nWeather=0\n"};after[1]={true,"[Renderer]\nBackend=native\n"};
    {TransactionLock lease(root);bool blocked=false;std::thread other([&]{try{store.read();}catch(...){blocked=true;}});other.join();assert(blocked);}
    fails([&]{store.commit(before,after,1);});assert(store.read()==before);assert(!std::filesystem::exists(root/"northlight-settings.journal"));
    store.commit(before,after);assert(store.read()==after);fails([&]{store.commit(before,after);});
    auto docs=documents(after);Config active=configOf(docs,"");auto fields=schema(docs,docs,active,"");unsigned q=0,c=0;for(auto& f:fields){q+=f.file==0;c+=f.file==2;}
    assert(q==std::size(NorthlightQuality::Keys)+1&&c>=22*28);fails([&]{applyChanges(docs,fields,"Q\tQuality\tWeather\t2\n");});fails([&]{applyChanges(docs,fields,"Q\tQuality\t../path\t1\n");});
    auto changed=applyChanges(docs,fields,"Q\tQuality\tWeather\t~\n");assert(explicitValue(changed[0],"Quality","Weather")=="~");assert(qualityOf(changed[0],"").weather==1);
    auto next=active.quality;next.weather=1;next.commandStream=0;auto applied=liveQualityUpdate(active.quality,next);assert(applied.weather==1&&applied.commandStream==active.quality.commandStream);
    const std::string bumpChanges="Q\tQuality\tAutoBump\t0\nQ\tQuality\tAutoBumpStrength\t130\nQ\tQuality\tAutoBumpDistance\t24\nQ\tQuality\tAutoBumpTerrain\t0\nQ\tQuality\tAutoBumpWalls\t1\nQ\tQuality\tAutoBumpGrass\t0\n";
    auto bumpDocs=applyChanges(docs,fields,bumpChanges);auto bumpConfig=configOf(bumpDocs,"");
    assert(bumpConfig.quality.autoBump==0&&bumpConfig.quality.autoBumpStrength==130&&bumpConfig.quality.autoBumpDistance==24&&bumpConfig.quality.autoBumpTerrain==0&&bumpConfig.quality.autoBumpWalls==1&&bumpConfig.quality.autoBumpGrass==0);
    unsigned bumpFields=0;for(const auto& f:fields)if(f.key.find("AutoBump")==0){assert(f.apply=="F");++bumpFields;}assert(bumpFields==7);
    fails([&]{applyChanges(docs,fields,"Q\tQuality\tAutoBumpDistance\t7\n");});fails([&]{applyChanges(docs,fields,"Q\tQuality\tAutoBumpStrength\t151\n");});
    std::filesystem::create_directories(root/"bump");Store bumpStore(root/"bump");auto bumpBefore=bumpStore.read(),bumpAfter=bumpBefore;bumpAfter[0]={true,bumpDocs[0].bytes()};bumpStore.commit(bumpBefore,bumpAfter);
    auto reloaded=configOf(documents(bumpStore.read()),"");auto liveBump=liveQualityUpdate(active.quality,reloaded.quality);
    assert(liveBump.autoBump==0&&liveBump.autoBumpStrength==130&&liveBump.autoBumpDistance==24&&liveBump.autoBumpTerrain==0&&liveBump.autoBumpWalls==1&&liveBump.autoBumpGrass==0);
    auto bumpFactory=applyChanges(bumpDocs,fields,"Q\tQuality\tAutoBump\t@factory\n");assert(configOf(bumpFactory,"").quality.autoBump==1);
    Service service(root);Transport transport;const auto request="NL1\ttest\t1\t1\t1\nsnapshot";std::string reply;
    for(unsigned i=0;i<200;++i){reply=transport.exchange(service,request);if(reply.find("\nNLS1\t")!=reply.npos)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}assert(reply.find("\nNLS1\t")!=reply.npos);
    assert(transport.exchange(service,request)==reply);assert(transport.exchange(service,"NL1\tx\t2\t1\t1\nsnapshot").empty());
    assert(!service.submit("oversize",std::string(131073,'x')));
    unsigned live=0;for(const auto& k:NorthlightQuality::Keys)live+=liveQuality(k.name);assert(live==79);
    {unsigned found=0;for(const auto& f:fields)if(f.key=="AOQuality"){
        ++found;assert(f.kind=="enum"&&f.options=="High,Medium,Low"&&f.saved=="High"&&f.active=="High"&&f.factory=="High"&&f.inherited=="High"&&f.apply=="F");}
     assert(found==1);
     for(const auto* value:{"High","Medium","Low"}){
        const auto selected=applyChanges(docs,fields,std::string("Q\tQuality\tAOQuality\t")+value+"\n");const auto config=configOf(selected,"");
        assert(std::string(NorthlightQuality::aoQualityName(config.quality.aoQuality))==value);
        assert(liveQualityUpdate(active.quality,config.quality).aoQuality==config.quality.aoQuality);
        const auto selectedFields=schema(selected,docs,active,"");for(const auto& f:selectedFields)if(f.key=="AOQuality")assert(f.saved==value&&f.active=="High"&&f.overrideValue==value);
        const auto restored=applyChanges(selected,fields,"Q\tQuality\tAOQuality\t@factory\n");assert(configOf(restored,"").quality.aoQuality==0);
     }
     fails([&]{applyChanges(docs,fields,"Q\tQuality\tAOQuality\tUltra\n");});}
    const auto displacementDocs=applyChanges(docs,fields,"Q\tQuality\tAutoBumpTerrain\t3\nQ\tQuality\tAutoBumpWalls\t3\nQ\tQuality\tAutoDisplacementStrength\t125\nQ\tQuality\tAutoDisplacementDistance\t16\nQ\tQuality\tAutoDisplacementDetail\t5\n");
    const auto displaced=liveQualityUpdate(active.quality,configOf(displacementDocs,"").quality);
    assert(displaced.autoBumpTerrain==3&&displaced.autoBumpWalls==3&&displaced.autoDisplacementStrength==125&&displaced.autoDisplacementDistance==16&&displaced.autoDisplacementDetail==5);
    auto strongRelief=applyChanges(docs,fields,"Q\tQuality\tAutoDisplacementStrength\t600\n");
    auto strongQuality=qualityOf(strongRelief[0],"");assert(strongQuality.autoDisplacementStrength==600&&strongQuality.autoModelDisplacementStrength==active.quality.autoModelDisplacementStrength);
    assert(liveQualityUpdate(active.quality,strongQuality).autoDisplacementStrength==600);
    fails([&]{applyChanges(docs,fields,"Q\tQuality\tAutoDisplacementStrength\t1001\n");});
    fails([&]{applyChanges(docs,fields,"Q\tQuality\tAutoDisplacementDetail\t6\n");});fails([&]{applyChanges(docs,fields,"Q\tQuality\tAutoDisplacementDistance\t3\n");});
    const auto occlusionDocs=applyChanges(docs,fields,"Q\tQuality\tAutoParallaxShadows\t0\nQ\tQuality\tAutoParallaxShadowStrength\t85\nQ\tQuality\tAutoParallaxShadowSteps\t12\nQ\tQuality\tAutoCavity\t1\nQ\tQuality\tAutoCavityStrength\t55\nQ\tQuality\tAutoMaterialOcclusionDistance\t6\n");
    const auto modelDocs=applyChanges(docs,fields,"Q\tQuality\tAutoBumpModels\t3\nQ\tQuality\tAutoModelDisplacementStrength\t40\nQ\tQuality\tAutoModelDisplacementDistance\t10\nQ\tQuality\tAutoModelDisplacementDetail\t3\nQ\tQuality\tAutoHeightPolarity\t2\nQ\tQuality\tAutoHeightDetails\t90\nQ\tQuality\tAutoHeightBalance\t60\n");
    const auto modelLive=liveQualityUpdate(active.quality,configOf(modelDocs,"").quality);assert(modelLive.autoBumpModels==3&&modelLive.autoModelDisplacementStrength==40&&modelLive.autoModelDisplacementDistance==10&&modelLive.autoModelDisplacementDetail==3&&modelLive.autoHeightPolarity==2&&modelLive.autoHeightDetails==90&&modelLive.autoHeightBalance==60);
    const auto occlusionConfig=configOf(occlusionDocs,"");const auto occlusionLive=liveQualityUpdate(active.quality,occlusionConfig.quality);
    assert(!occlusionLive.autoParallaxShadows&&occlusionLive.autoParallaxShadowStrength==85&&occlusionLive.autoParallaxShadowSteps==12&&occlusionLive.autoCavity&&occlusionLive.autoCavityStrength==55&&occlusionLive.autoMaterialOcclusionDistance==6);
    fails([&]{applyChanges(docs,fields,"Q\tQuality\tAutoCavityStrength\t101\n");});fails([&]{applyChanges(docs,fields,"Q\tQuality\tAutoParallaxShadowSteps\t3\n");});
    const auto materialDocs=applyChanges(docs,fields,"Q\tQuality\tAutoBumpTerrain\t2\nQ\tQuality\tAutoBumpWalls\t1\nQ\tQuality\tAutoBumpGrass\t0\nQ\tQuality\tAutoParallaxDepth\t120\nQ\tQuality\tAutoParallaxDistance\t12\nQ\tQuality\tAutoParallaxSteps\t24\n");
    const auto materialConfig=configOf(materialDocs,"");assert(materialConfig.quality.autoBumpTerrain==2&&materialConfig.quality.autoBumpWalls==1&&!materialConfig.quality.autoBumpGrass&&materialConfig.quality.autoParallaxDepth==120&&materialConfig.quality.autoParallaxDistance==12&&materialConfig.quality.autoParallaxSteps==24);
    fails([&]{applyChanges(docs,fields,"Q\tQuality\tAutoBumpGrass\t4\n");});fails([&]{applyChanges(docs,fields,"Q\tQuality\tAutoBumpTerrain\t4\n");});
    for(const auto& f:fields)assert(f.file!=0||f.key.rfind("Water",0)!=0);
    fails([&]{applyChanges(docs,fields,"Q\tQuality\tWaterSSRResolution\t25\n");});
    const auto base=Store::revision(store.read());const auto command="commit\t"+base+"\nQ\tQuality\tWeather\t1\nQ\tQuality\tCommandStream\t0\nQ\tQuality\tAOMethod\tGTAO\nQ\tQuality\tAOQuality\tMedium\nR\tRenderer\tExperimentalHDR\t1\nR\tRenderer\tHDRPaperWhite\t240\nR\tRenderer\tHDRUIWhite\t120\nR\tRenderer\tHDRMaxNits\t600\n";
    assert(service.submit("save",command));std::string saved;
    auto completed=[&](const char* id){std::string result;for(unsigned i=0;i<500;++i){if(service.result(id,result))return result;std::this_thread::sleep_for(std::chrono::milliseconds(2));}assert(false);return result;};
    saved=completed("save");assert(saved.rfind("SAVED\t",0)==0);auto update=service.after("");assert(update&&update->config.quality.weather==1&&update->config.quality.commandStream==0&&update->config.quality.aoMethod==3);
    assert(update->runtime->quality.weather==1&&update->runtime->quality.commandStream==1&&update->runtime->quality.aoMethod==3);
    assert(update->config.quality.aoQuality==1&&update->runtime->quality.aoQuality==1);
    assert(update->runtime->hdr[0]==0&&update->runtime->hdr[1]==240&&update->runtime->hdr[2]==120&&update->runtime->hdr[3]==600);service.acknowledge(*update);
    const auto committed=store.read();assert(service.submit("save",command)&&completed("save")==saved&&store.read()==committed);
    assert(service.submit("conflict","commit\t"+base+"\nQ\tQuality\tWeather\t0\n"));assert(completed("conflict").rfind("ERROR\t",0)==0&&store.read()==committed);
    assert(service.submit("refresh","snapshot"));auto final=completed("refresh");assert(final.find("NLS1\t"+update->revision+"\t"+update->revision+"\n")==0);
    for(const auto& f:fields)if(f.file==2&&f.section=="default"&&f.key=="sun_fog_gain")assert(f.inherited=="1"&&std::stof(f.factory)==4.3f);
    auto hdr=applyChanges(docs,fields,"R\tRenderer\tExperimentalHDR\t0\nR\tRenderer\tHDRPaperWhite\t203\nR\tRenderer\tHDRUIWhite\t160\n");assert(explicitValue(hdr[1],"Renderer","HDRUIWhite")=="160");
    fails([&]{applyChanges(docs,fields,"R\tRenderer\tHDRPaperWhite\t79\n");});for(const auto& f:fields)if(f.key=="ExperimentalHDR")assert(f.available&&f.apply=="R"&&f.factory=="0"&&f.active=="0");
    fails([&]{applyChanges(docs,fields,"R\tRenderer\tHDRMaxNits\t79\n");});fails([&]{applyChanges(docs,fields,"R\tRenderer\tHDRMaxNits\t10001\n");});
    for(const auto& f:fields)if(f.key=="HDRMaxNits")assert(f.available&&f.apply=="F"&&f.factory=="1000"&&f.active=="1000");
    auto graded=applyChanges(docs,fields,"R\tRenderer\tHDRContrast\t1.35\nR\tRenderer\tHDRExposure\t-1.25\n");auto grade=configOf(graded,"");assert(grade.hdr[4]==1.35f&&grade.hdr[5]==-1.25f);
    for(auto text:{"nan","inf","-inf","1oops","","4.01","-4.01"})fails([&]{applyChanges(docs,fields,std::string("R\tRenderer\tHDRExposure\t")+text+"\n");});
    fails([&]{applyChanges(docs,fields,"R\tRenderer\tHDRContrast\t0.49\n");});fails([&]{applyChanges(docs,fields,"R\tRenderer\tHDRContrast\t2.01\n");});
    auto resetGrade=applyChanges(graded,fields,"R\tRenderer\tHDRContrast\t~\nR\tRenderer\tHDRExposure\t~\n");assert(configOf(resetGrade,"").hdr[4]==1&&configOf(resetGrade,"").hdr[5]==0);
    Service gradeService(root);assert(gradeService.submit("grade","commit\t"+Store::revision(store.read())+"\nR\tRenderer\tHDRContrast\t1.35\nR\tRenderer\tHDRExposure\t-1.25\n"));
    std::string gradeReply;for(unsigned i=0;i<500&&!gradeService.result("grade",gradeReply);++i)std::this_thread::sleep_for(std::chrono::milliseconds(2));assert(gradeReply.rfind("SAVED\t",0)==0);
    auto liveGrade=gradeService.after("");assert(liveGrade&&liveGrade->runtime->hdr[4]==1.35f&&liveGrade->runtime->hdr[5]==-1.25f);
    service.setHdrStatus(true,0,"active");assert(service.submit("hdr-status","snapshot"));assert(completed("hdr-status").find("CAP\tHDR\tactive\t0\tactive\n")!=std::string::npos);
    const auto previewBefore=store.read();const auto savedUpdate=service.after("");
    const char* methods[]={"SSAO","HDAO","HBAOPlus","GTAO"};
    for(unsigned method=0;method<4;++method)for(unsigned enabled=0;enabled<2;++enabled){
        const auto id="preview-"+std::to_string(method)+std::to_string(enabled);
        assert(service.submit(id,"preview-ao\t"+std::string(methods[method])+"\t"+std::to_string(enabled)));
        assert(completed(id.c_str())=="PREVIEW\t"+std::string(methods[method])+"\n");
        assert(service.aoPreview()==int(method|(enabled<<2))&&store.read()==previewBefore&&service.after("")==savedUpdate);
    }
    unsigned invalidSerial=0;for(auto invalid:{"preview-ao\tVXAO\t1","preview-ao\tSSAO\t2","preview-ao\tHDAO\t1\nQ\tQuality\tWeather\t0","preview-ao\t../path\t0"}){
        const auto id="invalid-preview-"+std::to_string(++invalidSerial);assert(service.submit(id,invalid));
        assert(completed(id.c_str()).rfind("ERROR\t",0)==0&&service.aoPreview()==7&&store.read()==previewBefore);
    }
    assert(service.submit("preview-state","snapshot"));const auto state=completed("preview-state");assert(state.find("CAP\tAOPreview\t1\n")!=state.npos);
    assert(service.submit("preview-reset","preview-ao\tsaved"));assert(completed("preview-reset")=="PREVIEW\tsaved\n"&&service.aoPreview()==-1&&store.read()==previewBefore);
    assert(service.submit("preview-before-save","preview-ao\tHDAO\t1"));assert(completed("preview-before-save")=="PREVIEW\tHDAO\n");
    assert(service.submit("preview-conflict","commit\tstale\nQ\tQuality\tAOMethod\tSSAO\n"));assert(completed("preview-conflict").rfind("ERROR\t",0)==0&&service.aoPreview()==5);
    assert(service.submit("preview-save","commit\t"+Store::revision(store.read())+"\nQ\tQuality\tAOMethod\tSSAO\n"));assert(completed("preview-save").rfind("SAVED\t",0)==0&&service.aoPreview()==-1);
    assert(service.after("")->config.quality.aoMethod==0&&service.after("")->runtime->quality.aoMethod==0);
    // Reused snapshots must still notice exact file changes, even with unchanged timestamps.
    assert(service.submit("cache-warm","snapshot"));auto warm=completed("cache-warm");
    assert(service.submit("cache-hit","snapshot"));assert(completed("cache-hit")==warm);
    const auto ini=root/FileNames[0];const auto timestamp=std::filesystem::last_write_time(ini);
    auto image=readImage(ini);image.raw+="\n[Quality]\nAutoCavityStrength=42\n";writeImage(ini,image.raw);std::filesystem::last_write_time(ini,timestamp);
    assert(service.submit("cache-external","snapshot"));auto external=completed("cache-external");assert(external!=warm);
    assert(external.rfind("NLS1\t"+Store::revision(store.read())+"\t",0)==0);
    assert(service.submit("cache-external-repeat","snapshot"));assert(completed("cache-external-repeat")==external);
    service.setHdrStatus(false,123,"test status");assert(service.submit("cache-hdr","snapshot"));assert(completed("cache-hdr").find("CAP\tHDR\tinactive\t123\ttest status\n")!=std::string::npos);
    std::cout<<"Settings cache: identical hit, timestamp-preserving INI edit, fresh revision and uncached HDR status passed\n";
    std::cout<<"Settings: encoding, comments, inheritance, bounded schema, rollback, conflict and retry checks passed\n";
    std::cout<<"AO preview: all methods/on-off, no INI writes, strict validation, saved reset, commit/conflict handling passed\n";
}
