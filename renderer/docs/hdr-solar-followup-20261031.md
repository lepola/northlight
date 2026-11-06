# Visible-sun capture and late-veil transfer follow-up

The `.7` client session entered the requested character autonomously, completed the stationary [HDR/SDR comparison](hdr-optimization-wave4-20261031.md), then changed only the camera for a visible-sun capture. The character did not travel. These camera frames are excluded from the stationary comparison. Private game captures and account information are not published.

## Capture findings

The first upward view was obstructed by the animated mount and did not adequately contain the compact solar core. A subsequent first-person view contains the actual native-direction sun: approximately `(0.228, 0.228, 0.947)`, rather than the lower lighting-source direction printed in the general world-camera log. Native and rendered tangent radii both equal approximately `0.0416426`.

The FP16 pre-display scene is finite. Its maximum RGB is approximately `(23.766, 21.781, 22.406)`; automatic exposure is about `+0.260 EV`. Pixels whose maximum RGB exceeds one occupy 0.735% of the frame; those exceeding two occupy 0.273%. Using the logged body bounds, the median maximum channel falls from about 22.75 inside half a body radius to 0.758 at 1–2 radii, 0.469 at 2–4, and 0.276 at 4–8. This distinguishes the compact emitter from the broad tail. It does not measure panel nits or prove occlusion in every scene. SDR-converted desktop screenshots alone cannot establish the HDR bright-area size.

Private capture: `session-14540-32033875-0/capture-1-hdr-linear-scene.fgr`; numerical summary: `.local/hdr-autonomous-20261031/package7-visible-sun-summary.json`. The complete session log SHA-256 is `d07fa768bdefce75826b27208758f9b24c1986ec17da4827d925a2e28e2413f8`.

## Late-veil correctness fix

`CelestialVeilPS` still applied the nonlinear shoulder to a decoded tint. Its HDR output now decodes the authored shoulder's result, matching the corrected sky halo. The ordinary SDR transfer is retained. The main linear HDR pipeline already omits this separate veil to avoid duplicate bloom; this fix closes the alternate FP16 shader path and is not claimed as the explanation of the captured solar core or an FPS optimization.

The new independent GPU oracle fails on the previous production shader before the fix. With the fix, both DXVK 2.7.1 and 3.1.1 pass 43 sun cases, including 147,456 veil reference channels across white/warm/blue tints and four weights. Maximum reference error is `0.000923097`; the retained wrong-order control reaches `0.965911`. Alpha is exact; a completely hidden source contributes zero over ordinary geometry and clear sky. The existing raw-depth horizon, compact source and excessive-energy controls remain active.

The production build, nine-workload composition graph, 2776 Frame cases per backend and production HDR/SDR/MSAA8 proxy matrix pass. The sun runner records source, generated production header, harness, executable, backend, compiler and counterfactual hashes. Private acceptance: `.local/hdr-autonomous-20261031/wave5-final`.

## Rejected bloom experiment

Reordering bright extraction to calculate its common scalar before multiplying RGB passes numerical acceptance but leaves static slots and temporary registers unchanged. The paired visible-sun replay shows no consistent timing improvement: DXVK 2 no-MSAA/MSAA8 `0.155051/0.547136 → 0.155157/0.547285 ms`; DXVK 3 `0.150912/0.549397 → 0.153365/0.541867 ms`. It is reverted and not counted as an optimization.

## Remaining measured priorities

1. Final HDR grading, MSAA target traffic and presentation: the `.7` tail is about 0.49 ms longer than SDR. Use the native-MSAA presentation stand and captured post replay; preserve per-sample edges, logical readbacks, UI and recovery. The previously tested explicit resolve was slower and is not shipped.
2. Shared fog/cloud work: FogMarch is about 0.90 ms and FogClouds about 0.61–0.62 ms in both modes. These dominate effect generation but do not explain the HDR-only delta. Preserve numerical radiance, sampling budgets, occlusion and temporal behavior when testing reductions.
3. CPU admission/state inspection: measured HDR admission is about 0.05 ms and finishing about 0.02 ms. Require measured savings and invalidation/failure controls before introducing state caches. Finish is nested inside boundary time; transfer time includes native submission.

There are seven accepted mechanisms in the preceding two optimization waves. This follow-up adds a correctness fix and rejects a performance candidate. Sixty independently validated optimizations per direction have not been established. The remaining full-frame overhead, representative night/interior coverage and deferred two-monitor test stay open.

## Final installed package confirmation

The complete `.8` installer contains production snapshot `80f322d0`; archive SHA-256 `d5860e5e7059e09f9da25ef1c153d648c94ed389d0bbc1ef3adbc2f71ec3b32f`. Its DLL SHA-256 is `aba60c2ec6b8b4b5bad4a5f9733a4d584d456fc22e5ca0accaa340c22fdcf905`, matching the offline-tested and installed bytes.

The requested character subsequently entered the world autonomously on this exact DLL. The final session confirms active scRGB, zero compatibility layers/pixels in all 86 work samples, and no HDR frame/conversion or extension faults. Ctrl+R remains visible and renderer settings retain their pre-test SHA-256 `3b9c1ee874711e77c08f679914a2edf58cfac1bc18fefe7853d57b2baa251f84`. The camera was returned approximately to its ordinary third-person view and the client closed to flush its log.

Private `client-package8-complete.log` is 8,057,616 bytes, SHA-256 `1fe7cb944cd9baa3efcc1d39498378e4f13f9e14aa0de9336bcf88a7b4d7d53d`. This final run is HDR-only, includes loading and camera changes, and supplies no new bracketed HDR/SDR performance comparison. It does not replace the `.7` marked windows.

## Transfer and final-raster experiments

A subsequent six-case oracle checks all 256 SDR code levels in ARGB8 and XRGB8, varying alpha and channel combinations, with legacy expansion zero/one/two. It compares the integer inputs against equivalent floating-point inputs and independently evaluates the standard transfer formula with expansion off. On both backends, production output matches the float-input control exactly; maximum independent RGB error is `0.000487784`, and alpha quantization error is `0.000486374` (within one FP16 step). Incoming sRGB sampler state is restored. The complete Frame suite now passes **2782 cases per backend** without changing the `.8` production DLL.

Hardware sRGB sampling was tested as a replacement for the explicit decode only for eligible ARGB8/XRGB8 inputs. At input `254/255`, both DXVK backends produce `0.9921875`, versus the independent result `0.9911021` and current FP16 result `0.9907227`. The hardware candidate exceeds the unchanged `0.001` absolute transfer budget. Its compiler slot reduction and mixed preliminary timings do not override numerical rejection. All production edits are reverted; private candidate headers, shader, failing executable and hashes remain under `wave6-srgb-rejected` and `wave6-srgb-frame`.

Disabling multisample rasterization only for the final full-coverage grading quad also preserves the frozen output bytes but gives no consistent timing benefit. The 4K/MSAA8 captured post interval changes `0.548651 → 0.553024 ms` on DXVK 2 and `0.546837 → 0.541291 ms` on DXVK 3. This candidate is reverted too. These isolated intervals exclude game lighting generation, original per-sample edges, native UI and presentation. Private evidence: `wave6-raster-benchmark`, `wave6-raster-rejected`, and final production controls `wave6-decode-final`.

Neither rejected candidate is an accepted optimization. The seven-mechanism count, measured client overhead and outstanding acceptance items above remain unchanged.
