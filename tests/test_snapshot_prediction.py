#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.181 (r89 S1-S3): the snapshot cache's lookup modes on fake devices (test_snapshot_prediction.cpp).
Map, Predict and Prefetch read the same draws in lockstep and must agree on every read (result,
diagnostics, Mesh identity by creation ordinal, output Mesh, counters, LRU order) and every frame's
maintenance, over >= 2000 frames with events; the same with 4-bit hashes (same-hash replacements).
Directed liveness cases D1-D5 under ASan+UBSan; CacheKey == against memcmp()==0 for every single-byte
flip; the SnapshotMeter on a synthetic clock; hits allocate nothing. Counterfactuals must fail: CF1-CF4
(no prediction reset at an erase site: ASan heap-use-after-free in the directed cases), CF5-CF7 (a prefix
compare, prediction on untracked keys, equality without the declaration: a differential mismatch).
Plus source needles. Native code and static analysis only; no Wine, DLL or game is run."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import ast,hashlib,json,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
def literal(file,key):return next(ast.literal_eval(n.value) for n in ast.parse(file.read_text()).body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id==key for t in n.targets))
stub=literal(HERE/'test_terrain_snapshot.py','stub');prefix=literal(HERE/'test_draw_snapshot.py','harness').split('int main()')[0]
FILES=['source_identity.h','draw_snapshot.h','northlight_mem.h','world_renderer.h','test_snapshot_prediction.cpp','test_snapshot_prediction.py']
report={'scope':'Fake-device tests of draw_snapshot.h; no D3D runtime, Wine or game.',
        'source_sha256':{n:hashlib.sha256(fp.tracked(n).read_bytes()).hexdigest() for n in FILES},'runs':[]}
