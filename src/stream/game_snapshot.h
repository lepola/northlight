#pragma once
// 0.3.192 (CS): capture of the game-memory snapshot on the game thread, and the policy that decides where. The
// storage and the readSelf playback live in snapshot_store.h (included by world_context.h). Everything the replay
// thread's hooks later read from game memory at draw time is read here, in the same order and with the same
// double reads, by the raw readers themselves: a read function is injected into each, and in production it is
// readSelf, which records while a ScopedRecording is active. The static verify*/supported checks (they read client
// CODE, once per process) are deliberately not called here: code-range reads bypass the snapshot and stay live.
#include <cstdint>
#include "snapshot_store.h"
#include "world_camera.h"
#include "wmo_context.h"
#include "celestial_context.h"
#include "celestial_glare_native.h"
#include "shader_tags.h"

namespace NorthlightStream {
enum class Trigger:std::uint8_t{None=0,World=1,Sky=2,Ui=3,Present=4};
inline const char* triggerName(Trigger t){return t==Trigger::World?"world":t==Trigger::Sky?"sky":t==Trigger::Ui?"ui":t==Trigger::Present?"present":"none";}

// Per-frame decision of whether a draw takes a snapshot, called by the StreamDevice once per draw on the game thread
// with the draw's vertex-shader tags (shader_tags.h triggerTags), primitive count and whether the viewport covers the
// whole back buffer. Pure state: no memory reads, no allocation.
//   World: the first draw of the frame whose VS is terrain or WMO (the camera / view stack / sky block are valid
//          for the whole world pass and the light and camera hooks read them at its first draw).
//   Sky:   every draw with count<=4 and a full viewport that is not UI, up to MaxSky per frame: a sky disc or glare
//          quad is such a draw and each re-reads the celestial identities, so each gets a fresh snapshot.
//   Ui:    the first UI-tagged draw (renderEffects runs there).
//   Present: once per frame, from onPresent().
struct TriggerPolicy {
    static constexpr unsigned MaxSky=8,SkyMaxPrimitives=4;
    unsigned sky=0;bool world=false,ui=false;
    void beginFrame(){sky=0;world=false;ui=false;}
    Trigger onDraw(unsigned vsTags,unsigned primitiveCount,bool fullViewport){
        if(vsTags&unsigned(NorthlightShaderTags::kUi)){if(ui)return Trigger::None;ui=true;return Trigger::Ui;}
        if((vsTags&NorthlightShaderTags::kTriggerWorld)&&!world){world=true;return Trigger::World;}
        if(primitiveCount<=SkyMaxPrimitives&&fullViewport&&sky<MaxSky){++sky;return Trigger::Sky;}
        return Trigger::None;
    }
    Trigger onPresent(){beginFrame();return Trigger::Present;}
};

// The reader sequence, with the read function injected (readSelf in production; a fake memory in tests). The
// results are discarded: only the reads matter, and the readers stop at their first failure exactly as they do
// at replay, so a snapshot holds the same prefix the replay's own call will ask for.
template<class Read> inline void captureReaders(Read read){
    NorthlightWorldCamera::Camera camera;
    NorthlightWorldCamera::readCurrent(read,1.f,camera);       // main view: view stack
    NorthlightWorldCamera::readCurrentBasis(read,1.f,camera);  // sky phase: the camera's own basis
    NorthlightCelestial::Snapshot sky;
    NorthlightCelestial::readSnapshot(read,sky);               // 0xd38b00 sky block, size 0x388, twice
    unsigned char first[NorthlightWmoContext::SkyBytes],second[NorthlightWmoContext::SkyBytes];
    read(0xd38b00,first,sizeof first);read(0xd38b00,second,sizeof second); // wmo_context.h readGlobalLighting: size SkyBytes, twice
    NorthlightCelestialDisc::readIdentities(read);             // sun, moon, moon02 texture identities (each read twice)
    NorthlightCelestialGlare::readIdentities(read);            // sun and moon glare identities
}
#ifdef _WIN32
// Game thread: fills `s` (cleared first) with the game's current memory. Returns false when unusable (overflow);
// the trigger ordinal is the stream's draw counter at the triggering draw.
inline bool capture(GameSnapshot& s,Trigger trigger,std::uint64_t drawOrdinal){
    ScopedRecording recording(s);
    char map[64];float camera[3];
    NorthlightWorldContext::readMapAndCamera(map,camera); // the independent map/camera read the sky and WMO paths pair with readCurrent
    captureReaders(NorthlightWorldContext::readSelf);
    s.triggerKind=std::uint32_t(trigger);s.triggerDraw=drawOrdinal;
    SnapshotStats::triggers.fetch_add(1,std::memory_order_relaxed);
    return s.usable();
}
#endif
// Consumer side: draws elapsed between the snapshot's trigger and the draw now replaying, for the CSTREAM line.
inline void noteAge(const GameSnapshot& s,std::uint64_t currentDraw){
    SnapshotStats::ageDraws.fetch_add(currentDraw>=s.triggerDraw?currentDraw-s.triggerDraw:0,std::memory_order_relaxed);
    SnapshotStats::ageSamples.fetch_add(1,std::memory_order_relaxed);
}
} // namespace NorthlightStream
