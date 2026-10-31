#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Exercise production content-verified terrain caching without D3D/Wine/game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import ast,hashlib,json,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
tree=ast.parse((HERE/'test_terrain_snapshot.py').read_text())
text={n.targets[0].id:ast.literal_eval(n.value) for n in tree.body
      if isinstance(n,ast.Assign) and isinstance(n.targets[0],ast.Name) and n.targets[0].id in ('stub','harness')}
fixture=text['harness'].split('int main(){')[0]
fixture=fixture.replace('bool failLock=false;','bool failLock=false,failUnlock=false;UINT reportedSize=0;')
fixture=fixture.replace('out->Size=unsigned(bytes.size());','out->Size=reportedSize?reportedSize:unsigned(bytes.size());')
fixture=fixture.replace('assert(unlocks<=locks);return D3D_OK;','assert(unlocks<=locks);return failUnlock?HRESULT(0x80004005u):D3D_OK;')
fixture=fixture.replace('unsigned refs=1;\n    unsigned AddRef()', 'unsigned refs=1;std::uint16_t offset=4,stream=0;std::uint8_t type=D3DDECLTYPE_FLOAT3;\n    unsigned AddRef()')
fixture=fixture.replace('out[0]={0,4,D3DDECLTYPE_FLOAT3,0,0,0};','out[0]={stream,offset,type,0,0,0};')
fixture=fixture.replace('Declaration declaration;float view[16]={};','Declaration declaration;float view[16]={};UINT streamOffset=16,stride=24;')
fixture=fixture.replace('16+actualIndex*24+4','streamOffset+actualIndex*stride+declaration.offset')
fixture=fixture.replace('assert(stream==0);','assert(stream<=1);')
fixture=fixture.replace('*offset=16;*stride=24;','*offset=streamOffset;*stride=this->stride;')
source='#include <new>\n'+fixture+r'''
int main(){
    Device d;const Position a{-10200,-1180,3},b{-10198,-1180,4},c{-10200,-1178,5};
    auto fill=[&](){d.put(4,a);d.put(5,b);d.put(6,c);};fill();
    FrameCache cache;MeshSnapshot out;Diagnostics why;
    auto read=[&](UINT count=3,INT base=-3){return cache.readMesh(&d,D3DPT_TRIANGLELIST,base,7,count,1,1,d.view,out,&why);};
    assert(read()&&!why.contentCacheHit&&cache.persistentEntries()==1);
    assert(read()&&why.contentCacheHit&&cache.persistentHits()==1);
    out.positions[0].z=999;out.indices.set(0,999);out.bounds.chunks.clear();
    assert(read()&&why.contentCacheHit&&out.positions[0].z==3&&out.indices[0]==0&&out.bounds.chunks.size()==1);
    cache.clearFrame();assert(read()&&why.contentCacheHit); // Present does not trust identity alone.
    d.put(4,{a.x,a.y,30});assert(read()&&!why.contentCacheHit&&out.positions[0].z==30);
    assert(read()&&why.contentCacheHit); // Same interface pointer after DISCARD/mutation.
    d.vb.bytes[16+4*24+20]^=1;assert(read()&&!why.contentCacheHit); // Entire strided span checked.
    auto original=d.ib.bytes;std::uint16_t swapped[]={123,8,7,9};std::memcpy(d.ib.bytes.data(),swapped,sizeof swapped);
    assert(read()&&!why.contentCacheHit&&out.positions[0].x==b.x);d.ib.bytes=original;
    assert(read()&&!why.contentCacheHit&&out.positions[0].z==30);
    d.setIndices32();assert(read()&&!why.contentCacheHit);assert(read()&&why.contentCacheHit);
    // Contract differences force a miss even when the effective geometry matches.
    assert(read(4)&&!why.contentCacheHit);assert(read(4)&&why.contentCacheHit);
    d.put(3,c);assert(read(3,-4)&&!why.contentCacheHit);
    d.declaration.type=D3DDECLTYPE_FLOAT4;assert(read()&&!why.contentCacheHit);
    d.declaration.stream=1;assert(read()&&!why.contentCacheHit);
    d.declaration.offset=8;fill();assert(read()&&!why.contentCacheHit);
    d.stride=28;d.vb.bytes.resize(16+8*28);fill();assert(read()&&!why.contentCacheHit);
    d.streamOffset=20;d.vb.bytes.resize(20+8*28);fill();assert(read()&&!why.contentCacheHit);
    assert(read()&&why.contentCacheHit);
    const auto hits=cache.persistentHits();
    d.ib.failLock=true;assert(!read()&&why.reason==RejectReason::IndexLock);d.ib.failLock=false;
    d.ib.failUnlock=true;assert(!read()&&why.reason==RejectReason::IndexUnlock);d.ib.failUnlock=false;
    d.vb.failLock=true;assert(!read()&&why.reason==RejectReason::VertexLock);d.vb.failLock=false;
    d.vb.failUnlock=true;assert(!read()&&why.reason==RejectReason::VertexUnlock);d.vb.failUnlock=false;
    d.vb.reportedSize=32;assert(!read()&&why.reason==RejectReason::VertexRange);d.vb.reportedSize=0;
    float wrongView[16];std::memcpy(wrongView,d.view,64);wrongView[12]=20;
    assert(!cache.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,wrongView,out,&why)&&why.reason==RejectReason::ViewMismatch);
    assert(cache.persistentHits()==hits);assert(read()&&why.contentCacheHit);
    cache.clearPersistent();assert(cache.persistentEntries()==0&&cache.persistentHits()==0);
    assert(read()&&!why.contentCacheHit);
    // Warm cache still obeys per-frame capture/read budgets.
    Limits limit;limit.maxEntries=1;FrameCache capped(limit);
    assert(capped.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,out,&why));
    assert(!capped.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,out,&why)&&why.reason==RejectReason::CaptureLimit);
    capped.clearFrame();assert(capped.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,out,&why)&&why.contentCacheHit);
    limit.maxEntries=2048;limit.maxReadBytesPerFrame=100;FrameCache budget(limit);
    assert(budget.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,out,&why));
    assert(!budget.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,out,&why)&&why.reason==RejectReason::ByteBudget);
    budget.clearFrame();assert(budget.readMesh(&d,D3DPT_TRIANGLELIST,-3,7,3,1,1,d.view,out,&why)&&why.contentCacheHit);
    // Different vertex-range contracts fill the bounded slot array, under the same byte cap.
    cache.clearPersistent();
    const UINT entryLimit=UINT(FrameCache::persistentEntryLimit());
    for(UINT i=0;i<entryLimit+200;++i){cache.clearFrame();assert(read(3+i));assert(cache.persistentEntries()<=entryLimit&&cache.persistentBytes()<=32u*1024*1024);}
    assert(cache.persistentEntries()==entryLimit);cache.clearFrame();assert(read(entryLimit+202)&&why.contentCacheHit);
    cache.clearFrame();assert(read()&&!why.contentCacheHit); // Evicted oldest.
    // Wide, valid index spans exercise the memory cap before the entry cap.
    d.declaration.type=D3DDECLTYPE_FLOAT3;d.declaration.offset=4;d.declaration.stream=0;d.stride=24;d.streamOffset=16;
    d.vb.bytes.resize(16+200005u*24);d.put(4,a);d.put(100004,b);d.put(200004,c);
    std::uint32_t wide[]={4,100004,200004};d.ib.bytes.resize(sizeof wide);std::memcpy(d.ib.bytes.data(),wide,sizeof wide);
    cache.clearPersistent();
    for(UINT i=0;i<12;++i){cache.clearFrame();assert(cache.readMesh(&d,D3DPT_TRIANGLELIST,0,4,200001+i,0,1,d.view,out,&why));
        assert(cache.persistentBytes()<=32u*1024*1024);}
    assert(cache.persistentEntries()<12&&cache.persistentEntries()>0);
    assert(d.vb.locks==d.vb.unlocks&&d.ib.locks==d.ib.unlocks);
    assert(d.vb.refs==1&&d.ib.refs==1&&d.declaration.refs==1);
    // Reconstruct resources at exactly the same interface addresses. Identity
    // reuse is safe only because the new objects' current bytes are verified.
    alignas(Device) unsigned char storage[sizeof(Device)];FrameCache recycled;
    auto create=[&](float z){Device* p=new(storage)Device;p->put(4,{a.x,a.y,z});p->put(5,b);p->put(6,c);return p;};
    Device* object=create(3);
    assert(recycled.readMesh(object,D3DPT_TRIANGLELIST,-3,7,3,1,1,object->view,out,&why));
    object->Device::~Device();object=create(3);recycled.clearFrame();
    assert(recycled.readMesh(object,D3DPT_TRIANGLELIST,-3,7,3,1,1,object->view,out,&why)&&why.contentCacheHit);
    object->Device::~Device();object=create(73);recycled.clearFrame();
    assert(recycled.readMesh(object,D3DPT_TRIANGLELIST,-3,7,3,1,1,object->view,out,&why)&&!why.contentCacheHit&&out.positions[0].z==73);
    object->Device::~Device();
    std::puts("PASS content-cache hits verify current IB/VB bytes; same-pointer mutations, index LOD/format, layout/stream/base/stride/offset and immutable output");
    std::puts("PASS cache hits preserve view/range/budget/capture/lock/unlock failures; reset, bounded-entry/32MiB eviction, balanced COM/locks");
}
'''
with tempfile.TemporaryDirectory(prefix='northlight-terrain-content-') as temp:
    temp=Path(temp);(temp/'d3d9.h').write_text(text['stub']);(temp/'test.cpp').write_text(source)
    for label,flags in [('native',['-O2']),('sanitizer',['-O1','-g','-fsanitize=address,undefined'])]:
        binary=temp/label;subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-I',str(temp),*fp.test_include_flags(),str(temp/'test.cpp'),*flags,'-o',str(binary)],check=True)
        result=subprocess.run([str(binary)],capture_output=True,text=True);print(label+':\n'+result.stdout+result.stderr,end='');result.check_returncode()
report={'source_sha256':hashlib.sha256(fp.src('terrain_capture_bounds.h').read_bytes()).hexdigest(),
        'byte_verified_hit_mutation_contract_failure_lifetime_bounds_tests':True,'ASan_UBSan':True,
        'max_entries':8192,'max_accounted_cache_bytes':33554432,'game_launched':False}
(fp.output_dir()/'terrain-content-cache-validation.json').write_text(json.dumps(report,indent=2)+'\n')
