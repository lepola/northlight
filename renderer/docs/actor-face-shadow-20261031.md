# Character face shadow investigation

The face and shoulder boundaries reported against `0.3.216-test.20261031.9`
have a reproducible receiver-normal component. The packaged `WorldNormals`
and `ActorShadow` shaders produce facial bands and a triangular shoulder
boundary on an isolated HumanFemale mesh. The production repair now captures
the native skinned normal and uses it for depth-matched receivers. The same
camera fixture verifies the repair on native D3D9 and DXVK 3.

## Reproduction

The five production shaders (`ShadowVS`, `ShadowPS`, `WorldNormals`,
`ActorShadow`, `ActorTiles`) are extracted from commit `82a644ae` and verified
verbatim inside the `.9` DLL, SHA256
`c3664e15b22abb1394905998289d14e60675d31ac660e8aac0bcc3866ec63931`.
The source M2 and skin are read from the configured client's archives.
HumanFemale geoset 0 contains 544 vertices and 664 triangles.

The authored scene translates its bind pose to (-1844, 5406, 0), near the
reported city's coordinate scale. It uses a 1024-square actor map with the
production 12-unit half width, ordinary shadow bias and production two-step
tile reduction. The screen is 768 by 576, with the production normal and
visibility passes at half resolution. The shadow geometry, light matrix and
light direction remain fixed through 17 camera headings, -11.46 to +11.46
degrees, for each of three authored light directions.

The initial diagnosis used two normal-texture controls for `ActorShadow`:

- Production: the half-resolution `WorldNormals` result, including confidence.
- Model: interpolated, normalized authored normals, rasterized at full
  resolution alongside scene depth, with confidence 1.

No authored skin atlas, bump/POM/displacement, GI, AO, fog, HDR, temporal history,
animation, streaming or variable caster budget participates. The shadow map
is rendered directly from the same bind-pose vertices for both controls.

## Measurements

The scalar probe visits fixed triangle centroids. It retains only samples
whose projected full-resolution depth agrees within 0.01 world units, then
compares adjacent headings for the same sample. Head probes have world Z
above 1.65. Both backends retain 4,377 head-probe transitions.

| Backend | Transitions with visibility jump > 0.25, production | Model-normal control |
| --- | ---: | ---: |
| Native D3D9 | 73 | 18 |
| DXVK 3 | 76 | 18 |

The 95th percentile absolute jump is 0.06553 versus 0.04189 on native D3D9,
and 0.06768 versus 0.04321 on DXVK. These are isolated shadow visibility
measurements, not final screenshot RGB or game performance measurements.

## Interpretation and repair boundary

`worldNormalValue`, `wideNeighbour` and `tangentAxis` reconstruct surface
normals from depth and conditionally switch baselines and confidence. They
do not have the character's authored smooth normals. `ActorShadow` uses this
result for receiver lift and receiver-plane slope. Small curved model
features therefore inherit a different and camera-dependent shadow response
from the authored model surface. The control localizes a substantial part of
the demonstrated artifact to that normal/confidence input.

`NorthlightModelNormals` now captures opaque and alpha-tested lit M2 draws,
independently of AutoBump and shadow replay budgets. Its vertex patch copies
the normalized normal consumed by native lighting after the complete bone
transform. Native position, UV and lighting calculations remain intact. The
pixel patch reuses the existing native alpha dependency slicer. Capture uses
a private full-resolution FP16 normal/depth target and non-MSAA depth buffer,
including when the native scene is multisampled.

`WorldNormals` accepts this view normal only when its depth matches the final
scene within `max(.006, z*.0015)` and its squared length is between .5 and 1.5.
It rotates the accepted normal into world space and assigns confidence 1.
The shared normal buffer supplies directional shadows, GI and local lighting.
Uncaptured, hidden, invalid, blended, unlit and unsupported surfaces retain
the depth reconstruction. The native state, including UP stream/index bindings
and the two added varyings' WRAP states, is restored after each capture.
Targets are released on reset and memory-pressure trim; captures expire each
frame. No shadow bias, filter width or material setting changed.

## Production verification

