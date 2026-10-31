#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.209 (task 29) actor shadow map: native model test of actor_shadow_map.h (clang++, plain and ASan/UBSan) and a wiring
audit of world_renderer.h / quality_settings.h (static source analysis): the actor map is drawn in the near
map's iteration (cascade 0, Render only) and reused with it, only cascade 0 skips replays inside the inset, ActorShadowDetail=0
(or ActorShadows=0) draws nothing and WorldLighting reads a neutral white, s12 carries the actor map for the ActorShadow passes and the
visibility for the WorldLighting loop only (0.3.214: the water appearance pass that also received the maps is gone). No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
w=fp.src('world_renderer.h').read_text();q=fp.src('quality_settings.h').read_text()
h=fp.src('actor_shadow_map.h').read_text()
render=w[w.index('    bool render(IDirect3DSurface9* targetSurface'):]
checks={}
checks['constants: Radius 12, Inset .80, Size 1024; unsnapped centre (XY from the leashed centre, depth from the pivot); matrix rows from shadowMatrixFrom']=('inline constexpr float Radius=12.f;' in h and 'inline constexpr float Inset=.80f;' in h and 'inline constexpr unsigned Size=1024;' in h
    and 'f.cx=dot(center,f.x);f.cy=dot(center,f.y);' in h and 'NorthlightWorldMath::shadowMatrixFrom(frame(center,pivot,sun),Radius,m);' in h
    and 'if(!(distance>Leash))return std::fabs(dz)>Leash?center+f.z*dz:center;' in h and 'if(!(distance<=JumpDistance))return pivot;' in h
    and 'dot(delta,delta)>JumpDistance' not in h) # the jump test is light-space XY (a 3D one re-centred while flying under a high sun)
# Own Keys entry only; its position in Keys[] is test_quality_docs.py's business.
checks['quality: ActorShadowDetail 0..1, presets 1/1/0, actorShadowMap needs ActorShadows']=(re.search(r'\{\s*"ActorShadowDetail"\s*,\s*&Settings::actorShadowDetail\s*,\s*0\s*,\s*1\s*,\s*\{\s*1\s*,\s*1\s*,\s*0\s*\}\s*\}',q) is not None
    and 'inline bool actorShadowMap(const Settings& s,bool shadows){return actorShadowWork(s,shadows)&&s.actorShadowDetail;}' in q and 'unsigned actorShadowDetail=1;' in q)
# The actor map keeps the raw pivot and the committed near-cascade direction.
# The cascade anchor is computed first so transition travel and projection agree.
raw=render.index('NorthlightActorShadowMap::leash(actorCenter[source],actorCenterValid[source],pivot,rawSourceDirections[source])')
checks['matrix from the raw pivot and continuous source direction; off = nothing computed']=(render.index('cascadeAnchor.update(')<render.index('f.state.request(')<raw
    and 'const bool actorMapOn=NorthlightQuality::actorShadowMap(quality,effects.shadows)&&actorShadowPS&&actorTilesPS&&actorShadow[0]&&actorShadow[1]&&actorTiles[0]&&actorTiles[1]&&actorTilesMid&&actorVisibility;' in render
    and 'actorFrameOn[source]=NorthlightActorShadowMap::finiteMatrix(actorFrameMatrix[source]);if(!actorFrameOn[source])actorCenterValid[source]=false;' in render
    and 'NorthlightActorShadowMap::matrix(actorCenter[source],pivot,rawSourceDirections[source],actorFrameMatrix[source]);' in render
    and 'if(anchorNewMap)actorCenterValid[source]=false;' in render and render.index('if(anchorNewMap)actorCenterValid[source]=false;')<raw
    and 'actorCenterValid[s]=false;' in w and 'actorCenterValid[0]=actorCenterValid[1]=false;' in w and w.count('actorCenterValid[0]=actorCenterValid[1]=false;')==1) # releaseGPU (reset() calls it)
checks['leash: 1.5 yd, 40 yd jump re-centre; diagnostic dump s%d-actor']=('inline constexpr float Leash=1.5f;' in h and 'inline constexpr float JumpDistance=40.f;' in h
    and 'std::snprintf(name,sizeof name,"s%d-actor",source);NorthlightWorldDiagnostics::dump(d,actorShadowSurface[source]' in w)
