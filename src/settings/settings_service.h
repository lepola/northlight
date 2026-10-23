#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include "settings_schema.h"

namespace NorthlightSettings {
struct Update{Config config;std::string revision;std::shared_ptr<const Config> runtime;};
class Service {
    std::filesystem::path directory;
    Store store;Documents initial;std::string legacy;
    mutable std::mutex mutex;std::condition_variable wake;std::thread worker;bool stopping=false,busy=false;
    std::string jobId,request,response,activeRevision;std::shared_ptr<const Config> active;std::shared_ptr<const Update> update;
    std::string hdrStatus="CAP\tHDR\tinactive\t0\n";
    std::string snapshotKey,snapshotBody; // Worker-owned; file and active revisions invalidate the serialized schema.
    std::atomic<int> previewAO{-1}; // -1: saved state; otherwise method in bits 0..1, enabled in bit 2.
    std::string execute(const std::string& command){
        if(command=="preview-ao\tsaved"){previewAO.store(-1);return "PREVIEW\tsaved\n";}
        if(command.rfind("preview-ao\t",0)==0){
            const char* methods[]={"SSAO","HDAO","HBAOPlus","GTAO"};
            for(unsigned i=0;i<4;++i)for(unsigned enabled=0;enabled<2;++enabled)
                if(command==std::string("preview-ao\t")+methods[i]+"\t"+std::to_string(enabled)){
                    previewAO.store(int(i|(enabled<<2)));return "PREVIEW\t"+std::string(methods[i])+"\n";}
            throw std::runtime_error("Invalid AO preview");
        }
        auto at=command.find('\n');auto header=command.substr(0,at);auto before=store.read();auto revision=Store::revision(before);
        Config runtime;std::string runtimeRevision;{std::lock_guard<std::mutex> lock(mutex);runtime=*active;runtimeRevision=activeRevision;}
        if(header=="snapshot"){const int preview=previewAO.load();if(preview>=0){for(const auto& key:NorthlightQuality::Keys)if(std::string(key.name)=="AOMethod")runtime.quality.*key.field=unsigned(preview)&3u;runtime.quality.contactAO=(preview&4)!=0;}}
        std::string backends="native";for(auto name:{"dxvk","dxvk2","legacy"}){std::error_code ec;const auto file=directory/"renderer-backends"/name/(std::string(name)+"_d3d9.dll");if(std::filesystem::is_regular_file(file,ec))backends+=","+std::string(name);}
        const auto key=revision+"/"+runtimeRevision+"/"+backends+"/"+std::to_string(previewAO.load());
        if(header=="snapshot"&&key==snapshotKey){std::lock_guard<std::mutex> lock(mutex);return snapshotBody+hdrStatus+"CAP\tAOPreview\t0\n";}
        auto docs=documents(before);auto fields=schema(docs,initial,runtime,legacy,backends);
        if(header=="snapshot"){snapshotBody=serialize(fields,revision,runtimeRevision);snapshotKey=key;std::lock_guard<std::mutex> lock(mutex);return snapshotBody+hdrStatus+"CAP\tAOPreview\t0\n";}
        if(header.rfind("commit\t",0)!=0||at==command.npos)throw std::runtime_error("Unknown settings operation");
        if(header.substr(7)!=revision)throw std::runtime_error("INI files changed externally; refresh before saving");
        auto next=applyChanges(docs,fields,command.substr(at+1));auto config=configOf(next,legacy,true);auto after=before;
        for(unsigned i=0;i<4;++i){auto bytes=next[i].bytes();if(bytes!=before[i].raw)after[i]={true,std::move(bytes)};}
        auto effective=std::make_shared<Config>(runtime);effective->quality=liveQualityUpdate(runtime.quality,config.quality);effective->celestial=config.celestial;for(unsigned i=1;i<effective->hdr.size();++i)effective->hdr[i]=config.hdr[i];
        auto pending=std::make_shared<Update>(Update{std::move(config),Store::revision(after),std::move(effective)});
        store.commit(before,after);
        {std::lock_guard<std::mutex> lock(mutex);update=pending;previewAO.store(-1);}
        return "SAVED\t"+pending->revision+"\n";
    }
    void work(){
        for(;;){std::string command;{std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return stopping||busy;});if(stopping)return;command=request;}
            std::string result;try{result=execute(command);}catch(const std::exception& e){result="ERROR\t"+std::string(e.what())+"\n";}catch(...){result="ERROR\tSettings allocation failed\n";}
            {std::lock_guard<std::mutex> lock(mutex);response=std::move(result);busy=false;}}
    }
    Service(const std::filesystem::path& root,const Images& snapshot):directory(root),store(root),initial(documents(snapshot)),legacy(NorthlightQuality::narrow(readImage(root/"shadow-experiment.ini").raw)),activeRevision(Store::revision(snapshot)),active(std::make_shared<Config>(configOf(initial,legacy))){worker=std::thread([this]{work();});}
public:
    int aoPreview()const{return previewAO.load();}
    void setHdrStatus(bool enabled,unsigned status,const char* reason=nullptr){std::lock_guard<std::mutex> lock(mutex);hdrStatus="CAP\tHDR\t"+std::string(enabled?"active":"inactive")+"\t"+std::to_string(status)+(reason?"\t"+std::string(reason):"")+"\n";}
    explicit Service(const std::filesystem::path& root):Service(root,Store(root).read()){}
    ~Service(){{std::lock_guard<std::mutex> lock(mutex);stopping=true;}wake.notify_one();if(worker.joinable())worker.join();}
    bool submit(const std::string& id,const std::string& command){
        if(command.size()>131072||id.empty()||id.size()>64)return false;std::lock_guard<std::mutex> lock(mutex);
        if(jobId==id)return true;if(busy)return false;jobId=id;request=command;response.clear();busy=true;wake.notify_one();return true;
    }
    bool result(const std::string& id,std::string& out)const{std::lock_guard<std::mutex> lock(mutex);if(jobId!=id||busy)return false;out=response;return true;}
    std::shared_ptr<const Update> after(const std::string& revision)const{std::lock_guard<std::mutex> lock(mutex);return update&&update->revision!=revision?update:nullptr;}
    void acknowledge(const Update& value){std::lock_guard<std::mutex> lock(mutex);active=value.runtime;activeRevision=value.revision;}
};
inline std::mutex serviceMutex;
inline std::weak_ptr<Service> activeService;
inline std::shared_ptr<Service> acquire(const std::filesystem::path& root){std::lock_guard<std::mutex> lock(serviceMutex);auto s=activeService.lock();if(!s){s=std::make_shared<Service>(root);activeService=s;}return s;}
inline std::shared_ptr<Service> peek(){std::lock_guard<std::mutex> lock(serviceMutex);return activeService.lock();}
} // namespace NorthlightSettings
