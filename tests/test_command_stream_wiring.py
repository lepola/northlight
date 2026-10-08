#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.192 (CS) command stream, integration slice: the CommandStream key (last in Keys, 0..1, presets 1/1/1,
documented), the version banner, and that every hook outside src/stream/ is inert while the stream is inactive
(no active snapshot, null innerOf, null upIdentity, no core subtracted) so CommandStream=0 keeps the direct
path. DllMain still creates no thread. Source-text checks only; the behaviour is tested in test_game_snapshot."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import re
HERE=Path(__file__).resolve().parent
ROOT=HERE.parent
q=fp.src('quality_settings.h').read_text();r=fp.src('renderer.cpp').read_text();w=fp.src('world_renderer.h').read_text()
hooks=fp.src('stream_hooks.h').read_text();ctx=fp.src('world_context.h').read_text();disc=fp.src('celestial_disc_native.h').read_text()
ini=(ROOT/'renderer/windows-package/northlight-quality.ini').read_text();readme=(ROOT/'renderer/windows-package/README.txt').read_text()
gpu=fp.src('static_shadow_gpu.h').read_text();exp=fp.src('world_shadow_experiment.inl').read_text()
keys=q[q.index('inline const Key Keys[]={'):q.index('inline bool operator==(const Settings')]
checks={}
# 0.3.193: BlobShadowStrength is appended after it (origin[40]); test_quality_settings checks the order.
checks['quality key: in Keys before BlobShadowStrength, 0..1, presets 1/1/1, default 1, own origin slot']=(keys.rstrip().endswith('    {"CommandStream",&Settings::commandStream,0,1,{1,1,1}},\n    {"BlobShadowStrength",&Settings::blobShadowStrength,0,100,{50,50,50}},\n    {"Weather",&Settings::weather,0,1,{1,1,1}},\n    {"RainFog",&Settings::rainFog,0,2,{1,1,1}},\n    {"FogClouds",&Settings::fogClouds,0,1,{1,1,0}},\n    {"FogCloudDensity",&Settings::fogCloudDensity,0,200,{100,100,100}},\n    {"FogTemporal",&Settings::fogTemporal,0,1,{1,1,1}},\n};')
    and 'unsigned commandStream=1;' in q and 'char origin[40]=' in q and len(re.findall(r"'d'",q[q.index('char origin[40]='):].split('\n')[0]))==40)
checks['documented in the ini template (commented, with default) and the README table']=(';CommandStream=1' in ini and re.search(r'^  CommandStream +1 / 1 / 1 ',readme,re.M) is not None)
checks['WorldRenderer exposes the loaded value; the early reader uses the same loader']=('bool commandStream()const{return quality.commandStream!=0;}' in w
    and 'NorthlightQuality::load(hasFile?&in:nullptr,nullptr,problems).commandStream!=0' in hooks and 'northlight-quality.ini' in hooks)
checks['early reader is Win32-only and falls back to the direct path on any failure']=('inline bool commandStreamRequested(const wchar_t* rootPath){' in hooks and hooks.count('catch(...){return false;}')==2)
# 0.3.193: the faint blob shadows bump the version; 0.3.196: the removed DXVK 3 start-marker fallback bumps it;
# 0.3.196 (task 12): the stream is followed by the task's lookup caches and stream bookkeeping.
# 0.3.197 (task 13): the soft local-light cap and blended GI re-publication follow them.
# 0.3.198 (rain): the Forever-style rain follows the task 13 features. 0.3.199 (fog clouds): the moving fog clouds follow the rain.
checks['banner: 0.3.199 with the stream before the task 12, 13, rain and fog cloud features']=('logf("Northlight renderer 0.3.199;' in r and 'reduced terrain shadow reach under address-space pressure; command-stream replay thread; draw-hook lookup caches; lighter replay retire; fresh texture shadows evict only stale keeps; soft local-light cap with fades; blended GI re-publication; Forever-style rain (storm light bands, weather draw detection); moving fog clouds; backend=%s' in r and '0.3.191' not in r[r.index('logf("Northlight renderer'):][:200])
checks['GATE threads logs gameTid and replayTid from the hook atomics']=('frame=%u gameTid=%lu replayTid=%lu event=%s' in r and 'NorthlightStream::gameTid.load(std::memory_order_relaxed),NorthlightStream::replayTid.load(std::memory_order_relaxed),event);' in r
    and 'inline std::atomic<unsigned long> gameTid{0},replayTid{0};' in hooks)
