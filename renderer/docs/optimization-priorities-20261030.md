# Existing-log optimization priorities, 2026-10-30

Source: the latest renderer session, build identity 9eb04689bc486203efa1e24eb3c9ddbef2efd09c8bd16a466c9ca22d99a93e1b, with GTAO High, strength 0.6 and radius 2 yards. The private analysis retains the log hash and input counts. No new game or performance benchmark is run. The generating world cache is not read or changed.

The ranking uses 299 existing GPU timestamp records, excluding matching requested capture frame IDs, and 150 enabled CPU records with active effects. These are mixed-scene diagnostic intervals, not a controlled benchmark or a CPU+GPU FPS equation. Shutdown/Destroy idle gaps are not treated as rendering work. Pair stage groups within each record before taking percentiles; absent cloud work contributes zero.

| Existing GPU interval/group | Median ms | P95 ms |
| --- | ---: | ---: |
| AO + blur | 1.007 | 1.812 |
| Fog + clouds + temporal/blur | 0.679 | 1.820 |
| World composite + material detail | 0.613 | 1.103 |
| Directional removal smoothing | 0.110 | 0.703 |

Existing sampled CPU capture median is 1.568 ms, frame cleanup 0.821 ms and effects 0.869 ms. These labels have their existing scopes; the data does not attribute all cleanup to snapshots. GI alone has a GPU median of 0.086 ms, so a broad GI rewrite is a lower priority than AO or atmosphere in this session.

1. Keep production profiling representative: remove hidden snapshot algorithm rotation and A/B microbenchmarks. This accepted change is described in profile-capture-overhead-20261030.md, with unchanged rendering and explicit offline equivalence/failure checks.
2. Fixed-coordinate AO blur proof scanning is now completed separately in ao-proof-coordinates-20261030.md. Further investigate redundant horizon/depth work in High GTAO and the full filter. Preserve sample coverage, contact and silhouette behavior; reducing the quality setting is not counted as an optimization.
3. Investigate steady-state fog/cloud marching: prove empty-interval/field support before pruning density and shadow samples, preserve accumulated color/transmittance and temporal history. Earlier rejected dense-cloud shader variants remain excluded. Startup noise preparation is already optimized and does not reduce this runtime interval.
4. Share proven depth/fog/material reconstruction work in the final passes. Maintain the lit/unlit path, water, MSAA, UI boundary and HDR behavior; do not merge passes before their output/resource contracts are checked.
5. Reduce far grass reads and repeated source preparation. Across 298 sampled grass records there are 2,579 candidates, 1,884 range rejections, 677 accepted draws and 18 budget rejections. Safe early culling needs current source/version/bounds proof. Merely reusing stale bounds or raising limits would risk missing shadows. The remaining refusal cases should be characterized before changing admission.

The queue is a ranking, not a claim of five completed optimizations. Completed follow-ups remove runtime snapshot benchmark work and fold fixed AO blur proof coordinates. The remaining items are investigations. GPU/whole-game speedups remain unmeasured while performance runs are paused.
