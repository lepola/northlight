#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Execute the production early planet claim across missing late contexts and sky-read rejection."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import os, re, subprocess
import northlight_paths as fp

host = fp.src('celestial_disc_renderer.h').read_text()
claim = host[host.index('    template<class Palette> bool claimNativeDraw('):host.index('    bool claimNativeGlare(')]
owning = re.search(r'    bool owning\(\)const\{[^\n]+', host).group()
out = fp.output_dir(); out.mkdir(parents=True, exist_ok=True)
(out/'celestial_phase_methods.inc').write_text(owning+'\n'+claim)
exe = out/'celestial_phase_recovery.exe'
command=([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu'] if os.name=='nt' else ['c++'])
subprocess.run([*command, '-std=c++17', '-O2', '-UNDEBUG', *fp.include_flags(), '-I'+str(out),
                str(fp.TESTS/'celestial_phase_recovery.cpp'), '-o', str(exe)], env=fp.zig_env(), check=True)
subprocess.run([str(exe)], check=True, timeout=20)
assert 'void reset(){earlySkyOk=false;earlySkyMap.clear();' in host
renderer = fp.src('renderer.cpp').read_text()
assert 'celestialDiscs&&enabled&&debugMode==0&&worldDebug==0&&!applied&&celestialDiscs->nativeClaimPossible' in renderer
