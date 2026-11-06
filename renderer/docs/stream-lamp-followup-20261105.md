# Command publication and native light ownership, 2026-11-05

The latest installed build is `0.3.217-test.20261105.7`. The follow-ups below
record six game runs, capture/CPU optimizations and the remaining stalls. The
earlier MPQ/publication evidence is retained here.

The two complete lighting captures are `capture-1` and `capture-2` in
`D:/Games/335_engb_mod/northlight-diagnostics/session-30804-228518734-0`, renderer
frames 4089 and 4320. Both use source identity `260b7e28...55daef`, RTX 4090,
DXVK 3.1.1, HDR and MSAA 2 at 3840x2160. They are a different machine/scene from
the original Telredor report. Their export frames take 972.515 and 854.446 ms;
these diagnostic export costs are not ordinary gameplay frame-time samples.

Read-only evidence and executable checks are retained in
`out/followup-20261105/`. `latest-renderer.log` is the first snapshotted run;
`additional-renderer.log` retains the later run, including Effects OFF at frame
8411. `inputs.json`, `log-events.json` and `mpq/report.json` identify the inputs.
The user confirmed that the unwanted near-lamp illumination remains with F10 off.

## Light already present in scene

The nearby fixtures are `world/generic/human/passive doodads/lamps/stormwindstreetlamp01.m2`.
The installed asset resolves to `Data/patch-D.mpq`, SHA-256
`961dc4eeb232e921e7c64a2fff93ba9eea18d0721fb7e3d7d5cdccfa2b835254` (15,532 bytes).
It has one native point light at local `(0,0,4.33415985)`, diffuse
`(1,0.550000012,0.200000003)`, intensity 1.5. The stock asset from
`Data/common-2.MPQ`, SHA-256 `4372625c...0fa36f7`, has no native light records.
Both installed Northlight patch-Z archives omit this lamp model, so the added
native source is inherited from the HD archive.

That native source can illuminate the scene before Northlight composition.
Northlight separately reproduces it from FGL. Its shadow correction subtracts
only its own added contribution, leaving native illumination unshadowed. The
captured buffers already contain shadow correction on 188,322 and 192,411 pixels
above 0.001, respectively; an entirely absent shadow pass is not the cause here.
The captured component totals agree closely with an independent static replay.
The original captures do not contain the complete native draw stream, so this is
not a reconstructed whole-frame before/after image.

## Universal MPQ addition rule

Before accepting a generated native model, `native_model_lights.build` reads the
winning original model from the client's full archive chain, excluding the old
Northlight patch-Z. If its M2 light count is nonzero, a new-source replacement is
discarded. Disabled, animated, directional and unsupported native records all
count as existing lights; failure to interpret a light is not evidence of absence.
An unknown model format fails the transaction rather than being assumed unlit.

This gate is shared by equipment, spells, creatures, login scenes, gameobjects
and mounts. The lower-level append helpers already reject existing native arrays;
the final gate also protects against a generator using a stale or different model.
Source paths excluded for renderer ownership are normalized for case and slashes.
Renderer-owned world metadata likewise qualifies only models without native lights.
There is no model-name/hash exception or native-light transfer.

Existing login-scene repairs may still repair a source without adding one: their
validated reports must declare no new emitters, and the original native count
must remain exact. In particular, the gate rejects a count decrease as well as an
increase. The existing RGB-refresh and anchor validators remain in the generator.

The installed MPQ audit covers 3,512 M2 entries per archive. None overlap the 948
renderer-owned metadata models. Six login-scene models override native models
from patch-I; each retains the original count, so those are repairs rather than
additional light arrays. Another 24 M2 entries are generated sky models.
`native-duplicate-audit.json` records the counts and unchanged installed hashes.

The nearby streetlamp remains owned by its existing HD native source. The generic
MPQ gate does not remove that source or prove the captured scene illumination is
fixed. Native illumination plus the renderer's imported native contribution needs
a separate composition investigation; preserving native lighting rules out simply
zeroing the M2 light count as a solution.

## Ready commands waiting for publication

