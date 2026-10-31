# Offline Zangarmarsh camera-shadow investigation

Investigated package `0.3.216-test.20261031.9`, DLL SHA256
`c3664e15b22abb1394905998289d14e60675d31ac660e8aac0bcc3866ec63931`.
No game process was launched, attached to, or used for this investigation.
The renderer fix described below is implemented in the workspace. Installed
client files, settings and world-cache files were not changed.

## Reproduced fault

The native DetailDoodad shadow replay transforms world-space ground-cover
vertices into camera space with captured `c0..3`, then transforms them back
through the inverse camera and into the light with replacement `c4..7`.
These float operations do not cancel exactly at Zangarmarsh world coordinates.
Camera rotation therefore changes the rasterized position and UV of an otherwise
stationary caster. The UV-anchored Bayer alpha coverage amplifies these changes.

The controlled reproduction uses the client's actual `zmMush01` mesh and
`zm_detaildoodads.blp` texture, native DetailDoodad VS3 variant 0, the production
shadow-only vertex patcher, and the exact `ShadowReplayPS` bytecode found in the
packaged `.9` DLL. The mesh has eight vertices and four triangles. Its placements
are authored at real terrain triangle centroids around Swamprat Post
(114.584, 5210.417, 23.091), Expansion01 tile 22/31, area 3718. They are not a
capture of the client's procedural ground-cover placement or the tester's draws.

The light matrix, caster geometry, texture, opacity, draw order and render state
stay fixed while the camera rotates through 41 poses, -11.46 to +11.46 degrees.
The shadow map is 1024 by 1024 over the ordinary 48-yard cascade radius.

| Control | Changed coverage texels per adjacent pose | Total over 40 transitions |
| --- | ---: | ---: |
| Camera round trip, opacity fixed at 1 | 47–161 (mean 94.175) | 3,767 |
| Direct world-to-light projection, opacity fixed at 1 | 0 | 0 |
| Camera round trip, ordinary 0.5 alpha cutout instead of Bayer | 15–73 | 1,692 |
| Camera round trip with an authored native view-Z fade | 69–244 | 5,630 |

Native D3D9 and DXVK 3 produce the same counts. The direct-projection control
also has zero depth changes. The fixed-opacity round trip has 1,101 depth
changes greater than 1e-5 where both poses retain coverage. The opacity-fixed
map covers about 8,215 texels: its mean mask change is about 1.15% of that
coverage per pose. These are caster-map measurements, not final screen pixels.

The direct control uses the same patched vertex program and pixel bytecode,
with an identity first transform and the fixed world-to-light matrix as the
second transform. It is a diagnostic control, not a shipped renderer fix.
The separate view-Z fade uses an authored 24–64 interval; the tester's fade
constants were not captured. The precision failure remains when fade is fixed.

## Isolation and limits

A first test rendered 22,074 actual terrain triangles and sampled fixed world
positions under rotating cameras with the packaged WorldNormals/WorldLighting
bytecode. It did not reproduce the broad reported flicker: the largest extra
shadowing relative to geometric-normal input was below 0.07 at the tested low
light angle and below 0.001 at the other two angles. This narrows this
reproduction to the ground-cover caster path; it does not certify every terrain
view, time of day, cascade transition or complete scene.

GI, AO, material relief, HDR composition, temporal history, streaming, frustum
admission and variable capture budgets are absent from the caster-map test.
They cannot cause its measured changes. The caster-map failure is reproducible
without them, but the tester's complete video has not been replayed. Its exact
camera, procedural placements, render states and per-frame constants are not
available. This evidence identifies a camera-dependent shadow defect; it does
not establish that it accounts for every visible patch in the report.

The repair projects signature-verified DetailDoodad world positions directly
with c4..7 in the shadow-only vertex program. Its original c0..3 view transform
and c9 fade calculation remain intact. The final homogeneous translation uses
ADD, avoiding a second constant-register read in a Shader Model 3 instruction.
No extra shader instruction or register is required.

The replay constant bank receives the actual world-to-light matrix for grass;
other replay layouts retain their existing view-to-light projection. Directional
cascades, the fine actor map, local shadow cubes and diagnostic replay all pass
the matching matrix. Native grass draws, textures, alpha thresholds, dither,
fade constants, capture budgets and settings are unchanged.

## Fix validation

