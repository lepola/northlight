// Private-host CPU proof. Uses the official retracer object table; no COM calls.
#include "native_retrace_bridge.cpp"

const char* g_szD3D9DllName=nullptr;
HMODULE g_hD3D9Module=nullptr;
namespace retrace {int verbosity=-1;int debug=0;}
namespace d3dretrace {const retrace::Entry d3d9_callbacks[]={{nullptr,nullptr}};}

namespace {
using namespace NorthlightNativeRetraceBridge;
void check(bool value,const char* what){if(!value){std::fprintf(stderr,"ALIAS_FIXTURE failed=%s\n",what);std::exit(1);}}
void key(NorthlightStream::GameSnapshot& s,std::uintptr_t address,std::uint32_t value){check(s.record(address,&value,4,true),"record-key");}
void packet(NorthlightStream::GameSnapshot& s,std::uint32_t terminal,unsigned invalid=0){
    s.clear();
    for(unsigned body=0;body<3;++body)for(unsigned copy=0;copy<2;++copy){
        key(s,NorthlightCelestialDisc::BodyRecords[body]+0x10,body?0:invalid==1?1:0x10000);
        if(!body){unsigned char fields[64]{};const std::uint32_t gx=invalid==2?1:0x20000;std::memcpy(fields+0x24,&gx,4);
            if(invalid==3){fields[8]=4;const std::uint32_t variant=1;std::memcpy(fields+0x3c,&variant,4);}
            check(s.record(0x10020,fields,sizeof fields,true),"record-fields");key(s,0x20038,terminal);}
    }
    check(s.record(0xd38f50,nullptr,4,false),"record-known-failed-sun");
    check(s.record(0xd39000,nullptr,4,false),"record-known-failed-moon");
}
void collectors(){
    auto s=std::make_unique<NorthlightStream::GameSnapshot>();NativeIds ids;
    packet(*s,0x30000);check(nativeIdentities(*s,ids)&&ids.count==1&&ids.ids[0]==0x30000,"source-derived-terminal-dedup");
    packet(*s,0);check(nativeIdentities(*s,ids)&&ids.count==0,"explicit-zero-and-known-failed");
    packet(*s,1);check(!nativeIdentities(*s,ids),"invalid-terminal");
    for(unsigned invalid=1;invalid<=3;++invalid){packet(*s,0,invalid);check(!nativeIdentities(*s,ids),"invalid-chain-pointer");}
    s->clear();check(!nativeIdentities(*s,ids),"missing-key");
    ids={};for(unsigned i=0;i<MaxAliases;++i)ids.add(0x10000+i*4);
    check(ids.count==MaxAliases&&!ids.invalid,"fixed-capacity");ids.add(0x20000);check(ids.invalid,"alias-overflow");
}
void indexInputs(bool invalidBlob=false,bool wrongKind=false){
    static const char* args[]={"this","pIndexData"};
    static const trace::FunctionSig up{2,"IDirect3DDevice9::DrawIndexedPrimitiveUP",2,args};
    trace::Call copied(&up,0,0);copied.no=8;
    copied.args[0].value=new trace::Pointer(0x10000);copied.args[1].value=new trace::Blob(12);
    inputObjects(copied); // Source-owned index bytes have no COM identity.
    static const trace::FunctionSig set{3,"IDirect3DDevice9::SetIndices",2,args};
    trace::Call bound(&set,0,0);bound.no=9;
    bound.args[0].value=new trace::Pointer(0x10000);
    if(invalidBlob)bound.args[1].value=new trace::Blob(12);
    else{
        bound.args[1].value=new trace::Pointer(0x50000);
        int borrowed=0;retrace::addObj(bound,*bound.args[1].value,&borrowed);
        observeObject(bound,bound.args[1].value,wrongKind?Kind::Surface:Kind::IndexBuffer,0,true);
    }
    inputObjects(bound);
}
void recreation(const char* mode){
    static const trace::FunctionSig signature{4,"IDirect3DDevice9::CreateOffscreenPlainSurface",0,nullptr};
    trace::Call call(&signature,0,0);call.no=35788;
    trace::Pointer old(0x2577928),alias(0x257792c);int first=0,second=0;auto& objects=state().objects;
    retrace::addObj(call,old,&first);observeObject(call,&old,Kind::Surface,0,true);
    retrace::addObj(call,alias,&first);observeObject(call,&alias,Kind::Surface,0x2577928);
    call.no=35792;check(retireObject(objects.find(0x2577928)->second)==0x2577928,"original-final-release");
    check(!objects.find(0x2577928)->second.live&&!objects.find(0x257792c)->second.live,"release-kills-interface-family");
    retrace::delObj(old);retrace::delObj(alias);call.no=96960;
    retrace::addObj(call,old,&second);
    if(!std::strcmp(mode,"dead-getter"))observeObject(call,&old,Kind::Surface);
    observeObject(call,&old,Kind::Surface,0,true);
    const auto& fresh=objects.find(0x2577928)->second;
    check(fresh.live&&fresh.kind==Kind::Surface&&fresh.canonical==0x2577928&&fresh.replayed==&second&&!fresh.issued&&!fresh.queryType,"fresh-creation-lifetime");
    check(!objects.find(0x257792c)->second.live,"old-interface-stays-dead");
    if(!std::strcmp(mode,"stale-interface")){
        retrace::addObj(call,alias,&second);observeObject(call,&alias,Kind::Surface,0x2577928);
    }
    observeObject(call,&old,Kind::Surface); // A getter may observe this new live generation.
    call.no=96961;retireObject(objects.find(0x2577928)->second);retrace::delObj(old);
    static const trace::FunctionSig textureSignature{5,"IDirect3DDevice9::CreateTexture",0,nullptr};
    trace::Call textureCall(&textureSignature,0,0);textureCall.no=96962;
    retrace::addObj(textureCall,old,&first);observeObject(textureCall,&old,Kind::Texture,0,true);
    NativeIds ids;ids.add(0x2577928);std::uint64_t pairs[MaxAliases*2]{};aliasPairs(textureCall,ids,pairs);
    check(pairs[0]==0x2577928&&pairs[1]==reinterpret_cast<std::uintptr_t>(&first),"new-source-creation-proves-new-kind");
    check(!objects.find(0x257792c)->second.live,"previous-interface-never-revived");
}
}
int main(int argc,char** argv){
    using namespace NorthlightNativeRetraceBridge;
    const char* mode=argc==2?argv[1]:"positive";
    if(!std::strcmp(mode,"index-blob"))indexInputs(true);
    if(!std::strcmp(mode,"index-kind"))indexInputs(false,true);
    if(!std::strcmp(mode,"dead-getter")||!std::strcmp(mode,"stale-interface"))recreation(mode);
    static const trace::FunctionSig signature{1,"IDirect3DDevice9::SetTexture",0,nullptr};
    trace::Call call(&signature,0,0);call.no=7;NativeIds ids;ids.add(0x30000);
    auto& objects=state().objects;trace::Pointer old(0x30000);int borrowed=0,other=0;
    if(std::strcmp(mode,"missing")){
        retrace::addObj(call,old,&borrowed);observeObject(call,&old,!std::strcmp(mode,"wrong-kind")?Kind::Surface:Kind::Texture,0,true);
    }
    if(!std::strcmp(mode,"dead"))objects.find(0x30000)->second.live=false;
    if(!std::strcmp(mode,"canonical-dead"))objects.find(0x30000)->second.canonical=0x40000;
    if(!std::strcmp(mode,"mapping-changed"))retrace::addObj(call,old,&other);
    if(!std::strcmp(mode,"reuse"))observeObject(call,&old,Kind::Texture,0,true);
    std::uint64_t pairs[MaxAliases*2]{};aliasPairs(call,ids,pairs);
    check(!std::strcmp(mode,"positive"),"negative-was-not-rejected");
    check(pairs[0]==0x30000&&pairs[1]==reinterpret_cast<std::uintptr_t>(&borrowed),"actual-object-table-pair");
    trace::Pointer alias(0x30004);retrace::addObj(call,alias,&borrowed);observeObject(call,&alias,Kind::Texture,0x30000);
    ids={};ids.add(0x30004);aliasPairs(call,ids,pairs);
    check(pairs[0]==0x30004&&pairs[1]==reinterpret_cast<std::uintptr_t>(&borrowed),"canonical-interface-alias");
    collectors();indexInputs();recreation("positive");std::puts("NORTHLIGHT_NATIVE_ALIAS_FIXTURE PASS gpu_calls=0 authored_fixture=1 fps_eligible=0");
}
