# Autonomous HDR follow-up — 2026-10-31

## Correctness and fresh client evidence

The account owner's requested character was entered autonomously. Credentials remain outside repository files, test fixtures and packages. Four private client logs and two pairs of diagnostic captures were collected in Crystalsong Forest. The stationary camera remained at approximately `(5011.72, -252.49, 487.93)` facing `(0.7482, 0.6610, 0.0574)`. White Crystalsong foliage is intentional and was not changed.

The preceding raw-clear-depth fix already protects horizon silhouettes. The follow-up captures contain about 1.05 million horizon pixels with depth above 0.94 and below clear depth. Their median positive difference between the final linear scene and the preceding lighting composite is zero. Only about 0.054% exceed 0.01 after the glare fix. These are adjacent frames with animation and early UI differences, not an exact subtraction of identical frames. The standalone wrong-depth control remains the decisive geometry-leak regression.

The remaining broad solar halo had a transfer-order error: the nonlinear shoulder was applied to decoded tint. It now decodes the **result** of the authored SDR shoulder once for linear HDR addition. This prevents faint tails being lifted by up to 12.92× while retaining the compact body's HDR energy. Twelve additional GPU comparisons cover four tail weights and three tints; 147,456 channels agree with the decoded SDR reference within 0.000924. The complete sun oracle passes 28 cases per backend, including wrong-depth and excess-energy negative controls. The compact body remains at eight times relative diffuse white in that fixture; it is still tone mapped to the configured peak.

In the private captures, median positive sky contribution falls from 0.17236 to 0.02612; mean positive contribution falls from 0.32624 to 0.20544. Camera geometry is unchanged, but sunlight/time and adjacent-frame differences remain, so this is supporting evidence rather than a controlled energy ratio. Desktop screenshots clip HDR into their SDR image range and are not panel-nit measurements.

## Removed compatibility work

The fresh log identifies two active fixed-function texture stages, both using `MODULATE(TEXTURE, CURRENT)`, source-alpha blending and texture coordinate set zero. A null-stage shortcut alone does not optimize those draws: both textures are actually present.

Supported two-stage `SELECTARG1`, `SELECTARG2` and `MODULATE` combinations now shade their original geometry directly into the HDR target. UV sets zero and one are supported independently. Temporary results, extra active stages, coordinate transforms and unsupported argument modifiers retain the compatibility path. Failed shader creation also retains that path. A null texture in `COLORARG1=TEXTURE` terminates a stage according to the D3D9 fixed-function contract; other missing-texture cases retain the fallback.

The original one-texture shader remains byte-identical and retains its two-register save/restore. The two-texture program is created lazily and uses four registers. In the fresh client run, compatibility layers and layer pixels remain zero; the direct two-texture counter increases by exactly two per world frame. This avoids allocating the former reusable 4K UNORM/MSAA8 layer pair, whose pixel payload is about 284.8 MiB, as well as its per-draw clear, resolve and fullscreen composition.

The numerical oracle compares 108 nontrivial combinations against the old UNORM compatibility layer on each backend: two UV sets, nine RGB/alpha operation combinations, three opacities and MSAA off/four. Maximum RGB error is 0.00879 against a 0.025 tolerance, allowing the old layer's UNORM quantization. Five unsupported-state cases verify fallback. The comparison is of RGB; the direct path retains the native blend alpha equation. Constants and the original null pixel shader are restored. The full HDR Frame suite passes 152 cases per backend.

| Two draws at 3840×2160, MSAA8 | DXVK 2, layer → direct | DXVK 3, layer → direct |
| --- | --- | --- |
| 64×64 geometry | 0.17743 → 0.00030 ms | 0.18776 → 0.00053 ms |
| Fullscreen geometry | 0.79251 → 0.30193 ms | 0.74564 → 0.32314 ms |

These GPU timestamp medians cover only the specified compatibility workload. Fullscreen layer size is independent of the original draw's coverage. These figures are not whole-client savings.

## Stationary client comparison

DXVK 3, 3840×2160, MSAA8, automatic exposure/peak, paper white 203 nits, UI 160 nits, bloom one and expansion zero. The display reports a 1500-nit peak. All normal effects remain enabled; menus, capture frames, transitions and initial warm-up are outside the selected windows. Two consecutive SDR windows are bracketed by HDR windows. Live weather, sunlight, GPU clocks and background activity remain uncontrolled.

