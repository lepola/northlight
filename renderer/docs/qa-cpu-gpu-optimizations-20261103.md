# Forty CPU and forty GPU optimizations: offline acceptance

2026-11-03. This wave contains 40 CPU changes and 40 GPU changes above reference commit [`445c1637`](https://github.com/aphelion105/northlight/commit/445c1637010b23be77b269dcbca4686753ba6bfe). The reference commit preserves the preceding 60 material and 60 CPU changes plus the BLP guard, separately documented in [material QA](qa-material-optimizations-20261102.md), [CPU QA](qa-cpu-optimizations-20261102.md) and [BLP audit](blp-compat-audit-20261103.md). Its renderer source identity is `547595df8094837b3c879a9bff2df1a0790ecb8cff6f0db867ea8ff7681ee5bc`.

The tables count specific removed or reused computations, storage operations, wakeups and draws. A shared helper used by multiple shader variants counts once. Row-coordinate reuse and row-weight reuse remove separate calculations; cube blocker search and final PCF are separate loops. Rejected experiments, generated variants and catalogue additions do not increase the count. Validation establishes correctness on the stated offline corpus; it is not 80 independently measured gameplay speedups.

No game was launched. The installed MPQs, Wow.exe and game DLL were not rewritten or replaced. GPU stands used hidden standalone D3D9 devices on an RTX 4090 with DXVK 2.7.1 and 3.1.1. Native system D3D9 reports RESZ=0 here and remains unsupported by the existing capability gate; it is not counted as a passing backend.

## Acceptance

- CPU: frozen/current binary snapshots are identical at O0 and O2: `dbe20bc5c0b7f1096ef287e78cb05024191005d5202703d16607a48a1766e7f1`. Coverage includes dither ranks/offsets, weather transitions and forced merges, scissor rectangles, guarded-page position reads, schedule/bulk state, compact indices, alpha tails, probe state and canonical list/strip geometry. Eleven additional native helper tests pass, including 10,000 solve-pool generations, overflow fallback and retirement wakeups. Another probe oracle checks 2,285,440 exact slots; two real-capture terrain tests pass. Native local-light batching boundaries pass with current shader costs (304/416 direct, 417 fog slots).
- GPU: 1,856 frozen/current FP32 readback scenarios pass: 880 on DXVK 2, 880 on DXVK 3, and 96 with the authored 512x512 snow atlas. They compare 150,371,552 pixels, including odd dimensions, full mip chains, MSAA 0/4, negative/extended RGB controls, depth discontinuities, zero-effect settings and both active/inactive branches. Acceptance is `abs(error) <= 1e-5 + 1e-5 * abs(reference)` with nonfinite results rejected. Maximum absolute error is 1.78813934e-07. The ContactBloom host optimization is tested as eight real neutral-clear cases per backend against the frozen shader draw.
- Production precipitation: 3,072 strict comparisons across 24 targets pass against all four frozen precipitation shaders: RGB8/FP16/FP32, MSAA 0/2/4/8, weather/transition/density, camera motion, foreground occlusion and odd grids. All four final precipitation binaries and WeatherLensParticle are reference-identical; no rain/snow sampling optimization is counted.
- Production lens: rain, snow, frost and sand pass RGB8/FP16/FP32 and MSAA 0/4; destination alpha, floating-name stencil, constants and device state remain preserved. Production PCSS passes both backends, 12 edge cases each, plus local target restoration, toggles, allocation backoff, shader-unavailable and retained-map fallbacks.
- Final production DLL 0.3.218 passes the official compiler/freshness/SM3 gates and release-package validation. The BLP guard is retested on this exact DLL: 30 real texture samples, O0/O2, both backends, three proxy/readback cycles per mode, idempotence, unload and unchanged input hashes. Its runtime-only bounded loop is not counted among the new 80 changes; it does not repair arbitrary corrupt texture/model formats.

Machine-readable hashes, per-entry GPU timings, test totals and the full ledger are in [the acceptance record](qa-cpu-gpu-optimizations-20261103.json). Large local logs and copyrighted client data are excluded from Git. Local evidence root: `.local/out/cpu40-gpu40-20261103/`.

## CPU changes

| ID | Source | Removed or reused work |
| --- | --- | --- |
| CPU01 | [src/sky/dither_noise.h](../../src/sky/dither_noise.h) | Reuse mirrored Gaussian kernel entries instead of repeating exp |
| CPU02 | [src/sky/dither_noise.h](../../src/sky/dither_noise.h) | Split wrapped kernel updates into contiguous add/subtract spans |
| CPU03 | [src/sky/dither_noise.h](../../src/sky/dither_noise.h) | Visit only occupied bits in cluster selection |
| CPU04 | [src/sky/dither_noise.h](../../src/sky/dither_noise.h) | Visit only unoccupied bits in void selection |
| CPU05 | [src/sky/dither_noise.h](../../src/sky/dither_noise.h) | Reuse the immutable dither offsets across texture recreation |
| CPU06 | [src/sky/weather_lens.h](../../src/sky/weather_lens.h) | Skip merge preparation when there is no collision pair |
| CPU07 | [src/sky/weather_lens.h](../../src/sky/weather_lens.h) | Avoid clearing collision scratch that is overwritten before use |
| CPU08 | [src/sky/weather_lens.h](../../src/sky/weather_lens.h) | Skip trail metadata and trig when rivulets are disabled |
| CPU09 | [src/sky/weather_lens.h](../../src/sky/weather_lens.h) | Reuse speed between tail length and trail direction |
| CPU10 | [src/sky/weather_lens.h](../../src/sky/weather_lens.h) | Reject trail length before computing cross-axis distance |
| CPU11 | [src/sky/weather_lens.h](../../src/sky/weather_lens.h) | Skip closest-point division for separated swept heads |
| CPU12 | [src/sky/weather_lens.h](../../src/sky/weather_lens.h) | Skip the snow mass scan while computing a rain-only frost target |
| CPU13 | [src/sky/weather_motion.h](../../src/sky/weather_motion.h) | Cache authored wind velocity until kind/heading/strength changes |
| CPU14 | [src/lights/local_light_scissor.h](../../src/lights/local_light_scissor.h) | Use exact half-scale constants for even-sized scissor targets |
| CPU15 | [src/gi/gi_solve_pool.h](../../src/gi/gi_solve_pool.h) | Claim bounded probe jobs with one atomic fetch-add instead of a CAS loop |
| CPU16 | [src/core/cpu_retirement.h](../../src/core/cpu_retirement.h) | Notify idle waiters after releasing the reaper mutex |
| CPU17 | [src/gi/gi_solve_pool.h](../../src/gi/gi_solve_pool.h) | Notify the sole solve owner outside the completion mutex |
| CPU18 | [src/world/terrain_indices.h](../../src/world/terrain_indices.h) | Cache typed storage pointers in compact-index iterators |
| CPU19 | [src/world/terrain_indices.h](../../src/world/terrain_indices.h) | Dispatch compact-index equality once per comparison |
| CPU20 | [src/replay/replay_bounds_schedule.h](../../src/replay/replay_bounds_schedule.h) | Replace per-packet variable modulo with a wrapping cursor |
| CPU21 | [src/replay/replay_bounds_near.h](../../src/replay/replay_bounds_near.h) | Stop the two-source ring projection after an innermost hit |
| CPU22 | [src/replay/replay_bulk_layout.h](../../src/replay/replay_bulk_layout.h) | Reuse the previous immutable bulk-layout key lookup |
| CPU23 | [src/core/material_byte_ops.h](../../src/core/material_byte_ops.h) | Scan alpha in four independent SIMD chains |
| CPU24 | [src/core/material_byte_ops.h](../../src/core/material_byte_ops.h) | Short-circuit alpha scans with a transparent prefix |
| CPU25 | [src/sky/dither_noise.h](../../src/sky/dither_noise.h) | Remove the redundant per-texel occupancy byte vector |
| CPU26 | [src/gi/probe_blend.h](../../src/gi/probe_blend.h) | Cache the proof that both retained SH copies are zero |
| CPU27 | [src/gi/probe_blend.h](../../src/gi/probe_blend.h) | Skip transpose and second comparison for unchanged invalid SH |
| CPU28 | [src/gi/probe_activation.h](../../src/gi/probe_activation.h) | Invalidate activation metadata without clearing inaccessible keys/timestamps |
| CPU29 | [src/sky/weather_lens.h](../../src/sky/weather_lens.h) | Compute each quad screen coordinate once per side |
| CPU30 | [src/sky/weather_lens.h](../../src/sky/weather_lens.h) | Compute each quad U division once per side |
| CPU31 | [src/sky/weather_lens.h](../../src/sky/weather_lens.h) | Compute each quad local displacement once per side |
| CPU32 | [src/lights/local_light_scissor.h](../../src/lights/local_light_scissor.h) | Reuse symmetric Gram entries while preserving accumulation order |
| CPU33 | [src/lights/local_light_scissor.h](../../src/lights/local_light_scissor.h) | Reject provably empty far-offscreen scissors before rounding |
| CPU34 | [src/world/position_bounds.h](../../src/world/position_bounds.h) | Hash bounds-cache keys with native-width integer arithmetic |
| CPU35 | [src/world/terrain_capture_bounds.h](../../src/world/terrain_capture_bounds.h) | Reserve bounded chunk-set capacity at the small-set overflow |
| CPU36 | [src/shadows/static_shadow_dedup.h](../../src/shadows/static_shadow_dedup.h) | Use a fixed three-element canonical ordering network |
| CPU37 | [src/shadows/static_shadow_dedup.h](../../src/shadows/static_shadow_dedup.h) | Specialize indexed versus direct shadow triangle loading |
| CPU38 | [src/shadows/static_shadow_dedup.h](../../src/shadows/static_shadow_dedup.h) | Specialize list versus strip triangle loading outside the loop |
| CPU39 | [src/shadows/static_shadow_dedup.h](../../src/shadows/static_shadow_dedup.h) | Reuse the two validated shared vertices of consecutive strip triangles |
| CPU40 | [src/core/cpu_retirement.h](../../src/core/cpu_retirement.h) | Wake the CPU reaper only when it may be sleeping |

## GPU changes

| ID | Source | Removed or reused work |
| --- | --- | --- |
| GPU01 | [shaders/auto_bump_normal.hlsl](../../shaders/auto_bump_normal.hlsl) | Reject captures without material coverage before decoding octahedral detail |
| GPU02 | [shaders/auto_bump_normal.hlsl](../../shaders/auto_bump_normal.hlsl) | Reject mismatched capture depth before normal rebasing |
| GPU03 | [shaders/effects.hlsl](../../shaders/effects.hlsl) | Color-bounce SSAO uses four unrolled pairs instead of eight-way kernel lookup per tap |
| GPU04 | [shaders/effects.hlsl](../../shaders/effects.hlsl) | SSAO fetches bounce RGB only for positive tap weight |
| GPU05 | [shaders/effects.hlsl](../../shaders/effects.hlsl) | HDAO/HBAO/GTAO strength-zero skips kernel while preserving bloom |
| GPU06 | [shaders/effects.hlsl](../../shaders/effects.hlsl) | HDAO rejects unaccepted pairs before angle normalization |
| GPU07 | [shaders/effects.hlsl](../../shaders/effects.hlsl) | HBAO rejects invalid/outside-radius taps before reciprocal square root |
| GPU08 | [shaders/effects.hlsl](../../shaders/effects.hlsl) | Medium/Low GTAO skips subtexel taps before depth reads |
| GPU09 | [shaders/effects.hlsl](../../shaders/effects.hlsl) | AO blur reuses the snapped row Y coordinate across five columns |
| GPU10 | [shaders/effects.hlsl](../../shaders/effects.hlsl) | Composite rejects sky taps before neighbor normals and ambient reads |
| GPU11 | [shaders/effects.hlsl](../../shaders/effects.hlsl) | Legacy AO composite skips disabled bloom taps |
| GPU12 | [shaders/effects.hlsl](../../shaders/effects.hlsl) | Low ASSAO zero-offset taps preserve weight sum without depth/obscurance work |
| GPU13 | [shaders/effects.hlsl](../../shaders/effects.hlsl) | AO blur evaluates the spatial row edge weight once instead of five times |
| GPU14 | [shaders/local_light_effects.hlsl](../../shaders/local_light_effects.hlsl) | Expanded cube PCF reuses its final-filter row tangent across four columns |
| GPU15 | [src/proxy/renderer.cpp](../../src/proxy/renderer.cpp) | ContactAO-off and bloom-zero uses a neutral GPU clear instead of a fullscreen contact-bloom draw |
| GPU16 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | Steady exposure skips the adaptation exponential |
| GPU17 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | Highlight expansion skips relative-luminance work when bright coverage rejects expansion |
| GPU18 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | Legacy sRGB encoding skips three power operations for all-low RGB |
| GPU19 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | Black legacy pixels skip native fog depth and color-space decoding |
| GPU20 | [shaders/surface_effects.hlsl](../../shaders/surface_effects.hlsl) | Sand film skips fine noise where broad deposit is zero |
| GPU21 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | Legacy HDR world skips identity contrast power |
| GPU22 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | HDR meter branches over SDR decode for linear input |
| GPU23 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | Legacy highlight expansion skips statistics/relative luminance below support |
| GPU24 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | HDR layer branches over decode for linear input |
| GPU25 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | HDR bright extraction uses a bounded reciprocal identity, removing one clamp per tap |
| GPU26 | [shaders/world_effects.hlsl](../../shaders/world_effects.hlsl) | Lamp fog skips empty ray/sphere intersections before integration |
| GPU27 | [shaders/world_effects.hlsl](../../shaders/world_effects.hlsl) | Horizon haze skips azimuth/power for an absent sun lobe |
| GPU28 | [shaders/world_effects.hlsl](../../shaders/world_effects.hlsl) | Expanded directional PCF reuses each row UV and tent weight across up to nine columns |
| GPU29 | [shaders/surface_effects.hlsl](../../shaders/surface_effects.hlsl) | Frost skips gradient and scene lookup outside revealed coverage |
| GPU30 | [shaders/local_light_effects.hlsl](../../shaders/local_light_effects.hlsl) | Cube blocker search reuses its row tangent across four columns |
| GPU31 | [shaders/world_effects.hlsl](../../shaders/world_effects.hlsl) | Directional base PCF reuses row UV and tent weight across columns |
| GPU32 | [shaders/world_effects.hlsl](../../shaders/world_effects.hlsl) | Directional blocker search unrolls four fixed columns |
| GPU33 | [shaders/local_light_effects.hlsl](../../shaders/local_light_effects.hlsl) | Local light correction skips baseline/shadow work for zero facing |
| GPU34 | [shaders/auto_material_height.hlsl](../../shaders/auto_material_height.hlsl) | Inferred height skips color accent math for zero accent profile |
| GPU35 | [shaders/auto_material_height.hlsl](../../shaders/auto_material_height.hlsl) | Inferred height skips green/blue ratio where red/green edge is zero |
| GPU36 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | Fixed UI skips source texture when texture RGB and alpha weights are zero |
| GPU37 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | Legacy HDR composite skips fog/depth/decode when native fog is disabled |
| GPU38 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | Exposure reset skips history fetch and adaptation exponential |
| GPU39 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | Linear presentation computes shoulder only above its threshold |
| GPU40 | [shaders/hdr_effects.hlsl](../../shaders/hdr_effects.hlsl) | Premultiplied HDR layer skips transfer for transparent texels |

## Timing and rejected candidates

CPU microbenchmarks compare the frozen and current implementation with identical work. These are elapsed batch times, not frame times:

| Workload | Before, ms | After, ms |
| --- | ---: | ---: |
| dither_ranks | 8.5330 | 5.9853 |
| dither_recreate | 8.6967 | 0.0045 |
| rain_merge | 5.5692 | 4.9075 |
| alpha_opaque | 6.0439 | 5.0479 |
| alpha_zero | 6.0435 | 0.0002 |
| canonical_triangles | 3.1028 | 2.7465 |
| strip_triangles | 9.1655 | 7.7071 |

Warm dither recreation measures immutable-cache reuse, not a recurring per-frame gain. Position scanning is an unchanged control: its SIMD experiment was slower and was restored.

GPU timestamps are diagnostic paired batches. On the final DXVK 3 synthetic matrix, representative summed batch ratios (after/before) were AO 0.838, Composite 0.906, HDAOOnly 0.864, GTAOOnlyMedium 0.773 and WorldLighting 0.767. Small HDR and PCF changes range around parity, including regressions of roughly 1-2% in some aggregate microcases. Branch savings depend on scene occupancy and settings; the suite does not prove every change faster on every workload. No gameplay FPS improvement is claimed.

Rejected candidates include slower position SIMD; per-tap SSAO/blur and cube-shadow branches; directional tap branches; unused procedural lens paths; early returns flattened by FXC into cmp; a contact-bloom branch that overflowed SM3 variants; and gradient/specialized rain/snow sampling. The authored snow atlas exposed a 0.0001399 mismatch and an extreme rain phase exposed a 2.9e-5 mismatch. Both kernels were restored instead of relaxing tolerance. GPU15 uses the existing host clear, and GPU25 is an algebraic bright extraction (horizontal bloom falls from 65 to 60 static slots). Generated bytecode, manifests, local batching costs and approved hash fixtures remain checked.

The installed custom patches contributed 162 additional hash-only M2 shader identities (4,965 retained, 5,127 total), without changing prior mappings. The extractor now emits stable LF text. These are compatibility data, not additional optimizations. Test maintenance also accounts for the installer's owned moon/sky files separately from native-light model counts.

## General suite and existing asset failures

Consolidating the full run with targeted reruns after the approved source/hash/test updates gives **252 PASS, 7 FAIL, 225 SKIP, 0 TIMEOUT**. Skips include missing host C++ prerequisites and manual GPU stands; separately executed native checks are listed above and do not turn an entire skipped Python test into PASS. This is not an all-green full suite.

All seven remaining failures reproduce using the same installed custom client on unmodified commit `f2edbdfeb228c352b333bb3240e18ca694526c91` (isolated archive, 0 PASS / 7 FAIL):

| Test | Installed-asset mismatch |
| --- | --- |
| test_dalaran_candle_geometry.py | No paired candle geometry; fixture expects at least eight. |
| test_emitter_pin_report.py | Model/skin indices exceed the expected layout. |
| test_model_light_attachments.py | Expected stock attachment template is absent for an installed model. |
| test_point_transmission_cache.py | No Dalaran rays; fixture expects 72. |
| test_spell_asset_manifest_client.py | Custom BLP native-BC mip extent differs. |
| test_wmo_lamp_emission.py | No expected emitter geometry; fixture expects 27 lamps / 216 geometry records. |
| test_wmo_lamp_runtime.py | Synthetic policy passes; installed FGL yields zero lights versus 27 expected. |

These asset tests retain their assertions; MPQs and expected asset results were not rewritten to obtain green checks. Full-run evidence is `.local/out/test-output/20261103-023806/results.tsv`; reruns are `20261103-025309` and `20261103-042541`. Baseline failure evidence is `baseline-asset-tests/test-output/20261103-025415/results.tsv` below the evidence root.

The historical precipitation oracle also differs from its embedded `f770155` shader on the pre-wave reference. Its default assertion is retained. The added `--baseline-compiled` option instead loads the explicitly selected four-shader reference and strictly compares all four production paths; the final 3,072-case result above uses commit `445c1637`.

## Reproduction and artifact

On Windows, use the configured Zig 0.15.2 outside the restricted execution sandbox, as required by AGENTS.md. Export the reference commit into `.local/out/reference-445c1637` first:

```powershell
git archive 445c1637010b23be77b269dcbca4686753ba6bfe --output=.local/out/reference-445c1637.tar
python -m tarfile -e .local/out/reference-445c1637.tar .local/out/reference-445c1637
python tests/test_cpu_gpu_optimizations.py --baseline .local/out/reference-445c1637
python tests/validate_shader_optimizations_gpu.py --baseline .local/out/reference-445c1637/shaders/compiled --backend <plain-DXVK-d3d9.dll> --size 512 288 --output .local/out/shader-oracle
python tests/validate_surface_effects_gpu.py --baseline-compiled .local/out/reference-445c1637/shaders/compiled --backend <plain-DXVK-d3d9.dll>
python tests/test_pcss_filters_gpu.py --output .local/out/pcss --backend <DXVK2-d3d9.dll> --backend <DXVK3-d3d9.dll>
python scripts/build_renderer.py
```

Final DLL: `renderer/frd9.dll` (copy named `d3d9.dll` in the local evidence `release/` folder), version **0.3.218**, **9,097,728 bytes**, SHA-256 `1e716bbf0be51152505c57d7da7313a8f9988991f43560d79c1da2a84c13b0de`. Source identity: `0258e1f76b8eb21889e0de0cdb583e8f7c67a5c8df66d90bd234f2211ba8645a`. The DLL is built locally; no new installer has been packaged and no installed game DLL has been replaced.
