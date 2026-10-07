#pragma once
// 0.3.192 (CS): where the bytes of the write the replay thread is performing right now come from. The stream replays its own UnlockBuffer
// (queue payload or Block: ordinary cached memory) as Lock, memcpy, Unlock through the tracked wrapper, whose replay-side CPU copy
// (replay_copies.h) would otherwise be refreshed by reading the mapped DXVK pointer back, which can be write-combined memory with uncached,
// very slow CPU reads. The scope names the source for the range being written; the copy takes its bytes from there. Outside a scope (a pass-through
// lock the game writes through) the mapped pointer is read as before. thread_local: only the replay thread writes buffers.
#include <cstdint>
namespace NorthlightReplayCopies {
struct UnlockSource {const unsigned char* bytes=nullptr;std::uint32_t off=0,size=0;};
inline thread_local UnlockSource unlockSource;
class UnlockSourceScope {
    UnlockSource saved_;
public:
    UnlockSourceScope(const unsigned char* bytes,std::uint32_t off,std::uint32_t size):saved_(unlockSource){unlockSource={bytes,off,size};}
    ~UnlockSourceScope(){unlockSource=saved_;}
    UnlockSourceScope(const UnlockSourceScope&)=delete;UnlockSourceScope& operator=(const UnlockSourceScope&)=delete;
};
}