checks['inactive: every hook defaults to off']=all(x in hooks for x in ('inline thread_local const void* upIdentity=nullptr;','inline const void* (*innerOf)(const void*)=nullptr;','inline std::atomic<bool> streamActive{false};','return innerOf&&p?innerOf(p):p;'))
checks['inactive: readSelf is the live read behind one thread_local null check']=ctx.count('NorthlightStream::activeSnapshot')==1 and 'inline thread_local GameSnapshot* activeSnapshot=nullptr;' in fp.src('snapshot_store.h').read_text() and 'return readSelfLive(address,output,size);' in ctx
checks['inactive: celestial identities pass through innerOf (identity when null) before the registry map']=('map_(inner(exposed))' in disc and disc.count('map_(inner(exposed))')==2 and 'NorthlightStream::inner(reinterpret_cast<const void*>(exposed))' in disc)
checks['core budgets read NorthlightStream::cores() at the three sites, no raw hardware_concurrency']=(
    'const unsigned prepareCores=NorthlightStream::cores();' in exp and 'NorthlightQuality::giSolverThreads(quality,NorthlightStream::cores());' in w
    and 'NorthlightStaticPlanJob::Async&&NorthlightStream::cores()>2;' in gpu
    and sum(p.read_text().count('std::thread::hardware_concurrency()') for p in fp.sources({'.h','.inl','.cpp'}))==1)
checks['UP identity: the stream value only replaces the vertex pointer in the shadow-fate key']=(w.count('NorthlightStream::upIdentity')==2 and 'NorthlightStream::upIdentity?NorthlightStream::upIdentity:userVertices' in w)
dllmain=r[r.index('BOOL WINAPI DllMain('):]
dllmain=dllmain[:dllmain.index('\n}\n')]
checks['DllMain creates no thread (loader lock)']=not any(x in dllmain for x in ('CreateThread','std::thread','_beginthread','CreateRemoteThread','QueueUserWorkItem'))
checks['stream_hooks.h starts no thread']=not any(x in hooks for x in ('CreateThread','std::thread t','_beginthread','std::async'))
create=r[r.index('HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter'):];create=create[:create.index('\n};')]
device=r[r.index('class Device final'):r.index('class Factory final')]
checks['CreateDevice: stream flag read before the real create, streamActive before the Device, MULTITHREADED added only when streaming']=(
    'const bool stream=NorthlightStream::commandStreamRequested(rootPath);' in create
    and create.index('streamActive.store(true')<create.index('real->CreateDevice(adapter,type,window,flags,pp,out)')<create.index('new Device(*out,this)')
    and 'if(stream)flags|=D3DCREATE_MULTITHREADED;' in create)
checks['CreateDevice: any stream failure logs CSTREAM disabled and returns the Device unchanged']=(
    'logf("CSTREAM disabled reason=%s",reason);return device;' in r and 'streamActive.store(false' in r and 'catch(...){reason="exception";}' in r)
checks['the Device has no CommandStream branch (the stream sits in front of it)']=('commandStream' not in device.lower() and 'NorthlightStream::StreamDevice' not in device)
checks['the owner handoff runs on the replay thread through the one Device method']=('options.threadStart=[target]{target->adoptOwnerThread();};' in r and r.count('void adoptOwnerThread(){mirrorState.gate.ownerTid=MirrorGuard::threadId();}')==1)
checks['memory pressure reaches the stream only through the hook atomic']=('NorthlightStream::memoryPressure.store(memoryCaps==1' in r and 'inline std::atomic<bool> memoryPressure{false};' in hooks)
checks['the stream\'s buffer read-backs use NorthlightUpload::readBackLock (NOOVERWRITE only on DXVK >= 3) and keep READONLY as the fallback']=('options.readBackLock=&NorthlightUpload::readBackLock;' in r and 'c.readBackLock?c.readBackLock():D3::kLockReadOnly' in ''.join(fp.src('stream_proxies.h').read_text().split()))
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
print('PASS command stream wiring: key, docs, banner, GATE fields, inert hooks, no DllMain thread')
