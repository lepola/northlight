# HDR validation — 2026-10-31

Implementation follows [the agreed specification](hdr-pipeline-spec.md). The source and compiled shaders are present; `renderer/frd9.dll` is built locally. Offline tests use isolated folders. The client was subsequently launched for visual and performance acceptance; the first live builds exposed the failures recorded below.

## Environment and coverage

Windows, RTX 4090, packaged DXVK 2.7.1 and 3.1.1. Native DXGI reports one physical 3840×2160 HDR display, with peak/full-frame metadata of 1500 nits when HDR is enabled. Metadata is an application input, not a panel measurement.

| Check | Result |
| --- | --- |
| Production proxy Windows HDR off/on/off/on/off, both DXVK | Passed; no device Reset; live default-pool sentinel preserved; original Windows HDR state restored |
| Integration: 14 modes × 2 backends | Passed; both Present paths, SDR, HDR, grading, triple buffers, MSAA 2/4/8, 4K, Reset/resize, logical readbacks, alternating scene/UI generations |
| GPU Frame acceptance, both backends | 31 cases passed: diffuse/peak independence, hue, own world/fog/material headroom, expansion, AE bounds and T90 at 2/30/60/144 FPS, reset history, SDR MRT, MSAA, fixed-function legacy layer, scale-cache stability, screen blending |
| Expansion / own lighting separation | Passed: changing expansion changes the legacy base without increasing Northlight lighting coefficients |
| Celestial shader families | Passed: 132 cloud-order cases plus occlusion, sky restore, bloom and fog/halo families; HDR sun reaches relative radiance 8, synthetic moon 1.02984; clouds retain linear ordering |
| Real settings menu, private SDR/scRGB targets | Passed: UI whites 160/320; direct calibration reaches scRGB 18.75 (1500 signal nits) independently of UI white; state and transforms restored |
| Settings service | Passed: transactional migration, failure before marker, one-time reset, defaults, per-display isolation, stale-profile rejection, preview/Revert, commit/conflicts |
| UI admission CPU oracle | Optimized + ASan/UBSan passed; negative control reproduces the old stale-scene boundary problem |
| Other compatibility | 1000 production live-setting cases; retained 14-case legacy Reset fixture; original three SDR composite binaries remain byte-identical |
| Build and generated inputs | Passed; manifest source hashes match; `git diff --check` clean |

The full integration matrix's largest absolute error was 0.00171376701 scRGB including MSAA edge coverage. Exact calibration and timing properties have their own dedicated numeric assertions.

## Performance and allocations

GPU timestamp microbenchmark, median of three alternating batches after warmup, 48 frames per batch. This measures decode, AE, bloom and presentation only. It excludes game drawing, world lighting, early sky duplication, UI geometry, OS composition and CPU time; it cannot establish the specification's 5% full-frame target.

| Resolution | DXVK 2 old transfer / linear post, ms | DXVK 3 old transfer / linear post, ms |
| --- | --- | --- |
| 1920×1080 | 0.020629 / 0.064427 | 0.022123 / 0.052800 |
| 2560×1440 | 0.031787 / 0.082155 | 0.030677 / 0.074709 |
| 3840×2160 | 0.064789 / 0.161920 | 0.075328 / 0.161792 |

A non-MSAA RGBA16F 4K target is 63.28125 MiB of payload. The pipeline retains one full-resolution linear scratch target, small GPU metering/history targets, the additional FP16 output chain, and an optional legacy compatibility layer. It reuses existing half-resolution AO targets for bloom when available; otherwise it allocates two half-resolution FP16 bloom targets. Full material composition uses its existing color intermediate in FP16. Samples and buffer counts multiply output payload. The menu reports owned frame/output payload separately; neither that value nor the table is a residency or process-memory measurement.

## Evidence and remaining acceptance

Machine-local reports:

- `.local/hdr-frame-cache-blend-accepted-20261031/validation.json`
- `.local/hdr-calibration-menu-fixed-20261031/overlay-gpu/validation.json`
- `.local/hdr-post-benchmark-final-20261031/validation.json`

Initial offline-tested DLL SHA256: `cec03ce117fbb0ffeee7a7780653ea9d8a248c9b977c1803d831bd93f1368eca`. Build source identity: `17c02fa56ce68fc6c7a08b7ef4c31ea0ea437c7ea6575c5ac019473d8728635e`. Both final reports verify this exact DLL:

- `.local/hdr-shipping-matrix-20261031/validation.json` — 28 successful modes.
- `.local/hdr-shipping-lifecycle-20261031/validation.json` — both backends, transitions 0/1/0/1/0, Windows HDR restored.
- `.local/hdr-accepted-20261031/validation.json` — compact aggregate record.

Two-monitor movement is explicitly deferred by the user: this machine has no second display. HDR↔HDR profile switching and hotplug therefore have automated service/state coverage but no physical two-display acceptance. A client session is still required for day/night/interior visual references, Native spell/UI ordering during actual travel and Alt-Tab, and whole-frame performance/p95. The authored AE reference and restrained source gains need that visual acceptance. Wine/Linux and exclusive fullscreen are outside the verified Windows phase. Original game spell/particle pre-clamp HDR remains the separately agreed later phase.

