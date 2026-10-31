#pragma once
// Explicit diagnostic transport. No environment opt-in means no allocation,
// writer, module pin or filesystem access. Never included in the stand DLL.
#include "native_snapshot_capture.h"
#include "backend_loader.h"
#include "client_code_observation.h"
#include "world_context.h"
#include <memory>
#include <mutex>
#include <stdexcept>

namespace NorthlightNativeCapture {
class Session {
    using Marker=void(WINAPI*)(D3DCOLOR,LPCWSTR);
    HMODULE module_=nullptr;
    Marker marker_=nullptr;
    NorthlightStream::NativeSnapshotCapture capture_;
    using Code=NorthlightStream::ClientCodeObservation::Capture;
    Code code_;
    NorthlightStream::ClientImage::View clientView_;
    std::vector<unsigned char> clientBytes_;
    HANDLE clientFile_=INVALID_HANDLE_VALUE;
    std::filesystem::path directory_;
    std::string id_;
    bool observing_=false,codePrefixValid_=false;
    static inline std::mutex codeLock_;
    static inline Session* codeOwner_=nullptr;
    static inline std::atomic<bool> codeFault_{false};
    static void observe(std::uintptr_t address,const void* bytes,std::size_t size,bool ok) noexcept {
        try{
            std::unique_lock<std::mutex> lock(codeLock_,std::try_to_lock);
            if(!lock.owns_lock()){codeFault_.store(true,std::memory_order_relaxed);return;}
            if(codeOwner_)codeOwner_->code_.record(codeOwner_->clientView_,address,bytes,size,ok);
        }catch(...){codeFault_.store(true,std::memory_order_relaxed);}
    }
    bool loadClient(){
        if(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=NorthlightStream::ClientImage::View::Base)return false;
        std::wstring path;if(!modulePath(nullptr,path))return false;
        clientFile_=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        LARGE_INTEGER size{};DWORD got=0;
        if(clientFile_==INVALID_HANDLE_VALUE||!GetFileSizeEx(clientFile_,&size)||size.QuadPart<64||size.QuadPart>(64ll<<20))return false;
        clientBytes_.resize(std::size_t(size.QuadPart));
        if(!ReadFile(clientFile_,clientBytes_.data(),DWORD(clientBytes_.size()),&got,nullptr)||got!=clientBytes_.size())return false;
        NorthlightStream::ClientImage::Error error;
        return clientView_.open(clientBytes_.data(),clientBytes_.size(),error);
    }
    bool startObserving(){
        std::lock_guard<std::mutex> lock(codeLock_);
        if(codeOwner_||NorthlightWorldContext::SelfReadStats::observer.load()||!code_.begin(NorthlightWorldContext::SelfReadStats::calls.load()))return false;
        codeOwner_=this;codeFault_.store(false);observing_=true;
        NorthlightWorldContext::SelfReadStats::observer.store(&Session::observe,std::memory_order_release);
        // A read that began before installation must not become an unseen cached prefix.
        codePrefixValid_=NorthlightWorldContext::SelfReadStats::calls.load()==0;return codePrefixValid_;
    }
    void stopObserving(){
        std::lock_guard<std::mutex> lock(codeLock_);
        if(codeOwner_==this){NorthlightWorldContext::SelfReadStats::observer.store(nullptr,std::memory_order_release);codeOwner_=nullptr;}
    }
    static bool writeNew(const std::filesystem::path& path,const void* bytes,std::size_t size){
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;DWORD written=0;
        const bool ok=size<=MAXDWORD&&WriteFile(file,bytes,DWORD(size),&written,nullptr)&&written==size;
        return CloseHandle(file)!=FALSE&&ok;
    }
    void saveCodeProof() noexcept {
        if(!observing_||directory_.empty())return;
        try{
            namespace C=NorthlightStream::ClientCodeObservation;
            std::array<unsigned char,C::MaxEncodedBytes> bytes{};std::size_t size=0;C::Error error;
            bool complete=codePrefixValid_&&!codeFault_.load()&&C::encode(code_,bytes.data(),bytes.size(),size,error);
            std::string hash;
            const auto proof=directory_/L"client-code.nlcode";
            complete=complete&&writeNew(proof,bytes.data(),size)&&fileHash(proof.wstring(),hash);
            const std::string status="{\"format\":1,\"session\":\""+id_+"\",\"complete\":"+(complete?"true":"false")+
                ",\"prefix_calls\":0,\"policy\":\"strict-per-read\",\"bytes\":"+std::to_string(complete?size:0)+
                ",\"sha256\":\""+hash+"\",\"fps_eligible\":false}\n";
            const auto pending=directory_/L"client-code-status.pending",final=directory_/L"client-code-status.json";
            if(writeNew(pending,status.data(),status.size()))(void)MoveFileW(pending.c_str(),final.c_str());
        }catch(...){} // Missing/failed final status is refused by the plan compiler.
    }
    static bool environment(const wchar_t* name,std::wstring& value){
        wchar_t text[4096];const DWORD n=GetEnvironmentVariableW(name,text,4096);
        if(!n||n>=4096)return false;value.assign(text,n);return true;
    }
    static bool absolute(const std::wstring& value){return !value.empty()&&std::filesystem::path(value).is_absolute();}
    static bool modulePath(HMODULE module,std::wstring& path){
        wchar_t text[4096];const DWORD n=GetModuleFileNameW(module,text,4096);
        if(!n||n>=4096)return false;path.assign(text,n);return true;
    }
    static bool fileHash(const std::wstring& path,std::string& hash){
        HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        NorthlightBackendLoader::Sha256 sha;unsigned char bytes[16384];DWORD got=0;bool ok=true;
        for(;;){if(!ReadFile(file,bytes,sizeof bytes,&got,nullptr)){ok=false;break;}if(!got)break;sha.update(bytes,got);}
        ok=CloseHandle(file)!=FALSE&&ok;if(ok)hash=sha.hex();return ok;
    }
    static std::string json(const std::wstring& text){
        const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),nullptr,0,nullptr,nullptr);
        if(n<=0)throw std::runtime_error("Invalid UTF-16 capture path");
        std::string utf8(std::size_t(n),'\0'),out="\"";
        if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(text.size()),utf8.data(),n,nullptr,nullptr)!=n)throw std::runtime_error("Capture path conversion failed");
        for(unsigned char c:utf8){if(c=='\"'||c=='\\')out+='\\';if(c<32)throw std::runtime_error("Control character in capture path");out+=char(c);}
        return out+'\"';
    }
    static bool forward(void* opaque,const wchar_t* text,unsigned length)noexcept {
        auto* self=static_cast<Session*>(opaque);
        if(!self||!self->marker_||!text||!length||length>=NorthlightStream::NativeSnapshotCapture::MarkerChars||text[length])return false;
        self->marker_(0x4e4c0001,text);return true; // Forwarding alone does not prove trace recording.
    }
    bool identity(const std::filesystem::path& directory,const std::string& id,HMODULE self,const std::wstring& trace,const std::wstring& downstream){
        std::wstring wrapper,renderer,client;std::string wrapperHash,rendererHash,clientHash;
        if(!modulePath(module_,wrapper)||!modulePath(self,renderer)||!modulePath(nullptr,client)||
           !fileHash(wrapper,wrapperHash)||!fileHash(renderer,rendererHash)||!fileHash(client,clientHash))return false;
        const std::string text="{\"format\":1,\"session\":\""+id+"\",\"capture_layer\":\"above-northlight\",\"diagnostic\":true,\"fps_eligible\":false,\"trace_join_validated\":false,\"wrapper_path\":"+json(wrapper)+
            ",\"wrapper_sha256\":\""+wrapperHash+"\",\"renderer_path\":"+json(renderer)+",\"renderer_sha256\":\""+rendererHash+
            "\",\"client_path\":"+json(client)+",\"client_sha256\":\""+clientHash+"\",\"trace_path\":"+json(trace)+",\"force_module_path\":"+json(downstream)+",\"client_code_policy\":\"strict-per-read\"}\n";
        const auto path=directory/L"capture-host.json";
        if(text.size()>65536)return false;
        HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;DWORD written=0;
        const bool ok=WriteFile(file,text.data(),DWORD(text.size()),&written,nullptr)&&written==text.size();
        return CloseHandle(file)!=FALSE&&ok;
    }
