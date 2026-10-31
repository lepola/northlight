# Material performance investigation — 22 October 2026

## Implemented shared scene lifecycle refactor — 23 October 2026

The previously proposed refactor is now implemented: one bounded decoded-source bank, reusable placement/terrain-chunk acceleration, reusable shadow preparation/page metadata, and shared capture source identity that survives CPU owner replacement. Current pose, complete geometry/alpha coverage, MSAA, ranges and aggregate cache limits are preserved. Water/SSR remains withdrawn.

Three alternating CPU comparison rounds against `ac966ce` include actual world regions and the full 5,600-probe, 64-ray, three-bounce GI bake. Preparation plus bake takes 5.98% less time in the warm aggregate, 3.13% less during orbit/return and 31.61% less for the same region. Cold work takes 10.59% longer; warm bake alone takes 13.84% longer. These worker results are not a game-FPS measurement. Capture independently halves cold source reads in the frozen three-consumer fixture and removes retained return-orbit rereads.

All 21 whole-pipeline executions match canonical scene, plan, page, ray and full bake outputs; 140 material GPU cases and 96 terrain shadow layout comparisons pass with MSAA. Optimized/debug lifecycle, allocation-failure, admission, concurrency and regression checks pass. A separate release DLL is built; installed files/settings and the game remain untouched.

Full implementation, limits, costs and validation: [shared scene refactor](shared-scene-refactor.md).

## Fresh 01:23 run: pipeline invalidation and water/SSR withdrawal — 23 October

The complete run tests 31d20e1 (log SHA-256 ccfdb6125a498d7fd877235fb4ca6c12a9eeddc2fb560eaf26ec88564d25cb88). Previous fixes are active:4918 terrain snapshots, no Off count-pressure evictions,2.173 MiB offset and 9.145 MiB source-index duplication avoided. Remaining byte-pressure replacement still causes12.337 ms terrain capture at Off 3048; median Off terrain is0.303 ms. Materials have zero algorithm/capture work in all 44 Off samples. Frame 3209 instead spends230.259 ms with an empty replay scene queue; that episode is not explained by AO/fog arithmetic.

Water/SSR PR #93 is closed and its beauty/refraction/glint/SSR, material detector, forced redraw, copy/depth-hierarchy/target/code artifacts and eleven settings/menu controls are removed. Native liquid masks remain for AO/fog/celestial correctness. Independent precipitation and UI/HDR repairs are preserved. The old detector forced19.18 mask redraws per sampled Off frame on average despite disabled enhanced water; that unconditional path is removed. Actual new rate/FPS still require the user's run.

GI settings invalidation now follows exact worker dependencies, preserving pending changes and true light/GI updates without cancelling work after unrelated material/HDR/display edits. Exact single-bone prepared bounds also improve 8.1–9.7% in component tests, saving only 0.0135–0.0182 ms/8192 vertices; GPU capture and source-lifetime churn remain larger targets. Quality, MSAA, ranges, budgets and geometry coverage remain.

Three workers investigated five topics each. The detailed English report includes a staged comprehensive migration to shared immutable placement/chunk data, stable source identities, reusable BLAS/GPU pages, exact region membership/TLAS and proven GI transport reuse. That larger migration is proposed, not claimed as shipped. Current validation includes exact actor oracles/regressions, exhaustive88-key GI dependency tests,13 Lua menu scenarios,140 M2/terrain/WMO material capture comparisons and 2304 retained precipitation GPU cases. One Unix-oriented fake mask-state harness is unsupported on Windows and explicitly disclosed. The game was not launched; installed DLL/settings/assets remain untouched. Older sections below describe preceding builds.


## Fresh 00:50 run: exact terrain retention and actor evaluation — 23 October

The complete run tests 1404591 (log SHA-256 abedd725589925ae40468c3dc7b8d55db8ac8d7231bee4dc28c787160791687d). Previous source-index sharing is active and avoids 6.445 MiB; inactive native material GPU profiling is correctly suppressed in all 15 sampled Off frames. Only 13 master-On samples actually submit materials, and AO/water-mask state changes later in the run, so this is not a controlled before/after FPS test.

The foreground rotation bottleneck has two cache causes. At context frames 9652/9779 terrain preparation takes 7.918/11.266 ms with 373/562 entry-count evictions despite byte headroom. With materials Off, frame 11684 takes 11.520 ms with 525 misses and 531 byte-pressure evictions. The fixed entry ceiling now increases from 4096 to 8192 inside the same fully charged 32 MiB total limit. Source revalidation byte offsets use the existing exact 16/32-bit container with wide fallback; source/layout keys, lifetime/revision checks, geometry and precision remain intact.

Frozen-baseline CPU fixtures separately prove both constraints: count-bound 5632 warm misses and byte-bound 1296 misses become zero, eliminating repeated reads, eviction and replacement preparation with identical geometry. Five-repeat whole-fixture medians improve 44.05% and 17.46%; these are component results, not game FPS. Slot growth alone still thrashes in the byte-bound fixture.

Two additional stock skinning templates gain exact CPU evaluators shared by materials and actor/GI work. Original envelope eligibility, pose/read budgets, Strict fallback, operation order, rounding, failure/alias behavior and current transforms are preserved. New per-route vertex counters and actual renderer frame IDs make the next client run attributable. Normal/height caches are already warm; recorded GPU displacement height draws are zero in this run.

All production shader sources, bytecode, sample counts, ranges, MSAA and render coverage remain unchanged. A byte-exact 630-image FogBlur experiment is excluded because repeated GPU timing regressed. Three workers investigated five topics each. Exact actor tests, eight terrain suites, six AO/post regressions, 140 M2/terrain/WMO capture comparisons and an x86 release with all 17 exports pass. The English detailed report is renderer/docs/rotation-disabled-performance.md / NORTHLIGHT_ROTATION_DISABLED_PERFORMANCE_REPORT_EN.md; the separate DLL package includes raw evidence. The game was not launched, and installed DLL/settings/assets were preserved. Actual new FPS awaits the user's comparable rotation test. Older sections below describe preceding builds.


## Fresh 00:23 run: terrain snapshot eviction is the foreground rotation bottleneck — 23 October

The complete new log tests 11f79a8, SHA-256 c7d91da6b7154546a3f70d6f85ded70733f041ec3f5cd673526c407a14a50689. It contains both material states, AO/cloud switches and late effects toggles; sampled Off windows are analyzed separately. The preceding inactive-shader fix is confirmed: 37 queued originals, zero Off vertex compilation and zero cold vertex CPU. The new command/capture timers instead identify terrain snapshot preparation at 11.159/9.947/12.032 ms in Off frames 3556/4064/6858, with hundreds of misses and evictions at the 32 MiB CPU cache limit. Native material-reference draw CPU totals are below 1 ms in those frames; GameScene timestamps overlap submission gaps and do not establish pure GPU arithmetic cost.

Cached draws now share exactly equal immutable source-index blocks through a bounded weak memo. Full keys, source-write/lifetime verification, fallback byte checks, output geometry and the existing 32 MiB/4096 limits remain. Last-reference eviction recharges prospective shared blocks correctly, and allocation failures cannot leak charge. A frozen 4000-draw rotation fixture changes 8000 revisits from all misses/evictions to all hits, eliminating 80.16 MB rereads and 46.40 MB of repeated 20-byte vertex preparation, with the same geometry fingerprint. Its component timing is not game FPS; new sourceIndexBlocks/sourceIndexAvoidedMiB counters will verify sharing in the real client.

Inactive materials no longer issue redundant per-domain native GPU timestamp pairs; native CPU and global scene profiling remain, with nativeGpuEnabled explicitly labeling suppression. FogMarch is now reported even when clouds are Off, and FogBlur is separated from WorldComposite. The bounded nonblocking profiler accommodates 42 marks. All production shader sources/binaries are unchanged. A FogBlur proposal and a pose-fallback proposal were both excluded after timing regressions despite exact output tests.

Three workers investigated five topics each. The release builds with the same 17 exports. Validation passes 140 M2/terrain/WMO capture checks, 594 corrected-production-LINEAR FogTemporal comparisons, eight terrain CPU suites in O2/debug, allocation/lifetime/last-reference tests and profiler/query regressions. The detailed English report is renderer/docs/rotation-disabled-performance.md / NORTHLIGHT_ROTATION_DISABLED_PERFORMANCE_REPORT_EN.md. The game was not launched and installed DLL/settings/assets were preserved. Actual FPS of the new DLL is unmeasured; older sections below describe preceding runs.

## Fresh 23:49 material-Off run: eliminate eager shader work and duplicate terrain indices — 23 October

The complete new run tests bc382d9; log SHA-256 f535b934ad04a85fea8aa34e64cc1e01953d7564d4abecea952f0e5bf2057993. All 34 sampled material configurations are globally Off. Capture/pose/source/generation/replay/application work is zero, but the new cold profiler catches 37 eager recognized-vertex registrations totaling 45.1834 ms including startup, with one 12-source batch taking 16.0119 ms. The variants were created even while unused. They are now queued as bounded immutable metadata while the master/domain is inactive and built synchronously only for an enabled consumer, preserving the first enabled image, lifetimes and the 4,096-entry cap. Overflow recovery and failures are explicitly tested.

Three agents again investigated five distinct topics each. The preceding 20-byte GPU vertex format is active; current index regeneration still duplicates all-live directional lists. Exact CPU aliasing now reuses the existing point list when fixed membership proves equivalence; mixed/unknown cases retain the original filter. The pressure test preserves all 77,435,904 GPU indices and improves prepare/write from 223.840 to 25.345 ms over 100 generations. FogTemporal additionally skips its unnecessary neighborhood clamp when history equals current while keeping the original lerp; neutral fixtures save about 0.006–0.007 ms, with a small mixed-case branch cost. These component measurements are not game FPS predictions.

All 140 GPU capture checks (100 M2, 20 terrain, 20 WMO) pass with actual Off registration followed by the first enabled draw. Another 648 FogTemporal comparisons are exact, including production FP16, HDR, MSAA, signed-zero and nonfinite cases. Only FogTemporal bytecode changes; its integrated hash matches the independently tested entry, and the other 23 world entries remain identical. Terrain/actor/handoff and material failure/cap/Off→On CPU regressions pass. Additional actor decode/pose/hash/BVH and sampled replay-command timings preserve all rendering and synchronization policies.

The later rotation samples still show 16–18 ms in GameScene and 15–16 ms before the replay query task starts, with no cold registration. New command attribution will identify the earlier queued work; isolated producer-idle stalls are a separate class. The detailed current English report is renderer/docs/rotation-disabled-performance.md / NORTHLIGHT_ROTATION_DISABLED_PERFORMANCE_REPORT_EN.md. The agent did not launch the game, replace its installed DLL or change settings. Earlier sections below describe earlier runs.

## Fresh 23:25 run: rotation stalls remain with global materials Off — 22 October

The latest complete log tests `c5ef83c`, SHA-256 `14c972be1289ddf8047a4f18cbde375afbc7504a14584c3375ac20762eeefcda`. Global material `enabled=0` from sampled frame 3302 overrides retained modes 3/3/3/0. All expensive material capture, pose, source-read, generation, replay and final-application stages are zero. Reported nativeDraw is the original game rendering. The user is correct that the rotation problem persists outside materials: Off windows fall to 48.20–59.70 FPS and recover to 102.01 FPS after settling. This is not a controlled old/new DLL FPS measurement.

Three agents investigated five distinct topics each in GI/world streaming, materials, and AO/postprocessing. The main new optimization is lossless 20-byte live terrain GPU vertices instead of 32-byte world vertices with unused normals. Same float positions/UVs, topology, uint32 indices, active coverage and 16/8 MiB admission limits. A pressured 19-rotation fixture reduces uploads 94,841,600 -> 23,316,000 bytes and rollovers 5 -> 0; 96 production shadow/MSAA comparisons are bit exact. This targets measured remaining GPU arena churn after the preceding CPU index compaction.

