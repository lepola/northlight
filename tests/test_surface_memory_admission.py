# northlight-test: requires=cxx
"""Run every surface admission predicate against the production memory guard."""
from pathlib import Path
import re
import os
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

source = fp.src('renderer.cpp').read_text(encoding='utf-8')
weather = source[source.index('    void surfaceWeather()'):source.index('    void renderEffects()')]
predicates = re.findall(r'!?memoryGuard\.(?:pressure|capsHalved)\(\)', weather)
assert predicates == ['memoryGuard.capsHalved()'], 'Review the supplemental weather memory gate'
allowed = ['!(' + p + ')' for p in predicates]
harness = r'''
#include "memory_guard.h"
#include <cassert>
#include <iostream>
using namespace NorthlightMemoryGuard;
int main(){
    Guard memoryGuard;
    auto check=[&](bool expected){const bool allowed[]={__PREDICATES__};for(bool value:allowed)assert(value==expected);};
    check(true);
    // Actual player log: healthy address space, geometry waits for its larger floor.
    auto entry=memoryGuard.update(751*MiB,557*MiB,1000,768*MiB);
    assert(entry.entered&&entry.demand&&entry.trim&&memoryGuard.pressure()&&!memoryGuard.capsHalved());check(true);
    for(unsigned tick=2000;tick<120000;tick+=1000){memoryGuard.update(734*MiB,557*MiB,tick,768*MiB);check(true);}
    // Critical address space still blocks effects, including during hysteresis.
    memoryGuard.update(300*MiB,100*MiB,120000,768*MiB);assert(memoryGuard.capsHalved());check(false);
    memoryGuard.update(500*MiB,200*MiB,121000,0);check(false);
    memoryGuard.update(900*MiB,557*MiB,122000,0);check(false);
    memoryGuard.update(900*MiB,557*MiB,132000,0);assert(!memoryGuard.pressure());check(true);
    std::cout<<"Precipitation admission: production gate; sustained geometry demand, actual low memory and recovery passed\n";
}
'''.replace('__PREDICATES__', ','.join(allowed))
with tempfile.TemporaryDirectory(prefix='northlight-surface-admission-') as tmp:
    cpp = Path(tmp) / 'admission.cpp'
    exe = Path(tmp) / 'admission.exe'
    cpp.write_text(harness, encoding='utf-8')
    compiler=[str(fp.zig()),'c++','-target','x86_64-windows-gnu'] if os.name=='nt' else ['clang++']
    subprocess.run([*compiler, '-std=c++17', '-O2', '-UNDEBUG', *fp.test_include_flags(), str(cpp), '-o', str(exe)], env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], env=fp.zig_env(), check=True)