checks['a source without a near map keeps no actor map']='if(!actorMapOn||!sourceActive[source]){actorValid[source]=false;actorHold[source].invalidate();}' in render
# Pairing: invalid when the near map is redrawn, valid only after the actor draw; a reused/deferred near map continues before any of it.
loop=render[render.index('        for(int source=0;source<2;++source){\n          if(!effects.shadows||!sourceActive[source])continue;'):]
reuse=loop.index('if(action!=NorthlightQuality::CascadeAction::Render){')
begin=loop.index('reuse.begin(interval);')
invalid=loop.index('if(cascade==0)actorValid[source]=nearActorSkip[source]=false;')
draw=loop.index('if(cascade==0&&actorFrameOn[source]){')
valid=loop.index('memcpy(actorMatrices[source],am,64);actorValid[source]=true;')
union=loop.index('if(!check(quad(1024,1024),"shadow union"))return false;')
checks['paired with the near map: reuse/defer continues before it, Render invalidates, the actor draw precedes the union and commit']=(
    reuse<begin<invalid<draw<valid<union and loop.index('continue;',reuse)<begin and 'actorMatrices' not in loop[reuse:begin]
    and 'reuse.commit(interval,shadowPasses,matrices[cascade])' in loop and valid<loop.index('reuse.commit(interval,shadowPasses,matrices[cascade])'))
actor=loop[draw:loop.index('} /* actorShadows */')]
checks['actor draw: own target, shared 1024 depth, cleared white/1, cascade replay machinery, actor matrix, exactly the near-skipped replays']=(
    'actorShadowSurface[source]' in actor and 'd->SetDepthStencilSurface(shadowDepth);' in actor and '0xffffffff,1,0' in actor
    and 'replayProjection(context.inverseView,am,actorRows)' in actor and 'submitReplay(p.get(),actorBindings,actorPoses,actorRows,' in actor
    and 'if(nearSkip)for(const uint32_t index:actorReplays){const auto& p=replays[index];' in actor and 'for(size_t index=0;index<replays.size();++index)' not in actor and 'staticProofMask' not in actor and 'actorFrameMatrix[source]' in actor and 'const float* am=actorFrameMatrix[source];' in actor)
# The near skip: cascade 0 only, valid bounds only (insideInset rejects invalid bounds), actor matrix.
checks['near skip: cascade 0 only, never the far map, by the inset test on valid bounds']=(
    'bool nearSkip=cascade==0&&actorFrameOn[source];' in loop and 'if(nearSkip){actorReplays.clear();try{actorReplays.reserve(replays.size());}catch(...){nearSkip=false;}}' in loop
    and 'NorthlightActorShadowMap::insetExtent(p->pointBounds.valid,p->pointBounds.low,p->pointBounds.high,matrix,NorthlightActorShadowMap::Stay);' in loop
    and 'if(inActor){actorReplays.push_back(uint32_t(index));++nearSkipped;' in loop and 'extent<=NorthlightActorShadowMap::Inset' in loop
    and loop.count('insideInset(')==1 and 'insideInset(true,p->captureRoot,p->captureRoot,actorFrameMatrix[source])){if(p->shared)side.invalidNear();else side.unsupportedNear();}' in loop and 'if(!valid||!low||!high||!m)return false;' in h
    and h.count('std::isfinite(m[i])')==1 and h.index('std::isfinite(m[i])')<h.index('inline bool insideInset(')) # the matrix once per frame (finiteMatrix), not per replay
# A near map drawn with the skip lacks those casters: reusable only while its actor map is valid (the source went inactive, the detail went off).
checks['near map with skipped casters is redrawn, not reused, once its actor map is gone']=(
    'const bool actorLost=cascade==0&&nearActorSkip[source]&&!actorValid[source];' in loop
    and 'NorthlightQuality::cascadeAction(reuse,interval,shadowPasses,!reason&&key.valid&&!pull&&!actorLost,' in loop
    and loop.index('const bool actorLost=')<reuse
    and 'if(cascade==0)actorValid[source]=nearActorSkip[source]=false;' in loop and 'if(cascade==0)nearActorSkip[source]=nearSkip;' in loop
    and 'nearShadow[s].invalidate();actorValid[s]=false;nearActorSkip[s]=false;' in w)
