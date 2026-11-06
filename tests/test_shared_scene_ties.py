#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Independent primitive oracle for SAH box rounding and frozen median behavior."""
from pathlib import Path
import argparse
import json
import subprocess
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-ref', default='ac966cef51d1ce45c102e1f5787aada8a05a8832')
parser.add_argument('--world-cache', type=Path)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
out = args.output or fp.out()/'shared-scene-ties'
out.mkdir(parents=True, exist_ok=True)
for suffix in ['h', 'cpp']:
    source = subprocess.check_output(['git', 'show', f'{args.baseline_ref}:src/gi/world_gi.{suffix}'], cwd=fp.REPO).decode()
    source = source.replace('NorthlightGI', 'FrozenGI').replace('"world_gi.h"', '"frozen_gi.h"')
    (out/f'frozen_gi.{suffix}').write_bytes(source.encode())
prefix = (fp.TESTS/'test_local_scene_cache.cpp').read_text().split('int main(')[0]
prefix = prefix.replace('modelPath.c_str()', 'modelPath.string().c_str()').replace('(root/"tile.fg3").c_str()', '(root/"tile.fg3").string().c_str()').replace('(root/"models"/(std::string(64,\'0\')+".fgs")).c_str()', '(root/"models"/(std::string(64,\'0\')+".fgs")).string().c_str()')
body = r'''
#include "frozen_gi.h"
static FrozenGI::WorldScene frozen(const WorldScene& scene){
 FrozenGI::WorldScene out;for(const auto& v:scene.vertices)out.vertices.push_back({{v.position.x,v.position.y,v.position.z},{v.normal.x,v.normal.y,v.normal.z},v.u,v.v});
 for(const auto& t:scene.triangles)out.triangles.push_back({t.v0,t.v1,t.v2,t.material});
 for(const auto& m:scene.materials){FrozenGI::WorldMaterial n;n.albedo={m.albedo.x,m.albedo.y,m.albedo.z};n.width=m.width;n.height=m.height;n.rgba=m.rgba;n.alphaCutoff=m.alphaCutoff;n.addressU=m.addressU;n.addressV=m.addressV;n.terrain=m.terrain;n.wmo=m.wmo;out.materials.push_back(std::move(n));}return out;
}
// Scalar primitive/alpha oracle: no BVH, slab, reciprocal-box or traversal code.
static RayHit brute(const WorldScene& scene,Vec3 origin,Vec3 direction,float lo,float hi){
 RayHit best;best.distance=hi;direction=normalized(direction);
 for(uint32_t i=0;i<scene.triangles.size();++i){const auto&t=scene.triangles[i];const auto&a=scene.vertices[t.v0];const auto&b=scene.vertices[t.v1];const auto&c=scene.vertices[t.v2];
  Vec3 ab=b.position-a.position,ac=c.position-a.position,h=cross(direction,ac);float determinant=dot(ab,h);if(std::fabs(determinant)<1e-10f)continue;
  float inv=1.f/determinant;Vec3 s=origin-a.position;float u=dot(s,h)*inv;if(u<0||u>1)continue;Vec3 q=cross(s,ab);float v=dot(direction,q)*inv;if(v<0||u+v>1)continue;
  float distance=dot(ac,q)*inv;if(distance<lo||distance>=hi||distance>best.distance||(distance==best.distance&&i>=best.triangle))continue;
  const auto&m=scene.materials[t.material];if(!m.rgba.empty()){
   auto coord=[](float x,uint32_t mode){if(mode==3)return std::max(0.f,std::min(1.f,x));if(mode==2){float p=x-2.f*std::floor(x*.5f);return p<=1?p:2-p;}return x-std::floor(x);};
   auto texel=[](int x,uint32_t n,uint32_t mode){return mode==1?unsigned((x+int(n))%int(n)):unsigned(std::max(0,std::min(int(n)-1,x)));};
   float w=1-u-v,x=coord(w*a.u+u*b.u+v*c.u,m.addressU)*m.width-.5f,y=coord(w*a.v+u*b.v+v*c.v,m.addressV)*m.height-.5f,fx=x-std::floor(x),fy=y-std::floor(y),alpha=0;int ix=int(std::floor(x)),iy=int(std::floor(y));
   for(int yy=0;yy<2;++yy)for(int xx=0;xx<2;++xx){const auto index=(size_t(texel(iy+yy,m.height,m.addressV))*m.width+texel(ix+xx,m.width,m.addressU))*4;float weight=(xx?fx:1-fx)*(yy?fy:1-fy);alpha+=float(m.rgba[index+3])/255.f*weight;}if(alpha<m.alphaCutoff)continue;
  }best.hit=true;best.distance=distance;best.triangle=i;
 }return best;
}
static unsigned verify(WorldScene scene,Vec3 center,unsigned count,bool synthetic){
 std::string error;BVH current,median;assert(current.build(WorldScene(scene),error));assert(median.build(WorldScene(scene),error,false));FrozenGI::BVH old,oldMedian;assert(old.build(frozen(scene),error));assert(oldMedian.build(frozen(scene),error,false));
 unsigned corrected=0,hits=0;std::mt19937 rng(619);std::uniform_real_distribution<float> d(-1,1);
 auto check=[&](Vec3 o,Vec3 dir){const auto expected=brute(scene,o,dir,0,700),now=current.trace(o,dir,0,700);assert(expected.hit==now.hit);if(now.hit){if(expected.triangle!=now.triangle||expected.distance!=now.distance){fprintf(stderr,"oracle mismatch expected%u now%u distance%.9g/%.9g\n",expected.triangle,now.triangle,expected.distance,now.distance);abort();}++hits;}
  const auto before=old.trace({o.x,o.y,o.z},{dir.x,dir.y,dir.z},0,700);if(before.hit!=now.hit||(now.hit&&(before.triangle!=now.triangle||before.distance!=now.distance)))++corrected;
  auto a=median.trace(o,dir,0,700);auto b=oldMedian.trace({o.x,o.y,o.z},{dir.x,dir.y,dir.z},0,700);assert(a.hit==b.hit);if(a.hit)assert(a.triangle==b.triangle&&a.distance==b.distance&&a.backFace==b.backFace&&a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z&&a.normal.x==b.normal.x&&a.normal.y==b.normal.y&&a.normal.z==b.normal.z&&a.albedo.x==b.albedo.x&&a.albedo.y==b.albedo.y&&a.albedo.z==b.albedo.z);
 };
 if(synthetic){Vec3 o(118.348969f,-2.84802914f,159.546295f),dir(-.233288586f,.28829658f,-.400666535f);auto expected=brute(scene,o,dir,0,700);auto before=old.trace({o.x,o.y,o.z},{dir.x,dir.y,dir.z},0,700);assert(expected.triangle==30&&before.triangle==70&&expected.distance==before.distance);check(o,dir);printf("KNOWN_BOX_TIE frozen=%u brute=%u fixed=%u distance=%.9g\n",before.triangle,expected.triangle,current.trace(o,dir,0,700).triangle,expected.distance);}
 for(unsigned i=0;i<count;++i){Vec3 o=center+Vec3(d(rng)*280,d(rng)*280,d(rng)*100+80),dir=i%3?Vec3(d(rng),d(rng),d(rng)):Vec3(0,0,-1);check(o,dir);}
 // Exact vertices/edges, near-parallel grazing and both surface sides.
 for(size_t i=0;i<scene.triangles.size();i+=std::max<size_t>(1,scene.triangles.size()/128)){const auto&t=scene.triangles[i];const auto a=scene.vertices[t.v0].position,b=scene.vertices[t.v1].position,c=scene.vertices[t.v2].position;const Vec3 n=normalized(cross(b-a,c-a));for(Vec3 p:{a,(a+b)*.5f,(a+b+c)/3.f})for(float side:{-1.f,1.f})for(float grazing:{0.f,.9999f}){Vec3 dir=normalized(n*side+(b-a)*grazing);check(p-dir*100,dir);}}
 printf("BRUTE_ORACLE triangles=%zu random=%u hits=%u frozenSAHbugs=%u medianExact=1\n",scene.triangles.size(),count,hits,corrected);return corrected;
}
int main(int argc,char**argv){std::setvbuf(stdout,nullptr,_IONBF,0);auto root=fs::temp_directory_path()/("shared-ties-"+std::to_string(Clock::now().time_since_epoch().count()));fixtures(root);std::string error;WorldScene scene;assert(loadInstancedScenes({(root/"tile.fg3").string()},(root/"models").string(),{-288,-288,-320},{288,288,320},scene,error));assert(verify(std::move(scene),{},20000,true)>0);fs::remove_all(root);
 if(argc>1){Vec3 center(5244,-35,492);WorldScene real;assert(loadInstancedScenes(tiles(argv[1],"Northrend",center),(fs::path(argv[1])/"models").string(),center-Vec3(288,288,320),center+Vec3(288,288,320),real,error));verify(std::move(real),center,256,false);}
 puts("PASS independent primitive/alpha brute force, documented frozen SAH tie bug, exact frozen median, randomized/edge/grazing rays");}
'''
(out/'ties.cpp').write_bytes((prefix+body).encode())
results = []
for label, flags in [('O2', ['-O2']), ('debug', ['-O0', '-g'])]:
    exe = out/(label+'.exe')
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-UNDEBUG', *flags,
                    *fp.test_include_flags(), str(out/'ties.cpp'), str(fp.src('world_gi.cpp')), str(out/'frozen_gi.cpp'), '-o', str(exe)], env=fp.zig_env(), check=True)
    text = subprocess.check_output([str(exe), *([str(args.world_cache)] if label == 'O2' and args.world_cache else [])], text=True)
    (out/(label+'.log')).write_text(text, encoding='utf-8')
    print(label+'\n'+text, end='')
    results.append({'configuration': label, 'pass': True})
(out/'validation.json').write_text(json.dumps({'baseline': args.baseline_ref, 'gameLaunched': False, 'gpuUsed': False, 'runs': results}, indent=2)+'\n', encoding='utf-8')
