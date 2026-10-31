#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.212 (task 32) NameMask: floating unit names (world text) mark stencil bit 0x80, WorldComposite leaves marked pixels as the game drew them.

Native part: name_stencil.h (the name blend predicate over lazy state reads, the state tables, the depth-format verdict, the per-frame clear/mark bookkeeping) and the depth format upgrade of stream_hooks.h are driven
through the glue shape renderer.cpp uses (Clear hook, mark/unmark, composite) over fake draw sequences. Source part: the wiring in renderer.cpp, stream_device.h and world_renderer.h (name decision by the UI-hash vertex
shader and the perspective c3, duplicate stencil draw with restore, unmark wrap, Clear flag addition, composite stencil state, guards, depth upgrade) and the keys. Native clang++, plain -O2 and ASan/UBSan. No device or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re,subprocess,tempfile

HARNESS=r'''
#include "name_stencil.h"
#include "stream_hooks.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <string>
typedef unsigned long DWORD;
using namespace NorthlightNameStencil;
namespace TD=NorthlightTranslucentDepth;
struct Draw{bool water=false;DWORD z=1,blend=1,src=5,dst=6,color=15;};
static bool name(const Draw& d,unsigned* reads=nullptr){
    auto rd=[&](DWORD v){if(reads)++*reads;return v;};
    return nameBlend([&]{return rd(d.blend!=0);},[&]{return rd(d.src);},[&]{return rd(d.dst);},[&]{return rd(d.color);});
}
static bool undoes(const Draw& d){return TD::isUndo(d.water,[&]{return d.z;},[&]{return d.blend!=0;},[&]{return d.src;},[&]{return d.dst;},[&]{return d.color;});}
// A fake device: render states by id, a stencil buffer of one pixel, calls counted.
struct Dev{
    std::map<unsigned,DWORD> rs;unsigned stencil=0;unsigned clears=0,draws=0,sets=0;
    DWORD get(unsigned s){return rs.count(s)?rs[s]:0;}
    void set(unsigned s,DWORD v){rs[s]=v;++sets;}
    void clearStencil(){stencil=0;++clears;}
    void draw(bool depthPass,bool alphaPass){ /* one pixel: depth test result and alpha test result */
        ++draws;if(!get(StencilEnable)||!depthPass)return;
        if(get(AlphaTestEnable)&&!alphaPass)return;
        if(get(StencilFunc)==kCmpAlways&&get(StencilPass)==kOpReplace)stencil=(stencil&~get(StencilWriteMask))|(get(StencilRef)&get(StencilWriteMask));
    }
    bool compositeKeeps(){ /* the composite quad: stencil NOTEQUAL ref&mask */
        for(const auto& s:kComposite)rs[s.state]=s.value;
        return (stencil&get(StencilMask))!=(get(StencilRef)&get(StencilMask));
    }
};
static void apply(Dev& d,const Set* sets,size_t n,DWORD* prev,unsigned){for(size_t i=0;i<n;++i)prev[i]=d.get(sets[i].state);for(size_t i=0;i<n;++i)d.set(sets[i].state,sets[i].value);}
static void restore(Dev& d,const Set* sets,size_t n,const DWORD* prev){for(size_t i=0;i<n;++i)d.set(sets[i].state,prev[i]);}
int main(){
    // Both consumers request the same spare stencil bits independently. Turning
    // off floating-name protection must not disable the actor AO correction.
    for(unsigned names=0;names<2;++names)for(unsigned ao=0;ao<2;++ao){
        const std::string ini="[Quality]\nNameMask="+std::to_string(names)+"\nContactAO="+std::to_string(ao)+"\n";
        bool nameWanted=true,aoWanted=true;
        NorthlightStream::commandStreamFromText(ini,true,nullptr,nullptr,&nameWanted,nullptr,&aoWanted);
        assert(nameWanted==bool(names)&&aoWanted==bool(ao));
        assert((nameWanted||aoWanted)==bool(names||ao));
    }

    // ids and values are the Direct3D 9 ones (renderer.cpp static_asserts them against the SDK headers)
    assert(ZEnable==7&&ZWriteEnable==14&&AlphaTestEnable==15&&AlphaRef==24&&AlphaFunc==25&&StencilEnable==52&&StencilFunc==56&&StencilRef==57&&StencilMask==58&&StencilWriteMask==59&&kBit==0x80);
    // a name: alpha-blended Z-writing (or not) glyph quads, SRCALPHA source, RGB written, destination neither ZERO nor SRCCOLOR
    { Draw d;assert(name(d)); }                                                              /* SRCALPHA/INVSRCALPHA */
    { Draw d;d.z=0;assert(name(d)); }                                                        /* no Z write: also fine */
    { Draw d;d.dst=TD::kBlendOne;assert(name(d)); }                                          /* SRCALPHA/ONE */
    { Draw d;d.blend=0;assert(!name(d)); }                                                   /* no blending */
    { Draw d;d.color=0;assert(!name(d));d.color=8;assert(!name(d));d.color=1;assert(name(d)); } /* no RGB */
    { Draw d;d.dst=TD::kBlendZero;assert(!name(d)); }                                        /* SRCALPHA/ZERO: effectively opaque */
    { Draw d;d.dst=kBlendSrcColor;assert(!name(d)); }                                        /* modulate */
    { Draw d;d.src=TD::kBlendOne;assert(!name(d));d.src=0;assert(!name(d)); }                /* no SRCALPHA source: no alpha to test */
    { Draw d;unsigned n=0;d.blend=0;name(d,&n);assert(n==2); }                               /* colour, blend: unblended stops there */
    // unmark rule is the early-depth undo rule: opaque Z+RGB or water Z; a translucent name never unmarks itself
    { Draw d;assert(!undoes(d));d.blend=0;assert(undoes(d));d.blend=1;d.water=true;assert(undoes(d));d.water=false;d.z=0;d.blend=0;assert(!undoes(d)); }
    // depth formats: D24S8 75, D24X4S4 79, D15S1 73, D24FS8 81 and INTZ have stencil; D24X8 77, D16 80, D32 71 have none
    assert(hasStencil(75,0x5a544e49)&&hasStencil(79,0)&&hasStencil(73,0)&&hasStencil(81,0)&&hasStencil(0x5a544e49,0x5a544e49)&&!hasStencil(77,0x5a544e49)&&!hasStencil(80,0)&&!hasStencil(71,0));
    // depth upgrade: D24X8 -> D24S8 only while the flag is set and only for D24X8; counted per changed surface
    {   using namespace NorthlightStream;
        assert(upgradedDepthFormat(77)==77&&depthUpgrades.load()==0);
        depthUpgrade.store(true);assert(upgradedDepthFormat(77)==75&&depthUpgrades.load()==1&&upgradedDepthFormat(75)==75&&upgradedDepthFormat(80)==80&&upgradedDepthFormat(0x5a544e49)==0x5a544e49&&depthUpgrades.load()==1);
        depthUpgrade.store(false);assert(upgradedDepthFormat(77)==77); }
    // marking inside the game's own draw: the stencil states are set once (kMark, no effect while STENCILENABLE is off), a run only toggles STENCILENABLE; REPLACE 0x80 where the game's alpha and depth tests pass
    {   Dev d;d.rs[ZWriteEnable]=1;d.rs[AlphaTestEnable]=1;d.rs[AlphaFunc]=kCmpGreater;d.rs[AlphaRef]=96;d.rs[StencilEnable]=0;
        DWORD prev[sizeof kMark/sizeof kMark[0]];apply(d,kMark,sizeof kMark/sizeof kMark[0],prev,0);
        assert(d.get(StencilRef)==0x80&&d.get(StencilWriteMask)==0x80&&d.get(StencilFunc)==kCmpAlways&&d.get(StencilPass)==kOpReplace&&d.get(TwoSidedStencilMode)==0);
        for(const auto& st:kMark)assert(st.state>StencilEnable&&st.state<=TwoSidedStencilMode); /* stencil states only, never the enable: the game's colour, Z, blend and alpha test stay */
        assert(kMarkProbe[0].state==StencilPass&&kMarkProbe[0].value==kOpReplace&&kMarkProbe[1].state==StencilRef&&kMarkProbe[1].value==kBit);
        d.draw(true,true);assert(d.stencil==0);                       /* stencil off: states set, nothing marked */
        d.set(StencilEnable,1);
        assert(d.get(AlphaTestEnable)==1&&d.get(AlphaRef)==96&&d.get(ZWriteEnable)==1);
        d.draw(true,true);assert(d.stencil==0x80);d.stencil=0;d.draw(false,true);assert(d.stencil==0);d.draw(true,false);assert(d.stencil==0); /* occluded or alpha-rejected pixels are not marked */
        d.set(StencilEnable,0);d.stencil=0;d.draw(true,true);assert(d.stencil==0); }
    // alpha test off in the game: GREATER 0 around the draw, restored after; it drops only alpha-0 pixels
    {   Dev d;d.rs[AlphaTestEnable]=0;d.rs[AlphaFunc]=kCmpAlways;d.rs[AlphaRef]=5;DWORD prev[sizeof kAlphaGuard/sizeof kAlphaGuard[0]];apply(d,kAlphaGuard,sizeof kAlphaGuard/sizeof kAlphaGuard[0],prev,0);
        assert(d.get(AlphaTestEnable)==1&&d.get(AlphaFunc)==kCmpGreater&&d.get(AlphaRef)==0);restore(d,kAlphaGuard,sizeof kAlphaGuard/sizeof kAlphaGuard[0],prev);assert(d.get(AlphaTestEnable)==0&&d.get(AlphaFunc)==kCmpAlways&&d.get(AlphaRef)==5); }
    // names: the first run of a frame sets the 7 states and enables; every later run only toggles STENCILENABLE (2 calls); the probe tells when another source changed the set
    {   Dev d;d.rs[StencilEnable]=0;DWORD prev[sizeof kMark/sizeof kMark[0]];apply(d,kMark,sizeof kMark/sizeof kMark[0],prev,0);const unsigned afterSet=d.sets;
        for(int i=0;i<5;++i){d.set(StencilEnable,1);d.stencil=0;d.draw(true,true);assert(d.stencil==0x80);d.set(StencilEnable,0);}
        assert(d.sets==afterSet+10);
        d.compositeKeeps(); /* the composite changes FUNC/PASS/REF: the probe sees it and the next run re-sets the states */
        assert(d.get(kMarkProbe[0].state)!=kMarkProbe[0].value||d.get(kMarkProbe[1].state)==kBit); }
    // unmark wrap: pixels the depth-final draw covers lose the bit; the rest keep it; restored afterwards
    {   Dev d;d.stencil=0x80;d.rs[StencilEnable]=0;DWORD prev[sizeof kUnmark/sizeof kUnmark[0]];apply(d,kUnmark,sizeof kUnmark/sizeof kUnmark[0],prev,96);
        d.draw(false,true);assert(d.stencil==0x80);d.draw(true,true);assert(d.stencil==0);restore(d,kUnmark,sizeof kUnmark/sizeof kUnmark[0],prev);assert(d.get(StencilEnable)==0); }
    // composite: marked pixels are rejected, the others pass; nothing is written; depth off
    {   Dev d;d.stencil=0x80;assert(!d.compositeKeeps());d.stencil=0;assert(d.compositeKeeps());
        assert(d.get(ZEnable)==0&&d.get(ZWriteEnable)==0&&d.get(StencilWriteMask)==0&&d.get(StencilEnable)==1&&d.get(StencilFunc)==kCmpNotEqual&&d.get(StencilRef)==0x80&&d.get(StencilMask)==0x80);
        d.stencil=0x7f;assert(d.compositeKeeps()); /* other bits are irrelevant */ }
    // frame bookkeeping: the game's Clear(Z) of the world depth covers the first mark; else one stencil-only clear
    {   Mask m;m.setting=true;assert(m.armed()&&m.needsClear());
        m.noteClear(false);assert(!m.needsClear()&&m.stencilClears==0);m.noteMark();assert(m.hasMarks()&&!m.needsClear());
        m.endFrame();assert(!m.hasMarks()&&m.needsClear()&&m.marksTotal==1);
        m.noteClear(true);assert(m.stencilClears==1&&!m.needsClear());m.noteMark();m.noteMark();assert(m.marks==2&&m.marksTotal==3);++m.runs;m.endFrame();assert(m.runs==0);m.noteClear(true);m.noteMark();m.noteMark();
        m.endFrame();assert(m.marks==0&&m.stencilClears==0); }
    { Mask m;assert(!m.armed()); m.setting=true;assert(m.armed());m.disable("x");assert(!m.armed()&&std::string(m.reason)=="x");m.disable("y");assert(std::string(m.reason)=="x"); }
    // glue: a frame with the game's Clear, a mark, an unmark and the composite; a frame without a Clear gets ours; a frame without marks costs no unmark draw
    {   Mask m;m.setting=true;Dev d;d.rs[StencilEnable]=0;
        auto gameClear=[&]{if(m.armed()&&!m.hasMarks()){d.clearStencil();m.noteClear(false);}};
        auto mark=[&](bool depthPass,bool alphaPass){if(m.needsClear()){d.clearStencil();m.noteClear(true);}DWORD p[sizeof kMark/sizeof kMark[0]];apply(d,kMark,sizeof kMark/sizeof kMark[0],p,0);d.set(StencilEnable,1);d.draw(depthPass,alphaPass);d.set(StencilEnable,0);m.noteMark();};
        auto unmark=[&]{if(!m.hasMarks())return;DWORD p[sizeof kUnmark/sizeof kUnmark[0]];apply(d,kUnmark,sizeof kUnmark/sizeof kUnmark[0],p,0);d.draw(true,true);restore(d,kUnmark,sizeof kUnmark/sizeof kUnmark[0],p);++m.unmarkDraws;};
        d.stencil=0x80;
        gameClear();assert(d.stencil==0&&d.clears==1);
        mark(true,true);assert(d.stencil==0x80&&d.clears==1&&m.stencilClears==0);   /* no extra clear */
        unmark();assert(d.stencil==0&&m.unmarkDraws==1);                             /* solid geometry drawn over the name afterwards */
        mark(true,true);assert(d.stencil==0x80);
        assert(!d.compositeKeeps());
        m.endFrame();
        mark(true,true);assert(m.stencilClears==1&&d.clears==2);                    /* no Clear(Z) seen this frame: one of ours before the first mark */
        m.endFrame();unmark();assert(m.unmarkDraws==0);                              /* no marks: unmark draws are not wrapped */ }
    std::printf("PASS name stencil predicate, tables, depth upgrade and bookkeeping\n");
}
'''

