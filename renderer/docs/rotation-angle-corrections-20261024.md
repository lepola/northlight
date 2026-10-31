# Camera rotation: source admission and conservative shadow bounds

Private follow-up to the
[architecture report](rotation-architecture-20261024.md) and
[native replay correspondence work](native-code-correspondence-20261024.md).
The current game log is unchanged: it ends at October 24, 03:28:59 and has
SHA-256 `b71ba562267c13188f24219df83bd71bdf7273e9bd914210833227343d886443`.
There is no new runtime FPS measurement for these changes. The game and
optimizer are not launched or modified, and nothing is published.

## Parallel investigations

Each of three workers owns five distinct directions. The coordinator reviews
shared contracts and runs only focused checks after source edits freeze.

| Direction | Five topics | Evidence and implementation scope |
| --- | --- | --- |
| Equipment / particle lighting | Camera-dependent admission; shader/negative cache lifetime; current attachment/pose transforms; brightness/area/power response; source budgets and stable selection | The particle capture path rejects a source by its triangle centroid's clip position before evaluating emission. A source outside that margin can still illuminate a visible receiver. Remove that false point rejection, preserve aggregation and all budgets, and verify actual capture with camera-only changes. |
| Shadows / AO | Main-view caster selection; shadow-cache validity; depth/mask/MSAA consistency; cascade/atlas/bounds; invisible AO/contact work | Float center/extent transformation can shrink a static caster box below an actual transformed vertex. The box drives culling and dirty coverage. Repair the shared conservative-bound contract; do not change geometry or weaken light-plane rejection tolerances. |
| Native stand / Reset | Post-Reset bindings; copy formats/dimensions; synchronization/readback; tracer/backend ordering; resource/shader generation | Exact client sizing is now verified but does not repair the existing black post-Reset swapchain readback. One pre-copy source-readback control tests materialization separately; it remains diagnostic and cannot replace acceptance of the original command sequence. |

## Why these changes are useful

The light admission defect can make a fixed world source disappear when only
the camera turns. Testing a point against screen edges is insufficient for a
finite-radius light. A visible receiver with positive attenuation supplies an
independent semantic oracle. The corrected path retains existing source
selection and scissoring after clustering and tracking.

An early per-sample influence test was reviewed and rejected before execution:
even an individually invisible weak sample can change a visible neighbour's
cluster centre and colour. Track following adds another offset, so padding by
one merge radius would not prove safety. The unadopted version also increased
worst-case vertex reads while searching invisible multi-triangle draws. The
final change keeps all samples through the original aggregation, retains
distance/read/draw/light caps and introduces no guessed envelope or additional
artist-response formula. No early-work or universal CPU saving is claimed.

The shadow defect is a numerical enclosure failure, not evidence that the
entire reported shadow issue has been reproduced. With a narrow box near a
large local coordinate and a cancelling translation, rounding the center
before applying the extent can discard part of the true endpoint interval.
A plane between the shrunken bound and the real endpoint can then falsely
reject the batch. Independent transformed endpoints and an actual GPU-cache
selection path are required to validate a conservative correction.

These are correctness changes. They do not establish that either source
caused the 249.4252 ms empty producer-queue interval in the user log.
Larger terrain storage remains a separate, unmeasured 32/48 MiB experiment.

## Additional evidence for the producer gap

The existing optimizer session log contains a frame-specific sampler that
the renderer log alone does not expose. Its `FlightRec` frame 22722 is
278.03 ms, and the following `FreezeCatcher` block records 129 main-thread
samples. Northlight records 278.492 ms at renderer frame 22722, with
249.4252 ms waiting for producer input. The game's thread ID 53180 and the
session totals provide additional join evidence; measurement boundaries and
the two clocks still differ.

Several printed instruction addresses fall in the installed symbol map's
`LuaVmFast::Hooked_Execute` range, consistent with the log's own identification
of its `luaV_execute` hook. The symbol file has two separately sorted blocks:
both must be merged and sorted before lookup. These observations point to
Lua execution during part of the slow frame, not a material shader consuming
that entire interval. They do not identify a Lua script, callback or addon,
and the sampler prints only a limited set of address buckets. Unprinted
samples and the unsampled beginning of the frame remain unknown.

The same strict timestamp/thread join finds nine corresponding slow frames,
all with printed Lua evidence. Their Northlight scene-queue idle intervals
range from 140.404 to 250.589 ms, while actual time inside GetData is only
0.006 to 0.037 ms. These are selected slow frames, not an unbiased frame
distribution. Across all 67 completed sampler reports, only 1,785 of 4,346
samples are printed; source categories overlap. The evidence is retained in
`native-chain/optimizer-native-gap-followup.{md,json}`. It establishes neither
exclusive Lua duration nor responsibility for a particular script.

