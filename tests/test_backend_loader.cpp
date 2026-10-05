// Guarded backend loading (backend_loader.h) against a fake file system and
// loader. Optional argv: real files to scan, as "expect:path" (dxvk-<version>[+env], ours, plain).
#include "backend_loader.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <map>
using namespace NorthlightBackendLoader;
template<size_t N> static std::string lit(const char (&s)[N]){return std::string(s,N-1);}
static std::vector<unsigned char> bytes(const std::string& s){return {s.begin(),s.end()};}
static const std::string DXVK31=lit("MZ...DXVK: \0v3.1.1\0Build: \0x86\0...DXVK_CONFIG\0");
static const std::string DXVK27=lit("MZ...DXVK: \0v2.7.1\0Build: \0x86\0...DXVK_CONFIG\0DXVK_CONFIG_FILE\0");
static const std::string DXVK110=lit("MZ..DXVK: \0\0\0v1.10.3-20230507-async (macOS)\0..DXVK_CONFIG_FILE\0");
static const std::string OURS=lit("MZ..Northlight renderer 0.3.147; backend=%s DXVK_CONFIG\0");
// An older proxy build under another product name: only the name-free PROXY log format identifies it.
static const std::string OLD_PROXY=lit("MZ..Renamed renderer 0.3.165; backend=%s DXVK_CONFIG\0PROXY module=%ls root=%ls (host exe directory)\0DXVK: \0v2.7.1\0");
static const std::string RESHADE="MZ..ReShade 6.0 d3d9 proxy";
static int selfToken,otherToken;
struct FakeSys {
    using Module=int;   // 0 = none, 1 = this proxy, >=2 = other modules
    struct File{int id;std::string data;bool loadsSelf=false,loadFails=false,factory=true,factoryInSelf=false;};
    std::wstring selfPath=L"C:\\Game\\d3d9.dll";int selfId=7;
    std::map<std::wstring,File> files;std::vector<std::wstring> loaded,before;int releases=0,next=2;
    std::map<int,File*> modules;
    FakeSys(){files[NorthlightBackend::fold(selfPath)]=File{7,OURS};}
    void add(const std::wstring& p,File f){files[NorthlightBackend::fold(p)]=f;}
    const std::wstring& self(){return selfPath;}
    std::wstring full(const std::wstring& p){return p.find(L"<bad>")!=std::wstring::npos?std::wstring():p;}
    File* get(const std::wstring& p){auto f=files.find(NorthlightBackend::fold(p));return f==files.end()?nullptr:&f->second;}
    int identity(const std::wstring& p,unsigned long& error){File* f=get(p);if(!f){error=2;return -1;}return f->id==selfId;}
    bool read(const std::wstring& p,std::vector<unsigned char>& out){File* f=get(p);if(!f)return false;out=bytes(f->data);return true;}
    void beforeLoad(const std::wstring& p,const Inspection&){before.push_back(p);}
    int load(const std::wstring& p,unsigned long& error){loaded.push_back(p);File* f=get(p);
        if(!f||f->loadFails){error=126;return 0;}if(f->loadsSelf)return 1;modules[next]=f;return next++;}
    bool isSelf(int m){return m==1;}
    void* proc(int m,const char* name){assert(std::string(name)=="Direct3DCreate9");File* f=modules[m];
        return !f->factory?nullptr:f->factoryInSelf?(void*)&selfToken:(void*)&otherToken;}
    bool ownedBySelf(void* p){return p==&selfToken;}
    void release(int){++releases;}
};
static const std::wstring SYS=L"C:\\windows\\system32\\d3d9.dll",DX=L"C:\\Game\\renderer-backends\\dxvk\\dxvk_d3d9.dll",
    LEG=L"C:\\Game\\renderer-backends\\legacy\\legacy_d3d9.dll",GAME=L"C:\\Game\\d3d9.dll";
