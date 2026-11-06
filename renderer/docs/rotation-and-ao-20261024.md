# Northlight rotation, material capture and AO: 24 October 2026

This iteration preserves the existing integration work, includes upstream PR #89, reduces terrain and material preparation allocations, and adds opt-in AO quality levels for #105. Three agents audited five directions each. The latest runtime evidence still identifies queue-supply gaps and outstanding native work as major hitch locations. This build does not claim to eliminate camera hitches or establish a whole-game FPS uplift.

One consolidated final acceptance phase covered the adopted changes. Failed attempts and the rejected #106 actor-AO candidate remain recorded; acceptance criteria were not relaxed. No intermediate optimization benchmark was run. The game and `wow_optimize` were not launched, stopped, modified or installed by this task.

## Runtime identity and measurement limits

Both user runs examined here used the previous source `2edeb0c`, with startup source fingerprint `b9d53b5efbf996c36ac3c1e80fff7c2ff4d49c9fe74c25db15df3aa480c2ce84`. The installed DLL SHA-256 is `9bc13ef28e52d1331b58bea381533444becbcf67b24806ef9e3bbd33b94b6f2d`. Neither run validates the new `1fda027` changes. The backend is DXVK 3.1.1 at 3840 x 2160 with MSAA 2x. Device-name/vendor fields disagree, so a physical GPU model is not inferred.

The earlier 00:13 run includes materials-OFF samples at renderer frames 0-2413 and ON samples at 2540-3302. The transition is not precisely captured. The later user run ended at 01:06, before final shader compilation began at 01:25. Its sampled settings select mode 3 for all material domains, and its camera travels hundreds of yards. Different settings, motion and residency prevent a controlled cross-run FPS comparison.

CPU frame time, worker duration, GPU timestamps, command replay wall time, queue idle and query spans are different domains. Containing or overlapping intervals must not be added. Conditional stage medians describe different sample populations and cannot be summed into a synthetic frame. Selected slow-frame rows are not an unbiased frame-time distribution.

## What the latest 01:06 log establishes

| Observation | Recorded evidence | Implication |
|---|---|---|
| Largest selected stalls include queue-supply gaps | Frame 17149: 84.833 ms total, 81.179 ms scene, 76.145 ms scene queue idle. Frame 16690: 77.351 ms total, 66.238 ms idle. | The consumer is waiting for commands. This does not identify the producer's blocking call or prove that a GPU effect caused the wait. |
| Some stalls instead accumulate in the tail | Frame 11681: 40.942 ms tail and 40.615 ms query span, while actual GetData wall time is 0.073 ms. Frame 4309: 33.041 ms tail and 32.585 ms query span. | Outstanding EVENT/native work needs further attribution. Coincidence does not prove a particular GPU throttling policy. |
| Express readiness is working | Final counters: 35,744 posts, 35,744 hits, zero fallbacks. | The residual stalls are not explained by missed express-readiness fallbacks. Required ordering must remain intact. |
| Busy scenes remain draw-heavy | Matched frame 10160: 10.1613 ms across 2,380 indexed commands, within 11.6264 ms replay; GPU scene 12.332 ms. | Forwarded native draws and injected work share these intervals. They are not isolated material or shader costs. |
| Ordinary effects are secondary to the scene in this sample set | GPU GameScene median 4.735 ms; Effects 1.386 ms; AO 0.162 ms; AO blur 0.220 ms, across 151 matched samples. | These medians do not explain the full 40-85 ms selected hitches. Conditional cloud/fog/composite populations must remain separate. |
| Material preparation is still meaningful | Sampled CPU-exclusive median 2.72945 ms, including original native draws. Positive pose fallback median 0.4991 ms, maximum 2.9972 ms; snapshot median 0.18535 ms, maximum 2.1471 ms. | Current-pose and cold preparation remain targets, but cannot account for all queue-idle time. |
| Memory pressure disables capture batching | Pressure starts at frames 4370 and 11721, with recovery at 7493. In pressure windows replay GPU capacity is zero and queued/flushed captures are zero while immediate capture continues. | The accepted replay-bind optimization cannot help those windows. A bounded immediate-capture batching design is a next investigation; raising memory caps is not justified. |
| Normals are predominantly cached | Final totals: 2,735,368 hits, 9,754 generated, 9,659 evicted, 32 MiB budget. Normal-cache CPU median 0.0123 ms. Positive per-domain GPU build/mip medians 0.009216/0.031744 ms. | Continuous uncached generation is not supported. Pressure-driven churn deserves separate reuse-distance evidence. |
| Bump/cavity and GPU displacement have activity | Frame 12573: 3,546 changed normals, 301 cavity pixels, 905 pixels inside occlusion range. Self-shadow is zero in mode 3. Lifetime GPU displacement counters: support 1, 124 builds, 40,244 draws. | These paths have observable activity. Lifetime totals are not per-frame cost; mode 3's zero self-shadow is expected. No new image-quality claim follows from counters alone. |
| Terrain capture is now a secondary observed cost | 73 samples: median 0.113 ms, maximum 1.134 ms; median misses/evictions zero. The slowest sample has zero capacity misses/evictions, 567 exact reuses and 568 vertex revisions. | This run does not reproduce the earlier 5-8 ms terrain-cache churn storm. The previous shipped sharing is still useful. |
| GI refresh is delayed under worker memory pressure | 14 geometry publications: median build 500 ms, maximum 766 ms. Final admission episode: 108,594 ms, 684 attempts, ending superseded. | This is background refresh delay, not a 108-second rendered frame. Camera travel and request changes prevent a stationary-turn rebuild-thrashing conclusion. |

