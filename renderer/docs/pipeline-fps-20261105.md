# Common pipeline cost and performance follow-up, 2026-11-05

This follows the `.7` rotation runs in `stream-lamp-followup-20261105.md`.
The next package is `0.3.217-test.20261105.9`; its final identity, checks,
installation state and package hash are recorded in
`out/pipeline-fps-20261105/summary9.json`.

The supplied game log is preserved as
`out/pipeline-fps-20261105/user-run/renderer.log`, SHA-256
`6ea6b489faacc44f8ce80c0c0dae0b1ef3fea0dc6642638acd683fc3b0bb9af4`.
It starts with Weather alone enabled, then restores the full effect profile at
renderer frame 5414. The first applying-effects sample is frame 8128. There is
no usable Weather-only world interval in this log. Restoring the configuration
file can explain the setting transition; it does not establish a user action.

The first standalone native GPU benchmark overlapped the user's rotation:
06:51:43.088249 through 06:52:21.880927 UTC. This was a measurement scheduling
mistake. Consequently the raw 63.61 FPS and 33–45 ms query-wait peaks in this
log cannot establish uncontended Northlight performance, a regression, or a
game FPS improvement. Subsequent standalone checks were serialized, with the
game-process guard checked before execution. The analysis script preserves
this exclusion when regenerated.

For orientation, the warm descriptive window is frames 8699–10018, with eleven
fully focused 120-frame intervals. Sampled stage medians below identify work
to investigate; their durations remain subject to the overlap above.

| Stage | Sample median, ms | Interpretation |
| --- | ---: | --- |
| CPU capture | 2.110 | Includes several capture paths; not model normals alone |
| CPU terrain capture | 0.245 | A subphase of capture |
| CPU model replay capture | 0.459 | A different subphase; not all material work |
| CPU material model setup, inclusive | 0.883 | Includes nested snapshot/pose work |
| CPU material pose envelopes | 0.225 | Current-pose bounds, not buffer I/O |
| CPU world lighting setup | 0.064 | Shared setup, including atmospheric job consumption |
| GPU world lighting | 0.035 | The lighting kernel itself is small |
| GPU AO | 0.938 | Largest individual sampled effect kernel |
| GPU AO blur | 0.241 | Additional AO cost |
| GPU final world composite | 0.504 | Full-resolution reconstruction/composition |

CPU and GPU overlap; inclusive CPU stages overlap their children too. Do not
sum this table. The original WoW scene also costs substantial GPU time.
Warm immutable model-source buffer reads are already zero in the examined
samples. Remaining CPU work includes admission, metadata lookups, pose bounds,
state capture and replay submission; calling all of it geometry readback would
misidentify the bottleneck. Material profiling also includes the native draw
submission, which exists with Northlight disabled.

Enabling Weather, AO or another scene option activates the shared scene
pipeline through `NorthlightQuality::pipelineWork`. It captures scene inputs,
obtains depth/model normals and native lighting parameters, and prepares the
world renderer before final composition. Base relighting is shared by these
modes, so toggling an option can expose more than the option's own shader cost.
Removing that relighting or its model normals would change the image. The log
does not support blaming its approximately 0.035 ms lighting kernel for the
entire FPS drop.

The implemented changes retain the existing light ranges, geometry, AO sample
counts, precision and quality settings:

1. Weather/AO/material-only views no longer allocate geometry render targets or
   upload world mesh pages when their consumers do not need them. The CPU scene
   remains available for Weather shelter. GI, world shadows, fog and enabled
   local lights retain geometry preparation. Enabling GI with zero strength
   still exercises reactivation in the fixture. An atmospheric job's callable
   now also remains alive until its existing join completes.
2. The final compositor skips fog/local texture reads only when the renderer
   proves their inputs neutral. All original fog weighting and accumulation
   arithmetic is retained, including the neutral case. Default zero flags keep
   the legacy reads. The added c80 upload occurs after shadow-transition passes
   that also use that register, under the existing full state restoration.
3. Model-normal capture reuses its already validated viewport. It also checks
   the retained frame-target identity before querying a descriptor and reuses
   immutable dimensions for subsequent draws on that target. End-frame/reset
   release that identity. The 640-draw fixture verifies fewer than 720 viewport
   reads including flushes; the previous implementation fails this bound.

The neutral-input final-compositor results below are prepared 3840×2160 shader
benchmarks, not whole-frame FPS measurements. They use the same official
compatible Microsoft compiler accepted by the existing freshness gates.

| Backend / output | Previous, ms | Updated, ms | Reduction |
| --- | ---: | ---: | ---: |
| Native D3D9 / SDR | 0.268752 | 0.236840 | 11.9% |
| Native D3D9 / HDR | 0.275760 | 0.227176 | 17.6% |
| DXVK / SDR | 0.274600 | 0.243040 | 11.5% |
| DXVK / HDR | 0.288128 | 0.252536 | 12.4% |

