# Particle light outage investigation (#101)

Base: `02897e26f946d6628045bfae8e15b39f0713298b`. The original
[#101](https://github.com/lepola/northlight/issues/101) report is about
Thunderfury remaining visible while both its surface light and lamp-fog glow
disappear at fixed positions. No dedicated fix PR was found in the all-state
issue-number and particle-light PR searches. Related work is in #83, #92 and
#114. The screenshots show the emitter near the screen centre in Stranglethorn.

## Existing fixes and evidence

The integration base already contains `c568dea` (retain offscreen particle
centroids), `a251293` (guard the final unpaired direct-light rectangle and record
per-source submissions), and `a7b42e5` (keep current local radiance out of the
directional darkening floor and temporal history). These changes are retained.

The existing production overlap oracle reproduces a real downstream failure:
the original production shader/pipeline loses local colour at all 128 shadowed
receivers despite positive raw light. The current pipeline loses none. Both
runs cover 16 combinations and 256 receivers, with zero order difference,
maximum superposition error `0.000488281`, and zero retired-light trail.
This is an authored receiver fixture, not the original Thunderfury frame.

The new stationary particle oracle runs the production particle publication,
selection, batch/scissor helpers and shipped LocalDirect/LocalFog bytecode.
It preserves the same source identity and immediate colour for 64 frames,
then tests both passes at large map coordinates, first/last slots, and counts
1, 4, 8, 9, 12, 14, 16, 17, 24, 32, 128 and 136. Each backend passes all 144
cases: every case contains positive source contribution, and scissored pixels
match an unclipped reference exactly (475,200 checked components/backend).
System D3D9, DXVK 2 and DXVK 3 pass. No timing or game-FPS claim is made.

## Diagnostic repair

The previous source ledger stopped after its first 32 regular samples. A later
outage could therefore leave no source-level evidence. Retain that initial
admission and continue on the existing `captureSampled` diagnostic frames.
Diagnostics and RenderProfile remain required; explicit captures keep their
existing admission. Each sampled report remains bounded to eight sources.

Each selected source now records direct/fog submission independently, with
observed viewport/depth range, scissor enable/rectangle and known bits
(`1=viewport`, `2=scissor enable`, `4=scissor rectangle`). Only tracked batches
read these three CPU device states. Disabled, untracked and clipped batches
perform zero new device queries. There is no GPU readback, allocation, extra
rendering pass or quality change. Failed getters stay unknown even if a driver
writes an output before returning failure.

The summary joins capture probes/draws/accepted draws/points, published particle
count, final selected count, camera/projection, source depth near/far and viewport
depth range, full target size, water/fog availability and renderer/world frames.
Rows distinguish not-selected, not-reached, clipped, failed, zero selected RGB,
zero extinction and non-finite input. Successful nonzero submission remains
`pixel-contribution-unmeasured`; these CPU observations cannot certify shader
receiver/depth tests or physical item ownership.

## Validation and remaining acceptance

- `test_particle_light_diagnostics.py`: x86 O0/O2 executes the actual production
  helper and sampling expression, including failed getters, independent pass
  state and zero disabled/untracked getter work. Reintroducing late-session
  silence or unconditional getters fails the fixture.
- `test_local_light_trace.cpp`: x86 O0/O2 retains source/slot attribution,
  bounded rows, submission/clip/failure, unchanged inputs, and adds reason/state
  coverage.
- `test_particle_lights.py`: O0/O2 lifecycle, colour, cap/dedup and upload tests
  pass. Layout and log-format checks pass; the two new tests are registered.
  The base discovery list separately omits 14 existing tests; those unrelated
  registrations remain for integration.
- `test_particle_lighting_gpu.py`: 144 positive/scissor-parity cases on each of
  system D3D9, DXVK 2 and DXVK 3. The existing `test_local_overlap_gpu.py` passes
  both current and pinned legacy-counterexample modes.
- The isolated x86 renderer DLL builds. No shader source, binary, manifest or
  generated header changes remain.
- `test_diagnostics_switch.py` still reports the pre-existing unlabelled
  `NATIVE_LOCAL_LIGHT` site in `src/proxy/renderer.cpp`; that file is unchanged
  by this work. The assertions and allowlist are not relaxed.

The original source-ID/scene/depth/frame correspondence is absent. Available
current LOCAL_SOURCE samples have zero published dynamic rows and cannot stand
in for the reported outage. No new causal rendering fix or complete resolution
of #101 is claimed. The next reproduction can distinguish source absence,
selection loss, CPU-known zero input, failed/clipped draws and submitted draws
requiring the existing explicit lighting-input capture for pixel attribution.
No game launch, installed DLL replacement, configuration change, issue comment
or PR publication is performed.