A separate enabled-material optimization moves the existing oversized displacement build rejection before unused height/UV evaluation, while preserving current range proof and existing cached hits. Added profiling measures rare material shader creation (including registration while Off), precise GI actor/solve worker elapsed stages, AO setup versus draw, and query-trip queue/replay/return attribution. It does not weaken query completion, fences, material quality, range, sample count or MSAA. All HLSL and shader bytecode remain unchanged.

The release builds. Current validation includes 100 material capture checks, 96 shadow-layout comparisons, 144 exact AO blur/profile cases and 96 AO-generation cases, plus CPU bounds/state/terrain/GI/page/profiler checks. The game was not launched; installed DLL and settings are preserved. Actual game FPS for this build remains to be measured by the user. Detailed evidence, all 15 topics and remaining causal limits are in renderer/docs/rotation-disabled-performance.md and NORTHLIGHT_ROTATION_DISABLED_PERFORMANCE_REPORT_EN.md. Earlier sections below refer to earlier runs.

## Fresh 22:38 run: camera rotation and 15 parallel investigations — 22 October

The latest complete log is now the 22:38 run of `0be2a10`, SHA-256 `1948f37eb25e8c716e91b55c48d04cd39df52588f811a98ba63e6f9f88f588ea`. Its sampled modes are initially Off, then M2-only Displacement + normal maps, then all domains Displacement + normal maps. There is no Normals-only interval; cavity/self-shadow strengths are zero. The moon is active, unlike the preceding no-source run. Earlier sections below describe historical inputs and should not be read as findings from this newer run.

A changing-view window averages 35.18 FPS and a later settled window 78.22 FPS without changing material modes. Material CPU reaches 9.7860 ms (4.1555 ms exclusive bounds/pose, 1.2737 ms snapshot service); after settling it falls to 2.9994 ms. Normal generation is only 0.1107 ms at the slow sample and zero when warm. GPU GameScene/native terrain dominate the largest GPU sample. CPU/GPU intervals are separate and query waits overlap required work.

Three workers examined five topics each. The retained fixes are: constant-time material-bounds LRU instead of a 4,096-entry victim scan; lossless adaptive 16/32-bit CPU terrain indices to retain more exact views in the existing 32 MiB budget; and exact early shader rejection before unused material-normal decoding. The camera eye really travels around the avatar during orbit, so changing world/GI coverage to follow the avatar would change output and was not adopted.

The frozen bounds fixture improves 3.607653 -> 0.035755 ms, with exact victim/order/output checks. The terrain fixture reduces 3,816 warm-return misses and 38,236,320 source bytes to zero, with identical expanded geometry and unchanged uint32 GPU output. All 576 old/new shader image comparisons and 100 capture checks pass, including MSAA. Far material shader fixtures improve about 16%; near paths have 2–4% branch overhead and some 4K cases are noisy or regress. These are component measurements, not a measured game FPS improvement. Quality/ranges/MSAA/lifetime safeguards and settings remain unchanged; the game was not launched.

Detailed evidence, all 15 topics, validation, tradeoffs and next-run measurements are in `renderer/docs/rotation-performance.md` and the standalone `NORTHLIGHT_ROTATION_PERFORMANCE_REPORT_EN.md`. Existing selected-setter reuse is confirmed working (9,841 skipped / 673 issued at frame 3810). Generic current-pose fallback and legitimate changing-view scene work remain separate optimization targets.

## Beyond M2: parallel fog, AO and static-scene optimization — 22 October

Three workers evaluated **15 hypotheses**. The latest game log ranks the main non-M2 GPU Effects costs as FogMarch 0.861 ms, FogClouds 0.642 ms, WorldComposite 0.540 ms and AOBlur 0.331 ms (medians of 20 sampled frames). Five exact-work changes were retained: invalid regional fog density elision, zero-direct cloud shadow elision, an AO centre proof that avoids a redundant nine-read probe, request-local static selection axes, and adjacent static spatial-group lookup reuse.

Against standalone baseline `ddff7cf`, dense fog fixtures improve about 4.5–9.0%; dense clouds with no direct source improve 17.9%. AO improves about 3–4% on production-FP16 occluded/mixed fixtures. Three real WMO decodes use 3.34–4.03% less CPU, while the isolated static selection predicate improves 3.49x. These percentages describe separate components and must not be added or presented as game FPS gains. Fog has a measured short-view branch cost of about +0.006/+0.008 ms, and open AO has a tiny branch overhead. Earlier AO FP32-only performance claims were invalidated by production-FP16 testing and their code was discarded.

**52 fog and 162 AO comparisons pass bit exactly**, including AO odd grids, MSAA 0/2/4/8, threshold impulses and HDR bloom RGB. Static selection passes 262,144 exact decisions in release/debug builds, and real decoded/packed model fingerprints match the old code. Integrated shader/reference/wiring checks and 96 four-AO-method GPU cases pass; only the three selected shader entries change, with 31 other entries byte-identical. The combined DLL retains the preceding M2 capture/source fixes and profiler stages. Game settings, sample counts, quality, ranges, MSAA, memory limits and resource lifetimes are preserved.

The proposed broad unused-capture skip was deferred because a newly adopted world/light publication can introduce a consumer after capture; omitting its first lamp shadow would violate the appearance requirement. The agent did not launch the game or replace its installed DLL. Detailed English evidence, all 15 hypotheses, reproducible commands and honest performance limits are in `renderer/docs/non-m2-performance.md` and `NORTHLIGHT_NON_M2_PERFORMANCE_REPORT_EN.md`.

## Fresh run: shared sources confirmed; capture state reuse — 22 October, 20:42

### Observed mode and input evidence

The tested DLL is `840faf8`, SHA-256 `22a1407183b186cd66001ea6d0be4147a60082ee6e2fb916085d9a508f27362f`. The complete fresh log has SHA-256 `b6d586124b923178c0b69f0c1598bc61ab9c59530e1b6cbb8d287609c98a0b20` and ends at device destruction. Inputs, parsed frames, experiments and validation are preserved in `northlight-diagnostics/material-m2-normals-20261022-2042/`. The 24,328-byte `.prev.log` contains a short startup, so the representative previous run is the archived 20:12 run below. The game was not launched during this investigation.

**There is no M2 Normals-only (`mode=1`) interval in this log.** The sampled modes are `3/3/3/3` initially, `3/3/3/0` from frame 3810, `3/0/0/0` from 4826, and `0/0/0/0` from 5207. Value 3 means Displacement + normal maps. Cavity and self-shadow strengths remain zero. These observations cannot establish pure Normals-mode cost or the visual operation of disabled occlusion effects.

### What improved, and where time remains

The previous source-sharing change is working. At warm frames 2032 / 2159 / 2540, all **379 captures** are queued and flushed: 375 M2 and four WMO draws. All use resident GPU geometry. There are **zero UP draws/bytes, zero new uploads, and zero actual source reads** in capture, displacement, skin-range and rigid-range caches. Capture borrows 259 / 259 / 255 sources; displacement reports 652 / 652 / 649 peer hits. Normal builds, height reads, mip generation, subdivision and geometry upload stages are also zero in these frames. The maps are not regenerated every frame; an MPQ cache would not remove the remaining warm cost.

| Exclusive CPU stage, all material domains | Frame 2032 | Frame 2159 | Frame 2540 |
| --- | ---: | ---: | ---: |
| Bounds / pose | 1.5495 ms | 1.5278 ms | 1.5310 ms |
| Native scene draw submission | 1.0592 ms | 1.0158 ms | 1.0028 ms |
| Capture flush setup | 0.7572 ms | 0.7353 ms | 0.7232 ms |
| Packet recording | 0.4338 ms | 0.3783 ms | 0.3849 ms |
| Snapshot/cache service | 0.3345 ms | 0.3309 ms | 0.3136 ms |
| Capture setup | 0.2599 ms | 0.2485 ms | 0.2375 ms |
| Capture draw submission | 0.1210 ms | 0.1152 ms | 0.1140 ms |
| Normal-map cache lookup | 0.0404 ms | 0.0357 ms | 0.0357 ms |
| Total instrumented material CPU | 4.9391 ms | 4.7637 ms | 4.7150 ms |

M2 bounds/pose consumes 0.9609–0.9876 ms, its snapshot service 0.1046–0.1173 ms, and M2-labelled flush setup 0.6370–0.6692 ms. The previous warm run had 1.3286–1.3596 ms M2 bounds and 0.4271–0.4349 ms M2 snapshot service, with fewer M2 captures (323–346). This supports reduced preparation work; it is not a controlled same-camera FPS comparison. The native draw stage includes the game's ordinary rendering and is not wholly incremental material overhead.

Complete GPU frames take 11.185–11.551 ms. Capture flush intervals sum to 1.6363 / 1.3855 / 1.0389 ms, while final material application costs 0.1464–0.1475 ms. A mixed flush is labelled by its first packet; domain labels do not isolate its exact M2 contribution. CPU and GPU measurements are separate timelines and must not be added.

The largest remaining measured CPU stage is bounds/pose. Each of these warm frames still evaluates 8,177 pose vertices, records 311 envelope time-budget deferrals, and reaches 286 pose-budget refusals. There are no cold/built/unsupported envelope events in these frames: repeated current-pose evaluation and admission remain after source reads have disappeared. Source snapshots can be cached across animation; a bound derived from old bone matrices cannot safely be reused as a current visibility proof.

Movement remains more expensive. Samples 1143 / 1397 / 2667 / 3048 have roughly 9.64–12.67 ms instrumented CPU, including 3.55–4.19 ms bounds, 1.05–1.27 ms snapshot service and 1.20–1.88 ms flush setup. Newly encountered assets can use both independent cold-read budgets and return to UP fallback. Normal builds are only about 0.04–0.08 ms in these samples, although map generation/eviction does increase while travelling. The normal cache eventually reduces its limit under its own virtual-address guard; existing safety limits were preserved.

### Implemented optimization and measured proof

Each deferred capture previously rebound the same shaders, declaration, render states, sampler states, textures and entire constant banks even when the previous packet had identical values. The warm log measures about 0.72–0.76 ms in flush setup, making this a recurring, bounded target.

The flush now skips setters whose exact state is already installed by that same uninterrupted replay. The cache is local to one flush and requires no allocation. The first packet binds every selected state. Shader constants are compared bit for bit, including complete vertex float banks and vertex/pixel integer and Boolean banks; pixel float reuse also requires identical register range and length. Texture and sampler tracking is per stage, so transitions between native inputs and generated normal-map stages remain correct. Cache entries update only after successful setters. Failed draws stop replay as before.

Viewport, render targets, streams, frequency and index bindings retain their original calls, including after UP draws. The cache never crosses native rendering, a texture-write barrier, a separate flush or state restoration. Packet ownership, immutable source revisions, per-draw bone poses, alpha coverage, MSAA and all configured quality/range controls are unchanged. No additional material draw or GPU query is introduced.

A submission-focused offscreen fixture compares the final code against the actual `840faf8` capture/source headers. It renders 160 native/capture pairs at 320×200 with 2× MSAA, alpha-dependent M2 capture, resident indexed geometry and a changing four-bone pose. Both binaries use the renderer's production `memcmp`; three alternating rounds each have four warm frames and eight measured frames. All native/material/depth outputs remain bit exact.

| Median over three rounds | Previous deferred replay | New deferred replay |
| --- | ---: | ---: |
| CPU submission | 0.929013 ms | 0.761475 ms |
| GPU command interval | 0.850304 ms | 0.515584 ms |

This is an **18.03% CPU submission reduction** in this fixture. Per frame, 14,310 of 14,560 selected setter calls are skipped (98.28%); 250 remain. These counters cover only the selected setters, not every D3D call. The GPU interval includes command starvation and submission flow, so its reduction does not imply faster shader arithmetic. The small geometry deliberately isolates submission cost and does not predict game FPS.

