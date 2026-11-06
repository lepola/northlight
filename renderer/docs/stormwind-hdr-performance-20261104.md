# Stormwind HDR capture follow-up, 2026-11-04

The investigation uses `northlight-renderer.log` from process 59608 and world
capture `session-59608-210097468-0`, renderer frame 5821. The log identifies
source `eb84e040664fca049124279f3dd975dc842d84ebf2e2213fbe73214aa783fb7a`;
the installed DLL matches the preceding complete installer. This is not an
older-client report.

## Observations

The capture is Stormwind at 3840×2160, MSAA 2×, HDR enabled on an RTX 4090 with
DXVK 3.1.1. The last complete interval is 30.366 ms / 32.93 FPS. A nearby GPU
sample reports 29.685 ms total: 23.498 ms in native-scene submission, 3.719 ms
in effects and 2.276 ms after effects. Point-shadow rendering takes 0.328 ms;
the local-light and point-correction passes take 0.139 and 0.321 ms.

The same frame records 314 `StretchRect` calls. HDR compatibility work grows
to 33,011 full-resolution layers by frame 5400. The first twelve recorded
fallback draws use alpha blending and fog, with a bound pixel shader. The old
diagnostic omits its bytecode identity, so it cannot establish the exact reason
that this particular shader missed the existing direct path.

Material reuse is already effective in that sample: 90 resident draws,
11,777,460 reused bytes, zero uploaded bytes and zero new allocations. The
32 MiB resident budget remains enforced. No cache limits or visual-quality
settings were reduced to improve this result.

An external workload also prevents attributing the low frame rate solely to
Northlight. `HuntGame.exe` (PID 39672) started at 18:45:10 local time and was
still running after the WoW capture at 20:26:11. At 21:07:34, without WoW,
the GPU reported 98% utilization, 20,070 MiB used and 400.93 W. These process
lifetimes overlap; historical per-process GPU utilization was not recorded,
so the current 98% figure is not asserted for the capture instant. All timing
samples collected under this contention are excluded. The unrelated process
was not stopped or changed. See `out/stormwind-live-20261104/gpu-contention.json`.

## Lamp colour and HDR illumination

The hard teal discs around the lanterns are present before material composition.
The native scene and HDR base do not contain them. Additive-emission protection
was also masking atmospheric in-scattering per colour channel. Strong red
emission removed red fog while green and blue remained, producing the disc.
The shared compositor now separates receiver transmission from alpha coverage:
emission remains protected, but only actual alpha coverage masks scattering.
This applies to SDR, HDR, ordinary, prelit and unlit receivers, without a map or
effect whitelist.

The algebra also retains the original zero/near-zero denominator behavior.
The final compositor costs are 415→416 slots in SDR (within the existing 416
limit), 480→477 in HDR and 748→745 for combined HDR/material composition.
Neither instruction budgets nor numerical acceptance limits were raised.

An independent HDR boundary defect also prevented local illumination on
receivers above linear white: the SDR headroom expression became zero above 1.
HDR now uses the existing transported-radiance, one-stop limit; SDR retains its
existing headroom limit. This is not presented as the cause of the dark ground
receiver in this particular capture.

## What the captured shadow actually shows

The live capture already contains the character's lamp shadow. Seven shadow
cubes are ready, and the recorded point correction removes light behind the
character. A paired replay replaces the shadowed local buffer with the captured
uncorrected direct-light buffer while keeping the camera and other inputs fixed.

For receiver `(1600,1160)-(1940,1290)`, the reconstructed world result matches
the captured input with mean absolute error `4.43e-8`; material replay matches
exactly. The lamp shadow changes 10,574 final receiver pixels by more than
`1/255` in linear RGB. It survives the production HDR presentation and bloom:
maximum red differences are 1.92, 7.68 and 15.35 nits at -2, 0 and +1 EV.

The earlier replay helper omitted water, effect and rain samplers; unbound D3D9
samplers read white and erased the paired lighting delta. It now binds recorded
inputs and explicitly marks historical captures with missing masks as limited
to verified unmasked receivers. New captures export effect/emission and rain
inputs as well.

