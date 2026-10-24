# Northlight render pipeline: systemic findings and water/SSR withdrawal

## Implemented shared scene lifecycle refactor — 23 October 2026

The previously proposed refactor is now implemented: one bounded decoded-source bank, reusable placement/terrain-chunk acceleration, reusable shadow preparation/page metadata, and shared capture source identity that survives CPU owner replacement. Current pose, complete geometry/alpha coverage, MSAA, ranges and aggregate cache limits are preserved. Water/SSR remains withdrawn.

Three alternating CPU comparison rounds against `ac966ce` include actual world regions and the full 5,600-probe, 64-ray, three-bounce GI bake. Preparation plus bake takes 5.98% less time in the warm aggregate, 3.13% less during orbit/return and 31.61% less for the same region. Cold work takes 10.59% longer; warm bake alone takes 13.84% longer. These worker results are not a game-FPS measurement. Capture independently halves cold source reads in the frozen three-consumer fixture and removes retained return-orbit rereads.

All 21 whole-pipeline executions match canonical scene, plan, page, ray and full bake outputs; 140 material GPU cases and 96 terrain shadow layout comparisons pass with MSAA. Optimized/debug lifecycle, allocation-failure, admission, concurrency and regression checks pass. A separate release DLL is built; installed files/settings and the game remain untouched.

Full implementation, limits, costs and validation: [shared scene refactor](shared-scene-refactor.md).

Client run ending 23 October 2026 at 01:23 local time. Installed baseline: `31d20e182fd9f4a849df90aed85e773e3800a8d0`, DLL SHA-256 `962fc0df155949bcbf5b480957e8ca9e69564e984f4521e8637995704996ea66`. Fresh log: `ccfdb6125a498d7fd877235fb4ca6c12a9eeddc2fb560eaf26ec88564d25cb88`; genuine preceding log: `abedd725589925ae40468c3dc7b8d55db8ac8d7231bee4dc28c787160791687d`. The game was not launched by the investigation; installed DLL, settings and assets remain untouched.

The evidence separates three problems: occasional CPU geometry replacement spikes, repeated preparation of largely shared world regions, and long periods when the replay thread has no commands from the game. Steady material GPU capture is an additional real cost when enabled. A single shader optimization or another larger cache cannot solve all four.

This build removes our water beauty/SSR implementation, repairs unrelated GI invalidation on settings changes and further reduces exact single-bone bounds CPU work. It preserves native liquid masks, independent precipitation, UI/HDR repairs, material quality, MSAA, geometry coverage and existing memory budgets. It does **not** claim that sustained camera-rotation stutter is fully solved.

## What the new run proves

Material configurations are On at sampled frames 0–2921 (24 samples), Off at 3048–8509 (44), then On at 8636 (one). Eleven early On samples contain no material work; later On samples are real world rendering. Resident intervals 1397–1778 and 2286–2921 are separated from source/residency churn at 1905–2159. Saved settings match the preceding run, but visible sets and camera paths are not a controlled A/B.

| Observation | Evidence | Implication |
|---|---|---|
| Previous cache fix is active | Final 4918 entries, all 44 Off samples have zero count-pressure eviction; compact offsets avoid 2.173 MiB and shared source indices avoid 9.145 MiB | The old 4096-entry bottleneck is gone. |
| Byte pressure still causes large capture spikes | Off 3048: terrain 12.337 ms, 534 misses, 538 byte-pressure evictions, 6,139,140 B reread; median Off terrain is 0.303 ms | Geometry replacement and subsequent uploads are intermittent, not unconditional per-frame rebuilds. |
| Material Off works | All 44 Off pose/map/snapshot/capture/apply stages and cold builds are zero; native query profiling median 0.0001 ms | Shared world/GI work remains because it has independent consumers. |
| Previous pose specialization is used | Steady fallback is almost entirely single-bone, approximately 0.164–0.170 ms; churn frame 2032 includes 613 generic vertices | Exact CPU specialization helps the observed route, but does not remove GPU capture. |
| Visible material work remains measurable | Resident2286–2921 extra deferred capture GPU median 1.11309 ms plus apply 0.14080 ms; normal builds zero | Repeated normal-map generation is not the main steady-state cost. |
| Static region preparation repeats | 17 Off worker builds, median 391 ms; median local 66.597 / terrain 100.027 / shadow-plan 151.560 / pages 70.130 ms | The pipeline repeatedly reconstructs much of a scene that is already shared. These are background job times, not per-frame stalls to add together. |
| One large Off hitch is producer starvation | Frame 3209: total254.289 ms, scene233.180 ms, scene queue idle230.259 ms, effects CPU0.451 ms, query wall0.010 ms | The replay thread mostly had no commands. This does not establish whether the game was computing, loading, descheduled or waiting elsewhere. |

