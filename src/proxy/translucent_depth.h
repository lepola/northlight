#pragma once
// 0.3.188 TranslucentActorDepth: when the effects' depth resolve moves ahead of the first translucent
// Z-writing actor draw (stealth, ghost pets), so their depth stays out of depthTex. Portable: no device calls.
// mode 0 = never early; 1 = a skinned draw that writes Z and is alpha-blended or writes no RGB (depth-only
// prepass); 2 = the same for any world draw. Water draws never trigger. Inputs are read lazily through the
// callables: in mode 1 the skinned check first (a hash lookup that rejects terrain and WMO draws), then
// Z-write, then blend, then colour write.
namespace NorthlightTranslucentDepth {
template<class Z,class B,class C,class Skinned> inline bool shouldResolve(unsigned mode,bool water,Z&& zwrite,B&& blend,C&& colorWrite,Skinned&& skinned){
    if(!mode||water)return false;
    if(mode==1&&!skinned())return false;
    if(!zwrite())return false;
    return blend()||(colorWrite()&7)==0;
}
inline bool triggers(unsigned mode,bool skinned,bool water,unsigned long zwrite,unsigned long blend,unsigned long colorWrite){
    auto z=[&]{return zwrite;};auto b=[&]{return blend;};auto c=[&]{return colorWrite;};auto s=[&]{return skinned;};
    return shouldResolve(mode,water,z,b,c,s);
}
// One attempt per frame, success or not: a terrain draw after a success clears `captured` and the UI-time
// resolve redoes it; a failure leaves the frame to that UI-time resolve.
struct Frame {
    bool tried=false;unsigned resolves=0;
    bool attempt(){if(tried)return false;tried=true;return true;}
    void reset(){tried=false;resolves=0;}
};
}
