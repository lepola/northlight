# Herbs and server-spawned emitter coverage

Ragveil in the supplied screenshot is GameObjectDisplayInfo **6949**, resolving
to `world\skillactivated\tradeskillnodes\bush_ragveil.m2`. Its glow is an unlit
additive mesh on original billboard bones. The previous native extractor
rejected those bones, and the installer did not enumerate GameObjectDisplayInfo.
Static ADT placements cannot describe a server herb's harvest/despawn lifetime.

The installer now stages proven emission from GameObjectDisplayInfo model paths
through the existing native model transaction. Original bones, material evidence,
particle lifecycle, sixteen-source model limit and archive readback remain in use.
Billboard meshes retain their original native bone matrix rather than baking a
camera orientation. Harvestable `bush_` models are excluded from static emitter
metadata. Other metadata/native owners are excluded before gameobject staging.

Live additive model meshes may set CastShadow only when every triangle is read,
the draw fits the existing sixteen-triangle sampling cap, and its view-space
bounding diagonal is at most three yards. Particle draws, large meshes and partial
reads retain no shadow intent. Mixed mesh/particle clusters clear the flag, and
intent is refreshed each frame with no despawn tail. Existing point-shadow slots,
light limits, upload budgets and shaders are retained.

## Offline client checks

The active archive chain excluding generated patch-z produced these results at
commit `6a54ec92` (before the color-index follow-up below):

| Check | Result |
|---|---:|
| Standalone gameobject candidates after static-owner exclusion | 3,343 |
| Models staged / native light records | 505 / 1,930 |
| Existing native / unsupported / nonemissive models | 94 / 273 / 2,471 |
| Integrated gameobject stage after other native owners | 498 models / 1,908 lights |
| Integrated native model byte readbacks | 498 |
| Herb models staged | 14 |

Herbs: Ancient Lichen, Bruiseweed, Crownroyal, Dreaming Glory, Felweed, Flamecap,
Frozen Herb, Icethorn, Mana Thistle, Netherbloom, Nightmare Vine, Ragveil,
Spineleaf and Terrocone (client model spellings). Ordinary nonemissive herbs are
not made luminous. Other staged examples include blue glowing Coilfang mushrooms
(display 7756), Zangarmarsh floating spores (6911), druid wisps (250), crystal
portals (9321), Blood Elf lanterns and multiple brazier families. These are display
IDs, not server gameobject entry IDs. The cyan object's best client-file match and
palette-aware coverage are documented in [the Swamp Gas follow-up](cyan-object-coverage-20261031.md).

The stock and active art-layer builds each stage 498 gameobject models with
disjoint metadata ownership. Repeated active archives are byte-identical:
`12b98fd683ee8127d0cef5858278e161569ae28781c4cd96a69afcaadb5d9a21`.

Validation uses `test_art_layer`, `test_attached_emitter_sources`,
`test_native_model_install`, `test_world_emitter_index`, `test_glue_emitters`,
`test_equipment_emitter_patch`, `test_emitter_patch`, `test_world_emitter_coverage`, `test_spell_emitter_patch`,
`test_mount_emitter_patch`, `test_point_transmission_install`,
`test_point_transmission_cache`, `test_particle_lights`,
`test_particle_light_capture` and `test_local_source_shadows`.
Native shader capture passes x86/x64 O0/O2, including compact mesh admission,
large/missing-triangle rejection and source retirement. The point-shadow GPU
oracle uses an independent device without entering the game. The existing shadow
reader test's invalid-bit sentinel is corrected from supported GlassEmitter (16)
to unknown bit 32; the native M2 roundtrip expectation also retains the existing
CastShadow flag rather than expecting zero.

The standalone Windows x86 renderer builds. No assets, archives or generated caches
are committed; no client installation, game session or visual/FPS acceptance was
performed. Unavailable assets, unsupported animation/materials and native menu
slot ownership remain outside this coverage claim.

Reproduce the integrated checks with the user's configured client and StormLib:

```text
python scripts/run_tests.py test_art_layer test_attached_emitter_sources test_native_model_install test_world_emitter_index test_particle_lights test_particle_light_capture test_local_source_shadows
python scripts/build_renderer.py
```
