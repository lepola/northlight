# Telredor frame pacing review, 2026-11-05

The supplied Telredor report contains a large point-shadow draw burst, synchronous
buffer-lock storms, and separate producer-starvation stalls. Reducing texture
detail, AO quality, shadow resolution or lighting distance would not address all
three. The first optimization target is shadow submission and cache reuse with
the existing image, caster set and refresh behavior preserved.

The diagnosis below describes the original capture and pre-change repository.
Implemented changes, controlled results and the built test package are recorded
in the final section.

Evidence is `qa_report/northlight-renderer.log`, SHA-256
`d53fb9487898d5b97d7bf08f97e787f3751b20f00c625b183f32b5b580f40889`,
65,331 lines. The renderer reports source
`d5d862160d18fdcf1199df5d36f97e4e8f39819ba4ebd708a7f10daaef78fb05`,
built November 4 at 21:02:00 UTC, DXVK 3.1.1 and an RTX 3080. HDR is off and
MSAA is zero. The 1920x1080 video is the recording resolution; the log's
3,686,400-pixel material target corresponds to a 2560x1440 rendering target.

The supplied 8.208-second, 24 FPS HEVC clip shows its counter falling from
90 FPS / 11.10 ms to 20 FPS / 51.13 ms during a camera sweep, then recovering
to 90 FPS. The low capture rate affects motion presentation, but does not
explain that counter change. There is no shared timestamp marker establishing
an exact video-to-log-frame alignment. The log examples below are evidence of
the session's failure modes, not a claim that frame 24130 is a particular video
frame.

The parser and complete derived evidence are private local files:
`out/telredor-performance-20261105/analyze.py` and `evidence.json`.
Run `python out/telredor-performance-20261105/analyze.py` from the repository
root to reproduce the numbers and arithmetic checks. The initial investigation
was read-only. Subsequent renderer changes are described below; the installed
game and shaders remain unchanged.

## The decisive shadow burst

These are individual sampled frames in one session, not a before/after benchmark.
All three have complete focused interval coverage, `probeMode=off` and
`probeRan=0`.

| Renderer frame | Point-pass draws | Point CPU wall time | All effects CPU wall time | GPU frame interval | GPU effects interval |
| --- | ---: | ---: | ---: | ---: | ---: |
| 24003 | 71 | 0.661 ms | 1.576 ms | 12.525 ms | 3.011 ms |
| 24130 | 32,626 | 187.829 ms | 190.773 ms | 221.295 ms | 189.634 ms |
| 24257 | 97 | 1.359 ms | 2.326 ms | 11.705 ms | 2.711 ms |

At frame 24130 the point pass accounts for 98.46% of effects CPU wall time.
The raw D3D census independently records 33,558 indexed draws and 121,745 total
calls. Native game draws increase from 2,368 at frame 24003 to 5,343 at 24130;
the injected point workload increases much more sharply. The point bucket is
an actual raw-draw counter, not an estimate from model counts.

Sources: log lines 56365, 56367, 56371-56372, 56612-56620, 56625-56626,
56887-56898. Frame 24130's Present takes only 0.007 ms, and query counters are
zero. Its slow-frame record is 234.421 ms. The surrounding complete focused
120-frame interval ends at 24217 with mean 39.630 ms, median 25.357 ms,
p95 113.107 ms and maximum 236.463 ms (lines 56711-56715).

Across 86 focused, applied, probe-off bucket samples after world entry, the
median point CPU wall time is 0.379 ms and median draw count is 73.5. These
are statistics of sampled passes, not gameplay-frame percentiles.

The GPU timestamp interval does not prove 187 ms of shader execution: CPU
submission gaps and synchronization can leave the GPU idle between timestamps.
The independently measured CPU time and draw count establish a submission
problem worth addressing before changing shader quality.

