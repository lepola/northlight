# Complete integration audit — 2026-11-04

This publication includes the current local renderer work in the fork's `main`
and `review/combined-integration-20261021` branches. The latter is the head of
[upstream PR #98](https://github.com/lepola/northlight/pull/98).
The upstream repository has four branches at the time of inspection; none is a
separate integration branch. Updating PR #98 does not merge upstream `main`.

## Included work

- Camera-independent native lights, including animated bones and attachments;
  shared distance/intensity limits and native-source deduplication.
- Lamp fixture shadows and stale world-cache regression coverage; the two
  shadow-audit repairs preserve their original assertions.
- HDR world recovery after an early UI boundary, transparent smoke over the
  sky, and lighting/fog on translucent banners and projected shadows.
- Generic sky material classification and cloud/celestial composition, including
  fixed-function texture-factor RGB and multi-stage alpha preservation.
- Bounded model-normal distance proofs, current-pose envelope reuse, cached
  auxiliary-target absence and stable material-cache admission.
- The offline animated lightning stand and the recovered HDR state-block pool
  regression test from an older worktree.
- Static directional shadow-cache transitions from PR #113, adapted to the
  current shared PCF/PCSS visibility and actor/tiled fog paths. History contains
  static geometry only; current terrain and moving casters use their existing
  draw into a separate target during the one-second transition.

The audit inspected 71 registered worktrees, 180 local branches and zero stashes.
Historical topic adaptations, exact patch equivalents, superseded iterations and
the explicitly rejected fog prototype were distinguished from active changes.
The worktree audit recovered a missing HDR pool test. A separate source-level
review of 11 open topic PRs found one further gap: the static directional shadow
cache crossfade from PR #113, now adapted to the current renderer. The sky and
portal publication histories are merged without
rewriting them; conflict resolution retains the later lit-receiver fixes.
Private client assets and generated runtime caches remain outside Git.

| Related upstream PRs | Coverage in the combined integration |
| --- | --- |
| #92, #94, #96, #97, #111 | Existing integration ancestry plus later shadow, HDR, diagnostic and material fixes |
| #109, #110, #112, #114, #118 | Current fog/native sun, actor AO, emitter and GTAO implementations |
| #116, #117, #120 | Test tooling, bounded scene storage and Unicode paths |
| #76, #86 | Translucent grass/tree handling and DXVK present-wait compatibility |
| #113 | Continuous actor direction and nested PCF already present; static cache transitions restored in this publication |

The transition is optional on devices with fewer than 749 pixel-shader
instruction slots; the existing 512-slot baseline remains supported. Resources
are allocated only when a transition is possible, at 8 MiB per cascade slot
(32 MiB maximum for history/current-dynamic maps). Surface filtering shares the
existing half-resolution FP16 visibility target; when PCSS has not already
created it, that target costs a further 15.82 MiB at a 3840×2160 screen size.
Pressure, allocation failure, content changes, teleportation
and reset discard history. Inactive transitions add no GPU passes or sampler
writes, and unsupported/pressure paths retain the original cascade cadence.

## Validation

- Production Windows x86 DLL built with `python scripts/build_renderer.py` and
  the pinned Zig compiler; shader/catalog freshness gates pass. The Windows
  package builds and `tests/verify_packages.py` passes.
- Full device-mirror suite passes in WSL: optimized, ASan/UBSan, TSan and the
  expected-failure counterfactuals, including target mutation/re-entry controls.
- Nine focused scripts pass without skips: model range, displacement-state
  reuse, normal memory, diagnostics, draw gates, emission sources, HDR boundary,
  native celestial suppression and sky material catalog.
- Native sky policy/phase checks and all six celestial GPU replay families pass;
  layer capture covers 262 cases, including 131 MSAA cases. The new fixed-function
  regression fails before the correction and passes afterward.
- HDR pool/texture transforms pass on native D3D9, DXVK 2 and DXVK 3: 24 exact
  pooled/fresh comparisons and 144 transform cases per backend; pooled state
  blocks fall from 24 allocations to one while preserving constants.
- Full eight-case model-normal suites pass on DXVK 2 and DXVK 3, including
  camera motion, batching, residency, alpha, MSAA, fault and state restoration.
- Focused model-range GPU comparisons pass on all three backends: five actual
  client shader variants and 13 scenarios each. Disjoint screen areas and an
  anchor-only reference prove that the pixel comparison is not hidden by near
  geometry. Near geometry adds pixels, far geometry matches the anchor, and
  mixed bones/stale pose epochs refuse culling.
- Material reuse/working-set checks pass on all three backends: 180 exact map
  comparisons, zero repeated generation for the 20-map set, and zero transitions
  over 96 frames of the oversized 32-map set with 24 resident maps.
- The final production DLL passes ten HDR/SDR/MSAA integration runs across
  DXVK 2 and DXVK 3, including reset and early-UI world recovery.
- Transition controller, reprojection and resource checks pass optimized and
  sanitizer runs. Fault coverage includes 28 allocation/partial-COM failures,
  50 capture setup/draw/cleanup failures and 1,800 frames proving unchanged
  fallback cadence. Devices below the actual 749-slot requirement allocate
  nothing; the boundary-capability device succeeds.
- The two shadow audit scripts pass with their guard checks intact. The static
  plan suite passes all 26 checks, including optimized, ASan/UBSan and TSan
  worker proofs; its historical Apple-Clang hash check is platform-skipped.
  All 42 baseline world shaders retain their bytes and pass their existing
  independent manifest/cap and negative-control checks.
- Transition GPU checks pass on native D3D9, DXVK 2 and DXVK 3: 210 production
  cases and 161,280 independent PCF/PCSS comparisons per backend. Exact uploaded
  query inputs isolate the filter from native SM3 projection rounding; the
  output tolerance remains `3e-5`. Full production frames are checked against
  an enumerated one-UV-ULP input envelope where necessary, and current visibility
  endpoints match the baseline exactly in FP16. Sixteen negative controls reject
  the incorrect lifted-border coverage test. There are also 3,072 sky/water
  checks, 36,864 fog/cloud checks, 7,340,032 exact static-crop comparisons and
  2,097,152 current-dynamic draw/union comparisons per backend.

Measured added GPU time for one active source's transition resolve is
0.102/0.374 ms on native D3D9, 0.141/0.519 ms on DXVK 2 and 0.163/0.602 ms on
DXVK 3 at 1920×1080 / 3840×2160 screen sizes. These use the actual half-resolution
FP16 target and exclude geometry refresh, history creation and other frame work.
They measure the active transition, not a permanent frame cost or whole-game FPS.

The verified DLL has source identity
`d1e4423db8aee534ac114af2935d1c4de910422aa654a320361923c7852eb093`
and SHA-256
`7e734f8ee4c0153dc759052025147a06b5cf47ccce78d3dc40b603a7214a4301`.

The native D3D9 full model-normal run encountered `D3DERR_DEVICELOST` before
its batch oracle. Its corresponding DXVK runs pass; no assertion was removed
or weakened to accommodate the native device loss. Offline checks do not claim
a new gameplay/FPS measurement.

Local detailed evidence is under `out/publication-20261104/` (coverage inventory,
build, GPU and package logs) and `.local/out/publication-review/` (sky baseline
failure and corrected runs). `Wow.exe` was not changed; its SHA-256 remains
`99155d4d6ed4937a5ab1f5972d11e8605b986561b9f08686ab66af2c582194ec`.