public:
    static bool requested(){
        SetLastError(ERROR_SUCCESS);
        return GetEnvironmentVariableW(L"NORTHLIGHT_NATIVE_CAPTURE_ROOT",nullptr,0)!=0||GetLastError()!=ERROR_ENVVAR_NOT_FOUND;
    }
    ~Session(){stopObserving();capture_.finish();saveCodeProof();if(clientFile_!=INVALID_HANDLE_VALUE)CloseHandle(clientFile_);if(module_)FreeLibrary(module_);}
    Session()=default;Session(const Session&)=delete;Session& operator=(const Session&)=delete;
    void* sink(){return &capture_;}
    static std::shared_ptr<Session> fromEnvironment(HMODULE self,const char*& reason){
        reason=nullptr;
        // Probe without constructing a string: the normal path remains empty.
        if(!requested())return {};
        reason="invalid-explicit-environment";
        try{
            std::wstring root,trace,downstream;
            if(!environment(L"NORTHLIGHT_NATIVE_CAPTURE_ROOT",root)||!environment(L"TRACE_FILE",trace)||
               !environment(L"APITRACE_FORCE_MODULE_PATH",downstream)||!absolute(root)||!absolute(trace)||!absolute(downstream)||
               !std::filesystem::is_directory(root)||!std::filesystem::is_directory(downstream))return {};
            const HMODULE outer=GetModuleHandleW(L"d3d9.dll");
            if(!outer||outer==self){reason="outer-tracer-module-required";return {};}
            const auto procedure=GetProcAddress(outer,"D3DPERF_SetMarker");
            if(!procedure){reason="outer-marker-export-required";return {};}
            auto session=std::make_shared<Session>();
            if(NorthlightWorldContext::SelfReadStats::calls.load()!=0){reason="client-code-prefix-unobserved";return {};}
            if(!session->loadClient()){reason="client-code-image-unavailable";return {};}
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,reinterpret_cast<LPCWSTR>(procedure),&session->module_)||
               session->module_!=outer||session->module_==self){reason="outer-marker-owner-mismatch";return {};}
            static_assert(sizeof session->marker_==sizeof procedure,"Win32 marker function representation");
            std::memcpy(&session->marker_,&procedure,sizeof procedure);
            const std::string id="capture-"+std::to_string(GetCurrentProcessId())+'-'+std::to_string(GetTickCount64())+'-'+
                std::to_string(reinterpret_cast<std::uintptr_t>(session.get()));
            const auto directory=std::filesystem::path(root)/id;
            NorthlightStream::SnapshotCaptureSink::Limits limits;
            limits.bytes-=3*65536; // Owner metadata, bounded code proof and status share the existing cap.
            if(!session->capture_.open(directory,id,&Session::forward,session.get(),limits)){reason="snapshot-writer-open-failed";return {};}
            session->directory_=directory;session->id_=id;
            if(!session->identity(directory,id,self,trace,downstream)){reason="capture-identity-write-failed";return {};}
            if(!session->startObserving()){reason="client-code-observer-prefix-or-owner";return {};}
            reason="diagnostic-only-trace-join-unvalidated";return session;
        }catch(...){reason="capture-initialization-exception";return {};}
    }
};
} // namespace NorthlightNativeCapture
