#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Native x86 queue ordering and fresh/already-seen SendMessage while waiting for replay."""
from pathlib import Path
import json,os,subprocess,sys,tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

if os.name!='nt':
    print('SKIP: native Windows message-pump runtime required')
    raise SystemExit(0)

with tempfile.TemporaryDirectory(prefix='northlight-winwait-') as temporary:
    for label,flags in [('O2',['-O2']),('debug',['-O1','-g'])]:
        binary=Path(temporary)/(label+'.exe')
        subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu',*flags,'-UNDEBUG','-std=c++17',
                        '-Wall','-Wextra','-Werror',*fp.include_flags(),str(Path(__file__).with_suffix('.cpp')),
                        '-luser32','-o',str(binary)],env=fp.zig_env(),check=True)
        for arguments in ([],['timed'],['seen'],['timed','seen']):
            result=subprocess.run([str(binary),*arguments],text=True,capture_output=True,timeout=30,check=True)
            data=json.loads(result.stdout)
            assert data['roundtrips']==10000 and data['pumped_sendmessages']==8
            assert data['seen_sendmessages']==('seen' in arguments)
            print(label,result.stdout.strip(),flush=True)
print('PASS native x86 queue ordering and fresh/seen SendMessage, O2/debug, ordinary/timed consumer.')
