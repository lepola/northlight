#!/usr/bin/env python3
# northlight-test:
"""0.3.159 shadow removal smoothing (R1) post-compile gate (run by name after compile_world_shaders.py).

PENDING until world-shader-build.json matches world_effects.hlsl. Then: TemporalLight is the only
world shader whose bytecode changed from 0.3.158 (besides WorldComposite, which reads the
horizon lift colour from c35.yzw, and SourceVisibilityPS with its wrap ring, since 0.3.163; and
WorldLighting (baseline alpha) plus LocalDirect (daylight lamps), since 0.3.165; WorldNormals' wide-sample
threshold since 0.3.175), it stays within the SM3 limits (slots reported),
it samples Scene (s0) and BaselineLighting (s12) besides its 0.3.158 samplers, and every .bin
matches the manifest. Reads files only; no Wine, GPU or game."""
import sys; from pathlib import Path; sys.path.insert(0, str(Path(__file__).resolve().parents[1]))  # repo root
import northlight_paths as fp
import hashlib,json
BEFORE={ # world-shader-build.json of 0.3.158
    'WorldNormals':'1e8d22d66a1022bad26c930dd719398eba02c22179084a275f4fe52bf54c1db5',
    'WorldLighting':'e132fcd3de96d86e61a1b33cecfd981e8a633bd8098946b3addf571306a680bb',
    'WorldGI':'23dfdd6e1d08800f8958b278b09aebc97f0558f6b0ac83b5cf447891ef1466e7',
    'WorldFog':'4b40e8e5e49892a4f47b41bc21bb1079abf61664d7d623abc12a784efda189fe',
    'FogBlur':'b48b12488d6233e53031b9f947e643a47d5892fc7fd478356a0a08037a3a1e78',
    'LocalDirect':'94b34eebb6a542f79c23643c9b9b25ce70d49318c36703e5abf1692604bd7c8f',
    'TemporalLight':'9dd41a793a99539b457cc5cab7ab3aeaf3638349106addaa6ef970fa163aceff',
    'LocalFog':'3439bb63d20ae45ae5cd2d7975e7d97231d3445ab086063d65af1d44232d4f70',
    'SourceVisibilityPS':'55e6fbcb330f068f97418c9059fc6433381f3ec206c6563aa03a7abf9f2a0a1f',
    'WorldComposite':'45c3bf3aa0a864ed601f34534555bd85f744c8b4e3f914f59ebb810bd28b322a',
    'ShadowVS':'10172c530a92bd9cd5bbf13dff11ebbb53888c355b5b9b13a8aebe764215c165',
    'ShadowCacheVS':'54092470516c229529f1e86cadacb240ac6fcb2a877114b00e0cfed1d4f7a753',
    'ShadowPS':'318fe2a445a91294b3f1b0a4711ba226fee07397c1f2816d9d96d09a18fdbffd',
    'ShadowReplayPS':'194f5665d22f12a8c0e337be2938d036386c4e7982f048051a804714f85a592d',
    'ShadowUnion':'d090109910314885bc31fdf67ebd4c393f95c499da6dbd0c055498981f28d4c6'}
manifest=json.loads(fp.src('world-shader-build.json').read_text())
source=hashlib.sha256(fp.src('world_effects.hlsl').read_bytes()).hexdigest()
if manifest['source_sha256']!=source:
    print('PENDING world_effects.hlsl is not compiled yet (world-shader-build.json is older); run scripts/shaders/compile_world_shaders.py, then this check')
    sys.exit(1)
shaders=manifest['shaders'];checks={}
changed=sorted(k for k,v in shaders.items() if v['sha256']!=BEFORE.get(k))
checks[f'only TemporalLight (and the WorldComposite) changed (changed: {changed})']=changed==['LocalDirect','SourceVisibilityPS','TemporalLight','WorldComposite','WorldLighting','WorldNormals'] and sorted(shaders)==sorted(BEFORE)
t=shaders['TemporalLight']
# 0.3.174 (r72 decisions-g1 §8): an exact budget of 509, 0.3.185: 512 (receiver-plane test; was 509) (the ps_3_0 minimum 512 is the hard gate). Headroom is
# exhausted: any later TemporalLight change must free slots first, or this fails loudly.
checks[f"TemporalLight {t['static_instruction_slots']} slots == 512 (<= 512), {t['temporary_registers']}/32 temporaries (0.3.158: 217, 11)"]=t["static_instruction_slots"]==512<=512 and t['temporary_registers']<=32 and t['target']=='ps_3_0'
checks[f"TemporalLight samplers {t['samplers']} = 0.3.158 [1,8,14,15] + Scene s0 + AO/bloom s10 + BaselineLighting s12"]=t['samplers']==[0,1,8,10,12,14,15]
checks['every .bin matches its manifest hash']=all(hashlib.sha256(fp.src(f'{k}.bin').read_bytes()).hexdigest()==v['sha256'] for k,v in shaders.items())
for name,ok in checks.items():print(('PASS ' if ok else 'FAIL ')+name)
assert all(checks.values())
print('PASS shadow removal smoothing compile gate')