| Window | Renderer frames | Median of 120-frame mean times |
| --- | --- | --- |
| HDR A1 | 11400–14400 | 9.82 ms |
| SDR B1 | 21434–26594 | 9.35 ms |
| SDR B2 | 26595–31754 | 9.25 ms |
| HDR A2 | 36578–44978 | 9.91 ms |

The phase medians imply approximately 6% remaining HDR overhead, about 101 versus 107–108 FPS. This does not reproduce the original 110→70 report and does not prove every scene meets the performance target. The client run used DLL `0572e17ff74a0e50d63371e353a2e76a2627cd7309a6e3172d3f4c5aa19c101e`; final offline acceptance additionally validates the split single-/two-texture shader selection and optional-shader fallback.

Private evidence is under `.local/hdr-autonomous-20261031`: the stationary log, explicit frame markers, validated summary, capture comparison, frame/sun oracles and component benchmark. Raw account/game logs and captures are not published.

The packaged DLL (`ac96124a3e21e1289b3619c92e414c6619d058a402d99e4f7c869743c4d45479`) was subsequently installed and entered the same character again. Its fresh `client-package5-final.log` confirms active HDR, zero compatibility layers/pixels, two direct two-texture draws per world frame and no extension faults. This final HDR-only run is separate from the bracketed comparison above. Ctrl+R is visible; the user's unit frame physically overlaps its left edge, rather than a world pass erasing it. Renderer settings remain byte-identical to the pre-test copy.

## Further optimization directions and acceptance gates

1. **HDR resolves, target bandwidth and presentation.** The isolated 4K/MSAA8 HDR post path without an SDR mirror costs about 0.39–0.40 ms. A forced MRT mirror adds about 0.17 ms and is already omitted where permitted. Investigate remaining resolve/target ownership and state traffic before adding variants. Require MSAA edge equivalence, logical readbacks/partial writes, both Present paths, device recovery and measured timestamp improvement.
2. **Fog marching and cloud generation.** Fresh medians are about 1.20 ms and 0.63–0.68 ms respectively in both SDR and HDR. They are major total-frame costs, not evidence that HDR alone causes them. Candidate work must preserve frozen-capture radiance, actor/terrain occlusion, temporal stability and moving-camera behavior. Reducing sample counts or resolution is not an equivalent optimization without a separately agreed quality tradeoff.
3. **CPU draw admission, state inspection and command recording.** The two-texture path still validates active state. Profile this CPU cost separately before adding a cache. Any state cache must invalidate on native setters, state blocks, Reset and unknown/failing reads; require the boundary negative control, state restoration and a measured CPU win as well as GPU equivalence.

Two new performance mechanisms are implemented here: null-stage termination and direct two-texture composition. The former is validated but does not match the expensive captured draws; the latter removes them. Splitting shader variants prevents a regression introduced while implementing that second mechanism and is not counted as a third independent optimization. The solar transfer change is a correctness fix. The 113 oracle cases are tests, not 113 optimizations. **Sixty independently validated optimizations per direction have not been established.** None of the unimplemented investigations above is presented as validated work.

## Reproduction without the game

Run `python tests/verify_hdr_pipeline.py --output <isolated-output>` for the production build, composition graph, Frame/sun oracles and proxy matrix. Use `python tests/verify_hdr_frame_gpu.py --dxvk2 <dll> --dxvk3 <dll> --benchmark --output <isolated-output>` for component timestamps. Run `python tests/test_celestial_composition_gpu.py` once per backend using `NORTHLIGHT_TEST_D3D9`. GPU tests require WoW to be closed.

For an existing log, use `python scripts/analyze_hdr_log.py <log> --window HDR_A1:11400:14400 --window SDR_B1:21434:26594 --window SDR_B2:26595:31754 --window HDR_A2:36578:44978 --output <summary.json>`. Choose frame bounds from the actual run; do not reuse these numbers for another session. The parser rejects ambiguous windows, insufficient complete intervals and counter resets; `python tests/test_hdr_log_summary.py` validates these safeguards. Independent stage medians must not be summed.

The complete HDR acceptance graph and five celestial replay families pass on DXVK 2.7.1 and 3.1.1. CPU boundary ownership, its stale-copy negative control, diagnostic gating and the log parser pass. Day/night/interior coverage, full-frame performance acceptance across scenes and physical two-monitor migration remain open.
