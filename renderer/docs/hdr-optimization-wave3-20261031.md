# HDR optimization follow-up — 31 October 2026

This follow-up starts from the complete `.5` installer (`fdb37ace`). The solar transfer, raw-clear-depth horizon occlusion, native Ctrl+R ordering and direct two-texture UI fixes remain in place. Intentional Crystalsong foliage is unchanged. This document records independent changes separately from test cases and rejected experiments.

## Accepted work reductions

| Change | Removed work | Validation |
| --- | --- | --- |
| Batch stable quad state within each public HDR operation | Six repeated state setups in the seven-pass automatic-exposure/bloom finish; 240 API calls, with target/input hazard transitions retained | Hostile incoming game state, all auto/bloom/world-valid combinations, injected failure at every output transition and exact restoration |
| Bilinear metering reduction | 64 point samples become 16 exact 2×2 averages; static SM3 slots 330 → 91 | Both 64→8 and 8→1, constant/checker/impulse/noise/ramp; maximum FP32 reference error 0.000001431 |
| Dedicated vertical bloom kernel | Five point samples and the unused bright-extraction selection become four half-texel linear samples; slots 71 → 20 | Original 1,4,6,4,1 kernel, clamped edges, 1×1, odd and even dimensions; maximum FP32 error 0.000000477 |
| Neutral-contrast branch | Skip logarithm/exponent work when contrast is exactly one | Independent CPU grading formula: 270 combinations of source intensity, contrast 0.5/0.999/1/1.001/2, exposure and peak; exact alpha |
| Disabled-blend inspection | Avoid source/destination blend reads while blending is disabled, for fixed-function and programmable UI | Enabled/disabled blend, sRGB on/off, original state restored; failed sRGB read submits no draw |

The shader and UI changes are in `4ff690a2`; state batching is in `822a1d30`. The shared presentation/UI branch #94 includes `cd8e4def`. Its renderer does not use the full linear Frame pipeline; that implementation remains in #98.

These are five independent mechanisms, not 495 optimizations. Only the named reductions are established. Sixty accepted changes per direction have not been produced.

## Measurements and limits

The controlled final post-only benchmark compares identical harnesses and archived HDR headers from `fdb37ace`. It records source/header/backend/executable hashes and CPU submission independently from GPU elapsed time. It excludes the game, effect generation and presentation.

| 3840×2160 component | DXVK 2 reference → candidate | DXVK 3 reference → candidate |
| --- | --- | --- |
| HDR post, no MSAA, with legacy mirror | 0.17468 → 0.16668 ms | 0.16454 → 0.16324 ms |
| HDR post, MSAA8, without legacy mirror | 0.39832 → 0.39174 ms | 0.39087 → 0.39168 ms |
| Two UI state substitutions, no GPU draws, blend disabled | 0.000347 → 0.000319 ms | 0.000243 → 0.000236 ms |

The MSAA8 difference is small and DXVK 3 does not improve in this run. The quad-batching-only experiment reduced native CPU submission by approximately 0.001–0.0015 ms on DXVK 3; the combined final benchmark does not reproduce that CPU gain. Do not add the component figures, infer a whole-frame gain or claim the remaining HDR overhead is solved. Tiny UI timings also depend on compiler code layout and CPU scheduling.

The existing stationary client evidence still identifies FogMarch (approximately 1.20 ms) and FogClouds (0.63–0.68 ms) as major shared SDR/HDR costs. Reducing the original 8× MSAA, fog sampling budget, geometry coverage or effect quality is not part of this change.

## Rejected experiments

* Three-sample vertical bloom: hardware interpolation quantized the 1.2-texel weights. A 64-unit impulse produced 3.984375 instead of 4 in a tail sample. The four-sample version uses exactly representable half-texel weights and passes the stricter absolute-error oracle.
* Cloud march exit after its final possible emitting interval: FP16/FP32 outputs were identical across the authored controls, but a normal scenario regressed by approximately 17%. Applying it to actor variants also exceeded the existing 600-slot gate (607 slots). The production world shader is unchanged.
* Conditional omission of sRGB writes: reduced call count but repeatedly worsened the isolated two-texture CPU cost from about 0.000243 to 0.000383 ms per two substitutions on DXVK 3, including reversed executable order. These edits were removed. Disabled-blend reads were measured separately and retained.
* Branch around the complete tone-mapping shoulder below diffuse white: the 495-case numerical acceptance passes, but DXVK 3/MSAA8 post cost repeatedly rises from approximately 0.387–0.390 to 0.413–0.418 ms, including reversed executable order. Removed.
* A single oversized fullscreen triangle: the 495-case numerical acceptance passes and DXVK 2 improves, but DXVK 3/MSAA8 repeatedly regresses from approximately 0.388–0.390 to 0.402–0.403 ms. The existing two-triangle implementation is retained.

## Reproduction and evidence

Run the complete renderer build, nine-workload composition graph, HDR Frame/sun numerical oracles and production proxy matrix:

```text
python tests/verify_hdr_pipeline.py --output <isolated-output>
```

Frame acceptance passes 495 cases per backend, including 113 fixed-function UI comparisons/fallbacks, 32 state/failure cases, 25 filter cases, 16 UI-state cases and 270 independent grading cases. Sun acceptance retains 28 cases per backend, wrong-depth and excess-energy negative controls and SDR/HDR shoulder comparisons. Boundary ownership, diagnostic gating/capture wiring and log-parser checks pass.

Compare post-processing and isolated UI costs against a specific existing commit:

