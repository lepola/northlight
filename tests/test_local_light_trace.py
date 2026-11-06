#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Submission attribution and the production last-unpaired-batch bounds guard."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def main():
    renderer=fp.src('world_renderer.h').read_text()
    line=next(line.strip() for line in renderer.splitlines() if 'const auto& a=localScissorView.valid?' in line and 'directRects[index+1]' in line)
    assert line.count('localScissorView.valid&&pair?')==1
    # Execute the production reference expressions against an independently
    # checked array for every supported count and wide-shader/view combination.
    control=r'''
#include <array>
#include <cassert>
#include <cstdio>
struct Rect {long left=0,top=0,right=10,bottom=10;};
struct Checked {std::array<Rect,17> rows{};const Rect& operator[](unsigned i)const{assert(i<rows.size());return rows[i];}};
int main(){struct {Checked directRects;} atmosphere;struct {bool valid;} localScissorView;
for(unsigned size=1;size<=136;++size)for(bool view:{false,true})for(bool shader:{false,true}){
 localScissorView.valid=view;for(unsigned first=0;first<size;){const unsigned index=first/8;const bool pair=shader&&first+8<size;const Rect whole{};
 PRODUCTION
 assert(a.right==10&&b.right==10);first+=pair?16:8;}}
std::puts("All136counts x view/wide-shader availability pass");}
'''
    guards={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in [fp.src('world_renderer.h'),fp.src('local_light_trace.h'),Path(__file__),fp.TESTS/'test_local_light_trace.cpp']}
    with tempfile.TemporaryDirectory(prefix='northlight-light-trace-') as tmp:
        tmp=Path(tmp);bounds=tmp/'bounds.cpp';bounds.write_text(control.replace('PRODUCTION',line))
        for name,source in [('trace',fp.TESTS/'test_local_light_trace.cpp'),('bounds',bounds)]:
            for mode,flags in [('O2',['-O2']),('asan',['-O1','-g','-fsanitize=address,undefined'])]:
                exe=tmp/(name+'-'+mode)
                subprocess.run(['clang++','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(source),'-o',str(exe)],check=True)
                print(mode,subprocess.check_output([str(exe)],text=True),end='',flush=True)
        bounds.write_text(control.replace('PRODUCTION',line.replace('localScissorView.valid&&pair?','localScissorView.valid?')))
        exe=tmp/'old-bounds';subprocess.run(['clang++','-std=c++17','-UNDEBUG','-O2',str(bounds),'-o',str(exe)],check=True)
        assert subprocess.run([str(exe)],stdout=subprocess.PIPE,stderr=subprocess.PIPE).returncode!=0,'Original unpaired access was not rejected'
    assert guards=={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in guards}
    print('Original unpaired reference rejected; production inputs unchanged')
if __name__=='__main__':main()