The adjacent 120-world-frame point report records 66 rebuilt static faces,
and the replay set grows from 427 to 1,429 packets (lines 56390-56392 and
56653-56655). This supports investigating cold cube rebuilds and caster
multiplication. The existing log does not split frame 24130's point draws into
static, cached terrain, live terrain, ordinary replay and emitter-split draws;
it cannot identify their exact shares.

## Lock storms amplify expensive replay

Four sync-storm starts report approximately 116, 114, 155 and 225 synchronous
calls per frame. The last is dominated by 112.594 SyncLock and 112.594
SyncUnlock calls per frame over a 32-frame window, with zero buffer readbacks
per frame in that window (line 56652). It occurs around the shadow burst.
These are cross-thread rendezvous, not necessarily GPU readbacks.

The subsequent stream summary reports cumulative `noshadow=82711`, the
34.5/36 MiB large-buffer allowance in use, and approximately 6.067 ms/frame
in synchronous waits over its broader reporting interval (line 57448).
That summary also spans a focus transition, so it is context rather than an
isolated gameplay timing benchmark. Do not interpret cumulative refusal bytes
as memory allocated at once.

`src/stream/stream_proxies.h` already has asynchronous shadows, DISCARD staging,
zero-copy slices and overlapping-write handling. A written buffer without a
usable shadow falls back to a synchronous backend Lock and Unlock; see
`lockBuffer` around lines 1121-1176 and `unlockBufferImpl` around 1179-1232.
Adding another general queue or simply making CreateTexture asynchronous would
duplicate existing mechanisms: ordinary resource creation is already queued
by `StreamDevice::finishCreate` in `src/stream/stream_device.h`.

The responsible buffer identities, sizes, locked ranges and callers are not
reported for these no-shadow fallbacks. Measure them before changing admission.
A plausible optimization is a bounded mirror of frequently locked valid ranges
when full-buffer retention is wasteful, retaining the existing synchronous path
for unknown bytes, read-only semantics, GPU writes and allocation failures.
This is a proposal, not an established fix for a named buffer.

## Separate stalls while replay has little work

Three retained slow frames explicitly have focus, no render-thread log activity,
and no sampled instrumentation:

| Renderer frame | Whole frame | Effects CPU wall time | Replay queue idle time |
| --- | ---: | ---: | ---: |
| 7810 | 137.484 ms | 1.125 ms | 99.367 ms |
| 11358 | 140.925 ms | 1.831 ms | 102.459 ms |
| 17803 | 128.787 ms | 0.636 ms | 97.562 ms |

Sources: lines 17739, 24923 and 40974. Idle time is contained in the whole
frame; it must not be added to the other phase durations.

Nearby stream-domain gaps bracket CreateTexture, Derive and UnlockRect.
Examples at stream frames 7823, 11371 and 17816 show waits of roughly 36-51 ms,
often with little game-thread CPU-counter advancement and negligible measured
proxy work. Stream and renderer frame numbers are different domains; their
13-frame separation here is consistent with the recorded 13 skipped frames.
The selected lock interval overlaps only a few microseconds at the end of
several gaps. It does not account for the preceding wait. See lines
17729-17732, 24871-24874 and 40975-40978.

The report labels the application phase unknown. Client asset IO/decode,
an external hook, application sleeps and publication delay remain hypotheses.
A game-thread stack and wait/IO trace during these events is needed to choose
between them. A faster AO shader cannot fill a queue whose producer is stalled.
Separate long image-lock records also name DINPUT8.dll, including an 88.923 ms
Lock call at stream frame 1022; that establishes an additional hook caller,
not responsibility for every later stall.

## Optimization order with the image preserved

1. **Reduce point-pass draw multiplication.** Add sampled per-light/per-face
   draw categories and submitted triangle counts beside the existing bucket
   timer. Distinguish static rebuilds, cached terrain, live terrain, ordinary
   replay and emitter-split runs. Then extend the existing compatible-run
   coalescing (`pointBatchRun`) to the dominant stream, retaining primitive
   order, exact vertex/index data, alpha coverage, fixture exclusions and
   current pose constants. Rigid instancing is only eligible when the same
   per-instance transform and deformation can be reproduced. Existing static
   batch merging already works; rebuilding that optimization will not help.

