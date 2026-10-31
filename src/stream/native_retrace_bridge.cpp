#include "native_retrace_bridge.h"
#include "client_image.h"
#include "client_code_observation.h"
#include "snapshot_archive.h"
#include "native_retrace_identity.h" // Derived from the exact pure runtime readers by the preparation script.
#include "d3dretrace.hpp"
#include "retrace_swizzle.hpp"
#include <windows.h>
#include <wincrypt.h>
#include <d3d9.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <new>

// Compiled only into the explicitly patched private host, never the renderer.
extern const char* g_szD3D9DllName;
extern HMODULE g_hD3D9Module;

namespace NorthlightNativeRetraceBridge {
namespace {
using NorthlightStream::SnapshotArchive::u32;
using NorthlightStream::SnapshotArchive::u64;
struct File {
    HANDLE h=INVALID_HANDLE_VALUE;
    ~File(){if(h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    bool open(const wchar_t* path){h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);return h!=INVALID_HANDLE_VALUE;}
    bool size(std::uint64_t& out){LARGE_INTEGER n;if(!GetFileSizeEx(h,&n)||n.QuadPart<0)return false;out=std::uint64_t(n.QuadPart);return true;}
};
bool rewind(File& f){LARGE_INTEGER zero{};return SetFilePointerEx(f.h,zero,nullptr,FILE_BEGIN)!=FALSE;}
bool readAll(File& f,unsigned char* out,std::size_t bytes){
    while(bytes){const DWORD n=DWORD(bytes>65536?65536:bytes);DWORD got=0;if(!ReadFile(f.h,out,n,&got,nullptr)||got!=n)return false;out+=got;bytes-=got;}return true;
}
bool sameFile(File& a,File& b){
    BY_HANDLE_FILE_INFORMATION x{},y{};return GetFileInformationByHandle(a.h,&x)&&GetFileInformationByHandle(b.h,&y)&&
        x.dwVolumeSerialNumber==y.dwVolumeSerialNumber&&x.nFileIndexHigh==y.nFileIndexHigh&&x.nFileIndexLow==y.nFileIndexLow;
}
class Hash {
    HCRYPTPROV provider_=0;HCRYPTHASH hash_=0;
public:
    Hash(){if(CryptAcquireContextW(&provider_,nullptr,nullptr,PROV_RSA_AES,CRYPT_VERIFYCONTEXT|CRYPT_SILENT))CryptCreateHash(provider_,CALG_SHA_256,0,0,&hash_);}
    ~Hash(){if(hash_)CryptDestroyHash(hash_);if(provider_)CryptReleaseContext(provider_,0);}
    bool add(const unsigned char* bytes,std::size_t size){return hash_&&size<=MAXDWORD&&CryptHashData(hash_,bytes,DWORD(size),0);}
    bool equals(const unsigned char* expected){unsigned char value[32];DWORD n=sizeof value;return hash_&&CryptGetHashParam(hash_,HP_HASHVAL,value,&n,0)&&n==sizeof value&&!std::memcmp(value,expected,sizeof value);}
};
bool hashBytes(const unsigned char* bytes,std::size_t size,const unsigned char* expected){Hash hash;return hash.add(bytes,size)&&hash.equals(expected);}
bool hashFile(File& file,const unsigned char* expected){
    if(!rewind(file))return false;Hash hash;unsigned char block[65536];DWORD got=0;
    do{if(!ReadFile(file.h,block,sizeof block,&got,nullptr)||!hash.add(block,got))return false;}while(got);
    return hash.equals(expected)&&rewind(file);
}
struct NativeIds {
    std::array<std::uint32_t,MaxAliases> ids{};unsigned count=0;
    bool invalid=false;
    void add(std::uint32_t value){
        if(!value)return;
        if(!NorthlightCelestialDisc::clientPointer(value,4)){invalid=true;return;}
        for(unsigned i=0;i<count;++i)if(ids[i]==value)return;
        if(count==ids.size()){invalid=true;return;}
        ids[count++]=value;
    }
};
bool nativeIdentities(NorthlightStream::GameSnapshot& snapshot,NativeIds& identities){
    bool missing=false;identities={};snapshot.rewind();
    struct Reads {
        NorthlightStream::GameSnapshot* snapshot;bool* missing;NativeIds* identities;
        bool operator()(std::uintptr_t address,void* output,std::size_t bytes)const{
            switch(snapshot->serveRecorded(address,output,bytes)){
            case NorthlightStream::GameSnapshot::Serve::Ok:return true;
            case NorthlightStream::GameSnapshot::Serve::Failed:return false;
            default:*missing=true;return false;}
        }
        void nativeIdentity(std::uint32_t value)const{identities->add(value);}
        void invalidPointer()const{identities->invalid=true;}
    } read{&snapshot,&missing,&identities};
    NorthlightCelestialDisc::readIdentities(read);
    NorthlightCelestialGlare::readIdentities(read);
    snapshot.rewind();return !missing&&!identities.invalid;
}
bool path(const unsigned char* bytes,unsigned n,wchar_t* output){
    if(!n||n>MaxPathBytes||std::memchr(bytes,0,n))return false;
    const int got=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,reinterpret_cast<const char*>(bytes),int(n),output,MaxPathBytes);
    if(got<3)return false;output[got]=0;
    return ((output[0]>=L'A'&&output[0]<=L'Z')||(output[0]>=L'a'&&output[0]<=L'z'))&&output[1]==L':'&&(output[2]==L'\\'||output[2]==L'/');
}

// Avoid RTTI and unchecked Value conversion/asserts on malformed transports.
struct Value : trace::Visitor {
    bool number=false;std::uint64_t integer=0;const wchar_t* wide=nullptr;
    void visit(trace::Null*) override {number=true;}
    void visit(trace::Bool* x) override {number=true;integer=x->value?1:0;}
    void visit(trace::SInt* x) override {number=true;integer=std::uint64_t(x->value);}
    void visit(trace::UInt* x) override {number=true;integer=x->value;}
    void visit(trace::Pointer* x) override {visit(static_cast<trace::UInt*>(x));}
    void visit(trace::Enum* x) override {visit(static_cast<trace::SInt*>(x));}
    void visit(trace::Bitmask* x) override {visit(static_cast<trace::UInt*>(x));}
    void visit(trace::WString* x) override {wide=x->value;}
    void visit(trace::Repr* x) override {if(x->machineValue)x->machineValue->visit(*this);}
    void visit(trace::Float*) override {}
    void visit(trace::Double*) override {}
    void visit(trace::String*) override {}
    void visit(trace::Struct*) override {}
    void visit(trace::Array*) override {}
    void visit(trace::Blob*) override {}
};
bool number(trace::Value* v,std::uint64_t& n){if(!v)return false;Value out;v->visit(out);n=out.integer;return out.number;}
trace::Value* arg(trace::Call& c,unsigned i){return i<c.args.size()?c.args[i].value:nullptr;}
int named(trace::Call& c,const char* name){for(unsigned i=0;i<c.args.size();++i)if(c.sig->arg_names&&c.sig->arg_names[i]&&!std::strcmp(c.sig->arg_names[i],name))return int(i);return -1;}
trace::Value* output(trace::Call& c,unsigned i){auto* v=arg(c,i);const auto* a=v?v->toArray():nullptr;return a&&a->values.size()==1?a->values[0]:nullptr;}

enum class Kind {Factory,Device,Swap,Texture,Cube,VolumeTexture,Surface,Volume,VertexBuffer,IndexBuffer,Declaration,VertexShader,PixelShader,StateBlock,Query};
struct Object {Kind kind;std::uint32_t canonical;bool live=true,issued=false;unsigned queryType=0;void* replayed=nullptr;};
struct Original {const char* name=nullptr;retrace::Callback callback=nullptr;};
struct State {
    bool active=false,selected=false,installed=false,created=false,released=false,complete=false,markerPending=false;
    bool factoryStarted=false,lastStatusValid=false;
    unsigned originalCount=0,next=0;DWORD owner=0;
    std::uint64_t draws=0,presents=0,resets=0,queryCalls=0,queryPolls=0,queryEarlyReady=0;
    std::uint64_t planBytes=0;unsigned peBytes=0,count=0;
    File planFile,traceFile,dllFile;
    std::unique_ptr<unsigned char[]> plan;
    std::unique_ptr<NorthlightStream::GameSnapshot> aliasSnapshot;
    const unsigned char* pe=nullptr;const unsigned char* records=nullptr;const unsigned char* packets=nullptr;
    const unsigned char* codeProof=nullptr;unsigned codeProofBytes=0;
    wchar_t dllPath[MaxPathBytes+1]{},tracePath[MaxPathBytes+1]{};
    char dllUtf8[MaxPathBytes+1]{},session[MaxSessionBytes+1]{};
    std::array<Original,1024> originals{};
    std::map<std::uint32_t,Object> objects;
    HMODULE module=nullptr;
    int (__cdecl* open)(const void*,unsigned)=nullptr;
    int (__cdecl* snapshot)(const void*,unsigned,std::uint64_t,unsigned)=nullptr;
    int (__cdecl* aliases)(const std::uint64_t*,unsigned,std::uint64_t,unsigned)=nullptr;
    int (__cdecl* admitCodeProof)(const void*,unsigned)=nullptr;
    int (__cdecl* status)(std::uint64_t*,unsigned)=nullptr;
    const unsigned char* record()const{return next<count?records+std::size_t(next)*RecordBytes:nullptr;}
    std::uint64_t h64(unsigned n)const{return u64(plan.get()+n);}
    std::uint32_t h32(unsigned n)const{return u32(plan.get()+n);}
};
State& state(){static State value;return value;}
[[noreturn]] void reject(const char* reason,const trace::Call* call=nullptr){
    std::fprintf(stderr,"NORTHLIGHT_NATIVE_BRIDGE refused reason=%s call=%u full_scene_ready=0 aliases_complete=0\n",reason,call?call->no:0u);std::fflush(stderr);std::exit(3);
}
bool refused(const char* reason){std::fprintf(stderr,"NORTHLIGHT_NATIVE_BRIDGE refused reason=%s full_scene_ready=0 aliases_complete=0\n",reason);return false;}
std::uint32_t scalar(trace::Call& c,unsigned i){std::uint64_t n=0;if(!number(arg(c,i),n)||n>UINT32_MAX)reject("argument-u32",&c);return std::uint32_t(n);}
std::uint32_t returned(trace::Call& c){std::uint64_t n=0;if(!number(c.ret,n)||(n>UINT32_MAX&&n<0xffffffff80000000ull))reject("return-u32",&c);return std::uint32_t(n);}
bool successful(trace::Call& c){return c.ret&&std::int32_t(returned(c))>=0;}
Object& object(trace::Call& c,unsigned i){
    const auto key=scalar(c,i);auto& s=state();auto it=s.objects.find(key);
    if(!key||it==s.objects.end()||!it->second.live)reject("object-lifetime",&c);return it->second;
}
bool kindMatches(const char* name,Kind kind){
    const auto* end=std::strstr(name,"::");if(!end)return false;const auto bytes=std::size_t(end-name);
    auto is=[&](const char* type){return std::strlen(type)==bytes&&!std::memcmp(name,type,bytes);};
    const bool texture=kind==Kind::Texture||kind==Kind::Cube||kind==Kind::VolumeTexture;
    if(is("IUnknown"))return true;
    if(is("IDirect3DResource9"))return texture||kind==Kind::Surface||kind==Kind::VertexBuffer||kind==Kind::IndexBuffer;
    if(is("IDirect3DBaseTexture9"))return texture;
    const struct {const char* name;Kind kind;} types[]={{"IDirect3D9",Kind::Factory},{"IDirect3DDevice9",Kind::Device},{"IDirect3DSwapChain9",Kind::Swap},{"IDirect3DTexture9",Kind::Texture},{"IDirect3DCubeTexture9",Kind::Cube},{"IDirect3DVolumeTexture9",Kind::VolumeTexture},{"IDirect3DSurface9",Kind::Surface},{"IDirect3DVolume9",Kind::Volume},{"IDirect3DVertexBuffer9",Kind::VertexBuffer},{"IDirect3DIndexBuffer9",Kind::IndexBuffer},{"IDirect3DVertexDeclaration9",Kind::Declaration},{"IDirect3DVertexShader9",Kind::VertexShader},{"IDirect3DPixelShader9",Kind::PixelShader},{"IDirect3DStateBlock9",Kind::StateBlock},{"IDirect3DQuery9",Kind::Query}};
    for(const auto& type:types)if(is(type.name))return type.kind==kind;return false;
}
void inputObjects(trace::Call& c){
    static const char* names[]={"pTexture","pSourceTexture","pDestinationTexture","pSourceSurface","pDestinationSurface","pDestSurface","pRenderTarget","pNewZStencil","pStreamData","pIndexData","pShader","pDecl","pSurface","pSrcSurface","pDstSurface"};
    for(const char* name:names){const int i=named(c,name);if(i<0)continue;
        // UP carries copied index bytes; only SetIndices takes a COM object.
        if(!std::strcmp(name,"pIndexData")&&!std::strcmp(c.name(),"IDirect3DDevice9::DrawIndexedPrimitiveUP"))continue;
        const auto key=scalar(c,unsigned(i));if(!key)continue;const auto& value=object(c,unsigned(i));
        if((std::strstr(name,"Texture")&&(value.kind!=Kind::Texture&&value.kind!=Kind::Cube&&value.kind!=Kind::VolumeTexture))||
           ((std::strstr(name,"Surface")||!std::strcmp(name,"pRenderTarget")||!std::strcmp(name,"pNewZStencil"))&&value.kind!=Kind::Surface)||
           (!std::strcmp(name,"pStreamData")&&value.kind!=Kind::VertexBuffer)||(!std::strcmp(name,"pIndexData")&&value.kind!=Kind::IndexBuffer)||
           (!std::strcmp(name,"pDecl")&&value.kind!=Kind::Declaration)||(!std::strcmp(name,"pShader")&&value.kind!=(std::strstr(c.name(),"Pixel")?Kind::PixelShader:Kind::VertexShader)))reject("input-object-kind",&c);
    }
}
void observeObject(trace::Call& c,trace::Value* value,Kind kind,std::uint32_t canonical=0,bool creation=false){
    std::uint64_t old=0;if(!number(value,old)||!old||old>UINT32_MAX)reject("object-result",&c);
    auto& s=state();const auto key=std::uint32_t(old);auto i=s.objects.find(key);
    void* replayed=retrace::toObjPointer(c,*value);if(!replayed)reject("missing-replayed-object",&c);
    if(i!=s.objects.end()){
        if(creation&&!i->second.live){
            if(canonical&&canonical!=key)reject("object-reuse-or-kind",&c);
            // A source Create after final Release starts a new lifetime. Only
            // this slot is replaced; the previous interface aliases stay dead.
            i->second=Object{kind,key,true,false,0,replayed};return;
        }
        if(creation||!i->second.live||i->second.kind!=kind||i->second.replayed!=replayed||(canonical&&i->second.canonical!=canonical))reject("object-reuse-or-kind",&c);
    }else{
        if(s.objects.size()>=65536)reject("object-budget",&c);
        s.objects.emplace(key,Object{kind,canonical?canonical:key,true,false,0,replayed});
    }
}
std::uint32_t retireObject(Object& receiver){
    const auto canonical=receiver.canonical;
    for(auto& entry:state().objects)if(entry.second.canonical==canonical)entry.second.live=false;
    return canonical;
}
bool texture(Kind kind){return kind==Kind::Texture||kind==Kind::Cube||kind==Kind::VolumeTexture;}
void aliasPairs(trace::Call& c,const NativeIds& identities,std::uint64_t* pairs){
    auto& s=state();
    for(unsigned i=0;i<identities.count;++i){
        const auto old=identities.ids[i];auto found=s.objects.find(old);
        if(found==s.objects.end())reject("alias-missing-object",&c);
        const auto& current=found->second;if(!current.live)reject("alias-dead-object",&c);
        if(!texture(current.kind))reject("alias-object-kind",&c);
        const auto owner=s.objects.find(current.canonical);
        if(owner==s.objects.end()||!owner->second.live||!texture(owner->second.kind)||owner->second.kind!=current.kind)reject("alias-canonical-lifetime",&c);
        trace::Pointer original(old);void* replayed=retrace::toObjPointer(c,original);
        if(!replayed||replayed!=current.replayed)reject("alias-replayed-generation",&c);
        pairs[2*i]=old;pairs[2*i+1]=reinterpret_cast<std::uintptr_t>(replayed);
    }
}
void checkStatus(trace::Call& c,std::uint64_t expectedSnapshots);
void attachAliases(trace::Call& c,const unsigned char* r){
    auto& s=state();NorthlightStream::SnapshotArchive::Error error;NativeIds identities;
    if(!NorthlightStream::SnapshotArchive::decode(s.packets+u64(r+Record::Offset),u32(r+Record::Bytes),*s.aliasSnapshot,error)||!nativeIdentities(*s.aliasSnapshot,identities))reject("alias-packet",&c);
    std::uint64_t pairs[MaxAliases*2]{};aliasPairs(c,identities,pairs);
    if(!s.aliases(identities.count?pairs:nullptr,identities.count,u64(r+Record::Draw),u32(r+Record::Trigger)))reject("alias-admission",&c);
    checkStatus(c,s.next+1);
}
void checkStatus(trace::Call& c,std::uint64_t expectedSnapshots){
    auto& s=state();std::uint64_t values[8]{};
    if(!s.status||!s.status(values,8)||values[0]!=1||values[1]!=1||values[2]!=expectedSnapshots||values[6])reject("stand-status",&c);
    s.lastStatusValid=true;
}
void bind(trace::Call& c,const unsigned char* r){
    auto& s=state();if(!s.snapshot(s.packets+u64(r+Record::Offset),u32(r+Record::Bytes),u64(r+Record::Draw),u32(r+Record::Trigger)))reject("snapshot-admission",&c);
    checkStatus(c,s.next+1);
}
template<class T> T procedure(HMODULE module,const char* name){const auto raw=GetProcAddress(module,name);T result=nullptr;static_assert(sizeof result==sizeof raw,"procedure width");std::memcpy(&result,&raw,sizeof result);return result;}
void factory(trace::Call& c){
    auto& s=state();if(s.factoryStarted||c.no!=s.h64(Header::FactoryCall)||returned(c)!=s.h64(Header::Factory))reject("factory-identity",&c);
    s.owner=GetCurrentThreadId();s.factoryStarted=true;
    if(g_hD3D9Module)reject("backend-already-loaded",&c);
    s.module=LoadLibraryExW(s.dllPath,nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);if(!s.module)reject("stand-load",&c);
    wchar_t actual[MaxPathBytes+1];const DWORD n=GetModuleFileNameW(s.module,actual,MaxPathBytes+1);File moduleFile;
    if(!n||n>MaxPathBytes||!moduleFile.open(actual)||!sameFile(moduleFile,s.dllFile))reject("loaded-stand-identity",&c);
    s.open=procedure<decltype(s.open)>(s.module,"NorthlightStandOpen");s.snapshot=procedure<decltype(s.snapshot)>(s.module,"NorthlightStandSnapshot");s.status=procedure<decltype(s.status)>(s.module,"NorthlightStandStatus");s.aliases=procedure<decltype(s.aliases)>(s.module,"NorthlightStandAliases");
    if(!s.open||!s.snapshot||!s.status||!s.aliases||!s.open(s.pe,s.peBytes))reject("stand-open",&c);
    if(s.h32(Header::Flags)&CodeProofRequired){
        s.admitCodeProof=procedure<decltype(s.admitCodeProof)>(s.module,"NorthlightStandCodeProof");
        if(!s.admitCodeProof||!s.admitCodeProof(s.codeProof,s.codeProofBytes))reject("stand-code-proof",&c);
    }
    bind(c,s.record());
    g_szD3D9DllName=s.dllUtf8;g_hD3D9Module=s.module;
}
unsigned drawApi(const char* name){
    if(!std::strcmp(name,"IDirect3DDevice9::DrawPrimitive"))return 1;
    if(!std::strcmp(name,"IDirect3DDevice9::DrawIndexedPrimitive"))return 2;
    if(!std::strcmp(name,"IDirect3DDevice9::DrawPrimitiveUP"))return 3;
    if(!std::strcmp(name,"IDirect3DDevice9::DrawIndexedPrimitiveUP"))return 4;return 0;
}
void marker(trace::Call& c){
    auto& s=state();if(!s.created||s.markerPending||c.args.size()!=2)reject("marker-order",&c);
    if(scalar(c,0)!=0x4e4c0001u)return;
    Value text;if(auto* a=arg(c,1))a->visit(text);if(!text.wide)reject("marker-string",&c);
    const auto* r=s.record();if(!r||c.no!=u64(r+Record::MarkerCall))reject("marker-call",&c);
    char expected[512];const int n=std::snprintf(expected,sizeof expected,
        "NLSNAP1;session=%s;record=%u;device=%llx;frame=%llu;epoch=%u;thread=%u;trigger=%u;draw=%llu;bytes=%u;crc=%08x",
        s.session,s.next,(unsigned long long)u64(r+Record::Device),(unsigned long long)u64(r+Record::Frame),u32(r+Record::Epoch),u32(r+Record::OsThread),u32(r+Record::Trigger),(unsigned long long)u64(r+Record::Draw),u32(r+Record::Bytes),u32(r+Record::Crc));
    if(n<0||unsigned(n)>=sizeof expected)reject("marker-capacity",&c);
    for(int i=0;i<=n;++i)if(text.wide[i]!=wchar_t(static_cast<unsigned char>(expected[i])))reject("marker-contents",&c);
    if(u64(r+Record::Draw)!=s.draws+1||u64(r+Record::Frame)!=s.presents||u32(r+Record::Epoch)!=s.resets)reject("marker-domain",&c);
    if(s.next)bind(c,r);else checkStatus(c,1); // First packet was admitted before the factory.
    attachAliases(c,r);
    s.markerPending=true;
}
void query(trace::Call& c){
    auto& s=state();auto& entry=object(c,0);
    if(entry.kind!=Kind::Query||!entry.issued||c.args.size()!=4)reject("query-lifetime",&c);
    const auto bytes=scalar(c,2),flags=scalar(c,3),original=returned(c);
    if((bytes!=0&&bytes!=4)||(flags&~unsigned(D3DGETDATA_FLUSH))||(original!=S_OK&&original!=S_FALSE))reject("query-domain",&c);
    auto* q=retrace::asObjPointer<IDirect3DQuery9>(c,c.arg(0));if(!q)reject("query-object",&c);
    const auto type=q->GetType();if((type!=D3DQUERYTYPE_EVENT&&type!=D3DQUERYTYPE_OCCLUSION)||unsigned(type)!=entry.queryType||q->GetDataSize()!=4)reject("query-type",&c);
    const auto* data=arg(c,1);const bool hasData=data&&!data->toNull();std::uint64_t expected=0;
    if(bytes==4&&(!hasData||!number(output(c,1),expected)||expected>UINT32_MAX))reject("query-output-shape",&c);
    if(bytes==0&&hasData)reject("query-zero-size-pointer",&c);
    DWORD actual=0;HRESULT hr;const auto started=std::chrono::steady_clock::now();++s.queryCalls;
    do{
        hr=q->GetData(hasData?&actual:nullptr,bytes,flags);++s.queryPolls;
        if(hr!=S_OK&&hr!=S_FALSE)reject("query-actual-failure",&c);
        if(original!=S_OK||hr!=S_FALSE)break;
        if(std::chrono::steady_clock::now()-started>std::chrono::seconds(5))reject("query-timeout",&c);
    }while(true);
    if(original==S_OK&&(hr!=S_OK||(bytes==4&&actual!=expected)))reject("query-output-mismatch",&c);
    if(original==S_FALSE&&hr==S_OK)++s.queryEarlyReady;
}
void outputs(trace::Call& c,Object* receiver){
    struct Result {const char* name;Kind kind;};
    static const Result names[]={{"ppTexture",Kind::Texture},{"ppVolumeTexture",Kind::VolumeTexture},{"ppCubeTexture",Kind::Cube},{"ppSurface",Kind::Surface},{"ppBackBuffer",Kind::Surface},{"ppRenderTarget",Kind::Surface},{"ppZStencilSurface",Kind::Surface},{"ppVolume",Kind::Volume},{"ppStreamData",Kind::VertexBuffer},{"ppVertexBuffer",Kind::VertexBuffer},{"ppIndexBuffer",Kind::IndexBuffer},{"ppIndexData",Kind::IndexBuffer},{"ppDecl",Kind::Declaration},{"ppShader",Kind::VertexShader},{"ppStateBlock",Kind::StateBlock},{"ppQuery",Kind::Query},{"pSwapChain",Kind::Swap}};
    if(!c.ret||!successful(c))return;
    const char* method=std::strstr(c.name(),"::");method=method?method+2:c.name();
    for(const auto& result:names){const int i=named(c,result.name);if(i<0)continue;
        auto* value=output(c,unsigned(i));std::uint64_t key=0;if(!number(value,key))reject("object-output-shape",&c);if(!key)continue;
        Kind kind=result.kind;if(!std::strcmp(result.name,"ppShader")&&std::strstr(method,"Pixel"))kind=Kind::PixelShader;
        // GetTexture may return any texture family, but its lifetime must have
        // been established by its original creation, never guessed at a getter.
        if(!std::strcmp(method,"GetTexture")){
            const auto it=state().objects.find(std::uint32_t(key));if(it==state().objects.end()||!it->second.live||(it->second.kind!=Kind::Texture&&it->second.kind!=Kind::Cube&&it->second.kind!=Kind::VolumeTexture))reject("texture-alias",&c);kind=it->second.kind;
        }
        const bool creation=!std::strncmp(method,"Create",6)||!std::strcmp(method,"EndStateBlock");
        observeObject(c,value,kind,0,creation);
        if(kind==Kind::Query&&!std::strcmp(method,"CreateQuery")){
            auto& q=state().objects.find(std::uint32_t(key))->second;q.queryType=scalar(c,1);
            if(q.queryType!=D3DQUERYTYPE_EVENT&&q.queryType!=D3DQUERYTYPE_OCCLUSION)reject("query-create-type",&c);
        }
    }
    const int device=named(c,"ppDevice");if(device>=0){std::uint64_t key=0;if(!number(output(c,unsigned(device)),key)||key!=state().h64(Header::Device))reject("device-alias",&c);observeObject(c,output(c,unsigned(device)),Kind::Device);}
    const int alias=named(c,"ppvObj");if(alias>=0&&receiver)observeObject(c,output(c,unsigned(alias)),receiver->kind,receiver->canonical);
    const int container=named(c,"ppContainer");if(container>=0){std::uint64_t key=0;if(!number(output(c,unsigned(container)),key)||key>UINT32_MAX)reject("container-result",&c);auto it=state().objects.find(std::uint32_t(key));if(it==state().objects.end()||!it->second.live)reject("unproved-container-alias",&c);}
}
void dispatch(trace::Call& c){
    auto& s=state();retrace::Callback original=nullptr;for(unsigned i=0;i<s.originalCount;++i)if(!std::strcmp(s.originals[i].name,c.name())){original=s.originals[i].callback;break;}
    if(!original)reject("callback-identity",&c);
    if(!s.active){original(c);return;}
    if(s.complete||c.thread_id!=s.h32(Header::TraceThread)||(s.owner&&s.owner!=GetCurrentThreadId())||(c.flags&trace::CALL_FLAG_INCOMPLETE)||std::strstr(c.name(),"9Ex"))reject("call-owner-or-domain",&c);
    const char* name=c.name();const bool first=!std::strcmp(name,"Direct3DCreate9");
    const bool create=!std::strcmp(name,"IDirect3D9::CreateDevice");const unsigned draw=drawApi(name);
    const bool present=!std::strcmp(name,"IDirect3DDevice9::Present")||!std::strcmp(name,"IDirect3DSwapChain9::Present");
    const bool reset=!std::strcmp(name,"IDirect3DDevice9::Reset");const bool getData=!std::strcmp(name,"IDirect3DQuery9::GetData");
    Object* receiver=nullptr;
    if(std::strstr(name,"::")){receiver=&object(c,0);if(!s.factoryStarted||!kindMatches(name,receiver->kind))reject("call-before-factory-or-kind",&c);inputObjects(c);}
    if(first)factory(c);
    if(create){if(s.created||c.no!=s.h64(Header::CreateCall)||scalar(c,0)!=s.h64(Header::Factory)||!successful(c))reject("device-create",&c);std::uint64_t key=0;if(!number(output(c,6),key)||key!=s.h64(Header::Device))reject("device-result",&c);}
    if(draw||present||reset){if(!s.created||s.released||!successful(c))reject("failed-or-unowned-work",&c);if(draw||reset||!std::strcmp(name,"IDirect3DDevice9::Present")){if(scalar(c,0)!=s.h64(Header::Device))reject("work-device",&c);}}
    if(draw){
        ++s.draws;const auto* r=s.record();
        if(r&&u64(r+Record::Draw)==s.draws){if(!s.markerPending||c.no!=u64(r+Record::TargetCall)||draw!=u32(r+Record::Api)||s.presents!=u64(r+Record::Frame)||s.resets!=u32(r+Record::Epoch))reject("target-draw",&c);}
        else if(s.markerPending||(r&&u64(r+Record::Draw)<s.draws))reject("missed-target",&c);
    }
    if(!std::strcmp(name,"D3DPERF_SetMarker"))marker(c);
    if(getData)query(c);else original(c);
    if(first)observeObject(c,c.ret,Kind::Factory,0,true);
    if(create){observeObject(c,output(c,6),Kind::Device,0,true);s.created=true;}
    if(draw){const auto* r=s.record();if(r&&u64(r+Record::Draw)==s.draws){s.markerPending=false;++s.next;}checkStatus(c,s.next?s.next:1);}
    if(present)++s.presents;
    if(reset){++s.resets;for(auto& entry:s.objects)entry.second.issued=false;}
    if(receiver&&!std::strcmp(name,"IDirect3DQuery9::Issue")){
        if(!successful(c)||receiver->kind!=Kind::Query)reject("query-issue",&c);const auto flags=scalar(c,1);
        if(flags==D3DISSUE_END)receiver->issued=true;else if(flags==D3DISSUE_BEGIN&&receiver->queryType==D3DQUERYTYPE_OCCLUSION)receiver->issued=false;else reject("query-issue-flags",&c);
    }
    outputs(c,receiver);
    const char* method=std::strstr(name,"::");
    if(receiver&&method&&!std::strcmp(method+2,"Release")&&returned(c)==0){
        const auto canonical=retireObject(*receiver);
        if(canonical==s.h64(Header::Device)){if(c.no!=s.h64(Header::FinalRelease))reject("device-release-call",&c);s.released=true;checkStatus(c,s.next);}
    }
}
}

bool sourceTrace(const char* source,bool filteredOrRepeated){
    auto& s=state();wchar_t planPath[32768];const DWORD n=GetEnvironmentVariableW(L"NORTHLIGHT_RETRACE_PLAN",planPath,32768);
    if(!n){if(s.selected)return refused("plan-disappeared");return true;}
    if(n>=32768||s.selected||filteredOrRepeated)return refused("plan-selection-or-filter");s.selected=true;
    if(!s.planFile.open(planPath)||!s.planFile.size(s.planBytes))return refused("plan-open");
    constexpr std::uint64_t maximum=HeaderBytes+MaxSessionBytes+2*MaxPathBytes+MaxPeBytes+MaxRecords*RecordBytes+MaxPacketBytes+NorthlightStream::ClientCodeObservation::MaxEncodedBytes;
    if(s.planBytes<HeaderBytes||s.planBytes>maximum)return refused("plan-size");
    s.plan.reset(new(std::nothrow) unsigned char[std::size_t(s.planBytes)]);if(!s.plan||!readAll(s.planFile,s.plan.get(),std::size_t(s.planBytes)))return refused("plan-read");
    const auto* h=s.plan.get();if(std::memcmp(h,"NLJOIN1\0",8)||u32(h+8)!=Version||u32(h+12)!=HeaderBytes||u64(h+Header::Total)!=s.planBytes)return refused("plan-header");
    const auto flags=u32(h+Header::Flags);
    if(!(flags&AboveNorthlight)||(flags&~(AboveNorthlight|AliasesRequired|AuthoredTransport|CodeProofRequired)))return refused("capture-layer-or-required-aliases");
    if(flags&AuthoredTransport){wchar_t allowed[3]{};if(GetEnvironmentVariableW(L"NORTHLIGHT_RETRACE_ALLOW_AUTHORED",allowed,3)!=1||allowed[0]!=L'1')return refused("authored-not-allowed");}
    else if(!(flags&CodeProofRequired))return refused("client-code-proof-required");
    s.count=u32(h+Header::Records);s.peBytes=u32(h+Header::PeBytes);const auto sessionBytes=u32(h+Header::SessionBytes),dllBytes=u32(h+Header::DllPathBytes),traceBytes=u32(h+Header::TracePathBytes);
    const auto packetBytes=u64(h+Header::PacketBytes);
    if(!s.count||s.count>MaxRecords||!sessionBytes||sessionBytes>MaxSessionBytes||!dllBytes||dllBytes>MaxPathBytes||!traceBytes||traceBytes>MaxPathBytes||s.peBytes<64||s.peBytes>MaxPeBytes||packetBytes>MaxPacketBytes||!s.h32(Header::OsThread))return refused("plan-capacity");
    const auto expected=HeaderBytes+std::uint64_t(sessionBytes)+dllBytes+traceBytes+s.peBytes+std::uint64_t(s.count)*RecordBytes+packetBytes;
    if(expected>s.planBytes||s.planBytes-expected>NorthlightStream::ClientCodeObservation::MaxEncodedBytes||
       bool(flags&CodeProofRequired)!=(expected<s.planBytes)||!hashBytes(h+HeaderBytes,std::size_t(s.planBytes)-HeaderBytes,h+Header::PayloadHash))return refused("plan-payload");
    for(unsigned offset:{Header::FactoryCall,Header::CreateCall,Header::Factory,Header::Device,Header::FinalRelease})if(s.h64(offset)>UINT32_MAX)return refused("plan-u32-identity");
    if(!s.h64(Header::Factory)||!s.h64(Header::Device)||s.h64(Header::Factory)==s.h64(Header::Device)||s.h64(Header::FactoryCall)>=s.h64(Header::CreateCall)||s.h64(Header::CreateCall)>=s.h64(Header::FinalRelease)||!s.h64(Header::Draws)||s.h64(Header::Resets)>UINT32_MAX)return refused("plan-identity-order");
    const unsigned char* p=h+HeaderBytes;
    for(unsigned i=0;i<sessionBytes;++i)if(!((p[i]>='a'&&p[i]<='z')||(p[i]>='A'&&p[i]<='Z')||(p[i]>='0'&&p[i]<='9')||p[i]=='-'))return refused("plan-session");
    std::memcpy(s.session,p,sessionBytes);p+=sessionBytes;
    if(!path(p,dllBytes,s.dllPath))return refused("plan-dll-path");std::memcpy(s.dllUtf8,p,dllBytes);p+=dllBytes;
    if(!path(p,traceBytes,s.tracePath))return refused("plan-trace-path");p+=traceBytes;s.pe=p;p+=s.peBytes;s.records=p;s.packets=p+std::size_t(s.count)*RecordBytes;
    NorthlightStream::ClientImage::View image;NorthlightStream::ClientImage::Error imageError;
    if(!hashBytes(s.pe,s.peBytes,h+Header::PeHash)||!image.open(s.pe,s.peBytes,imageError))return refused("plan-pe");
    if(flags&CodeProofRequired){
        s.codeProof=h+expected;s.codeProofBytes=unsigned(s.planBytes-expected);
        std::unique_ptr<NorthlightStream::ClientCodeObservation::Proof> proof(new(std::nothrow) NorthlightStream::ClientCodeObservation::Proof);NorthlightStream::ClientCodeObservation::Error error;
        if(!proof||!NorthlightStream::ClientCodeObservation::decode(s.codeProof,s.codeProofBytes,image,*proof,error))return refused("client-code-proof");
    }
    wchar_t actualPath[MaxPathBytes+1];File selected;
    if(!source||!path(reinterpret_cast<const unsigned char*>(source),unsigned(std::strlen(source)),actualPath)||!selected.open(actualPath)||!s.traceFile.open(s.tracePath)||!sameFile(selected,s.traceFile)||!hashFile(s.traceFile,h+Header::TraceHash))return refused("selected-trace");
    if(!s.dllFile.open(s.dllPath)||!hashFile(s.dllFile,h+Header::DllHash))return refused("stand-dll-hash");
    s.aliasSnapshot.reset(new(std::nothrow) NorthlightStream::GameSnapshot);if(!s.aliasSnapshot)return refused("packet-scratch");
    auto& scratch=s.aliasSnapshot;bool aliasesRequired=false;
    std::uint64_t offset=0,lastDraw=0,lastTarget=0;
    for(unsigned i=0;i<s.count;++i){const auto* r=s.records+std::size_t(i)*RecordBytes;const auto bytes=u32(r+Record::Bytes);const auto target=u64(r+Record::TargetCall),markerCall=u64(r+Record::MarkerCall),draw=u64(r+Record::Draw);
        if(target>UINT32_MAX||markerCall>UINT32_MAX||markerCall<=target||target<=s.h64(Header::CreateCall)||(i&&target<=lastTarget)||draw<=lastDraw||(!i&&draw!=1)||draw>s.h64(Header::Draws)||target>=s.h64(Header::FinalRelease)||u64(r+Record::Device)!=s.h64(Header::Device)||u32(r+Record::TraceThread)!=s.h32(Header::TraceThread)||u32(r+Record::OsThread)!=s.h32(Header::OsThread)||u32(r+Record::Epoch)>s.h64(Header::Resets)||u64(r+Record::Frame)>s.h64(Header::Presents)||u32(r+Record::Trigger)<1||u32(r+Record::Trigger)>3||u32(r+Record::Api)<1||u32(r+Record::Api)>4||u32(r+Record::Reserved)||u64(r+Record::Offset)!=offset||bytes>packetBytes-offset||bytes<44)return refused("packet-record");
        NorthlightStream::SnapshotArchive::Error error;
        if(!NorthlightStream::SnapshotArchive::decode(s.packets+offset,bytes,*scratch,error)||scratch->triggerDraw!=draw||scratch->triggerKind!=u32(r+Record::Trigger)||u32(s.packets+offset+bytes-4)!=u32(r+Record::Crc))return refused("packet-archive");
        NativeIds identities;if(!nativeIdentities(*scratch,identities))return refused("packet-typed-identity");
        aliasesRequired=aliasesRequired||identities.count!=0;
        offset+=bytes;lastDraw=draw;lastTarget=target;
    }
    if(offset!=packetBytes)return refused("packet-tail");
    if(bool(flags&AliasesRequired)!=aliasesRequired)return refused("plan-alias-flag");
    s.active=true;std::fprintf(stderr,"NORTHLIGHT_NATIVE_BRIDGE selected records=%u aliases_required=%u authored_transport=%u fps_eligible=0 aliases_complete=0 full_scene_ready=0\n",s.count,unsigned(aliasesRequired),unsigned(bool(flags&AuthoredTransport)));return true;
}
void install(retrace::Retracer& retracer){
    auto& s=state();if(!s.active)return;if(s.installed)reject("callback-reinstall");s.installed=true;
    for(const auto* entry=d3dretrace::d3d9_callbacks;entry->name;++entry){
        if(s.originalCount>=s.originals.size())reject("callback-budget");s.originals[s.originalCount++]={entry->name,entry->callback};
        const retrace::Entry bridge[]={{entry->name,&dispatch},{nullptr,nullptr}};retracer.addCallbacks(bridge);
    }
}
void finish(){
    auto& s=state();if(!s.active)return;
    if(s.complete||!s.factoryStarted||!s.created||!s.released||!s.lastStatusValid||s.markerPending||s.next!=s.count||s.draws!=s.h64(Header::Draws)||s.presents!=s.h64(Header::Presents)||s.resets!=s.h64(Header::Resets))reject("incomplete-transport");
    if(!hashFile(s.traceFile,s.plan.get()+Header::TraceHash)||!hashFile(s.dllFile,s.plan.get()+Header::DllHash))reject("input-drift");
    s.complete=true;std::fprintf(stderr,"NORTHLIGHT_NATIVE_BRIDGE PASS records=%u draws=%llu presents=%llu resets=%llu queryCalls=%llu queryPolls=%llu queryEarlyReady=%llu authored_transport=%u fps_eligible=0 aliases_complete=0 full_scene_ready=0\n",s.next,(unsigned long long)s.draws,(unsigned long long)s.presents,(unsigned long long)s.resets,(unsigned long long)s.queryCalls,(unsigned long long)s.queryPolls,(unsigned long long)s.queryEarlyReady,unsigned(bool(s.h32(Header::Flags)&AuthoredTransport)));
}
}
