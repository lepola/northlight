# Northlight non-M2 performance investigation — 22 October 2026

## Outcome and measurement boundary

Three parallel workers evaluated five hypotheses each. Five changes were retained: skip exactly invalid regional fog density work; skip cloud shadow work with zero direct contribution; avoid the AO early-out probe when the already-loaded centre proves full filtering is required; reuse static selection axes within a request; and reuse the preceding spatial group lookup while decoding/packing triangles. All keep the original visible computation, resolution, samples, distances, MSAA, resource lifetimes and memory policies.

The frozen game log is from installed build `840faf8`, SHA-256 `22a1407183b186cd66001ea6d0be4147a60082ee6e2fb916085d9a508f27362f`. The standalone change baseline is `ddff7cf`, which also contains the previously validated material-capture setter reuse. No game run of this combined build exists yet. The agent did not launch the game, replace the installed DLL or edit game settings.

The log SHA-256 is `b6d586124b923178c0b69f0c1598bc61ab9c59530e1b6cbb8d287609c98a0b20`, archived under `northlight-diagnostics/material-m2-normals-20261022-2042/`. Its material setting was Displacement + normal maps (3), not Normals-only (1). The next user run must establish whole-game impact; standalone percentages are component measurements, not additive FPS gains.

## Where non-M2 time goes

These medians use 20 sampled frames in the 1270–3683 interval. Full sample ranges and source identity are saved in `northlight-diagnostics/nonm2-parallel-20261022/baseline-gpu-summary.json`.

| GPU component | Median elapsed time | Action |
| --- | ---: | --- |
| FogMarch | 0.861 ms | Exact invalid-density work elision |
| FogClouds | 0.642 ms | Same invalid-density guard plus zero-direct shadow guard |
| WorldComposite | 0.540 ms | Preserved; depth/color-guided lighting and fog upsampling needs independent proof before changing |
| AOBlur | 0.331 ms | Exact centre proof skips redundant nine-read probe |
| AO generation | 0.113 ms | Kernels and sample counts preserved |
| WorldNormals | 0.083 ms | Bytecode preserved |
| GI | 0.052 ms | Bytecode preserved |

The fog pair alone accounts for roughly half of the profiled Effects interval. Terrain CPU capture costs about 0.303 ms for 701 tracked cache hits in the stationary tail; another cache would not eliminate these calls. Static-worker selection (39.242 ms) and read/decode (166.042 ms) are session totals, while the largest model job is 46.187 ms: these are background preparation/stutter targets, not per-frame GPU costs. Ordinary GameScene GPU time includes the game's own rendering and must not be wholly attributed to Northlight.

## Retained results and limits

| Change | Standalone evidence | Limitation |
| --- | --- | --- |
| WorldFog validity guard | Dense horizontal/varied ground 8.7–9.0% faster, bit exact | Short upward view about +0.006 ms |
| FogClouds guards | Dense scenes 4.5–7.4% faster; dense zero direct source 17.9% faster, bit exact | Short upward view about +0.008 ms |
| AO centre guard | Production FP16 occluded/mixed fixtures around 3–4% faster, bit exact | Open/near-one cases pay a tiny branch cost; earlier FP32-only gains were rejected |
| Static selection basis reuse | 262,144 exact decisions; 524,288-query CPU benchmark 43.2377 -> 12.3923 ms | 3.49x applies to the predicate, not the whole selection task/frame |
| Static spatial group reuse | Three real WMO decodes 3.34–4.03% less CPU; decoded and packed fingerprints identical | Cold/background improvement; no warm FPS claim |

Final shader comparison suites pass **52 fog + 162 AO cases**, every FP16 output bit identical. The AO suite includes production FP16 and FP32 inputs, odd/even grids, MSAA 0/2/4/8, threshold impulses, finite/nontrivial output and HDR bloom RGB passthrough. Additional repeated timing runs preserve identical output. The static tests cover synthetic non-adjacent groups/materials, 16/32-bit indices, alpha/cutoff/addressing, invalid inputs, request reset and three read-only real WMO assets. O2/O0 selection and the existing immutable scene/map/lifetime/concurrency regression pass.

Integrated shader generation verifies that only WorldFog, FogClouds and AOBlur change bytecode. The remaining **22 world entries and nine basic effect entries** match the baseline exactly. The integrated AO quality/reference/proof audit, four-method D3D9 validation (96 cases), fog interval/motion/cloud/temporal wiring checks pass. Final release compilation/export verification is recorded in the package manifest and diagnostics; no new runtime instrumentation pass or readback was added.

