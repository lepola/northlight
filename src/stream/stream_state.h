#pragma once
// 0.3.192 (CS): StreamState, the game thread's authoritative shadow of all settable device state at the game's stream
// position, keyed by stream-proxy identity (it works like Wine CSMT and does not reuse the mirror's trust learning: that
// learning compares pointers, and a sync Get returns Device-level pointers). A slot is known once the game set it (or the
// defaults batch read it) and stays known until Reset, a state-block Apply, a replay-side failure or an audit mismatch.
// Known slots answer Get locally; unknown or sync-only slots fall back to a sync call. Game thread only, except
// loadDefaults(), which the replay thread runs while the game thread waits for it.
#include <cstdint>
#include <cstring>
#include <vector>
#include "stream_proxies.h"

namespace NorthlightStream {
// known: the stream can answer a Get. fromSet: the value is what the game last SET (not just read back from the Target: the defaults batch
// and sync Gets learn without it), the only values a repeated Set may be filtered against.
template<class T> struct Slot {T v{};bool known=false,fromSet=false;void set(const T& x){v=x;known=true;fromSet=true;}void learn(const T& x){v=x;known=true;fromSet=false;}};

struct StreamState {
    static constexpr unsigned kRS=256,kSamplers=21,kSampTypes=16,kTSStages=8,kTSTypes=33,kXforms=512,kStreams=16,kClip=32,kRTs=4;
    // sync-only bit index of an audited scalar slot (render state, sampler state, texture-stage state)
    static unsigned bitRS(unsigned s){return s;}
    static unsigned bitSamp(unsigned idx,unsigned t){return kRS+idx*kSampTypes+t;}
    static unsigned bitTss(unsigned stage,unsigned t){return kRS+kSamplers*kSampTypes+stage*kTSTypes+t;}
    static constexpr unsigned kBits=kRS+kSamplers*kSampTypes+kTSStages*kTSTypes;
    static_assert(kBits<=14*64,"grow StreamCore::syncOnly");
    // D3D9 sampler numbers 0..15, then the displacement/vertex samplers 256..260.
    static bool sampIndex(DWORD s,unsigned& idx){if(s<16){idx=s;return true;}if(s>=256&&s<=260){idx=16+(s-256);return true;}return false;}

    StreamCore* core=nullptr;
    Slot<DWORD> rs[kRS];
    Slot<DWORD> samp[kSamplers][kSampTypes];
    Slot<DWORD> tss[kTSStages][kTSTypes];
    Slot<D3DMATRIX> xf[kXforms];
    Slot<D3DVIEWPORT9> viewport;Slot<RECT> scissor;Slot<D3DMATERIAL9> material;
    struct Light {Slot<D3DLIGHT9> l;Slot<BOOL> enabled;};
    std::vector<Light> lights;
    struct Plane {bool known=false;float p[4]={};};
    Plane clip[kClip];
    struct VsF {bool known=false;float v[4]={};};struct VsI {bool known=false;int v[4]={};};
    VsF vsF[256],psF[256];VsI vsI[16],psI[16];Slot<BOOL> vsB[16],psB[16];
    struct Stream {ProxyBase* vb=nullptr;UINT offset=0,stride=0;bool known=false,fromSet=false;Slot<UINT> freq;};
    Stream streams[kStreams];
    bool indicesKnown=false,declKnown=false,vsKnown=false,psKnown=false,rtKnown[kRTs]={},dsKnown=false,texKnown[kSamplers]={};
    bool indicesSet=false,declSet=false,vsSet=false,psSet=false,texSet[kSamplers]={};   // fromSet of the bound-object slots
    ProxyBase *indices=nullptr,*decl=nullptr,*vs=nullptr,*ps=nullptr,*rt[kRTs]={},*ds=nullptr,*tex[kSamplers]={};
    Slot<DWORD> fvf;Slot<float> npatch;Slot<BOOL> swvp;Slot<UINT> palette;

    explicit StreamState(StreamCore* c=nullptr):core(c){}
    StreamState(const StreamState&)=delete;StreamState& operator=(const StreamState&)=delete;
    ~StreamState(){clear();}

    // Binds np into slot (np may be null): the proxy stays alive while bound. Game thread.
    static void bind(ProxyBase*& slot,ProxyBase* np){if(slot==np)return;if(np)np->bindAdd();if(slot)slot->bindRelease();slot=np;}   // the same proxy again changes no count
    void setLight(DWORD i,const D3DLIGHT9& l){if(i<kMaxLights){grow(i);lights[i].l.set(l);}}
    void enableLight(DWORD i,BOOL e){if(i<kMaxLights){grow(i);lights[i].enabled.set(e);}}
    static constexpr DWORD kMaxLights=1024;
    void grow(DWORD i){if(lights.size()<=i)lights.resize(i+1);}

    // Everything unknown again, every bind released (Reset; a state block's Apply: contents the stream cannot know).
    void clear(){
        for(auto& s:rs)s.known=false;for(auto& a:samp)for(auto& s:a)s.known=false;for(auto& a:tss)for(auto& s:a)s.known=false;
        for(auto& s:xf)s.known=false;viewport.known=scissor.known=material.known=false;lights.clear();for(auto& p:clip)p.known=false;
        for(auto& v:vsF)v.known=false;for(auto& v:psF)v.known=false;for(auto& v:vsI)v.known=false;for(auto& v:psI)v.known=false;
        for(auto& b:vsB)b.known=false;for(auto& b:psB)b.known=false;
        for(auto& s:streams){bind(s.vb,nullptr);s.known=false;s.fromSet=false;s.freq.known=false;}
        bind(indices,nullptr);bind(decl,nullptr);bind(vs,nullptr);bind(ps,nullptr);bind(ds,nullptr);
        for(auto& r:rt)bind(r,nullptr);for(auto& t:tex)bind(t,nullptr);
        indicesSet=declSet=vsSet=psSet=false;for(auto& k:texSet)k=false;
        indicesKnown=declKnown=vsKnown=psKnown=dsKnown=false;for(auto& k:rtKnown)k=false;for(auto& k:texKnown)k=false;
        fvf.known=npatch.known=swvp.known=palette.known=false;
    }
    void invalidate(){clear();}
    // D3D9 side effects of other calls on state the stream mirrors: DrawPrimitiveUP / DrawIndexedPrimitiveUP leave stream 0 (and, indexed,
    // the index buffer) unbound; SetRenderTarget resets the viewport and the scissor rectangle.
    void afterUserPointerDraw(bool indexed){bind(streams[0].vb,nullptr);streams[0].known=false;streams[0].fromSet=false;if(indexed){bind(indices,nullptr);indicesKnown=false;indicesSet=false;}}
    void afterRenderTarget(){viewport.known=false;scissor.known=false;}

    // ---- replay thread, game thread waiting: the defaults batch ----
    // Reads RT0, DS, the viewport and every render / sampler / texture-stage state from the Target, so typical Gets never sync.
    void loadDefaults(IDirect3DDevice9* dev);
};
}
