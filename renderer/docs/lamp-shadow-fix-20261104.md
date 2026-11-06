# Dalaran, orc and Stormwind lamp shadows

This follows the [earlier city-lamp audit](city-lamp-audit-20261104.md), which
identified failed Dalaran admission and two blocked orc floor setups.

## Dalaran: native sources and their own enclosure

The actual client has no ADT MDDF or active Dalaran WMO doodad placements for
`dalaran_lamppost01.m2` and `dalaran_lampwall01.m2`. Their GameObjectDisplayInfo
owners are 8091 and 8092. A fix that only generated placed FGL lights would miss
these objects. Their native intensity/enabled tracks also prevent ordinary
constant-source extraction.

The offline generator now records qualified native fixtures in
`lights/native-fixtures.bin` (NLF1 version 2). Qualification requires a DBC owner
and a unique rigid, opaque, unlit fixture region whose bounds contain the source.
These authored regions have openings; qualification does not claim a watertight
mesh. Runtime identification compares
internal name, vertex data and pointer-independent authored structure. Reading
name/vertices happens once per cold asset, within existing read limits. A stale
catalog or failed optional read preserves the ordinary native light without
granting shadow intent. Weapons, anonymous draw constants and attached objects
do not acquire fixture shadow intent merely through brightness or proximity.

Both real Dalaran sources pass the native reader, light bridge and shadow
selection without a model draw or an artificial FGL placement. The 64-byte
snapshot point layout, 12-yard native range cap and native energy fit remain.
The native clock still controls intensity and enabled state.

The live lamp can also cast a shadow over its own light. The post has 136 owned
luminous-region triangles. The wall model has 56 luminous triangles over a second
opaque layer that blocks every source direction. Its 56 base triangles pair
one-to-one with the luminous surface: identical UVs and texture, parallel normals
and winding, and vertex offsets of 0.002303–0.003821 model units. The remaining
266 metal triangles must stay opaque.

The offline proof recognizes only a complete, unique nearby copy of an already
owned luminous region, with exact UV correspondence and bounded displacement.
It does not remove the connected metal component. Exact local templates identify
the owned triangles in a live replay; production shader deformation and a
rigid transform must put the source at the selected light. Metal frames,
unmatched geometry and another lamp remain occluders. This path works without
an FGS lamp placement. Immutable mesh membership is cached; camera/pose changes
repeat only the linear transform verification, not the triangle search. A cold
copy retains only the owned vertices and required original shader streams for
that verification, with a 1 MiB limit per memo. Every owned vertex is checked;
the original GPU mesh and its surviving metal draw ranges remain unchanged.

An independent production-BVH check of 4,096 directions is fully blocked after
removing only the original 56 wall triangles. With both proven layers excluded,
3,517 directions open and 579 remain blocked by the retained metal.

Placed animated sources use FGA version 2 metadata with their original native
anchor/index. An unobserved or disabled source cannot illuminate through a
placeholder record, GI or a retained selection fade.

## Orcs: flame origin and physical occluders

The original native points of `orcbrazierstreetlamp.m2` and
`orchanginglantern.m2` sit at the flame nozzle, among the bowl/log geometry.
The generator derives a bounded, time-averaged luminous position from actual
particle motion, colour and opacity. The offsets are approximately 0.239 and
0.179 model units upward, with the authored lateral motion included. This is a
steady source approximation, not a simulation of the current particles.

Only a fixed nozzle, supported single inline clock and bounded rigid rotation
qualify. Unsupported motion, ambiguous emitters and unproven models retain their
original positions. All metal, logs and frame triangles stay in the scene.
Native energy, range and source identity are preserved. Constant world sources
remain available from the cache if native inventory is absent; raw-anchor
metadata prevents a second light from native draw constants.

The old stand's floor four yards below the nozzle is not the street model's
ground. Its authored base is at model Z=0, about 1.157 units below the corrected
source. Tests preserve the original blocked controls and add this actual ground
and a clear sector between metal supports. The low flame cannot cast a head
shadow onto the ground, so the articulation control moves an actual weighted
foot bone. The hanging lamp's bowl genuinely blocks the lower floor; a wall in
its open direction tests character and independent obstacle shadows.

## Stormwind: daylight composition

The current gameplay log has actor shadows, point shadows and soft shadows
enabled. It captures skinned geometry and renders the identified placed
`stormwindstreetlamp01.m2` source, but does not associate a specific actor with
each lamp. The following GPU reproduction therefore verifies the composition
defect, rather than reconstructing an exact pixel from that gameplay log.
The defect is reproducible later in the
pipeline: the previous shader suppressed the local shadow whenever the lamp
was weak relative to sunlight, while LocalDirect still added that lamp's light.

