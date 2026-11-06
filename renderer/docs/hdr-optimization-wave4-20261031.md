# HDR post and presentation follow-up — 31 October 2026

This follow-up preserves the complete feature assembly and the previous solar-tail, raw-depth horizon, native Ctrl+R and direct two-texture UI fixes. It starts from `edac930a`; the installed `.6` DLL is retained as the reference. Native MSAA, quality, sampling budgets and intentional Crystalsong foliage are unchanged.

## Accepted reductions

Two independent changes are implemented:

* Compute the display shoulder's fixed span and exponent coefficient once on the CPU. The output shader retains the existing curve and its zero-span fallback; `HdrWorldLinearOnly` drops from 53 to 50 static SM3 slots. The extra constant upload is included in submission timings.
* Use a dedicated horizontal bright-extraction shader. The first bloom pass no longer selects between extraction and an ordinary blur for each of its five taps; static slots fall from 71 to 65, and temporary registers from five to four. The original generic bloom shader remains byte-identical as the filter oracle's reference. Sample positions, weights, downsampling and clamping are unchanged.

These are two mechanisms, not 2776 optimizations. The earlier wave contains five accepted mechanisms. Sixty independently validated optimizations per direction have not been established.

## Numerical and integration acceptance

The production x86 build, nine-workload composition graph, sun regression and production HDR/SDR/MSAA8 proxy matrix pass on DXVK 2.7.1 and 3.1.1. HDR Frame now passes 2776 cases per backend, including 2520 independent CPU grading comparisons across three white levels, seven display peaks, source values around white, five contrasts and three exposure values. Alpha remains exact. The 56 filter cases include horizontal/vertical passes, metering reductions, negative values and the bright-extraction threshold.

Final DLL SHA-256: `257bfc80924a663a1a7ef1774fba95df6da4b98e55dc8ca0040d3a32f7aaf60e`. Private acceptance is under `.local/hdr-autonomous-20261031/wave4-final-profiled`. The complete post output from the saved 4K HDR scene is byte-identical to the reference on both backends and MSAA off/eight. This frozen scene does not contain every possible source value; the independent grading/filter tests supply broader numerical coverage.

| Frozen 4K post component | Reference | Candidate |
| --- | --- | --- |
| DXVK 2, no MSAA | 0.15386 ms | 0.15096 ms |
| DXVK 2, MSAA8 | 0.52832 ms | 0.52070 ms |
| DXVK 3, no MSAA | 0.14927 ms | 0.14682 ms |
| DXVK 3, MSAA8 | 0.51970 ms | 0.51729 ms |

The measured differences are small. Earlier paired runs vary; neither the component improvement nor static slot reduction proves a client FPS gain. Source/header, harness, executable, backend, input and output hashes are recorded in `wave4-final-benchmark/validation.json`. The replay excludes original per-sample game edges, lighting generation, native UI and OS presentation.

## Rejected presentation change

A separate experiment retains the original MSAA8 scene/UI surface, resolves it explicitly and presents a single-sample scRGB chain. Sixteen native-MSAA/size/buffer-count controls pass per backend, including exact resolved bytes and injected resolve failure. Five alternating-order 4K post/UI/Present blocks instead regress: median DXVK 2 0.75608 → 0.79791 ms; DXVK 3 0.78513 → 0.83236 ms. These intervals include presentation waits and CPU gaps. The experiment is removed; the shipped output-chain implementation is unchanged.

The reusable presentation stand records native-MSAA alpha-edge and lifetime controls separately from optional timings. Its final production control passes 16 cases per backend, including 12 cases with actual mixed MSAA edge samples. It reports the number of explicit-resolve failure cases actually exercised; that number is zero for the retained direct-presentation implementation.

```text
python tests/verify_hdr_present_gpu.py --reference-ref edac930a --dxvk2 <dll> --dxvk3 <dll> --output <isolated-output>
```

Add `--linear-capture <private-hdr-linear-scene.fgr>` for five alternating-order post/UI/Present timing blocks. Only `hdr_output.h` differs between the presentation variants; Frame and shaders are shared. GPU tests require WoW to be closed.

## CPU attribution

New sampled `CPU HDR` diagnostics distinguish admission outside the effect boundary, the boundary's effects/finish work, HDR finishing itself and transfer with the native draw submission. Finish is nested inside the boundary, and transfer includes native submission: these fields must not be summed or presented as pure HDR overhead.

The timers use the existing latched Diagnostics/RenderProfile sample and are absent from quiet/untimed/SDR paths. The CPU boundary oracle executes all eight combinations of sample/untimed/HDR flags with a counting clock and verifies retained native submission. The log parser separates these fields from CPU and whole-frame GPU profiles. Diagnostic gating, source ownership and its stale-copy negative control pass.

## Installed package and fresh client comparison

The exact validated DLL was packaged as `Northlight-0.3.216-test.20261031.7-Windows.zip` (19,736,122 bytes, production snapshot `9aa3f77f`). Archive SHA-256: `9cc9a8b40a56f534a8696075a70eb6810d818a7fa1fb403785a423d685e8a526`. Package verification passes and packaged/installed/offline-tested DLL hashes match.

The requested character entered autonomously. Stationary Crystalsong HDR/SDR/HDR windows use DXVK 3, 3840×2160, native MSAA8, automatic exposure, bloom and ordinary UI. Settings were restored byte-identically; menus, switches and warmup intervals are excluded. No other GPU stand ran concurrently.

| Median measurement | HDR A1 | SDR B | HDR A2 |
| --- | --- | --- | --- |
| Renderer frame bounds | 40921–43321 | 55460–66380 | 78657–88617 |
| Complete 120-frame intervals | 20 | 91 | 83 |
| Interval mean, ms | 8.9355 | 8.3340 | 8.9120 |
| Equivalent FPS | 111.91 | 119.99 | 112.21 |
| Whole-frame GPU timestamp span, ms | 8.7900 | 7.4160 | 8.7555 |
| GPU tail, ms | 1.329 | 0.829 | 1.317 |
| CPU HDR admission, ms | 0.0510 | — | 0.0523 |
| CPU HDR finish, ms | 0.0197 | — | 0.0199 |

HDR FPS remains 6.5–6.7% lower (6.9–7.2% longer frame intervals). The GPU timestamp span differs more, by about 18%; it includes CPU submission gaps and is not pure GPU busy time. Live weather, solar position and clocks remain uncontrolled. Independent stage medians must not be added. These observations do not attribute the session difference from `.6` to two small shader changes or establish the performance target.

Recurring two-texture UI stays on the direct path; compatibility layers and pixels remain zero. The saved comparison log SHA-256 is `8cddae00c75f93c486cc85782e25752821053edab75a54eade2fc624e3f3baf2`. Private evidence: `client-package7-abba.log`, `package7-markers.json`, `client-package7-summary.json` under `.local/hdr-autonomous-20261031`.

The agreed target remains at most 5% additional GPU-frame time per representative scene. It is not met. Day/night/interior acceptance and the deferred physical two-monitor case must not be inferred from this outdoor comparison.
