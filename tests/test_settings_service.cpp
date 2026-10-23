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
    Service service(root);Transport transport;const auto request="NL1\ttest\t1\t1\t1\nsnapshot";std::string reply;
    for(unsigned i=0;i<200;++i){reply=transport.exchange(service,request);if(reply.find("\nNLS1\t")!=reply.npos)break;std::this_thread::sleep_for(std::chrono::milliseconds(2));}assert(reply.find("\nNLS1\t")!=reply.npos);
    assert(transport.exchange(service,request)==reply);assert(transport.exchange(service,"NL1\tx\t2\t1\t1\nsnapshot").empty());
    assert(!service.submit("oversize",std::string(131073,'x')));
    unsigned live=0;for(const auto& k:NorthlightQuality::Keys)live+=liveQuality(k.name);assert(live>40&&live<std::size(NorthlightQuality::Keys));
    const auto base=Store::revision(store.read());const auto command="commit\t"+base+"\nQ\tQuality\tWeather\t1\nQ\tQuality\tCommandStream\t0\nR\tRenderer\tExperimentalHDR\t1\nR\tRenderer\tHDRPaperWhite\t240\nR\tRenderer\tHDRUIWhite\t120\nR\tRenderer\tHDRMaxNits\t600\n";
    assert(service.submit("save",command));std::string saved;
    auto completed=[&](const char* id){std::string result;for(unsigned i=0;i<500;++i){if(service.result(id,result))return result;std::this_thread::sleep_for(std::chrono::milliseconds(2));}assert(false);return result;};
    saved=completed("save");assert(saved.rfind("SAVED\t",0)==0);auto update=service.after("");assert(update&&update->config.quality.weather==1&&update->config.quality.commandStream==0);
    assert(update->runtime->quality.weather==1&&update->runtime->quality.commandStream==1);
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
    // Reused snapshots must still notice exact file changes, even with unchanged timestamps.
    assert(service.submit("cache-warm","snapshot"));auto warm=completed("cache-warm");
    assert(service.submit("cache-hit","snapshot"));assert(completed("cache-hit")==warm);
    const auto ini=root/FileNames[0];const auto timestamp=std::filesystem::last_write_time(ini);
    auto image=readImage(ini);image.raw+="\n[Quality]\nWeather=0\n";writeImage(ini,image.raw);std::filesystem::last_write_time(ini,timestamp);
    assert(service.submit("cache-external","snapshot"));auto external=completed("cache-external");assert(external!=warm);
    assert(external.rfind("NLS1\t"+Store::revision(store.read())+"\t",0)==0);
    assert(service.submit("cache-external-repeat","snapshot"));assert(completed("cache-external-repeat")==external);
    service.setHdrStatus(false,123,"test status");assert(service.submit("cache-hdr","snapshot"));assert(completed("cache-hdr").find("CAP\tHDR\tinactive\t123\ttest status\n")!=std::string::npos);
    std::cout<<"Settings cache: identical hit, timestamp-preserving INI edit, fresh revision and uncached HDR status passed\n";
    std::cout<<"Settings: encoding, comments, inheritance, bounded schema, rollback, conflict and retry checks passed\n";
}