ASAN=['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
with tempfile.TemporaryDirectory(prefix='northlight-snapshot-prediction-') as tmp:
    t=Path(tmp);(t/'d3d9.h').write_text(stub);(t/'snapshot_harness.h').write_text('#pragma once\n'+prefix)
    def build(name,flags,werror=True):
        exe=t/name
        subprocess.run(['clang++','-std=c++17',*(['-Wall','-Wextra','-Werror'] if werror else []),*flags,'-I',str(t),*fp.test_include_flags(),str(HERE/'test_snapshot_prediction.cpp'),'-o',str(exe)],check=True)
        return exe
    for label,flags in (('O2',['-O2']),('asan+ubsan',ASAN),('hash4 O2',['-O2','-DNORTHLIGHT_SNAPSHOT_HASH_BITS=4']),('hash4 asan+ubsan',[*ASAN,'-DNORTHLIGHT_SNAPSHOT_HASH_BITS=4'])):
        run=subprocess.run([str(build(label.replace(' ','-').replace('+','-'),flags))],capture_output=True,text=True)
        print(f'{label}:\n{run.stdout}',end='',flush=True);assert run.returncode==0,(label,run.stdout,run.stderr[-3000:])
        report['runs'].append({'build':label,'stdout':run.stdout})
    counterfactuals={1:'no reset in evictOldest',2:'no reset at the revalidation erase',3:'no reset at the same-hash replacement',4:'no reset in clearSnapshotCache',
                     5:'prediction on the key bytes before the draw arguments',6:'prediction on untracked keys without revalidation',7:'equality without the declaration'}
    for cf,what in counterfactuals.items():
        liveness=cf<=4
        flags=[*(ASAN if liveness else ['-O2']),f'-DNORTHLIGHT_SNAPSHOT_COUNTERFACTUAL={cf}',*(['-DNORTHLIGHT_SNAPSHOT_HASH_BITS=4'] if cf==3 else [])]
        run=subprocess.run([str(build(f'cf{cf}',flags,werror=False)),'directed' if liveness else 'differential'],capture_output=True,text=True)
        failed=run.returncode!=0 and ('heap-use-after-free' in run.stderr if liveness else 'MISMATCH' in run.stderr)
        print(f'CF{cf} ({what}): '+('fails as required' if failed else 'NOT DETECTED'),flush=True)
        assert failed,(cf,run.returncode,run.stdout,run.stderr[-3000:])
        report['runs'].append({'counterfactual':cf,'exit_code':run.returncode})

s=fp.src('draw_snapshot.h').read_text();w=fp.src('world_renderer.h').read_text()
def body(name):
    m=re.search(r'\b'+name+r'\([^;{]*\)\s*(?:const\s*)?(?:noexcept\s*)?\{',s);i=m.end()-1;depth=0
    for k in range(i,len(s)):
        depth+=(s[k]=='{')-(s[k]=='}')
        if depth==0:return s[i:k+1]
functions={n:body(n) for n in ('evictOldest','serve','store','clearSnapshotCache','dropPrediction','shareSnapshotPool')}
def enclosing(pos):
    return next((n for n,b in functions.items() if b and s.find(b)<=pos<s.find(b)+len(b)),None)
erasers=[enclosing(m.start()) for m in re.finditer(r'sources_->entries\.(erase|clear)\(',s)]
key=s[s.index('bool operator==(const CacheKey& o)const{'):s.index('struct CacheEntry {')]
sources={p.name:p.read_text() for d in fp.SRC_DIRS for p in Path(d).glob('*') if p.suffix in ('.h','.cpp','.inl')}
checks={
 'every source-pool erase/clear is in evictOldest, serve, store or clearSnapshotCache, and each resets local prediction':
   sorted(set(erasers))==['clearSnapshotCache','evictOldest','serve','store'] and all('dropPrediction(Erase::' in functions[n] for n in set(erasers))
   and s.count('dropPrediction(Erase::')==4,
 'predicted_ is assigned only in serve, dropPrediction and empty-group joining':all(enclosing(m.start()) in ('serve','dropPrediction','shareSnapshotPool') for m in re.finditer(r'(?<!CacheEntry\* )\bpredicted_=',s)) and s.count('CacheEntry* predicted_=nullptr;')==1,
 'cross-consumer invalidation checks group generation before predicted dereference':'if(!NORTHLIGHT_SNAPSHOT_COUNTERFACTUAL&&predictionGeneration_!=sources_->generation){predicted_=nullptr;predictionGeneration_=sources_->generation;}' in functions['serve'],
 'prediction on tracked keys only (the revalidation branch stays on the map path)':'if(lookup_!=Lookup::Map&&(key.tracked||NORTHLIGHT_SNAPSHOT_COUNTERFACTUAL==6)&&predicted_)' in functions['serve'],
 'CacheKey == has no memcmp':'memcmp' not in key and 'diff|=x^y' in key,
 '__builtin_prefetch only in the one helper':sum(t.count('__builtin_prefetch') for t in sources.values())==1
   and 'inline void prefetch(const void* address){__builtin_prefetch(address);}' in s,
 'the counterfactual and hash macros default to 0':'#define NORTHLIGHT_SNAPSHOT_COUNTERFACTUAL 0' in s and '#define NORTHLIGHT_SNAPSHOT_HASH_BITS 0' in s,
 'mode: Prefetch unless a RenderProfile=1 non-sample frame (Map/Predict/Prefetch by frame%3), set at the frame\'s first read':
   'const bool metered=NorthlightRenderThreadProbe::profiling()&&!sample;' in w and 'replaySnapshots.setLookup(metered?Lookup(snapshotFrameSerial%3):Lookup::Prefetch);' in w
   and 'if(!snapshotFrameBegun)snapshotBeginFrame(sample);' in w,
 'the snapshot span covers the snapshot phase and ends at the constants phase':'snapshotSpan.end();phase.next(CaptureConstants);' in w,
 'the SNAPSHOT ab line is Diagnostics-gated, every 600 frames and at destruction':'if(NorthlightDiagnostics::enabled())logf("SNAPSHOT ab class=%s' in w
   and 'if(++snapshotReportFrames>=600){snapshotReportFrames=0;logSnapshotAb();}' in w and 'logSnapshotAb(); /* 0.3.181: the SNAPSHOT ab window at device destroy */' in w,
 'keyEqLegacyNs times the byte loop copy, not memcmp':'k.legacyNs=time([&]{return NorthlightMem::byteCompare(&a,&b,sizeof(CacheKey))==0;});' in s,
}
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values()),erasers
report['checks']=checks
for n,digest in report['source_sha256'].items():assert hashlib.sha256(fp.tracked(n).read_bytes()).hexdigest()==digest,n
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
(out/'snapshot-prediction-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS snapshot prediction: three lookup modes equal, liveness clean under ASan, seven counterfactuals fail')
