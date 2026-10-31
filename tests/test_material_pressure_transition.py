#!/usr/bin/env python3
# northlight-test: requires=windows-exe,zig
"""Actual material pressure transition, full shared-pool and unchanged recovery oracle."""
from pathlib import Path
import hashlib,json,re,subprocess,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import northlight_paths as fp

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
out=fp.output_dir()/'material-pressure-transition';out.mkdir(parents=True,exist_ok=True)
source=fp.TESTS/'test_material_pressure_transition.cpp';reference=fp.TESTS/'displacement_state_reuse.cpp'
watched=[p for p in fp.SRC.rglob('*') if p.is_file()]+[source,reference,Path(__file__),fp.REPO/'northlight_paths.py']
inputs={str(p.resolve()):sha(p) for p in watched}
helper=reference.read_text().split('int main(){')[0]
assert helper.count('struct MaterialCaptureStateTestAccess {')==1
helper=helper.replace('struct MaterialCaptureStateTestAccess {','struct MaterialCaptureStateTestAccess {\n    static void pressureTransition();')
# This oracle accesses displacement only through its existing dedicated friend.
helper=helper.replace('struct DisplacementStateTestAccess {','struct DisplacementStateTestAccess {\n    static size_t pressureLimit(const NorthlightAutoDisplacement& g){return g.snapshots.snapshotCacheLimit();}\n    static NorthlightDrawSnapshot::Frame& pressureSources(NorthlightAutoDisplacement& g,unsigned i){return i==0?g.snapshots:i==1?g.rangeSnapshots:g.skinRangeSnapshots;}')
(out/'material_pressure_reference.h').write_bytes(helper.encode())
header=fp.src('auto_bump_renderer.h').read_text();geometry=fp.src('auto_displacement_renderer.h').read_text()
begin=header.index('    void setMemoryPressure(bool on,');end=header.index('    void trimMemory(',begin)
original='''    void setMemoryPressure(bool on,size_t=0,NorthlightStreaming::CpuRetirement* =nullptr,NorthlightStreaming::CpuRetirementBatch* =nullptr){
        if(memoryPressure==on)return;memoryPressure=on;deferredSnapshots.setSnapshotCacheLimit(on?4u<<20:24u<<20);
        if(on&&!deferred.empty()){deferredDiscarded+=unsigned(deferred.size());clearDeferred();drawn=occlusionDrawn=false;}
        deferredGPU.setLimit(on?0:32u<<20);
        if(on){deferredSnapshots.clearIndexCache();deferredDeclarations.clear();}
        normals.setMemoryPressure(on);displacement.setMemoryPressure(on);sharedPOM.setMemoryPressure(on);
        logf("MATERIAL_MEMORY pressure=%u normalBudgetMiB=%u geometryBudgetMiB=%u relief=active",unsigned(on),on?32:96,on?32:128);
    }
'''
control=out/'control';control.mkdir(exist_ok=True);(control/'auto_bump_renderer.h').write_bytes((header[:begin]+original+header[end:]).encode())
geometry_begin=geometry.index('    void setMemoryPressure(bool on,');geometry_end=geometry.index('    void trimMemory(',geometry_begin)
original_geometry='''    void setMemoryPressure(bool on,NorthlightStreaming::CpuRetirement* =nullptr,NorthlightStreaming::CpuRetirementBatch* =nullptr){
        current=nullptr;memoryPressure=on;snapshots.setSnapshotCacheLimit(on?6u<<20:24u<<20);rangeSnapshots.setSnapshotCacheLimit(on?1u<<20:4u<<20);skinRangeSnapshots.setSnapshotCacheLimit(on?1u<<20:4u<<20);
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;GlobalMemoryStatusEx(&memory);refreshBudget(memory);
        if(on){boundsLRU.clear();bounds.clear();boundsBytes=0;poseEnvelopes.clear();snapshots.clearIndexCache();rangeSnapshots.clearIndexCache();skinRangeSnapshots.clearIndexCache();}
    }
'''
(control/'auto_displacement_renderer.h').write_bytes((geometry[:geometry_begin]+original_geometry+geometry[geometry_end:]).encode())
records=[]
for optimization in ['-O2','-O0']:
    cells={}
    for name,folder in [('control',control),('current',out)]:
        exe=out/f'fixture-{name}-{optimization[1:]}.exe'
        command=[str(fp.zig()),'c++','-target','x86-windows-gnu',optimization,'-UNDEBUG','-std=c++17','-Wno-microsoft-exception-spec','-I',str(folder),'-I',str(out),*fp.test_include_flags(),str(source),'-luser32','-luuid','-o',str(exe)]
        if name=='current':command.insert(2,'-DNORTHLIGHT_TEST_MATERIAL_PRESSURE_POLICY=1')
        subprocess.run(command,env=fp.zig_env(),check=True);result=subprocess.run([str(exe)],capture_output=True,text=True)
        (out/f'{name}-{optimization[1:]}.log').write_text(result.stdout+result.stderr,encoding='utf-8');assert result.returncode==0,result.stdout+result.stderr
        line=next(line for line in result.stdout.splitlines() if line.startswith('MATERIAL_PRESSURE_PASS '));row=dict(re.findall(r'(\w+)=([^\s]+)',line));cells[name]=row
        records.append({'mode':optimization,'name':name,'command':command,'exe_sha256':sha(exe),'row':row})
    for field in ['before','after','recoveryMemoryReads','capMiB','allocationFree','heldBytesExact','declarationThread','predictionCleared','recovery']:assert cells['control'][field]==cells['current'][field],(field,cells)
    assert int(cells['control']['shrinkEvictions'])>0 and cells['current']['shrinkEvictions']=='0',cells
    assert int(cells['control']['geometryShrinkEvictions'])>0 and cells['current']['geometryShrinkEvictions']=='0',cells
    for field in ['pressureMemoryReads','trimMemoryReads']:assert cells['control'][field]=='1' and cells['current'][field]=='0',cells
    assert cells['current']['recoveryMemoryReads']=='2',cells
    if optimization=='-O2':
        negative=out/'fixture-disabled-batch.exe'
        negative_command=command[:-1]+[str(negative),'-DNORTHLIGHT_TEST_DISABLE_PRESSURE_BATCH=1']
        subprocess.run(negative_command,env=fp.zig_env(),check=True)
        result=subprocess.run([str(negative)],capture_output=True,text=True)
        (out/'disabled-batch.log').write_text(result.stdout+result.stderr,encoding='utf-8')
        assert result.returncode!=0 and 'ALL_PRESSURE_POOLS_MUST_SHARE_ONE_BATCH' in result.stderr
        records.append({'name':'disabled-batch','mode':optimization,'expected_failure':True,'exit_code':result.returncode})
assert all(sha(Path(p))==h for p,h in inputs.items()),'Source/helper changed during final validation'
(out/'validation.json').write_text(json.dumps({'inputs':inputs,'inputs_unchanged':True,'records':records,'timings_claimed':False,'gpu_or_game_launched':False},indent=2)+'\n')
print('PASS exact material pressure caps/ownership/recovery with no shrink-before-clear or unused pressure memory query.')
