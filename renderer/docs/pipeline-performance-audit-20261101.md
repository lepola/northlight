# Northlight pipeline and shader audit — 2026-11-01

This work removes redundant HDR/AO work, skips provably empty fog passes, and
adds fixed-quality ASSAO. The reported gameplay case is Crystalsong Forest,
airborne and looking down. All measurements below use standalone hidden devices;
the game was not launched and the installed client was not modified.

## Scope

The initial audit read 17 HLSL sources/includes and their host call paths, and
checked all 140 variants in eight manifests: bytecode hashes, embedded arrays,
assembly metrics, registers, samplers and shader-model limits. Cube sampling is
counted separately. Static texture instructions are not dynamic loop tap counts.

The traced frame includes native scene/depth/water/actor capture, AO, world
lighting/GI/shadows/fog, material composition, deferred effects, HDR
exposure/bloom/output and UI/presentation, including reset and fallback.
Numerical GPU tests target changed paths and their contracts. This is not a
capture of the entire gameplay scene or a claim that every possible defect is fixed.

## HDR changes

1. HDR uses ten new alpha-only variants for existing SSAO/HDAO/HBAO+/GTAO
   qualities. They remove five discarded SDR bloom reads per half-resolution
   pixel: 10,368,000 scene reads at 4K. High static slots change from 289 to 263
   for SSAO, 254 to 228 for HDAO, 314 to 288 for HBAO+, and 491 to 464 for GTAO.
   Actor AO already has alpha-only variants. Disabled AO clears a neutral target
   in HDR, and HDR fallback skips the unused SDR color-bounce draw.
2. When HDR sky is captured, covered native geometry decodes inside the world,
   unlit or combined material compositor. The owned sky resolve remains, but
   the separate full-screen `HdrDecodeWorld` draw disappears. The strict raw
   clear-depth test preserves distant geometry in the .94..1 depth interval.
   Legacy highlight expansion, name masks and unsupported formats retain the
   original path. If inline composition fails, fallback decodes over the saved
   sky rather than copying a partly overwritten output or resolving it again.
3. Combined HDR material composition decides its path before scratch allocation.
   A fresh 4K frame avoids an unused 66,355,200-byte FP16 surface (63.28 MiB).
   Separate passes, limited caps, shader refusal and diagnostic captures still
   allocate their required scratch. Existing scratch can remain retained to
   avoid allocation churn. This is a memory saving, not a per-frame timing claim.

Other stages retain native shader patch caching and UI blending, DF24/INTZ
depth resolve policy, coherent ownership masks, MRT normals/distance, cached
directional maps, actor PCSS, GI probe history, local-light selection and
material caches. Normal exposure does not read GPU results back to the CPU.
Compatibility UI fallback can require full-screen work per draw, but the
available log recorded zero compatibility layers. AOBlur already has a
nine-bilinear-read early-out for open surfaces. No sample counts, MSAA or
existing quality defaults were reduced by the HDR/fog optimizations.

## HDR composition timings

RTX 4090; DXVK 2.7.1/3.1.1; interleaved warm old/new GPU timestamp batches on
one device. Production composition/material shaders, authored scene/depth,
actual MSAA, particles and UI are used. AO/GI/shadow/fog generation uses fixed
inputs. These timings measure the composition graph, exclude AO savings, and
may include CPU submission gaps.