A larger skip of unused world/terrain capture is deliberately excluded. Current-frame sun/moon state alone cannot prove the complete consumer set because `render()` may adopt a new world publication and atmosphere selection may add a local/fading light after capture. A future refactor must establish consumer demand against that same publication before omitting geometry, preserve GI and diagnostics, and retain the first newly active lamp shadow. Sparse visibility grids or previous-frame inactivity are not used to remove rendering.

The next user-run log should compare stationary warmed windows at the same camera and record movement separately. Inspect FogMarch, FogClouds and AOBlur elapsed GPU time, plus static selection/read/job counters during streaming. The previous MATERIAL_BATCH_STATE and pose-envelope/fallback profiling remain in the combined DLL. WorldComposite and the capture consumer/publication contract are the next measured targets; no quality reduction is proposed.

## Reproduction and detailed worker evidence

The following three sections contain all 15 ranked hypotheses, rejected alternatives, exactness arguments, commands, timing tables and shader/source identities. Machine-readable evidence is under `northlight-diagnostics/nonm2-parallel-20261022/` and is also included with the replacement DLL where relevant.

## Direction 1: Fog GPU optimization investigation, 22 October 2026

Latest immutable input log: `material-m2-normals-20261022-2042`. On sampled frame 2159, FogMarch consumed approximately 0.863 ms and FogClouds 0.642 ms, together approximately 1.505 ms of the 2.888 ms Effects interval. These are existing game measurements; the experiments below replay standalone fixtures and never launch the game.

### Ranked top five hypotheses

| Rank | Hypothesis and evidence | Decision |
| --- | --- | --- |
| 1 | Both fog loops compute regional density, height policy, smooth near fade and optical-depth arithmetic even when regional coverage is exactly zero or the sample is below ground. The existing `sigma > 0` test occurs only after that work. | Implemented: branch around this work when `valid > 0`. The same world-grid intervals, sample positions, field fetches and nonzero-density integration remain unchanged. |
| 2 | FogClouds performs four filtered shadow comparisons and `exp(-tauBase)` for every nonzero cloud sample even when all direct-light channels are nonpositive or the configured direct gain is nonpositive. The final shader multiplies that entire term by zero. | Implemented: skip only that direct contribution under those exact conditions. Ambient cloud scattering, base attenuation and cloud transmittance still accumulate. |
| 3 | Stop subsequent attenuation/shadow work after accumulated transmittance becomes exactly zero. | Rejected: a prototype did not establish an independent benefit on ordinary finite-density fixtures; an extra per-interval condition adds work to the normal path. No approximate transmittance cutoff was introduced. |
| 4 | Hoist shadow light-plane transforms and lattice factors out of the march. | Deferred: the existing compiler already folds constants, and reassociating the transformed sample coordinates can move shadow comparisons near discontinuities. No measured image-safe independent gain established. |
| 5 | Replace four POINT regional-field reads by hardware interpolation, or combine WorldFog and FogClouds into one march. | Rejected for this patch: missing metadata nodes require validity-normalized height interpolation; hardware linear filtering changes it. The passes also use different world-fixed 48/40 interval grids and different base-density policies. Combining or reducing them without changing results needs a separate design and proof. |

### Final implementation and invariants

Only the two fog entry points in `shaders/world_effects.hlsl` changed. The validity branch prevents arithmetic on samples that previously contributed precisely zero. The cloud direct-source branch skips four shadow texture reads and the base-transmittance exponential only when their radiance contribution is exactly zero.

The regional field still uses its four validity-aware POINT reads at every retained interval. Positive-density samples keep their previous formulas and order. No distance, resolution, step count, sample location, filtering kernel, density, lighting gain or MSAA setting was reduced. No raymarch early termination threshold or new persistent cache was added. Production host code, render targets and device state are unchanged.

The pinned compiler accepts both shaders under SM3: WorldFog 427 -> 431 static slots, 21 -> 20 temporary registers; FogClouds 439 -> 452 slots, 22 -> 22 temporary registers. Static instruction counts increase because the new branches exist; runtime savings come from skipping zero-contribution blocks. Both retain the same sampler sets and remain below the 512-slot / 32-register limits.

