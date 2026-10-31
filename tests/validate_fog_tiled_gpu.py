#!/usr/bin/env python3
# northlight-test: requires=zig,windows-gpu manual
"""Production fog: CPU-checked GPU reductions and exact legacy/same-binary comparisons."""
from pathlib import Path
import argparse,ast,hashlib,importlib.util,json,os,re,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]));import northlight_paths as fp
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--prefix',required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--benchmark',action='store_true');p.add_argument('--actual-map',type=Path);p.add_argument('--actor-shadow',action='store_true');p.add_argument('--controls-only',action='store_true');a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
spec=importlib.util.spec_from_file_location('guard',fp.TESTS/'validate_native_scene_trace.py');guard=importlib.util.module_from_spec(spec);spec.loader.exec_module(guard)
tree=ast.parse((fp.TESTS/'validate_fog_actor_visibility_gpu.py').read_text());setup=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='SETUP' for t in n.targets))
setup=setup.replace('float z=probe?.25f:(x/29+y/37)%3?.35f:1.f;', 'float z=workload==0?1.f:workload==1?((x>450&&x<480&&y>450&&y<480)?.35f:1.f):workload==2?((x/150+y/170)%5==0?.35f:1.f):workload==6?0.f:(x/29+y/37)%3?.35f:1.f;')
setup=setup.replace(' bind(4,actorMap);', ''' if(const char* actual=std::getenv("NORTHLIGHT_ACTOR_MAP"))actorMap=image(actual);
 bind(4,actorMap);
 auto* actorTiles=fogActorTilesFixture(d,actorMap,true);owned.push_back(actorTiles);ok(d->SetTexture(5,actorTiles));
 float lower=-1e10f;Image minimum{1,1,D3DFMT_R32F,4,std::vector<unsigned char>(4)};std::memcpy(minimum.pixels.data(),&lower,4);
 auto* fineControl=texture(d,minimum);owned.push_back(fineControl);
 if(std::getenv("NORTHLIGHT_BAD_FOG_BINDING")){float upper=1;std::memcpy(minimum.pixels.data(),&upper,4);actorTiles=texture(d,minimum);owned.push_back(actorTiles);ok(d->SetTexture(5,actorTiles));}
 for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(5,state,D3DTEXF_POINT));
 ok(d->SetSamplerState(5,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(5,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(5,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
 const bool shadowCheck=std::getenv("NORTHLIGHT_TILE_SHADOW")!=nullptr;IDirect3DTexture9* originalTiles=nullptr;
 if(shadowCheck){
  originalTiles=fogActorTilesFixture(d,actorMap,false);owned.push_back(originalTiles);
  c[0][2]=.2f;c[0][3]=1000;c[1][0]=1.2686f;c[1][1]=1.9626f;c[1][2]=1;c[1][3]=0;c[2][0]=1;
  for(unsigned i=3;i<7;++i){std::fill(c[i],c[i]+4,0);c[i][i-3]=1;}
  for(unsigned i=68;i<72;++i)std::fill(c[i],c[i]+4,0);
  c[68][0]=c[69][1]=1.f/12;c[70][2]=1.f/1280;c[71][2]=.5f-8.f/1280;c[71][3]=1;
  c[72][0]=1;c[72][1]=1.f/1024;c[72][2]=12;c[72][3]=0;c[30][0]=0;c[16][0]=c[16][1]=0;c[16][2]=1;
  float raw=1000.f/999.8f*(1-.2f/8);Image flat{1,1,D3DFMT_R32F,4,std::vector<unsigned char>(4)};std::memcpy(flat.pixels.data(),&raw,4);bind(1,flat);
  const float normal[]={0,0,1,1};Image normals{1,1,D3DFMT_A32B32G32R32F,16,std::vector<unsigned char>(16)};std::memcpy(normals.pixels.data(),normal,16);bind(14,normals);bind(12,actorMap);
  for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})ok(d->SetSamplerState(10,state,D3DTEXF_POINT));
  ok(d->SetSamplerState(10,D3DSAMP_MIPFILTER,D3DTEXF_NONE));ok(d->SetSamplerState(10,D3DSAMP_ADDRESSU,D3DTADDRESS_CLAMP));ok(d->SetSamplerState(10,D3DSAMP_ADDRESSV,D3DTADDRESS_CLAMP));
 }
''')
assert 'fogActorTilesFixture' in setup
source=(fp.TESTS/'regional_fog_cache_gpu.cpp').read_text().replace('#include "regional_fog.h"','#include "regional_fog.h"\n#include "actor_shadow_map.h"\n#include "fog_actor_tiles_fixture.h"')
needle=' IDirect3DVolumeTexture9* noise=nullptr;';assert source.count(needle)==1;source=source.replace(needle,setup+needle)
needle='ok(d->SetPixelShader(ps[modeIndex]));'
assert source.count(needle)==1
source=source.replace(needle,needle+'if(std::getenv("NORTHLIGHT_TILED_FINE_CONTROL"))ok(d->SetTexture(5,modeIndex?actorTiles:fineControl));if(shadowCheck)ok(d->SetTexture(10,modeIndex?actorTiles:originalTiles));')
needle='assert(!changed&&!nonfinite&&maximum==0);'
assert source.count(needle)==1
source=source.replace(needle,'''assert(!changed&&!nonfinite&&!alpha&&maximum==0&&"FOG_OUTPUT_MISMATCH");
 if(shadowCheck){float minimum=1;for(size_t at=0;at<size_t(W)*H*4;at+=4){uint16_t raw;std::memcpy(&raw,output[1].data()+at*2,2);minimum=std::min(minimum,half(raw));}std::printf("ACTOR_SHADOW_MIN %.9g\\n",minimum);std::fflush(stdout);assert(minimum<.9f&&"ACTOR_SHADOW_CONTROL_MUST_CAST");}''')