A separate printed bucket first appears at 254 ms in `VirtualQuery`, through
`PerfDiagnostics::LogPerformanceSnapshot`. That late diagnostic tail must not
be conflated with the preceding 249 ms empty-queue episode. Symbol-map
classification is corroborating evidence, not a complete timing attribution
or an at-load cryptographic identity proof. The optimizer log/symbols are
read only; no optimizer implementation or setting is changed.

A faithful test environment must therefore distinguish replayed D3D workload
from original Lua/client preparation. Inserting a delay into a D3D trace
would reproduce a duration while concealing the routine that needs work.

## Issue review

Read-only GitHub search was refreshed for the eight open issues. The saved
snapshot is `current-issues-visual-followup.json` in the private follow-up
diagnostics directory. No GitHub comments, closures or PR changes are made.

- [#101](https://github.com/lepola/northlight/issues/101): the source-admission
  correction addresses one camera-dependent loss mechanism. A matching real
  item/receiver/frame capture is still needed to accept the complete issue.
- [#100](https://github.com/lepola/northlight/issues/100) and
  [#99](https://github.com/lepola/northlight/issues/99): no proximity-only
  deduplication, arbitrary energy reduction or guessed shoulder identity is
  introduced. Source identity and receiver-reference requirements remain.
- [#102](https://github.com/lepola/northlight/issues/102): preserve exact
  particle samples and existing read/draw caps; avoid definitely invisible
  work without reducing visible emission quality.
- [#104](https://github.com/lepola/northlight/issues/104): existing exact
  binding/constant reuse remains. No source-backed batch count or timing gain
  is claimed from these angle corrections.
- [#105](https://github.com/lepola/northlight/issues/105): AO quality remains
  opt-in; High defaults are retained.
- [#106](https://github.com/lepola/northlight/issues/106): no global thickness
  gate is adopted. Foot contact, character AO and real structural creases
  remain mandatory image constraints.
- [#17](https://github.com/lepola/northlight/issues/17): preserve Northlight
  state restoration; optimizer binaries, source and settings stay untouched.

## Acceptance

The final static-bounds fixture passes O2, O0 and ASan/UBSan. It exercises
independent exact/rounded/fused endpoint oracles, all six rejection planes,
the real GPU-cache selection and dirty-coverage paths, and invalid/overflow
fallback. Restoring only the former production bounds formula fails with
the required `STATIC_CASTER_FALSE_REJECTION` marker. Evidence:
`visual-angle-bounds-1/validation.json`.
The changed existing static-scene fixture also passes O2 with assertions
enabled, including its conservative-enclosure and tight-padding requirements;
evidence is `visual-angle-scene-1/validation.json`.

The final particle fixture passes 13 cases each in x86 O2 and O0 using stock
MPQ shader/texture bytes with authored geometry and receivers. It verifies
camera-only indexed/UP changes, positive receiver contribution, near/eye-plane
and large-triangle cases, exact cluster/track behavior, affine transforms,
finite inputs and original work caps. A boundary case preserves averaging
before the existing world-coordinate magnitude guard. Restoring the exact
old centroid screen gate fails for the expected rotation mismatch. Evidence:
`visual-angle-particle-2/validation.json` and its nested fixture report.
The earlier wrapper-path failure remains preserved; it is not final evidence.
These tests run no game or graphics device and make no timing claim.

The optional pre-copy Reset readback experiment completed with both baseline
and control passing six strict backbuffer RGB/query frames, byte-identical
between the two modes. The baseline now passes too, so the additional read
has no demonstrated causal benefit. Earlier black-frame failures remain
preserved. This is an authored transport diagnostic, not acceptance of the
original failed Reset sequence or a performance result. Evidence:
`native-chain/reset-followup/matched-precopy-target-1/validation.json`.

Normal and stand builds use renderer source identity
`a6d6e9f2aadc871d358d61ba0384d62da5ed9c5b93d7e24b48a8c008b0eea842`.
Both builds, exact PE/PDB GUID/age matching, normal-package admission and
stand-package refusal pass. The normal DLL/PDB package is privately staged
at `northlight-builds/northlight-angle-a6d6e9f-20261024`, without settings or
installation. All protected installed/log/optimizer/settings hashes are
unchanged. Evidence is `visual-angle-acceptance.json`; no full suite ran.
The original
Reset/depth/MSAA gates, genuine current animated scene/input correspondence,
specific shoulder emission and uncapped static/rotation/native-only FPS
comparisons remain separate requirements. Stable 120 FPS, ideally 160 FPS,
has not been verified. Component checks are never reported as game FPS.