with tempfile.TemporaryDirectory(prefix='northlight-name-mask-') as tmp:
    (Path(tmp)/'t.cpp').write_text(HARNESS)
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(Path(tmp)/'t.cpp'),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)

# source checks
r=fp.tracked('renderer.cpp').read_text();q=fp.src('quality_settings.h').read_text();w=fp.src('world_renderer.h').read_text();h=fp.src('name_stencil.h').read_text()
sd=fp.src('stream_device.h').read_text();sh=fp.src('stream_hooks.h').read_text()
checks={}
checks['header: portable, no device calls']=all(x not in h for x in ('d3d9','windows.h','ext->','#include <vector>','new ','malloc'))
impl=r[r.index('template<class Capture> void prepareDrawImpl('):r.index('    // Terrain draws run with the game')]
dom=impl.index('if(worldDomain){');dec=impl.index('nameMaskDecide(');cap=impl.index('capture(vs);')
checks['prepareDrawImpl: kind reset first; decision in the world domain for terrain frames, armed, no debug view, before capture(vs)']=(
    '++drawCalls;maskKind=0;' in impl and dom<dec<cap and 'if(terrain&&!nameMask.disabled&&worldDebug==0&&((nameMask.armed()&&drawUiHashVS)||nameMask.hasMarks()||unlitMarks||(classifyVs(vs).entry&kUnlitTag))){' in impl and 'nameMaskDecide(drawWaterVS,zw,ab,sb,db,cw,viewport);' in impl)