| DXVK / scenario | Before, ms | After, ms | Change |
| --- | ---: | ---: | ---: |
| DXVK 2 / world-1080 | 0.118848 | 0.120299 | +1.22% |
| DXVK 2 / world-4k-msaa4 | 0.606080 | 0.608917 | +0.47% |
| DXVK 2 / material-particles-4k-msaa8 | 1.078464 | 1.093525 | +1.40% |
| DXVK 2 / manual-no-bloom-4k | 0.389995 | 0.384149 | -1.50% |
| DXVK 2 / sky-4k-msaa8 | 1.517803 | 1.432619 | -5.61% |
| DXVK 2 / sky-downward-4k-msaa8 | 1.532309 | 1.436864 | -6.23% |
| DXVK 2 / sky-world-1080 | 0.154667 | 0.146155 | -5.50% |
| DXVK 2 / expansion-1080 | 0.181845 | 0.183061 | +0.67% |
| DXVK 2 / stencil-1080 | 0.285653 | 0.287232 | +0.55% |
| DXVK 2 / sdr-copy-4k-msaa8 | 1.145664 | 1.149184 | +0.31% |
| DXVK 2 / material-fallback-1080 | 0.135168 | 0.136640 | +1.09% |
| DXVK 3 / world-1080 | 0.096107 | 0.097835 | +1.80% |
| DXVK 3 / world-4k-msaa4 | 0.571883 | 0.568960 | -0.51% |
| DXVK 3 / material-particles-4k-msaa8 | 1.059093 | 1.026688 | -3.06% |
| DXVK 3 / manual-no-bloom-4k | 0.407168 | 0.402304 | -1.20% |
| DXVK 3 / sky-4k-msaa8 | 1.598869 | 1.484331 | -7.16% |
| DXVK 3 / sky-downward-4k-msaa8 | 1.626496 | 1.484224 | -8.75% |
| DXVK 3 / sky-world-1080 | 0.146667 | 0.135979 | -7.29% |
| DXVK 3 / expansion-1080 | 0.171371 | 0.173269 | +1.11% |
| DXVK 3 / stencil-1080 | 0.310400 | 0.285760 | -7.94% |
| DXVK 3 / sdr-copy-4k-msaa8 | 1.185429 | 1.149163 | -3.06% |
| DXVK 3 / material-fallback-1080 | 0.119211 | 0.121344 | +1.79% |

The downward-sky 4K/MSAA8 case saves 0.095 ms on DXVK 2 and 0.142 ms on DXVK 3
(6.23% and 8.75% of this graph). Other paths vary slightly; the largest measured
increase is about 0.015 ms. These results do not establish Crystalsong Forest
FPS. A 4K FP16 MSAA8 surface alone has 506.25 MiB of logical sample storage,
before compression/alignment; its resolve and composition still consume bandwidth.

Initial validation covered all 140 payloads; 2,785 HDR Frame cases per backend;
sun/halo/veil transfer controls; production proxy HDR/SDR/MSAA8-triple cases
(249/231/254 per backend); AO alpha and actor-reference comparisons; production
WorldRenderer combined/fallback/caps/refusal/diagnostic paths; and CPU lifetime,
UI, fold/blur, capture/depth, fog, haze and material-support checks. Existing
source guards were updated for already-present HDR signatures/budgets and
approved AO hashes. Numerical image thresholds were not relaxed.

## Fog follow-up

The path traced regional bounds, atmosphere preparation, WorldFog and actor/tiled
variants, FogClouds, local fog/combine, temporal history, two blur passes and
SDR/HDR composition.

- Clip only the cloud raymarch tail beyond the last possible bank. The prefix
  before entering a cloud must remain: its base-air optical depth attenuates
  later cloud light. An attempted prefix cut failed GPU comparison and was rejected.
- Skip march, temporal and both blur passes when the field is empty or the
  global height support is unreachable. Unknown bounds fail open; local lamps
  retain their independent longer-range path. Clear neutral fog/history to
  `(0,0,0,1)` and retain valid history for the next active frame.
- Cloud static slots: base 469→462, actor 598→589, tiled 663→654. Temporary
  registers remain 22/25/26. Other fog kernels remain unchanged.

4K authored inputs, half-resolution FP16 fog, warm interleaved GPU timestamps:

