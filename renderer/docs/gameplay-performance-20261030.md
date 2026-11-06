# Gameplay performance baseline, 2026-10-30

## Reproducible setup

Stationary level-80 character in Dalaran, Sunreaver's Sanctuary; continuous camera rotation at `MoveViewLeftStart(0.15)`. DXVK 3.1.1, 3840 x 2160, 8x MSAA, the existing Quality settings, High GTAO, experimental HDR, and a configured point-shadow limit of 128. No preset or effect-quality changes. The game was closed before standalone GPU work.

Renderer `cac1c2c`, source identity `55170128fab345db98dbabfa97ddd58615b3191925a68a7f21e0ead47a3a0f31`, DLL SHA256 `396786c7320cd0e9e2b9b9db2e60e6ab0a08cfa4db2c5a1d0e7d9fbe3c489862`.

## Effects on/off baseline

Short Ctrl+Shift+F10 presses previously escaped frame polling. The window hook now retains initial key-down edges until a frame consumes them, excludes auto-repeat, clears pending edges on lost focus, and preserves polling when the overlay is disabled or unavailable. Three affected portable tests pass. The actual settings overlay passes all 18 SDR/scRGB panel modes. Both ON and OFF transitions were confirmed in the client log.

Four fixed 60-second windows in OFF/ON/ON/OFF order; discard five seconds at the start of each window. Admit only complete, foreground `FRAME interval` records wholly inside a window, with all component switches enabled. Average frame time is weighted by frame count; FPS is total frames divided by total recorded interval time.

| Window | Accepted frames | Accepted seconds | Mean frame ms | FPS |
| --- | ---: | ---: | ---: | ---: |
| r1-a1-off | 5280 | 43.998 | 8.333 | 120.00 |
| r1-b1-on | 3120 | 36.668 | 11.753 | 85.09 |
| r1-b2-on | 2400 | 28.172 | 11.738 | 85.19 |
| r1-a2-off | 5040 | 42.001 | 8.334 | 120.00 |

Pooled ON: 85.13 FPS / 11.746 ms; OFF: 120.00 FPS / 8.333 ms. The observed difference is 3.413 ms.

This is not a fully uncapped comparison: `maxFPS=0` was confirmed in the client, but the renderer recorded `PresentationInterval=1`, and OFF plateaued at the 120 Hz display rate. The character did not move, but nearby players and NPCs varied. No post-optimization FPS improvement is claimed by this baseline. The original `maxFPS=120` was restored.

## Twenty measured GPU stages

41 samples from the admitted ON windows. Values are elapsed query intervals between marks, which can include producer/submission gaps; they are not isolated GPU busy times. A percentile of these sampled intervals is not a frame-time percentile.

| Rank | Stage | Mean ms | Median ms | Sample p95 ms |
| ---: | --- | ---: | ---: | ---: |
| 1 | AO | 1.183 | 1.185 | 1.237 |
| 2 | RemovalSmooth | 0.589 | 0.550 | 0.705 |
| 3 | WorldComposite | 0.453 | 0.450 | 0.458 |
| 4 | Fog | 0.441 | 0.390 | 0.717 |
| 5 | LocalLights | 0.391 | 0.314 | 1.099 |
| 6 | AOBlur | 0.376 | 0.240 | 1.067 |
| 7 | PointShadow | 0.341 | 0.134 | 1.165 |
| 8 | MaterialDetail | 0.206 | 0.208 | 0.214 |
| 9 | WorldLighting | 0.162 | 0.142 | 0.146 |
| 10 | GI | 0.160 | 0.121 | 0.130 |
| 11 | WorldNormals | 0.100 | 0.099 | 0.106 |
| 12 | FogClouds | 0.100 | 0.095 | 0.119 |
| 13 | TemporalLight | 0.088 | 0.062 | 0.071 |
| 14 | FogMarch | 0.071 | 0.072 | 0.083 |
| 15 | SunNearReplays | 0.062 | 0.047 | 0.112 |
| 16 | FogBlur | 0.060 | 0.079 | 0.081 |
| 17 | PointLight | 0.048 | 0.040 | 0.103 |
| 18 | ActorTiles | 0.046 | 0.006 | 0.014 |
| 19 | PreCelestial | 0.044 | 0.083 | 0.088 |
| 20 | ActorShadow | 0.043 | 0.041 | 0.062 |

CPU instrumentation attributes a mean 1.746 ms to capture, 1.249 ms to effect submission, 0.316 ms to cleanup, and 0.195 ms to draw gating (19 admitted samples). Within effect submission, point shadows average 0.529 ms, selection 0.156 ms, and upload 0.145 ms. These CPU and GPU figures overlap and must not be added as independent frame costs.

## Accepted first optimization

Move High GTAO's existing zero-footprint rejection before the paired depth samples and horizon calculations. The rejected samples previously had exactly zero weight. Sample count, useful sample positions, integration, bloom, radius, and strength are unchanged. Medium and Low retain their previous source and bytecode: applying the same branch to them caused regressions in the exploratory matrix.

