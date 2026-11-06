#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Actual x86 MPQ pipeline with pinned snapshots and measured VA reclaim episodes.

The private reserve-only ballast represents unrelated process address usage;
it is released on refusal and after each successful build. No game, GPU,
settings, installed binaries, fixture assets or memory floors are modified.
"""
import argparse
import hashlib
import io
import json
import platform
import re
from pathlib import Path
import statistics
import subprocess
import sys
import time
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def digest(path):
    h=hashlib.sha256()
    with path.open('rb') as stream:
        for data in iter(lambda:stream.read(1024*1024),b''):h.update(data)
    return h.hexdigest()

def fresh_output(path):
    if path.exists() and (not path.is_dir() or any(path.iterdir())):
        raise RuntimeError('Output must be absent or empty; stale evidence is never overwritten: '+str(path))
    path.mkdir(parents=True,exist_ok=True)

def fixture_inputs(root):
    root=root.resolve();manifest=root/'manifest.json';data=json.loads(manifest.read_text())
    assert data['format']==1 and data['map']=='Northrend','Unsupported fixture format/map'
    for key in ['missing_tiles','unsupported','missing_assets']:assert data[key]==[],(key,data[key])
    for key in ['unreadable','unlisted']:assert data['mpq_view'][key]==[],('MPQ '+key,data['mpq_view'][key])
    assert data['outputs'] and data['tiles'],'Empty fixture'
    actual={str(path.relative_to(root)).replace('\\','/') for path in root.rglob('*') if path.is_file() and path!=manifest}
    assert actual==set(data['outputs']),'Missing or unmanifested fixture files'
    for relative,expected in data['outputs'].items():
        path=(root/relative).resolve();assert path.is_relative_to(root),'Fixture path escapes its root'
        assert path.is_file() and path.stat().st_size==expected['bytes'],('Missing/truncated fixture output',relative)
        assert digest(path)==expected['sha256'],('Fixture hash mismatch',relative)
    models=root/'world-cache/models';tiles=root/'world-cache/Northrend'
    assert models.is_dir() and tiles.is_dir(),'Missing world cache'
    import struct
    for path in tiles.glob('*.fg3'):
        raw=path.read_bytes();assert len(raw)>=12,('Truncated tile',path)
        magic,version,count=struct.unpack_from('<4sII',raw)
        assert magic==b'FGS3' and version==3 and len(raw)==12+116*count,('Unsupported/truncated tile',path)
        for offset in range(12,len(raw),116):
            model=models/(raw[offset:offset+32].hex()+'.fgs')
            assert model.is_file(),('Missing referenced model',model)
    for path in models.glob('*.fgs'):
        with path.open('rb') as stream:header=stream.read(20)
        assert len(header)==20 and header[:8]==b'FGS2\x02\0\0\0',('Unsupported/truncated model',path)
    return {'root':str(root),'manifest_sha256':digest(manifest),'outputs':data['outputs'],'limitations':data['limitations']}

def source_inputs(paths):
    return {str(path.resolve()):{'bytes':path.stat().st_size,'sha256':digest(path)} for path in sorted(set(paths))}

def dependency_paths(path):
    text=path.read_text().replace('\\\n','')
    # The target's drive colon is not followed by whitespace. Only the make
    # rule separator is removed; escaped spaces in actual paths stay together.
    dependencies=re.split(r':\s+',text,maxsplit=1);assert len(dependencies)==2,('Malformed depfile',path)
    words=re.findall(r'(?:\\.|[^\s])+',dependencies[1])
    return {Path(re.sub(r'\\([ :#\\])',r'\1',word)).resolve() for word in words}

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--fixture',type=Path,required=True)
p.add_argument('--output',type=Path)
p.add_argument('--rounds',type=int,default=3)
p.add_argument('--no-pressure',action='store_true',help='Negative control: no reclaim should run')
p.add_argument('--camera-log',type=Path,help='Use seven selected captured eye positions instead of the fixed control path')
p.add_argument('--baseline-ref',default='de0d60f')
p.add_argument('--check-inputs-only',action='store_true',help='Verify inputs/source pins; do not invoke any compiler or binary')
args=p.parse_args();assert 1<=args.rounds<=5
out=(args.output or fp.out()/'scene-entry-reclaim').resolve();fresh_output(out)
fixture_before=fixture_inputs(args.fixture)
camera_before=source_inputs([args.camera_log]) if args.camera_log else {}
baseline=out/'baseline-source'
archive=subprocess.check_output(['git','archive','--format=zip',args.baseline_ref,'src'],cwd=fp.REPO)
with zipfile.ZipFile(io.BytesIO(archive)) as z:
    for entry in z.infolist():
        target=baseline/entry.filename;assert target.resolve().is_relative_to(baseline.resolve())
        if not entry.is_dir():target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(entry))
source=fp.src('world_renderer.h').read_text()
baseline_world=(baseline/'src/world/world_renderer.h').read_text()
old='if(!NorthlightGeometryMemory::admits(memory,plainEntry)){resetPreparation();memory=workerMemory(plainEntry);}'
assert baseline_world.count(old)==1 and '// BEGIN STAGED ENTRY RECLAIM:' not in baseline_world,'Frozen baseline does not ship the old entry policy'
block=source.split('// BEGIN STAGED ENTRY RECLAIM:')[1].split('\n',1)[1].split('// END STAGED ENTRY RECLAIM')[0]
prefix=r'''
#include <windows.h>
#include "geometry_memory.h"
#include "memory_admission_probe.h"
#include <cstdarg>
namespace NorthlightDiagnostics {[[maybe_unused]] static bool enabled(){return true;}}
[[maybe_unused]] static void logf(const char* format,...){va_list args;va_start(args,format);std::vprintf(format,args);std::puts("");va_end(args);}
static NorthlightGeometryMemory::Sample actualMemory(){
    SYSTEM_INFO info;GetSystemInfo(&info);const uint64_t limit=uint64_t(reinterpret_cast<uintptr_t>(info.lpMaximumApplicationAddress))+1;
    NorthlightGeometryMemory::Sample s;s.valid=true;
    for(uint64_t cursor=0;cursor<limit;){MEMORY_BASIC_INFORMATION m{};if(!VirtualQuery(reinterpret_cast<void*>(uintptr_t(cursor)),&m,sizeof m)){s.valid=false;break;}
        const auto end=uint64_t(reinterpret_cast<uintptr_t>(m.BaseAddress))+uint64_t(m.RegionSize);if(end<=cursor){s.valid=false;break;}
        if(m.State==MEM_FREE){s.available+=m.RegionSize;s.largest=std::max<uint64_t>(s.largest,m.RegionSize);}cursor=end;}
    return s;
}
struct Ballast {
    std::vector<void*> blocks;
    ~Ballast(){for(void* p:blocks)assert(VirtualFree(p,0,MEM_RELEASE));}
    void leave(uint64_t available){
        const uint64_t granularity=65536;
        for(unsigned attempt=0;attempt<40;++attempt){const auto s=actualMemory();assert(s.valid);if(s.available<=available+granularity)return;
            const auto bytes=std::min<uint64_t>(s.available-available,s.largest/2)/granularity*granularity;
            assert(bytes);void* p=VirtualAlloc(nullptr,size_t(bytes),MEM_RESERVE,PAGE_NOACCESS);assert(p);blocks.push_back(p);}
        assert(false);
    }
};
'''
sampler=source.split('    static NorthlightGeometryMemory::Sample geometryAdmissionFor(')[1].split('    // Issue #40:')[0]
baseline_sampler=baseline_world.split('    static NorthlightGeometryMemory::Sample geometryAdmissionFor(')[1].split('    // Issue #40:')[0]
assert sampler==baseline_sampler,'Admission sampler changed; baseline must use its own sampler'
prefix+='\nstatic NorthlightGeometryMemory::Sample geometryAdmissionFor('+sampler+'\n'
# This driver uses the public production scene/plan/pages/ray/bake paths. Only
# its pressure control and previous-generation lifetime differ from the normal
# seven-region oracle. Both baselines already ship shared terrain provenance.
driver=(fp.TESTS/'validate_shared_scene_pipeline.cpp').read_text()
driver=driver.replace('int main(int argc,char** argv){',prefix+'\nint main(int argc,char** argv){')
driver=driver.replace('NorthlightMeshPageKey::KeySet resident;SceneSizeHint hint;',
                      'NorthlightMeshPageKey::KeySet resident;SceneSizeHint hint;std::shared_ptr<BVH> previous;NorthlightMemoryAdmission::Probe probe;')
driver=driver.replace('Vec3(5248,10,508)','Vec3(5218,50,428)')
cameras=[]
if args.camera_log:
    for line in args.camera_log.read_text().splitlines():
        if not line.startswith('WORLD camera '):continue
        eye=[float(v) for v in re.search(r'eye=\(([^)]+)',line)[1].split()]
        if 5000<eye[0]<6000 and 300<eye[2]<600 and (not cameras or cameras[-1]['eye']!=eye):
            cameras.append({'eye':eye,'tick':int(re.search(r'tick=(\d+)',line)[1])})
    assert len(cameras)>=7
    cameras=[cameras[0],cameras[0],*[cameras[(i+1)*(len(cameras)-1)//5] for i in range(5)]]
    points=','.join('{'+','.join(format(value,'.9g') for value in row['eye'])+'}' for row in cameras)
    driver=driver.replace('const Vec3 c=origin+moves[step];','const Vec3 observed[]={'+points+'};const Vec3 c=observed[step];')
    driver=driver.replace('const Vec3 origin=', '[[maybe_unused]] const Vec3 origin=').replace('const Vec3 moves[]=', '[[maybe_unused]] const Vec3 moves[]=')
import math
selection=cameras or [{'eye':[5218+dx,50+dy,428]} for dx,dy in [(0,0),(0,0),(32,0),(0,32),(-32,0),(0,-32),(0,0)]]
for row in selection:
    x,y,_=row['eye'];tx=math.floor((17066.6666667-y)/533.3333333);ty=math.floor((17066.6666667-x)/533.3333333)
    for tile_y in range(ty-2,ty+3):
        for tile_x in range(tx-2,tx+3):
            assert (args.fixture/f'world-cache/Northrend/{tile_x}_{tile_y}.fg3').is_file(),('Missing selected camera tile',tile_x,tile_y)

start='''
        const auto entrySetupStart=Clock::now();
        Ballast ballast;const auto plainEntry=NorthlightGeometryMemory::buildBudget(64*NorthlightGeometryMemory::MiB);[[maybe_unused]] const auto entryBudget=plainEntry;
        const unsigned deficit=ENTRY_NO_PRESSURE?0:step==2||step==6?2:step==4?12:0;
        if(deficit)ballast.leave(plainEntry.available-uint64_t(deficit)*NorthlightGeometryMemory::MiB);
        auto memory=geometryAdmissionFor(probe,plainEntry);const auto entryBefore=actualMemory();
        const auto rawBefore=sources.liveBytes();const auto prepBefore=preparation.stats().bytes;
        auto& sceneSources=sources;auto& localGeometry=cache;auto& meshPreparation=preparation;
        struct Request{uint64_t id;}r{step};(void)r;
        auto workerMemory=[&](NorthlightGeometryMemory::Budget checked){assert(checked.available==plainEntry.available&&checked.largest==plainEntry.largest);return geometryAdmissionFor(probe,checked);};
        auto resetPreparation=[&]{meshPreparation.reset();sceneSources.reset();localGeometry.reset();};
        const double setupMs=ms(entrySetupStart);
        const auto reclaimStart=Clock::now();
#include "entry_episode.inl"
        const double reclaimMs=ms(reclaimStart);const auto measuredAfter=actualMemory();
        std::printf("ENTRY step=%u deficit=%u beforeAvailable=%llu afterAvailable=%llu beforeLargest=%llu afterLargest=%llu sourceBefore=%llu sourceAfter=%llu prepBefore=%llu prepAfter=%llu admitted=%u setupMs=%.6f reclaimMs=%.6f previous=%u\\n",step,deficit,
            (unsigned long long)entryBefore.available,(unsigned long long)measuredAfter.available,(unsigned long long)entryBefore.largest,(unsigned long long)measuredAfter.largest,
            (unsigned long long)rawBefore,(unsigned long long)sources.liveBytes(),(unsigned long long)prepBefore,(unsigned long long)preparation.stats().bytes,
            unsigned(NorthlightGeometryMemory::admits(memory,plainEntry)),setupMs,reclaimMs,unsigned(bool(previous)));
        if(!NorthlightGeometryMemory::admits(memory,plainEntry)){assert(previous&&previous->triangleCount());std::printf("REFUSED step=%u previousTriangles=%zu\\n",step,previous->triangleCount());continue;}
        const AllocationAdmission interior=[&](uint64_t bytes){if(bytes<NorthlightGeometryMemory::MiB)return true;const auto budget=NorthlightGeometryMemory::inBuildBudget(bytes);return NorthlightGeometryMemory::admits(geometryAdmissionFor(probe,budget),budget);};
'''
needle='        auto all=Clock::now(),t=all;'
assert driver.count(needle)==1;driver=driver.replace(needle,start+needle)
driver=driver.replace('        std::puts("");','        std::puts("");previous=std::make_shared<BVH>(std::move(local));')
for before,after in [('local,error,{},true,provider,revision','local,error,interior,true,provider,revision'),
                     ('terrain,error,1,{},terrainFilter','terrain,error,1,interior,terrainFilter'),
                     ('plan,error,{},radius','plan,error,interior,radius'),('pages,error,{},options','pages,error,interior,options')]:
    assert driver.count(before) in (1,2),before
    driver=driver.replace(before,after)

(out/'pipeline-driver.cpp').write_text(driver,encoding='utf-8')
if args.check_inputs_only:
    (out/'input-check.json').write_text(json.dumps({'inputs_checked_only':True,'compiler_invoked':False,'fixture':fixture_before,
        'baseline':subprocess.check_output(['git','rev-parse',args.baseline_ref],cwd=fp.REPO,text=True).strip(),
        'exact_old_entry_block':True,'identical_production_sampler':True,'captured_camera_rows':cameras},indent=2)+'\n',encoding='utf-8')
    print('PASS input hashes, complete dependencies, baseline policy and sampler pins; no compiler invoked')
    sys.exit(0)
executables={}
commands=[];depfiles=[]
zig=fp.zig().resolve()
compiler_info={'path':str(zig),'sha256':digest(zig),'version':subprocess.check_output([str(zig),'version'],text=True).strip(),
               'python':sys.version,'platform':platform.platform()}
def run(command):
    commands.append([str(word) for word in command])
    (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8')
    completed=subprocess.run(command,env=fp.zig_env(),stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
    (out/f'command-{len(commands):02}.log').write_text(completed.stdout,encoding='utf-8')
    completed.check_returncode()
sources_before={}
for root in [baseline,fp.REPO]:
    paths=[path for path in (root/'src').rglob('*') if path.is_file() and path.suffix in ('.h','.inl','.inc')]
    paths += [root/'src/gi/world_gi.cpp',root/'src/world/world_mesh_plan.cpp']
    sources_before.update(source_inputs(paths))
sources_before.update(source_inputs([out/'pipeline-driver.cpp',Path(__file__),fp.TESTS/'validate_shared_scene_pipeline.cpp']))
for name,root,body in [('baseline',baseline,old),('current',fp.REPO,block)]:
    folder=out/name;folder.mkdir(exist_ok=True);(folder/'entry_episode.inl').write_text(body,encoding='utf-8')
    sources_before.update(source_inputs([folder/'entry_episode.inl']))
    (out/'sources-before.json').write_text(json.dumps(sources_before,indent=2)+'\n',encoding='utf-8')
    includes=[flag for d in sorted((root/'src').iterdir()) if d.is_dir() for flag in ['-I',str(d)]]
    exe=folder/'test.exe'
    common=[str(zig),'c++','-target','x86-windows-gnu','-std=c++17','-O2','-UNDEBUG','-Wall','-Wextra','-Werror',
            '-DSHARED_SCENE_PIPELINE=1','-DCURRENT_TERRAIN_CHUNKS=1','-DPIPELINE_RADIUS=928','-DENTRY_NO_PRESSURE='+str(int(args.no_pressure)),'-I',str(folder),*includes]
    objects=[]
    for index,cpp in enumerate([out/'pipeline-driver.cpp',root/'src/gi/world_gi.cpp',root/'src/world/world_mesh_plan.cpp']):
        obj=folder/f'{index}.o';dep=folder/f'{index}.d';objects.append(obj);depfiles.append(dep)
        run([*common,'-MMD','-MF',str(dep),'-c',str(cpp),'-o',str(obj)])
    run([str(zig),'c++','-target','x86-windows-gnu',*[str(obj) for obj in objects],'-o',str(exe)])
    executables[name]=exe
compiled={path for dep in depfiles for path in dependency_paths(dep)}
compiled.update([fp.src('world_renderer.h'),baseline/'src/world/world_renderer.h',Path(__file__),fp.TESTS/'validate_shared_scene_pipeline.cpp'])
assert all(str(path.resolve()) in sources_before for path in compiled),'An actual compiler dependency lacks a before fingerprint'
compiled_before={str(path.resolve()):sources_before[str(path.resolve())] for path in sorted(compiled)}
assert source_inputs(compiled)==compiled_before,'Compiled source changed during compilation'
exe_hashes={name:digest(exe) for name,exe in executables.items()}
(out/'compiled-inputs.json').write_text(json.dumps(compiled_before,indent=2)+'\n',encoding='utf-8')
checks=['scene','plan','pagesHash','rays','bakeHash','localTriangles','planTriangles','pages','batches']
results={name:[] for name in executables};process_wall_ms={name:[] for name in executables}
for round_number in range(args.rounds):
    for name in (['baseline','current'] if round_number%2==0 else ['current','baseline']):
        command=[str(executables[name]),str(args.fixture/'world-cache'),'Northrend','bake'];commands.append(command)
        started=time.perf_counter()
        raw=subprocess.check_output(command,text=True)
        process_wall_ms[name].append((time.perf_counter()-started)*1000)
        (out/f'{name}-{round_number}.log').write_text(raw,encoding='utf-8')
        parsed={prefix:[dict(field.split('=',1) for field in line.split()[1:]) for line in raw.splitlines() if line.startswith(prefix+' ')]
                for prefix in ['PIPELINE','ENTRY','REFUSED']}
        assert len(parsed['ENTRY'])==7
        results[name].append(parsed)
        print(name,round_number,'builds',len(parsed['PIPELINE']),'refused',len(parsed['REFUSED']),
              'prepare+bake total',sum(float(row['prepareAndBakeMs']) for row in parsed['PIPELINE']),flush=True)
    for prefix in ['PIPELINE','REFUSED']:
        pair=[{row['step']:row for row in results[name][-1][prefix]} for name in ['baseline','current']]
        assert pair[0].keys()==pair[1].keys(),('Actual admission differed; do not compare unequal work',prefix,pair[0].keys(),pair[1].keys())
        for step,old in pair[0].items():
            for key in (checks if prefix=='PIPELINE' else ['previousTriangles']):assert old[key]==pair[1][step][key],(prefix,step,key)
summary={}
for name,rounds in results.items():
    summary[name]={}
    for label,steps in [('all',range(7)),('pressure',[2,4,6]),('unpressured',[0,1,3,5])]:
        rows=[row for run in rounds for row in run['PIPELINE'] if int(row['step']) in steps]
        summary[name][label]={key:statistics.median(float(row[key]) for row in rows) for key in ['localMs','terrainMs','planMs','pagesMs','totalMs','bakeMs','prepareAndBakeMs']}
compiled_after=source_inputs(compiled)
assert compiled_after==compiled_before,'Compiled source changed during execution'
assert exe_hashes=={name:digest(exe) for name,exe in executables.items()},'Executable changed during execution'
assert digest(zig)==compiler_info['sha256'],'Compiler changed during validation'
assert fixture_inputs(args.fixture)==fixture_before,'Fixture changed during validation'
assert (source_inputs([args.camera_log]) if args.camera_log else {})==camera_before,'Camera log changed during validation'
(out/'sources-after.json').write_text(json.dumps(compiled_after,indent=2)+'\n',encoding='utf-8')
(out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n',encoding='utf-8')
report={'all_equal':True,'baseline':subprocess.check_output(['git','rev-parse',args.baseline_ref],cwd=fp.REPO,text=True).strip(),'rounds':args.rounds,'same_original_16_8_8_MiB':True,'no_pressure_control':args.no_pressure,
        'entry_available_MiB':768,'entry_largest_MiB':96,'interior_reserve_MiB':640,'production_admission_sampler':True,'actual_VirtualQuery_samples':True,'reserve_ballast_held_during_build':True,
        'game_launched':False,'gpu_used':False,'captured_camera_rows':cameras,'camera_log_sha256':hashlib.sha256(args.camera_log.read_bytes()).hexdigest() if args.camera_log else None,'results':results,'median_cpu_ms':summary,'canonical_checks':checks,
        'fixture_inputs':fixture_before,'compiler':compiler_info,'commands':commands,'exe_sha256':exe_hashes,
        'compiled_sources_before':compiled_before,'compiled_sources_after':compiled_after,'exact_old_entry_block':True,'identical_production_sampler':True,
        'serial_episode_ms':{name:[sum(float(row['prepareAndBakeMs']) for row in run['PIPELINE'])+sum(float(row['reclaimMs']) for row in run['ENTRY']) for run in runs] for name,runs in results.items()},
        'entry_setup_ms':{name:[sum(float(row['setupMs']) for row in run['ENTRY']) for run in runs] for name,runs in results.items()},
        'process_wall_ms_including_oracle_bookkeeping':process_wall_ms,'not_in_game_FPS':True,
        'source_sha256':{str(path.relative_to(fp.REPO)).replace('\\','/'):hashlib.sha256(path.read_bytes().replace(b'\r\n',b'\n')).hexdigest()
                         for path in [fp.src('scene_source_cache.h'),fp.src('world_renderer.h'),Path(__file__),fp.TESTS/'validate_shared_scene_pipeline.cpp']}}
with (out/'validation.json').open('x',encoding='utf-8') as result:
    json.dump(report,result,indent=2);result.write('\n')
print(json.dumps(summary,indent=2))
