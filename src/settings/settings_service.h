#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include "settings_schema.h"

namespace NorthlightSettings {
// Renderer side: a pending config for the frame boundary. preview=true marks a draft that is not in the INIs.
struct Update{Config config;std::string revision;std::shared_ptr<const Config> runtime;bool preview=false;};
// Menu side (in-process; all calls are non-blocking and thread-safe, the worker thread does every file access).
struct Snapshot{std::string revision,activeRevision;std::vector<Field> fields;};
struct Change{std::string id,value;}; // value: literal, "~" = remove the override (inherit), "@factory"
struct Status{
    enum class State{Idle,Busy,Saved,Error} state=State::Idle;
    std::string message;        // human readable, English
    uint64_t serial=0;          // increments on every completed job (snapshot, preview, commit)
    bool restartRequired=false; // a saved "R" field differs from what the running renderer uses
};
struct HdrStatus{bool active=false;unsigned code=0;std::string reason;};
class Service {
    struct Commit{std::string base;std::vector<Change> changes;};
    struct Preview{bool clear=false;std::vector<Change> draft;};
    std::filesystem::path directory;
    Store store;std::string legacy;HdrStatus hdr;
    mutable std::mutex mutex;std::condition_variable wake;bool stopping=false,busy=false,snapshotJob=false;
    std::optional<Commit> commitJob;std::optional<Preview> previewJob; // latest wins; a commit is never replaced
    Documents initial;std::string initialRevision;          // creation-time state of the running renderer ("R" fields compare against it)
    std::string activeRevision;std::shared_ptr<const Config> active;std::shared_ptr<const Update> update;
    std::shared_ptr<const Snapshot> latest;Status last;bool previewing=false,committing=false,restart=false;uint64_t previewSeq=0;
    std::thread worker;
    struct Loaded{Images images;Documents docs;std::string revision,problem;Config runtime;std::vector<Field> fields;};
    static bool restartNeeded(const std::vector<Field>& fields){for(const auto& f:fields)if(f.available&&f.apply=="R"&&f.saved!=f.active)return true;return false;}
    std::string backends()const{
        std::string out="native";for(auto name:{"dxvk","dxvk2","legacy"}){std::error_code ec;const auto file=directory/"renderer-backends"/name/(std::string(name)+"_d3d9.dll");if(std::filesystem::is_regular_file(file,ec))out+=","+std::string(name);}return out;
    }
    Loaded load(){
        Loaded l;l.images=store.readLenient(l.problem);l.docs=documents(l.images);l.revision=Store::revision(l.images);Documents base;
        {std::lock_guard<std::mutex> lock(mutex);l.runtime=*active;base=initial;}
        l.fields=schema(l.docs,base,l.runtime,legacy,backends());return l;
    }
    static std::string text(const std::vector<Change>& changes){
        std::string out;for(const auto& c:changes){
            if(c.id.find_first_of("\r\n")!=c.id.npos||c.value.find_first_of("\r\n")!=c.value.npos)throw std::runtime_error("Invalid settings change");out+=c.id+"\t"+c.value+"\n";}return out;
    }
    // Live part of a parsed config on top of what the renderer runs: "R" keys keep their running values.
    static std::shared_ptr<Config> effective(const Config& runtime,const Config& config){
        auto out=std::make_shared<Config>(runtime);out->quality=liveQualityUpdate(runtime.quality,config.quality);out->celestial=config.celestial;
        for(unsigned i=1;i<out->hdr.size();++i)out->hdr[i]=config.hdr[i];return out;
    }
    // Caller holds no lock. Publishes the update the renderer applies at its next frame boundary.
    void publish(Config config,std::string revision,std::shared_ptr<const Config> runtime,bool preview){
        auto pending=std::make_shared<Update>(Update{std::move(config),std::move(revision),std::move(runtime),preview});
        std::lock_guard<std::mutex> lock(mutex);update=std::move(pending);previewing=preview;
    }
    void publishSnapshot(const std::string& revision,const std::string& activeRev,std::vector<Field> fields){
        const bool needed=restartNeeded(fields);auto s=std::make_shared<Snapshot>(Snapshot{revision,activeRev,std::move(fields)});
        std::lock_guard<std::mutex> lock(mutex);latest=std::move(s);restart=needed;
    }
    // The snapshot (read-only view of the current files) is still delivered; saving stays blocked until the user resolves the leftovers.
    static std::string blocked(const std::string& cause){
        return "An interrupted save could not be rolled back ("+cause+"). Saving is disabled. In the game folder, restore the INI from its .northlight.bak copy, then delete northlight-settings.journal and the *.northlight.bak/*.northlight.tmp files, and reopen the menu.";
    }
    Status doSnapshot(){
        auto l=load();std::string activeRev;{std::lock_guard<std::mutex> lock(mutex);activeRev=activeRevision;}
        publishSnapshot(l.revision,activeRev,std::move(l.fields));
        Status s;if(!l.problem.empty()){s.state=Status::State::Error;s.message=blocked(l.problem);}return s;
    }
    Status doPreview(const Preview& job){
        Status s;
        if(job.clear){bool was;{std::lock_guard<std::mutex> lock(mutex);was=previewing;}
            if(!was)return s;
            auto l=load();auto config=configOf(l.docs,legacy);auto runtime=effective(l.runtime,config);
            publish(std::move(config),l.revision,std::move(runtime),false);s.message="Preview cleared";return s;}
        auto l=load();std::vector<Change> live;std::map<std::string,const Field*> known;for(const auto& f:l.fields)known[f.id()]=&f;
        for(const auto& c:job.draft){auto at=known.find(c.id);if(at!=known.end()&&(at->second->apply=="F"&&at->second->available))live.push_back(c);} // "R" and unknown ids are not previewable
        auto next=applyChanges(l.docs,l.fields,text(live));auto config=configOf(next,legacy,true);auto runtime=effective(l.runtime,config);
        uint64_t seq;{std::lock_guard<std::mutex> lock(mutex);seq=++previewSeq;}
        publish(std::move(config),l.revision+"+preview"+std::to_string(seq),std::move(runtime),true);s.message="Preview applied";return s;
    }
    Status doCommit(const Commit& job){
        auto l=load();
        if(!l.problem.empty())throw std::runtime_error(blocked(l.problem));
        if(job.base!=l.revision)throw std::runtime_error("INI files changed externally; refresh before saving");
        auto next=applyChanges(l.docs,l.fields,text(job.changes));auto config=configOf(next,legacy,true);auto after=l.images;
        for(unsigned i=0;i<4;++i){auto bytes=next[i].bytes();if(bytes!=l.images[i].raw)after[i]={true,std::move(bytes)};}
        auto runtime=effective(l.runtime,config);const auto revision=Store::revision(after);
        store.commit(l.images,after); // throws before anything is published; on failure the files are unchanged
        publish(config,revision,runtime,false);
        Documents base;{std::lock_guard<std::mutex> lock(mutex);base=initial;}
        publishSnapshot(revision,revision,schema(next,base,*runtime,legacy,backends()));
        Status s;s.state=Status::State::Saved;{std::lock_guard<std::mutex> lock(mutex);s.message=restart?"Saved; some changes apply after a restart":"Saved";}return s;
    }
    void work(){
        for(;;){
            std::optional<Commit> commit;std::optional<Preview> preview;
            {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stopping||commitJob||previewJob||snapshotJob;});if(stopping)return;
             if(commitJob){commit=std::move(commitJob);commitJob.reset();committing=true;}else if(previewJob){preview=std::move(previewJob);previewJob.reset();}else snapshotJob=false;
             busy=true;}
            Status result;
            try{result=commit?doCommit(*commit):preview?doPreview(*preview):doSnapshot();}
            catch(const std::exception& e){result.state=Status::State::Error;result.message=e.what();}
            catch(...){result.state=Status::State::Error;result.message="Settings allocation failed";}
            {std::lock_guard<std::mutex> lock(mutex);result.serial=last.serial+1;last=std::move(result);busy=committing=false;}
        }
    }