| Base FogClouds case | DXVK 2 before → after, ms | DXVK 3 before → after, ms |
| --- | --- | --- |
| Along the layer (7) | 0.564 → 0.388 (−31.1%) | 0.359 → 0.275 (−23.5%) |
| Looking downward (17) | 0.571 → 0.549 (−3.8%) | 0.401 → 0.378 (−5.7%) |
| Looking upward (18) | 0.543 → 0.515 (−5.1%) | 0.372 → 0.342 (−8.3%) |

Downward actor/tiled savings range from 0.2% to 11.6% depending on backend.
Case 13 has no useful interval reduction; the largest increase was 0.0048 ms
for DXVK 3 actor fog. These are pass timings, not game FPS.

Validation: 180 exact FP16/FP32 comparisons across three variants and both
DXVKs, plus 48 at 4K; boundaries, horizontal/vertical rays, water, unknown
bounds and actor shadows. Production WorldRenderer on both DXVKs with MSAA0/2
preserves final color and the 0.85 history weight across leaving/re-entering
fog, while eliminating empty draws. Sixteen native fog proofs, 34,000 interval
checks and all 37 world shader/header payload checks passed.

## AO methods and ASSAO

ASSAO is selectable through Lighting & AO or `AOMethod=ASSAO` / `4`, using the
existing strength/radius controls. Defaults remain SSAO/High. See
[AO settings and implementation](ao-methods.md).

| Method | High / Medium / Low kernel samples | Behavior |
| --- | --- | --- |
| SSAO | 8 / 8 / 8 | Angular obscurance with quadratic falloff; quality aliases the same shader. |
| HDAO | 16 / 8 / 8 pairs | Opposite-sample valley detection; Medium and Low compile identically. |
| HBAO+ | 8×4 / 6×3 / 4×4 | Directions × radial steps, normal bias and squared-distance falloff. Medium→Low reduces 18 taps to 16. |
| GTAO | 4×4 / 3×4 / 2×4 pairs | Horizon integration, open-hemisphere normalization and thin-occluder falloff. High skips sub-texel reads; lower qualities zero their contribution. |
| ASSAO | 12 / 5 / 3 pairs | Progressive weighted Intel samples and near-camera radius limiting; Medium/High retain detail/edge/halo terms. |

Counts exclude receiver/normal/water reads, bloom and filtering. HDAO's unused
normal reconstruction is compiled away. Static SM3 slots do not count repeated
loop work: HBAO+ has 314 SDR slots at every quality but different execution costs.

