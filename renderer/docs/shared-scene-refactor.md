# Shared scene and geometry lifecycle refactor

Source baseline: `ac966cef51d1ce45c102e1f5787aada8a05a8832` (23 October 2026). This is an implementation report. In-game FPS and the user's next image inspection remain separate from offline measurements. The game is not launched or modified by validation.

## Implemented data flow

An immutable decoded FGS2 source bank now serves local GI preparation and directional terrain loading. The static shadow streamer can borrow an already decoded source and convert it directly to its compact representation; a miss uses its original bounded compact decoder. This avoids increasing its cold full-model decode allowance.

GI retains immutable world-space acceleration for placements and complete, contiguous, proven MCNK terrain chunks between region builds. A small region hierarchy references reusable placement/chunk BLAS. Partial, interleaved or unproven chunk membership follows the exact flat fallback. Geometry that cannot enter the optional cache remains in an admitted mandatory subset tree over the same flat compatibility scene, with original global triangle IDs. This hybrid path preserves complete crop coverage. The flat scene remains available to existing fog and upload consumers; removing that small assembly copy is not required to eliminate repeated acceleration/preparation work.

The shadow preparation cache reuses compact triangle sort ordinals, batch bounds and content records. Lightweight source revision, placement and exact selected-membership provenance supports both local geometry and directional terrain. Current global vertex/material numbering is applied when composing the upload plan, preserving baseline batch and triangle order. Cutout texture pixels remain exact. When the complete preparation layout repeats and every page is still in the active GPU key set, a metadata-only page memo skips packing and hashing. A missing proof or resident key uses ordinary preparation/upload.

World capture, deferred materials and displacement share one immutable source preparation pool. Tracking identity includes resource lifetime, write revision, draw/ranges, semantic vertex layout and representation. Each consumer retains its own capture admission policy. Model and terrain GPU caches retain a small verified source identity independently of CPU snapshot lifetime: CPU eviction/reconstruction no longer requires GPU reupload of unchanged tracked bytes. Unknown/untracked inputs retain conservative fallback. Current pose, constants, draw state and light inputs are still captured for every draw.

## Limits and lifecycle

- The former 32 MiB optional geometry allowance is partitioned into 16 MiB live shared acceleration, 8 MiB decoded sources/revision registry and 8 MiB shadow/page preparation. Active shared leases remain charged after cache reset or eviction. Mandatory flat compatibility data, fallback tree and temporary decode allocations still use geometry admission.
- Oversized or unretained decoded source leases are separately counted as mandatory transient bytes; they are not claimed to fit the optional 8 MiB source cache. No extra unaccounted persistent source pool is introduced.
- The capture pool's aggregate caps are the sum of the previous participants: 112 MiB normally and 42 MiB under the existing pressure limits (64/32 world, 24/4 deferred, 24/6 displacement). Unique payload accounting includes outstanding shared snapshots after cache eviction. Per-frame read/capture budgets remain per consumer.
- GPU resident entries retain identity metadata, GPU resources and their existing budgets/LRU/fence policy. They retain neither original game resources nor full CPU source geometry to preserve identity.
- Map changes, device reset, pressure trimming, source writes, lifetime changes and unavailable proof retain explicit invalidation/fallback paths. Region coverage, update cadence, quality settings, MSAA and draw distances are preserved. GI probe invalidation remains conservative across geometry changes because transport includes multi-bounce dependencies.
- Installed world-cache files are immutable during a play session, as assumed by the prior placement cache. A validated file revision (path, size and modification stamp checked before/after decoding) receives a monotonically unique token. An unchanged revision preserves its token across payload eviction; a changed stamp always receives a new token, even for identical bytes. No repeated full-source hash is needed. A same-size, same-timestamp external rewrite remains outside that existing immutable-session contract. Mesh page and membership identities retain their existing 128-bit content-key policy.

## Correctness repair discovered during the refactor

The old SAH traversal could prune an equal-distance candidate through floating-point box rounding and violate its documented lowest-global-triangle tie rule. Conservative box-only traversal bounds repair that case. Primitive intersection arithmetic and exact minimum/maximum distances are unchanged; alpha-invalid or farther primitives do not become valid hits. The median path retains its previous behavior. Frozen controls, an independent brute-force oracle and deliberate edge/grazing/coplanar cases distinguish this bug fix from an unexplained image difference.

## Profiling and validation

`WORLD shared scene` correlates source reads/revision reuse, prepared/reused triangles, page memo hits and memory with `buildId`. `WORLD shared acceleration` separates BLAS builds/reuse, TLAS pieces, mandatory fallback triangles and compatibility memory. `STATIC SHARED SOURCE` records shared conversions versus compact decodes. `MODEL_SOURCE_LIFETIME` and material lifetime lines correlate shared capture preparation and GPU reconstructed-identity hits with renderer frame IDs. Memory tallies expose unique shared CPU/key bytes, rather than summing the same payload across consumers.