The separate 4K, 64×64 tessellated fixture remains bit exact but is GPU/backpressure sensitive: old/new CPU medians are 1.592313 / 1.565525 ms and GPU medians 21.922688 / 22.146432 ms. This does not establish a GPU improvement. Complete machine-readable results and per-round logs are retained.

A prepared-envelope bounds prototype was also measured before adoption. Although it helped a four-bone microbenchmark, the 75-bone cases regressed, so that algorithm change was not included. Instead, new sampled CPU stages `poseEnvelope` and `poseFallback` separate envelope service from generic posed-vertex scans, including budget refusals. They use exclusive nested attribution without adding GPU queries. To compare with the old aggregate in future logs, add the new `boundsPose`, `poseEnvelope` and `poseFallback` exclusive times; the parent no longer includes its children's time. Bounds algorithms and budgets remain unchanged in this DLL.

### Validation and next game measurement

The final M2 suite passes **100 GPU checks**, including 40 genuine old/new shader-family comparisons with 14 outputs each, MSAA 0/2/4/8, animation, source DISCARD mutation, raster/scissor changes, sampler changes, float/integer/Boolean bank changes and restoration, native alpha, texture-write barriers, packet/memory limits, resident allocation failure and multistream fallback. A separate WMO run passes **19 checks**, including diffuse-dependent capture with zero vertex alpha. Geometry/source-sharing, displacement-state and material-profiler CPU tests pass. The profiler test explicitly verifies exclusive attribution for both new pose stages.

The x86 release DLL builds successfully, preserving the 17 exports; only the two existing compiler warnings remain. The installed DLL and settings were not replaced. The package contains a separate replacement `d3d9.dll`.

The next user-run log should report `MATERIAL_BATCH_STATE issued=... skippedIdentical=...`, together with `poseEnvelope` / `poseFallback`, existing source/read counters and resident/UP counts. A stationary warm interval will show whether skipped setters reduce game flush CPU and which bounds path dominates. For a pure M2 Normals test, the sampled configuration must show M2 value **1**. Sparse zero-pixel audit grids are not used as a reason to omit rendering.


## Fresh run: GPU residency confirmed; shared source snapshots — 22 October, 20:12

### What the user actually ran

The installed DLL matches `ba4a755`, SHA-256 `4393bdfebae6f8f6f222472a5dbd982d38e30133d78001ef7acf0498b961187e`. The complete fresh log ends at device destruction and has SHA-256 `1993365eba85dc6170342a8488f017c0e501fe501a20f6e238238dfbf06423a3`. Immutable inputs and parsed frame records are in `northlight-diagnostics/material-m2-normals-20261022-2012/`; the folder follows the user's requested test name, not the observed mode. Its previous-log copy is the 19:37 run. The game was not launched during this investigation.

**The log does not contain an M2 Normals-only interval.** M2 is Off through frame 2159 and uses **Displacement + normal maps (`mode=3`)** at samples 2286, 2413 and 2540. The saved configuration also contains `AutoBumpModels=3`; the menu maps value 1 to Normal maps and value 3 to Displacement + normal maps. Cavity and self-shadow are disabled. Therefore this run validates residency under displacement-mode admission, but cannot establish the new cost of a pure M2 Normals-only setting.

### The previous optimization worked in the game

At warm frames 2413 / 2540, all **328 / 351 captures** use resident GPU geometry, including **323 / 346 M2 captures**. Both frames have **zero UP draws, zero UP bytes and zero source reads in the capture cache**. New resident uploads are zero / 19,452 bytes (eight small promotions in the second frame). The material GPU geometry cache occupies approximately 20.85–20.89 MB, below its unchanged 32 MiB limit. There are no snapshot fallbacks, unsafe live-state fallbacks, texture refusals or source-cache budget failures in capture.

| Material work, all domains unless specified | Frame 2413 | Frame 2540 |
| --- | ---: | ---: |
| Capture draw CPU submission | 0.0994 ms | 0.0995 ms |
| Bounds / pose CPU | 1.8673 ms | 1.9003 ms |
| M2 share of bounds / pose CPU | 1.3286 ms | 1.3596 ms |
| Geometry snapshot CPU | 0.6049 ms | 0.6114 ms |
| M2 share of snapshot CPU | 0.4349 ms | 0.4271 ms |
| Capture flush setup CPU | 0.6468 ms | 0.7247 ms |
| Packet recording CPU | 0.3122 ms | 0.3372 ms |
| Material CPU total | 5.0308 ms | 5.1465 ms |
| Sum of all capture flush GPU intervals | 1.8293 ms | 2.1524 ms |
| Complete GPU frame | 10.184 ms | 11.226 ms |

Capture submission previously consumed approximately 2.48–2.67 ms CPU in displacement-mode samples; it now consumes about 0.10 ms. The previous run's comparable mode-3 warm sample had 7.6292 ms material CPU attribution and a 14.704 ms complete GPU interval. The new game windows show 13.95–15.07 ms median frame times, with mean FPS 66.99–71.13, versus 17.70–19.30 ms median windows in the earlier displacement run. This is useful observational improvement, not a controlled identical-scene FPS benchmark: draw counts, GPU interval distribution, focus and outliers vary. CPU and GPU times are separate timelines and must not be added.

The main remaining measured CPU cost is **bounds/pose plus source snapshot preparation**. Normal builds, mip generation, height reads, subdivision and displacement geometry uploads are zero in both warm samples. The normal-map cache retains 108 lifetime builds and zero evictions. The displacement lifetime counters show zero displaced draws in this camera position, despite mode 3 being enabled: geometry admission still runs before falling back to normal capture. Sampled pose work evaluates approximately 8,163–8,187 vertices per frame and reaches 97 pose-budget refusals. These refusals preserve native rendering/capture; they do not justify treating a model as invisible.

### Cause and proof before implementation

The code maintains independent full-source snapshot caches for material capture and displacement (24 MiB each), plus a 4 MiB skin-range cache used by normal/POM distance admission. Each uses the same tracked buffers and copies the same immutable mesh independently. The warmed capture set already occupies 25,163,316 bytes; displacement also admits the broader world set. The live displacement counters reach 37,704 snapshot budget refusals and 20,939 bounds misses, filling the 4,096-entry bounds table. Cache-owner churn can invalidate otherwise reusable weakly owned bounds/envelopes and repeatedly spend cold-read/build budgets. The old log does not expose displacement's exact per-frame read bytes or envelope-build share, so their precise contribution remains an inference to test with new counters.

A CPU experiment reproduced the capacity problem before changing production code, using the actual snapshot reader and fake write-tracked buffers. It walks 96 world spans and 160 M2 spans, approximately 33.5 MB of unique geometry, over 40 frames. Both full-source caches retain their existing 24 MiB limits and 2 MiB/frame cold-read budgets.

| Final warm frame | Independent sources | Shared immutable sources |
| --- | ---: | ---: |
| Accepted world + model pass reads, out of 416 | 351 | 416 |
| Actual source read bytes | 2,096,448 | 0 |
| Retained source bytes across the two caches | 46,146,672 | 33,656,832 |
| Peer hits | 0 | 160 |

The integrated regression reproduces these counts and proves stable shared mesh ownership across the final frames. A separate Normals distance-admission reproduction preserves its 4 MiB cache / 256 KiB read budget and the capture cache's 24 MiB / 2 MiB limits: final skin-range reads fall from **262,056 bytes to zero**, with 140 peer hits. This establishes avoided I/O, copying and source-identity churn. It does not predict a particular whole-game FPS gain or claim that all current pose evaluations disappear.

### Implemented change and safeguards

The existing snapshot reader now optionally serves a local cache miss from a peer's resident immutable snapshot. Displacement and capture share their full-source caches in both directions; skin-range admission may also borrow from capture. No duplicate snapshot is inserted into the borrower, no extra resource getters/readbacks are introduced for the peer lookup, and cache capacities and cold-read budgets are unchanged.

Borrowing requires a fully tracked current generation, identical identity/version/metadata providers and an exact complete snapshot key, including declaration/layout, stream ranges, draw arguments and index/source revisions. Both readers must already use the private actual-read accounting policy; the ordinary capture admission policy cannot borrow around its budget. A peer lookup never follows another peer link, so reciprocal links cannot recurse. Unknown resources, changed generations and unsupported layouts take their original read/fallback paths. Bone constants and bounds are still evaluated for the current pose; old poses are never reused as a visibility proof. Reset and memory-pressure paths keep their existing release behavior.

`MATERIAL_BATCH_SOURCE` now adds `sourcePeerHits`. New `MATERIAL_GEOMETRY_SOURCE` records expose displacement resident service/peer hits, misses, actual read bytes, cache occupancy, skin-range peer hits/read bytes, rigid range reads, posed vertices, and envelope readiness/build/unsupported/budget counters. Resident-service hits include requests served for the peer. These counters will distinguish remaining vertex evaluation from repeated source reads and envelope construction in the next game run. The existing `MATERIAL_BATCH_GPU` continues to verify zero repeated warm UP submission.

### Validation and next measurement

The final material suite passes **100 GPU checks**: the existing 60 capture/MSAA/animation/mutation/pressure/failure checks plus 40 old/new shader-family comparisons. Each old/new M2 fixture compares 14 native, normal, POM, displacement and depth outputs **bit exactly** against `ba4a755`, with the old snapshot and displacement headers restored for a genuine baseline. Source-sharing CPU tests cover revisions, reciprocal misses, provider mismatch, ordinary budget preservation, immutable generations and both displaced/normal-only working sets. Geometry and displacement-state tests pass. The existing Map/Predict/Prefetch differential test also passes in normal and forced 4-bit-hash collision builds, including its directed lifetime cases. Those runs use Zig x86 Windows builds; they are not sanitizer runs.

The release DLL built successfully for x86 with all 17 exports preserved. Only the two existing compiler warnings remain. The installed DLL was not replaced.

The next game run should show increasing peer hits, reduced displacement/skin-range `readBytes`, stable envelope readiness and fewer repeated builds. Compare warmed windows with the same camera and record `mode=1` explicitly when testing M2 Normals-only. If bounds/pose remains dominant with zero reads and ready envelopes, target its actual current-pose evaluator next; if capture setup dominates, reduce identical state/constant setters within an uninterrupted flush. Sparse zero-pixel audit grids are not used to disable rendering. Render distances, quality controls, cavity/self-shadow range, MSAA and installed settings were preserved.


## M2 Normals run: batching confirmed, repeated geometry uploads removed — 22 October, 19:37

### Fresh evidence and comparison

The tested DLL is `110a1e3`, SHA-256 `225b65ab1b12210adc5500ec67e0d41af32396543048096e8b2a5b556c6debd2`. Immutable logs, settings and frame records are saved in `northlight-diagnostics/material-m2-normals-20261022-1937/`; the fresh log SHA-256 is `ea65e59146b72203e052bedfcfdd6490c0c3cde9bde102a8ce710adfd3b9454e`. Its previous-log copy is the 18:48 run discussed below. The game was not launched during this investigation.

**The last change fixed capture eligibility and source-cache churn.** At M2 Normals frames 2667 / 2794 / 2921, all **331 / 329 / 332 M2 captures** are queued and flushed. There are zero snapshot fallbacks, zero unsafe live-state fallbacks, zero texture refusals and zero source reads after warm-up. The 351 retained source snapshots occupy 25,163,316 bytes within the unchanged 24 MiB cache limit. Previously only 20–21 M2 captures were queued, with 301–302 immediate captures and 109–110 source-cache fallbacks. The texture-write guard now covers 193–194 texture-dependent draws; no source write barriers occur in these samples.

**Generated normal maps are also reused.** The log reports 108 lifetime generations, zero evictions and 51,423,832 bytes of GPU maps. All three warm samples have zero `normalBuild`, zero height reads and zero mip generation. Normal-cache lookup takes only 0.0284–0.0349 ms CPU. An MPQ/disk cache would not remove the measured per-frame cost.

