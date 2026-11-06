# Additional world emitter coverage

Starting from `794fd3b1`, the native model stage enumerated GameObjectDisplayInfo
but missed map/WMO props without a display row. Static metadata cannot represent
all animated bones/materials. The stage now considers every listed canonical
`World/*.m2` as well as the existing DBC paths, using the same emitter qualification
and native lifecycle writer already used for herbs and other gameobjects.

Existing world-metadata and staged attached-model owners are excluded before
generation. Existing native light arrays are preserved. The original sixteen-source
model limit, position/energy bounds, runtime light limits and shaders are retained.
No production C++ or GPU allocation changes are required.

Admission uses actual supported additive material/particle RGB, texture, opacity,
skin bone and original rate/enable tracks. Generic sprite names do not determine
emission: tinted smoke/water sprites also implement magical glows, flames and
alchemy effects. Ordinary alpha-only materials and unsupported replacement colors,
external schedules, hidden/deforming mesh and invalid sources remain refused.

## Configured active-client audit

| Check | Result |
|---|---:|
| Additional listed World candidates absent from static metadata and the DBC list | 7,508 |
| Qualified additional models before native serialization | 388 |
| Additional validated models / native sources | 380 / 1,421 |
| Standalone combined gameobject/world stage | 885 models / 3,351 sources |
| Integrated stage after other native owners | 878 models / 3,329 sources |
| Exact model-only MPQ readbacks | 885 |
| Previously staged models removed | 0 |

The eight additional models refused by native serialization remain reported with
their existing position/color validation failure. All 380 additions are disjoint
from the reviewed static-light catalog and the compiled world emitter metadata.

Examples include Karazhan chandeliers/sconces; Blood Elf and Draenei candles and
lamps; Bloodmyst, Silvermyst and Terokkar crystals; glowing Coilfang/Zangarmarsh
mushrooms; Scourge braziers; Vrykul lamp posts; and Iron Dwarf chandeliers.
These counts describe supported assets, not simultaneous runtime light slots or
proof of every model's placement in a particular map.

Authored fixtures verify a World model absent from GameObjectDisplayInfo is
enumerated and staged, while another lighting owner's model stays excluded.
A separate animated world fixture reads back its original rate and enable values
at eight times, including zero emission, on/off transitions and a global-clock wrap.
Original model/DBC bytes and the model-only MPQ transaction are checked.

Client-dependent tests generate fixtures from the user's archives in ignored
output folders. No proprietary assets or caches are committed, no client files
are installed and no game session or whole-game FPS measurement is performed.

Reproduce with `test_art_layer`, `test_attached_emitter_sources`,
`test_native_model_install`, `test_world_emitter_index`, `test_world_emitter_coverage`,
`test_equipment_emitter_patch`, `test_mount_emitter_patch`, `test_spell_emitter_patch`,
`test_glue_emitters`, `test_particle_light_capture`, `test_install_world_cache`
and `test_northlight_install` through `scripts/run_tests.py`.
All twelve checks pass. Stock, active and repeated-active archives validate;
both repeated active archives have SHA256
`2eeacd3ac92d0eaf0db58c763239ee156d23518141a782fe2e09cb72ae62ac44`.

Apply with an installer built from the updated PR so it rebuilds patch-z. Retain
`EquipmentLights=1` for native/live light capture (Quality default) and let the
installer perform cache-freshness checks. No manual WoW Cache deletion is needed.
