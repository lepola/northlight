#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Exercise the production decal policy and frame gate without a game or GPU."""
from pathlib import Path
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

renderer=fp.src('renderer.cpp').read_text(encoding='utf-8')
world=fp.src('world_renderer.h').read_text(encoding='utf-8')
panel=fp.src('settings_panel.cpp').read_text(encoding='utf-8')
schema=fp.src('settings_schema.h').read_text(encoding='utf-8')

def method(text, signature):
    start=text.index(signature)
    end=text.index('{',start)+1
    depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}')
        end+=1
    return text[start:end]

assert 'in.hideBlobs=world&&!world->blobShadowsEnabled();' in renderer
live=renderer[renderer.index('world->applyLiveSettings(update->config);'):]
assert live.index('setStrength(world->blobShadowStrength())')<live.index('latchDrawGates();')
assert '{"blobs","Round ground shadows","BlobShadows","BlobShadowStrength",""}' in panel
assert '"BlobShadows"' not in schema[schema.index('restart={'):schema.index('};',schema.index('restart={'))]
assert 'k.low==0&&k.high==1?"check":"slider"' in schema

source=r'''
#include "quality_settings.h"
#include "draw_gates.h"
#include "settings_draft.h"
#include <cassert>
#include <cstdio>
struct World {
    NorthlightQuality::Settings quality;
    bool context=false,composited=false,shadows=false;
    bool hasContext()const{return context;}
    bool terrainShadowActive()const{return shadows&&composited;}
    __GETTERS__
};
struct Filter {unsigned strength=0;bool active()const{return strength<100;}};
struct Device {
    Filter* shadowBlobs=nullptr;World* world=nullptr;
    bool enabled=false,applied=false,terrain=false,failed=false;
    struct {struct {bool shadows=false;} settings;} effectKeys;
    __PREDICATE__
};
int main(){
    using namespace NorthlightQuality;
    for(auto presetName:{"Quality","Balanced","Performance"}){
        std::vector<std::string> problems;
        std::istringstream old(std::string("[Quality]\nPreset=")+presetName+"\nBlobShadowStrength=83\n");
        auto kept=load(&old,nullptr,problems);assert(problems.empty()&&kept.blobShadows==1&&kept.blobShadowStrength==83);
        for(unsigned actor=0;actor<2;++actor){
            std::istringstream off(std::string("[Quality]\nPreset=")+presetName+"\nBlobShadows=0\nBlobShadowStrength=83\nActorShadows="+std::to_string(actor)+"\n");
            auto hidden=effective(load(&off,nullptr,problems));assert(problems.empty()&&hidden.blobShadows==0&&hidden.blobShadowStrength==83);
            World w;w.quality=hidden;assert(w.blobShadowStrength()==0);
            w.quality.blobShadows=1;assert(w.blobShadowStrength()==83);
        }
    }
    {std::vector<std::string> p;std::istringstream bad("[Quality]\nBlobShadows=2\n");auto s=load(&bad,nullptr,p);assert(s.blobShadows==1&&p.size()==1);}
    {
        namespace S=NorthlightSettings;namespace D=NorthlightSettingsDraft;
        auto docs=S::factoryDocuments();docs[0]=S::Document("[Quality]\nActorShadows=0\nBlobShadowStrength=83\n");
        auto active=S::configOf(docs,"");auto fields=S::schema(docs,docs,active,"");
        const S::Field *toggle=nullptr,*strength=nullptr;
        for(const auto& f:fields){if(f.key=="BlobShadows")toggle=&f;if(f.key=="BlobShadowStrength")strength=&f;}
        assert(toggle&&strength&&toggle->kind=="check"&&toggle->apply=="F"&&toggle->available&&toggle->factory=="1");
        D::Draft draft;draft.bind(&fields);std::string missing;
        assert(D::blocked(draft,*toggle,missing)==D::Block::None); // Still editable with ActorShadows=0.
        draft.set(*toggle,"0");assert(D::blocked(draft,*strength,missing)==D::Block::NeedsKey&&missing=="BlobShadows");
        auto saved=S::applyChanges(docs,fields,toggle->id()+"\t0\n");
        S::Document reopened(saved[0].bytes());auto next=S::qualityOf(reopened,"");
        auto live=S::liveQualityUpdate(active.quality,next);
        assert(!live.blobShadows&&live.blobShadowStrength==83&&!live.actorShadows);
        assert(live.pointShadows==active.quality.pointShadows&&live.actorShadowDetail==active.quality.actorShadowDetail);
        assert(!S::giRequestChanged(active.quality,live,active.celestial,active.celestial));
        auto restored=S::applyChanges(saved,fields,toggle->id()+"\t1\n");auto again=S::qualityOf(restored[0],"");
        assert(again.blobShadows&&again.blobShadowStrength==83);
    }
    unsigned cases=0;
    for(unsigned bits=0;bits<(1u<<13);++bits)for(unsigned strength:{0u,50u,100u}){
        const auto bit=[&](unsigned n){return (bits&(1u<<n))!=0;};
        World w;w.context=bit(7);w.quality.blobShadows=bit(8);w.quality.actorShadows=bit(11);
        w.composited=bit(10);w.shadows=bit(12);w.quality.blobShadowStrength=strength;
        Filter f{w.blobShadowStrength()};Device d;d.shadowBlobs=bit(0)?&f:nullptr;d.world=bit(6)?&w:nullptr;
        d.enabled=bit(2);d.applied=bit(3);d.terrain=bit(4);d.failed=bit(5);d.effectKeys.settings.shadows=bit(9);
        // Bit 1 independently represents a disabled filter, such as the strength-100 default path.
        if(bit(1))f.strength=100;
        const bool safe=bit(0)&&!bit(1)&&bit(2)&&!bit(3)&&bit(4)&&!bit(5)&&bit(6)&&bit(7);
        const bool expected=safe&&(!bit(8)||(strength<100&&bit(9)&&bit(10)&&bit(11)&&bit(12)));
        assert(d.blobFilterActive()==expected);
        NorthlightDrawGates::Inputs in;in.blobs=d.shadowBlobs&&f.active();in.world=d.world;in.enabled=d.enabled;in.failed=d.failed;
        in.hideBlobs=d.world&&!w.blobShadowsEnabled();in.shadowsKey=bit(9);in.actorShadows=bit(11);in.worldShadows=w.shadows;in.debugOff=true;
        for(bool on:{false,true}){auto gate=NorthlightDrawGates::latch(on,in);assert((gate.blob&&d.blobFilterActive())==expected);
            // Toggling the native decal must not change any directional/local-shadow policy or terrain gate.
            in.hideBlobs=!in.hideBlobs;assert(NorthlightDrawGates::latch(on,in).terrainShadow==gate.terrainShadow);in.hideBlobs=!in.hideBlobs;}
        ++cases;
    }
    std::printf("PASS native decal: %u policy cases, default/preset/INI validity and strength restoration\n",cases);
}
'''
source=source.replace('__GETTERS__','\n'.join(method(world,s) for s in (
    'unsigned blobShadowStrength()const','bool blobShadowsEnabled()const','bool actorShadowsEnabled()const')))
source=source.replace('__PREDICATE__',method(renderer,'bool blobFilterActive()const'))
output=fp.out()/'blob-shadow-toggle';output.mkdir(parents=True,exist_ok=True)
cpp=output/'toggle.cpp';cpp.write_text(source,encoding='utf-8')
for opt in ('-O0','-O2'):
    exe=output/('toggle-'+opt[1:]+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-std=c++17',opt,'-UNDEBUG',*fp.test_include_flags(),str(cpp),'-o',str(exe)],env=fp.zig_env(),check=True)
    subprocess.run([str(exe)],check=True)