The exploratory three-quality matrix passed 108 exact FP16 output comparisons on DXVK 3. The final High-only oracle compares genuine baseline/current Microsoft bytecode at 256 x 192, odd 137 x 79, 1920 x 1080, and 3840 x 2160, with nine quantized-depth scenes per size. It also verifies that replacing AO with neutral alpha fails the pixel comparison. The dedicated `validate_gtao_footprint_gpu.py` command records independent backend hashes, compiler provenance, input hashes, pixels, and ABBA query timings.

Final High-only validation: DXVK 3 and DXVK 2 each pass 36 exact-output cases and reject the disabled-AO control. At 3840 x 2160, the geometric mean of per-scene new/old elapsed-time ratios is 0.9323 on DXVK 3 (6.77% less time) and 1.0063 on DXVK 2 (no reliable benefit). At 1920 x 1080 the reductions are 6.69% and 3.44%, respectively. These are isolated-pass measurements, not whole-frame FPS gains. Native D3D9, DXVK 3, and DXVK 2 each pass 99 AO-method cases and 48 grazing-angle cases with real concavity retained.

The stage ranking is a work queue, not twenty completed optimizations. Capture, point-shadow submission, removal smoothing, full-screen compositing, and fog follow the measured cost order.

Private raw evidence: `.local/gameplay-rotation-20261030/paired-renderer.log`, `paired-events.json`, `paired-identity.json`, `paired-report.json`, and `gtao-footprint/`. No credentials are part of these reports.

## General regression validation

The initial portable CPU run completed with 230 PASS, one stale draw-gate assertion, and 196 prerequisite/manual SKIPs. After restoring the existing Linux StormLib and client paths, the 56 previously covered prerequisite-dependent tests plus the repaired draw-gate test all pass (57 PASS, zero failures). Combined coverage is 287 validated passing tests, zero unresolved failures, and 140 explicit prerequisite/manual SKIPs. The draw-gate test still compiles and exercises the real hook paths with randomized faults and sanitizers; its frame-boundary master-toggle contract now follows the retained event instead of requiring the old polling expression. The grazing GPU runner also now creates its executable beside the requested report, fixing custom-output runs on a clean checkout.

Production build: source identity `fa0ee0122efc652b342beefcfd3332c2ce6eaaaa8c42b8291351d8f13ff06c07`, DLL SHA256 `199f31ace96bbb206764e37628cc8a50c52f1162203250391b1b4f0d6ee3e234`. Four existing compiler warnings remain; no build errors. This build includes the earlier scene-memory/Reset recovery and HDR fixes as ancestors; those matrices are documented separately in `render-recovery-validation-20261030.md` and were not relabeled as a new full-DLL GPU run.

## Second optimization: prepare point-shadow clip planes

Cube projection matrices are constant while one light's update visits static, terrain, live, and replay AABBs. Previously every face/AABB pair revalidated all 16 matrix floats and rebuilt six double plane coefficients plus their floating-point cancellation bounds. `FaceClip` prepares these once per cube update; content signatures use the same path. Sphere broad-phase checks, face order, maximum-coordinate extents, per-component GPU-overflow protection, cancellation margins, and invalid-input fail-open behavior are retained.

The candidate's masks equal the scalar reference in 174,335 cases, including actual cube projections, unknown bounds, NaN/infinity, arbitrary extreme finite transforms, and nextafter samples around near/range boundaries. Windows x86 O2 ABBA timing over 163,840 AABBs per block (six retained samples per path) gives median 20.320 ms for the reference and 11.411 ms prepared, a 43.84% reduction for this clipping subphase. Linux x64 gives 19.382 versus 10.532 ms. The prepared object occupies 3,120 bytes of temporary stack, with no allocation or retained per-source storage. The microbenchmark excludes the sphere broad phase, draw submission, and all GPU work; it is not a point-shadow-pass or whole-frame FPS claim.

The fresh full CPU suite on the combined build passes 288 tests with zero failures and 140 explicit prerequisite/manual SKIPs, including the new release/sanitizer clipping oracle. Combined production build source identity: `b7f71d3d8153574897b26bfe675dfbdef3285cfb6b0b9f63d6142dc89b100ebf`; DLL SHA256: `84635c1dffb31ffe359bec5d6326bd17dc6141e3a66a334e972a63bd45d96620`.

The exact combined DLL also passes all 28 actual-DLL HDR/DXVK modes (6,890 checks, zero failures), including MSAA 8, triple buffering, 4K, and Reset. The verified Windows installer is `releases/Northlight-0.3.216-test.20261030.3-Windows.zip` (19,619,258 bytes), SHA256 `11360f6fb1d64da69a75681a0891f5bf5b2ac26b01af59011e1ca59fc2f619fb`; its payload DLL equals the tested build. These HDR checks do not replace the separate earlier scene-memory matrix.