2. **Keep proven static faces reusable across camera turns.**
   The pre-change `renderPointShadows` releases an unselected slot immediately,
   while
   `SelectionTracker::update` removes offscreen entries. Retain source-keyed
   static faces within an explicit bounded allowance and reuse them only when
   the light, geometry and material-content signatures agree. Reuse the
   existing `pointFaceDigests` / `pointStaticRebuild` validation. Keep dynamic
   faces fresh on re-entry. Do not retain every full dynamic cube up to the
   configured 64-light limit merely because slots exist. This candidate is
   supported by the code lifetime and rebuild counts. Measurements of the
   implemented retention are recorded below.

3. **Remove avoidable lock rendezvous.** Rank the actual no-shadow buffers by
   synchronous time, range and reuse, then improve range retention or staging
   within existing memory budgets. Preserve read-after-write ordering,
   DISCARD/NOOVERWRITE behavior, overlapping locks, ProcessVertices invalidation,
   resource lifetime and failed-lock behavior. Fixing the point-pass burst first
   also shortens the replay work that synchronous callers can get stuck behind.

4. **Trim steady capture/material CPU work after the spikes.**
   Frame 24130 spends 16.836 ms in the capture scope versus 4.455 ms at 24257.
   Material exclusive CPU time rises from 3.212 ms at 24003 to 8.504 ms at
   24130. These scopes overlap and must not be summed. Share proven source,
   constant and pose results across material/shadow consumers and retain
   compatible state through adjacent capture draws. Exact state restoration
   at native draws and resource-write boundaries remains mandatory. The log
   already shows effective geometry/GPU reuse and near-zero cold material
   builds in the decisive sample, so "cache all meshes" is not a new solution.

5. **Resolve producer stalls using wait stacks.** If the trace establishes
   asset decode/IO, preload the same source data off the critical path while
   preserving publication ordering. If work is committed but withheld, repair
   that specific publication/wake path. If an external hook is responsible,
   fix or configure that component. The present evidence does not justify
   choosing one of these fixes or increasing frames ahead.

Increasing PointShadowRefreshMs or lowering PointShadowFacesPerFrame would
change moving-shadow freshness. Lowering light counts, ranges, AO/POM settings
or resolution changes the look. Those are quality tradeoffs, not the requested
optimizations. Keep them out of the acceptance path. The current crowd schedule
already permits complete-cube reuse for 33 ms even when the configured refresh
minimum is zero; see `RefreshSchedule::period` in
`src/lights/point_light_shadow.h`. Preserve that existing policy rather than
silently adding more lag.

## Measurement and acceptance

The current capture has Diagnostics, RenderProfile, ShadowFateDiagnostics and
DiagReplayProbe enabled. The probe cycles off/full/state/logic windows.
Non-off windows are not a controlled quality-preserving production baseline.
Use the existing lightweight interval and slow-frame telemetry for a fresh
same-camera comparison; disable the extra replay probe for the timing run.
Do not change visual settings to obtain a better number.

`FRAME slow` retains only four worst events per 120 frames. `FRAME cost`
excludes logging frames and their neighbors. Its window 36 consequently shows
a 58.030 ms maximum while the complete interval and retained events show
234-236 ms stalls. Excluding these events would conceal the actual problem.
Do not derive whole-session p99 or 1% lows from these incomplete summaries.
The parser checks bucket arithmetic and matches GPU Effects against the
independent GPU-frame Effects interval.

There are no recorded Reset calls or memory trims in this report. All 42 memory
samples have pressure zero; minimum available virtual space is 669 MiB and
minimum largest free block 540 MiB. This is not evidence for periodic cache
clearing, and the limited 32-bit address space argues against blanket budget
increases. The maximum recorded logger call/flush time is 3.458 ms, which does
not explain the 180-200 ms point workload. Later unfocused intervals approach
33.3 ms and must be excluded from foreground performance comparisons.