cpp=a.output/'fog.cpp';cpp.write_text(source);exe=a.output/'fog.exe'
subprocess.run([str(fp.zig()),'c++','-target','x86-windows-gnu','-O2','-UNDEBUG','-std=c++17','-I',str(fp.TESTS),*fp.test_include_flags(),str(cpp),str(fp.src('world_gi.cpp')),'-luser32','-o',str(exe)],env=fp.zig_env(),check=True)
report=dict(pass_=False,game_launched=False,whole_scene_replay=False,synthetic_light_transform=True,synthetic_environment=True,actor_shadow_check=a.actor_shadow,actual_actor_map=str(a.actual_map) if a.actual_map else None,cases=[],shaders={})
if a.actual_map:report['actor_map_sha256']=hashlib.sha256(a.actual_map.read_bytes()).hexdigest()
for entry in ['WorldFogActor','FogCloudsActor','WorldFogActorTiled','FogCloudsActorTiled','ActorTiles','ActorTilesWide','ActorShadow']:
    report['shaders'][entry]=hashlib.sha256((fp.COMPILED/(entry+'.bin')).read_bytes()).hexdigest()
report['fixture_sha256']=hashlib.sha256(cpp.read_bytes()).hexdigest();report['tile_fixture_sha256']=hashlib.sha256((fp.TESTS/'fog_actor_tiles_fixture.h').read_bytes()).hexdigest()
for backend in ([] if a.controls_only else [fp.client_root()/'renderer-backends/dxvk/dxvk_d3d9.dll',Path(os.environ['SystemRoot'])/'SysWOW64/d3d9.dll']):
    label='dxvk' if 'dxvk' in str(backend) else 'native'
    for entry in (['ActorShadow'] if a.actor_shadow else ['WorldFog','FogClouds']):
        for bits in ([16] if a.benchmark or a.actor_shadow else [16,32]):
            for case in ([1,2,5] if a.actor_shadow else [0,2,5,6] if a.benchmark else range(7)):
                for control in ([False] if a.benchmark or a.actor_shadow else [False,True]):
                    guard.processes();folder=a.output/(label+'-'+entry+'-'+str(bits)+'-'+str(case)+('-fine' if control else '-legacy'));folder.mkdir()
                    env={**fp.zig_env(),'NORTHLIGHT_FOG_VISIBILITY_CASE':str(case),'DXVK_LOG_PATH':str(folder),'DISABLE_VULKAN_OBS_CAPTURE':'1'}
                    if a.actual_map:env['NORTHLIGHT_ACTOR_MAP']=str(a.actual_map)
                    if control:env['NORTHLIGHT_TILED_FINE_CONTROL']='1'
                    if a.benchmark:env['NORTHLIGHT_TILE_BENCH']='1'
                    if a.actor_shadow:env['NORTHLIGHT_TILE_SHADOW']='1'
                    old=fp.COMPILED/(entry+'.bin' if a.actor_shadow else entry+('ActorTiled.bin' if control else 'Actor.bin'))
                    new=old if a.actor_shadow else fp.COMPILED/(entry+'ActorTiled.bin')
                    command=[str(exe),a.prefix,str(old),str(new),'WorldFog' if a.actor_shadow else entry,str(backend),'7',str(bits),*(['1920','1080','16'] if a.benchmark else ['128','96','1'])]
                    run=subprocess.run(command,cwd=folder,env=env,capture_output=True,text=True,timeout=120);(folder/'gpu.log').write_text(run.stdout+run.stderr)
                    assert run.returncode==0,(folder,(run.stdout+run.stderr)[-1600:])
                    assert 'FOG_TILE_REDUCTION_PASS' in run.stdout
                    row=json.loads(next(s for s in run.stdout.splitlines() if s.startswith('{')));row.update(backend=label,case=case,fine_control=control,actual_entry=entry,command=command)
                    tile_timing=re.search(r'TILE_GPU_FINAL_MS narrow=([0-9.]+) wide=([0-9.]+)',run.stdout)
                    if tile_timing:row.update(tile_narrow_ms=float(tile_timing[1]),tile_wide_ms=float(tile_timing[2]))
                    assert row['nonfinite']==row['alpha_changes']==0
                    if control:assert row['changed']==0
                    report['cases'].append(row);print(label,entry,bits,case,'fine' if control else 'legacy','PASS',row['old_ms'],row['new_ms'],flush=True)
                    (a.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
if a.controls_only:
    report['negative_controls']=[]
    for env_name,marker in [('NORTHLIGHT_BAD_FOG_TILES','FOG_TILE_REDUCTION_MISMATCH'),('NORTHLIGHT_BAD_FOG_BINDING','FOG_OUTPUT_MISMATCH')]:
        guard.processes();folder=a.output/env_name;folder.mkdir()
        env={**fp.zig_env(),'NORTHLIGHT_FOG_VISIBILITY_CASE':'2','NORTHLIGHT_TILED_FINE_CONTROL':'1',env_name:'1'}
        code=fp.COMPILED/'WorldFogActorTiled.bin'
        run=subprocess.run([str(exe),a.prefix,str(code),str(code),'WorldFog',str(Path(os.environ['SystemRoot'])/'SysWOW64/d3d9.dll'),'7','16','128','96','1'],cwd=folder,env=env,text=True,capture_output=True,timeout=120)
        (folder/'negative.log').write_text(run.stdout+run.stderr);assert run.returncode!=0 and marker in run.stdout+run.stderr
        report['negative_controls'].append(dict(fault=env_name,rejected=True));print('PASS rejected',env_name,flush=True)
report['pass_']=True;(a.output/'validation.json').write_text(json.dumps(report,indent=2)+'\n')