The production GPU counterfactual with the actual lamp and weighted character
has positive direct light and populated actor/obstacle shadow texels, yet zero
receiver shadow under the old daylight gate on both native D3D9 and DXVK.
The correction now removes the occluded lamp contribution independently of sun
strength. The existing 30% direct-light gain in sunlight and the tiny-energy
fade remain; sunlight itself is never subtracted by this correction.

## Cache compatibility

Cache regeneration uses [explicit generator revisions](cache-generator-versions.md),
not DLL versions or source hashes. Unversioned data upgrades once. A compatible
installed prebuilt variant also survives a new package that carries no prebuilt
variant, provided its client identity and required files still match.

No game executable is patched. Client assets are read only; proprietary models,
caches and captures used by the stand are not included in Git or the installer.

## Performance scope

The RTX 4090 stand uses one selected lamp, a real weighted character and an
independent box, a 256-pixel cube and a 512×512 receiver. Four balanced baseline /
candidate process pairs run on native D3D9 and DXVK, with eight samples of eight
frames per mode. QPC and validated GPU timestamps exclude fixture preparation
and readback. Warm static-cache runs rebuild zero faces; forced runs rebuild all
six. Front-facing and back-facing receiver normals are both covered.

For identical Stormwind inputs, paired median GPU deltas are 0.000736–0.001536 ms
for daytime PCSS and 0.001408–0.003072 ms for daytime hard shadows. The night
deltas are between −0.000160 and +0.000128 ms. Authored direct-light buffers are
byte-identical before/after the correction, including the 30% daylight gain.

Qualified Dalaran lights had no functioning native shadow path in the baseline,
so their baseline deltas are not an equal-workload performance claim. The current
complete cube/PCSS pass costs 0.0676–0.0778 ms per tested lamp across both backends
and refresh modes. The final ownership helper's unchanged-bank median is about
0.00017 ms. Moving-camera checks cost 0.01162–0.01283 ms for the post and
0.01048–0.01055 ms for the wall. Compacting the owned inputs reduced the DXVK
process measurements by approximately 4.8× and 2.9× respectively. All 1,024-call
warm, moving-camera and rigid-pose runs add zero triangle-membership proofs.
Every owned vertex still participates in current rigid-placement verification.
All 246 readbacks from the 22 city scenarios are byte-identical before/after this
CPU optimization; no GPU geometry or shading changed.

The native reader/bridge with 128 sources remains approximately 0.173–0.178 ms,
with the same 262 reads / 359,276 bytes and zero warm allocations. Fixture lookup
is performed once per selected light rather than once per replay. Light budgets,
distance limits, cube resolution and shadow-filter sample counts are unchanged.
These are component measurements, not an in-game city-frame/FPS guarantee.

Raw correctness and timing records are under `out/lamp-shadow-fix-20261104/`.
Reproduce the discovered city integration suite with
`python tests/test_city_lamp_regression.py`; it generates its seven fixtures from
the configured client and needs no files from this investigation's output folder.

## Regression coverage

- The city suite runs 22 GPU scenarios on native D3D9 and the installed DXVK:
  seven night fixtures, the two original physically occluded orc floor controls,
  and Stormwind daytime hard/soft shadows on each backend. Dalaran uses actual
  live lamp replays, native-reader classification and zero cached lamp casters.
- The preceding 16 lamp cases still pass on both backends, including renamed
  and scaled fixtures, foreign geometry, indexed/UP and list/strip replays.
  Existing positive thresholds are retained; physical floor negatives are
  additional controls, not replacements for positive character coverage.
- The independent local-source GPU oracle passes 360 cases / 297,480 checked
  pixels per backend on native D3D9 and DXVK 2, including 4–128 sources, daylight,
  source gains, partial/off shadows, nonnegative composition and transmission.
- Native CPU checks cover O0/O2 reader/bridge behavior, archive tag round-trips,
  stale/missing catalogs, optional read failures, disabled/distant sources,
  root/attachment distinctions and retained equipment/particle energy limits.
- Geometry tests reject incomplete, ambiguous, displaced, moving, differently
  textured or wrongly oriented duplicate layers. Actual source-owned geometry
  and foreign/frame controls also pass at O0/O2.
- Installer/cache tests cover explicit revisions, source-only reuse, required
  catalog repair, local/prebuilt compatibility, import closure and a real,
  reproducible four-tile cache build. No proprietary fixture is checked into Git.
