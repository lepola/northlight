#!/usr/bin/env python3
# northlight-test:
"""0.3.158 ActorShadows wiring audit (static source analysis; nothing is run).
ActorShadows=0 keeps the static mod shadows (terrain, world-cache casters, the union) and drops
every replay (actor) shadow: model capture runs only for GI actor packets, no shadow consumer
reads the replays of a GI frame (replayShadows), every map and cube is complete without them
(replaysComplete, so nothing defers or demands a capture), the replay-derived keys are forced
off at load (effective()). Blob shadows: the filter was bypassed at 0 only until 0.3.192; since
0.3.193 BlobShadowStrength (0..100) decides, at both values: 0 hides, 100 draws the game's blob, between a faint texture. ActorShadows=1:
with the component enabled both predicates are freshReplays, the replay blocks run as before. The decision and
schedule model is exercised in test_quality_settings.cpp."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re
w=fp.src('world_renderer.h').read_text();pt=fp.src('world_point_rendering.inl').read_text()
flash=fp.src('world_flash_rendering.inl').read_text()
q=fp.src('quality_settings.h').read_text();r=fp.src('renderer.cpp').read_text()
def once(text,needle,what):assert text.count(needle)==1,f'{what}: expected exactly one {needle!r} (found {text.count(needle)})'
def code(text):return re.sub(r'/\*.*?\*/','',re.sub(r'//[^\n]*','',text),flags=re.S)

def guarded_block(text,head):
    once(text,head,'guarded block')
    start=text.index(head);first=text.index('{',start);depth=0
    # Match actual braces, including nested loops/lambdas, not a closing comment.
    for token in re.finditer(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|/\*.*?\*/|//[^\n]*|[{}]',text[first:],re.S):
        value=token.group()
        if value=='{':depth+=1
        elif value=='}':
            depth-=1
            if not depth:return text[start:first+token.end()]
    raise AssertionError('Unclosed guarded block: '+head)

def rejected(audit,text,label):
    try:accepted=audit(text)
    except AssertionError:return
    assert not accepted,'Audit accepted broken guard: '+label
checks={}
# Settings: the ActorShadows key (0..1, presets 1/1/1, default 1); its position in Keys[] is test_quality_docs.py's business.
checks['key ActorShadows 0..1, presets 1/1/1, default 1']=re.search(r'\{\s*"ActorShadows"\s*,\s*&Settings::actorShadows\s*,\s*0\s*,\s*1\s*,\s*\{\s*1\s*,\s*1\s*,\s*1\s*\}\s*\}',q) is not None and 'unsigned actorShadows=1;' in q
checks['effective() forces exactly the two replay keys']=('inline Settings effective(Settings s){if(!s.actorShadows)for(const auto& k:ActorShadowForced)s.*k.field=0;return s;}' in q
    and 'inline const ForcedKey ActorShadowForced[]={\n    {"ShadowFateDiagnostics",' in q and 'persistentRigidProps' not in q and 'persistentCasters' not in q
    and '{"ShadowFateDiagnostics",&Settings::shadowFateDiagnostics},{"DiagReplayProbe",&Settings::diagReplayProbe}};' in q)
checks['actorShadowWork']='inline bool actorShadowWork(const Settings& s,bool shadows){return shadows&&s.actorShadows;}' in q
# loadQuality: effective() right after the QUALITY/warning lines, before anything reads the settings.
lq=w[w.index('    void loadQuality(){'):w.index('    HRESULT quad(UINT w,UINT h,')]
eff='quality=NorthlightQuality::effective(quality);'
checks['effective() applied once in loadQuality, after the QUALITY line, before configure()']=(w.count('NorthlightQuality::effective(')==1 and eff in lq
    and lq.index('logf("QUALITY %s file=%s')<lq.index('logf("QUALITY warning')<lq.index(eff)<lq.index('NorthlightDiagnostics::configure(')<lq.index('NorthlightRenderThreadProbe::configure(')
    and 'logf("QUALITY ActorShadows=%u: %s; forced off: %s",quality.actorShadows,' in lq and lq.index('NorthlightQuality::forcedOff(quality)')<lq.index(eff))
ctor=w[w.index('        loadQuality();pipelineActive='):w.index('        (void)NorthlightStreaming::cpuRetirement();')]
checks['constructor reads quality only after loadQuality']='shadowFate(quality)' in ctor
# Capture decision: ActorShadows=0 is "shadows off" for capture (GI actor frames only).
dec=w[w.index('    bool modelCaptureSkipped(){'):w.index('    void captureModel(')]
checks['capture decision uses actorShadowWork']=('const bool shadows=ready()&&NorthlightQuality::actorShadowWork(quality,effects.shadows);' in dec and 'captureSkipPossible(quality,shadows)' in dec
    and 'in.shadows=shadows;' in dec and 'effects.shadows' not in dec.replace('NorthlightQuality::actorShadowWork(quality,effects.shadows)',''))
# render(): the three predicates, defined once, and their ActorShadows=1 reductions.
render=w[w.index('    bool render(IDirect3DSurface9* targetSurface'):] # the last member of WorldRenderer
body=code(render)
defs={}
for name in ('freshReplays','actorShadows','replayShadows','replaysComplete'):
    m=re.findall(r'const bool '+name+r'=([^;]*);',body);assert len(m)==1,name;defs[name]=m[0]
checks['predicate definitions']=defs=={'freshReplays':'captureMode!=CaptureSkipped','actorShadows':'quality.actorShadows!=0',
    'replayShadows':'effects.shadows&&freshReplays&&actorShadows&&ready()','replaysComplete':'freshReplays||!actorShadows'}
def value(expr,fresh,actor,geometry=True,shadows=True):return eval(expr.replace('effects.shadows','componentShadows').replace('ready()','geometryReady').replace('&&',' and ').replace('||',' or ').replace('!',' not '),{},{'freshReplays':fresh,'actorShadows':actor,'geometryReady':geometry,'componentShadows':shadows})
checks['ActorShadows=1 reduces both to freshReplays; 0: consumers off, schedules complete']=all(
    value(defs['replayShadows'],f,True)==f and value(defs['replaysComplete'],f,True)==f and value(defs['replayShadows'],f,False) is False and value(defs['replaysComplete'],f,False) is True
    for f in (False,True))
checks['unavailable geometry never consumes shadow replays']=all(value(defs['replayShadows'],f,a,False) is False for f in (False,True) for a in (False,True))
checks['disabled component never selects shadow replays']=all(value(defs['replayShadows'],f,a,g,False) is False for f in (False,True) for a in (False,True) for g in (False,True))
# Consumers (replayShadows) and schedules/commits (replaysComplete, actorShadows).
consumers=['if(replayShadows)selectShadowReplays();','if(effects.shadows&&replayShadows)replayBoundsKick();','(replayShadows&&!timedReplay())',
           'if(effects.shadows&&replayShadows){auto proofs','if(effects.shadows&&replayShadows)pointCalculateReplayBounds();']
schedules=['||!replaysComplete||shadowTransitionFailed||memoryPressure', '!reason&&key.valid&&!pull&&!actorLost&&!transition.state.fading(),shadowDiagnosticCapture!=0,replaysComplete);','if(cascade==0)nearRendered=!environmentReuse&&captureMode==CaptureFresh&&interval>1&&actorShadows;',
           'if(!environmentReuse){if(splitNear||captureMode==CaptureFresh||!actorShadows)reuse.commit(interval,shadowPasses,matrices[cascade]);else captureDemand=true;}',
           'renderPointShadows(atmosphere.lights,replaysComplete,actorShadows);else pointReady=false;',
           'renderFlashShadows(atmosphere.lights,replaysComplete,actorShadows);else for(auto& s:flashSlots)s.ready=false;']
def schedules_guarded(text):
    for needle in consumers+schedules:once(text,needle,'render()')
    remainder=text
    for needle in consumers+schedules:remainder=remainder.replace(needle,'')
    # Only the definitions may remain: an unreviewed consumer still fails.
    return remainder.count('replayShadows')==1 and remainder.count('replaysComplete')==1
checks['consumers gated on replayShadows, schedules on replaysComplete']=schedules_guarded(body)
checks['crossfade uses current-frame capture only and retains baseline scratch outside fades']=('dynamicShadow=(historyReady||transition.state.fading())?transition.dynamic:shadowScratch' in body and '||!replaysComplete||shadowTransitionFailed||memoryPressure' in body)
checks['no bare freshReplays consumer left in render()']=body.count('freshReplays')==3 # its definition and the two predicates
# The cascade replay block: skipped whole with ActorShadows=0; terrain before it and the union after it stay.
start=render.index('            if(actorShadows){ /* 0.3.158');end=render.index('            } /* actorShadows */')
block=render[start:end]
checks['cascade replay block gated, terrain before, union after']=(render.count('if(actorShadows){')==1 and render.count('} /* actorShadows */')==1
    and code(block).count('{')==code(block).count('}')+1
    and all(x in block for x in ('replayBoundsJoin(); /* 0.3.143: first pointBounds reader */','d->SetPixelShader(replayPS);','auto replayLoop=[&](auto& split)->bool{',
        'keepReplaySplit(slot,split,','replayProbe(rows,matrices[cascade],realLoopMs);'))
    and render.index('"live terrain shadow"')<start and end<render.index('if(!unionShadow(shadowSurface[slot],dynamicShadow,nearEnvironment[source],0,0,0,1024))return false;')
    and render.count('submitReplay(')==2 and all(start<m.start()<end for m in re.finditer(r'submitReplay\(',render)) and # 0.3.209 (task 29): the actor map's loop is the second
    render.count('replayLoop(')==2 and all(start<m.start()<end for m in re.finditer(r'replayLoop\(',render)))
# The probe moved before the live dump: legal because it never runs on a diagnostic capture frame.
checks['probe never on a diagnostic capture frame']='probeRecord=replayProbeActive()&&slot==0&&captureMode==CaptureFresh&&!shadowDiagnosticCapture;' in block
# Point cube: withReplays gates both replay loops and the schedule's replay count.
pb=pt[pt.index('    bool renderPointShadow(const NorthlightLocalLights::Light& light,bool fresh=true,bool withReplays=true){'):pt.index('    bool renderPointLighting(')]
candidate_head='if(withReplays)for(size_t i=0;i<replays.size();++i){'
count='if(fresh)pointReplayCount=withReplays?replays.size():0;'
def point_replays_guarded(text):
    candidates=guarded_block(text,candidate_head);draws=guarded_block(text,'if(withReplays){')
    once(text,count,'point replay count')
    for needle in ('const auto& bounds=replays[i]->pointBounds;', 'pointOwnShell(*replays[i],ownShell,runs)',
                   'pointAppendCandidates(pointReplayCandidates,i,mask,faces);'):once(candidates,needle,'point candidate guard')
    for needle in ('for(size_t index:pointReplayCandidates[face]){const auto& p=replays[index];',
                   '"cube animated draw"','"cube split emitter draw"'):once(draws,needle,'point draw guard')
    outside=text.replace(candidates,'').replace(draws,'').replace(count,'')
    return (text.index(candidates)+len(candidates)<=text.index(draws)
            and text.index(draws)+len(draws)<text.index('// With no dynamic casters, min(cache, cache) copies the static face')
            and re.search(r'\breplays\b',code(outside)) is None)
checks['point: both replay loops and the count gated on withReplays']=point_replays_guarded(pb)

def flash_replays_guarded(text):
    draw=guarded_block(text,'if(withReplays){')
    for needle in ('replayBoundsJoin();','for(const auto& p:replays){','submitReplay(p.get(),'):
        once(draw,needle,'flash replay guard')
    maps='s.ready=renderFlashMap(s,0,withReplays)&&renderFlashMap(s,1,withReplays);'
    once(text,maps,'both flash cascades');once(text,'if(!fresh){captureDemand=true;continue;}','fresh flash maps')
    return (text.index('if(!fresh){captureDemand=true;continue;}')<text.index('if(!flashResources(s))continue;')<text.index(maps)
            and re.search(r'\breplays\b',code(text.replace(draw,''))) is None)
checks['flash: fresh maps and both actor cascades gated']=flash_replays_guarded(flash)

# Negative controls use modified source strings; production files are never edited.
assert point_replays_guarded(pb.replace(candidate_head,candidate_head+' /* } */ const char* brace="}";'))
mutations=[
    (schedules_guarded,body.replace(schedules[-1],''),'missing flash schedule'),
    (schedules_guarded,body.replace('renderFlashShadows(atmosphere.lights,replaysComplete,actorShadows)',
                                  'renderFlashShadows(atmosphere.lights,true,actorShadows)'),'stale flash capture'),
    (schedules_guarded,body.replace('renderFlashShadows(atmosphere.lights,replaysComplete,actorShadows)',
                                  'renderFlashShadows(atmosphere.lights,replaysComplete,true)'),'flash actor switch bypass'),
    (schedules_guarded,body+' consume(replayShadows,replaysComplete);','unreviewed consumer'),
    (point_replays_guarded,pb.replace(candidate_head,candidate_head.removeprefix('if(withReplays)')),'unguarded point candidates'),
    (point_replays_guarded,pb.replace('if(withReplays){','if(true){'),'unguarded point draws'),
    (point_replays_guarded,pb.replace(count,'if(fresh)pointReplayCount=replays.size();'),'unguarded replay count'),
    (point_replays_guarded,pb.replace(candidate_head,'pointOwnShell(*replays[0],ownShell,runs);'+candidate_head),'unguarded emitter read'),
    (point_replays_guarded,pb+' for(const auto& p:replays)consume(p);','unguarded replay range'),
    (flash_replays_guarded,flash.replace('if(withReplays){','if(true){'),'unguarded flash actors'),
    (flash_replays_guarded,flash.replace('renderFlashMap(s,1,withReplays)','renderFlashMap(s,1,true)'),'far flash switch bypass'),
    (flash_replays_guarded,flash.replace('if(!fresh){captureDemand=true;continue;}',''),'stale flash map'),
]
for audit,text,label in mutations:rejected(audit,text,label)
checks['audit rejects all broken shadow guards']=True
# Blob filter: the default strength policy follows actor shadows; explicit hiding is independent.
helper='bool blobFilterActive()const{return shadowBlobs&&shadowBlobs->active()&&enabled&&terrain&&!failed&&world&&world->hasContext()&&(!world->blobShadowsEnabled()||(!applied&&effectKeys.settings.shadows&&world->terrainShadowActive()&&world->actorShadowsEnabled()));}'
checks['native blob stays when no replacement shadow was composited']=('bool terrainShadowActive()const{return effects.shadows&&ready()&&shadowsComposited;}' in w and 'shadowsComposited=geometryReady&&effects.shadows&&(sourceActive[0]||sourceActive[1])&&debug==0;' in w)
# 0.3.187: the four draw entry points share drawHook(); FrameDrawGates=0 and =1 both test the helper.
checks['blob filter: one helper, every draw through drawHook']=(r.count(helper)==1 and r.count('if(!claimed&&blobFilterActive())extensionWork("blob shadow filter",[&]{blobFilter(count,claimed);});')==1
    and r.count('if(!claimed&&drawGates.blob){stage="blob shadow filter";if(blobFilterActive())blobFilter(count,claimed);}')==1 and r.count('return drawHook(t,count,')==4
    and r.count('!world->blobShadowsEnabled()||(!applied&&effectKeys.settings.shadows&&world->terrainShadowActive()&&world->actorShadowsEnabled())')==1 and r.count('blobClaim(count)')==1
    and 'bool actorShadowsEnabled()const{return quality.actorShadows!=0;}' in w)
# Documentation: the shipped ini keeps the key commented (the file must still parse as Quality).
ini=fp.src('windows-package/northlight-quality.ini').read_text();readme=fp.src('windows-package/README.txt').read_text(encoding='utf-8')
checks['ini and README document ActorShadows']=(';ActorShadows=1\n' in ini and '\nActorShadows=' not in ini and 'Allowed 0..1. 1 / 1 / 1' in ini[ini.index('; Actor shadows'):ini.index(';ActorShadows=1')]
    and 'ActorShadows          1 / 1 / 1      shadows of characters and moving objects (0 = static shadows only' in readme)
# 0.3.193: there is no compile-time switch; the strength is held by the filter, the Device asks it (blobFilterActive, the latch), and it is read from the quality settings once at device creation.
bf=fp.src('shadow_blob_filter.h').read_text()
bq=fp.src('quality_settings.h').read_text()
checks['0.3.193 BlobShadowStrength: read once at device creation, gates blobFilterActive and the latched gate; no compile-time switch']=('HidesNativeBlobs' not in bf and 'HidesNativeBlobs' not in r
    and 'unsigned blobShadowStrength=50;' in bq and 'shadowBlobs=std::make_unique<NorthlightShadowBlobFilter>(ext,world->blobShadowStrength());' in r and 'blobStrength' not in r
    and 'in.blobs=shadowBlobs!=nullptr&&shadowBlobs->active();' in r and 'bool blobFilterActive()const{return shadowBlobs&&shadowBlobs->active()&&' in r
    and 'unsigned blobShadowStrength()const{return quality.blobShadows?quality.blobShadowStrength:0;}' in w and 'blobShadowStrength=0)' not in bf and 'strength(blobShadowStrength)' in bf)
checks['0.3.193 filter: Skip at 0, Faint only under a modulate blend, faint texture A8R8G8B8 MANAGED, released in the destructor']=(
    'enum class Claim {None,Skip,Faint};' in bf and 'if(strength>=100||' in bf and 'op==D3DBLENDOP_ADD&&((src==D3DBLEND_DESTCOLOR&&dst==D3DBLEND_ZERO)||(src==D3DBLEND_ZERO&&dst==D3DBLEND_SRCCOLOR))' in bf
    and 'D3DFMT_A8R8G8B8,D3DPOOL_MANAGED' in bf and '~NorthlightShadowBlobFilter(){release(faint);}' in bf and 'faintMipChain(strength)' in bf and 'return {Claim::Skip};}   // 0.3.193: cannot lighten it' in bf and 'faintFailures++<3' in bf and 'frame-faintFailedFrame<600' in bf and 'guard.p=nullptr;return {Claim::Faint,bound};' in bf and 'blob shadow draws follow BlobShadowStrength' in bf)
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
print('PASS ActorShadows wiring: predicates reduce to freshReplays at 1; at 0 GI-only capture, no replay consumer, no deferral, forced keys; 0.3.193 BlobShadowStrength wiring')
ini2=ini[ini.index(';BlobShadowStrength=50')-1500:ini.index(';BlobShadowStrength=50')+30]
assert ';BlobShadowStrength=50\n' in ini and '\nBlobShadowStrength=' not in ini and 'Allowed 0..100. 50 / 50 / 50' in ini2 and 'BlobShadowStrength   ' in readme
print('PASS BlobShadowStrength documented in the ini and the README')