ASSAO adapts [Intel's fixed Q0/Q1/Q2 implementation](https://github.com/GameTechDev/ASSAO/tree/c21d57a95b43c700ee01a2eb18f5589d7becefb2),
with the MIT notice in `licenses/ao-methods.txt`. Northlight supplies its normal
reconstruction, rotation and bilateral filter. Adaptive Q3, depth mips and
deinterleaving are not included; parity with WoW Forever's integration is not
claimed. No new textures or preparation passes are added. ASSAO can produce
stronger contact shading at the same strength and does not use the specialized
HBAO+/GTAO actor-thickness correction.

Zero-offset samples are explicitly rejected. Normalizing a tiny reconstructed
receiver delta under fused arithmetic previously created false distant-terrain
shadows on DXVK 3: 21/9/9 grazing failures for High/Medium/Low before the fix,
zero afterward, while genuine concave contact remains.

SDR world composition uses ContactBloom RGB/AO alpha. SDR fallback retains the
original color-bounce draw and replaces only alpha for advanced methods.
That replacement now uses `*Only`, eliminating five discarded scene-color taps.
HDR uses the same AO calculation and half-resolution FP16 target, with bloom
built later from linear radiance. HBAO+/GTAO High can additionally use ownership
and AOActorPrepare; HBAO correction fades between 2 and 3 yd. Other methods and
qualities do not acquire those passes.

All 29 pre-existing combined AO/filter/composite payloads remain unchanged.
ASSAO SDR/HDR static slots are 492/466 (High), 494/457 (Medium), 384/351 (Low),
with at most 15 temporary registers. Only variants do not declare Scene.
Method/quality switches retain scene/depth/AO textures.

## AO GPU timings

RTX 4090, authored 3840×2160 concave corner/foreground/sky scene; 1920×1080
FP16 AO, D24-quantized world range 0..0.94, strength 100, radius 200. Sixteen
ABBA blocks use 64 warm and 64 timed draws each. Blur is measured separately
on each kernel's actual output. Times exclude resolve, native actor marking,
composition, HDR presentation and gameplay. Timestamp intervals can include
submission gaps.

| Method / quality | DXVK 2 SDR | DXVK 2 HDR | DXVK 2 blur | DXVK 3 SDR | DXVK 3 HDR | DXVK 3 blur |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| SSAO, all | 0.225 | 0.213 | 0.048 | 0.224 | 0.207 | 0.032 |
| HDAO High | 0.795 | 0.771 | 0.075 | 0.790 | 0.766 | 0.049 |
| HDAO Medium | 0.408 | 0.385 | 0.076 | 0.405 | 0.385 | 0.055 |
| HDAO Low | 0.408 | 0.382 | 0.076 | 0.405 | 0.382 | 0.052 |
| HBAO+ High | 0.835 | 0.814 | 0.080 | 0.839 | 0.820 | 0.058 |
| HBAO+ Medium | 0.484 | 0.466 | 0.080 | 0.488 | 0.467 | 0.054 |
| HBAO+ Low | 0.433 | 0.417 | 0.083 | 0.442 | 0.413 | 0.058 |
| GTAO High | 0.756 | 0.733 | 0.061 | 0.747 | 0.733 | 0.041 |
| GTAO Medium | 0.578 | 0.545 | 0.060 | 0.589 | 0.570 | 0.043 |
| GTAO Low | 0.393 | 0.365 | 0.060 | 0.404 | 0.386 | 0.041 |
| ASSAO High | 0.617 | 0.590 | 0.074 | 0.621 | 0.610 | 0.050 |
| ASSAO Medium | 0.274 | 0.258 | 0.074 | 0.279 | 0.265 | 0.050 |
| ASSAO Low | 0.178 | 0.160 | 0.067 | 0.178 | 0.160 | 0.046 |

HDR kernels are slightly cheaper on this scene. ASSAO Medium substantially
reduces High's cost while retaining detail/halo calculations. Blur's early-out
depends on the AO image. HDAO Medium/Low timing differences are measurement
variation of identical bytecode.

25 of 26 SDR/HDR pairs match AO bit for bit. HBAO+ Low on DXVK 3 differs at five
of 2,073,600 pixels by one FP16 step (0.00048828125), with no other AO difference
or nonfinite value. This is compilation rounding, not a kernel change. Small
exact tests are not a guarantee of bit identity in every scene. All three ASSAO
qualities match exactly in the 4K comparison.

On a separate 1728×1117 far-actor fixture at 2 yd, preparation+kernel+blur costs
for ordinary/actor HBAO+ are 0.148/0.199 ms (DXVK 2) and 0.148/0.190 ms (DXVK 3).
GTAO costs 0.115/0.159 and 0.115/0.149 ms respectively. Native marking is excluded;
these different-scene values must not be added to the 4K table.

Each backend passes 549 basic method/quality cases, 624 grazing cases, 3,456
ASSAO grid comparisons and 40 independent actor-reference cases. Grid coverage
includes FP16/FP32, even/odd dimensions, MinZ 0/0.05, MaxZ 1/0.94, radii
0.1/2/10 yd and MSAA0/2/4/8. Small comparisons retain exact alpha and fallback
RGB assertions. Settings/save/apply, 200 live switches, resource lifetimes and
failure/retry, fold and bilateral checks pass. Six new shaders compile with
Microsoft d3dcompiler_47 within SM3 limits. The built DLL passes SDR/HDR/Reset/
MSAA8/triple-buffer production proxy checks on both DXVKs.

These measurements do not reproduce gameplay vegetation, the complete depth
resolve or the whole HDR frame. Crystalsong Forest FPS acceptance still needs
paired gameplay captures with matching camera, settings and time of day.

## Local evidence and publication scope

Ignored local evidence directories are `.local/pipeline-audit-20261101/`,
`.local/fog-optimization-20261101/` and `.local/ao-assao-20261101/`. They contain
baseline snapshots, shader inventories, JSON results, raw timings and build logs.
The final pre-publication AO DLL is preserved as
`.local/ao-assao-20261101/build-artifact/frd9.dll`, SHA-256
`c9472701f5cfd75616fd5bb30b9966e13f10381b6da21029234bab2774f37a2c`.
It was not installed or included as a repository artifact. The earlier HDR and
fog DLL hashes are `c8d6844de3da1641a2681b8d930e2aa3381bcf4f50af072861480e9df07f00a2`
and `96f818ca4a624204bfe969faa7ea6175dd7011cf5f6027f41d8582b15ae8a272`.

These historical results apply to their tested combined snapshots. Publication
ports are validated separately on their own branches: #98 carries integration,
#118 carries independent AO/ASSAO, and #109 carries independent fog. #94 is the
presentation layer and does not contain the complete linear world pipeline.
Concurrent weather development is not part of this publication.

## Initial compiled-variant inventory

Slots are static D3D9 instruction budgets; Regs are temporary registers, Tex
counts static texture instructions, and Loops counts static rep/loop operations.
This is the initial audit snapshot, before the later fog/ASSAO additions above.
Extended world variants require capability checks; ordinary variants retain the
base budget. These numbers do not predict GPU milliseconds.

| Family | Entry | Profile | Slots | Regs | Tex | Loops |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| auto-bump | BumpOpaque | ps_3_0 | 280 | 10 | 5 | 2 |
| auto-bump | BumpLayer | ps_3_0 | 281 | 12 | 6 | 2 |
| auto-bump | BumpWall | ps_3_0 | 285 | 11 | 6 | 2 |
| auto-bump | BumpGrass | ps_3_0 | 282 | 12 | 6 | 2 |
| auto-bump | BumpWallOpaque | ps_3_0 | 281 | 12 | 5 | 2 |
| auto-bump | BumpComposite | ps_3_0 | 494 | 14 | 12 | 4 |
| auto-bump | BumpNative | ps_3_0 | 283 | 12 | 5 | 2 |
| auto-bump | BumpSharedOpaque | ps_3_0 | 287 | 11 | 5 | 2 |
| auto-bump | BumpSharedLayer | ps_3_0 | 288 | 12 | 6 | 2 |
| auto-bump | BumpSharedWall | ps_3_0 | 292 | 11 | 6 | 2 |
| auto-bump | BumpSharedGrass | ps_3_0 | 289 | 12 | 6 | 2 |
| auto-bump | BumpSharedWallOpaque | ps_3_0 | 288 | 12 | 5 | 2 |
| auto-bump | BumpSharedComposite | ps_3_0 | 501 | 15 | 12 | 4 |
| auto-bump | BumpSharedNative | ps_3_0 | 290 | 12 | 5 | 2 |
| auto-bump | BumpTerrainOpaque | ps_3_0 | 303 | 10 | 5 | 2 |
| auto-bump | BumpTerrainLayer | ps_3_0 | 304 | 12 | 6 | 2 |
| auto-bump | BumpSharedTerrainOpaque | ps_3_0 | 310 | 11 | 5 | 2 |
| auto-bump | BumpSharedTerrainLayer | ps_3_0 | 311 | 12 | 6 | 2 |
| auto-bump | AutoNormal | ps_3_0 | 316 | 4 | 9 | 0 |
| auto-bump | AutoNormalModel | ps_3_0 | 167 | 6 | 25 | 0 |
| auto-bump | ParallaxUV | ps_3_0 | 147 | 8 | 3 | 1 |
| auto-bump | ParallaxSharedUV | ps_3_0 | 172 | 9 | 5 | 1 |
| auto-bump | BumpNormalOpaque | ps_3_0 | 128 | 7 | 1 | 0 |
| auto-bump | BumpNormalLayer | ps_3_0 | 129 | 9 | 2 | 0 |
| auto-bump | BumpNormalGrass | ps_3_0 | 130 | 9 | 2 | 0 |
| auto-bump | BumpNormalTerrainOpaque | ps_3_0 | 151 | 7 | 1 | 0 |
| auto-bump | BumpNormalTerrainLayer | ps_3_0 | 152 | 7 | 2 | 0 |
| auto-bump | ModelNormalCapture | ps_3_0 | 13 | 1 | 0 | 0 |
| celestial-disc | CelestialDiscPS | ps_3_0 | 304 | 9 | 6 | 0 |
| celestial-disc | CelestialHaloVisibilityPS | ps_3_0 | 201 | 10 | 10 | 1 |
| celestial-disc | CelestialVeilPS | ps_3_0 | 186 | 6 | 5 | 0 |
| celestial-disc | CelestialTerrainVS | vs_3_0 | 6 | 2 | 0 | 0 |
| celestial-disc | CelestialTerrainPS | ps_3_0 | 1 | 0 | 0 | 0 |
| hdr | HdrUi-ps_2_0 | ps_2_0 | 24 | 3 | 0 | 0 |
| hdr | HdrUi-ps_3_0 | ps_3_0 | 23 | 3 | 0 | 0 |
| hdr | HdrWorld | ps_3_0 | 35 | 3 | 1 | 0 |
| hdr | HdrOverlay | ps_3_0 | 25 | 3 | 1 | 0 |
| hdr | HdrFixedUi | ps_3_0 | 31 | 3 | 1 | 0 |
| hdr | HdrFixedUi2 | ps_3_0 | 52 | 4 | 2 | 0 |
| hdr | HdrMeter | ps_3_0 | 28 | 4 | 1 | 0 |
| hdr | HdrReduce | ps_3_0 | 330 | 6 | 64 | 0 |
| hdr | HdrReduceLinear | ps_3_0 | 91 | 5 | 16 | 0 |
| hdr | HdrDecodeWorld | ps_3_0 | 60 | 5 | 3 | 0 |
| hdr | HdrCopy | ps_3_0 | 1 | 0 | 1 | 0 |
| hdr | HdrLegacyComposite | ps_3_0 | 65 | 5 | 4 | 0 |
| hdr | HdrAdapt | ps_3_0 | 29 | 2 | 2 | 0 |
| hdr | HdrBloom | ps_3_0 | 71 | 5 | 5 | 0 |
| hdr | HdrBloomHorizontal | ps_3_0 | 65 | 4 | 5 | 0 |
| hdr | HdrBloomVertical | ps_3_0 | 20 | 4 | 4 | 0 |
| hdr | HdrWorldLinear | ps_3_0 | 64 | 4 | 3 | 0 |
| hdr | HdrWorldLinearOnly | ps_3_0 | 50 | 4 | 3 | 0 |
| hdr | HdrLegacyEncode | ps_3_0 | 15 | 3 | 1 | 0 |
| hdr | HdrCalibration | ps_3_0 | 1 | 0 | 0 | 0 |
| hdr | HdrLayer | ps_3_0 | 27 | 5 | 1 | 0 |
| local-light | LocalShadowVS | vs_3_0 | 9 | 2 | 0 | 0 |
| local-light | LocalShadowPS | ps_3_0 | 54 | 3 | 1 | 0 |
| local-light | LocalLighting | ps_3_0 | 466 | 13 | 9 | 0 |
| effects | AO | ps_3_0 | 267 | 10 | 8 | 1 |
| effects | AOContactBloom | ps_3_0 | 289 | 9 | 12 | 1 |
| effects | ContactBloom | ps_3_0 | 29 | 4 | 5 | 0 |
| effects | HDAOContactBloom | ps_3_0 | 254 | 14 | 9 | 2 |
| effects | HBAOPlusContactBloom | ps_3_0 | 314 | 11 | 12 | 2 |
| effects | GTAOContactBloom | ps_3_0 | 491 | 18 | 13 | 2 |
| effects | HDAOContactBloomMedium | ps_3_0 | 259 | 14 | 9 | 2 |
| effects | HBAOPlusContactBloomMedium | ps_3_0 | 314 | 11 | 12 | 2 |
| effects | GTAOContactBloomMedium | ps_3_0 | 485 | 19 | 13 | 2 |
| effects | HDAOContactBloomLow | ps_3_0 | 259 | 14 | 9 | 2 |
| effects | HBAOPlusContactBloomLow | ps_3_0 | 314 | 11 | 12 | 2 |
| effects | GTAOContactBloomLow | ps_3_0 | 486 | 19 | 13 | 2 |
| effects | AOBlur | ps_3_0 | 350 | 16 | 20 | 2 |
| effects | Composite | ps_3_0 | 328 | 15 | 13 | 1 |
| effects | AOActorPrepare | ps_3_0 | 278 | 12 | 8 | 1 |
| effects | HBAOActorContactBloom | ps_3_0 | 404 | 19 | 12 | 3 |
| effects | GTAOActorContactBloom | ps_3_0 | 496 | 24 | 12 | 4 |
| effects | HBAOActorOnly | ps_3_0 | 378 | 19 | 7 | 3 |
| effects | GTAOActorOnly | ps_3_0 | 471 | 25 | 7 | 4 |
| effects | RainMaskMRT | ps_2_0 | 5 | 1 | 1 | 0 |
| effects | RainScrub | ps_2_0 | 9 | 2 | 2 | 0 |
| effects | AOOnly | ps_3_0 | 263 | 9 | 7 | 1 |
| effects | HDAOOnly | ps_3_0 | 228 | 14 | 4 | 2 |
| effects | HDAOOnlyMedium | ps_3_0 | 232 | 15 | 4 | 2 |
| effects | HDAOOnlyLow | ps_3_0 | 232 | 15 | 4 | 2 |
| effects | HBAOPlusOnly | ps_3_0 | 288 | 12 | 7 | 2 |
| effects | HBAOPlusOnlyMedium | ps_3_0 | 288 | 12 | 7 | 2 |
| effects | HBAOPlusOnlyLow | ps_3_0 | 288 | 12 | 7 | 2 |
| effects | GTAOOnly | ps_3_0 | 464 | 19 | 8 | 2 |
| effects | GTAOOnlyMedium | ps_3_0 | 459 | 20 | 8 | 2 |
| effects | GTAOOnlyLow | ps_3_0 | 459 | 20 | 8 | 2 |
| static-shadow | StaticCasterVS | vs_3_0 | 15 | 3 | 0 | 0 |
| static-shadow | StaticCasterInstancedVS | vs_3_0 | 15 | 3 | 0 | 0 |
| static-shadow | StaticCasterPS | ps_3_0 | 10 | 1 | 1 | 0 |
| static-shadow | StaticCasterFastPS | ps_3_0 | 5 | 1 | 1 | 0 |
| static-shadow | StaticCasterOpaqueFastPS | ps_3_0 | 1 | 0 | 0 | 0 |
| static-shadow | StaticCasterOpaquePS | ps_3_0 | 6 | 1 | 0 | 0 |
| surface | RainQuality | ps_3_0 | 190 | 6 | 1 | 0 |
| surface | SnowQuality | ps_3_0 | 240 | 7 | 1 | 0 |
| surface | RainPerformance | ps_3_0 | 141 | 5 | 1 | 0 |
| surface | SnowPerformance | ps_3_0 | 175 | 5 | 1 | 0 |
| surface | RainLens | ps_3_0 | 139 | 4 | 2 | 0 |
| surface | SnowLens | ps_3_0 | 127 | 5 | 1 | 0 |
| surface | WeatherLensParticle | ps_3_0 | 192 | 5 | 1 | 0 |
| surface | FrostVignette | ps_3_0 | 210 | 6 | 1 | 0 |
| surface | SandLens | ps_3_0 | 230 | 7 | 0 | 0 |
| world | WorldNormals | ps_3_0 | 346 | 22 | 8 | 3 |
| world | WorldNormalsNoDistance | ps_3_0 | 324 | 22 | 7 | 3 |
| world | WorldLighting | ps_3_0 | 411 | 16 | 7 | 3 |
| world | ActorShadow | ps_3_0 | 332 | 10 | 9 | 5 |
| world | ActorTiles | ps_3_0 | 90 | 4 | 16 | 0 |
| world | WorldGI | ps_3_0 | 233 | 19 | 11 | 1 |
| world | WorldFog | ps_3_0 | 468 | 22 | 11 | 1 |
| world | WorldFogAmbient | ps_3_0 | 340 | 14 | 7 | 1 |
| world | WorldFogActor | ps_3_0 | 596 | 23 | 12 | 3 |
| world | FogBlur | ps_3_0 | 233 | 8 | 15 | 0 |
| world | FogClouds | ps_3_0 | 469 | 22 | 12 | 1 |
| world | FogCloudsActor | ps_3_0 | 598 | 25 | 13 | 3 |
| world | FogTemporal | ps_3_0 | 252 | 13 | 16 | 0 |
| world | LocalDirect | ps_3_0 | 293 | 8 | 5 | 0 |
| world | LocalDirect1 | ps_3_0 | 195 | 8 | 5 | 0 |
| world | LocalDirect16 | ps_3_0 | 405 | 8 | 5 | 0 |
| world | RemovalSmooth | ps_3_0 | 222 | 18 | 18 | 1 |
| world | TemporalLight | ps_3_0 | 210 | 13 | 12 | 0 |
| world | LocalFog | ps_3_0 | 397 | 6 | 2 | 0 |
| world | LocalFogCombine | ps_3_0 | 22 | 3 | 1 | 0 |
| world | SourceVisibilityPS | ps_3_0 | 285 | 7 | 25 | 0 |
| world | WorldComposite | ps_3_0 | 380 | 19 | 19 | 1 |
| world | WorldUnlitComposite | ps_3_0 | 247 | 13 | 10 | 1 |
| world | MaterialComposite | ps_3_0 | 261 | 10 | 12 | 0 |
| world | WorldCompositeHdr | ps_3_0 | 438 | 22 | 19 | 1 |
| world | WorldUnlitCompositeHdr | ps_3_0 | 302 | 13 | 10 | 1 |
| world | MaterialCompositeHdr | ps_3_0 | 273 | 10 | 12 | 0 |
| world | WorldMaterialCompositeHdr | ps_3_0 | 694 | 22 | 30 | 1 |
| world | MaterialAudit | ps_3_0 | 240 | 8 | 6 | 0 |
| world | ShadowVS | vs_3_0 | 9 | 2 | 0 | 0 |
| world | ShadowCacheVS | vs_3_0 | 10 | 2 | 0 | 0 |
| world | ShadowPS | ps_3_0 | 5 | 1 | 1 | 0 |
| world | ShadowReplayPS | ps_3_0 | 44 | 3 | 1 | 0 |
| world | ShadowUnion | ps_3_0 | 34 | 2 | 2 | 0 |
| world | ActorTilesWide | ps_3_0 | 330 | 4 | 64 | 0 |
| world | WorldFogActorTiled | ps_3_0 | 657 | 24 | 13 | 4 |
| world | FogCloudsActorTiled | ps_3_0 | 663 | 26 | 14 | 4 |