Batching alone has not delivered a reliable whole-frame improvement. The two earlier normal samples consumed 4.687–4.692 ms of material CPU time; the new samples consume 6.109–6.907 ms. Warm GPU frame time varies from 12.001 to 14.653 ms, against 13.154–13.285 ms previously. These are different user-run windows, not a controlled performance comparison. New normal-mode windows at frames 2760 / 2880 have median frame times of 14.434 / 14.880 ms; mean FPS is 67.85 / 58.57, with a 256 ms outlier in the second window. The later M2-Off window has a 10.597 ms median and 91.14 mean FPS. Enabling M2 materials still carries a substantial cost.

### Where the sampled time goes

CPU figures below are exclusive stage measurements across all material domains, per frame. They must not be added to GPU timings, which measure a different timeline.

| Work | Frame 2667 | Frame 2794 | Frame 2921 |
| --- | ---: | ---: | ---: |
| Capture draw submission, CPU | 2.4410 ms | 2.4030 ms | 2.5589 ms |
| Native draw submission, CPU | 0.9551 ms | 0.9735 ms | 1.0699 ms |
| Bounds / pose, CPU | 0.8703 ms | 0.9085 ms | 0.9706 ms |
| Deferred flush setup, CPU | 0.7421 ms | 0.7405 ms | 0.8973 ms |
| Packet recording, CPU | 0.3161 ms | 0.3132 ms | 0.4246 ms |
| Geometry snapshot stage, CPU | 0.2881 ms | 0.2978 ms | 0.3773 ms |
| Normal-map cache lookup, CPU | 0.0284 ms | 0.0293 ms | 0.0349 ms |
| Material CPU total | 6.1090 ms | 6.1425 ms | 6.9069 ms |
| M2-labelled capture flushes, GPU | 2.4276 ms | 2.4144 ms | 2.9223 ms |
| M2 native scene draws, GPU | 0.7755 ms | 0.7507 ms | 0.8613 ms |
| Full GPU frame | 12.001 ms | 13.171 ms | 14.653 ms |

GPU interval coverage is complete. Flush intervals are labelled by their first packet, so a mixed WMO/M2 batch is attributed to that first domain; the table does not claim perfect per-domain attribution within mixed batches. The final full-screen material application is approximately 0.158–0.159 ms GPU. The current M2 bottleneck is capture submission/replay, rather than normal generation or final compositing.

The code explains the CPU regression: queued meshes are immutable and cached on the CPU, but every replay still calls `DrawIndexedPrimitiveUP` / `DrawPrimitiveUP`. The sampled packets reference **48.02–48.09 MB of geometry per frame**, requiring repeated backend submission of those bytes. Six packet-budget boundaries plus the final flush produce seven flushes. The 8 MiB packet budget charges a shared mesh again for each draw; it is a retention/accounting bound, not proof that all submitted bytes are unique.

### Implemented optimization

Capture now reuses the existing `NorthlightReplayGPU::Cache` for immutable mesh snapshots. After reuse across frames is established, geometry lives in DEFAULT/WRITEONLY GPU vertex/index buffers and uses ordinary indexed/non-indexed draw calls. Bone matrices, native material constants, texture bindings, alpha coverage and raster state still come from each individual packet. Animated poses are not baked into the geometry cache.

The cache uses snapshot ownership and source revisions, not mutable game-buffer pointers. Unchanged meshes upload once. New or changed meshes, exhausted admission budgets and failed allocations retain the original UP fallback. Existing replay-cache limits bound promotion to 16 attempts / 4 MiB / approximately 1 ms per frame, allowing the first attempt to make progress. Material residency is capped at 32 MiB, released on reset/trim, and disabled under memory pressure. CPU snapshot capacity, render distances, normal strength, shader algorithms and MSAA are unchanged.

New `MATERIAL_BATCH_GPU` records expose resident draws, UP draws/bytes, actual new upload bytes, reused bytes, resident/probation meshes, creation CPU time and admission/eviction reasons. A healthy warm frame should have resident draws for unchanged meshes and low or zero `uploadedBytes` / `upBytes`; actual source revisions may legitimately cause fallback.

### Controlled validation

A standalone D3D9 fixture compares the previous `110a1e3` header with the new implementation using identical alpha-dependent four-bone M2 shaders, 3840×2160 output, 2× MSAA, 160 native/capture draws, a 64×64 plane grid and cavity disabled. The repeated geometry volume is **48,176,640 bytes/frame**, close to the fresh game's packet volume. Four warm-up frames precede eight measured frames; three rounds alternate the execution order. All old/new normal targets and resolved scene-color targets are **bit exact**.

| Whole fixture frame submission | Previous queued UP capture | Resident capture |
| --- | ---: | ---: |
| Median CPU submission | 8.743713 ms | 0.784175 ms |
| Median GPU interval | 19.906560 ms | 18.343680 ms |
| Warm UP draws / bytes | 160 / 48,176,640 | 0 / 0 |
| Warm new geometry upload bytes | Repeated UP submission | 0 |

CPU submission falls by **91.0% (7.96 ms)** on this deliberately upload-heavy fixture. GPU samples overlap across rounds, so the median GPU difference is not evidence of a guaranteed GPU speedup. The fixture repeats one 301,104-byte mesh; the game has a much larger unique working set. This establishes removal of redundant uploads, not a predicted whole-game FPS gain. The next user run must confirm real residency and stage savings.

The regression suite covers the 40 SM2/SM3 M2 shader families and compares 14 image/depth outputs each. Additional checks exercise resident indexed and non-indexed geometry, changing bone constants with resident source geometry, source-buffer mutation, alpha texture writes, forced GPU-cache allocation failure, failed replay, unused packets, packet boundaries, multi-stream fallback, memory-pressure release, and MSAA preservation. MSAA-off intentionally retains immediate capture. CPU geometry, source-cache, state-reuse and existing replay-cache failure/lifetime/budget tests are also run. Detailed machine-readable results are retained alongside this evidence.

Final validation: **60 GPU regression checks passed**, plus CPU geometry/state tests and the existing replay-cache policy test. The release DLL built successfully for x86 with all 17 exports matching the installed DLL; the two pre-existing compiler warnings are unchanged. The installed DLL and game settings were preserved.

### Remaining priorities

1. Confirm resident capture and reduced `captureDraw` CPU time in the next game log; compare warm frames and unchanged scene settings.
2. Deduplicate shared geometry in packet-memory accounting if packet boundaries remain frequent; preserve bounded retention and draw order.
3. Reduce identical state/constant setters inside an uninterrupted capture flush; its current exclusive setup costs 0.74–0.90 ms CPU.
4. Reduce repeated conservative bounds/pose work (0.87–0.97 ms CPU) without rejecting animated geometry from stale poses.
5. Investigate capture visibility against the final scene depth if capture GPU time remains dominant. Sparse audit samples cannot safely justify skipping a draw.

The visibility audit confirms a nonzero bump contribution at frame 2667, but only one coarse 128×72 sample lies in the fading normal range and none in the full-strength range. Other sampled grids miss visible contributions. This helps explain why bump looks weak in this camera position; it does not prove a broken normal shader or that every captured model is invisible. Normal detail fades over 36–48 yards; cavity/self-shadow share the configured 8-yard range and are disabled in this run. No appearance or distance setting was changed to obtain the optimization.


## M2 Normals run: cached maps, incomplete batching — 22 October, 18:48

### Evidence from the new user run

The installed DLL is the delivered `6db9341` build, SHA-256 `753b77e19fb7176e15892f900f2353cb974dbd5113b4d75c71d87f1895b8c760`. Immutable inputs and parsed frame records are in `northlight-diagnostics/material-m2-normals-20261022-1848/`. The new log has SHA-256 `7b741848281c5f324238379940cc028e52c5cf1d16787391587dec10c68051e9`; the previous log is the retained 17:33 run. The new run enables M2 displacement temporarily, then **M2 Normals at frames 2921/3048**, and later M2 Off. The game was not launched during this investigation.

**Normal-map regeneration is not the warm bottleneck.** The cache reports 108 lifetime builds, 109,453 hits, zero evictions, 51,423,832 bytes of generated GPU maps and a 96 MiB map budget. Both normal-mode samples execute zero normal builds, zero height reads and zero mip generation. The 322 M2 cache lookups cost approximately **0.029 ms CPU**, all hits. The `AUTONORMAL generated` field is a lifetime build count, not builds per frame. `MATERIAL_STAGE normalBuild` and `mips` establish actual sampled-frame work. Original albedo textures come from the client assets; generated normals are retained GPU textures. Adding an MPQ/disk cache would not remove the measured per-frame capture cost.

The expensive work is capture and its fallback paths:

| M2 work | Frame 2921 | Frame 3048 |
| --- | ---: | ---: |
| M2 captures / queued M2 captures | 322 / 21 | 322 / 20 |
| Immediate M2 captures | 301 | 302 |
| Unbatchable live-state candidates | 192 | 192 |
| Failed snapshot-path candidates | 109 | 110 |
| Immediate setup GPU interval | 0.7035 ms | 0.7127 ms |
| Immediate capture GPU interval | 1.4735 ms | 1.5124 ms |
| Original 510 M2 draws, GPU interval | 2.2221 ms | 1.8555 ms |
| Queued M2 flushes / GPU interval | 10 / 0.0594 ms | 9 / 0.0532 ms |
| All material/native CPU attribution | 4.6866 ms | 4.6916 ms |

World batching **does execute**: five WMO captures are queued and flushed, with a 0.0143 ms world flush interval at frame 2921. The remaining M2 queue is small and repeatedly interrupted by immediate capture. Thus `enabled=1` alone was insufficient evidence of useful batching. The old `snapshotFallback` field aggregates several possible refusal causes; the log cannot distinguish read-budget exhaustion from a multistream rejection. The new counters described below make that distinction explicit.

With M2 Off, sampled total material/native CPU attribution is approximately 2.15–2.59 ms; the 510 original M2 draws cost 0.1976 ms GPU at frame 2667 and 0.1946 ms at frame 3302. GPU transitions can be attributed to a following original draw: the large original-draw interval in normal mode is not evidence that the unmodified native shader suddenly performs more normal math. CPU/GPU intervals overlap and must not be added. Profiling-query submission itself costs 0.57–0.58 ms in the warm normal samples, with zero new queries; instrumentation is sampled.

The frame-window means around frames 3000/3120 are 17.849/16.757 ms (56.03/59.68 FPS), versus 12.113 ms (82.56 FPS) at frame 3360 with M2 Off. These windows include setting transitions near their boundaries and occasional stalls, so they are observational context, not a controlled before/after benchmark. The normal-mode GPU samples themselves total 13.154–13.285 ms. The earlier displacement sample creates new profiling queries and spends 4.295 ms in bounds/pose work; it should not be mixed into warm normal-only attribution.

All twelve regular-grid audits report zero captured and zero near-scene samples, including during normal mode. This supports a distant-camera/limited near-coverage explanation, but a 128×72 grid can miss small visible regions and does not establish an empty full-resolution target. It cannot safely be used to skip the next frame's rendering. Cavity and self-shadow remain disabled. Strength, distance and quality settings are unchanged; normal detail still fades over 36–48 at the configured range of 48.

### Implemented optimizations and their limits

1. **Guarded batching for alpha-dependent M2 captures.** The patched native alpha program identifies the pixel samplers it actually consumes. A packet retains those 2D textures and all thirteen sampler states per consumed stage, alongside the existing immutable geometry and current shader constants. It no longer rejects a capture merely because native alpha needs the original diffuse/layer texture. Matching texture writes execute the pending private capture before any source change. Unrelated writes do not drain the batch or enter a material raw-state scope.

   This reuses the existing serialized command-stream/resource-writer hooks, including Texture/Surface Lock/Unlock, dirty regions, mip generation and device copies. SetLOD and surface GetDC/ReleaseDC now also report a boundary. Production enables this path only when that writer contract exists: command stream active, mirror active and the existing spell/resource sink installed. A mirror/resource escape drains the batch before disabling the guard; a failed command-stream setup disables it too. Without the contract, native alpha capture remains immediate. Render-target/depth textures, cube/volume inputs, relative pixel constants and unsupported state retain conservative fallbacks. Ordinary managed/default 2D sources can be guarded; no texture readback or texture copy is introduced.