The historical capture lacks those masks and exposure history. This proves the
identified receiver's shadow survives the pipeline, not whole-scene parity,
exact physical HDR-monitor appearance, or every lamp/camera configuration.
The small contrast visible on this pavement is not evidence that no shadow map
was rendered. No artificial global shadow darkening was added.

## Bounded HDR compatibility work

The existing compatibility-layer shader already bypasses colour conversion for
premultiplied zero-alpha pixels. That branch now discards the pixel instead of
blending an exact zero contribution into the destination. Covered pixels keep
the existing branch and arithmetic. Native draws, clear/resolve operations,
MSAA, multiplicative/factor blends and destination alpha remain unchanged.

In the controlled sparse scene, surviving composite samples fall from 2,048 to
128 without MSAA, and from 4,096 to 256 with MSAA 2×. This is 93.75% less
blend coverage in that fixture, not a claimed FPS improvement. All 432 colour,
blend and write-mask cases match the reference RGBA16F output bit-for-bit on
native D3D9 and DXVK 2/3, with MSAA 0/2: 2,592 paired cases.
Dense controls retain the same sample count and exact output. The previous
shader fails the sparse-work regression. Evidence is in
`out/stormwind-hdr-sparse-final-20261104/validation.json`.

The work counter uses a non-rejecting alpha test for the controlled output
alphas to force late counting on DXVK; otherwise early fragment tests can count
discarded pixels. Image-parity draws use the unchanged production state.
Bounded diagnostics now record the failing shader's identity and instruction
count for the first twelve compatibility reports. No speculative expansion of
the shader-patching admission rules was retained.

## Regression evidence

- Independent atmospheric oracle: 12,096 cases per backend on native D3D9,
  DXVK 2 and DXVK 3, MSAA 0/2; black/tiny channels, distinct HDR base, disabled
  coverage, legacy masks, debug modes and full rain included. The original
  `3e-5` limit remains intact; maximum error is `7.63e-6`. All seven original
  shaders fail the atmospheric negative control.
- Comprehensive effect coverage: 5,632 compositor cases on native D3D9 and
  11,264 on each DXVK backend, with native-colour and mixed-order/state controls
  retained. Native RGBA is exact; maximum compositor error is `0.000879526`.
- The native-program/mirror smoke matrix passes all seven backend/MSAA
  configurations with the original transparent-sky and coverage interactions.
- HDR local-headroom oracle: SDR caps, coloured HDR, above-white receivers,
  alpha and no-lamp controls; 30 cases on each of the three backends pass,
  and previous shaders fail the above-white control.
- Actual captured receiver replay through material composition and production
  HDR presentation, with strict journal validation before replay.
- Existing GI fallback regression: 1,009 cases pass. Horizon haze, lamp-daylight,
  point-shadow scheduling, solar volume and shader freshness checks pass.
- Actual WorldRenderer MSAA 0/2 tests pass, as do 1,512 ordinary and 1,512 prelit
  brightness cases with the original limits and extreme-light controls.
- Capture-input validation rejects missing/corrupt modern exports and conflicting
  HDR transfers; the historical-mask fallback is explicit and shared by both
  capture replay drivers.

GPU readbacks in the expanded oracle are batched; no cases or accuracy checks
were removed to accommodate external GPU contention. The existing brightness
fixture likewise uses one batched readback instead of thousands of waits,
retaining its original 90-second limit. An unrelated stale rain-test WorldGI
hash was reconciled with the already-integrated QA fix `84689f198f00`; the exact
pin and independent GI acceptance remain enforced.

Local evidence is in `out/stormwind-live-20261104/log-findings.json`,
`out/stormwind-hdr-live/evidence.json`, and
`out/stormwind-shadow-live/final-pipeline/validation.json`.
The latter includes input hashes and a labelled shadow comparison crop.

`Wow.exe` remains SHA256
`99155d4d6ed4937a5ab1f5972d11e8605b986561b9f08686ab66af2c582194ec`.
Cache compatibility versions are unchanged. All rendering checks use private
offscreen targets; the game installation is not replaced during validation.