The earlier 00:13 run separately exposed terrain byte-capacity churn: OFF frame 1016 took 5.897 ms with 383 misses/378 evictions; ON frame 2794 took 8.152 ms with 494 misses and only six revision changes. Stable captures were approximately 0.31-0.37 ms. That evidence motivated the accepted source-offset retention change. It must not be substituted for the latest run's much smaller capture timings.

Existing distances remain 48 yards for normals, 16 yards for POM, and a shared 8 yards for cavity/self-shadow. No range or sampling reduction is silently applied. In the earlier run, selected mode 3 produced no actual displaced geometry activity; the later run does. The earlier single 688 ms geometry publication likewise applies only to that run, not the later 14-publication workload.

## Fifteen directions reviewed by three agents

These are separate areas, ranked within each agent's latest review. Accepted changes also use the earlier churn evidence and production-source invariants. Deferred ideas are not shipped or claimed as measured improvements.

| ID | Direction | Decision and evidence |
|---|---|---|
| G1 | GI worker memory admission | Keep the current safety threshold. The latest 108,594 ms episode is a refresh delay under insufficient virtual memory, not a frame. Next instrumentation should distinguish admission, cancellation and applicable retained geometry. |
| G2 | Travel rebuild coalescing | Fourteen publications follow substantial travel; some build IDs lag newer requests. Preserve applicable geometry and coalesce stale work before considering another BVH rewrite. No unproven rebuild-policy change ships. |
| G3 | Local construction and shadow preparation | Worker medians: local 180.948 ms, shadow planning 132.569 ms, terrain loading 90.358 ms, pages 90.087 ms. These overlap worker scopes, not main-frame costs. Preparation reuse is a bounded next direction. |
| G4 | Prepared/page reuse attribution | Build 24 has 1,875 prepared hits/311 misses and 1.29 million reused triangles; other jobs lose reuse, and all 14 report zero page-memo hits. Attribute pressure, provenance and layouts before declaring a cache defect. |
| G5 | Terrain representation and coverage allocation | Adopt exact source-offset sharing inside the existing raw-index owner and eight inline coverage cells with complete fallback. Keep 32/16 MiB limits, geometry proofs and current-revision validation. The latest terrain timings lower its next-iteration priority. |
| M1 | Capture batching under pressure | Pressure sets replay capacity to zero. Investigate bounded immediate-capture batching without raising caps or changing draw order; this requires a new acceptance phase and is not in this build. |
| M2 | Repeated current-pose preparation | Positive medians: fallback 0.4991 ms, envelope 0.2906 ms, bounds 0.27845 ms. Fresh palettes and conservative bounds remain mandatory. Do not revive the rejected one-bone metadata cache. |
| M3 | Snapshot and capture setup | Adopt allocation-free tracked lookup and proof-based replay-local redundant bind elision. Next consider duplicate preparation and copying only proven live constants; unsupported relative reads retain conservative handling. |
| M4 | Normal-cache churn | Reuse is dominant; generation is small. Inspect eviction/reuse distance under pressure rather than introduce another normal-map cache or MPQ rewrite. |
| M5 | Actual near-effect and displacement coverage | Counters prove normal/cavity/displacement activity in the later run. Further GPU work should isolate actual capture/draw coverage and first-use shader tails, retaining the 48/16/8-yard limits and output quality. |
| A1 | Producer/queue-supply gaps | Highest latest hitch location. Preserve ordering and identify the producer's blocking site; consumer idle alone is insufficient attribution. No arbitrary query bypass ships. |
| A2 | Native/injected draw throughput | Dense indexed replay remains busy. Separate original forwarding, hook and API work before batching across observable state boundaries. No broad draw-state rewrite ships. |
| A3 | Residual EVENT/tail synchronization | All readiness posts hit. Tail/query-span coincidence remains an outstanding-work clue, not proof of a particular throttle. Keep generation and reuse rules intact. |
| A4 | AO/fog/post cost and requested issues | Include PR #89 and opt-in AO quality for #105. High remains exact. Reject the #106 actor-mask prototype after quality/cost failures; no reduced-quality default or unproven warmup fix ships. |
| A5 | Episodic state/API and profiling pressure | One active terrain sample has 4,201 native-counted calls, 700 Get requests but one native Get; later samples are zero. Draw-hook median 0.077 ms/max 0.374 ms. Counts do not establish dominant CPU time. Narrow RESZ/profiler proposals remain parked. |