2. **Avoid source-cache cycling under a larger working set.** Successful queued M2 captures report a cumulative 8.37–9.78 MiB of source payload per sampled frame. That sum can include repeated meshes; it is not a measurement of unique retained sources. The former 4 MiB cap cannot retain comparable larger working sets, as the controlled test below demonstrates. The retained snapshot cap is now **24 MiB**, with the existing **2 MiB/frame cold-read budget unchanged**. This bounds warm-up traffic without making cached sources cycle through a smaller-than-working-set cache. Under memory pressure the cap returns to 4 MiB, pending work and caches are released, and queueing remains disabled. Geometry is still copied into immutable snapshots; native buffer writes cannot alter pending captures.

   A runnable tracked-buffer regression uses 128 source spans and sixteen warm-up frames. With the old cap its final frame has **32 hits and 2,032,236 read bytes**; with 24 MiB it has **128 hits and zero read bytes**, under the same 2 MiB/frame budget. This proves the working-set effect in the controlled test. The previous user log lacks cache miss/eviction reasons, so the exact proportion of its 109–110 failures caused by cache pressure remains to be measured.

3. **Reject unsupported multistream packets before reading their buffers.** The private UP replay currently supports one stream. The declaration is now checked before source reads or texture/state recording, so an inevitable immediate fallback cannot spend the source-read budget first. A regression preserves the immediate output bit-for-bit and asserts zero snapshot bytes for these candidates.

4. **Drain and retry bounded full batches.** Reaching the 512-packet or 8 MiB packet budget executes the existing queue, then retries the current packet in a fresh batch. It no longer forces that packet through an extra immediate capture. Private capture order, native scene order, MSAA and native alpha coverage are preserved. The 520-draw regression queues and replays all 520 draws across bounded batches. Native texture source references have a separate conservative 128 MiB retention cap; they allocate no duplicate texture image. Unsupported or unavailable resources still use immediate capture.

5. **Log the reasons, rather than infer them from one fallback total.** `MATERIAL_BATCH_SOURCE` records whether the texture guard is active, how many texture-dependent captures queue, actual write barriers, refused textures, source-budget failures, multistream failures, batch limits, cache hits/misses, source read bytes and retained cache size. These accompany the existing CPU/GPU stages and per-domain queued/flushed counters. `enabled`, `textureGuard`, `queued` and `sourceHits` describe different facts; none should substitute for the others.

### Validation and measured benefit

All **40 SM2/SM3 × rigid/skinned/weighted/four-bone/varying-pose × combiner fixtures** pass with native alpha test and a visible checker mask. Fourteen image outputs per fixture compare bit-for-bit between immediate and guarded deferred capture. Additional checks cover MSAA 0/2/4/8, source DISCARD writes, current palettes/scissor/fill state, packet limits, immediate barriers, multistream fallback, unused queues, failure, pressure and complete asynchronous profiling. Disabling the texture guard restores immediate alpha capture and preserves all fourteen outputs. The test's normal/visibility probes require nonzero visible coverage; an empty alpha-clipped image does not count as success.

Writes through the generated game-facing Texture/Surface wrappers, SetLOD and mip generation are tested while capture is pending. The original capture/color targets remain bit-exact after source alpha is replaced. CPU geometry, resident-snapshot working-set and displacement state/range suites pass. The one-shot mirror-disable callback is checked to run under its gate, before mirror disable. Shader source and bytecode are unchanged in this follow-up.

The final isolated, alternating, three-round **complete 4K/MSAA 2× alpha-dependent M2 pass** includes 32 native draws, all captures, recording/restoration, clears and color resolve, with cavity/self-shadow disabled. Median GPU time is **3.680000 → 2.838656 ms (22.86% reduction)**; median CPU submission is **0.064838 → 0.143875 ms (+0.079037 ms)**. Both compared targets are bit-exact. An earlier repeat of the same alpha-dependent configuration measured a 26.15% GPU reduction; the final-run median is used here. These are synthetic pass results, not measured game FPS gains. Immutable records are `complete-release.json` and `m2-alpha-validation.json` beside the input log.

Reproduce with `python tests/validate_material_deferred.py --backend <dxvk-d3d9.dll> --alpha-test --texture-guard --occlusion-off` for the 40-fixture suite; add `--quick --benchmark` for the complete-pass timing. Run `python tests/test_material_geometry.py` for the bounded working-set test. Keep GPU benchmarks isolated from other GPU tests.

The final Windows x86 DLL builds successfully with the same 17 proxy exports and the two existing warnings. SHA-256: `225b65ab1b12210adc5500ec67e0d41af32396543048096e8b2a5b556c6debd2`. The world-capture MSAA/queue regression also passes.

No game process was launched, and no game-root DLL or settings were changed. The next matching M2 Normals run should compare the number of immediate captures against the previous 301–302, confirm `textureGuard=1`, and check that warmed `sourceReadBytes` falls while `normalBuild` remains zero. That run is necessary to establish actual game FPS improvement and identify any remaining resource types or multistream captures that cannot be batched.


## New user run: world capture overhead and missing opaque bump — 22 October, 17:33

### What the fresh logs establish

The installed DLL matches the delivered `fb3ed48` build: SHA-256 `84a7caa183aae9e7e2997642f524b628a6f83497dec1da1105393e233de196a4`. The immutable snapshots are in `northlight-diagnostics/material-batch-live-20261022-1733/`; the new main log has SHA-256 `9296e189f90e29a5eee5647c6b242a22f13ba3a1faaea313d3072768cd1cdab4`. The main and previous logs both keep **M2 Off** (`modes=3/3/3/0`, also `AutoBumpModels=0` in the saved INI). Consequently, `MATERIAL_DEFERRED` reports `enabled=1 queued=0 flushed=0`: batching is permitted by the loaded DXVK 3 backend but is never exercised by M2. This run cannot establish its in-game speedup. Comparing it with the earlier M2-enabled run would conflate a code change with a different configuration.

With world materials enabled, warm sampled frame 3175 reports:

| Work | CPU exclusive (ms) | GPU interval (ms) |
| --- | ---: | ---: |
| Material constants, all domains | 0.327 | — |
| Material source snapshots | 0.211 | — |
| Bounds/pose checks | 0.366 | — |
| Capture setup, all domains | 0.419 | 1.088, for only 5 WMO captures |
| WMO capture submission | 0.0044 | 0.0777 |
| Original terrain draws, 811 | 0.598 | 2.727 |
| Original M2 draws, M2 materials Off | 0.312 | 0.224 |
| Final material composite | 0.0021 | 0.1587 |

Total measured material CPU work is **2.510 ms** versus roughly **1.07–1.54 ms** in later material-disabled samples. The shader/map cache is warm; displacement still builds and draws zero geometry. There are **969 rejected captures out of 974 successful range proofs**, with only five WMO replays remaining. The bounds cache has 891 entries and no new evidence of the former 1024-entry churn in this M2-disabled run. Expensive admission and target transitions remain even when practically no material pixels contribute.

The warm material-enabled windows at frames 3121/3241/3361 are **12.654–13.717 ms / 72.90–79.03 FPS**. Nearby disabled windows at frames 2641/2761/2881 are **10.803–11.234 ms / 89.02–92.57 FPS**. Menu, loading, focus-loss and GI warm-up intervals are excluded. This is an observational setting comparison, not a controlled before/after measurement of the new code.

The largest remaining original-scene interval in this sample is terrain rendering. Outside materials, the effect pass totals **3.134 ms**, including **FogMarch 0.847 + FogClouds 0.625 = 1.472 ms**, AO/blur 0.500 ms, and WorldComposite 0.547 ms. These are later optimization candidates; disabling those effects or reducing their quality is not part of this fix. GPU interval attribution can include transitions charged to a subsequent draw; CPU and GPU timings must not be added.

### Why bump can disappear

All six material-enabled 128×72 probes report `rawCaptured=0`, not merely rejected final depth. This is sampled coverage, not proof that every full-resolution pixel is empty; small coverage can miss the grid. It does show that the old normal-strength-only explanation is insufficient.

A definite coverage bug was reproduced in the existing WMO path. With native alpha blending and alpha test both disabled, native WMO RGB is visible even when vertex/diffuse alpha is zero. Capture nevertheless clipped or weighted its normals by that unused alpha. In an authentic WMO shader fixture at 320×200, the previous code captures **0 pixels**; the corrected code captures **37,894 pixels**. Original native color is **bit-exact**. The final normal-lighting difference rises from **5.96e-8 numerical noise** to **0.50935 maximum RGB difference**. This proves the defect and the correction. The fresh user log does not record source alpha, so it cannot prove that this defect is the only cause of its five empty-looking captures.

There is also a distance/configuration constraint: M2 is Off, the camera is about **47.6–48.8 units from the character**, and the normal range is 48. Normal detail has full weight only through 36 and fades to zero over 36–48. Character/material detail at that camera distance is therefore heavily faded or outside range. Cavity and self-shadow are still disabled; when enabled, they retain their common near range (8 in Quality). Distances, strength, POM steps, subdivision and MSAA have not been increased or reduced automatically.

The sampled audit now distinguishes scene coverage inside the normal range (`sceneNormalRange`), inside the full-weight region (`sceneNormalFull`), and inside the common occlusion range (`sceneOcclusionRange`). These complement raw capture and depth-mismatch counts. They will distinguish an empty near region from a capture-coverage problem in the next user run. All nine 128×72 queries remain asynchronous and bounded; production uses no GPU waits or full-target readbacks.

### Changes implemented and measured

1. **Reject distant rigid displacement draws before capture state setup.** The conservative range proof now also applies to rigid mode-3 draws. Buffered mode-3 probes reuse only already resident, write-tracked geometry snapshots from the existing geometry cache: a cold or untracked source follows the original preparation path. They perform no buffer locks, consume no new-source read budget, and create no duplicate source copies in the smaller optional range cache. UP draws retain their bounded position-only probe. Rejection uses the maximum of the effective capture and geometry ranges, preserving a geometry consumer even when normal range is shorter. Unknown, nonfinite and budget-limited inputs retain the original path. Skinned mode-3 draws retain their existing pose path rather than paying for a duplicate pose probe. A proof beyond geometry range also avoids full geometry preparation when only normal capture can still contribute.

   A real-device, admission-only comparison against `fb3ed48` measures **1.028003 → 0.496478 ms CPU per 1000 far draws**, averaged across 32 frames: **51.70% less admission CPU**. Pixel-shader and depth-surface queries fall from **32,000 each to zero**. Both variants perform zero GPU capture work. This is not a whole-frame FPS claim.

2. **Batch safe world captures as well as M2.** Terrain opaque and supported WMO opaque captures now use the same bounded immutable replay queue. A native opaque diffuse WMO draw uses the existing map-only capture because its unused diffuse alpha creates no live-texture dependency. Normal-map binding is stored per packet (world sampler 2, M2 sampler 14); counters retain the actual material domain. Native scene order, private capture order, native color shaders and MSAA are preserved. Prepared displacement, layered/alpha-dependent captures and unsupported state keep immediate replay. The previous 512-packet/8 MiB packet budget, 2 MiB new-source read budget and 4 MiB retained source cache remain unchanged.

   In the final isolated 4K/MSAA 2× WMO test, three alternating complete-pass rounds reduce median GPU time **17.583232 → 9.038208 ms (48.60%)**. CPU submission changes **0.093912 → 0.133500 ms (+0.039588 ms)**. Original color and normal/depth targets are bit-exact. The tested opaque-terrain pass similarly changes **17.809664 → 9.299584 ms (47.78%)**. These synthetic scenes use 32 native draws plus captures and color resolve, with occlusion disabled as in the user log. A concurrent validation/benchmark run timed out its diagnostic query and was excluded; the isolated WMO repeat is stable at 17.14–17.79 ms versus 8.97–9.15 ms. A user game run is still needed to establish actual FPS changes and queue utilization.