static FakeSys world(){FakeSys s;s.add(SYS,{1,"MZ system d3d9"});s.add(DX,{2,DXVK27});return s;}
static void inspection(){
    auto a=inspect((const unsigned char*)DXVK27.data(),DXVK27.size());
    assert(a.read&&a.dxvk&&a.dxvkVersion=="v2.7.1"&&a.dxvkConfigEnv&&!a.ours);
    auto a3=inspect((const unsigned char*)DXVK31.data(),DXVK31.size());
    assert(a3.read&&a3.dxvk&&a3.dxvkVersion=="v3.1.1"&&a3.dxvkConfigEnv&&!a3.ours);
    auto b=inspect((const unsigned char*)DXVK110.data(),DXVK110.size());
    assert(b.dxvk&&b.dxvkVersion=="v1.10.3-20230507-async (macOS)"&&!b.dxvkConfigEnv);
    auto c=inspect((const unsigned char*)OURS.data(),OURS.size());assert(c.ours&&!c.dxvk&&!c.dxvkConfigEnv);
    auto p=inspect((const unsigned char*)OLD_PROXY.data(),OLD_PROXY.size());assert(p.ours&&!p.dxvk&&p.dxvkVersion.empty());
    const std::string cut=OLD_PROXY.substr(0,OLD_PROXY.find("root=%ls")+7);   // truncated PROXY format: not ours
    auto q=inspect((const unsigned char*)cut.data(),cut.size());assert(!q.ours);
    auto d=inspect((const unsigned char*)RESHADE.data(),RESHADE.size());assert(!d.ours&&!d.dxvk);
    // Version resource only (UTF-16 ProductName "DXVK" after padding).
    const std::string res=lit("P\0r\0o\0d\0u\0c\0t\0N\0a\0m\0e\0\0\0\0\0D\0X\0V\0K\0\0\0");
    auto e=inspect((const unsigned char*)res.data(),res.size());assert(e.dxvk&&e.dxvkVersion.empty());
    auto f=inspect((const unsigned char*)res.data(),26);assert(!f.dxvk);   // truncated at the key: no overrun
    auto g=inspect(nullptr,0);assert(g.read&&!g.dxvk&&!g.ours);
}
static void loading(){
    {FakeSys s=world();auto r=load(s,{DX},SYS);   // normal DXVK backend
     assert(r.module>=2&&!r.fallback&&r.attempts.size()==1&&r.attempts[0].outcome==Outcome::Loaded&&r.attempts[0].info.dxvkVersion=="v2.7.1");
     assert(s.before.size()==1&&s.before[0]==DX);
     Sha256 h;h.update(DXVK27.data(),DXVK27.size());assert(r.attempts[0].info.sha256==h.hex());}
    // Only one module may be named d3d9.dll: a backend with that name beside the proxy fails loudly.
    {FakeSys s=world();s.add(L"C:\\Game\\reshade\\d3d9.dll",{5,RESHADE});auto r=load(s,{L"C:\\Game\\reshade\\d3d9.dll"},SYS);
     assert(!r.module&&!r.fallback&&r.attempts.size()==1&&r.attempts[0].outcome==Outcome::D3d9Name&&s.loaded.empty()&&s.before.empty());}
    {FakeSys s=world();auto r=load(s,{SYS},SYS);assert(r.module>=2&&!r.fallback);}   // Backend=native: the system runtime keeps its name
    // macOS: proxy preloaded from <game>\\mods\\ (dlls.txt), WoWSilicon DXVK copied under a new name.
    {FakeSys s=world();s.files.clear();s.selfPath=L"C:\\Game\\mods\\d3d9.dll";s.add(s.selfPath,{7,OURS});s.add(DX,{2,DXVK110});s.add(GAME,{4,DXVK110});
     auto r=load(s,{DX},SYS);assert(r.module>=2&&r.attempts[0].info.dxvkVersion=="v1.10.3-20230507-async (macOS)");
     auto g=load(s,{GAME},SYS);assert(!g.module&&g.attempts[0].outcome==Outcome::D3d9Name);}
    {FakeSys s=world();auto r=load(s,{L"c:/GAME/D3D9.dll"},SYS);   // configured path is this proxy
     assert(r.attempts[0].outcome==Outcome::SelfPath&&r.fallback&&r.module>=2&&r.attempts.back().path==SYS);
     assert(s.loaded==std::vector<std::wstring>{SYS});}   // the proxy file is never loaded
    {FakeSys s=world();s.add(L"C:\\Game\\hardlink.dll",{7,OURS});auto r=load(s,{L"C:\\Game\\hardlink.dll"},SYS);
     assert(r.attempts[0].outcome==Outcome::SelfFile&&r.fallback&&r.module>=2);}
    {FakeSys s=world();s.add(LEG,{3,OURS});auto r=load(s,{LEG},SYS);   // another build of ours (copy/older version)
     assert(r.attempts[0].outcome==Outcome::OwnBuild&&r.fallback&&r.module>=2&&s.before==std::vector<std::wstring>{SYS});}
    {FakeSys s=world();s.add(LEG,{3,OLD_PROXY});auto r=load(s,{LEG},SYS);   // older proxy with another banner
     assert(r.attempts[0].outcome==Outcome::OwnBuild&&r.fallback&&r.module>=2&&s.before==std::vector<std::wstring>{SYS});}
    {FakeSys s=world();FakeSys::File f{3,RESHADE};f.loadsSelf=true;s.add(LEG,f);auto r=load(s,{LEG},SYS);   // loader returned this module
     assert(r.attempts[0].outcome==Outcome::SelfModule&&s.releases==1&&r.fallback&&r.module>=2);}
    {FakeSys s=world();FakeSys::File f{3,RESHADE};f.factoryInSelf=true;s.add(LEG,f);auto r=load(s,{LEG},SYS);   // export forwarded to "d3d9.Direct3DCreate9"
     assert(r.attempts[0].outcome==Outcome::FactoryInSelf&&s.releases==1&&r.fallback&&r.module>=2);}
    {FakeSys s=world();auto r=load(s,{LEG},SYS);   // missing configured backend: error, no silent fallback
     assert(!r.module&&!r.fallback&&r.attempts.size()==1&&r.attempts[0].outcome==Outcome::Missing&&r.attempts[0].error==2);}
    {FakeSys s=world();FakeSys::File f{3,RESHADE};f.factory=false;s.add(LEG,f);auto r=load(s,{LEG},SYS);
     assert(!r.module&&!r.fallback&&r.attempts[0].outcome==Outcome::NoFactory&&s.releases==1);}
    {FakeSys s=world();FakeSys::File f{3,DXVK110};f.loadFails=true;s.add(LEG,f);auto r=load(s,{LEG},SYS);
     assert(!r.module&&!r.fallback&&r.attempts[0].outcome==Outcome::LoadFailed&&r.attempts[0].error==126);}
    {FakeSys s=world();auto r=load(s,{L"<bad>"},SYS);assert(!r.module&&r.attempts[0].outcome==Outcome::LoadFailed);}
    // System runtime is itself this proxy: refuse, no second attempt, no loop.
    {FakeSys s=world();s.add(SYS,{7,OURS});auto r=load(s,{SYS},SYS);assert(!r.module&&!r.fallback&&r.attempts.size()==1&&s.loaded.empty());}
    {FakeSys s=world();s.add(SYS,{7,OURS});auto r=load(s,{GAME},SYS);
     assert(!r.module&&r.fallback&&r.attempts.size()==2&&r.attempts[1].outcome==Outcome::SelfFile&&s.loaded.empty());}
}
// A foreign proxy backend that resolved "d3d9.dll" by name calls our export again.
static int backendCalls=0,systemCalls=0,wraps=0;
static int ourExport(int depthLimit);
static int foreignBackend(int depthLimit){++backendCalls;return depthLimit?ourExport(depthLimit-1):1;}
static int systemRuntime(int){++systemCalls;return 2;}
static int ourExport(int depthLimit){
    ExportScope scope;
    int (*fn)(int)=ExportScope::reentered()?systemRuntime:foreignBackend;
    const int raw=fn(depthLimit);if(!ExportScope::reentered())++wraps;return raw;
}
static void reentry(){
    assert(ourExport(5)==2&&backendCalls==1&&systemCalls==1&&wraps==1&&ExportScope::depth()==0);
    assert(ourExport(0)==1&&backendCalls==2&&systemCalls==1&&wraps==2&&ExportScope::depth()==0);
}
static void hashing(){
    assert(Sha256().hex()=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    Sha256 abc;abc.update("abc",3);assert(abc.hex()=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    const std::string text="abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    Sha256 whole;whole.update(text.data(),text.size());Sha256 parts;for(char c:text)parts.update(&c,1);
    const std::string h=whole.hex();assert(h==parts.hex()&&h=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    Sha256 million;std::string a(1000,'a');for(int i=0;i<1000;++i)million.update(a.data(),a.size());
    assert(million.hex()=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}
int main(int argc,char** argv){
    inspection();loading();reentry();hashing();
    for(int i=1;i<argc;++i){   // real binaries, scanned read-only
        std::string arg=argv[i];const size_t colon=arg.find(':');const std::string expect=arg.substr(0,colon),path=arg.substr(colon+1);
        std::ifstream f(path,std::ios::binary);assert(f);std::vector<unsigned char> d((std::istreambuf_iterator<char>(f)),{});
        auto r=inspect(d.data(),d.size());
        if(expect=="ours")assert(r.ours&&!r.dxvk);
        else if(expect=="plain")assert(!r.ours&&!r.dxvk);
        else assert(!r.ours&&r.dxvk&&expect=="dxvk-"+r.dxvkVersion+(r.dxvkConfigEnv?"+env":""));
        std::printf("real %s: ours=%d dxvk=%d version=%s DXVK_CONFIG=%d\n",path.c_str(),r.ours,r.dxvk,r.dxvkVersion.c_str(),r.dxvkConfigEnv);
    }
    std::printf("PASS backend loader: DXVK/own-build scan (banner or PROXY format), self path/file/module/forwarded factory refusals -> system fallback once, missing = error, second d3d9.dll name refused, re-entry, SHA-256\n");
}