dc=r[r.index('bool nameMaskReady(){'):r.index('// The bound depth buffer is the frame')]
checks['decision: the game must not use stencil (disable once), the depth format is checked once per surface and format (logged, a surface without stencil only skips the mask), unmark needs marks, a name needs the full viewport and the world depth']=(
    'nameMask.disable("no stencil' not in r and dc.index('D3DRS_STENCILENABLE')<dc.index('nameMask.disable("the game uses stencil")')<dc.index('worldDepth!=maskFormatFor||format!=maskFormatValue')<dc.index('NAMEMASK world depth surface=%p format=%lu')<dc.index('no stencil bits: no name mask while this surface is the world depth')
    <dc.index('(nameMask.hasMarks()||unlitMarks)&&NorthlightTranslucentDepth::isUndo(')<dc.index('maskKind=2')<dc.index('maskKind=1'))
checks['names: the UI-hash vertex shader (tag 2) whose c3 is not the screen-space (0,0,0,1) of the boundary, never water, alpha-blended, full viewport, world depth bound']=(
    'if(water||!drawUiHashVS){' in dc and 'drawWaterVS=(entry&kWaterTag)!=0;' in r and 'drawUiHashVS=uiTextVs(vs,entry);' in r and 'if(!NorthlightNameStencil::nameBlend(ab,sb,db,cw))return;' in dc
    and dc.index('!drawUiHashVS')<dc.index('hasMarks()||unlitMarks)&&NorthlightTranslucentDepth::isUndo(')<dc.index('nameBlend(ab,sb,db,cw)') # the cheapest test first: all but the names stop at the tag
    and 'if(FAILED(ext->GetVertexShaderConstantF(3,c3,1))||NorthlightUiBoundary::clipWOne(c3))return;' in dc
    and dc.index('!drawUiHashVS')<dc.index('GetVertexShaderConstantF(3,c3,1)')<dc.index('viewport.Width==width&&viewport.Height==height&&worldDepthBound()'))