3. **Honor native coverage rather than unused WMO alpha.** Opaque WMO capture writes full coverage; alpha-test and blending continue to use their native alpha. Unneeded blending of the fully covered normal target is disabled. The coverage offset folds into the shared composite's existing multiply-add, keeping its strict **512-slot SM3 limit** without reducing quality. Alpha-test and alpha-blending regressions compare the old/new outputs bit-for-bit (13 and 8 outputs respectively). The intentional change is restored bump on opaque surfaces whose native alpha is unused.

### Validation and reproduction

- The CPU range/state suite passes, including a normal range shorter than geometry range, conservative boundaries, source replacement, pose budgets, nonfinite values, constant epochs and reset/lifetime handling.
- All **40 M2 shader/skin/combiner fixtures** pass; the normal/capture/native POM bytecode for M2 is unchanged. New world queue tests cover MSAA 0/2/4/8, immutable DISCARD snapshots, current matrices, changing scissor/fill state, immediate barriers, the 520-draw packet limit, unused queues, injected failure, memory pressure and complete asynchronous profiling. Native D3D9 world tests also pass with batching forced for compatibility; production still leaves batching disabled there.
- All **56 terrain/WMO/composite/grass MSAA/pressure cases** pass, plus **256 shared-POM comparisons**. Shared POM remains disabled by default. Only the WMO coverage capture programs and the diagnostic audit change shader bytecode. CPU/GPU benchmark timings are preserved separately from correctness comparisons.
- Prepare world fixtures with `python tests/validate_parallax_msaa.py --backend <dxvk-d3d9.dll> --output-subdir material-msaa-release`. Run `tests/validate_material_deferred.py --backend <dxvk-d3d9.dll> --family terrain --quick --baseline-ref fb3ed48 --benchmark --occlusion-off` for admission/terrain proof; use `--family wall --wall-diffuse --vertex-alpha-zero --quick --baseline-ref fb3ed48` for the missing-bump and native-alpha regressions. Use `--family wall --quick --benchmark --occlusion-off` for WMO timing, running GPU benchmarks without concurrent GPU workloads.

The final Windows x86 DLL builds successfully with the same 17 proxy exports and the two existing compiler warnings. SHA-256: `753b77e19fb7176e15892f900f2353cb974dbd5113b4d75c71d87f1895b8c760`. Windows CPU geometry/state/snapshot-policy tests and final terrain, M2, WMO alpha/MSAA/queue regressions pass. Additional native clang++/sanitizer snapshot scripts could not start because clang++ is unavailable on this host; they are not counted as passes. No new compiler warning was introduced.

No game process was launched and no game-root DLL or settings were replaced. The next user run should explicitly enable M2 Normals for an M2 batching comparison, retain the same camera for on/off timing, and include a close view of a WMO surface for the bump check. The new domain counters identify world batching separately from M2 batching.

## M2 capture bottleneck — 22 October, 15:18 user run (previous build)

The new log confirms that the shared material capture path is expensive even in normal-only mode. The installed DLL is `7c5c699`, SHA-256 `57a923a347c118496572fc06e40ed69c20b19a1f6753feb4d2342eb3939e232b`. The immutable input is `northlight-diagnostics/material-live-stages-20261022/northlight-renderer.log`, SHA-256 `79495013dbaa887454d768137ed37c99472cb0656b5f622d46ca2a83c488fa60`. This is a 3840×2160, MSAA 2×, DXVK 3.1.1 run. `parsed-stages.json` and `warm-stage-summary.json` preserve the per-frame evidence. No game process was started during this work.

### Where the frame time goes

These are warmed samples with **zero newly created profiling queries**, not cold shader/map creation:

| M2 setting / sampled frame | Material CPU, all domains (ms) | M2 capture draw GPU (ms) | Original M2 draw GPU interval (ms) | Final material composite GPU (ms) |
| --- | ---: | ---: | ---: | ---: |
| Displacement + normals / 2286 | 5.773 | 1.786 (332 draws) | 2.066 (510 draws) | 0.158 |
| POM / 2921 | 4.185 | 1.890 (348 draws) | 2.231 (510 draws) | 0.159 |
| Normals / 3175 | 3.662 | 2.144 (326 draws) | 2.039 (510 draws) | 0.159 |
| Off / 3302 | 2.366 | 0 | 0.190 (510 draws) | 0.160 |

The normal-only frame excludes POM and displacement math as the primary explanation. Its native pixel shader is unchanged, yet its original-draw interval is about 1.85 ms longer than with models off. Repeated transitions between the MSAA scene and the single-sample material targets interrupt the native pass; a backend can charge transition work to the following draw. Individual rows do not identify a shader's isolated arithmetic cost. The complete-pass experiment below tests the transition hypothesis directly.

CPU work is also measurable. At frame 2286, bounds/pose evaluation costs **2.028 ms**, source snapshots **0.804 ms**, capture setup **0.893 ms**, capture submission **0.260 ms**, and restoration **0.253 ms** across all domains. At frame 3175 those stages cost **0.695 / 0.300 / 0.758 / 0.212 / 0.209 ms**. Normal-map generation, mip generation, height extraction, subdivision and geometry uploads execute **zero work** in these samples. Sampled query submission itself costs about 0.52–0.57 ms in the active-model samples; profiling is sampled, not performed every frame. CPU and GPU intervals overlap and must not be added.

The observed normal-only frame window is **15.491 ms / 64.55 FPS** at frame 3240. Later model-off windows are **10.733–11.282 ms / 88.64–93.17 FPS** at frames 3480/3600/3720. Displacement-mode windows are roughly 16–17 ms; POM windows roughly 15–16 ms. Focus-loss and large-stutter windows are excluded from this comparison. These setting toggles corroborate the slowdown, but are not a before/after test of the new DLL.

### Are the effects working?

- **Cavity and POM self-shadow are disabled in this user run.** Every configuration reports `cavity=0.000 selfShadow=0.000`; no occlusion target is published. Their zero visible-pixel counts are expected and cannot establish a broken shader.
- **Bump is executing but has almost no confirmed visible contribution in this scene.** Of 17 regular-grid probes, 15 report zero valid samples; the other two report only 3–4 valid samples and 3 changed normals. This 128×72 grid is not a full-resolution visibility measurement. Range clipping or occlusion may explain the result; the old probe cannot distinguish them from a depth mismatch. The camera is roughly 49 units from the character and the configured normal range is 48, which is relevant context, not proof of the rejection cause.
- **GPU displacement is supported but executes zero height-fetch draws and builds zero geometry in this run.** The 8-yard model geometry setting cannot be blamed for tessellation that did not occur.
- Independent production-shader fixtures confirm that all three effects can change the image near the camera: maximum RGB differences are **0.2668 normal / 0.1327 self-shadow / 0.01060 cavity**. At a tested distance of 12 yards with an 8-yard common occlusion range, cavity and self-shadow differences are exactly zero while normal detail remains visible. These are test-scene results, not evidence that the effects were enabled in the user's session.

### Implemented changes and the evidence for them

**1. Group eligible M2 captures after native scene rendering on the actual DXVK 3 runtime.** The existing capture shaders and single-sample material targets remain in use. Original game draws keep their order and MSAA settings. Eligible captures retain their original order within the private pass, instead of switching render targets for every model. The queue is drained before an intervening immediate capture and before final material consumption. Unsupported or prepared-displacement draws retain immediate capture; native D3D9, DXVK2 and non-MSAA paths retain it by default.

The final alternating, three-round, complete-pass benchmark includes all 32 native draws, all captures, state snapshots/restoration, lazy clears, queue recording, and MSAA color resolve at 4K/MSAA 2×. Median GPU time changes from **7.702400 to 3.997312 ms (48.10% reduction)**. Median CPU submission changes from **0.063875 to 0.102362 ms (+0.038487 ms)**. Original color, captured normal/depth, and captured cavity/occlusion targets are **bit-exact**. `deferred-release-dxvk.json` preserves every measurement. This establishes a useful synthetic pass improvement; it does not establish the game's new FPS.

A separate run matching the user's **normal-only, cavity/self-shadow disabled** configuration also passes. Its three alternating 4K/MSAA 2× rounds measure median **17.683584 → 9.405696 ms GPU (46.81% reduction)** and **0.065388 → 0.121262 ms CPU (+0.055874 ms)**. Both original color and captured normal/depth are bit-exact; no occlusion target is allocated. These absolute values belong to a separate benchmark configuration and should not be compared directly with the auxiliary-target benchmark above. Raw records are in `deferred-no-occlusion.json`. Add `--occlusion-off` to the benchmark command to reproduce it.

Each queued draw owns an immutable mesh snapshot, current vertex/bone constants, required pixel constants, integer/boolean constants, raster/scissor/viewport state, shader/declaration references and a generated-map lease. Captures that read other live native textures, vertex textures, multiple streams, user clip planes, N-patches, or unsupported topology use the original path. Limits are **512 packets / 8 MiB charged packet-plus-source payload per batch, 2 MiB new source reads per frame, and 4 MiB retained source snapshot cache**; the existing index cache also remains bounded. Pressure disables queueing and releases pending work, its source/index caches and cached declarations. A failed batch hides incomplete material outputs and retains native rendering. Unconsumed queues are discarded at frame end without capture draws or target clears.

**2. Preserve scissor state across immediate capture too.** Render-target changes reset the D3D9 scissor rectangle. The former immediate replay could widen the game's subsequent native draw and the capture region. Both paths now preserve the authored rectangle; private target clears cover the full target before per-draw scissor is restored. A regression alternates solid/wireframe rendering, scissor rectangles, buffer DISCARD writes and current bone palettes, then compares all three targets and complete VS/PS float constant banks.

**3. Stop tiny bounds-cache records from thrashing at 1024 entries.** The log reaches 1024 entries / **65,536 bytes**, despite its 8 MiB byte budget. Repeated misses accompany millions of rescanned vertices: 8.28 million cumulative object scans by frame 2400 and 22.29 million by frame 3000. The count cap increases to **4096**, with the byte cap unchanged and weak source ownership preserved. A 1536-source regression now sustains **6144 warm hits with zero new misses**, uses less than 256 KiB, and still enforces entry/byte limits and address-reuse safety. A matching game run is needed to quantify its CPU benefit.

**4. Expose actual visibility loss.** The sampled audit adds `rawCaptured` and `depthMismatch`. `valid`, `normalChanged`, `cavity` and `selfShadow` retain their meaning. Raw coverage with rejected depth can indicate ordinary occlusion or a mismatch; it does not prove a depth bug by itself. The six 128×72 queries use a three-slot asynchronous ring and `GetData(..., 0)`, with no production GPU wait or full-target readback. Only the diagnostic `MaterialAudit` shader bytecode changes; rendering/capture shader bytecode remains unchanged.

**5. Attribute queue costs explicitly.** `deferredRecord` measures CPU snapshot/state recording; `deferredFlush` measures the complete batch GPU interval, including transitions/restoration. Its child `captureDraw` rows provide CPU submissions without overlapping GPU timestamps. `MATERIAL_DEFERRED` reports queued/flushed draws, source/state fallback, prepared geometry and unused discards on sampled frames. The next user log can therefore show whether batching was actually used and whether source budgets or live-state dependencies force frequent barriers.

### Validation and remaining check

DXVK: all **40 M2 shader/skin/combiner fixtures** compare 14 outputs each bit-for-bit, plus MSAA 0/4/8 comparisons, DISCARD-and-palette mutation, changing scissor/fill mode, immediate/deferred barriers, a 520-draw packet-limit case, unused queues and injected replay failure. Native D3D9 passes the weighted four-bone fixture and the same MSAA/safety cases with queueing forced for compatibility tests; queueing remains disabled there in production. The **56-case terrain/wall/composite/grass MSAA/pressure matrix** passes. CPU regressions cover bounds ownership/accounting, geometry/snapshot/upload correctness, state/constant epochs and nonblocking profiling. The shader gate recompiles all world entries; only `MaterialAudit` bytecode differs.