checks['profile marks: the near replays before the actor map have their own name (SunNear/MoonNear stay unique)']=(
    'profile->mark(source==0?"SunNearReplays":"MoonNearReplays")' in loop and loop.count('"SunNear"')==2)
# ActorShadowDetail=0 / ActorShadows=0: the pass is never drawn; the visibility is still cleared white every frame.
light=render[render.index('actor visibility (half res, r sun, g moon)'):render.index('if(profile)profile->mark(anyShadowTransition?"WorldLightingFade":"WorldLighting");')]
checks['visibility target and clear only on a frame with an ActorShadow pass (a valid actor map of a drawn, active source); otherwise neutralWhite']=(
    light.index('actorPass[source]=!(')<light.index('if(anyActorPass){')<light.index('if(!check(d->SetRenderTarget(0,actorVisibilitySurface),"actor visibility target")||!check(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xffffffff,1,0),"clear actor visibility"))return false;\n        }')
    and '||!effects.shadows||!sourceActive[source]||!actorValid[source]||!actorShadow[source]||!actorShadowPS||!actorTilesPS||!actorTiles[source]||!actorTilesMid||!actorVisibility);' in light
    and 'if(!actorPass[source])continue;' in light and light.count('0xffffffff')==1)
checks['every ActorTiles pass (own 64x64 target per source, own mark) before the visibility clear, so the visibility target is cleared and drawn without a switch; tiles on s10 (restored after)']=(
    light.index('d->SetPixelShader(actorTilesPS);')<light.index('if(!check(quad(tiles,tiles),"actor shadow tile pass"))return false;')<light.index('if(profile)profile->mark("ActorTiles");')
    <light.index('"clear actor visibility"')<light.index('d->SetTexture(12,actorShadow[source]);d->SetTexture(10,actorTiles[source]);')<light.index('d->SetPixelShader(actorShadowPS);')
    and light.index('d->SetRenderTarget(0,actorTilesMidSurface)')<light.index('if(!check(quad(mid,mid),"actor shadow tile pass"))return false;')<light.index('d->SetRenderTarget(0,actorTilesSurface[source])')
    <light.index('d->SetTexture(12,actorTilesMid);d->SetPixelShaderConstantF(72,midInfo,1);')<light.index('if(!check(quad(tiles,tiles),"actor shadow tile pass"))return false;') # two 4x4 steps: 1024 -> 256 -> 64
    and light.count('SetRenderTarget(0,actorVisibilitySurface)')==1
    and '!optional(tiles,tiles,D3DFMT_R32F,&actorTiles[0],&actorTilesSurface[0])||!optional(tiles,tiles,D3DFMT_R32F,&actorTiles[1],&actorTilesSurface[1])' in w
    and 'if(anyActorPass)d->SetTexture(10,textures[10]);' in light and light.index('d->SetPixelShader(actorShadowPS);')<light.index('if(anyActorPass)d->SetTexture(10,textures[10]);'))
checks['ActorShadow pass: c68..c71 matrix, c72 info, actor map on s12, RED sun / GREEN moon, mask restored']=(
    'SetPixelShaderConstantF(68,actorMatrices[source],4)' in light and 'SetPixelShaderConstantF(72,actorInfo,1)' in light and 'const float actorInfo[4]={1.f,1.f/float(NorthlightActorShadowMap::Size),NorthlightActorShadowMap::Radius,0.f};' in light
    and 'd->SetTexture(12,actorShadow[source]);d->SetTexture(10,actorTiles[source]);' in light and 'source?D3DCOLORWRITEENABLE_GREEN:D3DCOLORWRITEENABLE_RED' in light and 'D3DRS_COLORWRITEENABLE,15' in light)