The first local run records 7,366 held automatic publications / 469,926 commands
by stream frame 4200. Those are counts, not the duration of avoidable stalls.
The queue previously published its first 64-command batch while leaving a
sleeping consumer unnotified until 256 commands, 256 KiB or an explicit flush.
It now wakes on the first automatic publication when the consumer is asleep.
The existing one-signal-per-sleep handshake, batching thresholds, memory caps,
command order and blocking semantics are retained, with no added clock reads.

A lone asynchronous resource create or image upload can also precede substantial
caller-side decoding work. Those commands now explicitly publish after recording
their complete payload and ownership. The backend can prepare/upload them while
the caller works on the next asset; no synchronous wait or extra GPU write is
introduced. Read-only and synchronous fallback behavior are retained.

The new sparse-resource test requires a single create and each of two texture
writes to retire without another producer command, validates exact bytes and
requires zero new synchronous calls. The complete command-stream suite passes in
O2, ASan/UBSan, no-filter and no-direct configurations, including ordered texture
updates, partial/GPU writes, resource lifetimes, replay ordering and frame skips.
The existing O0/O2 wake oracle passes for both the old policy control and current
policy, including sparse batches and 128 sleeper-handshake iterations.

The original Telredor buffer fallback storm is not reproduced by these local
runs. Long intervals with no ready commands, such as client-side asset loading,
cannot be attributed to the renderer from these logs alone. Loading and shutdown
intervals are separated from focused steady-world frames. No claim is made that
all client-side stalls or gameplay p99 have been eliminated. The subsequent
live follow-up below still reproduces producer gaps with a drained queue.

## Validation and delivery

The native-install, world-index, attached-source and login-scene suites pass all
54 tests. They cover attempted additions beside existing sources, malformed source
formats, native repair count preservation, path normalization, lifecycle and
disjoint ownership. The full art-layer integration additionally checks native
counts against the real source chain, stock/all-archive builds and repeatability.
No quality preset, shader, shadow resolution, refresh policy or light limit is reduced.

The final full integration passes for stock, the complete installed source chain
and a repeated build of that chain. Both rebuilt base/locale archives are
byte-identical to each other and to the installed archives, SHA-256
`d75742b95f9ca520fd1c6c85e3d353de2d6b5d2289a00d1ecccc000496f06820`.
The installed chain has 948 indexed models / 2,640 renderer emitters and 3,488
native model overrides, including the six existing-light repairs. Thus the final
guard changes no current game asset bytes; it enforces the rule for future builds
and rejects a generator attempting to add beside an existing source.

The publication-stage production DLL was built through `scripts/build_renderer.py`, source identity
`6fd82b0b5c99384add6f55bf7fa88576a853d63a8cce4add9b20eb4ddf9b68e0`,
DLL SHA-256 `8745da7e7fc0f42a79576670b2787f093479d81508a32667d4feef4b4d646f2b`.
`releases/Northlight-0.3.217-test.20261105.4-Windows.zip` passes the official
package verifier (102 files), SHA-256
`6937b1784de41ee3f0b07a382e2e7f6382086c087a2f2f8733db702e4dfb0be6`.
The complete installer regenerates patch-Z from the user's own source assets;
no game model bytes are distributed in that package. A DLL-only update cannot
update the installer-side MPQ rule.

`out/followup-20261105/summary.json` aggregates that stage's validation and hashes.
That stage did not modify installed game files or run the game. The live work
below installs a newer DLL; the nearby lamp's inherited native scene component
remains unresolved.

## Live rotation and capture follow-up

Evidence is retained in `out/rotation-live-20261105/summary.json`, the three
`baseline`, `candidate` and `final` log directories, and `events.jsonl`.
All runs use the saved paladin Bebronk in Stormwind beside the streetlamp and
planter, at 3840x2160, HDR, MSAA 2 and the existing Quality settings. Camera yaw
was temporarily set to 360 degrees/second with continuous right rotation;
the character was stationary. The saved camera CVar was restored, effects
were left enabled, and WoW exited after the final run. No account credentials
were read or changed.

