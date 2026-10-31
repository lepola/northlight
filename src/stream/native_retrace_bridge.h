#pragma once
// Private apitrace host only. No renderer/device build includes this header.
#include <cstddef>
#include <cstdint>

namespace retrace { class Retracer; }

namespace NorthlightNativeRetraceBridge {
constexpr std::size_t HeaderBytes=256,RecordBytes=80;
constexpr std::uint32_t Version=1,AboveNorthlight=1,AliasesRequired=2,AuthoredTransport=4,CodeProofRequired=8;
constexpr unsigned MaxAliases=10;
constexpr std::uint32_t MaxRecords=20000,MaxPathBytes=4096,MaxSessionBytes=64;
constexpr std::uint64_t MaxPeBytes=64ull<<20,MaxPacketBytes=256ull<<20;
// LE header offsets. No native structs, padding, client pointers or byte swaps
// enter the file contract. Packet offsets are relative to the packet block.
namespace Header {
constexpr unsigned Total=16,Records=24,Flags=28,SessionBytes=32,DllPathBytes=36,
    TracePathBytes=40,PeBytes=44,PacketBytes=48,FactoryCall=56,CreateCall=64,
    Factory=72,Device=80,Draws=88,Presents=96,Resets=104,FinalRelease=112,
    TraceThread=120,OsThread=124,TraceHash=128,PeHash=160,DllHash=192,PayloadHash=224;
}
namespace Record {
constexpr unsigned TargetCall=0,MarkerCall=8,Device=16,Draw=24,Frame=32,
    Epoch=40,Trigger=44,TraceThread=48,OsThread=52,Bytes=56,Crc=60,
    Offset=64,Api=72,Reserved=76;
}
// sourceTrace is called at the actual parser filename selection, before open.
// NORTHLIGHT_RETRACE_PLAN absent: stock callbacks are forwarded unchanged.
bool sourceTrace(const char* path,bool filteredOrRepeated=false);
void install(retrace::Retracer& retracer);
void finish();
}
