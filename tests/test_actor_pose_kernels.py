#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig,client,stormlib
"""Exact shared pose evaluators against 1404591 and the stock-client shader corpus; optional CPU A/B."""
from pathlib import Path
import argparse,hashlib,json,struct,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
import client_fixtures
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--benchmark',action='store_true');p.add_argument('--baseline-ref',default='1404591');a=p.parse_args()
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
baseline=subprocess.check_output(['git','show',a.baseline_ref+':src/replay/actor_deformation.h'])
(out/'legacy_actor_deformation.h').write_text(baseline.decode().replace('NorthlightActorDeformation','LegacyActorDeformation'),encoding='utf-8')
client_fixtures.actor_client_programs(out)
corpus=out/'stock-vertex-corpus.bin';manifest=out/'stock-vertex-corpus.json'
if not corpus.exists() or not manifest.exists():
    fp.use_source_modules()
    from world_scene_builder import Assets
    from extract_signatures import variants
    assets=Assets(archives='stock');records=[]
    try:
        with corpus.open('wb') as output:
            prefixes=tuple('shaders\\vertex\\vs_'+model+'\\' for model in ['1_1','2_0','3_0'])
            for name in sorted(n for n in assets.providers if n.startswith(prefixes) and n.endswith('.bls')):
                for variant,_,code in variants(assets.read(name)):
                    output.write(struct.pack('<I',len(code)));output.write(code);records.append(dict(path=name,variant=variant,sha256=hashlib.sha256(code).hexdigest()))
    finally:assets.close()
    manifest.write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8')
records=json.loads(manifest.read_text(encoding='utf-8'));print('ACTOR_KERNEL_INPUTS',json.dumps(dict(baseline_sha256=hashlib.sha256(baseline).hexdigest(),corpus_sha256=hashlib.sha256(corpus.read_bytes()).hexdigest(),programs=len(records))),flush=True)
for optimization in ['-O2','-O0']:
    exe=out/('actor-pose-kernels'+optimization+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu',optimization,'-UNDEBUG','-std=c++17','-I'+str(out),*fp.test_include_flags(),str(fp.TESTS/'actor_pose_kernels.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    subprocess.run([str(exe),str(corpus),*(['benchmark'] if a.benchmark and optimization=='-O2' else [])],check=True)
