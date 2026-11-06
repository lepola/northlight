#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Bounded opt-in snapshot spool; no game/GPU/full-scene claim."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp


def sha(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def main():
    parser=argparse.ArgumentParser(__doc__);parser.add_argument('--tsan',action='store_true');args=parser.parse_args()
    source=fp.TESTS/'test_snapshot_capture_sink.cpp'
    inputs=[Path(__file__),source,fp.src('snapshot_capture_sink.h'),fp.src('snapshot_archive.h'),
            fp.src('snapshot_store.h'),fp.src('stream_device.h'),fp.SCRIPTS/'prepare_native_scene.py']
    before={str(p):sha(p) for p in inputs};records=[]
    wiring=fp.src('stream_device.h').read_text()
    assert 'SnapshotCompleted snapshotCompleted=nullptr;void* snapshotSink=nullptr;' in wiring
    start=wiring.index('void takeSnapshot(Trigger t)')
    body=wiring[start:wiring.index('void written(',start)]
    assert body.index('if(!capture(*s,t,drawOrdinal))')<body.index('if(snapshotCompleted)snapshotCompleted(')<body.index('q.reserve(')
    assert 'core.presentsRecorded.load(' in body and 'core.queryEpoch.load(' in body and 'Replayer::currentTid()' in body
    spec=importlib.util.spec_from_file_location('preparation',fp.SCRIPTS/'prepare_native_scene.py')
    preparation=importlib.util.module_from_spec(spec);spec.loader.exec_module(preparation)
    variants=[('O2',['-O2']),('O0',['-O0']),('asan',['-O1','-g','-fsanitize=address,undefined','-fno-omit-frame-pointer'])]
    if args.tsan:variants.append(('tsan',['-O1','-g','-fsanitize=thread','-fno-omit-frame-pointer']))
    with tempfile.TemporaryDirectory(prefix='snapshot-sink-') as directory:
        root=Path(directory)
        for name,flags in variants:
            executable=root/name;output=root/(name+'-output')
            command=['clang++','-std=c++17','-Wall','-Wextra','-Werror','-pthread',*flags,*fp.test_include_flags(),str(source),'-o',str(executable)]
            print('COMPILE',command,flush=True);subprocess.run(command,check=True)
            result=subprocess.run([str(executable),str(output)],capture_output=True,text=True,timeout=60)
            print(name,result.stdout,result.stderr,flush=True);result.check_returncode()
            assert 'SNAPSHOT_CAPTURE_SINK_PASS groups=7' in result.stdout
            journal=output/'roundtrip/capture.journal.jsonl'
            rows=[json.loads(line) for line in journal.read_text().splitlines()]
            assert rows[0]['kind']=='snapshot-journal' and rows[0]['trace_join_validated'] is False
            assert len(rows)==5 and rows[-1]['writer_drained'] and rows[-1]['failures']==0
            for i,row in enumerate(rows[1:-1]):
                assert row['record']==i and row['device_key']==0x123400 and row['frame']==7
                assert row['draw_ordinal']==123 and row['epoch']==3 and row['thread']==42 and row['trigger']==2
                assert 'trace_call' not in row
                metadata=preparation.inspect_snapshot(output/'roundtrip'/row['path'])
                assert metadata['draw_ordinal']==123 and metadata['copies']==3 and metadata['recorded_failures']==1
            # A writer journal is deliberately not a closed official trace index.
            fake_trace=root/'not-a-real-trace';fake_trace.write_bytes(b'negative control only')
            options=argparse.Namespace(output=root/(name+'-invalid-import'),capture_layer='above-northlight',
                scene_manifest=None,trace=fake_trace,snapshot_index=journal,retracer=None,backend_dll=None,
                northlight_dll=None,quality_ini=None,backend_kind=None)
            try:preparation.prepare(options)
            except ValueError:pass
            else:raise AssertionError('A writer journal was accepted as an exact official trace join')
            records.append({'mode':name,'command':command,'exe_sha256':sha(executable),'stdout':result.stdout,
                            'stderr':result.stderr,'journal_sha256':sha(journal)})
    assert all(sha(Path(p))==value for p,value in before.items()),'Frozen inputs changed'
    output=fp.output_dir();output.mkdir(parents=True,exist_ok=True)
    (output/'snapshot-sink-validation.json').write_text(json.dumps({'inputs':before,'inputs_unchanged':True,
        'records':records,'game_launched':False,'gpu_used':False,'producer_allocation_oracle':True,
        'trace_join_validated':False,'full_scene_fps_claimed':False},indent=2)+'\n')
    print('PASS bounded opt-in snapshot spool; strict journal rejection; production callback defaults null')


if __name__=='__main__':main()