When both inputs are active there is no proven speedup: the prepared cases
range from essentially unchanged to approximately 6% higher compositor cost
(the largest difference is about 0.015 ms on native SDR). These results must
not be presented as an 11–18% game FPS increase. The earlier candidate which
also removed neutral fog arithmetic is superseded because it changed FP16
values; only the exact variant is included.

Validation evidence under the same output directory:

- `composite-native-exact/validation.json` and
  `composite-dxvk-exact/validation.json`: 124 comparisons each, every FP16
  component exact, covering six SDR/HDR entry points, four neutral-input masks,
  odd grids, edges, sky, depth discontinuities, water and 4K cases.
- `screen-native2/world-screen-gpu/validation.json` and
  `screen-dxvk/msaa{0,2}.log`: actual WorldRenderer, MSAA 0/2, exact screen-only
  output and retained CPU scene, geometry reactivation, resource pressure/retry,
  fog history, combined HDR/material paths and state restoration. The frozen
  `.7` renderer compiles but fails the new no-upload assertion in
  `screen-negative/world-screen-gpu/validation.json`, as required.
- `normals-dxvk9/validation.json`: all eight native VS2/VS3 rigid/skinned/weighted
  variants, exact 640-draw MSAA images, source mutation/eviction and fallback
  checks. `normals-native9/validation.json`: native D3D9 control with timestamps
  disabled, including the envelope proof. Different target identities and
  partial viewports are rejected. `normals-negative9/3-skinned.log` confirms the
  frozen prior header compiles and fails the redundant-viewport-read bound.
- Job-system O0/O2 checks, capture-skip wiring, layout, Python compilation,
  production shader freshness and official package verification pass.

An additional GTAO experiment skipped arithmetic when both sampled positions
were invalid. All 27 image comparisons were exact, but 4K timings showed no
consistent benefit and one case was about 3% slower. It remains only in
`ao-candidate/`; production AO source and bytecode are unchanged. No AO samples,
shadow detail, lighting distance or texture quality were reduced.

There is no clean in-game before/after run for this package yet. The previous
uncontended `.6`/`.7` runs were effectively equal at about 73.2 FPS; they cannot
measure these new changes. The near-lamp native-light issue from the earlier
report is not resolved by this performance work. The MPQ add-only-if-no-native-
source guard remains in place.

The additional geometry pass, intended for `.10`, addresses preparation before
those GPU passes. Its evidence is in `out/pipeline-geometry-20261105/`.

The material envelope calculation runs only with the material system active.
`NorthlightAutoBump::capture` immediately returns when `active()` is false;
`AutoBump=0` disables that path. The separate native model-normal receiver is
used by common lighting and can remain active with materials disabled. World
shadow/actor capture has its own demand gates, so these are different costs.

For recognized rigid or uniform one-bone M2 programs, material preparation now
uses the existing audited clip.w range proof before evaluating the pose
envelope. This is enabled only when every material consumer and the geometry
range are at most 48 yards. Its conservative threshold remains 48.01. Source
ownership, full layout checks, projection validation, N-patch exclusion, cold
scan limits and the proof's 0.25 ms budget are retained. Mixed bones, unknown
programs, wider ranges and exhausted budgets use the previous path. The extra
bounded proof cache occupies 32,816 bytes; it does not retain geometry payloads.

The producer passes this proof to model-normal capture only for the exact raw
source, shader and declaration, with an unchanged trusted constant-bank epoch
under the held draw gate. The normal consumer validates that identity, epoch
and N-patch state. A valid handoff avoids repeating the proof and avoids pixel,
raster, viewport and render-target preparation for a draw that contributes no
normal pixels. A stale handoff falls back to the existing proof/capture path.

Independently, normal capture now defers target/viewport admission until after
its own range proof. This also applies with materials disabled. The first
admitted target in each frame still establishes dimensions before packet
preparation, preserving resize/cache-reset order. Later proven-far draws need
no viewport or render-target read. Immediate capture, failed reads and native
draw ordering retain the existing fallbacks.

In the CPU-only buffered `prepare` fixture, balanced ABBA samples of 400 far
draws measured 0.2399 ms before and 0.1392 ms after the preflight, with 400 versus
zero envelope evaluations. This is about 42% less time for that prepared CPU
subtask, not a game FPS improvement. The fixture verifies retained current
source ownership, near/mixed/projection/N-patch/wide-range fallbacks and exact
epoch invalidation. GPU fixtures compare the same normal/depth pixels with and
without range optimization; the material-independent rigid case also requires
zero viewport reads for the rejected candidate. No shader bytecode, sample
count, lighting distance or quality setting changes in this follow-up.