checks['font mods (Lexara, TweakWoW2): a name may also be drawn with the VS the UI boundary fallback learned, only while it is armed; the boundary itself still tests tag 2 only']=(
    'bool uiTextVs(IDirect3DVertexShader9* vs,int entry)const{return (entry&kTagMask)==2||(entry&kUiTextTag)||(vs&&uiFallback.armed()&&reinterpret_cast<std::uintptr_t>(vs)==uiFallback.learnedVs());}' in r
    and r.count('uiTextVs(')==3 and '} else if (tag==2) {' in r)
checks['the boundary keeps its own test: tag 2, orthographic rows, depth state and main-target viewport call renderEffects (screen UI is never a name)']=(
    'NorthlightUiBoundary::accepts(zw!=0,rows,zf==D3DCMP_ALWAYS,z!=0)' in r and 'fullViewport(desc,nullptr,true)' in r and 'SUCCEEDED(ext->GetVertexShaderConstantF(0,rows,4))' in r)
indexed=r[r.index('HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive('):r.index('HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(')]
checks['draw states: a mirror-known render state is read in place under the gate of the draw hook (no lock, no COM call), the same active() and known test as GetRenderState, else GetRenderState']=(
    'v=drawRenderState(t);known=true;' in impl and 'if(unsigned(t)<DeviceMirror::RenderStates&&mirrorState.active()&&mirrorState.renderStateKnown[t]){v=mirrorState.renderStates[t];return true;}' in r
    and 'return SUCCEEDED(ext->GetRenderState(t,&v));' in r and 'DWORD drawRenderState(D3DRENDERSTATETYPE t){DWORD v=0;readDrawRenderState(t,v);return v;}' in r and indexed.index('Guard mirrorLock(mirrorState.gate);')<indexed.index('auto draw=')<indexed.index('return drawHook(') and 'ext->DrawIndexedPrimitive(' in indexed)
