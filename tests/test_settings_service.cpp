#undef NDEBUG
#include "settings_service.h"
#include <sys/stat.h>
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
    {std::string astral="[Quality]\n# \xf0\x9f\x98\x80 caf\xc3\xa9\n";Document a(astral);a.encoding=Document::Utf16LE;auto bytes=a.bytes();assert(bytes.rfind("\xff\xfe",0)==0&&bytes.find(std::string("\x3d\xd8\x00\xde",4))!=bytes.npos);Document back(bytes);assert(back.text()==astral&&back.bytes()==bytes);
     fails([&]{Document bad(std::string("\xff\xfe\x00\xd8[\0",6));}); // lone high surrogate
     fails([&]{Document bad(std::string("\xff\xfe\x00\xdc",4));});      // lone low surrogate
     for(auto invalid:{"\xc0\x80","\xed\xa0\x80","\xf5\x80\x80\x80","\xe2\x82","\xff"}){Document e;e.encoding=Document::Utf16LE;e.lines={{invalid,""}};fails([&]{e.bytes();});}}
    Document encodedProfile("[default]\nsun_fog_gain=2.5\n");encodedProfile.encoding=Document::Utf16LE;writeImage(root/"profile-encoding.ini",encodedProfile.bytes());NorthlightCelestialProfiles::Table palette;std::string error;
    assert(NorthlightCelestialProfiles::load((root/"profile-encoding.ini").string(),palette,error)&&palette.fallback.fogGain[0]==2.5f);
    Document encodedRange("[Azeroth:12]\nterrain_radius=2048\n");encodedRange.encoding=Document::Utf16LE;writeImage(root/"range-encoding.ini",encodedRange.bytes());NorthlightRegionalShadow::Table range;
    assert(NorthlightRegionalShadow::load((root/"range-encoding.ini").string(),range,error)&&range.at("Azeroth",12)==2048);
    Store store(root,50);auto before=store.read(),after=before;after[0]={true,"[Quality]\nWeather=0\n"};after[1]={true,"[Renderer]\nBackend=native\n"};
    {TransactionLock lease(root);bool blocked=false;std::thread other([&]{try{store.read();}catch(...){blocked=true;}});other.join();assert(blocked);}
    fails([&]{store.commit(before,after,1);});assert(store.read()==before);assert(!std::filesystem::exists(root/"northlight-settings.journal"));
    store.commit(before,after);assert(store.read()==after);fails([&]{store.commit(before,after);});
    auto docs=documents(after);Config active=configOf(docs,"");auto fields=schema(docs,docs,active,"");unsigned q=0,c=0;for(auto& f:fields){q+=f.file==0;c+=f.file==2;}
    assert(q==std::size(NorthlightQuality::Keys)+1&&c>=22*28);fails([&]{applyChanges(docs,fields,"Q\tQuality\tWeather\t2\n");});fails([&]{applyChanges(docs,fields,"Q\tQuality\t../path\t1\n");});
    auto changed=applyChanges(docs,fields,"Q\tQuality\tWeather\t~\n");assert(explicitValue(changed[0],"Quality","Weather")=="~");assert(qualityOf(changed[0],"").weather==1);
    auto next=active.quality;next.weather=1;next.commandStream=0;auto applied=liveQualityUpdate(active.quality,next);assert(applied.weather==1&&applied.commandStream==active.quality.commandStream);
    // Preserve the local material and AO schema through the new in-process service.
    for(const auto* key:{"AutoBumpModels","AutoDisplacementStrength","AutoDisplacementDistance","AutoDisplacementDetail","AutoModelDisplacementStrength","AutoModelDisplacementDistance","AutoModelDisplacementDetail","AutoHeightBalance","AutoMaterialOcclusionDistance","AOQuality"}){
        unsigned found=0;for(const auto& f:fields)if(f.file==0&&f.key==key){++found;assert(f.available&&f.apply=="F");auto changed=applyChanges(docs,fields,f.id()+"\t"+f.factory+"\n");assert(configOf(changed,"").quality.preset==active.quality.preset);}assert(found==1);
    }
    for(const auto* value:{"High","Medium","Low"}){auto selected=applyChanges(docs,fields,std::string("Q\tQuality\tAOQuality\t")+value+"\n");auto config=configOf(selected,"");assert(std::string(NorthlightQuality::aoQualityName(liveQualityUpdate(active.quality,config.quality).aoQuality))==value);}
    unsigned live=0;for(const auto& k:NorthlightQuality::Keys)live+=liveQuality(k.name);assert(live>40&&live<std::size(NorthlightQuality::Keys));
    for(const auto& f:fields)if(f.file==2&&f.section=="default"&&f.key=="sun_fog_gain")assert(f.inherited=="1"&&std::stof(f.factory)==4.3f);
    // HDR mode and grading both participate in live preview and Revert.
    {unsigned hdrFields=0;for(const auto& f:fields)if(f.file==1&&f.section=="Renderer"&&f.key!="Backend"){++hdrFields;assert(f.available&&f.apply=="F");auto edited=applyChanges(docs,fields,f.id()+"\t"+f.factory+"\n");assert(configOf(edited,"").hdr.size()==NorthlightHdrSettingsContract::Count);}assert(hdrFields==NorthlightHdrSettingsContract::Count);
     auto unavailable=fields;for(auto& f:unavailable)if(f.key=="HDRMaxNits")f.available=false;fails([&]{applyChanges(docs,unavailable,"R\tRenderer\tHDRMaxNits\t600\n");});
     Document hdrDoc("[Renderer]\nHDRContrast=1.35\nHDRExposure=-1.25\nHDRPaperWhite=240\n");auto g=configOf({Document(""),hdrDoc,Document(""),Document("")},"");assert(g.hdr[4]==1.35f&&g.hdr[5]==-1.25f&&g.hdr[1]==240);}
    // Lock waits: a holder that lets go within the wait is tolerated, one that does not is not.
    {std::thread holder([&]{TransactionLock lease(root);std::this_thread::sleep_for(std::chrono::milliseconds(150));});std::this_thread::sleep_for(std::chrono::milliseconds(30));
     TransactionLock waited(root,2000);holder.join();}
    // Read-only INI (POSIX chmod): the commit names the file, leaves no transaction files and does not poison later saves.
    {auto ro=root/FileNames[0];auto now=store.read();auto edit=now;edit[0]={true,"[Quality]\nWeather=1\n"};
     chmod(ro.c_str(),0444);bool writable=access(ro.c_str(),W_OK)==0; // running as root ignores the mode
     if(!writable){try{store.commit(now,edit);assert(false);}catch(const std::runtime_error& e){assert(std::string(e.what()).find("northlight-quality.ini is read-only")!=std::string::npos);}
      assert(store.read()==now);for(auto s:{".northlight.tmp",".northlight.bak"})assert(!std::filesystem::exists(root/(std::string(FileNames[0])+s)));assert(!std::filesystem::exists(root/"northlight-settings.journal"));}
     chmod(ro.c_str(),0644);store.commit(now,edit);store.commit(edit,now);assert(store.read()==now);}
    // Journal recovery. Build a "prepared" journal by hand, as a crash would leave it.
    {auto ini=root/FileNames[0];auto base=store.read();const auto oldRaw=base[0].raw;const std::string newRaw="[Quality]\nWeather=1\n";
     auto journal=[&](const std::string& state){writeImage(root/"northlight-settings.journal",state+"\n0 1 "+hash(oldRaw)+" "+hash(newRaw)+"\n");};
     auto leftovers=[&]{writeImage(root/(std::string(FileNames[0])+".northlight.tmp"),newRaw);writeImage(root/(std::string(FileNames[0])+".northlight.bak"),oldRaw);};
     auto clean=[&]{for(auto s:{".northlight.tmp",".northlight.bak"})assert(!std::filesystem::exists(root/(std::string(FileNames[0])+s)));assert(!std::filesystem::exists(root/"northlight-settings.journal"));};
     // 1. crash before any replace: the file is still at its old hash, so nothing is restored and everything is cleaned (this used to throw forever).
     journal("prepared");leftovers();assert(Store::recoverAtStartup(root).empty());clean();assert(store.read()==base);
     // ...also when the old file is read-only
     journal("prepared");leftovers();chmod(ini.c_str(),0444);assert(Store::recoverAtStartup(root).empty());chmod(ini.c_str(),0644);clean();assert(store.read()==base);
     // 2. crash after the replace: rolled back from the backup, on the next locked read.
     journal("prepared");leftovers();writeImage(ini,newRaw);assert(store.read()==base);clean();
     // 3. an external edit is never overwritten; the error names the file and keeps the evidence, and the store reports it instead of throwing at construction.
     journal("prepared");leftovers();writeImage(ini,"[Quality]\nWeather=0\n; mine\n");auto mine=readImage(ini);
     auto error=Store::recoverAtStartup(root);assert(error.find("external edit of northlight-quality.ini")!=std::string::npos);Store lazy(root,50);bool threw=false;try{lazy.read();}catch(const std::runtime_error&){threw=true;}assert(threw&&readImage(ini)==mine);
     assert(std::filesystem::exists(root/(std::string(FileNames[0])+".northlight.bak")));
     writeImage(ini,oldRaw);assert(lazy.read()==base);clean(); // once the user restores the file, the same store recovers by itself
     // 4. a committed journal is just cleaned; stale files without a journal do not block a later commit.
     journal("committed");leftovers();assert(Store::recoverAtStartup(root).empty());clean();
     leftovers();auto edit=base;edit[0]={true,newRaw};store.commit(base,edit);store.commit(edit,base);clean();assert(store.read()==base);}
    // Service: snapshot, preview coalescing, clearPreview, commit, recovery of status.
    {Service service(root);auto waitFor=[&](auto pred){for(unsigned i=0;i<1500;++i){if(pred())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return false;};
     auto idle=[&]{return service.status().state!=Status::State::Busy;};
     assert(!service.snapshot());assert(waitFor([&]{return service.requestSnapshot();}));assert(waitFor([&]{return service.snapshot()&&idle();}));
     auto snap=service.snapshot();const auto diskBefore=store.read();assert(snap->revision==Store::revision(diskBefore)&&snap->activeRevision==snap->revision);
     unsigned q=0,r=0;for(const auto& f:snap->fields){q+=f.file==0;r+=f.apply=="R";}assert(q==std::size(NorthlightQuality::Keys)+1&&r>0);
     assert(!service.status().restartRequired);
     auto find=[&](const std::shared_ptr<const Snapshot>& s,const char* key){for(const auto& f:s->fields)if(f.key==key)return f;assert(false);return Field{};};
     assert(find(snap,"Weather").apply=="F"&&find(snap,"CommandStream").apply=="R"&&find(snap,"EquipmentLights").apply=="F"&&find(snap,"AOMethod").apply=="F");
     // preview: no file writes; "R" and unknown ids are ignored; the latest draft wins
     const auto serial0=service.status().serial;
     std::vector<Change> d1={{"Q\tQuality\tWeather","1"},{"Q\tQuality\tCommandStream","0"},{"Q\tQuality\tNoSuchKey","1"}},d2={{"Q\tQuality\tWeather","0"}};
     assert(service.preview(d1));assert(service.preview(d2));
     assert(waitFor([&]{auto u=service.after("");return idle()&&service.status().serial>serial0&&u&&u->preview;}));
     auto u=service.after("");assert(u->preview&&u->config.quality.weather==0&&u->runtime->quality.commandStream==qualityOf(documents(diskBefore)[0],"").commandStream);
     assert(store.read()==diskBefore);assert(u->revision.rfind(snap->revision+"+preview",0)==0);
     assert(service.preview({{"Q\tQuality\tWeather","1"},{"Q\tQuality\tCommandStream","0"}}));
     assert(waitFor([&]{auto v=service.after(u->revision);return idle()&&v&&v->preview&&v->config.quality.weather==1;}));
     auto v=service.after(u->revision);assert(v->config.quality.commandStream==qualityOf(documents(diskBefore)[0],"").commandStream); // "R" key not previewed
     service.acknowledge(*v);assert(!service.after(v->revision));assert(service.revision()==snap->revision);
     // an invalid draft is an error, not a crash, and does not replace the active preview
     const auto serial1=service.status().serial;assert(service.preview({{"Q\tQuality\tWeather","9"}}));
     assert(waitFor([&]{return idle()&&service.status().serial>serial1;}));assert(service.status().state==Status::State::Error&&!service.status().message.empty()&&!service.after(v->revision));
     // clearPreview returns the renderer to the saved configuration under the real revision
     assert(service.clearPreview());assert(waitFor([&]{auto c=service.after(v->revision);return idle()&&c&&!c->preview;}));
     auto cleared=service.after(v->revision);assert(cleared->revision==snap->revision&&cleared->config.quality.weather==qualityOf(documents(diskBefore)[0],"").weather);service.acknowledge(*cleared);
     const auto serial2=service.status().serial;assert(service.clearPreview());assert(waitFor([&]{return service.status().serial>serial2;}));assert(!service.after(cleared->revision)); // nothing previewed: no update
     // commit: transactional, then the live part reaches the renderer
     const int savedWeather=qualityOf(documents(diskBefore)[0],"").weather;const std::string flipped=savedWeather?"0":"1";
     assert(service.commit(snap->revision,{{"Q\tQuality\tWeather",flipped},{"Q\tQuality\tCommandStream","0"}}));
     assert(waitFor([&]{return idle()&&service.status().state==Status::State::Saved;}));
     auto saved=service.after(cleared->revision);auto disk=store.read();assert(saved&&!saved->preview&&saved->revision==Store::revision(disk)&&saved->revision!=snap->revision);
     assert(saved->config.quality.weather==(flipped=="1")&&saved->config.quality.commandStream==0&&saved->runtime->quality.weather==(flipped=="1"));
     assert(saved->runtime->quality.commandStream==qualityOf(documents(diskBefore)[0],"").commandStream);
     assert(service.status().restartRequired&&service.status().message.find("restart")!=std::string::npos);
     auto after=service.snapshot();assert(after->revision==saved->revision&&find(after,"Weather").saved==flipped&&find(after,"CommandStream").saved=="0"&&find(after,"CommandStream").active!="0");
     service.acknowledge(*saved);assert(service.revision()==saved->revision);
     // HDR preview changes the running grading while preserving every saved byte.
     {bool listed=false;for(const auto& f:service.snapshot()->fields)if(f.key=="HDRMaxNits"){listed=true;assert(f.available);}assert(listed);
      const auto before=store.read();const auto serial=service.status().serial;assert(service.preview({{"R\tRenderer\tHDRMaxNits","600"},{"R\tRenderer\tExperimentalHDR","1"}}));
      assert(waitFor([&]{return idle()&&service.status().serial>serial;}));auto p=service.after(saved->revision);assert(p&&p->preview&&p->runtime->hdr[3]==600&&p->runtime->hdr[0]==1&&store.read()==before);
      service.acknowledge(*p);assert(service.clearPreview());assert(waitFor([&]{auto v=service.after(p->revision);return idle()&&v&&!v->preview;}));auto restored=service.after(p->revision);assert(restored->runtime->hdr==saved->runtime->hdr&&store.read()==before);service.acknowledge(*restored);
      const auto bad=service.status().serial;assert(service.commit(saved->revision,{{"R\tRenderer\tHDRMaxNits","99999999"}}));assert(waitFor([&]{return idle()&&service.status().serial>bad;}));assert(service.status().state==Status::State::Error&&store.read()==before);
      service.setHdrStatus(true,0,"active scRGB");const auto status=service.hdrStatus();assert(status.active&&status.code==0&&status.reason=="active scRGB");}
     // stale base and bad values fail without touching the files
     const auto committed=store.read();const auto serial3=service.status().serial;
     assert(service.commit(snap->revision,{{"Q\tQuality\tWeather",savedWeather?"1":"0"}}));assert(waitFor([&]{return idle()&&service.status().serial>serial3;}));
     assert(service.status().state==Status::State::Error&&store.read()==committed);
     const auto serial4=service.status().serial;assert(service.commit(saved->revision,{{"Q\tQuality\tWeather","2"}}));assert(waitFor([&]{return idle()&&service.status().serial>serial4;}));assert(service.status().state==Status::State::Error&&store.read()==committed);
     // a read-only INI surfaces as the cause, then saving works again
     const auto ro=root/FileNames[0];chmod(ro.c_str(),0444);
     if(access(ro.c_str(),W_OK)!=0){const auto serial5=service.status().serial;assert(service.commit(saved->revision,{{"Q\tQuality\tWeather",savedWeather?"1":"0"}}));assert(waitFor([&]{return idle()&&service.status().serial>serial5;}));
      assert(service.status().message.find("northlight-quality.ini is read-only")!=std::string::npos&&store.read()==committed);}
     chmod(ro.c_str(),0644);const auto serial6=service.status().serial;assert(service.commit(saved->revision,{{"Q\tQuality\tWeather",savedWeather?"1":"0"}}));assert(waitFor([&]{return idle()&&service.status().serial>serial6;}));assert(service.status().state==Status::State::Saved);
     // an external edit between snapshot and save is refused
     auto stale=service.snapshot()->revision;auto image=readImage(ro);image.raw+="\n; edited elsewhere\n";writeImage(ro,image.raw);
     const auto serial7=service.status().serial;assert(service.commit(stale,{{"Q\tQuality\tWeather","1"}}));assert(waitFor([&]{return idle()&&service.status().serial>serial7;}));assert(service.status().state==Status::State::Error);}
    // Unresolvable recovery (external edit after a crash): the menu still gets a read-only snapshot and an actionable error; saving is blocked until resolved.
    {auto ini=root/FileNames[0];auto base=store.read();const std::string newRaw="[Quality]\nWeather=1\n";
     writeImage(root/"northlight-settings.journal","prepared\n0 1 "+hash(base[0].raw)+" "+hash(newRaw)+"\n");
     writeImage(root/(std::string(FileNames[0])+".northlight.bak"),base[0].raw);writeImage(ini,"[Quality]\nWeather=0\n; edited by hand\n");
     Service svc(root);auto waitFor=[&](auto pred){for(unsigned i=0;i<1500;++i){if(pred())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return false;};
     assert(waitFor([&]{return svc.requestSnapshot();}));assert(waitFor([&]{return svc.snapshot()&&svc.status().state!=Status::State::Busy;}));
     auto st=svc.status();assert(st.state==Status::State::Error&&st.message.find("northlight-settings.journal")!=std::string::npos&&st.message.find(".northlight.bak")!=std::string::npos&&st.message.find("external edit")!=std::string::npos);
     assert(svc.snapshot()->revision==Store::revision(Store(root).readUnlocked()));
     assert(svc.commit(svc.snapshot()->revision,{{"Q\tQuality\tWeather","1"}}));assert(waitFor([&]{return svc.status().state!=Status::State::Busy;}));
     assert(svc.status().state==Status::State::Error&&readImage(ini).raw.find("edited by hand")!=std::string::npos);
     // after the user resolves it (restore the INI, drop the leftovers) the menu recovers by itself
     writeImage(ini,base[0].raw);std::filesystem::remove(root/"northlight-settings.journal");std::filesystem::remove(root/(std::string(FileNames[0])+".northlight.bak"));
     assert(waitFor([&]{return svc.requestSnapshot();}));assert(waitFor([&]{return svc.status().state!=Status::State::Busy;}));assert(svc.status().state==Status::State::Idle);}
    // Enum values are normalized to the canonical option spelling in the snapshot; the INI text is untouched.
    {Document q("[Quality]\nPreset=performance\nAOMethod=gtao\nNoSuchKey=1\n"),r("[Renderer]\nBackend=DXVK\n");Documents d={q,r,Document(""),Document("")};
     auto f=schema(d,d,configOf(d,""),"","native,dxvk,legacy");unsigned seen=0;
     for(const auto& x:f){auto is=[&](const char* key,const char* value){return x.key==key&&x.overrideValue==value;};
      if(x.key=="Preset"){assert(is("Preset","Performance")&&x.saved=="Performance");++seen;}
      if(x.key=="AOMethod"){assert(is("AOMethod","GTAO")&&x.saved=="GTAO");++seen;}
      if(x.key=="Backend"){assert(is("Backend","dxvk")&&x.saved=="dxvk");++seen;}}
     assert(seen==3);assert(d[0].text().find("Preset=performance")!=std::string::npos);
     Documents bad={Document("[Quality]\nPreset=Turbo\n"),Document("[Renderer]\nBackend=nope\n"),Document(""),Document("")};
     for(const auto& x:schema(bad,bad,configOf(bad,""),"","native,dxvk,legacy"))if(x.key=="Preset")assert(x.overrideValue=="Turbo");else if(x.key=="Backend")assert(x.overrideValue=="nope");}
    // A display profile changes only its calibration; stale or hidden profile edits cannot be committed to another display.
    {Documents profiles=factoryDocuments();std::string p="350",q="700",manual="Manual",ev=".5";
     profiles[1].set("HDR.first","HDRPaperWhite",&p);profiles[1].set("HDR.second","HDRMaxNits",&q);profiles[1].set("HDR.second","HDRPeakMode",&manual);profiles[1].set("Renderer","HDRExposure",&ev);
     const auto first=configOf(profiles,"",true,"first"),second=configOf(profiles,"",true,"second");
     assert(first.hdr[1]==350&&second.hdr[1]==203&&second.hdr[3]==700&&second.hdr[7]==0&&first.hdr[5]==second.hdr[5]);
     const auto a=schema(profiles,profiles,first,"","native,dxvk","first"),b=schema(profiles,profiles,second,"","native,dxvk","second");
     fails([&]{applyChanges(profiles,b,"R\tHDR.first\tHDRPaperWhite\t300\n");});
     const auto edited=applyChanges(profiles,a,"R\tHDR.first\tHDRPaperWhite\t300\n");assert(configOf(edited,"",false,"first").hdr[1]==300&&configOf(edited,"",false,"second").hdr[3]==700);}
    // One-time reset is one transaction. A failed migration cannot set its marker or modify other renderer settings.
    {auto path=root/"hdr-migration";std::filesystem::create_directories(path);Store migrating(path);auto before=migrating.read(),after=before;
     after[1]={true,"[Renderer]\nBackend=dxvk2\nExperimentalHDR=0\nHDRExposure=3\nHDRLegacyExpansion=2\n[HDR.old]\nHDRMaxNits=500\n"};migrating.commit(before,after);
     {TransactionLock lock(path);assert(!migrateHdr(path).empty());assert(migrating.readUnlocked()==after);}
     assert(migrateHdr(path).empty());auto migrated=migrating.read();auto docs=documents(migrated);const auto config=configOf(docs,"");
     assert(config.hdr[0]==1&&config.hdr[5]==0&&config.hdr[6]==1&&config.hdr[7]==1&&config.hdr[9]==0);
     assert(explicitValue(docs[1],"Renderer","Backend")=="dxvk2"&&explicitValue(docs[1],"Renderer","HDRPipelineVersion")=="2"&&explicitValue(docs[1],"HDR.old","HDRMaxNits")=="~");
     const std::string value="1.25";docs[1].set("Renderer","HDRExposure",&value);auto changed=migrated;changed[1]={true,docs[1].bytes()};migrating.commit(migrated,changed);assert(migrateHdr(path).empty()&&migrating.read()==changed);
     Service svc(path);auto waitFor=[&](auto pred){for(unsigned i=0;i<1500;++i){if(pred())return true;std::this_thread::sleep_for(std::chrono::milliseconds(2));}return false;};
     svc.selectHdrProfile("first");assert(waitFor([&]{auto snap=svc.snapshot();return snap&&snap->hdrProfile=="first"&&svc.status().state!=Status::State::Busy;}));
     auto first=svc.after("");assert(first&&first->runtime->hdr[1]==203);svc.acknowledge(*first);
     assert(svc.preview({{"R\tHDR.first\tHDRPaperWhite","300"}}));assert(waitFor([&]{auto u=svc.after(first->revision);return u&&u->preview&&svc.status().state!=Status::State::Busy;}));
     auto preview=svc.after(first->revision);assert(preview->runtime->hdr[1]==300);svc.acknowledge(*preview);
     svc.selectHdrProfile("second");assert(waitFor([&]{auto snap=svc.snapshot();return snap&&snap->hdrProfile=="second"&&svc.status().state!=Status::State::Busy;}));
     auto second=svc.after(preview->revision);assert(second&&!second->preview&&second->runtime->hdr[1]==203&&migrating.read()==changed);
     const auto serial=svc.status().serial;assert(svc.commit(svc.snapshot()->revision,{{"R\tHDR.first\tHDRPaperWhite","300"}}));assert(waitFor([&]{return svc.status().serial>serial&&svc.status().state!=Status::State::Busy;}));assert(svc.status().state==Status::State::Error&&migrating.read()==changed);}
    // The startup baseline: a new Service (or attach) sees the files as they are now.
    {Service again(root);auto s=again.revision();assert(s==Store::revision(store.read()));}
    std::cout<<"Settings: encoding (incl. astral and invalid input), comments, inheritance, bounded schema, rollback, read-only, journal recovery, snapshot, preview coalescing, commit and conflict checks passed\n";
}