```text
python tests/verify_hdr_frame_gpu.py --benchmark --reference-ref fdb37ace --dxvk2 <dll> --dxvk3 <dll> --output <isolated-output>
```

The fog runner now accepts `--backend` and `--baseline-ref`, records source/backend hashes and checks the actual process inventory before GPU work. Its identical-source FP16/FP32 control passes on DXVK 2. It remains an authored fog replay, not a whole captured game frame.

Private evidence is under `.local/hdr-autonomous-20261031`: `wave3-final`, `wave3-final-benchmark`, `quad-cpu-paired`, `ui-elision-isolation`, the rejected cloud experiment and the preserved complete `.5` client log. Raw account/game logs and captures are not published. The complete installer retains all previously integrated features.

## Complete Windows package

`Northlight-0.3.216-test.20261031.6-Windows.zip` contains the complete feature assembly at `bbd1c9b5`, including the previously integrated material maps, actor AO, vegetation/emission and shadow fixes. Its DLL is the exact offline-validated integration build, not the narrower #94 renderer. Both packaged DXVK backends are retained. The package verifier passes payload/runtime hashes and installer contracts.

* ZIP: 19,734,917 bytes; SHA-256 `9c25eb1ae79ee18cb8da78009b66f407cc6b36b1131f4a0932aead1efe222bf6`.
* DLL: SHA-256 `9f566edb32f9663d64d43616731106e2d57542f5feb04fa241b4605f8e0b32fb`.

This exact DLL was installed and the requested character entered autonomously. The client reports active HDR on the 1500-nit Philips display, paper white 203 nits and UI 160 nits, without compatible UI layers. The native counter is visible. A stationary HDR/SDR/HDR comparison is recorded separately from the offline component timings.

## Packaged client comparison

The exact `.6` DLL was measured on DXVK 3 at 3840×2160/MSAA8, with the same stationary Crystalsong camera, all normal effects, automatic exposure/peak, white 203 nits, UI 160 nits, bloom one and expansion zero. Explicit frame windows exclude menus, capture frames, switching and warm-up. The complete log and raw captures remain private.

| Window | Renderer frames | Complete 120-frame intervals | Median interval mean |
| --- | --- | --- | --- |
| HDR A1 | 22801–26761 | 33 | 9.777 ms |
| SDR B | 34637–42917 | 69 | 9.016 ms |
| HDR A2 | 62823–68223 | 45 | 9.794 ms |

This run shows 8.44–8.63% additional frame time, or 7.78–7.94% lower equivalent FPS (about 102 versus 111). It does not demonstrate a whole-frame improvement over the earlier `.5` session, whose sunlight, clocks and background conditions differ. The residual HDR overhead is not solved. FogMarch remains approximately 1.194–1.195 ms in both modes; FogClouds is approximately 0.65–0.68 ms. HDR compatibility layers/pixels remain zero and recurring direct two-texture draws remain two per world frame. No HDR conversion or extension faults were logged. Renderer settings remain byte-identical after restoring the saved HDR configuration.

The marked-log SHA-256 is `7128e9dc5a3bea003bbb3f222589ae11dbd9f550ca41568fff3e2c11494f4877`. A subsequent diagnostic capture records current HDR-linear/exposure products at frame 70715; its capture frames are outside the timing windows. The existing effect-only GPU profile ends before the HDR finish/UI phase. Its totals must not be used to assign the entire client difference to fog or to add independent stage medians together. Further attribution needs coverage of that missing phase and the actual MSAA resolve/presentation path. Live day/night/interior acceptance and physical two-display migration remain open.

The log analyzer now reports the existing whole-frame GPU profile and CPU profile separately from the effect-only profile. Whole-frame GPU tail medians are 1.330 ms (HDR A1), 0.864 ms (SDR) and 1.293 ms (HDR A2). This tail includes HDR finishing and native UI; its approximately 0.43–0.47 ms difference is supporting attribution, not an exact sum with independently sampled effect medians. CPU and GPU intervals remain distinct in the JSON output. Tests verify separation, missing profiles and window exclusion.

## Frozen HDR post replay

The benchmark accepts a private resolved `hdr-linear-scene.fgr` directly, runs the production resolve/meter/adaptation/bloom/tone-map path and saves its resolved output for comparison. It records input, source-header, harness, backend, executable and output hashes. Stale output files are removed before each run. The original game per-sample edges, lighting generation, UI and OS presentation are excluded. The fixed replay uses 203-nit white, 1000-nit peak and a reset exposure history; it is separate from the live 1500-nit display configuration.

```text
python tests/verify_hdr_frame_gpu.py --benchmark --reference-ref cf2c41e1 --linear-capture <private-hdr-linear-scene.fgr> --dxvk2 <dll> --dxvk3 <dll> --output <isolated-output>
```

An identical-source control using the new packaged-client capture produces byte-identical resolved images for both executable variants, both backends and MSAA off/eight. At 4K, DXVK 3 replay costs approximately 0.16 ms without MSAA and 0.56 ms with MSAA8; DXVK 2 is approximately 0.16–0.17 and 0.56–0.57 ms. These are component costs and no improvement is claimed for an identical-source control. The capture's linear scene is finite; adjacent-frame horizon comparison has zero median positive contribution and about 0.067% of pixels above 0.01. Animation/early UI differ between these frames, so the authored wrong-depth regression remains the occlusion proof. This view does not replace visible-sun, day/night/interior or multi-monitor acceptance.
