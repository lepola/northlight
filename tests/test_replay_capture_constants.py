#!/usr/bin/env python3
# northlight-test: requires=cxx
"""0.3.180 (C0-C2) capture constants. Native tests of replay_capture_constants.h at -O2, under
ASan/UBSan and in the NORTHLIGHT_PALETTE_VALIDATE build: test_replay_capture_constants.cpp (the
templated captureBlocks() through ReaderSource and ClockSource bit-equal to the 0.3.179 Reader
overload, C0's verify() cadence, the counterfactual sources) and test_replay_palette_blocks.cpp (the
Reader overload against full copies). The C2 counterfactual: a fetch that writes the bank must trip the
validation build's assertion, on the block and the full-fetch path. Plus a source audit of the
renderer wiring. Native code and static analysis only; no Wine, Windows binary or game is run."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib,json,re,subprocess,tempfile
HERE=Path(__file__).resolve().parent
FILES=['replay_capture_constants.h','captured_constant_epoch.h','world_renderer.h','renderer.cpp','device_mirror.h',
       'test_replay_capture_constants.cpp','test_replay_palette_blocks.cpp','test_replay_capture_constants.py']
report={'scope':'Native replay_capture_constants.h tests plus a renderer wiring audit; no game, Wine or DLL run.',
        'source_sha256':{n:hashlib.sha256(fp.tracked(n).read_bytes()).hexdigest() for n in FILES},'runs':[]}
BUILDS=[('O2',['-O2']),('asan+ubsan',['-O1','-g','-fsanitize=address,undefined','-fno-sanitize-recover=all']),
        ('validate',['-O2','-DNORTHLIGHT_PALETTE_VALIDATE=1'])]
with tempfile.TemporaryDirectory(prefix='northlight-capture-constants-') as tmp:
    for test in ('test_replay_capture_constants','test_replay_palette_blocks'):
        for label,flags in BUILDS:
            exe=Path(tmp)/f'{test}-{label}'
            subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(HERE/f'{test}.cpp'),'-o',str(exe)],check=True)
            out=subprocess.run([str(exe)],check=True,capture_output=True,text=True).stdout
            print(f'{test} {label}:\n{out}',end='',flush=True);report['runs'].append({'test':test,'build':label,'stdout':out})
            if test=='test_replay_capture_constants' and label=='validate':
                for mode in ('c2-race-block','c2-race-full'):
                    bad=subprocess.run([str(exe),mode],capture_output=True,text=True)
                    tripped=bad.returncode!=0 and re.search(r'Assertion[^\n]*failed',bad.stderr)
                    print(f'C2 counterfactual {mode}: '+('assertion tripped' if tripped else 'NOT DETECTED'),flush=True)
                    assert tripped,(mode,bad.returncode,bad.stdout,bad.stderr)
                    report['runs'].append({'test':test,'build':label,'counterfactual':mode,'exit_code':bad.returncode})

read=lambda n:fp.tracked(n).read_text()
w,r,c=read('world_renderer.h'),read('renderer.cpp'),read('replay_capture_constants.h')
templated=c[c.index('template<class Replay,class Source,class Fetch,class FetchFloats> bool captureBlocks('):c.index('template<class Replay> bool samePose(')]
checks={
 'C1: the renderer reads the mirror clock in place (no Reader callback)':'world->setConstantEpochSource({&mirrorState.constantEpoch,&mirrorState});' in r
   and 'constantEpochReader' not in w and 'NorthlightReplayCaptureConstants::ClockSource<DeviceMirror> constantEpochSource;' in w
   and 'captureBlocks(*p,previous,constantEpochSource,[&]{' in w,
 'C0: profile Stats on sample frames, self-check by its own flag and serial':'},sample?&constantEpochProfile:nullptr,constantSelfCheck,constantSelfCheckState))return;' in w
   and 'NorthlightReplayCaptureConstants::SelfCheck constantSelfCheckState{0,&constantEpochStats};' in w,
 'C0: the serial is cleared with the self-check object':'constantEpochStats={};constantEpochProfile={};constantSelfCheckState.serial=0;' in w,
 'C0: samePose keeps its 0.3.179 Stats':'samePose(*replays.back(),*p,sample?&constantEpochStats:nullptr)' in w,
 'C0: verify() on a block hit only by the self-check flag and serial (or the validation build)':
   'if(ValidateEvery||(selfCheck&&SelfCheckInterval&&check.serial%SelfCheckInterval==0))verify(p,fetchFloats,check.stats);' in templated
   and 'stats->blockTests%' not in templated,
 'C2: no post-fetch re-read outside the validation assertions':templated.count('source.valid()')==2 and templated.count('source.snapshot()')==6
   and templated.count('if constexpr(ValidateEvery)assert(')==2 and 'raced' not in templated,
}
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
report['checks']=checks
for n,digest in report['source_sha256'].items():assert hashlib.sha256(fp.tracked(n).read_bytes()).hexdigest()==digest,n
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
(out/'capture-constants-validation.json').write_text(json.dumps(report,indent=2)+'\n')
print('PASS capture constants: templated captureBlocks bit-equal to the 0.3.179 path through both sources; C0 cadence; C2 assertions')