public:
    // No file access that can fail for good: the creation-time snapshot is read unlocked (the renderer's own loaders do the same).
    explicit Service(const std::filesystem::path& root):directory(root),store(root){
        attach();worker=std::thread([this]{work();});
    }
    ~Service(){{std::lock_guard<std::mutex> lock(mutex);stopping=true;}wake.notify_one();if(worker.joinable())worker.join();}
    // A (re)created device reads the INIs afresh: re-baseline what "running" means and drop any draft aimed at the previous device.
    void attach(){
        Images images;std::string leg;try{images=store.readUnlocked();leg=NorthlightQuality::narrow(readImage(directory/"shadow-experiment.ini").raw);}catch(...){images={};}
        Documents docs;Config config;try{docs=documents(images);config=configOf(docs,leg);}catch(...){docs=documents(Images{});config=configOf(docs,leg);}
        std::lock_guard<std::mutex> lock(mutex);legacy=leg;initial=std::move(docs);initialRevision=Store::revision(images);activeRevision=initialRevision;
        active=std::make_shared<Config>(std::move(config));update=nullptr;previewing=false;
    }
    void setHdrStatus(bool active,unsigned code,const std::string& reason=""){std::lock_guard<std::mutex> lock(mutex);hdr={active,code,reason};}
    HdrStatus hdrStatus()const{std::lock_guard<std::mutex> lock(mutex);return hdr;}
    std::string revision()const{std::lock_guard<std::mutex> lock(mutex);return activeRevision;}
    // false while any job is queued or running; the overlay retries next frame.
    bool requestSnapshot(){std::lock_guard<std::mutex> lock(mutex);if(busy||snapshotJob||commitJob||previewJob)return false;snapshotJob=true;wake.notify_one();return true;}
    std::shared_ptr<const Snapshot> snapshot()const{std::lock_guard<std::mutex> lock(mutex);return latest;}
    // Live-only draft, no file writes. Coalesced: a newer draft replaces one that has not started. Only "F" fields take part.
    bool preview(std::vector<Change> draft){
        if(draft.size()>1024)return false;std::lock_guard<std::mutex> lock(mutex);previewJob=Preview{false,std::move(draft)};wake.notify_one();return true;
    }
    bool clearPreview(){std::lock_guard<std::mutex> lock(mutex);previewJob=Preview{true,{}};wake.notify_one();return true;}
    bool commit(const std::string& baseRevision,std::vector<Change> changes){
        if(changes.size()>1024)return false;std::lock_guard<std::mutex> lock(mutex);if(commitJob||committing)return false;
        commitJob=Commit{baseRevision,std::move(changes)};wake.notify_one();return true;
    }
    Status status()const{std::lock_guard<std::mutex> lock(mutex);Status s=last;s.restartRequired=restart;if(busy||snapshotJob||commitJob||previewJob)s.state=Status::State::Busy;return s;}
    std::shared_ptr<const Update> after(const std::string& revision)const{std::lock_guard<std::mutex> lock(mutex);return update&&update->revision!=revision?update:nullptr;}
    void acknowledge(const Update& value){std::lock_guard<std::mutex> lock(mutex);active=value.runtime;activeRevision=value.preview?activeRevision:value.revision;}
};
inline std::mutex serviceMutex;
inline std::weak_ptr<Service> activeService;
inline std::shared_ptr<Service> acquire(const std::filesystem::path& root){
    std::lock_guard<std::mutex> lock(serviceMutex);auto s=activeService.lock();
    if(s)s->attach();else{s=std::make_shared<Service>(root);activeService=s;}
    return s;
}
} // namespace NorthlightSettings
