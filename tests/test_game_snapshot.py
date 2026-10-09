#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.192 (CS) game-memory snapshot: native test of snapshot_store.h / game_snapshot.h / shader_tags.h /
stream_hooks.h (clang++, plain and ASan/UBSan) and a wiring audit: readSelf serves from the active snapshot
and is the old live read otherwise, the raw readers are injected with that reader, the verify*/supported
checks are not captured, and Device::CreateVertexShader uses the shared tag function. No game or GPU."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import subprocess,tempfile
HERE=Path(__file__).resolve().parent
ctx=fp.src('world_context.h').read_text();snap=fp.src('game_snapshot.h').read_text();store=fp.src('snapshot_store.h').read_text()
r=fp.src('renderer.cpp').read_text();wr=fp.src('world_renderer.h').read_text();hooks=fp.src('stream_hooks.h').read_text();tags=fp.src('shader_tags.h').read_text()
checks={}
checks['readSelf: one thread_local null check, else the unchanged live read']=('inline bool readSelfLive(uintptr_t address,void* output,size_t size) {\n    SelfReadStats::calls' in ctx
    and 'if(NorthlightStream::GameSnapshot* snapshot=NorthlightStream::activeSnapshot)\n        return NorthlightStream::snapshotRead(*snapshot,address,output,size,readSelfLive,NorthlightStream::codeRange());\n    return readSelfLive(address,output,size);' in ctx
    and 'inline thread_local GameSnapshot* activeSnapshot=nullptr;' in store)
checks['capture runs the raw readers with the injected readSelf, recording while ScopedRecording is active']=(
    'captureReaders(NorthlightWorldContext::readSelf);' in snap and 'ScopedRecording recording(s);' in snap
    and all(x in snap for x in ('readCurrent(read,','readCurrentBasis(read,','readSnapshot(read,sky)','NorthlightCelestialDisc::readIdentities(read)','NorthlightCelestialGlare::readIdentities(read)','read(0xd38b00,first,sizeof first);read(0xd38b00,second,sizeof second)','readMapAndCamera(map,camera)')))
checks['the verify*/supported code checks are not captured']=not any(x in snap.replace('verify*/supported','') for x in ('verifyCode','verifyIdentityCode','verifySignatures','supportedClient','NorthlightCelestialGlare::verify','failingSignature'))
checks['playback and recording are scoped to the thread, never process-wide']=('ScopedPlayback' in store and 'class ScopedRecording' in store and 'activeSnapshot=previous_;' in store and 'std::atomic<GameSnapshot' not in store)
checks['Device tags through the shared function; shaderHash through the shared FNV']=('int tag=NorthlightShaderTags::deviceTag(h);' in r and 'contains(kTerrainVS,h)?1:contains(kUiVS,h)?2:0' not in r
    and 'return NorthlightShaderTags::fnv1a(words.data(), size);' in r and '#include "shader_tags.h"' in r)
sd=fp.src('stream_device.h').read_text();sp=fp.src('stream_proxies.h').read_text()
checks['learned UI VS: onDraw uses drawTags with the relaxed atomic; CreateVertexShader hashes once; the atomic lives in stream_hooks.h']=(
    'drawTags(v->tags,v->hash,learnedUiVsHash.load(std::memory_order_relaxed))' in sd and 'p->hash=NorthlightShaderTags::fnv1a(code,tokens*4);p->tags=NorthlightShaderTags::triggerTags(p->hash);' in sd
    and 'std::uint64_t hash=0;' in sp and 'inline std::atomic<std::uint64_t> learnedUiVsHash{0};' in hooks and 'inline unsigned drawTags(' in snap)
checks['shader_tags.h: static tables only, no device, no game memory']=all(x not in tags for x in ('readSelf','d3d9','windows.h','GetFunction'))
checks['stream_hooks.h: UP identity thread_local, innerOf null by default, core budget subtracts one only when active']=(
    'inline thread_local const void* upIdentity=nullptr;' in hooks and 'inline const void* (*innerOf)(const void*)=nullptr;' in hooks
    and 'hw>1&&streamActive.load(std::memory_order_relaxed)?hw-1:hw;' in hooks)
checks['no hardware_concurrency left outside the one budget function']=sum(p.read_text().count('std::thread::hardware_concurrency()') for p in fp.sources({'.h','.inl','.cpp'}))==1
checks['the UP cache identity uses upIdentity when set, the data still comes from the passed pointer']=('key.vb=reinterpret_cast<std::uintptr_t>(NorthlightStream::upIdentity?NorthlightStream::upIdentity:userVertices);' in wr and 'readUP(p->decl,draw,userIndices,userFormat,userVertices,userStride' in wr)
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
with tempfile.TemporaryDirectory(prefix='northlight-snapshot-') as tmp:
    for label,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
        exe=Path(tmp)/('test-'+label)
        subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-UNDEBUG',*flags,*fp.test_include_flags(),str(HERE/'test_game_snapshot.cpp'),'-o',str(exe),'-pthread'],check=True)
        print(label,subprocess.check_output([str(exe)],text=True),end='',flush=True)
print('PASS game snapshot: record, FIFO playback, fallbacks, triggers, VS tags')
