# HDR glare and composition follow-up — 2026-10-31

## Capture and log findings

Three private 3840×2160 captures contain between 1.19 and 1.61 million horizon pixels with raw depth above the native world viewport maximum (0.94), but below clear depth. Normalizing that range to [0,1] classified real silhouettes as sky. Celestial source visibility, disc fragments and veil classification now require raw clear depth; repair, terrain and water masks remain in force.

The HDR halo also multiplied optical density by eight as if it were radiance. In the controlled regression scene this produced a peak of 33.5938 relative to diffuse white across the halo. The first correction bounded that energy; the [autonomous follow-up](hdr-autonomous-validation-20261031.md) additionally fixes transfer order by decoding the authored shoulder's output once, preventing lifted broad tails. The compact sun body retains its HDR source peak of eight; moon radiance is unchanged. The duplicate SDR veil is omitted in HDR, where the linear bloom path already spreads highlights. SDR retains its authored veil.

The latest client log recorded 32,348 full-resolution compatibility layers over 15,000 frames. Native fixed-function attenuation draws (`ZERO`, `INVSRCALPHA`, `ADD`) can multiply linear destination RGB directly. They now retain native geometry without layer allocation, resolve or fullscreen composition. Alpha writes and sRGB-write state are restored exactly.

Among 88 sampled HDR GPU intervals, median CelestialVeil was 0.423 ms, WorldComposite 0.428 ms and MaterialDetail 0.277 ms. Whole-frame HDR median was 10.633 ms across 89 stable intervals, but only one stable SDR interval with matching effect switches was available. These samples identify costs; they do not establish a paired whole-game HDR/SDR improvement.

## Combined world/material composition

An optional 676-slot shader keeps world color in registers through material relighting, eliminating one full-resolution FP16 store/read and draw. Devices with insufficient instruction slots or a failed optional shader creation retain the existing two passes. Diagnostic captures also retain the original passes and sampler ownership.

Legacy highlight expansion retains two passes on both backends: combining it caused a reproducible DXVK 2 regression (about 0.245 to 0.474 ms in the isolated 1080p scenario). Expansion itself remains enabled. NameMask and native unlit receivers retain their stencil behavior. In the combined path the GPU profiler attributes material application to WorldComposite rather than a separate MaterialDetail draw.

The six existing SDR/HDR composition entry points recompile to identical bytecode. No new full-resolution render target is allocated.

## Validation

The standalone acceptance runner builds the production x86 DLL and runs nine composition scenarios on each DXVK backend. All pass finite-output, exact-alpha, native counter-over-sky, zero warm allocation, draw-removal and performance checks. Maximum reference difference is 0.01171875 scRGB. Three attenuation layers remove three fullscreen draws; eligible inline decoding and combined material application each remove one more.

| Controlled workload | DXVK 2 reference → optimized | DXVK 3 reference → optimized |
| --- | --- | --- |
| World, 4K/MSAA4 | 1.292 → 0.603 ms | 0.851 → 0.587 ms |
| Materials/particles, 4K/MSAA8 | 1.596 → 1.075 ms | 1.561 → 1.108 ms |
| Sky fallback, 4K/MSAA8 | 1.998 → 1.545 ms | 2.053 → 1.664 ms |
| Legacy expansion, 1080p/MSAA4 | 0.242 → 0.180 ms | 0.242 → 0.174 ms |

These are composition workloads with controlled inputs, not client FPS. The comparison includes the earlier eligible inline-decode optimization. It excludes game simulation, command recording, effect generation and presentation. The removed veil's former 0.423 ms is a client-log observation, not an additional measured end-to-end saving.

- Actual production WorldRenderer runs on DXVK 2 and 3 with MSAA off and MSAA2. Tests exercise low caps, shader creation failure, the combined draw, diagnostic fallback and expansion fallback; full output comparison preserves NameMask and unlit receivers. The existing aggregate-memory-pressure fixture now reserves and releases its required contiguous witness explicitly, avoiding dependence on driver address-space fragmentation without relaxing admission budgets.
- HDR sun oracle: 16 cases per backend cover clear sky, horizon geometry, near geometry, source/history occlusion and output mapping. The wrong-depth control leaks through 4,096 pixels; fixed output does not. Controlled glare peak falls from 33.5938 to 0.984863, while the compact body remains eight. Bright area above 1.2×203 nits falls from 4,096 to 121 pixels in this authored scene.
- Five celestial GPU replay families pass on each backend: 21 occlusion, 132 cloud-order, six sky-restore, 18 diffuse-bloom and 27 fog/halo cases. These also cover the SDR veil and moon.
- HDR Frame acceptance passes 36 numerical cases per backend. Production proxy acceptance passes 1,468 checks across HDR, SDR and MSAA8/triple-buffer modes.
- CPU HDR/UI, NameMask, celestial veil, sun hue and command-stream checks pass; portable sanitizer suites run under Ubuntu WSL. No game launch or client modification is required.

Reproduce with `python tests/verify_hdr_pipeline.py --output <private-output>`, `python tests/test_world_screen_effects_gpu.py`, and `python tests/test_celestial_composition_gpu.py`. Backend selection for the last two uses `NORTHLIGHT_TEST_D3D9`. The acceptance runner guards against a running WoW process. Local evidence is retained under `.local/hdr-glare-20261031`; private game captures are not distributed.

Whole-client day/night/interior tuning, the full-frame HDR overhead target and physical two-monitor migration still require in-game verification. The complete installer retains the previously integrated actor AO, persistent material maps, herb/prop emission and all earlier feature work.