Validation includes a frozen-source whole-pipeline comparison on seven real world regions (cold, repeated center, four orbit directions and return), actual static compact conversion, exact source/ray/probe/plan/page/signature checks, allocation failure and held-generation tests, tracked writes/lifetime ABA/reset/pressure cases, and genuine-baseline GPU material/MSAA/render-state comparisons. Windows debug/optimized checks are not ASan/UBSan certification.

## Final measurements

The standalone production-code comparison uses the frozen `ac966ce` source closure and actual Northrend world-cache data. Three alternating baseline/candidate rounds each cover seven regions: cold center, repeated center, four 32-unit offsets and return. Each execution includes the same 5,600-probe, 64-ray, three-bounce production bake and seed. An additional 8,192 random trace/occlusion queries per region are timed separately. Reported values are medians; the warm aggregate includes the repeated region and orbit/return executions.

| Preparation plus GI bake | Baseline ms | Candidate ms | Time reduction |
| --- | ---: | ---: | ---: |
| Cold | 360.329 | 398.504 | -10.59% |
| Same region | 371.212 | 253.883 | +31.61% |
| Orbit and return | 371.097 | 359.480 | +3.13% |
| Warm aggregate | 371.155 | 348.968 | +5.98% |

These are CPU worker measurements, not whole-game frame timings or measured FPS uplift. In particular, a 6% reduction in this worker cannot be converted into 6% higher FPS. The user still needs a comparable run of the delivered DLL.

Preparation alone improves 9.29% during orbit/return; shadow planning improves 22.15%. Repeated-region page preparation falls from 44.746 to 0.056 ms when the full proof and resident keys are available. Separately, the frozen three-consumer capture fixture halves cold source lock/read work (56,622,592 to 28,311,296 bytes), then removes return-orbit rereads in its retained working set. These component savings must not be added to the combined CPU percentage.

There are material remaining costs: the cold combined path is 10.59% slower, warm GI bake alone is 13.84% slower and the separate random ray batch is about 30.5% slower. Shared-tree traversal costs partly offset preparation reuse. This establishes a useful reuse foundation with a modest net orbit gain; it does not establish that rotation stutter is solved. The next target is the shared traversal/TLAS cost and unchanged terrain decode/assembly work, using the new per-build counters to decide which matters in the game. Producer-empty replay queue time from the previous run also remains a separate possible limiter.

## Release validation and handoff

- All 21 region executions match nine canonical outputs against the frozen baseline, including exact flat scene, plan, page signatures, ray results and the full production probe/response bake hash.
- Four GI suites pass optimized and debug runs: real crops, edge/tie/brute-force controls, held generations, concurrent readers, 600 allocation-fault sites and chunk/admission fallback. The old equal-distance SAH pruning bug is documented above.
- Shadow preparation/page tests cover 106,705 exact assertions, six actual compact static-source conversions, true-LRU/preflight order and held-page memory. Optional failures retain the original exact path.
- Shared capture lifecycle and existing snapshot/replay/terrain suites pass optimized/debug runs; prediction differential checks cover 254,180 reads per build across three lanes plus 41,600 changed key bytes. No sanitizer certification is claimed on Windows.
- The genuine-baseline GPU matrix passes all 140 material/capture cases, including MSAA 0/4/8. Terrain layout passes 96 exact pixel comparisons across MSAA 0/2/4/8 and directional/cached/point shadow programs. Shader sources and embedded arrays are unchanged.
- The x86 PE32 release builds successfully and retains the original 17 exported entry points. The native source-bank, geometry handoff, concurrent worker and expiry regressions pass. Two existing compiler warnings remain; there are no build errors.

DLL: `5,586,944` bytes; SHA-256 `5cc0b3fc5ece8f2400fef27eefb5349a85b9289d2e4150263310f8b466fb6d4d`.


The separate package contains `d3d9.dll`, matching existing addon files, reports and machine-readable evidence. No installed DLL, configuration, addon or world-cache asset was replaced, and the game was never launched. Water/SSR remains withdrawn. The installed log baseline is still the user's earlier `31d20e1` DLL, so it cannot measure this new code's FPS.

Evidence lives in `northlight-diagnostics/shared-scene-refactor-20261023`: `dll-validation.json`, `pipeline-release-final/validation.json`, the three component reports/manifests, allocation/lifetime logs and GPU logs. Exploratory failed attempts are preserved separately and excluded from the final measurements.