### Final standalone validation

**52 comparisons passed with every FP16 output component bit-exact:** 13 scenarios x 2 shaders x 2 argument orders. Each image contains 8,294,400 components (1920 x 1080 x RGBA). There were zero changed RGB/alpha values and zero nonfinite differences. The fixture uses saved scene/depth constants plus controlled regional fields, 3D noise and shadow inputs. It covers empty metadata, below-ground/out-of-field views, direct source absent, direct gain zero, long near fade, dense horizontal viewing, varying ground, the existing reduced step-budget setting and a visible-water receiver.

The validator compiles baseline `ddff7cf` and current fog source with the same recognized Microsoft d3dcompiler_47 binary. Compiler SHA-256: `4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3`. It uses an isolated compiler folder and never rewrites shared generated headers.

Each benchmark invocation alternates both shaders in ABBA order for 20 blocks; each block performs 64 warm-up draws and 64 timestamped draws. The table averages each shader's median across the two reversed-argument invocations. GPU timings are fixture-specific, not a predicted game FPS increase.

| Scenario | WorldFog GPU ms (change) | FogClouds GPU ms (change) |
| --- | --- | --- |
| 0: Captured depth / upward view | 0.040688 -> 0.046608 (+14.5%) | 0.035912 -> 0.043744 (+21.8%) |
| 1: No direct source | 0.038968 -> 0.033176 (-14.9%) | 0.035808 -> 0.036184 (+1.1%) |
| 2: Zero direct gain | 0.032520 -> 0.033544 (+3.1%) | 0.036200 -> 0.036456 (+0.7%) |
| 3: Empty regional field | 0.031352 -> 0.026200 (-16.4%) | 0.033296 -> 0.025080 (-24.7%) |
| 4: Below ground | 0.034960 -> 0.031248 (-10.6%) | 0.035864 -> 0.033224 (-7.4%) |
| 5: Outside regional field | 0.009232 -> 0.009232 (+0.0%) | 0.009256 -> 0.009272 (+0.2%) |
| 6: Long near fade | 0.017728 -> 0.017856 (+0.7%) | 0.016280 -> 0.016776 (+3.0%) |
| 7: Dense horizontal view | 0.564840 -> 0.515888 (-8.7%) | 0.520608 -> 0.497408 (-4.5%) |
| 8: Dense / no direct source | 0.448832 -> 0.404536 (-9.9%) | 0.548016 -> 0.449824 (-17.9%) |
| 9: Dense / zero direct gain | 0.459664 -> 0.408200 (-11.2%) | 0.538456 -> 0.458640 (-14.8%) |
| 10: Dense / varying ground | 0.561808 -> 0.511240 (-9.0%) | 0.529704 -> 0.490752 (-7.4%) |
| 11: Existing reduced step budget | 0.152344 -> 0.121424 (-20.3%) | 0.166016 -> 0.145968 (-12.1%) |
| 12: Visible water ends ray | 0.177552 -> 0.169320 (-4.6%) | 0.144968 -> 0.139664 (-3.7%) |

The dense horizontal fixture improves WorldFog by 8.7% and FogClouds by 4.5%; varying ground improves them by 9.0% and 7.4%. Cloud cases with no direct source or gain improve 17.9% and 14.8%. Empty regional fields improve 16.4% and 24.7%.

**Measured tradeoff:** the short captured-depth upward view regresses by approximately 0.006 ms (WorldFog) and 0.008 ms (FogClouds), including a larger first-order sample; this is not omitted. Most sparse-case changes are under 0.001 ms. The shader introduces conditional overhead when almost no march work exists to skip. The measured expensive cases, closer in cost to the logged workload, improve. No claim of an across-all-scenes speedup or a game FPS increase is made.

Additional checks passed: `test_fog_empty_intervals.py` (18,000 exact interval cases), `test_fog_motion.py` (1,800 optical path + 300 world translation cases), `test_fog_clouds_wiring.py`, `test_fog_temporal_wiring.py`, Python compilation and `git diff --check` for owned files. These wiring checks ran before regenerating production shader artifacts; the integrating build must regenerate and rerun them.

### Reproduce and integrate

From the repository:

```powershell
python tests/validate_fog_work_elision_gpu.py --backend D:/Games/335_engb_mod/renderer-backends/dxvk/dxvk_d3d9.dll --prefix D:/Games/335_engb_mod/northlight-diagnostics/session-65956-69396500/capture-1- --baseline-ref ddff7cf --compatible-compiler --output out/.svn/nonm2-fog-final
```