The quality and renderer INIs remain byte-identical to the baseline, SHA-256
`01db04e246bd3a57da5d21b9dae51265d12ec50c0b81cc5ba1668f8ac05b4688` and
`e52967754d15cd395bd0960db8460bc51044cffc11816f5598332b598c309a39`.
No shader, quality preset, shadow resolution, capture range or light cap was reduced.
The existing installed MPQs were not modified during these runs.

Three changes target redundant capture/replay work:

- Material capture checks the native vertex/pixel material pair before reading
  geometry or evaluating a skin envelope. Unsupported pairs retain the original
  native draw and skip capture work. Supported far draws now perform one shader
  lookup earlier; this is an intentional tradeoff, not zero-cost admission.
- Model-normal capture snapshots textures and samplers only after the existing
  conservative distance/pose proofs accept the draw. Mixed, near, stale and
  unknown cases keep their existing capture and immediate fallback behavior.
- Model-normal batch replay uploads the changed float-register span between
  consecutive captured draws. The first packet uploads the complete bank;
  batch boundaries and state restoration reset that assumption. This reuses
  the existing bytewise register-difference helper, preserving signed zero and
  NaN payloads without approximating poses or changing draw order.

The first installed baseline already contains the point-shadow changes, but
predates the command-publication fixes. The intermediate candidate includes
publication fixes plus material admission and texture capture ordering; the
final build additionally includes the register-span change.

| Run | Rotation, UTC | Duration | Effects-on analysis frames | Focused intervals | Median interval FPS | Median interval p95, ms |
| --- | --- | ---: | --- | ---: | ---: | ---: |
| Baseline | 00:41:28.465–00:43:53.781 | 145.316 s | 11200–16000 | 30 | 66.19 | 21.133 |
| Intermediate | 01:12:34.322–01:15:26.832 | 172.510 s | 6500–11000 | 29 | 70.71 | 20.078 |
| Final | 01:26:00.709–01:27:36.707 | 95.998 s | 12500–17700 | 31 | 70.60 | 20.366 |

Each focused interval contains 120 frames with foreground focus throughout.
The aggregate rates from their mean times are 66.410, 70.931 and 71.107 FPS.
Effects-off rotation in the first two runs reaches the existing 120 FPS cap;
the final run keeps effects enabled throughout. These are instrumented live
observations, not an isolated causal speedup: rotations are not phase-aligned,
visibility and native scene work vary, and sun/removal/shadow update workloads
differ. The median of interval p95 values is not a whole-run p95. The final
capture CPU median is 2.413 ms; effects GPU median is 3.975 ms. Model-normal GPU
cost varies substantially with visible geometry, so its live times do not
establish the register change's isolated benefit.

### Stalls still present

The final retained slow-frame records include sixteen individually focused
frames above 33.333 ms, with a worst value of 104.530 ms at frame 17034:
21.998 ms scene, 78.545 ms effects, 3.836 ms tail. The surrounding 120-frame
interval includes a focus transition (`focus=0`) and is excluded from the
fully focused FPS table, but these peaks are retained in the evidence. Their
cause is not established. The intermediate run also has a 141.149 ms peak.
Slow-frame retention is bounded, so these counts are not complete stall counts.

Separate final scene stalls still include 22–25 ms producer gaps. At frame 14090,
the replay thread waits 24.7149 ms between `Device::BeginScene` and `UnlockRect`.
Recorded, published and replayed sequence numbers are equal at entry: there is
no withheld ready batch. The producer sync, present and backpressure deltas are
zero; image-lock accounting contributes only 7.428 microseconds and the paired
image-lock interval probe selects no overlapping lock. CPU accounting has
15.625 ms granularity, and the producer application phase is unknown. This
does not identify geometry capture, lazy publication or texture filling as the
cause. No speculative query-completion or synchronization bypass was added.

### Verification and installed artifact

