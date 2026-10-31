// Authored CPU component only. No D3D factory, Device, GPU or imported client.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <tlhelp32.h>
#include "native_capture_windows.h"
#include <fstream>
#include <array>
#include <iostream>
#include <iterator>

namespace C=NorthlightStream::ClientCodeObservation;
namespace I=NorthlightStream::ClientImage;
namespace W=NorthlightWorldContext;
namespace {
alignas(4096) const unsigned char Observed[64]={0x17,0x38,0x59,0x7a,0x9b,0xbc,0xdd,0xfe,0x21,0x43,0x65,0x87,0xa9,0xcb,0xed,0x0f};
constexpr const char* WrapperHash="fdb77d7b79418034a56310ad73035821928cdf12eff34a130f1bc315b5ea00cd";
void require(bool ok,const char* reason){if(!ok)throw std::runtime_error(reason);}
std::filesystem::path environment(const wchar_t* name){
    wchar_t text[4096];const auto size=GetEnvironmentVariableW(name,text,4096);
    require(size&&size<4096,"environment-path");std::filesystem::path path(std::wstring(text,size));
    require(path.is_absolute(),"absolute-path");return path;
}
std::filesystem::path module(HMODULE handle){
    wchar_t text[4096];const auto size=GetModuleFileNameW(handle,text,4096);require(size&&size<4096,"module-path");
    return std::filesystem::path(std::wstring(text,size));
}
std::vector<unsigned char> read(const std::filesystem::path& path){
    std::ifstream input(path,std::ios::binary);require(bool(input),"read-file");
    return {std::istreambuf_iterator<char>(input),std::istreambuf_iterator<char>()};
}
std::string hash(const std::filesystem::path& path){auto bytes=read(path);NorthlightBackendLoader::Sha256 sha;sha.update(bytes.data(),bytes.size());return sha.hex();}
std::string json(const std::string& text){
    std::string out="\"";for(unsigned char c:text){require(c>=32,"json-control");if(c=='\\'||c=='\"')out+='\\';out+=char(c);}return out+'\"';
}
std::string hex(const std::array<unsigned char,64>& bytes){
    static const char digits[]="0123456789abcdef";std::string out;
    for(auto byte:bytes){out+=digits[byte>>4];out+=digits[byte&15];}return out;
}
struct Library {HMODULE handle=nullptr;~Library(){if(handle)FreeLibrary(handle);}};
struct DataPatch {
    void* address;DWORD old=0;unsigned char original;bool active=false;
    DataPatch(void* p,unsigned char before):address(p),original(before){
        MEMORY_BASIC_INFORMATION page{};require(VirtualQuery(p,&page,sizeof page)&&page.Type==MEM_IMAGE&&
            (page.Protect&0xff)==PAGE_READONLY,"owned-readonly-data-page");
        require(VirtualProtect(p,1,PAGE_READWRITE,&old)!=FALSE,"data-protect");active=true;
    }
    bool restore()noexcept{
        if(!active)return true;SIZE_T copied=0;DWORD ignored=0;
        const bool wrote=WriteProcessMemory(GetCurrentProcess(),address,&original,1,&copied)&&copied==1;
        const bool protectedAgain=VirtualProtect(address,1,old,&ignored)!=FALSE;
        MEMORY_BASIC_INFORMATION page{};const bool verified=VirtualQuery(address,&page,sizeof page)&&page.Protect==old;
        if(wrote&&protectedAgain&&verified)active=false;
        return wrote&&protectedAgain&&verified;
    }
    ~DataPatch(){if(active)(void)restore();}
};
void moduleList(std::ostream& out){
    const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,GetCurrentProcessId());
    out<<",\"modules\":[";bool complete=false,first=true;
    if(snapshot!=INVALID_HANDLE_VALUE){MODULEENTRY32W entry{};entry.dwSize=sizeof entry;
        if(Module32FirstW(snapshot,&entry)){do{
            if(!first)out<<',';first=false;out<<json(std::filesystem::path(entry.szExePath).u8string());
        }while(Module32NextW(snapshot,&entry));complete=GetLastError()==ERROR_NO_MORE_FILES;}
        CloseHandle(snapshot);
    }
    out<<"],\"module_snapshot_complete\":"<<(complete?"true":"false");
}
}
int main(int argc,char** argv){
    try{
        require(argc==2,"mode-argument");const std::string mode=argv[1];
        require(mode=="no-opt-in"||mode=="prefix"||mode=="match"||mode=="duplicate-patch","mode");
        const auto self=GetModuleHandleW(nullptr);require(reinterpret_cast<std::uintptr_t>(self)==I::View::Base,"actual-fixed-base");
        const auto own=module(self),root=environment(L"NORTHLIGHT_OBSERVER_ROOT"),evidence=environment(L"NORTHLIGHT_OBSERVER_RESULT");
        const auto bytes=read(own);I::View view;I::Error imageError;require(view.open(bytes.data(),bytes.size(),imageError),"own-raw-image");
        const auto address=reinterpret_cast<std::uintptr_t>(Observed);require(view.region(address,sizeof Observed)==I::Region::Static,"own-static-data");
        std::array<unsigned char,64> raw{},first{},second{};require(view.readStatic(address,raw.data(),raw.size()),"own-file-backed-data");
        require(W::SelfReadStats::calls.load()==0&&!W::SelfReadStats::observer.load(),"fresh-read-state");
        Library wrapper;std::filesystem::path wrapperPath;std::string wrapperSha;
        if(mode!="no-opt-in"){
            wrapperPath=environment(L"NORTHLIGHT_OBSERVER_WRAPPER");require(hash(wrapperPath)==WrapperHash,"official-wrapper-pin");
            wrapper.handle=LoadLibraryExW(wrapperPath.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);require(wrapper.handle!=nullptr,"outer-wrapper-load");
            require(GetModuleHandleW(L"d3d9.dll")==wrapper.handle,"outer-wrapper-name");
            const auto marker=GetProcAddress(wrapper.handle,"D3DPERF_SetMarker");require(marker!=nullptr,"outer-marker");
            HMODULE owner=nullptr;require(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(marker),&owner)&&owner==wrapper.handle,"actual-marker-owner");
            wrapperPath=module(owner);wrapperSha=hash(wrapperPath);require(wrapperSha==WrapperHash,"loaded-wrapper-pin");
        }
        if(mode=="prefix")require(W::readSelfLive(address,first.data(),first.size())&&first==raw,"actual-prefix-rpm");
        const char* reason=nullptr;auto session=NorthlightNativeCapture::Session::fromEnvironment(self,reason);
        const std::string refused=reason?reason:"";bool restored=false,decoded=false;
        if(mode=="no-opt-in"){
            require(!session&&!reason&&!NorthlightNativeCapture::Session::requested(),"no-opt-in-session");
            require(std::filesystem::is_empty(root)&&!W::SelfReadStats::observer.load(),"no-opt-in-output-or-observer");
        }else if(mode=="prefix"){
            require(!session&&refused=="client-code-prefix-unobserved","prefix-refusal");
            require(std::filesystem::is_empty(root)&&!W::SelfReadStats::observer.load(),"prefix-output-or-observer");
        }else{
            require(session&&W::SelfReadStats::observer.load(),"actual-session-observer");
            require(W::readSelfLive(address,first.data(),first.size())&&first==raw,"actual-first-rpm");
            if(mode=="duplicate-patch"){
                auto* byte=reinterpret_cast<void*>(address+3);DataPatch patch(byte,first[3]);const unsigned char changed=first[3]^1;SIZE_T copied=0;
                require(WriteProcessMemory(GetCurrentProcess(),byte,&changed,1,&copied)&&copied==1,"write-owned-const-data");
                require(W::readSelfLive(address,second.data(),second.size())&&second[3]==changed&&second!=first,"source-returns-actual-patched-data");
                require(patch.restore(),"restore-owned-data-and-protection");restored=true;
                std::array<unsigned char,64> after{};require(W::readSelfLive(address,after.data(),after.size())&&after==first,"actual-restored-rpm");
            }else require(W::readSelfLive(address,second.data(),second.size())&&second==first,"actual-second-rpm");
            session.reset();require(!W::SelfReadStats::observer.load(),"observer-disconnected");
            std::vector<std::filesystem::path> directories;for(const auto& entry:std::filesystem::directory_iterator(root))if(entry.is_directory())directories.push_back(entry.path());
            require(directories.size()==1,"single-capture-directory");const auto proofPath=directories[0]/L"client-code.nlcode";
            if(mode=="match"){
                const auto encoded=read(proofPath);auto proof=std::make_unique<C::Proof>();C::Error error;
                require(C::decode(encoded.data(),encoded.size(),view,*proof,error)&&proof->spans()==1&&proof->observations()==2&&
                    proof->payloadBytes()==64&&proof->read(address,second.data(),second.size())&&second==first&&!proof->covers(address,16),"actual-observer-proof-decode");decoded=true;
            }else require(!std::filesystem::exists(proofPath),"patched-proof-must-not-publish");
        }
        std::ofstream out(evidence,std::ios::binary);require(bool(out),"evidence-output");
        out<<"{\"format\":1,\"mode\":"<<json(mode)<<",\"authored_component\":true,\"fps_eligible\":false,\"device_calls\":0,\"gpu_used\":false"
            <<",\"self_path\":"<<json(own.u8string())<<",\"self_sha256\":"<<json(hash(own))<<",\"loaded_base\":"<<reinterpret_cast<std::uintptr_t>(self)
            <<",\"wrapper_path\":"<<json(wrapperPath.u8string())<<",\"wrapper_sha256\":"<<json(wrapperSha)<<",\"marker_owner_verified\":"<<(wrapper.handle?"true":"false")
            <<",\"observed_address\":"<<address<<",\"first_read_hex\":"<<json(hex(first))<<",\"second_read_hex\":"<<json(hex(second))
            <<",\"data_restored\":"<<(restored?"true":"false")<<",\"proof_decoded\":"<<(decoded?"true":"false")<<",\"reason\":"<<json(refused)
            <<",\"observer_disconnected\":"<<(!W::SelfReadStats::observer.load()?"true":"false")<<",\"self_read_calls\":"<<W::SelfReadStats::calls.load();
        moduleList(out);out<<",\"pass\":true}\n";out.close();require(bool(out),"evidence-close");
        std::cout<<"NORTHLIGHT_NATIVE_CODE_OBSERVER PASS mode="<<mode<<" authored_component=1 device_calls=0 gpu_used=0 fps_eligible=0\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<"NORTHLIGHT_NATIVE_CODE_OBSERVER failed="<<error.what()<<'\n';return 1;}
}