GPU timestamps include command-stream gaps and resource dependencies. CPU, GPU, worker and query intervals overlap and must not be summed into an FPS prediction. The retained slow-frame ring samples the slowest frames in each window; it is not a complete frame distribution. `FRAME interval` windows themselves vary from about119 FPS to47–60 FPS during the later Off section, so the problem is still broader than materials.

## Water and SSR: removed from our implementation

[PR #93](https://github.com/lepola/northlight/pull/93) is closed at the user's request. Its branches and historical commits remain recoverable; the integration removes the unwanted behavior in a new explicit commit, preserving unrelated fixes. The withdrawal removes water material detection, forced material-mask redraw, beauty/refraction/glints, SSR tracing, depth hierarchy, scene/depth copies, reflection targets, water diagnostics, water-only shader artifacts and their tests. All eleven Water/SSR settings and menu controls are removed. Old INI entries are retained on disk and recognized as retired, with no rendering effect.

The native liquid-mask renderer is restored byte-for-byte to its pre-feature implementation. It identifies liquid pixels and surface distance so AO/relighting avoid treating them as solid ground and fog stops at the water surface rather than the submerged bed. It also supplies existing celestial receiver logic. This is a dependency of the remaining renderer, independent of enhanced water appearance. Native MRT and its necessary fallback remain.

The removal has a concrete avoidable-work benefit: in the Off window `Water=0` and `WaterSSR=0`, yet the material detector bypassed normal planning and forced native-mask redraws, averaging 19.18 and peaking 43 per sampled frame, with MRT0. That unconditional detector/forced path is removed. WaterMaterial/WaterSSR GPU marks themselves were zero in those Off samples, so their removal cannot be credited with eliminating the observed Off stutter. Native fallback can still redraw water when MRT is unavailable; the next client log must establish its actual new rate.

The mixed original PR also contained sheltered precipitation and general disabled-depth UI fixes. Those independent features remain. The surviving precipitation shaders are byte-identical to baseline; 2304 GPU cases cover rain/snow, zero amount, foreground occlusion, odd/even grids, UNORM/FP16/FP32 targets and MSAA 0/2/4/8. Native water-mask wiring/geometry and UI ordering checks also pass. One Unix-oriented fake-Windows mask-state harness remains unsupported on this Windows host; that limitation is recorded rather than presented as a pass.

## Shared invalidation: implemented now

Previously every `applyLiveSettings` call set `settingsRequestDirty=true`. A material, HDR, AO, fog or display-only celestial edit could therefore increment GI request IDs and cancel or republish work even when its input was unchanged.

The new shared dependency gate requests GI work only when one of its six live quality inputs changes: GI, bounce count, dynamic probes, probe movement step, probe look-ahead or shadow-direction quantization. It also compares the exact celestial light hue/mix/strength fields across fallback and the union of zone keys. Equal-fallback zone insertion/removal and explicit-key metadata alone do not invalidate. Pending dirtiness is preserved with OR; true GI reactivation, actor removal and current camera/map/light triggers remain. Display/resource settings still apply normally.

Optimized/debug tests independently classify all 88 quality keys, exercise 76 profile-field cases,1000 no-op/HDR applications with pending-dirty preservation, equal-fallback insertion/removal and GI/reactivation/dynamic-actor cases. Existing1000-case live resource/budget tests also pass in both builds. This is an exact invalidation fix for settings changes; it is not advertised as the explanation for uninterrupted Off rotation.

## Exact current-pose preparation: implemented now

For the complete audited single-bone kernel, material fallback now decodes the two used float4 inputs instead of clearing sixteen registers per vertex, and resolves shader-local c0 once for the current immutable draw. Palette reads, ordered DEF overrides, operation order, address rounding, failed-output/partial-bounds behavior, generic/Strict/envelope paths and the 8192-vertex budget remain unchanged. No pose cache or extra retained memory is added.

All twelve complete decode-and-bounds benchmark cases improve 8.07–9.70%; the 8192-vertex cases save approximately 0.0135–0.0182 ms. Seven standalone controls are reported separately, with the largest positive difference0.000813 ms; noise/improvement in controls is not claimed as a benefit. Each optimized/debug build passes714752 old/new/oracle cases,133632 prepared/alias cases and 6936 bounds/malformed cases. Existing material state, deformation, actor-shadow and GI actor-scene regressions pass. The benefit is modest in whole-frame terms; GPU capture and geometry lifetime churn remain larger targets.

## Comprehensive architecture: the next substantial change

The existing pipeline has several independent ownership domains: captured CPU snapshots, immutable actor data, GPU material residents, terrain arenas, static region BVHs, shadow plans/pages and GI probe publications. A cache eviction or a different eye-centered crop can create new owners even when much underlying geometry is unchanged. Rebuilding then discovering page equality is expensive; the final region already reuses54 of96 pages, but only after another plan/build cycle.

The proposed solution is **one shared immutable scene representation with consumer-specific views**, introduced in measurable stages:

1. **Separate data identity, view/request identity and GPU residency.** Establish exact lifetime/revision/layout/range tokens for captured data and immutable tile/model/placement/material identities for cached world data. Attribute unique bytes, new owners, preparation, uploads and consumer references. Unknown revisions retain live validation. A request ID or pointer alone must not stand in for geometry identity.
2. **Prepare immutable placement/chunk data once.** Reuse existing placement pieces and content-keyed pages, including their exact transformed positions, triangle order, UVs, normals, alpha/material rules and bounds. Keep world-space geometry and per-piece acceleration structures within current admission limits. Boundary pieces without an exact complete-coverage proof retain the present crop/build path.
3. **Represent a camera region as membership and a small top-level structure.** Reuse unchanged per-piece acceleration structures (BLAS) and pages; update the top-level structure (TLAS), membership, batches and genuinely new pages. Compare the accepted triangle set and tie precedence against the old flattened region. Preserve two-generation ownership, in-flight event fences, map/reset/teleport behavior and exact fallbacks.
4. **Share preparation between terrain capture, shadows, GI and materials.** Consumers request the geometry attributes/results they actually need from the same validated immutable source. CPU snapshot eviction should not automatically force identical GPU data to be prepared again when its content/lifetime proof remains valid. Unique-byte accounting prevents shared references from bypassing budgets.
5. **Give GI a proven transport identity.** Reuse cannot depend only on a BVH pointer, a probe location or a nearby box: multi-bounce rays and shadow queries can depend on more distant changes. Reuse must prove unchanged lighting, source membership, materials and all recorded hit/no-hit query domains. Unknown or changed dependencies fall back to the current invalidation/trace path. The current conservative behavior remains until this proof is implemented.

Separately, the 230 ms producer-idle episode needs game-thread CPU/wait/I/O attribution before changing queue or query policy. Pretending that a shorter GPU shader would fix a missing command stream would target the wrong stage. Current EVENT completion, message pumping, ordering and frame-skip policies remain intact.

The migration gate for each stage is a frozen-baseline comparison of triangles/attributes, visibility/query results and final MSAA images, including orbit away/back, actual movement, partial writes/DISCARD, alpha animation, map/asset/settings changes, allocation/VA pressure, reset and in-flight retirement. Measure frame p50/p95/p99 and worst hitch, plus reads/new owners/uploads/background CPU. Do not claim success from cache-hit percentages alone.

No proposed stage relies on hiding objects, reducing ranges/resolution/samples, retaining stale GI/shadows or raising memory caps. The immutable-region migration is a concrete follow-up design; it is **not included in this DLL**. An extra-memory triangle-bounds BVH experiment and a frozen camera anchor were rejected because they did not meet the benefit/coverage constraints.

## Fifteen parallel investigation topics

| Direction | Five topics |
|---|---|
| World / GI / streaming | Shared scene preparation and static generation identity; foreground terrain/arena churn; actual8192/compact-cache effectiveness; camera/request scheduling and coverage; shared settings invalidation and BVH preparation tradeoffs. |
| Materials / deformation | Actual new pose routes; current-pose preparation and repeated consumers; Off versus shared actor work; source/cache/state/residency lifetimes; shader creation, cold work and admission gates. |
| AO / postprocessing / feature removal | AO history/rotation costs; fog/cloud/composite costs; local/HDR/shared-resource work; safe water/SSR removal versus native masks/weather/UI dependencies; frame scheduling and consumer-driven execution. |

The package's `validation` directory contains all three English worker reports, frozen input hashes, component measurements, raw regression/build logs, withdrawal metadata and the final integration manifest. Root settings tests include 13 real Lua5.1 menu scenarios, parser/preset/retired-key tests, transaction conflicts/rollback, exact source documentation, installer round-trip in a temporary fake client and static CVar ABI verification. Sanitizer configurations run as ordinary Windows debug where noted; no unsupported sanitizer certification is claimed.

The next user run should retain the same scene, camera radius, orbit path and feature states. Verify forced water redraw disappearance, byte-pressure misses/read/upload peaks, GI reason 256 after unrelated settings edits, source/GPU-owner churn and producer-idle slow frames. New DLL FPS and in-game visuals remain unmeasured until that run.

Final integration: x86 PE32 release builds with all 17 original exports. All 140 genuine-baseline material capture comparisons pass. Retained precipitation passes 2304 GPU cases with unchanged bytecode; 16 removal CPU checks pass and one platform-specific harness is explicitly unsupported. Exact actor, GI dependency, settings and 13 Lua menu scenarios pass. DLL SHA-256: `18182b225fa3d33a1655903cf0dcc1963557001d666793af003702fed0c46917`.