Both native D3D9 and the package's exact DXVK 3.1.1 backend pass the updated
Zangarmarsh reproduction. The fixed fixture uses the production shadow vertex
patcher and replay constant-bank helper, with the same real mushroom assets,
placements, lights and 41 camera poses as the original comparison.

| Case | Changed coverage over 40 transitions | Depth changes > 1e-5 |
| --- | ---: | ---: |
| .9, opacity fixed at 1 | 3,767 | 1,101 |
| Fixed projection, opacity fixed at 1 | 0 | 0 |
| Fixed projection, opacity fixed at 0.5 | 0 | 0 |
| .9 with native view-Z fade | 5,630 | 899 |
| Fixed projection with native view-Z fade | 713 | 126 |

The entire fixed full-opacity map is byte-identical to the frozen .9 direct
projection control at every pose, not merely equal after thresholding.
Zero opacity casts no texels; half opacity retains 4,781; full opacity retains
8,215. The remaining changes with an active view-Z fade are expected coverage
changes as its original camera-dependent fade changes, rather than a claim
that all camera-dependent grass behavior has been removed.

Additional acceptance:

- All 12 native VS2/VS3 variants pass optimized and debug CPU tests: clip
  position is independent of camera constants, native fade matches exactly,
  and unknown shaders leave the output untouched.
- Native D3D9 and DXVK 3 each pass the actual production capture test: 12 vertex
  variants, 16 pixel variants, 384 directional/local mask checks, fade 0/0.5/1,
  legacy alpha modes, transparent/additive/unknown/disabled controls and native
  state preservation. The dense 64-batch, 65,536-vertex fixture remains exact
  across four draw orders and both directional/local pixel shaders.
- Replay grouping passes 128,013 reference bank comparisons, including direct
  grass projection, native view/fade retention, repeated groups and unchanged
  actor projection. Bounded constant-bank and optimized/debug replay-validity
  regressions pass.
- Probe, draw-state and actor-map source assertions pass. Their full legacy
  clang++/sanitizer runners were skipped because that host compiler is absent;
  this is not reported as sanitizer acceptance.
- An isolated Windows x86 .9-source build plus these six production-file
  changes succeeds, including shader provenance gates. This avoids bundling
  concurrent material edits from the shared workspace. Its DLL is
  `.local/shadow-camera-20261031/build-dot9-grass-fix/renderer/frd9.dll`,
  7,588,864 bytes, SHA256
  `c108b7cf49c1ce9411a4c45c4022562d7ea5344829386ffb135e7a0a8fce476b`.
  Source identity:
  `4c4d84a34d37b75799ec013939a50280d31cd491a2e12ef921c11f57fcd90577`.

`fix-validation.json` records backend, source, asset and build identities.
`native-grass-fixed.csv` and `dxvk-grass-fixed.csv` retain per-pose counts.
No game process was launched and no client package was installed. This remains
an offline asset reproduction, not visual acceptance of the tester's full scene.

## Evidence and reproduction

Private assets, source harnesses, package/shader identities, CSV measurements,
raw map readbacks and the comparison image are in
`.local/shadow-camera-20261031/`. No proprietary assets are committed.
`validation.json` and `grass-assets.json` record the measured inputs and results.
The preparation verifies the relevant patcher/math source against `82a644ae`
and verifies the seven relevant shader payloads are present in the package DLL.

The original baseline was prepared from the .9 source snapshot with the
configured client and compiler (the preparation deliberately verifies .9 source
identity, so it is not rerun against the modified renderer):

```powershell
python .local/shadow-camera-20261031/prepare.py
python .local/shadow-camera-20261031/inspect_grass.py
python .local/shadow-camera-20261031/run.py
python .local/shadow-camera-20261031/run_grass.py
python .local/shadow-camera-20261031/summarize.py
```

The runners refuse to proceed while `wow.exe` exists. GPU work uses hidden
D3D9 devices and isolated output surfaces. The summary asserts that both
backends reproduce the round-trip mask instability and that the direct
projection control has zero mask and depth changes.

To repeat the fix checks using the retained private baseline assets/readbacks:

```powershell
python .local/shadow-camera-20261031/validate_fix.py
python .local/shadow-camera-20261031/summarize_fix.py
python tests/test_detail_doodad_shadow.py
python scripts/run_tests.py test_grass_shadow_gpu test_replay_validity_fastpath --jobs 1
```