checks['hash UI boundary draws and everything after applied are never marked (prepareDrawImpl returns once applied; the decision sits after beforeDraw)']=(
    impl.index('if(applied){')<impl.index('beforeDraw(vs);')<impl.index('nameMaskDecide('))
mc=r[r.index('bool nameMaskClear(){'):r.index('// The run of name draws')]
checks['clear: a stencil-only Clear before the first name without a game clear (not with a scissor test, a full viewport for the call)']=(
    mc.index('D3DRS_SCISSORTESTENABLE')<mc.index('GetViewport(&viewport)')<mc.index('ext->SetViewport(&full)')<mc.index('ext->Clear(0,nullptr,D3DCLEAR_STENCIL,0,1.f,0)')<mc.index('ext->SetViewport(&viewport)')<mc.index('nameMask.noteClear(true)'))
ma=r[r.index('bool maskApply('):r.index('// The first name of a frame')] if False else r[r.index('bool maskApply('):r.index('template<size_t N> void maskRestore(')]
checks['states are read first (in place from the mirror when known), then set, restore puts the originals back']=ma.index('readDrawRenderState(')<ma.index('SetRenderState')<ma.index('sets[i].value') and 'GetRenderState' not in ma
run=r[r.index('bool nameMaskRunBegin(){'):r.index('// The draw hook\'s last step')]
checks['run: begin clears if needed then applies kMark once (nameRun), end restores it; the break keeps it only for a UI-hash vertex shader (tag 2)']=(
    run.index('if(nameRun)return true;')<run.index('nameMask.needsClear()&&!nameMaskClear()')<run.index('readDrawRenderState(D3DRS_STENCILPASS,pass)')<run.index('for(const auto& set:NorthlightNameStencil::kMark)ext->SetRenderState(')<run.index('SetRenderState(D3DRS_STENCILENABLE,TRUE)')<run.index('nameRun=true;++nameMask.runs;')
    and 'nameRun=false;CpuScope c(sampledDrawTimers()?&cpuNameMark:nullptr);ext->SetRenderState(D3DRS_STENCILENABLE,FALSE);' in run and 'nameRunPrevious' not in r and 'kMarkProbe[0].value' in run and 'if(!ext->peekVertexShader(vs)||!vs||!uiTextVs(vs,classifyVs(vs).entry))nameMaskRunEnd();' in run)
