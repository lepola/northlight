# northlight-test: requires=windows-exe,zig manual
"""Paired ABBA upload measurements with one verified binary; no FPS inference."""
import argparse
import json
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
sys.path.insert(0,str(fp.TESTS))
from validate_world_cold_upload_gpu import sha,inputs


def main():
    parser=argparse.ArgumentParser(__doc__)
    parser.add_argument('--prebuilt',type=Path,required=True)
    parser.add_argument('--backend',type=Path,required=True)
    parser.add_argument('--rounds',type=int,default=3)
    args=parser.parse_args();assert args.rounds>0
    out=fp.output_dir()/'upload-performance';out.mkdir(parents=True,exist_ok=True)
    initial=inputs();frozen={str(p):sha(p) for p in (args.prebuilt,args.backend,fp.TESTS/'validate_world_cold_upload_gpu.py',Path(__file__))}
    samples=[];proofs=[]
    for round_index in range(args.rounds):
        for order,storage in enumerate(('resident','spooled','spooled','resident')):
            folder=out/f'{round_index}-{order}-{storage}';env=os.environ.copy();env['NORTHLIGHT_TEST_OUTPUT_DIR']=str(folder)
            command=[sys.executable,str(fp.TESTS/'validate_world_cold_upload_gpu.py'),'--prebuilt',str(args.prebuilt),'--backend',str(args.backend),'--storage',storage,'--available','1024']
            run=subprocess.run(command,env=env,capture_output=True,text=True)
            folder.mkdir(parents=True,exist_ok=True);(folder/'runner.log').write_text(run.stdout+run.stderr,encoding='utf-8')
            assert run.returncode==0,run.stdout+run.stderr
            proof=json.loads((folder/'cold-upload'/'validation.json').read_text(encoding='utf-8'));assert proof['pass']
            proofs.append(str(folder/'cold-upload'/'validation.json'))
            for line in proof['cases'][0]['stdout'].splitlines():
                if not line.startswith('COLD_UPLOAD_PASS '):continue
                fields=dict(re.findall(r'(\w+)=([^ ]+)',line))
                sample={'round':round_index,'order':order,'storage':storage,'warm':int(fields['warm'])}
                for key in ('uploadSubmitMs','uploadWallMs','gpuUploadTimelineMs','resetReplayMs'):sample[key]=float(fields[key])
                samples.append(sample)
            print(f'ABBA round={round_index} order={order} storage={storage} PASS',flush=True)
    summary={}
    for storage in ('resident','spooled'):
        for warm in (0,1):
            group=[x for x in samples if x['storage']==storage and x['warm']==warm];assert len(group)==2*args.rounds
            summary[f'{storage}-warm{warm}']={key:{'median':statistics.median([x[key] for x in group]),'minimum':min(x[key] for x in group),'maximum':max(x[key] for x in group),'samples':len(group)} for key in ('uploadSubmitMs','uploadWallMs','gpuUploadTimelineMs','resetReplayMs')}
    assert inputs()==initial and all(sha(Path(p))==digest for p,digest in frozen.items()),'Source/binary/backend changed during performance comparison'
    (out/'validation.json').write_text(json.dumps({'pass':True,'inputs_unchanged':True,'frozen_sha256':frozen,'same_binary':str(args.prebuilt),'backend':str(args.backend),'order':'resident/spooled/spooled/resident','rounds':args.rounds,'available_mib':1024,'units':'milliseconds','gpu_timestamp_scope':'elapsed GPU timeline including submission/Present gaps, not GPU busy time or FPS','samples':samples,'summary':summary,'provenance_reports':proofs},indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':main()
