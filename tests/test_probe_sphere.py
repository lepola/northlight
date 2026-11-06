#!/usr/bin/env python3
# northlight-test: requires=cxx
"""Cached Fibonacci sampling preserves the sphere and bounds rotation roundoff."""
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp
s=fp.src('world_gi.cpp').read_text()
sphere=s[s.index('struct ProbeSphere {'):s.index('// Shared by the direct and replayed moving solves')]
source=r'''
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include "gi_trig.h"
using NorthlightGI::sincosAngle;
struct Vec3{float x,y,z;};
'''+sphere+r'''
int main(){
 ProbeSphere table;float worst=0;
 for(unsigned count:{6u,16u,32u,48u,64u,16u}){
  table.prepare(count);const auto previous=table.directions;table.prepare(count);
  for(unsigned seed=0;seed<2048;++seed){
   const float angle=float(seed)*(.0030679615757712823f),c=std::cos(angle),s=std::sin(angle);
   for(unsigned i=0;i<count;++i){
    const auto p=table.directions[i];assert(p.x==previous[i].x&&p.y==previous[i].y&&p.z==previous[i].z);
    const Vec3 actual{p.x*c-p.y*s,p.x*s+p.y*c,p.z};
    const float z=1.f-2.f*(float(i)+.5f)/float(count),r=std::sqrt(std::max(0.f,1-z*z)),phi=angle+float(i)*2.39996322972865332f;
    const Vec3 reference{r*std::cos(phi),r*std::sin(phi),z};
    worst=std::max(worst,std::hypot(actual.x-reference.x,actual.y-reference.y));
    assert(actual.z==reference.z&&std::abs(actual.x*actual.x+actual.y*actual.y+actual.z*actual.z-1)<1e-6f);
   }
  }
 }
 assert(worst<2e-5f);std::printf("PASS Fibonacci lattice: exact latitudes, unit directions, stable cache, maximum direction error %.9g\n",worst);
}
'''
with tempfile.TemporaryDirectory(prefix='probe-sphere-') as t:
    p=Path(t);(p/'test.cpp').write_text(source)
    for flags in [['-O2'],['-O1','-g','-fsanitize=address,undefined']]:
        subprocess.run(['clang++','-std=c++17','-UNDEBUG','-Wall','-Wextra','-Werror',*flags,*fp.test_include_flags(),str(p/'test.cpp'),'-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)