checks['run ends before anything that could draw or clear with stencil on: prepareDrawImpl (any non-UI-hash draw), nameMaskDraw (non-name), Clear, EndScene, Reset, renderEffects, finishFrameImpl']=(
    '++drawCalls;maskKind=0;if(nameRun)nameMaskRunBreak();' in impl and 'if(kind!=1)nameMaskRunEnd();' in r
    and 'nameMaskRunEnd(); /* 0.3.212 (task 32): a run of names never spans a Clear */' in r[r.index('HRESULT STDMETHODCALLTYPE Clear('):][:700]
    and 'override { Guard mirrorLock(mirrorState.gate);nameMaskRunEnd();' in r and 'void renderEffects() {\n        if(hdr.active&&!hdrAtUiBoundary)return;\n        nameMaskRunEnd();' in r and 'void finishFrameImpl() {\n        nameMaskRunEnd();' in r
    and (lambda reset: reset.index('nameMaskRunEnd();')<reset.index('rainMrtUnbind();'))(r[r.index('Reset(D3DPRESENT_PARAMETERS* pp) override'):][:900]))
checks['the stencil-in-use guard reads the game\'s STENCILENABLE once per frame (reset at the frame end and Reset), so a run knows the value to restore']='if(!nameMask.stencilChecked){' in r and 'nameMask.stencilChecked=true;' in r and 'FAILED(ext->GetRenderState(D3DRS_STENCILENABLE,&stencilOn))' in r
md=r[r.index('HRESULT nameMaskDraw('):r.index('    // One shader query/reference per original draw')]
checks['draw: unmark states around the game draw; a name draws with the run set and the alpha guard only when its alpha test is off; counted only after a successful draw']=(
    md.index('maskApply(NorthlightUnlitReceiver::Unmark,previous)')<md.index('rainBlendDraw(rain,claimed,draw)',md.index('maskApply(NorthlightUnlitReceiver::Unmark'))<md.index('maskRestore(NorthlightUnlitReceiver::Unmark,previous)')
    and md.index('nameMaskRunBegin()')<md.index('readDrawRenderState(D3DRS_ALPHATESTENABLE,alphaTest)')<md.index('maskApply(NorthlightNameStencil::kAlphaGuard,alphaPrevious)')<md.index('const HRESULT hr=rainBlendDraw(rain,claimed,draw);',md.index('kAlphaGuard'))<md.index('maskRestore(NorthlightNameStencil::kAlphaGuard,alphaPrevious)')<md.index('if(SUCCEEDED(hr)&&nameRun)nameMask.noteMark();')
    and 'if(!kind)return rainBlendDraw(rain,claimed,draw);' in md and 'if(claimed||!nameMask.armed())return rainBlendDraw(rain,claimed,draw);' in md)
hook=r[r.index('template<class Capture,class Draw> HRESULT drawHook('):];hook=hook[:hook.index('\n    }\n')+7]
checks['drawHook: both gate paths end in nameMaskDraw (optional draw wrappers retain the game call)']=hook.count('nameMaskDraw(rainBlend,claimed||mist,nativeDraw)')==2 and 'auto nativeDraw=[&]{return aoActorNativeDraw(draw);};' in hook and 'rainBlendDraw' not in hook
checks['draw lambdas retain all four device entry points']=len(re.findall(r'return drawHook\(t,count,\[&\]\(IDirect3DVertexShader9\* vs\)\{.*?\},draw\);\}',r,re.S))==4 and all(x in r for x in ('ext->DrawPrimitive(','ext->DrawIndexedPrimitive('))
clr=r[r.index('HRESULT STDMETHODCALLTYPE Clear('):][:2800]
checks['Clear hook: the stencil flag joins the game\'s full Clear(Z) of the world depth (no rects, scissor off) while the frame has no marks; with marks it is dropped; the mask bit in the game\'s value disables']=(
    'boundDepthIs(maskDsId)' in clr and 'n==0&&SUCCEEDED(ext->GetRenderState(D3DRS_SCISSORTESTENABLE,&scissor))&&!scissor&&SUCCEEDED(ext->GetViewport(&viewport))&&nameMaskFullViewport(viewport)' in clr and 'flags|=D3DCLEAR_STENCIL;stencil=0;' in clr and 'flags&=~DWORD(D3DCLEAR_STENCIL)' in clr
    and 'nameMask.noteClear(false)' in clr and 'nameMask.disable("the game clears stencil with the mask bit")' in clr and clr.index('flags|=D3DCLEAR_STENCIL')<clr.index('ext->Clear(n,rects,flags,color,z,stencil)'))
re_=r[r.index('void renderEffects() {'):r.index('/* The guards of every mask decision')]
checks['viewport: a world-depth Clear(Z) is the full clear only with a viewport of the depth surface size (Clear is clipped to it); our own stencil Clear sets a full viewport for the call and restores it']=(
    'v.Width==worldDepthDesc.Width&&v.Height==worldDepthDesc.Height' in r)