The reusable test extends `tests/world_pass_gpu.cpp` through an optional seventh argument. Existing six-argument AO/fog invocation behavior remains unchanged. Explicit fog scenarios require bit-exact comparison and use the longer timing sequence.

Integration (only when no compiler maps generated headers):

```powershell
python scripts/shaders/compile_world_shaders.py --compatible-compiler
```

On this host the selected-entry build rejects the recognized compatible compiler hash in its retained-manifest guard. Integration therefore rebuilds all world entries with the approved compatible compiler, verifies that exactly WorldFog/FogClouds change bytecode and all 22 other entries remain identical, and then runs the release build. The worker itself did not change production generated headers, shader binaries, build manifests, the installed DLL or settings.

### Final shader identity

| Shader | Baseline SHA-256 | Final SHA-256 |
| --- | --- | --- |
| WorldFog | `be48fbfe77fe6f2622c8c5ba4eaebf617e52479347c5e77712c01e9548cf3c3c` | `4393291ce9c3cc46806104b32452e6699a86c7369a77bfd67ac9f7ed80d17b34` |
| FogClouds | `0c6042111f58d3c53e4260509d5690711e6132c19aad500af1cecca2d1bc2c1b` | `8f896bade5f7d5df7960e942a19cc04caeaa5a18c4c84c072ebdc2c67e6daffb` |

Compiled source snapshot SHA-256: `9386a8b56053267a44d3d4736b8b052d04e88c9680227610ef83ed4a8f7e5454`. Future edits to the world source can change its full source hash; compare each final fog bytecode hash above after integration.

Machine evidence: `fog-validation.json`; raw execution log: `fog-final-validation.log`. Exploratory/rejected shader variants remain under `out/.svn/nonm2-fog` and are excluded from final check totals.


## Direction 2: AO / contact postprocessing optimization report — 22 October 2026

### Evidence and scope

The immutable `material-m2-normals-20261022-2042` log is the source for the diagnosis. In the 20 sampled all-material-mode frames 1270–3683, AO costs a median **0.113 ms GPU**, AOBlur **0.331 ms**, and all profiled effects **2.890 ms**. AOBlur is therefore the relevant target in this direction; fog and world composite are investigated separately. This is sampled elapsed GPU time, not a controlled FPS A/B experiment.

Only `shaders/effects.hlsl` changes production behavior. No new render targets, source copies, allocations, captures, GPU readbacks, material distances, effect strengths or quality settings are introduced. The game has not been launched. The installed DLL and INI files have not been changed.

### Top five hypotheses

| Rank | Hypothesis | Evidence and decision |
|---|---|---|
| 1 | Use the raw AO centre, which is already loaded, to prove the nine-read “unoccluded?” probe cannot succeed. | **Adopted.** One outer branch skips the probe only for `raw.a <= .995`, then runs the complete original 25-tap bilateral filter. Production FP16 old/new tests are bit exact; repeated occluded/mixed benchmarks improve about 3–4%. |
| 2 | Stop the existing nine-footprint probe after the first low footprint. | Rejected. The loop + break variant saves only approximately 2–3% on several occluded production-FP16 fixtures, but adds approximately 13% to the cheap open/sky path. |
| 3 | Specialize/unroll the fixed nine-footprint probe, with or without the break. | Rejected. Unroll alone produces no robust gain. Unroll + break initially appeared 9–13% faster with FP32 input, but **the production input is FP16**: the apparent improvement disappeared, with approximately 13% regression on open/sky. The FP32 result is not a production performance claim. |
| 4 | Hoist repeated Y-coordinate snap and row weight outside the five-wide blur row. | Rejected. Pixels remain bit exact, but production FP16 results stay around 0% improvement, with some noisy regressions. No source churn retained. |
| 5 | Unroll the full five-tap inner filter row. | Rejected by the SM3 budget: the pinned Microsoft compiler emits 655 static slots, exceeding `ps_3_0`'s 512-slot limit. Unrolling the probe too reaches 676 slots. |

### Selected implementation and exactness argument

