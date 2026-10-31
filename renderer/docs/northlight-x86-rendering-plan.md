# Northlight x86: a twelve-month rendering optimization plan

Status: proposed direction; no forecast of a particular FPS uplift. Remain in the
existing x86 process, leave wow.exe and wow_optimize unchanged, and preserve current
quality, MSAA, active effects and aggregate memory limits. No second render process.

## Goal

Make a camera turn update visibility and current instance/pose state instead of
reconstructing and resubmitting Northlight geometry and material inputs separately
for every consumer. A persistent GPU scene and one shared preparation path serve
materials, directional/local shadows, AO and GI where their semantics permit it.

The maximum version is a replacement for the client's graphics renderer inside
Northlight, in the same x86 process. It intercepts current scene/model inputs before
the client constructs individual D3D9 draws. Replacing that stage removes the native
graphics preparation it owns; a proxy that only intercepts the finished draw stream
cannot recover that CPU time. The retained-scene and shared-pass changes below are
incremental foundations, independently useful before such a replacement is viable.

The current backend already translates D3D9 to Vulkan in this x86 process. Modern
compute/indirect work is an owned-backend engineering option, not a change of client
architecture. Its interop/barrier/submission costs must be measured. Existing GPU
skinning, normal generation, command streaming and workers are foundations, not new
offload opportunities by themselves.

## Measured starting points

The user run ending 2026-10-27 09:52 local used source `1efd61fb` / DLL `add400e4`.
Seven focused gameplay interval windows report approximately 49.65–75.28 FPS with
GI/shadows/fog enabled. Eight sampled world GPU intervals have median total 14.942 ms,
GameScene 8.323 ms and Effects 4.755 ms. These timestamp intervals can include CPU
submission gaps; the stage numbers are not independent savings estimates.

At frame 2413, 6,292,288 bytes of reads mostly accompany 832 vertex-generation
revalidations that reuse exact XYZ; only 48 content-cache misses occur. Frame 1905
instead rebuilds 647 entries: cold triangle classification 2.1695 ms and retention
0.8546 ms are material costs. Warm lookup costs about 0.22 ms with no geometry reads.
These workloads need different fixes, rather than a general cache-size increase.

## Main changes

### First validated increment: shared replay residency (2026-10-27)

Materials and model shadows now borrow an already resident immutable GPU view from
each other before admitting another copy. The existing material and batched shadow
stores remain the allocation owners, with their original 32/64 MiB limits and
admission/fallback policies. Borrowing creates no resources, performs no readback,
wait or shader/state change, and retains COM references through the consuming draw.
All existing actor-shadow consumers use the same world replay bindings.

The lookup preserves full source identity or exact immutable owner identity plus
the batch's vertex/index placement. It never caches a pose or lighting state.
A device-thread-only hint records actual cross-consumer CPU capture use and avoids
peer searches for private meshes; the hint is not a validity proof. It fits existing
Mesh padding, and reset/reconstruction clears it. Unsupported or unavailable views
use the original local residency/UP/bulk path.

The prototype was tested before production integration on 177 stock M2 skin
subsets from three client models. Exact byte/ownership controls cover zero, partial
and full overlap, pressure, changed sources, multiple streams, nonindexed draws,
held references and reset. Actual source-lifecycle tests establish hint admission.
Native DXVK comparisons check 1,327,104 identical MRT pixels across MSAA 0/4/8,
changing camera/deformation, alpha clipping, depth and residency loss.

Seven alternating native timing pairs with a fully overlapping authored workload
measured 15.5686 -> 11.7248 ms CPU submission/preparation over 48 frames (about
0.0801 ms per test frame). Uploaded bytes fell 7,078,320 -> 4,056,192 and peak owned
geometry 1,672,056 -> 879,816 bytes. These are component results, not game FPS or
whole-scene visual acceptance. The no-overlap CPU fixture retains exact allocation,
fallback and memory counts, with timing within noise after the eligibility guard.

Reproduce with `tests/test_shared_gpu_residency.py`,
`tests/test_shared_gpu_residency_gpu.py`, `tests/test_shared_gpu_wiring.py` and
`tests/test_shared_source_lifecycle.py`. Client-dependent inputs are SHA-pinned and
read from the tester's own MPQs; generated assets and private measurements are not
distributed.

Remaining representations need independent experiments. Static FGS/compact caster
meshes and live terrain use different layouts and subset memberships; they cannot
be reinterpreted as native material vertices. GI currently consumes CPU acceleration
data, so GPU buffer sharing alone does not replace its bake. The next steps are
canonical object/source handles with compatible resident views, followed by shared
deformation and eligible native/material pass fusion. A complete common GPU scene
for every effect is not claimed by this first increment.

### Remaining architecture work

1. **One immutable draw description.** Freeze current geometry ownership, source
   spans, declaration/program identity, consumed constants, viewport and raster
   state once. Every eligible consumer reads the same facts. Preserve the second
   mutation stamp and current pose; avoid per-consumer getters, snapshot copies,
   bounds evaluation and source lookup. Work is not shared across incompatible
   texture-write/order/depth/alpha boundaries.