checks['s12: visibility for the WorldLighting loop only, then the frame-start (null) binding; later readers rebind baselineLight']=(
    light.index('d->SetTexture(12,anyActorPass?actorVisibility:neutralWhite);')<light.index('d->SetPixelShader(lightingPS);')
    and light.rstrip().endswith('d->SetTexture(12,textures[12]); /* 0.3.209 (task 29): the frame-start binding (null) again; the visibility was WorldLighting\'s only */')
    and 'textures[]={foldScene?foldScene:color,depth,shadow[0],shadow[1],probe[0],probe[1],probe[2],probe[3],nullptr,nullptr,probe[4],waterMask,nullptr,' in render
    and render.count('d->SetTexture(12,baselineLight)')==2)
checks['resources: maps, tiles and visibility only with the shaders (ActorShadowDetail=1) and optional (a failure turns the map off, not the renderer); neutral white mandatory 1x1; released with the rest']=(
    "releaseActorTargets();logf(\"WORLD actor shadow map disabled: render targets unavailable\");" in w and 'actorVisibility,&actorVisibilitySurface))return false;' not in w
    and '||!optional(w/2,h/2,D3DFMT_A8R8G8B8,&actorVisibility,&actorVisibilitySurface)' in w
    and 'if(quality.actorShadows&&quality.actorShadowDetail&&(FAILED(d->CreatePixelShader(kActorShadowShader,&actorShadowPS))||FAILED(d->CreatePixelShader(kActorTilesShader,&actorTilesPS))))' in w
    and 'void releaseActorTargets(){for(auto& t:actorShadow)drop(t);for(auto& t:actorShadowSurface)drop(t);for(auto& t:actorTiles)drop(t);for(auto& t:actorTilesSurface)drop(t);drop(actorTilesMid);drop(actorTilesMidSurface);drop(actorVisibility);drop(actorVisibilitySurface);drop(actorShadowPS);drop(actorTilesPS);}' in w
    and 'releaseActorTargets();drop(neutralWhite);' in w and '*static_cast<DWORD*>(lock.pBits)=0xffffffffu;neutralWhite->UnlockRect(0);' in w
    and 'nearShadow[s].invalidate();actorValid[s]=false;' in w)
# Hysteresis hold and the near-first bounds order: wired once, gated diagnostics, shown on the actor map log line.
hold=fp.src('actor_side_hold.h').read_text();job=fp.src('replay_bounds_job.h').read_text();near=fp.src('replay_bounds_near.h').read_text();point=fp.src('world_point_rendering.inl').read_text()
checks['hold: per-source table, begun with the diagnostics flag (or invalidated when the pass does not run); skinned casters take the root or the valid bounds centre as identity, others today\'s rule']=(
    'NorthlightActorSideHold::Hold actorHold[2];' in w and 'if(!actorMapOn||!sourceActive[source]){actorValid[source]=false;actorHold[source].invalidate();}' in w and 'if(nearSkip)actorHold[source].begin(sideDiag,unsigned(frames)+1);else if(cascade==0)actorHold[source].invalidate();' in loop
    and 'if(!p->shadowSkinned)inActor=extent<=NorthlightActorShadowMap::Inset;' in loop and 'const float* position=p->shadowSkinned?p->position():nullptr;' in loop and 'const std::uint64_t model=NorthlightActorSideHold::holdModel(*p);float centre[3];const float* at=position;' in loop and 'return p.captureRootValid?h:~h;' in hold and loop.count('holdModel(*p)')==2 and 'if(!p->captureRootValid){' in loop and 'if(!at){for(unsigned k=0;k<3;++k)centre[k]=.5f*(p->pointBounds.low[k]+p->pointBounds.high[k]);at=centre;}}' in loop
    and 'inActor=actorHold[source].decide(model,at,extent);' in loop and loop.count('actorHold[source].decide(')==1
    and 'if(inActor&&position)actorHold[source].stamp(model,position,matrix,extent);' in loop and 'else if(p->shadowSkinned&&!p->pointBounds.valid&&p->shared){' in loop
    and 'inActor=actorHold[source].carry(NorthlightActorSideHold::holdModel(*p),position,matrix);' in loop and loop.count('actorHold[source].carry(')==1)
checks['hold diagnostics: overflow and band counters only when the pass was begun with diagnostics']=(
    'void begin(bool diagnostics=false,unsigned realFrame=0){advance(1);real_=realFrame?realFrame:frame_;diagnostics_=diagnostics;bandHeld=0;carried=0;}' in hold and 'if(diagnostics_)++overflows;' in hold and 'if(diagnostics_&&!inside)++bandHeld;' in hold)