Material admission covers sixteen rejected material/domain pairs with zero
geometry/pose work, unchanged native draw results and balanced references.
Its pre-change negative control fails the new assertion. Capture target tests
pass O0/O2 and all four negative controls. Old/new admission image comparison
passes 180 terrain/WMO/M2 comparisons, including MSAA 0/2 and pose/mutation
cases. The generic full-bank comparison mode was incompatible with the
current HEAD baseline's already bounded constants; the retained corrected
runner compares the actual HEAD header without that legacy macro.

The complete DXVK model-normal suite passes all eight SM2/SM3 rigid, single,
weighted and four-weight fixtures, including exact 640-draw batch images at
4K/MSAA 2, source mutations, alpha inputs and state restoration/faults. Native
D3D9 quick/envelope checks also pass. A cold state-mirror test proves distant
rejected draws issue no texture/sampler reads; its old-header control fails.
The mutable-pose batch transfers fewer than four float4 registers per draw on
average, including the initial 256-register upload, versus the old 256 registers
per changed packet. Its pre-change control fails too. That is a fixture transfer
reduction of more than 64 times, not a claimed game-FPS multiplier. These exact
checks cover prepared fixtures rather than arbitrary whole-game images.

The final production build passes `scripts/build_renderer.py` with the existing
four compiler warnings. Source identity is
`4fe624bb8e4e422067eaeae364a3c4f8ec84869d95c2a1f3feefd11c78b75596`;
DLL SHA-256 is `7b8025847f949b4c8f6731b1364b9dbbe8471546ce8f63042cf734810f31a157`.
The built DLL, installed `D:/Games/335_engb_mod/d3d9.dll`, standalone release
and ZIP payload match. The final game startup records that source identity;
all three logs contain zero `EXTENSION fault` records.

`releases/Northlight-0.3.217-test.20261105.5-Windows.zip` passes the official
package verifier: 102 files, 20,500,506 bytes, SHA-256
`123b9d5a3867e7f4abcd79bb2164f413c6c35e56ede2468b68dafc71bd4a2d0f`.
The previous `.4` package remains unchanged. The final build includes the MPQ
addition guard described above; it does not remove the HD lamp's native source.

## Narrow range-proof reads and a second live cycle

Evidence for this cycle is in `out/rotation-followup-20261105/summary.json`;
the original logs, configurations, test validations and comparison scripts
remain beside it. The same saved-account login and paladin were used, at the
same lamp/planter position, with HDR, MSAA 2 and 3840x2160 unchanged.

The model-normal distance proof previously read all 256 vertex float4
registers before deciding whether a distant draw needed capture. It now reads
projection c2..c5 and the selected rigid/single bone's three affine rows from
the existing trusted state mirror: seven registers for a complete proof.
Both spans must match the current constant epoch under the existing held draw
gate. Unknown mirror spans retain the complete-bank fallback. Near, mixed,
unsupported and unproved cases still capture their full replay inputs.
Projection, finite-value, rounding, immutable-source ownership and existing
48.01-distance checks remain in the common proof; no extra cache was added.

The CPU range suite passes its existing 6,400 checks plus all 75 legal bone
indices at near/boundary/far positions, narrow/full agreement and failed-span
fallbacks. The full DXVK suite passes all eight SM2/SM3 cases, exact 640-draw
4K/MSAA 2 batch images, mutations, alpha inputs and restoration/fault checks.
Native D3D9 quick and weighted-envelope checks pass. GPU assertions verify
seven-register known far proofs and complete-bank unknown-span fallbacks.
A negative control that disables narrow reads fails the seven-register
assertion as expected; this is a runtime control, not a compile failure.
These are prepared-fixture equivalence checks, not proof for every game frame.

The baseline rotates from 01:39:27.116 to 01:42:07.365 UTC (160.249 s).
The candidate starts at 01:55:12.547 UTC with 360 degrees/s and a local
100-second automatic stop; it is observed stopped 108.816 seconds after start.
The analysis ends before that automatic stop. Camera speed is restored using
its saved value, the temporary update script is cleared, and WoW exits normally.
Effects remain enabled throughout both runs. Both INIs match byte-for-byte;
no shader, visual preset, light budget or installed MPQ changes in this cycle.