checks['renderEffects: marks only with an armed feature, a stencil depth, the world depth and no debug view; the world gets the surface for its composite, the legacy composite binds it around its quad with the shared bind']=(
    'const bool maskOn=nameMask.armed()&&nameMask.hasMarks()&&maskStencil&&worldDepth&&worldDebug==0;' in re_ and 'world->setNameMask(maskOn?worldDepth:nullptr);' in re_
    and re_.index('{NameMaskCompositeBind mask(ext,maskOn?worldDepth:nullptr);')<re_.index('if (error(quad(width,height),"composition pass")) return false;}'))
bind=w[w.index('struct NameMaskCompositeBind {'):w.index('class WorldRenderer {')]
checks['composite bind (one helper for both composites): the world depth-stencil and kComposite only with marks; D3DERR_NOTFOUND counts as no depth bound; the three states and the previous depth-stencil restored at scope end']=(
    'if(!maskDepth)return;' in bind and 'got==D3DERR_NOTFOUND' in bind and bind.index('GetDepthStencilSurface(&previousDepth)')<bind.index('SetDepthStencilSurface(maskDepth)')<bind.index('GetRenderState(kRestored[i],&previous[i])')<bind.index('NorthlightNameStencil::kComposite')
    <bind.index('~NameMaskCompositeBind(){')<bind.index('SetRenderState(kRestored[i],previous[i])')<bind.index('SetDepthStencilSurface(previousDepth)')<bind.index('previousDepth->Release()')
    and '{D3DRS_STENCILENABLE,D3DRS_ZENABLE,D3DRS_ZWRITEENABLE}' in bind)
comp=w[w.index('IDirect3DSurface9* maskDepth=unlitMask?'):w.index('if(!compositeOk)return false;composited=true;')]
checks['WorldComposite: the pointer consumed, the quad inside the bind scope']=(
    'nameMaskDepth=unlitMaskDepth=nullptr;' in comp and 'void setNameMask(IDirect3DSurface9* depthStencil){nameMaskDepth=depthStencil;}' in w and 'nameMaskQpc' not in w)
checks['debug view: the last effect pass before applied=true when the frame has marks, stencil EQUAL on the bit, ZENABLE off, nothing written']=(
    'if(maskOn&&nameMask.debug)nameMaskDebugView(outputTarget);' in r and r.index('nameMaskDebugView(outputTarget);')<r.index('applied = true; ++appliedFrames;')
    and 'IDirect3DSurface9* outputTarget=linearHdr?hdrPresentBack:saved.targets[0];' in r
    and 'D3DRS_STENCILFUNC,D3DCMP_EQUAL' in r and 'D3DRS_STENCILWRITEMASK,0' in r)
checks['depth upgrade: the key read from the ini at CreateDevice, D24S8 checked with CheckDeviceFormat and CheckDepthStencilMatch before the auto depth and CreateDepthStencilSurface change, Reset too, the stream applies it on the game thread']=(
    'const bool stream=NorthlightStream::commandStreamRequested(rootPath,&framesAhead,&frameSkip,&nameMaskWanted,&occlusionLocal,&contactAOWanted);' in r and r.index('commandStreamRequested(rootPath,&framesAhead,&frameSkip,&nameMaskWanted,&occlusionLocal,&contactAOWanted)')<r.index('upgradeAutoDepth(real,adapter,type,pp,nameMaskWanted||contactAOWanted,"CreateDevice");')
    and 'nameMaskRequested' not in r and 'if(nameMask)*nameMask=s.nameMask!=0;' in sh and 'if(contactAO)*contactAO=s.contactAO!=0;' in sh and 'return commandStreamFromText(text,has,framesAhead,frameSkip,nameMask,occlusionLocal,contactAO);' in sh
    and 'if(nameMask.setting){const D3DFORMAT upgraded' not in r and 'D3DUSAGE_DEPTHSTENCIL,D3DRTYPE_SURFACE,D3DFMT_D24S8' in r and 'CheckDepthStencilMatch(' in r
    and 'upgradeAutoDepth(parent,cp.AdapterOrdinal,cp.DeviceType,pp,world->nameMask()||world->contactAO(),"Reset");' in r and 'if(pp&&world)' in r
    and 'NorthlightStream::upgradedDepthFormat(unsigned(Format))' in r and 'D3DFORMAT(NorthlightStream::upgradedDepthFormat(unsigned(fmt)))' in sd
    and 'inline unsigned upgradedDepthFormat(unsigned format)' in sh)
checks['kill switch: NameMask=0 marks nothing, adds no name clear flag and leaves the composite alone; independent AO can request stencil']=(
    'if(nameMask)*nameMask=false;' in sh and 'nameMask.setting=world->nameMask();' in r and 'bool armed()const{return setting&&!disabled;}' in h
    and 'if(maskKind==1&&!nameMask.armed())maskKind=0;' in r and 'nameMask.armed()&&nameMask.hasMarks()&&maskStencil' in r)
