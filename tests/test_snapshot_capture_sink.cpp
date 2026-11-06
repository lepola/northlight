#undef NDEBUG
#include "snapshot_capture_sink.h"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <new>
#include <vector>

static thread_local bool forbidAllocation=false;
void* operator new(std::size_t n){assert(!forbidAllocation);if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
namespace fs=std::filesystem;
using S=NorthlightStream::SnapshotCaptureSink;
using NorthlightStream::GameSnapshot;
namespace A=NorthlightStream::SnapshotArchive;
namespace NorthlightStream {
struct SnapshotCaptureSinkTest {
    static bool open(S& s,const fs::path& p,S::Limits limits={}){return s.open(p,"final-oracle",limits,false);}
    static void launch(S& s){s.writer_=std::thread([&s]{s.consume();});}
};
}
using Probe=NorthlightStream::SnapshotCaptureSinkTest;
static auto snapshot(){
    auto s=std::make_unique<GameSnapshot>();std::uint32_t a=42,b=84;
    assert(s->record(0x1000,&a,4,true)&&s->record(0x1000,&b,4,true)&&s->record(0x1004,nullptr,4,false));
    s->triggerKind=2;s->triggerDraw=123;return s;
}
static auto bytes(const fs::path& p){std::ifstream f(p,std::ios::binary);assert(f);return std::vector<unsigned char>{std::istreambuf_iterator<char>(f),{}};}
static void decode(const fs::path& p){
    const auto b=bytes(p);auto s=std::make_unique<GameSnapshot>();A::Error e;assert(A::decode(b.data(),b.size(),*s,e));
    assert(s->triggerKind==2&&s->triggerDraw==123);A::Reader reader(*s);unsigned v=0;
    assert(reader(0x1000,&v,4)&&v==42&&reader(0x1000,&v,4)&&v==84);
    assert(!reader(0x1004,&v,4)&&!reader(0x1000,&v,4)&&!reader(0x9999,&v,4));
}
int main(int argc,char** argv){
    assert(argc==2);const fs::path root=fs::absolute(argv[1]);assert(fs::create_directory(root));unsigned groups=0;
    const auto s=snapshot();const auto originalCopies=s->copies();
    {auto sink=std::make_unique<S>();assert(Probe::open(*sink,root/"roundtrip"));
     const auto before=fs::file_size(root/"roundtrip/capture.journal.jsonl");
     for(unsigned i=0;i<3;++i){forbidAllocation=true;const bool ok=sink->submit(*s,0x123400,7,3,42);forbidAllocation=false;assert(ok);}
     assert(sink->status().accepted==3&&sink->status().retired==0);
     assert(fs::file_size(root/"roundtrip/capture.journal.jsonl")==before&&!fs::exists(root/"roundtrip/000000.nlsnap"));
     Probe::launch(*sink);assert(sink->finish());const auto status=sink->status();
     assert(status.joined&&!status.traceJoinValidated&&status.accepted==3&&status.retired==3&&!status.failures);
     for(unsigned i=0;i<3;++i){char name[32];std::snprintf(name,sizeof name,"%06u.nlsnap",i);decode(root/"roundtrip"/name);}++groups;}
    {auto sink=std::make_unique<S>();assert(Probe::open(*sink,root/"overflow"));
     for(unsigned i=0;i<S::Slots;++i)assert(sink->submit(*s,1,0,0,42));
     assert(!sink->submit(*s,1,0,0,42)&&!sink->submit(*s,1,0,0,42));
     assert(sink->status().accepted==S::Slots&&sink->status().failures==S::Overflow);
     assert(!fs::exists(root/"overflow/000000.nlsnap"));Probe::launch(*sink);assert(!sink->finish());
     assert(sink->status().retired==S::Slots&&sink->status().failures==S::Overflow);++groups;}
    {auto sink=std::make_unique<S>();assert(Probe::open(*sink,root/"records",{1,S::MaxSessionBytes}));
     assert(sink->submit(*s,1,0,0,42)&&!sink->submit(*s,1,0,0,42));Probe::launch(*sink);assert(!sink->finish());assert(sink->status().failures==S::Budget);
     auto limited=std::make_unique<S>();assert(Probe::open(*limited,root/"bytes",{S::MaxRecords,1024}));
     assert(!limited->submit(*s,1,0,0,42)&&limited->status().accepted==0);Probe::launch(*limited);assert(!limited->finish());assert(limited->status().failures==S::Budget);++groups;}
    {const auto p=root/"exists";assert(fs::create_directory(p));std::ofstream(p/"keep")<<"sentinel";
     auto sink=std::make_unique<S>();assert(!sink->start(p,"valid")&&!sink->start(root/"retry","valid"));
     assert(!fs::exists(p/"capture.journal.jsonl")&&bytes(p/"keep")==std::vector<unsigned char>({'s','e','n','t','i','n','e','l'}));
     auto collision=std::make_unique<S>();assert(Probe::open(*collision,root/"collision"));
     const auto file=root/"collision/000000.nlsnap";std::ofstream(file)<<"immutable";const auto before=bytes(file);
     assert(collision->submit(*s,1,0,0,42));Probe::launch(*collision);assert(!collision->finish());
     assert(collision->status().failures&S::Io);assert(bytes(file)==before);++groups;}
    {auto sink=std::make_unique<S>();assert(Probe::open(*sink,root/"producer"));
     S::completed(nullptr,*s,1,0,0,42);S::completed(sink.get(),*s,1,0,0,42);
     assert(!sink->submit(*s,1,0,0,43));Probe::launch(*sink);assert(!sink->finish());assert(sink->status().failures==S::Producer);
     auto invalid=std::make_unique<S>();assert(!invalid->start(root/"invalid","quote\"session"));
     assert(!fs::exists(root/"invalid"));++groups;}
    {auto sink=std::make_unique<S>();assert(sink->start(root/"stress","stress"));
     for(unsigned i=0;i<128;++i){const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
         for(;;){const auto st=sink->status();assert(!st.failures);if(st.accepted-st.retired<S::Slots)break;
             assert(std::chrono::steady_clock::now()<deadline);std::this_thread::yield();}
         forbidAllocation=true;const bool ok=sink->submit(*s,1,i,0,42);forbidAllocation=false;assert(ok);}
     assert(sink->finish()&&sink->status().retired==128);decode(root/"stress/000127.nlsnap");++groups;}
    {const auto p=root/"lifetime";{auto sink=std::make_unique<S>();assert(sink->start(p,"lifetime"));assert(sink->submit(*s,1,0,0,42));}
     decode(p/"000000.nlsnap");assert(s->copies()==originalCopies&&s->mode==GameSnapshot::Mode::Idle);++groups;}
    std::printf("SNAPSHOT_CAPTURE_SINK_PASS groups=%u slots=8 producerAllocations=0 journalIsNotTraceMapping=1\n",groups);
}