| Build | Analysis frames | Fully focused 120-frame intervals | Median interval FPS | Aggregate FPS | Median interval p95, ms | Capture CPU median, ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| `.5` baseline | 15500–24500 | 61 | 72.71 | 73.036 | 20.035 | 2.1740 |
| `.6` candidate | 27574–33807 | 42 | 72.63 | 73.182 | 19.882 | 2.0345 |

The candidate's sampled proof attempts read 140,426 float4 registers instead
of the 3,815,168 required by complete-bank reads for those attempts: 96.32%
less, including fallback overhead. This concerns proof reads only; accepted
packets still need their replay inputs. Median sampled normal draws are
106.5/105 and geometry-source bytes read are zero in both warm runs. The GPU
effects medians are 4.1815/4.381 ms. FPS is effectively unchanged in these
observations; camera phases, native geometry and lighting work are not aligned,
so the timing differences do not isolate this optimization. Interval p95
medians are not whole-run p95. Mixed-focus intervals are retained separately.

Stalls remain. The candidate retains eight individually focused records above
33.333 ms in this window, with a 48.352 ms peak at frame 30757 (41.851 ms scene,
2.301 ms effects). Its surrounding interval contains a focus transition and is
excluded from the fully focused table, while the peak remains in evidence.
The baseline's corresponding retained maximum is 135.912 ms; different run
lengths, focus transitions and bounded slow-frame retention prevent treating
this as a proven reduction in stall frequency or severity.

At candidate frame 30757 the consumer waits 26.0433 ms between BeginScene and
UnlockRect. Recorded/published/replayed sequence numbers are equal at entry,
sync/present/backpressure deltas are zero, image-lock accounting is 9.526 us,
and the paired lock-interval probe selects no overlapping lock. Producer CPU
accounting reports 15.625 ms at coarse resolution; the application phase is
unknown. Windows Performance Recorder CPU tracing was attempted but failed
with OS error `0xc5585011` (performance profiling policy could not be enabled).
No policy changes or trace were produced. These observations do not identify
geometry capture or publication as the source of this remaining producer gap.
The native HD lamp scene-light issue above remains unresolved.

Production build and official package verification pass with the same four
compiler warnings. Source identity is
`102dab5a49eda218ab433fba60362a4788ae1f1888aab5123ae795dbeb12acec`;
DLL SHA-256 is `c5c293fa669be1cf5093f34271b5f21af67d35aabcfcdc5fe9a71159ea5e298e`.
The production DLL, installed game DLL, standalone release and ZIP payload
match. The game startup records this identity and both new logs contain zero
`EXTENSION fault` records. Package
`releases/Northlight-0.3.217-test.20261105.6-Windows.zip` contains 102 files,
20,502,570 bytes, SHA-256
`f89a86812c3fc2bd9c2abd9481f3ea9156e61fc67790f891197a14ee9a9c936a`.
Previous `.4` and `.5` packages remain unchanged.

## Spell-fog CPU overlap and a third live cycle

The `.6` warm log already reports zero model-normal geometry-source bytes read.
Its lighting-setup CPU bucket instead spends a median 0.443 ms, including
0.376 ms fitting spell-fog ramps in the expensive sampled updates. The new
change runs that same next-frame calculation on the existing CPU job pool
while the renderer submits the remaining GPU passes. Inputs, timestep,
smoothing, fit candidates, revisions and publication before the next frame
are retained. A private result is joined and published before the first reader;
a scope guard also settles every early return. There are no new threads or
skipped updates. ReplayJobs=0 and an unavailable floating-point environment
retain the inline calculation.

The job captures the caller's floating-point environment and restores the
worker's environment afterwards. This matters for this 32-bit client: x87
precision and SSE denormal/rounding controls can differ between threads. The
CPU differential check passes 5,698 direct fits, 5,600 shared fits and 500
complete hold updates against the preserved `.6` header. Queued publication
and inline execution agree exactly across four rounding modes, three x87
precision modes, SSE FTZ/DAZ combinations, hints, fading and invalid-input
resets. An unavailable-environment fallback also agrees. The serial benchmark
ratio is 1.0006: no arithmetic speedup is claimed. The job-system suite passes
Windows O0/O2 and renderer lifetime/wiring checks. Those Windows runs do not
run ASan/UBSan/TSan.

