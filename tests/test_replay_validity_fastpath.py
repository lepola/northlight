#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Exact partial/full replay banks and overlay state after validity memoization."""
from pathlib import Path
import subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
out=fp.output_dir();out.mkdir(parents=True,exist_ok=True)
old=subprocess.check_output(['git','show','116d732:src/replay/replay_constant_ranges.h'],cwd=fp.REPO,text=True)
(out/'range_reference.h').write_text(old.replace('namespace NorthlightReplayConstants {','namespace ReplayRangeReference {\nusing namespace NorthlightReplayConstants;'))
for opt in ['2','0']:
    exe=out/('replay-validity-O'+opt+'.exe')
    subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O'+opt,'-UNDEBUG','-std=c++17','-I',str(out),*fp.test_include_flags(),str(fp.TESTS/'test_replay_validity_fastpath.cpp'),'-o',str(exe)],env=fp.zig_env(),check=True)
    r=subprocess.run([str(exe)],check=True,capture_output=True,text=True);(out/('O'+opt+'.log')).write_text(r.stdout);print(r.stdout,end='')