For each implemented change, compare identical inputs before/after: all six
R32F cube faces, cutout coverage, material normals/occlusion, and final SDR
output. Include turning away/back, animated actors, moving/removed lights,
emitter self-shadow exclusions, buffer mutations, reset/recreation and memory
pressure. Demand exact output where the existing path permits it; preserve
existing numerical tolerances elsewhere. Measure full-frame p95/p99/max and
stall counts on raw focused frames, with diagnostic sample frames labeled
separately. Check draw counts and synchronous waits on the same frames.
Native D3D9 and both supported DXVK backends need the existing regression gates.
These are the acceptance criteria for subsequent work. The controlled checks
below establish equivalence for their specific inputs; they do not establish
whole-game frame-time percentiles.

## Implemented changes and validation

Production source identity:
`260b7e28d179084d0867f345c7c5fb9baa3d74b6f34185ae3c7b2cec8055daef`.
The implementation changes `world_point_rendering.inl`, `world_renderer.h`
and `world_memory_guard.inl`:

- Extend existing static/terrain batch merging across index gaps only when
  every intervening batch is conservatively proven outside the current
  light/face. Keep index order, material/cutout semantics, page boundaries and
  device primitive limits. Additional clipped triangles cannot exceed selected
  triangles in the merged run. Excluded and unknown batches remain barriers.
- Merge consecutive indexed triangle-list model packets only with identical
  geometry bindings, compatible material state and the existing exact captured
  pose proof, including integer/boolean constants. Preserve emitter exclusions,
  strip boundaries and logical face-refresh weights. No new index buffer is
  allocated for either merge path.
- Retain at most eight completed static caches when a light leaves selection.
  These consume spare slots within the existing configured light limit, with
  at most 12 MiB of idle static targets. Re-entry validates light, map and
  content signatures and requires a freshly completed dynamic cube. Clear
  retained caches on invalidation, GPU release and memory pressure.
- Allocate static R32F faces only when there are candidate static casters.
  Empty faces sample a shared 1x1 white texture. Allocation retries preserve
  existing valid resources, and empty/populated transitions rebuild correctly.
- Add sampled `POINT workload` draw/triangle/rebuild/restore counters after
  the measured effects spans, plus actual allocated face counts in `POINT pool`
  and resource inventory accounting.

No shader, resolution, precision, light distance/count limit, quality preset
or shadow-refresh policy changed. Point faces remain 256x256 R32F; the configured
limit remains 64, with the existing supported maximum of 128.

### Memory cost

One face is `256 * 256 * 4 = 262,144` bytes, or 256 KiB. Six working cube faces
cost 1.5 MiB; a fully populated six-face static cache costs another 1.5 MiB.
The new per-active-source logical texture payload is therefore
`1.5 MiB + populatedStaticFaces * 0.25 MiB`, excluding shared resources and
driver overhead. Each empty static face saves 256 KiB without changing depth
precision or coverage. A source with two populated faces uses 2 MiB instead
of 3 MiB; six populated faces still use 3 MiB. The authored GPU test verifies
that two-face allocation and exact cube output on all three backends.

The cached Telredor orbit has candidates in all six static faces of its active
lights. Its mean active static allocation remains 19.125 MiB in both versions;
this scene does not demonstrate a reduction from lazy allocation. Idle retained
faces are separately bounded and counted, rather than hidden in that active
allocation figure. Increasing the global light limit was unnecessary.

### Controlled results

| Workload | Before | After | Output check |
| --- | ---: | ---: | --- |
| Authored clipped-gap static workload | 2,048 draws | 2 draws | All six R32F faces exact on native D3D9, DXVK 2 and DXVK 3 |
| Authored consecutive model packets | 192 draws | 6 draws | All six faces exact for opaque and alpha-cutout inputs on all three backends |
| Cached Telredor static component, 32 camera steps | 15,403 draws | 5,189 draws | 2,448 corresponding face hashes identical on DXVK 3 |
| Same Telredor component, rebuilt static faces | 990 | 426 | Same face-hash comparison |

