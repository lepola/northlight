#!/usr/bin/env python3
# northlight-test: requires=zig,windows-exe
"""Run production context gates: an unfogged actor must not hide later world fog."""
from pathlib import Path
import os
import runpy
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
import northlight_paths as fp


def main():
    test = runpy.run_path(str(ROOT / 'tests/test_model_world_context.py'))
    method = test['method']
    harness = test['HARNESS'].split('int main(){')[0]
    harness = harness.replace('#include <map>', '#include <map>\n#include <memory>\n#include "legacy_fog.h"')
    harness = harness.replace('struct World {', '''struct World {
    NorthlightLegacyFog::Constants legacyFog;
    NorthlightLegacyFog::Hold legacyFogHold;
    struct Active {std::string map="Northrend";} activeValue;Active* active=&activeValue;
''')
    harness = harness.replace('Ext* d;bool', 'explicit World(Ext* device):d(device){}\nExt* d;bool')
    harness = harness.replace('std::map<IDirect3DVertexShader9*,Capture> captureShaders;',
                              'std::map<IDirect3DVertexShader9*,Capture> captureShaders;\nstd::map<IDirect3DVertexShader9*,std::shared_ptr<Capture>> wmoShaders;')
    harness = harness.replace('assert(reg==30)', 'assert(reg==12||reg==30)')
    terrain = method('src/world/world_renderer.h', 'void terrainContext(').split('float view[16]')[0] + 'valid=true;}'
    wmo = method('src/world/world_renderer.h', 'bool wmoContext(').split('float columns[16]')[0] + 'valid=true;return true;}'
    harness = harness.replace('void terrainContext(){valid=true;}', terrain)
    harness = harness.replace('bool wmoContext(IDirect3DVertexShader9*){valid=true;return true;}', wmo)
    getter = method('src/world/world_renderer.h', 'const NorthlightLegacyFog::Constants& legacyFogConstants(')
    harness = harness.replace('__MODEL__', test['MODEL'] + '\n' + getter)
    harness = harness.replace('__BEFORE__', test['BEFORE']).replace('__PROJECTION__', test['PROJECTION'])
    # Model-context reads are injected as in the existing production-gate test.
    # Only the already-valid terrain/WMO prefix is under test here.
    harness += r'''
int main(){
    const bool expectBug=EXPECT_FOG_BUG;unsigned cases=0;
    auto* shader=reinterpret_cast<IDirect3DVertexShader9*>(1);
    for(unsigned kind=0;kind<2;++kind){
        Device p;p.world->captureShaders[shader].fog=false;p.beforeDraw(shader);
        assert(p.world->hasContext()&&!p.world->legacyFogKnown);
        p.vc.skinned=false;p.vc.entry=kind?0:1;p.vc.wmo=kind!=0;
        p.world->wmoShaders[shader]=std::make_shared<World::Capture>();
        p.beforeDraw(shader);
        assert(p.world->hasContext()&&p.terrain);
        assert(p.world->legacyFogKnown!=expectBug);++cases;
    }
    {Device p;p.world->captureShaders[shader].fog=false;p.beforeDraw(shader);
     p.vc.skinned=false;p.vc.entry=1;p.viewportOK=false;p.beforeDraw(shader);
     assert(!p.world->legacyFogKnown);++cases;}
    {Device p;p.world->captureShaders[shader].fog=false;p.beforeDraw(shader);
     p.vc.skinned=false;p.vc.wmo=true;p.viewportOK=false;
     p.world->wmoShaders[shader]=std::make_shared<World::Capture>();p.beforeDraw(shader);
     assert(!p.world->legacyFogKnown);++cases;}
    {Device p;auto& w=*p.world;NorthlightLegacyFog::Constants fog;
     fog.parameters[0]=-.02f;fog.parameters[3]=1;fog.color[0]=.55f;fog.color[1]=.32f;fog.color[2]=.12f;
     w.legacyFogHold.update("Northrend",fog,true,GetTickCount());
     assert((w.legacyFogConstants().parameters[3]>.99f)!=expectBug);++cases;
     w.activeValue.map="Kalimdor";assert(w.legacyFogConstants().parameters[3]==0);++cases;
     w.activeValue.map="Northrend";w.legacyFogKnown=true;w.legacyFog=NorthlightLegacyFog::Constants{};
     assert(w.legacyFogConstants().parameters[3]==0);++cases;
     w.legacyFogKnown=false;w.legacyFogHold.lastUpdate=GetTickCount()-NorthlightLegacyFog::Hold::HoldMs-1;
     assert(w.legacyFogConstants().parameters[3]==0);++cases;}
    std::printf("AO fog capture: %u cases, %s\n",cases,expectBug?"original regression reproduced":"PASS");
}
'''
    output = ROOT / 'out/fog-capture'
    output.mkdir(parents=True, exist_ok=True)
    cpp, exe = output / 'fog-capture.cpp', output / 'fog-capture.exe'
    cpp.write_text(harness, encoding='utf-8')
    expected = '--expect-bug' in sys.argv
    subprocess.run([str(fp.zig()), 'c++', '-target', 'x86-windows-gnu', '-std=c++17', '-O2', '-UNDEBUG',
                    '-DEXPECT_FOG_BUG=' + str(int(expected)), *fp.include_flags(), str(cpp), '-luser32', '-o', str(exe)],
                   env=fp.zig_env(), check=True)
    subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