The new saved-account paladin session rotates at 360 degrees/s from
02:21:16.687 UTC with a local 120-game-second automatic stop. Effects are
disabled at 02:23:10.146 UTC, about 113.46 seconds after start. That leaves
too little rotating OFF time, so an additional 35-second OFF orbit starts at
02:26:32.781 UTC. Effects are restored at 02:27:08.044 UTC, the saved camera
speed is restored and the temporary update script removed. Normal `/quit`
completes and the window inventory confirms that WoW is closed. Both INIs
remain byte-identical to the baseline hashes above; shaders and MPQs are
unchanged in this cycle.

| Build / phase | Analysis frames | Fully focused 120-frame intervals | Median interval FPS | Aggregate FPS | Median interval p95, ms | Capture CPU median, ms | Effects CPU median, ms |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `.6` ON baseline | 27574–33807 | 42 | 72.63 | 73.182 | 19.882 | 2.0345 | 1.9885 |
| `.7` ON | 12237–19462 | 48 | 72.97 | 73.262 | 19.868 | 1.9760 | 1.7120 |
| `.7` OFF control | 44430–47669 | 23 | 120.01 | 120.075 | 8.520 | 0 | 0 |

The new lighting-setup bucket median is 0.079 ms. All 53 expensive sampled
fits execute on a worker; their median fit time is 0.240 ms and median join
wait is 0.015 ms (maximum 0.156 ms). Per-sample constants plus join time is
recorded in `live-comparison.json`; submission time and join are separate
costs. GPU effects medians are 4.381/4.359 ms. Overall FPS is effectively
unchanged. Camera phase and scene daylight differ between runs; these live
observations do not establish an isolated FPS gain. The OFF rate is capped at
120 FPS and still uses the Northlight proxy. Interval p95 medians are not
whole-run p95; mixed-focus intervals are excluded from FPS, with individually
focused peaks retained separately.

Stalls remain: ON frame 18383 retains 48.631 ms total, including 30.873 ms
effects. It follows a resource recreation at frame 18382 and a logged 125 ms
submission; the cause of the recreation and the unsampled effects cost is not
established. Another ON frame, 17426, has a drained-queue producer gap of
24.7727 ms, BeginScene to UnlockRect. OFF frame 44537 also has a 24.5343 ms
gap on that boundary with effects work zero. Sequence numbers are equal at
entry and sync/present/backpressure deltas are zero; image-lock accounting
is only 9.224/7.524 microseconds. Producer application phase remains unknown.
This does not identify geometry capture as the cause or demonstrate a pure
stock-client reproduction. Slow-frame retention is bounded. The inherited
native HD lamp's scene-light issue remains unresolved.

Evidence, original logs, frozen reference header, test logs, analysis scripts
and hashes are retained in `out/rotation-cycle3-20261105/summary.json` and
adjacent files. Exact point-shadow GPU fixtures pass DXVK and native D3D9;
their recorded source hashes precede the final timing-only log fields.
Production build, layout/diff checks and official package verification pass,
with the existing four compiler warnings. Source identity is
`1cbb05ad1db39592b37a697b55c43a9a80c1aedac66c102b2a52cba98063f9c3`;
DLL SHA-256 is `069565e1cd7b69e352704b92c9b729e0aa5872c751c16ee53b9e01d35b5fdc18`.
The production, installed and archived candidate DLLs and ZIP payload match;
startup records this identity with zero extension faults or spell-fog resets.
`releases/Northlight-0.3.217-test.20261105.7-Windows.zip` contains 102 files,
20,505,690 bytes, SHA-256
`5d6be463b16e28c9b43d45246890fa3657e619b877f02682500571213eb5b9ff`.
Previous `.4`, `.5` and `.6` ZIP hashes are unchanged.