The Telredor comparison uses repository baseline `4b9616de`, not the exact
tester DLL. It was captured before the final model-packet merge was added and
executes the static-only path (`withReplays=false`); source records are retained
with both runs. The final authored tests include the completed production source.
Thus 66.3% fewer component draws and 57.0% fewer static rebuilds are supported;
neither is a claim about whole-game FPS or the exact 32,626-draw logged frame.

For the same static component, median CPU submission time was 0.646 -> 0.667 ms;
median CPU-to-GPU completion time was 1.843 -> 1.182 ms. Maximum completion time
was 4.808 -> 5.134 ms. This measurement does not establish a CPU submission or
worst-frame speedup. The deterministic operation counts and exact outputs are
the stronger results.

Completed checks:

- New production-path GPU tests: batching barriers/overflow, opaque/cutout
  replay, unchanged refresh weights, static retention, light/geometry/map
  mutation, empty-to-populated transitions, pressure, LRU and hard limits on
  native D3D9, DXVK 2 and DXVK 3.
- Existing point-terrain GPU regression on native D3D9 and DXVK 3, including
  dynamic transitions, face reuse, binding preservation and visibility re-entry.
- Cached Telredor visibility suite on DXVK 2 and DXVK 3: 2,034 ordinary exact
  face checks and 8,136 recovery face checks per backend, in addition to the
  32-step before/after orbit above.
- Lamp self-shadow regression: 48 cases across all three backends. After the
  default process guard found an open WoW, the user explicitly authorized these
  independent correctness stands alongside it. The runner now has an explicit
  `--allow-running-game` flag; its default guard is preserved and that flag
  cannot be used for benchmark runs. These results support correctness only.
- Portable diagnostics, prepared clipping, point light/shadow schedule, replay
  draw-state and memory-guard tests; Windows quality-settings tests; pressure
  transition O2/O0/ASan/UBSan tests with the old-order negative control. The
  pressure runner also records the explicitly authorized open-game mode and
  rejects timing pairs in that mode.
- Existing captured-pose checks in O2 and ASan/UBSan: 128,013 reference draw-bank
  comparisons per run, including changing views, poses, ranges and culled draws.
- Production DLL build through `scripts/build_renderer.py`, shader/SM3 freshness
  gates, official Windows package verification, layout check and whitespace check.

Inspect `out/telredor-optimization-20261105/summary.json` for the aggregated
machine-readable evidence. Individual records live under `authored-final2`,
`current-orbit`, `baseline-orbit-dxvk3`, `coverage-regression`,
`lamp-regression-open`, `linux`, `linux-memory`, `linux-pressure-open`,
`pose-tests` and `package-verification` in that directory.

### Built artifacts and remaining uncertainty

The test package is
`releases/Northlight-0.3.217-test.20261105.2-Windows.zip` (102 files), SHA-256
`a37a509d3b26db0054d744755e59438b51a2cdd1a6d501778ccb8df88ddea3d2`.
The matching standalone DLL is
`releases/Northlight-0.3.217-test.20261105.2/d3d9.dll`, SHA-256
`3e23aab852f456cba207f7223c9246da839858d51dbea7a2d8e4602018d4b7a5`.
The embedded source identity matches the final production source and DLL.
Official package verification reports no problems.

The installed game was not modified or stopped. No whole-game before/after
frame-time recording has been made with this DLL. The separate producer stalls
and unidentified synchronous-buffer fallbacks from the original report remain
unresolved; the new workload counters help distinguish them from shadow bursts
in a fresh log. These changes remove demonstrated shadow-submission and cache
rebuild work while preserving the tested outputs, but do not prove elimination
of every stutter shown in the recording.
