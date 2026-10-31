#include "snapshot_archive.h"
#include "client_code_observation.h"
#include <windows.h>
#include <cassert>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using Open=int(__cdecl*)(const void*,unsigned);
using Snapshot=int(__cdecl*)(const void*,unsigned,std::uint64_t,unsigned);
using Status=int(__cdecl*)(std::uint64_t*,unsigned);
struct Api {Open open;Snapshot snapshot;Status status;};
template<class T> static T function(HMODULE module,const char* name){
    const FARPROC address=GetProcAddress(module,name);assert(address);
    static_assert(sizeof(T)==sizeof address,"Win32 function pointer representation");
    T result;std::memcpy(&result,&address,sizeof result);return result;
}
struct ThreadInput {Api api;const std::vector<unsigned char>* packet;};
static DWORD WINAPI otherThread(void* argument){
    const auto& input=*static_cast<ThreadInput*>(argument);std::uint64_t status[8]{};
    assert(!input.api.snapshot(input.packet->data(),unsigned(input.packet->size()),10,2));
    assert(!input.api.status(status,8));return 0;
}
int main(int argc,char** argv){
    assert(argc==4);
    const std::string mode=argv[3];
    const HMODULE module=LoadLibraryA(argv[1]);assert(module);
    const Api api{function<Open>(module,"NorthlightStandOpen"),function<Snapshot>(module,"NorthlightStandSnapshot"),
                  function<Status>(module,"NorthlightStandStatus")};
    const auto create=function<void*(WINAPI*)(unsigned)>(module,"Direct3DCreate9");
    const auto createEx=function<HRESULT(WINAPI*)(unsigned,void**)>(module,"Direct3DCreate9Ex");
    assert(api.open&&api.snapshot&&api.status&&create&&createEx);
    std::ifstream input(argv[2],std::ios::binary);assert(input);
    const std::vector<unsigned char> image{std::istreambuf_iterator<char>(input),{}};
    NorthlightStream::GameSnapshot snapshot;snapshot.triggerKind=2;snapshot.triggerDraw=10;
    const std::uint32_t value=0x12345678;assert(snapshot.record(0x18000000,&value,4,true));
    std::vector<unsigned char> packet(NorthlightStream::SnapshotArchive::MaxBytes);
    std::size_t bytes=0;NorthlightStream::SnapshotArchive::Error error;
    assert(NorthlightStream::SnapshotArchive::encode(snapshot,packet.data(),packet.size(),bytes,error));packet.resize(bytes);
    std::uint64_t status[8]{};
    assert(!api.status(status,8));assert(!api.snapshot(packet.data(),unsigned(bytes),10,2));
    if(mode=="before-open"){
        assert(!create(32));assert(!api.open(image.data(),unsigned(image.size())));
    }else{
        assert(api.open(image.data(),unsigned(image.size())));
        assert(!api.open(image.data(),unsigned(image.size())));
        assert(api.status(status,8)&&status[0]==1&&status[1]==1&&status[2]==0&&status[7]==0);
        assert(!api.status(status,7)&&!api.status(nullptr,8));
        if(mode.rfind("code-proof-",0)==0){
            // Authored observations of a data-only PE, not a loaded game capture.
            namespace Code=NorthlightStream::ClientCodeObservation;
            NorthlightStream::ClientImage::View view;NorthlightStream::ClientImage::Error imageError;
            assert(view.open(image.data(),image.size(),imageError));
            Code::Capture capture;assert(capture.begin(0));
            unsigned char observed[16];
            for(const std::uintptr_t address:{0x4f6650u,0x7bfd1cu}){
                assert(view.readStatic(address,observed,sizeof observed));
                capture.record(view,address,observed,sizeof observed,true);
            }
            std::vector<unsigned char> proof(Code::MaxEncodedBytes);std::size_t proofBytes=0;Code::Error proofError;
            assert(Code::encode(capture,proof.data(),proof.size(),proofBytes,proofError));proof.resize(proofBytes);
            const auto admit=function<Open>(module,"NorthlightStandCodeProof");
            if(mode=="code-proof-late-snapshot"){
                assert(api.snapshot(packet.data(),unsigned(bytes),10,2));
                assert(!admit(proof.data(),unsigned(proof.size())));
                assert(api.status(status,8)&&status[0]==1&&status[1]==0&&status[2]==1&&status[7]==0);
            }else{
                if(mode=="code-proof-corrupt")proof.back()^=1;
                const bool valid=mode!="code-proof-corrupt";
                assert(admit(proof.data(),unsigned(proof.size()))==int(valid));
                assert(api.status(status,8)&&status[0]==1&&status[1]==unsigned(valid)&&status[2]==0&&status[7]==0);
                if(mode=="code-proof-repeat"){
                    assert(!admit(proof.data(),unsigned(proof.size())));
                    assert(api.status(status,8)&&status[0]==1&&status[1]==0&&status[2]==0&&status[7]==0);
                }else assert(mode=="code-proof-valid"||mode=="code-proof-corrupt");
            }
            assert(status[3]==0&&status[4]==0&&status[5]==0&&status[6]==0);
        }else if(mode=="before-snapshot"){
            assert(!create(32));assert(api.status(status,8)&&status[2]==0&&status[7]==1);
        }else{
            if(mode=="wrong-thread"){
                ThreadInput context{api,&packet};const HANDLE thread=CreateThread(nullptr,0,otherThread,&context,0,nullptr);
                assert(thread&&WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CloseHandle(thread);
            }
            assert(api.snapshot(packet.data(),unsigned(bytes),10,2));
            assert(api.status(status,8)&&status[1]==1&&status[2]==1&&status[7]==0);
            if(mode=="corrupt"){
                packet.back()^=1;assert(!api.snapshot(packet.data(),unsigned(bytes),11,2));
                assert(api.status(status,8)&&status[1]==0&&status[2]==1);
                assert(!create(32));
            }else if(mode=="order"){
                assert(!api.snapshot(packet.data(),unsigned(bytes),10,2));
                assert(api.status(status,8)&&status[1]==0&&status[2]==1);assert(!create(32));
            }else{
                assert(mode=="valid"||mode=="wrong-thread");
                snapshot.triggerDraw=11;assert(NorthlightStream::SnapshotArchive::encode(snapshot,packet.data(),packet.size(),bytes,error));
                assert(api.snapshot(packet.data(),unsigned(bytes),11,2));
                assert(api.status(status,8)&&status[1]==1&&status[2]==2&&status[7]==0);
            }
        }
    }
    void* extended=reinterpret_cast<void*>(1);assert(FAILED(createEx(32,&extended))&&!extended);
    // No successful graphics factory/device call occurs in this host scenario.
    std::printf("PASS dedicated DLL C ABI %s; no graphics device or client execution\n",mode.c_str());
}
