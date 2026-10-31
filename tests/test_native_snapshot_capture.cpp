#undef NDEBUG
#include "native_snapshot_capture.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <new>
#include <vector>

static thread_local bool forbidAllocation=false;
void* operator new(std::size_t size){assert(!forbidAllocation);if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
namespace fs=std::filesystem;
using C=NorthlightStream::NativeSnapshotCapture;
using S=NorthlightStream::SnapshotCaptureSink;
using NorthlightStream::GameSnapshot;
namespace A=NorthlightStream::SnapshotArchive;
namespace NorthlightStream {
struct SnapshotCaptureSinkTest {
    static bool open(S& s,const fs::path& path){return s.open(path,"receipt",{},false);}
    static void launch(S& s){s.writer_=std::thread([&s]{s.consume();});}
};
struct NativeSnapshotCaptureTest {
    static bool open(C& c,const fs::path& path,C::MarkerFn fn,void* context,S::Limits limits={}){return c.begin(path,"capture-oracle",fn,context,limits,false);}
    static void launch(C& c){SnapshotCaptureSinkTest::launch(c.sink_);}
    static S::Receipt receipt(const C& c){return c.pending_;}
};
}
using Probe=NorthlightStream::NativeSnapshotCaptureTest;
static auto snapshot(){
    auto s=std::make_unique<GameSnapshot>();unsigned a=42,b=84;
    assert(s->record(0x1000,&a,4,true)&&s->record(0x1000,&b,4,true)&&s->record(0x1004,nullptr,4,false));
    s->triggerKind=2;s->triggerDraw=123;return s;
}
static auto bytes(const fs::path& path){std::ifstream input(path,std::ios::binary);assert(input);return std::vector<unsigned char>{std::istreambuf_iterator<char>(input),{}};}
static auto text(const fs::path& path){const auto data=bytes(path);return std::string(data.begin(),data.end());}
struct MarkerContext {
    std::FILE* file=nullptr;unsigned calls=0;bool succeeds=true,parallel=false;
    C* nested=nullptr;const GameSnapshot* packet=nullptr;
    explicit MarkerContext(const fs::path& path){file=std::fopen(path.string().c_str(),"wb");assert(file);}
    ~MarkerContext(){assert(std::fclose(file)==0);}
};
static bool marker(void* opaque,const wchar_t* value,unsigned length)noexcept{
    assert(!forbidAllocation);auto& context=*static_cast<MarkerContext*>(opaque);++context.calls;
    assert(value&&length&&length<C::MarkerChars&&value[length]==0);
    if(!context.succeeds)return false;
    if(context.nested){
        const auto nested=[&]{C::completed(context.nested,*context.packet,0x123400,7,3,42);};
        if(context.parallel){std::thread other(nested);other.join();}else nested();
    }
    for(unsigned i=0;i<length;++i){assert(value[i]>0&&value[i]<128);assert(std::fputc(int(value[i]),context.file)!=EOF);}
    assert(std::fputc('\n',context.file)!=EOF);return true;
}
static void complete(C* capture,const GameSnapshot& packet,std::uint64_t frame=7,std::uint32_t thread=42){
    forbidAllocation=true;C::completed(capture,packet,0x123400,frame,3,thread);forbidAllocation=false;
}
static void observe(C* capture,const GameSnapshot& packet,std::uint64_t frame=7,std::uint32_t thread=42){C::observed(capture,packet,0x123400,frame,3,thread);}
static bool finish(C& capture){Probe::launch(capture);return capture.finish();}
int main(int argc,char** argv){
    assert(argc==2);const fs::path root=fs::absolute(argv[1]);assert(fs::create_directory(root));const auto packet=snapshot();unsigned groups=0;
    {auto capture=std::make_unique<C>();forbidAllocation=true;C::completed(nullptr,*packet,1,0,0,1);C::observed(nullptr,*packet,1,0,0,1);
     C::completed(capture.get(),*packet,1,0,0,1);C::observed(capture.get(),*packet,1,0,0,1);forbidAllocation=false;
     const auto status=capture->status();assert(!status.started&&!status.sink.accepted&&!status.observerFailures&&!status.pending&&status.diagnostic&&!status.fpsEligible&&!status.traceJoinValidated);++groups;}
    {auto sink=std::make_unique<S>();S::Receipt receipt;receipt.accepted=true;receipt.bytes=99;receipt.device=999;
     forbidAllocation=true;assert(!sink->submit(*packet,1,0,0,42,&receipt));forbidAllocation=false;
     assert(!receipt.accepted&&!receipt.bytes&&!receipt.device&&!receipt.crc32);
     assert(NorthlightStream::SnapshotCaptureSinkTest::open(*sink,root/"receipt"));
     forbidAllocation=true;assert(sink->submit(*packet,0x123400,7,3,42,&receipt));forbidAllocation=false;
     assert(receipt.accepted&&receipt.record==0&&receipt.device==0x123400&&receipt.frame==7&&receipt.epoch==3&&receipt.thread==42&&receipt.trigger==2&&receipt.draw==123);
     NorthlightStream::SnapshotCaptureSinkTest::launch(*sink);assert(sink->finish());const auto raw=bytes(root/"receipt/000000.nlsnap");
     assert(receipt.bytes==raw.size()&&receipt.crc32==A::u32(raw.data()+raw.size()-4)&&receipt.crc32==A::checksum(raw.data(),raw.size()-4));++groups;}
    {MarkerContext context(root/"roundtrip.markers.txt");auto capture=std::make_unique<C>();assert(Probe::open(*capture,root/"roundtrip",marker,&context));
     const auto before=fs::file_size(root/"roundtrip/capture.journal.jsonl");
     for(unsigned i=0;i<2;++i){complete(capture.get(),*packet,7+i);assert(context.calls==i);
         const auto receipt=Probe::receipt(*capture);assert(receipt.accepted&&receipt.record==i);
         assert(fs::file_size(root/"roundtrip/capture.journal.jsonl")==before&&!fs::exists(root/"roundtrip/000000.nlsnap")&&!fs::exists(root/"roundtrip/native-capture-status.json"));
         observe(capture.get(),*packet,7+i);assert(context.calls==i+1&&!capture->status().pending);}
     assert(finish(*capture));const auto status=capture->status();assert(status.complete&&status.marked==2&&status.sink.accepted==2&&status.sink.retired==2&&!status.sink.failures&&!status.observerFailures);
     assert(status.sink.budgetBytes+C::StatusBytes<=S::MaxSessionBytes&&capture->finish()&&!fs::exists(root/"roundtrip/native-capture-status.pending"));++groups;}
    {MarkerContext context(root/"unobserved.markers.txt");auto capture=std::make_unique<C>();assert(Probe::open(*capture,root/"unobserved",marker,&context));
     complete(capture.get(),*packet);complete(capture.get(),*packet);observe(capture.get(),*packet);
     assert(!finish(*capture));const auto status=capture->status();assert(status.pending&&status.sink.accepted==1&&!status.marked&&(status.observerFailures&C::ObserverOrder)&&!context.calls);++groups;}
    {MarkerContext context(root/"observe-first.markers.txt");auto capture=std::make_unique<C>();assert(Probe::open(*capture,root/"observe-first",marker,&context));
     observe(capture.get(),*packet);assert(!finish(*capture)&&!context.calls&&(capture->status().observerFailures&C::ObserverOrder));++groups;}
    for(unsigned changed=0;changed<6;++changed){
        const auto name="provenance-"+std::to_string(changed);MarkerContext context(root/(name+".markers.txt"));auto capture=std::make_unique<C>();assert(Probe::open(*capture,root/name,marker,&context));
        complete(capture.get(),*packet);const auto trigger=packet->triggerKind;const auto draw=packet->triggerDraw;
        if(changed==4)packet->triggerKind=1;if(changed==5)packet->triggerDraw=124;
        C::observed(capture.get(),*packet,changed==0?0x123401:0x123400,changed==1?8:7,changed==2?4:3,changed==3?43:42);
        packet->triggerKind=trigger;packet->triggerDraw=draw;
        assert(!finish(*capture)&&!context.calls&&(capture->status().observerFailures&C::Provenance));++groups;
    }
    {MarkerContext context(root/"marker-failure.markers.txt");context.succeeds=false;auto capture=std::make_unique<C>();assert(Probe::open(*capture,root/"marker-failure",marker,&context));
     complete(capture.get(),*packet);observe(capture.get(),*packet);assert(!finish(*capture));const auto status=capture->status();assert(context.calls==1&&!status.marked&&status.pending&&(status.observerFailures&C::Marker));++groups;}
    for(bool parallel:{false,true}){
        const char* name=parallel?"concurrent":"reentrant";MarkerContext context(root/(std::string(name)+".markers.txt"));auto capture=std::make_unique<C>();context.nested=capture.get();context.packet=packet.get();context.parallel=parallel;
        assert(Probe::open(*capture,root/name,marker,&context));complete(capture.get(),*packet);observe(capture.get(),*packet);
        assert(!finish(*capture));const auto status=capture->status();assert(status.sink.accepted==1&&status.marked==1&&!status.pending&&(status.observerFailures&C::Concurrent));++groups;
    }
    {MarkerContext context(root/"overflow.markers.txt");auto capture=std::make_unique<C>();assert(Probe::open(*capture,root/"overflow",marker,&context));
     for(unsigned i=0;i<S::Slots;++i){complete(capture.get(),*packet,i);observe(capture.get(),*packet,i);}
     complete(capture.get(),*packet,9);observe(capture.get(),*packet,9);assert(!finish(*capture));const auto status=capture->status();
     assert(status.sink.accepted==S::Slots&&status.marked==S::Slots&&status.sink.failures==S::Overflow&&(status.observerFailures&C::Sink));++groups;}
    {MarkerContext context(root/"budget.markers.txt");auto capture=std::make_unique<C>();assert(Probe::open(*capture,root/"budget",marker,&context,{1,S::MaxSessionBytes}));
     complete(capture.get(),*packet);observe(capture.get(),*packet);complete(capture.get(),*packet,8);observe(capture.get(),*packet,8);
     assert(!finish(*capture)&&capture->status().sink.failures==S::Budget&&capture->status().marked==1);++groups;}
    {MarkerContext context(root/"byte-budget.markers.txt");auto capture=std::make_unique<C>();assert(Probe::open(*capture,root/"byte-budget",marker,&context,{S::MaxRecords,1792}));
     complete(capture.get(),*packet);observe(capture.get(),*packet);assert(!finish(*capture));const auto status=capture->status();
     assert(status.sink.failures==S::Budget&&!status.sink.accepted&&!status.marked&&status.sink.budgetBytes+C::StatusBytes==1792&&!context.calls);++groups;}
    {MarkerContext context(root/"archive.markers.txt");auto capture=std::make_unique<C>();assert(Probe::open(*capture,root/"archive",marker,&context));auto empty=std::make_unique<GameSnapshot>();empty->triggerKind=2;
     complete(capture.get(),*empty);observe(capture.get(),*empty);assert(!finish(*capture)&&capture->status().sink.failures==S::Archive&&!context.calls);++groups;}
    {MarkerContext context(root/"producer.markers.txt");auto capture=std::make_unique<C>();assert(Probe::open(*capture,root/"producer",marker,&context));
     complete(capture.get(),*packet);observe(capture.get(),*packet);complete(capture.get(),*packet,8,43);observe(capture.get(),*packet,8,43);
     assert(!finish(*capture)&&capture->status().sink.failures==S::Producer&&capture->status().marked==1);++groups;}
    {MarkerContext context(root/"lifetime.markers.txt");{auto capture=std::make_unique<C>();assert(capture->open(root/"lifetime","capture-oracle",marker,&context));complete(capture.get(),*packet);observe(capture.get(),*packet);}
     assert(fs::exists(root/"lifetime/000000.nlsnap")&&text(root/"lifetime/native-capture-status.json").find("\"complete\":true")!=std::string::npos);++groups;}
    {MarkerContext context(root/"status-collision.markers.txt");auto capture=std::make_unique<C>();assert(Probe::open(*capture,root/"status-collision",marker,&context));
     const auto file=root/"status-collision/native-capture-status.json";std::ofstream(file)<<"immutable";complete(capture.get(),*packet);observe(capture.get(),*packet);
     assert(!finish(*capture)&&(capture->status().observerFailures&C::StatusIo)&&text(file)=="immutable");
     assert(text(root/"status-collision/native-capture-status.pending").find("\"complete\":true")!=std::string::npos);++groups;}
    {MarkerContext context(root/"pending-collision.markers.txt");auto capture=std::make_unique<C>();assert(Probe::open(*capture,root/"pending-collision",marker,&context));
     const auto file=root/"pending-collision/native-capture-status.pending";std::ofstream(file)<<"immutable";complete(capture.get(),*packet);observe(capture.get(),*packet);
     assert(!finish(*capture)&&(capture->status().observerFailures&C::StatusIo)&&text(file)=="immutable"&&!fs::exists(root/"pending-collision/native-capture-status.json"));++groups;}
    {MarkerContext context(root/"packet-collision.markers.txt");auto capture=std::make_unique<C>();assert(Probe::open(*capture,root/"packet-collision",marker,&context));
     const auto file=root/"packet-collision/000000.nlsnap";std::ofstream(file)<<"immutable";complete(capture.get(),*packet);observe(capture.get(),*packet);
     assert(!finish(*capture)&&(capture->status().sink.failures&S::Io)&&text(file)=="immutable");++groups;}
    {auto capture=std::make_unique<C>();assert(!capture->open(root/"invalid-session","quote;session",marker));assert(!fs::exists(root/"invalid-session")&&!capture->finish());
     auto missing=std::make_unique<C>();assert(!missing->open(root/"missing-marker","valid",nullptr)&&!fs::exists(root/"missing-marker"));
     const auto existing=root/"existing";assert(fs::create_directory(existing));std::ofstream(existing/"keep")<<"immutable";auto collision=std::make_unique<C>();assert(!collision->open(existing,"valid",marker));assert(!collision->finish()&&text(existing/"keep")=="immutable"&&!fs::exists(existing/"native-capture-status.json"));
     auto oversize=std::make_unique<C>();assert(!oversize->open(root/"oversize","valid",marker,nullptr,{S::MaxRecords,S::MaxSessionBytes+1})&&!fs::exists(root/"oversize"));++groups;}
    {S::Receipt receipt;receipt.accepted=true;receipt.record=S::MaxRecords-1;receipt.device=~std::uintptr_t(0);receipt.frame=~std::uint64_t(0);receipt.draw=receipt.frame;receipt.epoch=~std::uint32_t(0);receipt.thread=receipt.epoch;receipt.trigger=3;receipt.bytes=A::MaxBytes;receipt.crc32=0xffffffff;
     const std::string session(64,'z');wchar_t wide[C::MarkerChars];const auto size=C::formatMarker(session.c_str(),receipt,wide);assert(size&&size<C::MarkerChars&&wide[size]==0);
     std::ofstream output(root/"maximum.marker");for(unsigned i=0;i<size;++i){assert(wide[i]>0&&wide[i]<128);output.put(char(wide[i]));}output<<'\n';
     assert(!C::formatMarker("bad;session",receipt,wide)&&!wide[0]);receipt.bytes=0;assert(!C::formatMarker("valid",receipt,wide));++groups;}
    std::printf("NATIVE_SNAPSHOT_CAPTURE_PASS groups=%u producerAllocations=0 exactReceiptCRC=1 markerOutsideCompleted=1 traceJoinValidated=0\n",groups);
}
