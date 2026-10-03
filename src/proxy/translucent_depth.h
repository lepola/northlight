#pragma once
// 0.3.188 (task 3): the effects' depth resolve moves ahead of the first translucent Z-writing actor draw
// (stealth, ghost pets), so their depth stays out of depthTex. Portable: no device calls.
// Trigger: a skinned draw that writes Z and is translucent: alpha-blended other than ONE/ZERO (that pair
// writes the source unchanged, so it counts as opaque), or writing no RGB (the game's depth-only prepass of
// a faded model). Water draws never trigger. Non-skinned translucent Z draws (effects, doodads) do not
// either: the game draws them before its last opaque draws, which would then miss the depth.
// Undo: after a successful early resolve, a later world draw that writes Z and RGB effectively opaque (not
// translucent), or a water draw that writes Z, means depth the resolve missed: the caller clears `captured`
// so the UI-time resolve redoes it (terrain draws do the same in beforeDraw). Every undo re-arms the latch so
// the next translucent skinned draw resolves again, at most kMaxAttempts attempts per frame (an interleaved
// opaque/translucent pattern must not resolve dozens of times); past the cap the frame is left to the UI-time resolve.
// Freeze: a Clear(Z) of the world depth while the early capture holds ends that world pass; its depth is kept for
// the frame (no later undo or re-arm), since the UI-time resolve would only see the cleared buffer.
// Inputs are read lazily through the callables: skinned first (a hash lookup that rejects terrain and WMO
// draws), then Z-write, then colour write / blend; SRCBLEND and DESTBLEND only when blending is on.
namespace NorthlightTranslucentDepth {
constexpr unsigned long kBlendZero=1,kBlendOne=2; /* D3DBLEND_ZERO, D3DBLEND_ONE */
template<class B,class S,class D> inline bool opaqueBlend(B&& blend,S&& src,D&& dst){return !blend()||(src()==kBlendOne&&dst()==kBlendZero);}
// Non-water Z-writing draw that writes RGB effectively opaque (also the census' lastOpaqueZ rule).
template<class Z,class B,class S,class D,class C> inline bool opaqueZWrite(Z&& zwrite,B&& blend,S&& src,D&& dst,C&& colorWrite){
    return zwrite()&&(colorWrite()&7)!=0&&opaqueBlend(blend,src,dst);
}
template<class Z,class B,class S,class D,class C,class Skinned> inline bool shouldResolve(bool water,Z&& zwrite,B&& blend,S&& src,D&& dst,C&& colorWrite,Skinned&& skinned){
    if(water||!skinned())return false;
    if(!zwrite())return false;
    return !opaqueBlend(blend,src,dst)||(colorWrite()&7)==0;
}
template<class Z,class B,class S,class D,class C> inline bool isUndo(bool water,Z&& zwrite,B&& blend,S&& src,D&& dst,C&& colorWrite){
    if(!zwrite())return false;
    return water||((colorWrite()&7)!=0&&opaqueBlend(blend,src,dst));
}
// Per-frame latch: `tried` is set by an attempt (success or not) and cleared by an undo; attempts are capped.
// A failed resolve leaves the frame to the UI-time resolve. `earlyCaptured`: an early resolve succeeded and no undo since.
struct Frame {
    static constexpr unsigned kMaxAttempts=4;
    bool tried=false,earlyCaptured=false;unsigned attempts=0;
    bool armed()const{return !tried&&attempts<kMaxAttempts;}
    bool attempt(){if(!armed())return false;tried=true;++attempts;return true;}
    void success(){earlyCaptured=true;}
    bool undo(){if(!earlyCaptured)return false;earlyCaptured=false;tried=false;return true;} /* true: counts as an undone early resolve */
    void freeze(){earlyCaptured=false;tried=true;attempts=kMaxAttempts;}
    void reset(){tried=earlyCaptured=false;attempts=0;}
};
}
