"""Read-only shader allowlist from the active client. Hashes only, no game bytecode shipped."""
from pathlib import Path
import json,re,sys
root=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(root),str(root/'renderer')]
from world_scene_builder import Assets
from extract_signatures import variants,fnv1a64

assets=Assets()
vertex={};pixel={};names=[]
try:
    for name in assets.providers:
        m=re.fullmatch(r'shaders\\(vertex\\vs_[23]_0|pixel\\ps_[23]_0)\\([^\\]+)\.bls',name)
        if not m:continue
        stage,family=m.groups()
        if stage.startswith('vertex'):
            kind=1 if family=='terrain' else 2 if family in ('mapobjdiffuse_t1','mapobjspecular_t1','mapobjdiffuse_comp') else 3 if family=='detaildoodad' else 0
        else:
            kind=1 if family in ('terrain1','terrain2','terrain2_pcf','terrain3','terrain3_pcf') else 2 if family=='terrain0' else 3 if family in ('mapobjdiffuse','mapobjspecular') else 5 if family=='mapobjopaque' else 4 if family=='detaildoodad' else 6 if family=='mapobjcomposite' else 7 if family.startswith("combiners_") else 0
        if not kind:continue
        table=vertex if stage.startswith('vertex') else pixel
        for index,_,code in variants(assets.read(name)):
            table.setdefault(fnv1a64(code),kind)
        names.append(name)
finally:assets.close()
lines=['// Generated from enabled client archives; exact original shader identities.','#pragma once','#include <cstdint>',
       'struct AutoBumpIdentity {std::uint64_t hash;unsigned kind;};']
for name,table in [('Vertex',vertex),('Pixel',pixel)]:
    lines.append('static constexpr AutoBumpIdentity kAutoBump%s[]={'%name)
    lines.extend('    {UINT64_C(0x%016x),%d},'%(h,k) for h,k in sorted(table.items()))
    lines.append('};')
(root/'src/gamedata/auto_bump_identities.h').write_text('\n'.join(lines)+'\n')
out=root/'out/bump-recon/identities.json'
out.write_text(json.dumps({'vertex':len(vertex),'pixel':len(pixel),'families':names},indent=2)+'\n')
print('Bump identities:',len(vertex),'vertex,',len(pixel),'pixel')