fin=r[r.index('NAMEMASK frame=')-300:r.index('NAMEMASK frame=')+1800]
checks['frame end: one sampled NAMEMASK line (marked, unmarked, composite, depthFormat, stencil), state reset after it']=(
    'marked=%u runs=%u unmarked=%u composite=%d depthFormat=%lu stencil=%d' in fin and fin.index('NAMEMASK frame=')<fin.index('nameMask.endFrame();'))
checks['one-shot lines: depth upgrade (or why not), stencil in the game, a world depth without stencil (capped), failed clear']=all(x in r for x in ('STENCIL depth upgraded D24X8->D24S8','STENCIL depth upgrade unavailable','NAMEMASK disabled: the game draws with stencil enabled','(no stencil bits: no name mask while this surface is the world depth)','NAMEMASK disabled: stencil clear failed'))
checks['release forgets the depth identities; the setting is read once at device creation']=('maskFormatFor=nullptr;maskFormatValue=0;maskStencil=false;maskDsId=nullptr;' in r[r.index('void releaseResources()'):r.index('bool error(HRESULT')]
    and 'nameMask.debug=world->nameMaskDebug();' in r)
# Each key's own entry (name, member, range, presets) and initialiser; Keys[] order and origin[] length belong to test_quality_docs.py.
checks['quality keys: NameMask 0..1 presets 1/1/1, NameMaskDebug 0..1 presets 0/0/0']=(
    re.search(r'\{\s*"NameMask"\s*,\s*&Settings::nameMask\s*,\s*0\s*,\s*1\s*,\s*\{\s*1\s*,\s*1\s*,\s*1\s*\}\s*\}',q) is not None
    and re.search(r'\{\s*"NameMaskDebug"\s*,\s*&Settings::nameMaskDebug\s*,\s*0\s*,\s*1\s*,\s*\{\s*0\s*,\s*0\s*,\s*0\s*\}\s*\}',q) is not None
    and 'unsigned nameMask=1;' in q and 'unsigned nameMaskDebug=0;' in q and 'nameMaskAlphaRef' not in q)
ini=(fp.REPO/'renderer'/'windows-package'/'northlight-quality.ini').read_text();txt=(fp.REPO/'renderer'/'windows-package'/'README.txt').read_text()
checks['documented: ini (commented, ranges and presets) and README.txt']=(';NameMask=1\n' in ini and 'NameMaskAlphaRef' not in ini and 'NameMaskAlphaRef' not in txt and ';NameMaskDebug=0\n' in ini and 'Allowed 0..1. 1 / 1 / 1' in ini
    and re.search(r'^  NameMask +1 / 1 / 1 ',txt,re.M) is not None and re.search(r'^  NameMaskDebug +0 / 0 / 0 ',txt,re.M) is not None)
fl=r[r.index('NAMEMASK frame='):][:900]
checks['cost: sample frames only (sampledDrawTimers/sampled), plain CpuScopes; decision timed every 8th call and scaled by calls/timed; mark and unmark summed; one cpuUs total; no calibration, no composite hand-off']=(
    '(++nameDecideCalls&7)==1' in r and md.count('CpuScope c(sampledDrawTimers()?&cpuNameMark:nullptr);')==2 and md.count('CpuScope c(sampledDrawTimers()?&cpuNameUnmark:nullptr);')==2
    and 'double(cpuNameDecide)*double(nameDecideCalls)/double(nameDecideTimed)*us' in r and 'decideUs=%.1f markUs=%.1f unmarkUs=%.1f cpuUs=%.1f' in fl and 'cpuNameDecide=cpuNameMark=cpuNameUnmark=0;' in r
    and all(x not in r for x in ('CountedScope','qpcPairTicks','NAMEMASK timer calibration','cpuNameComposite','takeNameMaskQpc')) and 'netTicks' not in h)
checks['no duplicate draw, no GPU pair timer: names are marked by the game\'s own draw']=('nameMarkDraw' not in r and 'nameGpu' not in r and 'NorthlightGpuPairTimer' not in r and not (fp.REPO/'src'/'core'/'gpu_pair_timer.h').exists())
checks['banner: 0.3.216 with the name mask']='logf("Northlight renderer 0.3.216;' in r and "floating names keep the game's colour (stencil mask); instance map names with an apostrophe or a space reach the world context; lamps in view first, screen-coherent lamp batches (16-lamp passes); backend=%s" in r
for n,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+n)
assert all(checks.values())