The extended fixture runs the actual capture class and native client vertex
and pixel shaders before the updated `WorldNormals` shader. Its original
geometry, light directions, 17 camera headings, sampling rule and packaged
baseline shaders are unchanged. All eight SM2/SM3 rigid, single-bone, weighted
and four-influence programs pass on each backend, 16 cases total.

| Backend | Packaged .9 jumps > 0.25 | Production repair | Authored-normal control |
| --- | ---: | ---: | ---: |
| Native D3D9 | 73 | 21 | 18 |
| DXVK 3 | 76 | 21 | 18 |

Each case retains the same 4,377 head-probe transitions. The repair's 95th
percentile absolute difference from the authored-normal control is 0.01709.
A disabled-shadow negative control has error 0.39355 and is rejected by the
0.03 acceptance bound; zero jumps alone cannot pass. Nonfinite
visibility also fails. The facial bands and triangular shoulder boundary
are absent from the inspected before/after shadow-visibility image.

Additional controls pass for native alpha cutout (transparent coverage absent,
opaque coverage retained), identical capture with a 2x-MSAA native target,
stale depth, zero normal, disabled capture, native
PS constants/shaders/targets and UP stream/index restoration, WRAP restoration,
frame expiry and capture reset. No AutoBump instance is needed by this test.

The standalone material PR #97 builds as a Windows x86 DLL, SHA256
`fbc58190c1cc41b813a425969d89010d7a0a626bb4654f9efcd9a2b65a7df5e9`.
Its host wiring retains the branch's existing lifecycle and shader entry set;
no HDR pipeline or optional no-distance shader is imported. Both changed
shader payloads are verified inside the DLL and against their generated
arrays/manifests: `ModelNormalCapture` uses 13 SM3 slots and `WorldNormals`
uses 346.

The original combined-source validation also passes material variant,
private-depth, material replay-state, raster and shadow-history checks. Material binding equivalence passes again on
the fixed snapshot after an initial run detected concurrent source edits.
Two unrelated checks in that original combined snapshot remain failing: the HDR manifest test applies
a 512-slot limit to the existing 687-slot combined HDR shader, and the removal
smoothing test searches for an obsolete final-pass source anchor. Both
counterexamples were verified against unchanged HEAD artifacts.

The authored-normal control changes both normal direction and confidence, and its normal
texture has higher resolution. It does not separately quantify those three
contributions. Residual jumps remain. Bind-pose rasterization, point sampling
and probe visibility are still camera dependent. The tester's exact skin
atlas, pose, draw stream, lighting and settings were not captured, so this
does not identify every reported texture distortion or certify a complete
in-game scene. Residual probe jumps remain, as in the authored-normal control.
The capture adds one replay per admitted native M2 draw plus a normal/depth
target; whole-game frame cost has not been measured. Capture is bounded to
48 units of view depth, and allocation retains the existing depth-normal fallback under
insufficient address space.

## Evidence

Private inputs, source harness, bytecode, raw readbacks, per-probe CSVs,
`inputs.json`, `validation.json` and `actor-shadow-comparison.png` are in
`.local/actor-face-report-20261031/`. Client assets stay outside Git.

Re-run from the repository root with the configured local client/toolchain:

```powershell
python .local/actor-face-report-20261031/prepare.py
python .local/actor-face-report-20261031/run.py
python .local/actor-face-report-20261031/summarize.py
```

The repair, fixed source snapshot, final DLL/PDB, verified native/DXVK results,
`build-validation.json` and `character-shadow-before-after.png` are in
`.local/actor-face-fix-20261031/`. Reproduce the production comparison with:

```powershell
python tests/test_model_normals_gpu.py --fixture .local/actor-face-report-20261031 --backend C:/Windows/SysWOW64/d3d9.dll --output .local/actor-face-fix-check-native
python tests/test_model_normals_gpu.py --fixture .local/actor-face-report-20261031 --backend D:/Games/335_engb_mod/renderer-backends/dxvk/dxvk_d3d9.dll --output .local/actor-face-fix-check-dxvk
```

The runner requires WoW to be closed and uses hidden D3D9 devices. No game
session, installed file, setting, MPQ or world-cache change is part of this
investigation.