The original early-out fetches nine bilinear footprints and returns the unfiltered raw AO when every footprint is at least `.9995`. Otherwise it runs the full filter. The footprint at `(+.5,+.5)` AO steps contains the centre texel with about one quarter of its weight. A sufficiently dark centre therefore proves that this existing early-out cannot be taken; the other eight probe reads and remaining probe bookkeeping provide no useful information.

The new outer guard preserves the original loop for `raw.a > .995`. Otherwise it directly reaches the original complete filter. This is an exact decision shortcut, **not an extra approximation of the filtered result**. The existing `.9995` threshold and all previous early-out behavior remain intact.

For an ideal grid the per-axis centre weight is at least `.5`; odd full dimensions reduce the positive displacement below half an AO texel, and clamp increases the centre contribution at the last texel. The CPU check covers all full dimensions 1–8192. Allowing two 1/256-texel phase errors per axis gives a conservative combined weight **0.24224853515625**. With all other AO values at their maximum 1, centre AO `.995`, and an additional two half-precision lerp roundings, the footprint stays at most **0.9992770385742188**, below `.9995`. Production AO entries saturate their alpha to `[0,1]`; bloom-only/water/sky alpha is 1. The guard uses alpha only, and HDR bloom RGB passes through unchanged.

The selected shader uses **328 static slots and 16 temporary registers**, versus 321 and 16 previously; both are within SM3 limits. The probe still contains its original nine reads when needed. The 25 filter taps, tap order and weights, plane rejection, silhouette tangent test, depth snap, sky/water rules, bloom RGB, alpha output, half resolution, and MSAA handling are unchanged.

### Reproducible validation

Final command, run from the repository root:

```powershell
python tests/validate_ao_blur_equivalence_gpu.py --baseline-ref ddff7cf --backend D:/Games/335_engb_mod/renderer-backends/dxvk/dxvk_d3d9.dll --benchmark --output out/.svn/nonm2-ao/final-center
```

The harness builds an independent hidden D3D9 device and compiles both the real previous-commit source and current source with the pinned Microsoft compiler. It does not load the game or write shared generated headers/manifests.

**162 final comparisons passed**, with zero changed FP16 output bytes and no nonfinite output. Coverage includes FP16 production raw-AO textures and FP32 source textures; even and odd grids; MSAA 0/2/4/8; flat/open areas, depth discontinuities, grooves, sky, water, mixed sky/surface, mixed occlusion, nearly-unoccluded windows, and sparse single-centre impulses straddling the new guard threshold. The FP16 fixture also explicitly verifies that all raw RGB bits, including HDR values, pass through untouched. A further 54 old/new image comparisons accompany the three repeated timing runs.

CPU reference checks passed for AO noise suppression, mean preservation, silhouette behavior, sky and RGB passthrough, the existing early-out, and the new conservative centre-weight proof. Integration regenerated the shared AO bytecode/header/manifest; the full source/manifest audits in `tests/test_ao_blur.py` and 96 approved-bytecode GPU cases in `tests/test_ao_methods.py` passed.

Selected AOBlur SHA-256: `9dae2ddd9fb74470c96ff8f56a8be0802f588051f18efbfe9e75e92326249d64`. Previous AOBlur SHA-256: `f90638a01cda1c5360231f2b3b3c11bdf7bcc381dd9a9be0cba4d846af57cab8`. Other AO entry bytecode is expected to remain identical.

### Production-format GPU timings

Inputs are full scene dimensions; the shader runs at the production half resolution. Each row is the median of three independent run medians. Every run alternates old/new order, warms up, and measures six timestamp blocks per variant. CPU submission is outside the reported shader intervals.

| Input / case | Old GPU ms | New GPU ms | Change |
|---|---:|---:|---:|
| 1920×1080, depth edge | .033440 | .032384 | 3.16% faster |
| 1920×1080, grooved surface | .034752 | .033472 | 3.68% faster |
| 1920×1080, water region | .064576 | .061856 | 4.21% faster |
| 1920×1080, mixed sky/surface | .026752 | .025760 | 3.71% faster |
| 3840×2160, depth edge | .130624 | .126432 | 3.21% faster |
| 3840×2160, grooved surface | .137632 | .131648 | 4.35% faster |
| 3840×2160, water region | .265568 | .257792 | 2.93% faster |
| 3840×2160, mixed sky/surface | .101792 | .097344 | 4.37% faster |
| 3840×2160, mixed open/occluded | .075904 | .072928 | 3.92% faster |
| 3840×2160, fully open | .015552 | .015808 | +.000256 ms branch cost |
| 3840×2160, near-one threshold field | .134560 | .135136 | +.000576 ms |
| 3840×2160, sparse threshold impulses | .128736 | .128832 | +.000096 ms |