Reproduce with `python tests/validate_material_deferred.py --backend <dxvk-d3d9.dll> --benchmark`, `python tests/validate_material_stages.py --backend <dxvk-d3d9.dll>`, `python tests/validate_parallax_msaa.py --backend <dxvk-d3d9.dll>` and the material CPU tests. Fixture prerequisites are documented by those runners. Build with `python scripts/build_renderer.py`.

The Windows x86 DLL builds successfully, retains all **17 proxy exports**, and is **5,481,984 bytes** with SHA-256 `84a7caa183aae9e7e2997642f524b628a6f83497dec1da1105393e233de196a4`. The build emits the same two existing shared-POM/height-texture warnings. Injected memory pressure also passes with a populated source cache, and the live GPU profiling fixture reports complete coverage including `deferredFlush` and all six visibility counters. The installed game DLL remains unchanged; a separate test package is produced.

A new user game run must confirm actual queue utilization, the complete native/capture GPU delta, bounds misses and visible coverage. Shared POM remains disabled by default. No distance, strength, subdivision quality or MSAA setting is reduced by this change.

## Material stage diagnostics and near-only occlusion — 22 October, 13:48 user run

The installed DLL is the delivered `1f98a0b` build: SHA-256 `c82ea36ef2aff004e28510ffdfabdb8fcd0e42158a2baea787983139e77ed479`. The new immutable log is `northlight-diagnostics/material-stages-20261022/` (input SHA-256 `8e7d1a79ac1afe48be6afaa833d2f2ae6621ec0ec20e034b8e726e5673ab6cf6`). `inputs.json`, `log-index.json` and `frame-windows.json` preserve source identity, setting changes and matching camera windows. The game was not launched during this investigation.

### What the latest game run establishes

- At the same logged camera near the character, model **normal-only** mode reports 15.116 ms / 66.15 FPS at frame 1800, versus 10.386 and 10.944 ms / 96.28 and 91.38 FPS with models off at frames 1440 and 1560. This is an observational toggle, not a controlled benchmark of two DLL versions.
- At the later stable camera, roughly 49 units from the character, model normals report 14.433–14.787 ms, whereas model-off windows are roughly 10–11 ms. Other material domains remain active. Turning AutoBump off later reduces the interval to roughly 8.7–9.1 ms.
- GPU displacement is supported but still executes **zero height-fetch draws**, builds zero meshes and retains zero geometry entries. About 301–353 model captures per frame remain. Mode 1 does not run POM. Optimizing POM or subdivision alone cannot explain or remove this normal-only cost.
- GPU GameScene is roughly 7.5–8 ms with materials versus roughly 2.5–3 ms globally off; final MaterialDetail is only about 0.177 ms. CPU capture samples are roughly 3.6–3.7 ms versus 1.6–1.8 ms. These overlapping CPU/GPU intervals must not be added.
- Bounds counters stop rescanning stable vertices once warm, but approximately 970 far draw admissions per frame still occur in the later windows. The previous cache fix addresses repeated source scans, not the cost of hundreds of native/capture transitions.

The old log has no reliable per-stage normal/POM/cavity times. Those values cannot be reconstructed from the aggregate timings. The new instrumentation supplies that missing evidence in the next user-run log.

### Changes implemented

`AutoMaterialOcclusionDistance` gives cavity and POM self-shadow one near range: Quality **8**, Balanced **6**, Performance **4**, editable from **2 to 16**. Both contributions fade over the final max(range / 4, 1 yard). Self-shadow also retains POM's own range and requires POM mode and an active directional light; displacement mode does not implicitly enable it. Normal mapping retains its independent range (48 by default).

The capture shader branches around cavity/self-shadow outside the common near range. The final compositor also avoids their sampling outside that range. Proven far draws do not bind/write the occlusion MRT. Its target is allocated and cleared only on actual near demand, and no stale occlusion texture is exposed on a frame with no successful occlusion draw. If normal gain is zero, capture follows only the effective near occlusion consumers rather than the normal range.

Buffered skinned normal/POM admission now reuses the existing conservative skin-envelope evaluator with the **current** palette. It can reject a whole far capture and its native POM replacement before normal-map generation or render-target changes. It does not skin every vertex on every draw. Rigid admission retains position-only snapshots. Unknown programs, UP skin draws, non-finite data and exhausted budgets fail open, preserving the existing shader distance checks. Consequently, unsupported far draws can still incur admission/normal replay and neutral MRT writes; there is no claim that all distant draw overhead has vanished.

Optional rigid and skin admission have independent 512/256 KiB frame-read budgets; they cannot spend displacement's 2 MiB allowance. Retained payload caps remain **32 MiB combined** (24 displacement + 4 rigid range + 4 skin range), or **8 MiB** under pressure (6 + 1 + 1). Skin envelope work retains its existing bounded cache/time policy. Separate certified constant banks preserve displacement pose reuse.

### New CPU/GPU evidence in the log

Enable the existing `Diagnostics=1` and `RenderProfile=1`. Every 127th frame records the actual material work, grouped by domain (1 terrain, 2 walls, 3 grass, 4 models) and mode (0 off, 1 normal, 2 POM, 3 displacement).

| Stage | What is measured / why it matters |
| --- | --- |
| admission | Recognition, coverage and contribution decisions; candidate/range rejection counters |
| constants | Reading current pose/material constants; detects driver-call overhead |
| snapshot | Immutable geometry reads, vertex/item counts and bytes |
| boundsPose | Conservative bounds, cached envelopes and pose fallback; scan/pose/budget counters |
| heightRead | CPU height acquisition and inference inputs |
| uv | Original UV evaluation for displacement |
| subdivide | CPU topology and mesh construction |
| geometryUpload | Private vertex/index/height upload submission on CPU |
| normalCache | Lookup, hit/miss and map acquisition |
| normalBuild | CPU setup plus a GPU interval around actual normal/height/cavity map generation |
| mips | GPU interval around actual mip generation |
| captureSetup | CPU setup and GPU command interval around capture state/target changes |
| captureDraw | Actual GPU material replay: normal/POM/cavity/self-shadow are fused inside its shader |
| captureRestore | CPU restoration and GPU command interval around restoring the main targets/state |
| pomSetup | Native POM shader preparation and binding |
| nativeDraw | Actual original/POM/displaced scene draw, also classified on sampled material-off frames |
| finalApply | Full-resolution material relighting |
| sharedPOM | Experimental UV-sharing preparation, zero when disabled |
| visibilityAudit | Explicit diagnostic cost of the small visibility probe described below |

`MATERIAL_STAGE` includes all stages, even zero-work ones. `MATERIAL_CPU` provides domain/mode rows with exclusive and inclusive times; nested CPU time is not double-counted. `MATERIAL_WORK` supplies work counters. `MATERIAL_CONFIG` and `MATERIAL_APPLY` record ranges, gains, mode and lighting context.

`MATERIAL_GPU` uses asynchronous timestamp pairs surrounding **actual work**, never diagnostic replay of the material draws. Three bounded slots and at most 4096 spans per sampled frame prevent unbounded query growth. GetData uses flags zero: no flush, spin, sleep or GPU wait. Disjoint/error/reset handling retains CPU profiling. `MATERIAL_GPU_COVERAGE` distinguishes complete and partial samples. Timings include GPU stalls/command starvation, not just shader ALU. Query creation/issue CPU overhead and cold query counts are logged separately; cold samples should not be used as steady-state frame-rate comparisons. Profiling disabled performs no clock reads or extra diagnostic draws.

The fused pixel shader cannot truthfully yield independent normal/POM/cavity/self-shadow timestamps. Compare equal-camera, warmed A/B settings using the logged configuration and visible coverage. RT transition costs can also be charged by the backend to a following draw, so inspect complete intervals as well as individual rows.

### Does the effect reach visible pixels?

`MATERIAL_PIXELS` uses four asynchronous occlusion queries on a **128 x 72 regular grid**, only on sampled profiling frames. The production depth-validity test distinguishes a valid captured material from a changed normal and non-neutral cavity/self-shadow factors. This is sampled coverage, not an exact full-resolution pixel count or proof of a large final brightness difference. The probe uses a private tiny target, restores state and is timed as `visibilityAudit`; it does not read back a 4K image or stall the render thread. Unsupported queries disable this diagnostic alone.

On the actual DXVK MSAA 2x fixture, 5173 grid points have valid capture, roughly 5165 have changed normals, 1944 have cavity attenuation and 44 have self-shadow attenuation. Off modes report zero corresponding coverage. Both effects can work while being visually subtle: cavity modifies ambient contribution, self-shadow modifies the selected directional source. A small source contribution, weak curvature, unsuitable light angle or a small near footprint can hide the change. These fixture results do **not** prove that the user's particular game scene is receiving the effects; the next game's coverage/configuration rows resolve that.

Initial comparison testing exposed a fixture error: raw DXVK had not received the renderer's AMD identity override, which its RESZ depth resolve requires. Matching the production configuration restored visible material effects. The assertions were not weakened. Native D3D9 independently confirms nonzero near effects. At a translated far view, both cavity and self-shadow produce **exactly zero RGB difference**, while normal mapping remains visible.

Demonstration images live in the same diagnostic directory. They show equal-exposure OFF/ON pairs and explicitly labelled amplified difference/attenuation masks. A synthetic coarse masonry texture with 100% strengths still produces a modest final image change. Its stored self-shadow attenuation peaks near 25%, cavity near 11%; the final image changes much less because these attenuate selected lighting terms. No artistic mockup is presented as a rendered result.

### Validation of the delivered change

The x86 DLL builds successfully. Both backends pass 56 terrain/wall/composite/grass cases covering MSAA 0/2/4/8 and allocation-pressure fallback, plus 40 M2 shader/skin/combiner cases each. The dedicated near/far and coverage test passes on both backends, including a current-palette far/near/far sequence and an actual far capture rejected without publishing material targets. CPU tests cover query failures, saturation, disjoint/reset, disabled clock cost, state restoration, constant epochs, independent budgets, settings transport and menu ABI. Two existing compiler warnings remain in shared POM initialization and height-texture indentation; no new compile errors or warnings were introduced.

### Major refactor evidence and boundary

A paired **3840 x 2160, MSAA 2x, 32-draw** fixture compares current native/capture interleaving with grouping the identical native draws before identical captures. Color, captured normal/depth and occlusion buffers match **bit-for-bit**. Three alternating rounds give median GPU intervals **7.673728 ms interleaved vs 3.712000 ms grouped**, a **51.63%** reduction for that workload (`batch-timing.json`, `batch-equivalence.json`). This supports target/pass transitions as an optimization candidate; it is not a measured game FPS improvement.

The grouping experiment does not yet include the cost of retaining arbitrary live game draw state and immutable vertex/index data. Retaining COM buffers alone is unsafe because the game can rewrite them before replay. A production deferred-capture refactor therefore remains gated on a complete packet-retention benchmark and the new live stage/coverage evidence. The experiment is retained as a reproducible test, not silently enabled in the DLL. The immediate change removes proven unused far occlusion work and makes the remaining cost attributable.

## New DLL user run at 12:52 and M2 follow-up

The latest run loads the delivered f8a9a65 DLL, SHA-256 `de66ac3dc4a586d1d2dde953f04c5fe3540b4d388b4e656dc09896b1b02de9d5`. Evidence is preserved outside Git in `northlight-diagnostics/m2-performance-20261022/user-run-new-dll-1252.log` and `new-dll-log-index.json`. Resolution is 4K, MSAA 2x, HDR enabled, DXVK 3.1.1 / RTX 4090. The same logged camera gives:

| State | Selected window end frames | Mean frame time | FPS from mean |
| --- | --- | --- | --- |
| M2 Displacement + normals only (`0/0/0/3`) | 1320–1920, six windows | 14.182 ms | 70.51 |
| All material domains disabled | 2160–2640, five windows | 9.078 ms | 110.16 |

These are observational toggles, not a controlled comparison with the previous build's different camera. CPU capture is approximately 2.1–2.3 versus 0.6–0.7 ms; GPU GameScene approximately 6.2–7.0 versus 2.1–2.9 ms. Final GPU MaterialDetail is only 0.176 ms at frame 1524. CPU/GPU timings overlap and cannot be summed. No separate normal/cavity/POM GPU timings are available in this log.

GPU displacement is supported but reports zero draws/builds/cached meshes. Around 306 model material captures still run per frame. The camera is approximately 19–20 units from the character, outside model geometry's 8-unit radius. Mode 3 does not run POM or its self-shadow; normal/cavity remains active to 48. During M2-only operation, the earlier unused object-bound scans remain zero. Enabling world displacement later reaches 108,037,640 scanned object vertices by frame 4200, exposing a separate bounds-cache churn problem.

This follow-up changes:

- Bounds retain weak source ownership, validate control-block identity against address reuse, and charge metadata rather than complete source meshes. Limits remain 8 MiB / 1024 bounds. A 32 MiB source set retains 64 bounds in less than 64 KiB and passes 640 warm lookups; an independent old/new control reproduces the old unwanted source retention.
- Generic posed-vertex fallback reuses the existing validated declaration bindings once per draw. Pose evaluation, geometry, fade and subdivision remain unchanged.
- Rigid M2 normal/POM captures use the existing conservative early distance admission. Capture follows its active consumers: normal/cavity uses normal range; self-shadow-only capture uses the smaller of normal and POM range. Native POM and displaced native geometry remain independent.
- Logs add bounds hit/miss/entry/byte counters.

A separate no-POM capture specialization was evaluated and **not retained**. Although its prefix falls from 271 to 93 static slots, five alternating complete-pass 4K/MSAA 2x rounds give medians of 0.374432 ms for the original and 0.373632 ms for the specialization (0.21%, within the run-to-run spread). This does not establish a useful speedup. An unrestricted variant also changed one native D3D9 fixture by one FP16 depth step and 0.000073 in color; retaining the original shader on displaced geometry restores exact comparisons. That guarded experiment passes 3840 paired outputs across both backends and all four MSAA settings, plus 3584 alpha/texkill cases, but adds no demonstrated performance benefit. Its source, patch and results remain outside Git in `northlight-diagnostics/m2-performance-20261022/normal-capture-experiment/`. The shipping change keeps all existing shader bytecode and interfaces unchanged; tolerances are not relaxed.

### Which distance should capture use?

Capture replays geometry into normal/depth/coverage and occlusion targets before the native color draw; final lighting consumes them later. It is not a free copy. Normal mapping tilts lighting normals; POM shifts UVs through a height-field raymarch; displacement moves real rasterized vertices; cavity modifies ambient lighting using cached inferred detail.

Capture must end when none of these captured effects contributes. The configured model geometry radius is 8 units, fading over 6–8. Normal/cavity is still visible to depth 48, fading over 36–48; POM is visible to depth 16, fading over 9.6–16 plus minification fade. Geometry uses spherical camera distance; normal/POM use view-space depth. Setting all limits to 8 would remove currently visible normal/cavity detail. No such quality setting is changed here.

GPU distance/depth rejection can occur after CPU setup and vertex work. Animated models require conservative current-pose bounds; unknown or budget-limited proofs retain rendering. Objects crossing the boundary retain their near portion. General occluded-object elimination is not guaranteed. Cached data may be shared by visible instances and is retained within budgets to avoid camera-turn rebuilds; retention is not per-frame shading. Bounds no longer retain full source payloads solely to remember six numbers.

### Reproduce this follow-up

Run `tests/test_displacement_state_reuse.py`, `tests/test_material_geometry.py` and `tests/validate_model_materials.py --backend <d3d9.dll>` on DXVK and system D3D9. Build with `python scripts/build_renderer.py`. The state regression covers effective ranges, conservative boundary crossings, source lifetime/address reuse and cache accounting. Model fixtures cover 40 shader-model/skin/combiner combinations with actual geometry, animation and binding/constant restoration. The separate experimental comparison runner and its shader sources are preserved in the diagnostic snapshot above; its results are not attributed to the shipping shader path.

The final shipping follow-up passes the CPU geometry/snapshot/upload and range/state/cache regressions, and all 40 model cases on each backend (80 total). Windows x86 builds with the same two existing warnings. All existing shader bytecode is unchanged. Tests operate offscreen; the game is not launched and the installed DLL is not replaced. A matching user game run is still required to establish its FPS effect. Shared POM remains disabled by default.

## Previous investigation at 11:02

The latest user-run log still shows a large material cost without displacement output. This code-only continuation removes unnecessary CPU work and capture setup. A shared native/capture POM implementation passes image comparisons, but complete-pass timing exposed a net regression, so it remains an explicit profiling opt-in. The game was not launched and no DLL was installed. **No new in-game FPS or 120 FPS result is claimed.**

## Fresh log evidence

The user run ends at frame 5519, with the log last written at approximately 11:02. It is preserved as `northlight-diagnostics/goal-120fps-20261022/latest-user-run-1102.log`. Selected focused windows from this run have a stable logged camera:

| State | Window end frames | Mean interval | FPS from mean interval |
| --- | --- | --- | --- |
| Materials on | 3000–3720 | 14.422 ms | 69.34 |
| Materials off | 3960, 4080 | 9.231 ms | 108.34 |
| Materials restored | 4320–4680 | 14.552 ms | 68.72 |
| Shadows/cavity off | 4920, 5040 | 14.372 ms | 69.58 |
| Shadows/cavity restored | 5280, 5400 | 14.650 ms | 68.26 |

These observational toggles implicate material capture/preparation beyond shadow/cavity taps. They do not isolate one algorithm's cost. The older DLL does not log every mode-only live edit, and CPU/GPU/query timings overlap. Exact windows and source lines are in `latest-user-material-analysis.json`.

At frame 5400, GPU height reports `supported=1 draws=0`; displacement has no builds or cached geometry. Pose-budget refusals reach 659,651, envelope hits 184,317 and fallback vertices 28,238,142. Snapshot budget refusals stopped at 11,608 after warm-up. The normal cache is stable at 85 entries / 41,899,268 bytes with 549 lifetime generations. Capture still processes 104 terrain, 47 wall and 274 model draws into 3840×2160 targets. Nearby timed CPU capture samples are 3.8–4.6 ms; GPU Scene is approximately 7 ms and Effects approximately 3 ms. Do not sum these overlapping samples.

## Five priorities and changes

1. **Bound rejected model work.** Generic skinned displacement built, scanned and retained object-space bounds before checking the pose budget, although its exact posed-vertex path never consumed those bounds. The unused work is removed. A 9000-vertex rejected draw now performs zero object-bound scans, retains no bounds and evaluates no poses. Running the new regression against the previous bounds logic fails. Exact skin envelopes, current-pose evaluation, near fade and subdivision retain their previous behavior.
2. **Reject capture with no contribution.** Move existing RGB-write/depth-write eligibility ahead of shader lookup, geometry reads and capture setup. Skip capture when normal strength, cavity and applicable parallax shadows contribute nothing, retaining native POM and actual geometry displacement. Release unused targets. All modes off makes the material pipeline inactive. This does not infer visibility for arbitrary occluded geometry.
3. **Avoid unnecessary opaque blending.** Terrain base capture and opaque model capture produce alpha exactly one. Their capture targets overwrite without blending. Fractional layers and cutouts retain their previous blend/alpha behavior. Native scene blending and MSAA stay unchanged.
4. **Share POM only when it pays.** A triangle-stamped, full-float UV cache lets the native pass reuse capture's raymarch. Image/state tests pass, but initial 4K MSAA 2x complete-pass medians worsen by approximately 32% for terrain, 30% for walls and 7.5% for composites; grass is approximately flat. After removing opaque blending, terrain remains about 15.7% slower (0.499 to 0.577 ms). Reducing the shared prefix from 185 to 172 static slots still gives 0.501 to 0.596 ms for terrain and 0.822 to 0.860 ms for composites in a subsequent three-run median. A 720p check also does not establish a win. Reuse is therefore **disabled by default**; `NORTHLIGHT_EXPERIMENTAL_SHARED_POM=1` enables profiling. These are warm standalone fixture intervals, not game frame times. Further work should reduce intermediate bandwidth or merge shading while preserving coverage, instead of enabling a measured regression.
5. **Make attribution observable.** Logs now include actual material modes, POM depth/range/steps, object-bound scan counts, early capture rejection counts and shared-POM production/consumption/cache bytes. Retain the separately validated adaptive EVENT-query waits. Wait duration is a dependency, not a removable CPU kernel.

## Shared POM contract

The existing single-sample material capture remains separate from the MSAA scene. Capture stores UV0 and a draw/triangle stamp in an RGBA32F target, plus RG32F UV1 for supported layered materials. Native point lookups reuse full-float UVs only for the matching triangle. Overlap, uncovered MSAA centers, alpha holes and erased/stale stamps run the unchanged raymarch. Original diffuse gradients and native alpha/texkill remain intact. Both passes use the same tagged vertex program and privately expanded triangle attributes; no subdivision or quality setting is reduced.

Unsupported/centroid/partial-precision UV declarations, occupied TEXCOORD6 or sampler slots, SM2 vertex programs, insufficient MRT/format/blending support, pressure, shader failure or budget exhaustion retain the original path. Generated-map identity and sampler addressing must match. Reads are limited to 512 KiB/frame, retained snapshot payload to 8 MiB, expanded/source entries to 24 MiB / 256 entries, and uploads to two builds / 2 MiB per frame. Entries used this or the previous frame are protected against churn. At 4K extra UV targets cost about 126.6 MiB, or 189.8 MiB for dual UVs; allocation is bounded to 192 MiB and requires 256 MiB virtual-address headroom. Default operation allocates none of these targets.

## Reproduction and validation

- Build Windows x86 with `python scripts/build_renderer.py`; output stays in the checkout.
- Run `python tests/test_displacement_state_reuse.py` for rejected-pose work, early capture gates, POM-only operation, exact triangle expansion and distance/epoch/source/budget/resize/lifetime contracts.
- Run `python tests/test_material_geometry.py` for subdivision, snapshots and uploads.
- Prepare client fixtures with `python tests/validate_parallax_msaa.py --backend <plain-d3d9.dll> --output-subdir shared-pom-msaa`.
- Run `python tests/validate_shared_pom.py --backend <dxvk-d3d9.dll>` for actual hit/miss masks, default-disabled behavior, shader-failure fallback, partial holes, stamp wrap, UP/indexed draws, sampler mismatch and POM-only operation. Terrain/wall/composite/grass images are compared at 1x/2x/4x/8x. The runner explicitly enables reuse.
- Use `--color-only --output-subdir shared-pom-native` on system D3D9. Production images/state are checked there; its driver rejects the separate diagnostic hit-mask shader, so direct hit/miss pixel counts are DXVK evidence only.
- Run `python tests/benchmark_shared_pom.py --backend <plain-d3d9.dll>` for capture plus native shading, including extra MRT writes, clears and state changes. The runner alternates paths and reports raw intervals and medians. Timestamp intervals can include submission starvation; neither these results nor earlier kernel-only timing establish game FPS.

Preserved evidence includes paired backend validation JSON/logs, 4K timing runs, CPU regressions, the rejected-pose negative control and build output. The earlier 56-case MSAA/pressure matrix is retained separately. Final shared-POM comparisons cover 256 color outputs per backend, plus default/failure/addressing/POM-only checks. The final model matrix covers all 40 shader/skin/combiner variants, including animation and complete vertex-constant/binding restoration. The integration PR remains draft pending a matching full-quality game run and representative visual checks, deferred under the code-only instruction.