2. **Geometry versions at the right granularity.** An unrelated vertex-buffer
   update must not force every draw to rediscover unchanged positions. Establish
   exact range/position-content proofs at existing write/snapshot boundaries,
   reusing immutable pages. A global version change alone cannot prove that XYZ
   stayed the same. Share vertex/index storage across overlapping subdraws where
   identical content and ownership are established, under the same 32 MiB budget.
3. **Persistent GPU data.** Retain static geometry, compatible material tables and
   instance records; upload dirty ranges and current pose deltas. Reuse current
   deformation across passes when it is cheaper than repeated VS evaluation.
   GPU visibility can generate indirect batches for Northlight-owned passes.
   Retire duplicates of Northlight CPU geometry when no CPU consumer still needs
   them. Client-owned memory cannot simply be freed or reclassified as VRAM.
4. **Fuse eligible native color and material production.** The original opaque
   draw should provide native color and the required material inputs once, sharing
   its POM result. Preserve alpha discard, blend order, depth/stencil, derivatives,
   HDR and MSAA coverage. Extend the owned backend/shader translation where D3D9
   MRT/blend restrictions prevent it. Unsupported paths retain exact replay. A
   full-resolution MSAA normal/occlusion twin allocation is not automatically
   affordable; memory layout and resolving need an independently proven design.
5. **One graph for owned effects.** Make depth/normals/visibility dependencies
   explicit, reuse compatible outputs, alias dead transient storage, and remove
   redundant transitions/color round trips. Keep semantic differences explicit:
   material normals are not universally interchangeable with geometric normals;
   transparent and UI work cannot be arbitrarily sorted into opaque batches.
6. **Rotation-stable transport.** Reuse static light/world-space visibility,
   conservatively skip empty intervals, and invalidate changed actor/source regions
   with current-frame state. Dense inputs must have a fast fallback. Clustered
   light selection can avoid per-object native-slot limitations on supported
   rendering paths, with one owner per source and source-matched shadows.

## No duplicate rendering

The game still issues its original D3D9 stream. Northlight optimizes its own
preparation and eligible execution paths inside the same backend/process. Sharing
the original draw's material result replaces capture work; it does not add another
full-scene render. Each pass has one execution owner. Unknown cases stay compatible.

The maximum version would intercept the graphics scene before native render
preparation reaches D3D9, replacing that graphics stage in the same x86 process.
Northlight would consume current camera/model/pose/material state directly and
avoid generating a second description after the old renderer already prepared it.
That requires build-specific runtime graphics hooks inside Northlight: the file
`wow.exe` remains unchanged, but its loaded graphics execution is intercepted.
It is a distinct intervention from a D3D9-only proxy and is not implemented by
this proposal. Gameplay-dependent state stays native.

The client still performs native Lua, simulation, animation/culling and command
generation. Work already completed before the D3D9 boundary cannot be removed by
an owned GPU pass at that late boundary. An earlier graphics hook can remove only
the graphics work it actually replaces. Measure the useful game-thread CPU floor separately
from renderer-induced waits. A 10 ms irreducible producer still caps real frames at
100 FPS, even if every Northlight effect becomes free.

## Sequence and acceptance

| Months | Deliverable | Required evidence |
|---|---|---|
| 1–2 | Complete private frame replay, CPU floor and scene-level interception feasibility | Authentic inputs, current poses, exact mutation/lifetime/reset behavior; prove replacement skips the old graphics stage and preserves gameplay |
| 3–4 | Position/range proofs, shared immutable pages and persistent GPU resources | Cold/warm/turn/crowd gains under unchanged memory caps; no missed geometry updates |
| 5–7 | Eligible native/material fusion and deformation reuse | Native-color/material/MSAA/HDR image oracles and net CPU/GPU benefit including resolve/bandwidth |
| 8–10 | GPU-generated owned batches and shared light/fog/effect graph | Dense and sparse scenes, moving casters, all active effect coverage; no stale animation/history |
| 11–12 | HD/zone/menu coverage, precompilation/streaming and recovery | Multi-minute live and replay p50/p95/p99, latency, RAM/VRAM, source freshness and device-reset checks |

The target is 8.33 ms for 120 real FPS at explicitly fixed resolution and settings.
Frame generation, stale geometry, missing effects, reduced distances/samples or
silent MSAA changes do not satisfy it. No percentage uplift is promised before
complete-scene and live-client measurements establish what the boundary permits.

The actor-tile proof experiment illustrates the gate: exact output and sparse
speedups were insufficient because dense scenes regressed. It remains private;
the production shader stays unchanged by that experiment.

References: [existing offload audit](wowdecomp-offload-audit-20261026.md),
[DXVK architecture and x32 packages](https://github.com/doitsujin/dxvk),
[GPU-generated indirect drawing](https://learn.microsoft.com/en-us/windows/win32/direct3d12/indirect-drawing),
[external GPU memory/synchronization requirements](https://docs.vulkan.org/guide/latest/extensions/external.html).