checks['actor map log line: hold band, overflows, near list size and near walk stops']=(
    ' holdBand=%u holdOverflows=%lu nearListSize=%u nearWalkStops=%lu sideFlipsRootedSinceLog=%u sideFlipsUnrootedSinceLog=%u invalidBoundsNearSinceLog=%u unrootedNoBounds=%u unrootedNoBoundsSinceLog=%u selfUnrooted=%u selfNoBoundsFramesSinceLog=%u holdCarried=%u holdCarriedTotal=%lu"' in loop
    and 'actorHold[source].bandHeld,actorHold[source].overflows,replayNearSize,replayNearStops,' in loop and 'actorSide[source].sideFlipsRooted,actorSide[source].sideFlipsUnrooted,' in loop and 'actorHold[source].carried,actorHold[source].carriedTotal);' in loop
    and 'if(NorthlightDiagnostics::enabled()){replayNearSize=unsigned(replayBoundsWorker.nearList().size());replayNearStops+=result.totals.nearStops;}' in point)
checks['bounds near-first: the list is filled by gather with the items, walks rotate per phase and take at most half of a phase budget']=(
    'priority->consider(i,p.position(),p.selfShadow)' in job and 'schedule(*cache_,items_,inverse_,&cancel_,priority_.on?&priority_:nullptr)' in job
    and 'halfLeft(budget.operations,budget.maxOperations)' in job and 'halfLeft(budget.vertices,budget.maxVertices)' in job and 'used+(max>used?(max-used)/2:0)' in job and 'cache.phaseNanoseconds(phase)<until' in job and job.count('nearWalk(')==3 and 'NorthlightReplaySchedule::pass(index.size(),list->cursor[phase][r]%index.size()' in near
    and not re.search(r'\bnear[(=]',job+near+hold)) # near is a Windows macro
rigidm=fp.src('world_rigid_memory.inl').read_text()
checks['position without bounds: the palette root, or the origin of an injected rigid prop (not a palette root, never mixed into captureRoot)']=(
    'float anchor[3]={};bool anchorValid=false;' in w and 'const float* position()const{return captureRootValid?captureRoot:anchorValid?anchor:nullptr;}' in w
    and 'p->captureRootValid=rooted;if(rooted)std::memcpy(p->captureRoot,root,sizeof p->captureRoot);p->anchorValid=false;' in w
    and 'p->captureRootValid=false;p->selfShadow=false;std::memcpy(p->anchor,e.world+9,sizeof p->anchor);p->anchorValid=true;' in rigidm)
checks['near list: built from the actor maps as last drawn and the selection\'s self; rotating pass leaves listed packets to the walks once their share is spent, a packet over the share is delisted']=(
    'nearList().begin(actorMatrices[0],actorValid[0],actorMatrices[1],actorValid[1])' in point
    and 'item.outer=priority&&priority->consider(i,p.position(),p.selfShadow);' in job and 'skipOuter&&items[index].outer?false:visit(index,phase)' in job
    and 'const bool skipOuter=priority&&priority->skipOuter();' in job and job.count('rotate(index,')==3 and 'inline constexpr float Limit=NorthlightActorShadowMap::Stay+(NorthlightActorShadowMap::Leash+CasterYards)/NorthlightActorShadowMap::Radius;' in near)
sel=fp.src('world_shadow_experiment.inl').read_text()
checks['ring 0 is the self: the selection marks the self\'s draws every frame (not only with diagnostics), injected props never inherit the mark']=(
    'for(const auto& a:actorShadowScratch.actors)if(a.self)for(size_t i=a.first;i<a.first+a.count;++i)replays[actorShadowDraws[i].index]->selfShadow=true;' in sel
    and 'if(NorthlightDiagnostics::enabled())for(const auto& a:actorShadowScratch.actors)if(a.self)' not in sel and 'p->selfShadow=false;std::memcpy(p->anchor' in rigidm)
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-actormap-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/'test_actor_shadow_map.cpp'),str(fp.src('world_gi.cpp')),'-o',str(exe)],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS actor shadow map: model and wiring')
