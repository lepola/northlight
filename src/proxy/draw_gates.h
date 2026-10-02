#pragma once
// 0.3.187 per-frame draw gates: the Draw* hooks only record; whether the sky
// claim/observation, the blob shadow filter and the terrain shadow swap can
// act at all is decided once per frame. Portable: no device or Win32 calls.
//
// Each gate is a conjunction of conditions that never rise inside a frame:
// they can only change at the frame boundary (finishFrameImpl: F10 enabled,
// F9 shadows, F12 world debug, the retry's failed=false, setEffects), in Reset
// (failed=false; the gates are latched again there) or at device creation
// (pointers, ActorShadows). A fault or a failure inside the frame can only
// lower them. So every draw's original predicate implies its gate, and
// "gate && original" is the original: the gate only skips draws the original
// would have rejected. Everything that changes inside a frame (applied,
// terrain, the world context, composited shadows, the sky renderer's own
// state) stays in the original per-draw predicates.
namespace NorthlightDrawGates {
struct Inputs {
    bool sky=false,blobs=false,world=false;        /* the renderers exist (device lifetime) */
    bool enabled=false,failed=false;               /* F10 and the device-level failure */
    bool shadowsKey=false;                          /* F9 (effectKeys.settings.shadows) */
    bool actorShadows=false;                        /* ActorShadows (quality, device creation) */
    bool worldShadows=false;                        /* the world renderer's effects.shadows (setEffects) */
    bool debugOff=false;                            /* debugMode==0 && worldDebug==0 (F12) */
};
struct Frame {
    // Default: every gate open (the per-draw predicates alone decide, as before 0.3.187).
    bool sky=true,blob=true,terrainShadow=true;
};
// on=false (FrameDrawGates=0): every gate open, the 0.3.184 per-draw work.
inline Frame latch(bool on,const Inputs& i){
    Frame f;if(!on)return f;
    f.sky=i.sky&&i.enabled;                                                         /* native sky claim and observation */
    f.blob=i.blobs&&i.enabled&&!i.failed&&i.shadowsKey&&i.world&&i.actorShadows;  /* blobFilterActive() */
    f.terrainShadow=i.world&&i.enabled&&!i.failed&&i.debugOff&&i.worldShadows;    /* planTerrainShadowSwap() */
    return f;
}
}