The gain is modest and limited to pixels whose centre proves the expensive filter is needed. Open/near-one scenes retain their previous work and pay a tiny outer-branch cost. These results do not predict a specific game FPS increase. Further substantial AOBlur savings would need an independently validated way to share or precompute its geometric data; reducing its 5×5 kernel, sample count, resolution or edge guards would change quality and was not attempted.

### Evidence files

- `ao-final-center-validation.json`: final 162 cases, compiler metadata and selected bytecode hashes.
- `ao-center-repeated-results.json`: the three repeated production-FP16 benchmark runs.
- `ao-center-timing-summary.json`: medians used in the table.
- `ao-cpu-reference.json`: CPU quality checks and centre-weight proof.
- `ao-candidate-results.json`, `ao-fp16-repeated-results.json`, `ao-other-fp16-results.json`, `ao-row-results.json`: rejected experimental candidates. These are evidence, not shipped changes.


## Direction 3: Non-M2 world and static-shadow CPU optimization

Date: 22 October 2026. Baseline: `ddff7cf`. Game was never launched by this investigation.

### Evidence and scope

The frozen input is `northlight-diagnostics/material-m2-normals-20261022-2042/northlight-renderer.log`, SHA-256 `b6d586124b923178c0b69f0c1598bc61ab9c59530e1b6cbb8d287609c98a0b20`.

In the stationary tail, terrain capture costs about **0.303 ms for 701 calls**, world model replay capture **0.134–0.138 ms**, and replay bounds **0.197–0.207 ms of worker wall time** (render-thread wait varies). All 701 terrain snapshots are tracked cache hits: no byte reads, rebuilds or lifetime/revision uncertainty. Another geometry cache would duplicate existing work.

Static shadow streaming reports **39.242 ms cumulative selection**, **166.042 ms cumulative reads/decode**, and **46.187 ms maximum background model job** across the session. These are not per-frame times. Resident static uploads are zero in the warm samples. This investigation changes static worker CPU preparation, without changing drawing, shader code, MSAA, distances, budgets or cache eviction.

### Top five hypotheses

| Rank | Hypothesis | Evidence | Decision |
| --- | --- | --- | --- |
| 1 | Skip world capture when there is no current consumer | Samples have zero sun/moon RGB, no directional or point draws, and 219–678 unused model replays. NearShadowInterval=1 prevents the existing skip predicate from even examining source demand. Terrain and replay work still run. | **Deferred for correctness.** Current-frame source colors are proven, but `render()` can adopt a newly completed world publication after capture, and atmosphere selection can introduce new/fading local lights. A safe change must prove the complete light/publication set, retain GI actor capture and diagnostics, and preserve the first newly active lamp shadow. No visibility-based or last-frame skip is shipped. |
| 2 | Reuse static spatial-group lookup for adjacent triangles | Real current-area WMO `7d53…` has 419,562 triangles in 2,987 groups but only 58,808 consecutive key runs: 86.0% of tree lookups are redundant. A second WMO avoids 80.6%. | **Implemented** in both file decode and `packModel`. Full group key still includes material and all three spatial coordinates. Ordered map iteration and triangle order are unchanged. |
| 3 | Prepare static selection light-space axes once per request | The old predicate normalizes a source and constructs its basis for every far placement, although only the bounds change. The worker reports 39.242 ms cumulative selection. | **Implemented.** Lazy preparation retains the near-object fast return and exactly the previous source order, arithmetic and overlap comparisons. A private local object ends with the request, so map/light/center changes cannot reuse stale axes. |
| 4 | Remove terrain cache metadata revalidation | 701 tracked terrain hits still perform two batched VB/IB metadata checks each. This is part of the approximately 0.303 ms capture cost. | **Rejected for now.** The second stamp detects writes, pending locks and resource lifetime changes between checks. Removing it without a stronger shared-lock proof would weaken correctness. Existing unknown-resource byte verification and failure behavior remain. |
| 5 | Enlarge caches or rewrite terrain upload scheduling | Static models are resident; warm terrain reads and uploads are already zero. Movement causes new terrain/mesh generations and transient work. | **Rejected as the next fix.** Additional residency cannot remove the measured warm call overhead and would increase 32-bit address-space pressure. Current limits, memory admission, asynchronous retirement and invalidation remain unchanged. |

