# Cyan object: Swamp Gas client-file match

The best-supported match for the cyan cloud beside Umbrafen Lake is
`creature\cloud\cloudswampgas.m2`, CreatureModelData **2307**. Its cyan body and
separately textured purple sparkles match the screenshot. CreatureDisplayInfo
**18075** selects ParticleColor **286**, with model scale 0.5 and display alpha
190/255. Display **20373** uses the same cyan palette at scale 2.0. The screenshot
does not establish a unique server creature entry or GUID; 18075 is the normal-size
client variant, rather than a recovered server ID.

The model has four particle emitters. Its star uses `purple_star.blp`; the two
smoke emitters use the 8x8 `toonsmoke16.blp` flipbook. M2 particle color selectors
11, 12 and 13 select columns from ParticleColor's Start[3], Mid[3], End[3]. Palette
286 produces cyan smoke; other displays of this same M2 select purple, green,
orange and yellow/green palettes. The model's literal RGB alone cannot identify
the rendered color. Format references: [ParticleColor build 12340 definition](https://raw.githubusercontent.com/wowdev/WoWDBDefs/master/definitions/ParticleColor.dbd)
and [WoW Model Viewer replacement-color evaluation](https://wowmodelviewer.github.io/wowmodelviewer/particle_8cpp_source.html#l00152).

All six relevant inputs (M2, both textures and the three display/model/palette
DBCs) are byte-identical between the configured stock and active archive chains
excluding generated patch-z. M2 SHA256:
`e863b158c0edb8b746a05a6047e42014d75ab73993d820e4995c488d9282742b`.
ParticleColor.dbc SHA256:
`5813e3a16315e150df1e571955b5e412281f211df3ce9cb65621639fb486f82f`.

## Coverage and fix

The cyan body is already supported through live additive particle capture, which
reads the actual rendered display color, texture, alpha and position. This is a
diffuse cloud, so its light has no compact-source shadow flag.

The audit reproduced a separate offline staging gap in the same model. Its
non-recolored ripple (particle 1, color selector 0) passed emission qualification
but was rejected when the shared model advertised any display palettes. The guard
now recognizes both -1 and 0 as unreplaced, consistent with the particle qualifier.
It stages only that original-color ripple; palette-controlled particles 0/2/3
remain excluded from universal native light generation. The negative fixture now
uses the actual replacement selector 11 instead of incorrectly treating 0 as one.

`test_particle_light_capture.py` reads the user's own client assets at runtime.
Across five palettes, three lifetime alpha keys and every used atlas frame it
checks 1,935 cases on each of x86/x64 O0/O2: 730 visible cases and 144 cyan body
cases per run. Native shader color, texture and alpha composition pass; an
independent Python texture decoder bounds RGB565 rounding within two byte quanta.
Empty/subthreshold sprites stay dark, diffuse sources stay unshadowed, and
retirement has no light tail. Native staging and the model-only MPQ byte roundtrip
also pass for the real ripple, with all three colored emitters explicitly refused.
Synthetic tests preserve the original model and reject actual replacement-colored
sources while allowing -1/0 alongside shared display palettes.

At `794fd3b1`, the full stock, active and repeated-active art-layer tests pass. The integrated
gameobject stage remains at 498 models. Both repeated active archives have SHA256
`130b26981400759e3e1a8d61ceba27c7699e5c647fb8a91d170049bda510a535`.
Cache installer, native model installer and point-transmission transaction tests
also pass. No production C++ or shader changed in this follow-up.
Further world-model coverage is recorded in [the world-prop follow-up](world-native-coverage-20261031.md).

No game session or visual/FPS acceptance was performed. Proprietary model, texture
and DBC fixtures are generated only in ignored test output, never committed.

## Applying the changes

Use an installer built from the updated PR. Rerunning it rebuilds the lighting art
layer (`patch-z`) from the client's files and performs the normal cache-freshness
checks. This installs the new herb/gameobject model lights and the ripple fix.
The changed `attached_emitter_sources.py` is in the lighting-cache source digest;
let the installer refresh affected data. No manual full terrain rebuild or WoW
`Cache` deletion is needed.

The cyan cloud's main colored glow uses the live renderer path and needs no extra
asset-cache regeneration. Set `EquipmentLights=1` in `[Quality]` of
`northlight-quality.ini` (Quality's default; Balanced/Performance default to 0),
keep local lighting enabled, and restart after installing the updated PR build.
Regenerating with an older installer does not include the new staging changes.
