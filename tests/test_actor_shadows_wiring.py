#!/usr/bin/env python3
# northlight-test:
"""0.3.158 ActorShadows wiring audit (static source analysis; nothing is run).
ActorShadows=0 keeps the static mod shadows (terrain, world-cache casters, the union) and drops
every replay (actor) shadow: model capture runs only for GI actor packets, no shadow consumer
reads the replays of a GI frame (replayShadows), every map and cube is complete without them
(replaysComplete, so nothing defers or demands a capture), the replay-derived keys are forced
off at load (effective()). Blob shadows: the filter was bypassed at 0 only until 0.3.192; since
0.3.193 BlobShadowStrength (0..100) decides, at both values: 0 hides, 100 draws the game's blob, between a faint texture. ActorShadows=1:
both predicates are exactly freshReplays, the replay blocks run as before. The decision and
schedule model is exercised in test_quality_settings.cpp."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re
w=fp.src('world_renderer.h').read_text();pt=fp.src('world_point_rendering.inl').read_text()
q=fp.src('quality_settings.h').read_text();r=fp.src('renderer.cpp').read_text()
def once(text,needle,what):assert text.count(needle)==1,f'{what}: expected exactly one {needle!r} (found {text.count(needle)})'
def code(text):return re.sub(r'/\*.*?\*/','',re.sub(r'//[^\n]*','',text),flags=re.S)
checks={}
# Settings: key appended last (origin indices of the older keys unchanged), default 1 in every preset.
keys=q[q.index('inline const Key Keys[]={'):q.index('};',q.index('inline const Key Keys[]={'))]
# 0.3.187: FrameDrawGates is appended after it (Keys[30] stays ActorShadows); 0.3.190 appends ShadowPivotCorrection; 0.3.192 CommandStream; 0.3.193 BlobShadowStrength (last).
checks['key last, 0..1, presets 1/1/1']=keys.rstrip().replace('\n    {"Weather",&Settings::weather,0,1,{1,1,1}},\n    {"RainFog",&Settings::rainFog,0,2,{1,1,1}},\n    {"FogClouds",&Settings::fogClouds,0,1,{1,1,0}},\n    {"FogCloudDensity",&Settings::fogCloudDensity,0,200,{100,100,100}},\n    {"FogTemporal",&Settings::fogTemporal,0,1,{1,1,1}},\n    {"RainBlend",&Settings::rainBlend,0,1,{1,1,1}},','').endswith('{"ActorShadows",&Settings::actorShadows,0,1,{1,1,1}},\n    {"FrameDrawGates",&Settings::frameDrawGates,0,1,{1,1,1}},\n    {"ShadowPivotCorrection",&Settings::shadowPivotCorrection,0,1,{1,1,1}},\n    {"CommandStream",&Settings::commandStream,0,1,{1,1,1}},\n    {"BlobShadowStrength",&Settings::blobShadowStrength,0,100,{50,50,50}},') and 'unsigned actorShadows=1;' in q and 'char origin[41]=' in q
checks['effective() forces exactly the two replay keys']=('inline Settings effective(Settings s){if(!s.actorShadows)for(const auto& k:ActorShadowForced)s.*k.field=0;return s;}' in q
    and 'inline const ForcedKey ActorShadowForced[]={\n    {"ShadowFateDiagnostics",' in q and 'persistentRigidProps' not in q and 'persistentCasters' not in q
    and '{"ShadowFateDiagnostics",&Settings::shadowFateDiagnostics},{"DiagReplayProbe",&Settings::diagReplayProbe}};' in q)
checks['actorShadowWork']='inline bool actorShadowWork(const Settings& s,bool shadows){return shadows&&s.actorShadows;}' in q
# loadQuality: effective() right after the QUALITY/warning lines, before anything reads the settings.
lq=w[w.index('    void loadQuality(){'):w.index('    HRESULT quad(UINT w,UINT h)')]
eff='quality=NorthlightQuality::effective(quality);'
checks['effective() applied once in loadQuality, after the QUALITY line, before configure()']=(w.count('NorthlightQuality::effective(')==1 and eff in lq
    and lq.index('logf("QUALITY %s file=%s')<lq.index('logf("QUALITY warning')<lq.index(eff)<lq.index('NorthlightDiagnostics::configure(')<lq.index('NorthlightRenderThreadProbe::configure(')
    and 'logf("QUALITY ActorShadows=%u: %s; forced off: %s",quality.actorShadows,' in lq and lq.index('NorthlightQuality::forcedOff(quality)')<lq.index(eff))
ctor=w[w.index('        loadQuality();effects.gi='):w.index('        (void)NorthlightStreaming::cpuRetirement();')]
checks['constructor reads quality only after loadQuality']='shadowFate(quality)' in ctor
# Capture decision: ActorShadows=0 is "shadows off" for capture (GI actor frames only).
dec=w[w.index('    bool modelCaptureSkipped(){'):w.index('    void captureModel(')]
checks['capture decision uses actorShadowWork']=('const bool shadows=NorthlightQuality::actorShadowWork(quality,effects.shadows);' in dec and 'captureSkipPossible(quality,shadows)' in dec
    and 'in.shadows=shadows;' in dec and 'effects.shadows' not in dec.replace('NorthlightQuality::actorShadowWork(quality,effects.shadows)',''))
# render(): the three predicates, defined once, and their ActorShadows=1 reductions.
render=w[w.index('    bool render(IDirect3DSurface9* targetSurface'):] # the last member of WorldRenderer
body=code(render)
defs={}
for name in ('freshReplays','actorShadows','replayShadows','replaysComplete'):
    m=re.findall(r'const bool '+name+r'=([^;]*);',body);assert len(m)==1,name;defs[name]=m[0]
checks['predicate definitions']=defs=={'freshReplays':'captureMode!=CaptureSkipped','actorShadows':'quality.actorShadows!=0',
    'replayShadows':'freshReplays&&actorShadows','replaysComplete':'freshReplays||!actorShadows'}
def value(expr,fresh,actor):return eval(expr.replace('&&',' and ').replace('||',' or ').replace('!',' not '),{},{'freshReplays':fresh,'actorShadows':actor})
checks['ActorShadows=1 reduces both to freshReplays; 0: consumers off, schedules complete']=all(
    value(defs['replayShadows'],f,True)==f and value(defs['replaysComplete'],f,True)==f and value(defs['replayShadows'],f,False) is False and value(defs['replaysComplete'],f,False) is True
    for f in (False,True))
# Consumers (replayShadows) and schedules/commits (replaysComplete, actorShadows).
consumers=['if(replayShadows)selectShadowReplays();','if(effects.shadows&&replayShadows)replayBoundsKick();','(replayShadows&&!timedReplay())',
           'if(effects.shadows&&replayShadows){auto proofs','if(effects.shadows&&replayShadows)pointCalculateReplayBounds();']
schedules=['!reason&&key.valid&&!pull,diagnosticCapture!=0,replaysComplete);','if(cascade==0)nearRendered=captureMode==CaptureFresh&&interval>1&&actorShadows;',
           'if(captureMode==CaptureFresh||!actorShadows)reuse.commit(interval,shadowPasses,matrices[cascade]);else captureDemand=true;',
           'renderPointShadow(replaysComplete,actorShadows);else pointReady=false;']
for needle in consumers+schedules:once(body,needle,'render()')
checks['consumers gated on replayShadows, schedules on replaysComplete']=body.count('replayShadows')==1+len(consumers) and body.count('replaysComplete')==3
checks['no bare freshReplays consumer left in render()']=body.count('freshReplays')==3 # its definition and the two predicates
# The cascade replay block: skipped whole with ActorShadows=0; terrain before it and the union after it stay.
start=render.index('            if(actorShadows){ /* 0.3.158');end=render.index('            } /* actorShadows */')
block=render[start:end]
checks['cascade replay block gated, terrain before, union after']=(render.count('if(actorShadows){')==1 and render.count('} /* actorShadows */')==1
    and code(block).count('{')==code(block).count('}')+1
    and all(x in block for x in ('replayBoundsJoin(); /* 0.3.143: first pointBounds reader */','d->SetPixelShader(replayPS);','auto replayLoop=[&](auto& split)->bool{',
        'keepReplaySplit(slot,split,','replayProbe(rows,realLoopMs);'))
    and render.index('"live terrain shadow"')<start and end<render.index('if(!check(quad(1024,1024),"shadow union"))return false;')
    and render.count('submitReplay(')==1 and start<render.index('submitReplay(')<end and render.count('replayLoop(')==2 and all(start<m.start()<end for m in re.finditer(r'replayLoop\(',render)))
# The probe moved before the live dump: legal because it never runs on a diagnostic capture frame.
checks['probe never on a diagnostic capture frame']='probeRecord=replayProbeActive()&&slot==0&&captureMode==CaptureFresh&&!diagnosticCapture;' in block
# Point cube: withReplays gates both replay loops and the schedule's replay count.
pb=pt[pt.index('    bool renderPointShadow(bool fresh=true,bool withReplays=true){'):pt.index('    bool renderPointLighting(')]
gs,ge=pb.index('            if(withReplays){\n'),pb.index('            } /* withReplays */')
cand='if(withReplays)for(size_t i=0;i<replays.size();++i){const auto& bounds=replays[i]->pointBounds;'
count='if(fresh)pointReplayCount=withReplays?replays.size():0;'
outside=(pb[:gs]+pb[ge:]).replace(cand,'').replace(count,'')
checks['point: both replay loops and the count gated on withReplays']=(pb.count(cand)==1 and pb.count(count)==1
    and gs<pb.index('for(size_t index:pointReplayCandidates[face]){const auto& p=replays[index];')<pb.index('"cube animated draw"')<ge<pb.index('// Union: min(scratch, cached static face) into the cube face.')
    and code(pb[gs:ge]).count('{')==code(pb[gs:ge]).count('}')+1
    and re.search(r'\breplays(\[|\.size\(\))',code(outside)) is None)
# Blob filter: one helper for all four draw entry points, off with ActorShadows=0 (F9 keeps its meaning).
helper='bool blobFilterActive()const{return shadowBlobs&&shadowBlobs->active()&&enabled&&effectKeys.settings.shadows&&!applied&&terrain&&!failed&&world&&world->hasContext()&&world->actorShadowsEnabled();}'
# 0.3.187: the four draw entry points share drawHook(); FrameDrawGates=0 and =1 both test the helper.
checks['blob filter: one helper, every draw through drawHook']=(r.count(helper)==1 and r.count('if(!claimed&&blobFilterActive())extensionWork("blob shadow filter",[&]{blobFilter(count,claimed);});')==1
    and r.count('if(!claimed&&drawGates.blob){stage="blob shadow filter";if(blobFilterActive())blobFilter(count,claimed);}')==1 and r.count('return drawHook(t,count,')==4
    and r.count('effectKeys.settings.shadows&&!applied&&terrain')==1 and r.count('blobClaim(count)')==1
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
    and 'unsigned blobShadowStrength()const{return quality.blobShadowStrength;}' in w and 'blobShadowStrength=0)' not in bf and 'strength(blobShadowStrength)' in bf)
checks['0.3.193 filter: Skip at 0, Faint only under a modulate blend, faint texture A8R8G8B8 MANAGED, released in the destructor']=(
    'enum class Claim {None,Skip,Faint};' in bf and 'if(strength>=100||' in bf and 'op==D3DBLENDOP_ADD&&((src==D3DBLEND_DESTCOLOR&&dst==D3DBLEND_ZERO)||(src==D3DBLEND_ZERO&&dst==D3DBLEND_SRCCOLOR))' in bf
    and 'D3DFMT_A8R8G8B8,D3DPOOL_MANAGED' in bf and '~NorthlightShadowBlobFilter(){release(faint);}' in bf and 'faintMipChain(strength)' in bf and 'return {Claim::Skip};}   // 0.3.193: cannot lighten it' in bf and 'faintFailures++<3' in bf and 'frame-faintFailedFrame<600' in bf and 'guard.p=nullptr;return {Claim::Faint,bound};' in bf and 'blob shadow draws follow BlobShadowStrength' in bf)
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
print('PASS ActorShadows wiring: predicates reduce to freshReplays at 1; at 0 GI-only capture, no replay consumer, no deferral, forced keys; 0.3.193 BlobShadowStrength wiring')
ini2=ini[ini.index(';BlobShadowStrength=50')-1500:ini.index(';BlobShadowStrength=50')+30]
assert ';BlobShadowStrength=50\n' in ini and '\nBlobShadowStrength=' not in ini and 'Allowed 0..100. 50 / 50 / 50' in ini2 and 'BlobShadowStrength   ' in readme
print('PASS BlobShadowStrength documented in the ini and the README')