### Implemented changes and quality guarantees

`src/shadows/static_shadow_scene.cpp` now retains a pointer to the map-owned index vector for the previous spatial group. `std::map` insertion preserves that vector object's address; vector element reallocations do not affect the map-owned vector address. Non-adjacent groups still use the map normally. Validation and the 65,536 group limit still run whenever a new key is examined. Emission remains the existing ordered map traversal.

The same file has a request-local `SelectionVolume` that computes each active source's normalized direction, right and up axes at most once, only if a bound reaches the directional checks. It keeps the exact near fallback box, +/-640 light depth, 240-unit orthogonal reach, margin, source activity and degenerate-direction behavior. The public single-query selector uses the same predicate. No camera-dependent culling is introduced.

No render-thread D3D state, draw count, geometry, texture data, alpha threshold/addressing, shadow map resolution, MSAA, resource lifetime, memory budget or timing budget is modified.

### Validation

- `tests/test_static_shadow_selection.py`: release (`-O2`) and debug (`-O0`) x86 Windows CPU fixtures pass **262,144 exact decisions** against the frozen pre-change arithmetic. Includes near and directional boundaries, zero/tiny/axis directions, both sources, active changes and center/direction changes.
- Predicate benchmark: 524,288 queries, ten alternating measurements, identical accepted count 108,288. Release median **43.2377 -> 12.3923 ms**, **3.49x predicate throughput** (71.3% less predicate CPU). Debug also passes. This does not predict the speedup of full worker selection, whose set/map/publication work remains.
- Existing `test_static_shadow_scene.cpp` passes after both final changes: map transitions, retained immutable snapshots, file reuse, budget failure, missing files, pause/resume, timed eviction and concurrent readers.
- `tests/test_static_shadow_grouping.py --baseline-ref ddff7cf` compares the frozen implementation and current implementation, including non-adjacent group revisits, different materials, 16/32-bit indices, alpha/cutoff/address changes, failures and job-local reset. Synthetic digest is identical: `f2890bd44dba0962`.
- Three real WMO FGS inputs are read-only and SHA-256 checked before/after. Full decoded and `packModel` output digests, material identities, alpha masks/addressing, batches, index sequences and retained-byte accounting match the baseline. Release measurements alternate baseline/current across three rounds with one warm-up plus eight measured decodes per model and round.

The final isolated full CPU decode medians are:

| Real WMO | Vertices / triangles | Before ms | After ms | CPU reduction |
| --- | --- | ---: | ---: | ---: |
| `7d53da3a…` | 369,900 / 419,562 | 40.1591 | 38.6079 | 3.86% |
| `5d4e1ee1…` | 61,758 / 58,366 | 7.2594 | 6.9666 | 4.03% |
| `d81bcafc…` | 41,789 / 42,699 | 5.3695 | 5.1904 | 3.34% |

These CPU-only background measurements are useful for cold streaming and CPU contention. They are not GPU or gameplay FPS results. The final timing run was isolated after all other GPU/CPU benchmarks and static regressions completed. Eight measured decodes per round follow a warm-up; the table uses the median of the three per-round medians. Earlier paired runs showed 4.5–5.4% less decode CPU; the more conservative final result is reported here.

Evidence: `world-selection-final.log`, `world-scene-regression-final.log`, `world-grouping-final-isolated.log`, `world-grouping-validation.json` in this report's directory. No installed DLL, game settings or source FGS asset was changed. The combined x86 release build and export verification passed; the package manifest records the final DLL identity.

Final matching prepared-output fingerprints (64-bit FNV diagnostic, in addition to the source SHA-256 records):

| Asset prefix | Streaming decode | `packModel` |
| --- | --- | --- |
| `7d53da3a…` | `0243d2b00975da19` | `51934507499828f9` |
| `5d4e1ee1…` | `e4eed2224bcd540e` | `3059b6d9d2a177cc` |
| `d81bcafc…` | `781e5c9d52f4c51b` | `5deb333006c3a0cf` |

The streaming and portable packer may retain different vector capacities; they are compared separately against their own pre-change implementation. No additional GPU test was required for these CPU-only transformations: the exact prepared draw inputs remain identical, and the parent owns the combined offscreen GPU verification and release build.