## Adopted implementation

Terrain coverage keeps up to eight visited cells inline and falls back to the original complete set for larger/disjoint draws. Exact immutable source-offset sequences share storage through the existing raw-index owner, with no separate pool or per-sequence table. Width, count and every value must match. Unknown owners, unique sequences and mismatches retain owned storage. Larger wrappers, retained capacities, escaped lifetimes and recharge are accounted against existing limits. Vertex stride/base, ordered XYZ, winding, coverage holes and provenance remain unchanged.

Tracked snapshot lookup uses bounded stack identity words instead of allocating a temporary vector/shared key. Persisted key serialization, capacity, complete FNV equality, collision checks, source revisions and provenance are unchanged.

Resident material replay suppresses repeated viewport, frequency and MRT1 binds only inside a proven uninterrupted replay. The first bind remains; UP fallback invalidates assumptions; MRT1 elision requires a proven default full viewport with depth 0..1. Partitioned/nondefault depth viewports retain conservative behavior. Draw order, MSAA and failure restoration remain covered. The implementation file lives under `src/water/auto_bump_renderer.h`, but this change concerns materials and adds no liquid/SSR optimization work.

## AO issues #105 and #106

[Issue #105](https://github.com/lepola/northlight/issues/105) is addressed with `AOQuality=High/Medium/Low` (numeric 0/1/2), a live menu enum and transactional persistence. High remains the default in every preset. Its original shader bodies and seven approved bytecode hashes remain unchanged. SSAO ignores quality and does not recreate its shader for a quality-only change. GTAO uses 4x4/3x4/2x4, HBAO+ 8x4/6x3/4x4 and HDAO 16/8/8. Medium/Low can add noise or shimmer and require an explicit choice. Personal INIs are not changed.

[Issue #106](https://github.com/lepola/northlight/issues/106) remains unresolved. Its actor-specific MRT prototype was implemented, evaluated in final acceptance, then removed. The frozen oracle exercised 214 synthetic scenes plus 45 packed-depth/partial/stale provenance controls. There were 85 scene/control failures and a separate 4K kernel-cost failure. A GTAO High contact sample (camera 3, radius 4) changed from 0.20719 to 0.08917, violating the preservation criterion. Some halo improvements did not compensate for contact/structural shading failures.

At 3840 x 2160, warmed AO-kernel median changed from 0.944512 to 1.703616 ms: **+0.759104 ms**, above the pixel-scaled 0.429722 ms acceptance limit. At 1728 x 1117 the delta was +0.051520 ms. These measurements exclude mask capture, clear, MSAA resolve and classification; neither is a complete feature cost or game-FPS measurement.

The separate actual-helper state stand also failed before completing its MSAA/cost matrix. The first attempt used the wrong 1x DXVK depth-copy destination; after matching production DF24/INTZ selection, another `D3DERR_INVALIDCALL` remained. Sloped MSAA depth matching also remained an unresolved coverage risk. No complete actor-MRT state, image, MSAA or total-cost pass is claimed. Thresholds were not relaxed.

All 67 pending candidate files were preserved exactly with hashes and a binary working patch before removal. A removal manifest proves unrelated files unchanged. The accepted source contains no actor-mask targets, patches, draws, telemetry or new optional 128 MiB allocation. Seven dedicated programs and two tests belonging only to that rejected path were removed. A replacement must preserve contact and structural AO, prove useful real skinned/MSAA coverage, and pass complete cost acceptance before adoption.

The investigation used [Microsoft's D3D9 MRT contract](https://learn.microsoft.com/en-us/windows/win32/direct3d9/multiple-render-targets) for capability, format, write-mask and antialiasing restrictions. Target creation alone is not sufficient validation.

## Upstream integration and preservation

[PR #89](https://github.com/lepola/northlight/pull/89) merged upstream on 23 October at 22:48 UTC. Main is included through `fb568b4b31a667e2aa590b8ecd7e794351ee5e77`, via integration merge `17ae4b32f800f5a793de8f479f25aed1b59d80ba`. Much of PR #89's original performance work was already in this branch; inclusion does not imply another blanket speedup.

Four conflicts were resolved while retaining deferred AO logging, renderer-frame diagnostics and the existing late-water counter, and importing unmeasured-CPU `-1` semantics plus current fog/compiler/test fixes. The performance fixture removes unreachable branches but retains all 12 active fog/composite comparisons.

Before the merge, all 31 pending files were copied with hashes and preserved in durable stash `de2aa2c5638afd99a2faa4a4be12e5acd4254171`, retained by `safety/rotation-working-before-pr89-20261024`. After restoration, 30 files matched original bytes exactly and `renderer.cpp` matched an independently reconstructed three-way result. The base safety branch is `safety/rotation-base-before-pr89-20261024`. Both the old branch head and upstream main are ancestors of the result; no unmerged entries remain. No history rewrite is used.

## Final adopted validation

**68 distinct selected automatic scripts pass: 38 native and 30 Windows.** This is not a claim that all 304 discovered scripts ran. Coverage includes genuine ASan/UBSan/TSan command-stream controls; allocation, provenance, lifetime and accounting failures; material state/order comparisons; Lua 5.1 draft/Cancel/Apply persistence; the original 99 AO cases; 99 cases each for Medium/Low; and 48 grazing cases for each quality. All 16 adopted effects shaders were rebuilt with the pinned Microsoft compiler. Seven High hashes are unchanged, and current world bytecode provenance is required rather than accepting `compile_pending`.

The coverage oracle passes 4,351 exact checks in optimized, unoptimized and sanitizer modes. A common one-cell rebuild performs one allocation versus three in the frozen reference. Snapshot lookup passes 118 serialization/collision/capacity/provenance cases in all three modes with zero temporary lookup allocations. Material replay compares 24 state/order/failure cases in optimized/unoptimized modes against an unelided production-source control and rejects two deliberately unsafe variants.

The existing system-D3D9 material stand passes single-sample and MSAA 2x/4x/8x plus 120 pressure/recovery frames. It checks state, resource admission and draw behavior; it is not exhaustive per-sample color/depth readback. Twelve paired fog/composite GPU cases pass the existing `1e-5` pixel-error bound. Historical contact-normal capture inputs remain unavailable; no baseline/checksum was fabricated.

Initial failures are retained. One fixture omitted a valid fourth shared view created by a base offset; another changed the version provider, unintentionally clearing the unknown-owner cache. Draw-gate extraction included unrelated rejected actor code. The menu fixture incorrectly required an undocumented final newline. The fixes retain geometry, fallback, full-equality, lifetime, caps and exact UI-field assertions. Failed or source-affected gates were rerun to PASS; unrelated passing gates were not repeatedly benchmarked.

## MPQ-backed final regression

One baseline/candidate pair uses the complete 36 ADT / 4,608 draw fixture, checking all 40 output hashes and recorded 00:13 camera headings. Eight heading sectors are complete with no missing tiles; the 16-sector representation lacks sector 5 and is not claimed. Four cycles cover stable resources, vertex revisions, vertex+index revisions and changed source layouts at 32/16 MiB caps. Ordered triangle XYZ, complete coverage, current-input proofs and all eight mutation controls pass. Historical captured revisions may differ after exact revalidation; current-input proofs are checked independently.

The table gives aggregate warm capture CPU over 11,284 calls per cell. **A single pair is descriptive, not a statistically controlled timing/FPS result.** Some cells are slower. Allocation counts cover successful `new`/`new[]` calls inside `readMeshShared` only.

| Cache | Inputs | CPU baseline -> adopted (ms) | Misses baseline -> adopted | Allocations baseline -> adopted | Peak physical offsets baseline -> adopted (bytes) |
|---|---|---:|---:|---:|---:|
| 32 MiB | stable | 4.0921 -> 2.7062 | 0 -> 0 | 0 -> 0 | 1,311,468 -> 8,788 |
| 32 MiB | vertex revisions | 21.0421 -> 20.8852 | 0 -> 0 | 0 -> 0 | 1,311,468 -> 8,788 |
| 32 MiB | vertex+index revisions | 29.5930 -> 39.3142 | 0 -> 0 | 0 -> 0 | 1,311,468 -> 8,788 |
| 32 MiB | changed layouts | 53.0783 -> 52.2789 | 0 -> 0 | 0 -> 0 | 2,615,976 -> 17,576 |
| 16 MiB after pressure | stable | 3.5305 -> 3.4987 | 0 -> 0 | 0 -> 0 | 1,311,468 -> 8,788 |
| 16 MiB after pressure | vertex revisions | 20.8268 -> 24.4370 | 0 -> 0 | 0 -> 0 | 1,311,468 -> 8,788 |
| 16 MiB after pressure | vertex+index revisions | 50.3379 -> 52.4690 | 0 -> 0 | 0 -> 0 | 1,311,468 -> 8,788 |
| 16 MiB after pressure | changed layouts | 216.3159 -> 173.7916 | 8,392 -> 7,774 | 109,192 -> 70,108 | 1,954,664 -> 20,476 |

The fitting stable cell retains 9,994,056 bytes instead of 11,296,116, with 2,262 shared views. The 16 MiB changed-layout cell reduces misses from 8,392 to 7,774 and allocations from 109,192 to 70,108. These deterministic representation/counter results support the cache change, while the mixed timings rule out a blanket CPU improvement claim.

The fixture uses real MPQ geometry and recorded headings, but its cone, draw list and rebased layouts are reconstructed. It does not reproduce the game's native producer stalls. A runner preflight attempted to create shared TEMP/TMP twice; this was corrected before any workload, and the successful run uses a new evidence directory. Test outputs and caches are isolated from installed game data.

## Build, delivery and protected inputs

The x86 DLL was built successfully from **`1fda0271917e83b41e7c1bcf0214893a3fb5cfea`** with Zig 0.15.2. Source hashes were unchanged across the build. Generated output was proved equivalent to committed bytes apart from line endings before exact committed bytes were restored. Two existing misleading-indentation warnings in unchanged displacement code remain; the build succeeds. PE/PDB GUID and age match exactly.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `d3d9.dll` | 5,681,664 | `c1054fe6298cb2a99cbb6a0697e34a0947a8a196276c4228f3d27c34af50c30e` |
| `frd9.pdb` | 46,161,920 | `e37f79ec4519511a2f5456dd91b69ff477822a3a23c79775fe22813e6cc11b47` |

The new startup `BUILD` source identity is **`262d3f27588e572ad899b8b843140717352ff08b82ae326b21f8e34cd3124074`**. A later report-only publication commit does not change the built source.

The separate package is `northlight-builds/northlight-rotation-and-ao-1fda027-20261024`. It includes the DLL, matching PDB, report, hash manifest and complete NorthlightSettings addon. Manual DLL replacement is sufficient for existing controls; the new AO-quality menu also requires the supplied addon. High remains default. Nothing is automatically installed.

The input guard found nine controlled installed/settings files unchanged, including installed renderer/optimizer binaries, optimizer configuration and Northlight INIs. `WTF/Config.wtf` independently changed at 01:06:48, matching the user's later game run and preceding the first final compilation at 01:25:02. Its new bytes are frozen and preserved; this task did not write or restore it. The initial raw guard failure and the narrowly evidenced exception remain recorded. Private MPQs, world cache and the previous delivery package are retained.

Local evidence is under `northlight-diagnostics/rotation-next-20261024-0013`: `fresh-0106/records.json`, `fresh-0106/analysis.json`, `pr89-preservation.json`, `adopted-public-validation.json`, `adopted-ao-methods-gpu-validation.json`, `final-shaders-adopted/validation.json`, `gi/final-mpq-run/validation.json`, `final-gpu/validation.json`, `build-final/validation.json`, `build-final/pdb-match.json`, and `untouched-validation.json`. Rejected #106 inputs and failures are retained under `before-ao-adoption` and the actor candidate acceptance directories. These local records are separate from the Git source and do not substitute for game validation of the new DLL.