## Live regressions and follow-up

The client exposed two defects missed by the initial offline fixtures. The horizon depth (~0.9985) was incorrectly treated as sky by a mask normalized against the main world band's 0.94 maximum. The first capture contained 2,092,125 geometry pixels in that mistaken mask. HDR now preserves native horizon geometry and replaces only actual depth-clear sky. The updated 35-case GPU fixture passes on DXVK 2 and 3. The 98fe24b9a09fe6be8b157148d73692b1524a41bbf91d56f1f355bb6fe6521ee7 DLL was installed, and its 28-mode proxy matrix and live Windows HDR transitions passed. The UI fallback learner also resumed after HDR Present fallback stopped hiding missed boundaries. This restored the learned UI boundary in the game log. The white Crystalsong trees are intentional game content, not a defect.

That client session still fails performance acceptance: the user's HDR toggles produced ~9 ms SDR versus ~14–15 ms HDR with the same camera and effects enabled. A newly supplied capture and log were preserved in `.local/hdr-perf-ui-20261031`. One representative pair (frames 7620/7747) shows 7,614 versus 2,320 extension D3D calls; HDR added 45 target clears and many whole-target resolves. The post-only microbenchmark did not include those per-draw compatibility passes and therefore did not reveal this cost.

The follow-up removes native horizon replay (it carries no owned HDR source) and converts patchable screen-blend draws directly on their original geometry. The prior compatibility layer already mapped these sources to linear additive light; replacing the full-screen layer preserves that policy. A 4K/MSAA8 negative-control benchmark of 32 small sprites measured 3.132/2.831 ms for the old layer versus 0.0138/0.0140 ms for direct geometry (DXVK 2/3). These are isolated draw costs, not a claim about client FPS. Source/alpha/restore assertions and the 35-case GPU fixture pass on both backends.

Live draw-state evidence identified the native Ctrl+R VS `f365a0cc944f7509`, distinct from the main scalable UI VS `ebc0741194f90a39`. Its c0..c3 transform maps pixel coordinates with scales 2/width and 2/height, with depth writes off, ZFUNC ALWAYS, and the full 0..1 viewport band. The renderer now recognizes that screen-space proof independently of the dominant UI shader. World-depth names and render-target-consuming scene passes remain excluded. The user confirmed that the counter is fixed on DLL `1023a16890fc872ff742c1344376162715ca4cda99bf4c4f7f33305b18c3ab71` (source `9686ccb5461fa439b067f71f03c5f215dce419460ed821bd0b6aa5b63cf38090`). The exact production boundary methods pass optimized and sanitizer CPU tests with the observed native transform and a negative world-depth case.

The same build starts HDR sky capture only when an owned on-screen celestial body is submitted, before that body's SDR draw. It no longer duplicates empty native sky frames. An initial full-resolution HDR base copy is needed only for stencil-preserved names; ordinary composition covers the full target itself. User toggles still show a residual ~1–1.3 ms frame-time cost in the logged intervals (roughly 10 ms SDR versus 11–11.7 ms HDR); this does not meet the 5% full-frame target. Logs are in `.local/hdr-perf-ui-20261031/pixel-ui/live.log` and `live-intervals.json`. No claim of complete performance acceptance is made.

A further optimization removes the per-frame SDR mirror MRT for discard swapchains. The pre-grade SDR image is generated on its first actual copy/readback/partial-write request, from the retained linear scene. COPY/FLIP swapchains retain the eager mirror. HDR-only presentation bytecode omits the unused SDR encoding. A dedicated GPU assertion checks the deferred mirror with MSAA and grading; the full production proxy matrix also checks logical readback and partial writes after UI without erasing HDR UI. The 4K/MSAA8 post-only benchmark measures ~0.54–0.56 ms with the eager SDR mirror versus ~0.34–0.35 ms without it on DXVK 2/3. This is an isolated GPU improvement; a new client run is needed to measure its whole-frame effect.

Day/night/interior acceptance and the final whole-client performance target remain incomplete.

## Published complete installer snapshot

The [complete `.3` installer](all-features-installer-20261031.md) combines this pipeline with the previously local actor-AO/material-persistence/emitter assembly. Its exact production DLL passes the standalone eight-scenario graph, 36 Frame cases per backend and 1,468 production proxy checks on DXVK 2/3. Actor AO, actor stencil and persistent-map GPU checks also pass on both backends. The one-command runner is `python tests/verify_hdr_pipeline.py --output .local/hdr-check`; authored fixtures require no private capture. The graph exercises world/material composition and native-style effects/UI, while GI/shadow/fog generation and game-thread work remain outside its timing scope.

Eligible UNORM scenes without expansion, captured HDR sky or preserved stencil pixels use the sampler's sRGB decode directly in world composition. This removes one full-resolution FP16 write and one draw. Other cases retain the explicit decode, and a failed folded composition materializes the legacy base before falling back. The graph checks reference agreement below 0.012 scRGB, unchanged alpha, finite output and the exact removed draw. A component speedup is not a claim that the game's 5% full-frame target is met.
