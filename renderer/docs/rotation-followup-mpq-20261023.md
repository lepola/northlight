# Camera rotation follow-up: measured reclamation and reusable offline tests

This follow-up uses the user's completed 23 October run with Northlight DLL SHA-256 `e47705e79b395659a2a5650c68ac3ea732f4c49f2f8bc79adf905191f3a57255` and the externally updated wow_optimize 3.23.1, build `73fe7d16`. The renderer baseline is `de0d60f881ceae554ab319ce84275063c844f6df`. Only Northlight is modified. The game is never started by these checks, and installed DLLs, configuration and MPQs are not output destinations.

The frozen renderer log SHA-256 is `20f09c30fba1afdeb5f342fcd4f2c07bc8876cf102ffd96d856e68595b4fa0df`. Logs and raw evidence are under `northlight-diagnostics/rotation-followup-20261023-1541/`. Recorded wall-clock times use Europe/Kaliningrad.

## What the new log establishes

The previous terrain-retention fix is active: the startup marker identifies its policy, all twenty sampled terrain-cache records retain the 32 MiB limit, and frame 2985 reports `caps=full worldCaps=half terrainCaptureMiB=32`. Real buffer validation and capacity-driven churn remain: frame 1397 has 274 misses/evictions and 5.926 ms terrain capture; material-OFF frames 2032 and 2159 have 3.184 and 2.214 ms capture respectively. These are different workloads from the earlier 16 MiB run, not a controlled game-FPS comparison.

Material state changes during this run. Sampled configuration is ON initially, OFF from frame 1905, and ON again from frame 2921. Transitions occur between samples, so these numbers are bounds. OFF samples have zero material-detail GPU time and no material capture/cold work. Fog march/temporal/blur medians are approximately 0.914/0.032/0.174 ms; AO plus blur approximately 0.445 ms. GPU timestamps include resource dependencies. Material native-draw spans include the game's rendering and are not wholly incremental material cost.

Geometry preparation remains expensive on its worker: 27 publications, median 406 ms, including local geometry 157.188 ms, far-terrain load 81.077 ms, shadow plan 74.541 ms and page packing 71.114 ms. These are worker wall times, not frame blocking. Nine post-startup builds have zero preparation hits; eight also reduce source identity hits to four and immutable BLAS reuse to zero. The unconditional entry-reset path explains why usable preparation repeatedly becomes cold.

There are also producer-side pauses. Material-OFF frame 2457 lasts 248.312 ms, with 217.807 ms scene queue idle and 242.411 ms total queue idle. The accompanying external diagnostic stack samples include Lua execution and `VirtualQuery` on the game thread. This establishes a producer gap; it does not isolate addon work from hook overhead. Queue idle and query waits overlap the phase timings and must not be added to them. No optimizer code, binary or setting is changed.

## Production changes

### Preserve preparation when a small release really satisfies admission

Entry reclaim previously discarded all optional geometry owners and source revision IDs immediately. It now tries decoded source payloads first, resamples the original admission budget, then tries local optional owners if necessary, and finally executes the original complete reset if still refused. Retaining the bounded validated revision registry allows separately retained prepared geometry to remain useful after raw bytes are evicted.

Every action is based on measured current headroom. The 768 MiB entry floor, 96 MiB entry contiguous requirement, raised entry floors, 640 MiB interior reserve, shared live-allocation accounting and 16/8/8 MiB partition limits remain intact. Outstanding immutable leases remain charged. Map changes, explicit worker trim and actual global trim retain complete reset. Failure keeps the previous usable snapshot under its existing spatial rules. No crop, distance, probe origin, ray count, bounce count, material, alpha or triangle ordering is changed.

Rare `WORLD entry reclaim` diagnostics report the selected stage, current validity/VA, largest-block lower bounds, retained source/preparation bytes and unchanged required floors.

### Measure the entire memory event

The old `MEMORY guard trim ... ms=` timer started after surface/material/state-cache cleanup. The fresh frame-2985 finish costs 39.854 ms while that partial timer reports 11.160 ms; the difference cannot be assigned to an individual subsystem from that old record.

The trim timer now encloses all cleanup and reports `surfaceMs`, `materialMs`, `stateBlocksMs`, `waterStateMs` and `worldMs`. A separate event-only `MEMORY guard work ... totalMs=` encloses cap application and trim, including their nested event logging. Its difference from the trim timer is not a pure material cost. These clock reads occur only on memory events and do not add an every-frame timer. Admission and reclamation policy are unchanged by this instrumentation.

## Reproducing the behavior without another game run

The saved inputs are reusable. Ordinary policy/lifetime/clock tests need no game assets. Private MPQ extraction is performed once into a separate fixture; subsequent capture, worker and shader regressions consume saved assets and camera traces.

| Test layer | Inputs and behavior | Independent acceptance check |
| --- | --- | --- |
| Foreground capture | 36 actual ADTs, 4,608 draws, all 54 recorded material-OFF camera samples; stable VB, rewritten VB, rewritten VB+IB and a separately labelled synthetic two-layout ring | Every emitted triangle matches current source vertices/indices; holes and winding remain; exact-reuse/rebuild/read counters and true-low behavior are checked |
| Mutation controls | Changed XYZ and cyclic triangle-index rotation; deliberately stale revision tokens | Four legitimate changes rebuild correctly; four stale-token controls fail specifically at the independent MPQ triangle oracle |
| Entry pressure | Real x86 address-space sampling, reserve-only ballast held during accepted builds, a previous BVH pinned, captured eye positions | Same admitted/refused work and exact scene/plan/page/8,192-ray/5,600-probe bake outputs under unchanged admission rules |
| Memory and clocks | Actual policy snippets, immutable lease ownership, invalid/fragmented samples and recorded frame-2457 phase/idle/query counters | Refusal and lifetime checks; phase partition remains exact without double-counting queue or query waits |
| Fog image replay | Production-built regional fields and BVH depth from MPQ static geometry, plus explicit boundary/format/mask controls | Frozen baseline/candidate GPU output comparison on hidden native D3D9 and standalone DXVK devices |

The CPU capture stand uses fake D3D buffers and an explicit 110-degree candidate cone. Buffer rewrites and ring rebasing are explicit scenarios: logs do not record exact per-draw buffer offsets, stride changes, LOD or occlusion. Fixture copies and the independent geometry oracle are outside its capture-only timer. The shader stand uses stated projection/lighting assumptions and static models; it does not claim to reproduce animated game rasterization. Unsupported or missing inputs must fail or be reported explicitly.

## Measured entry-reclaim result

Three alternating baseline/current pairs use genuine production admission. Each sequence accepts the same six builds and refuses the same seventh, preserving the old snapshot. Two small-shortfall episodes start near 766 MiB available; releasing decoded payloads actually returns 6.547–6.633 MiB VA and preserves approximately 5.5 MiB prepared owners. At the deeper shortfall near 756 MiB, complete fallback still refuses exactly as before. This is observed physical reclamation, not a simulated assumption that freeing heap objects returns address space.

Including every entry-reclaim interval, serial preparation plus bake plus reclaim totals are 3754.703/3755.694/3755.958 ms before and 3572.347/3523.440/3536.449 ms after: medians 3755.694 to 3536.449 ms, 5.84% less. Preparation plus bake alone changes 3746.978 to 3532.192 ms (5.73% less). Admitted pressure-window preparation/bake median is 669.698 to 581.250 ms, 13.21% less; shadow-plan median 126.391 to 60.883 ms. A no-pressure control accepts all seven builds without reclaim and changes 3979.338 to 3981.859 ms (+0.063%, effectively unchanged). Initial entry sampling, ballast setup, oracle bookkeeping and process launch were outside the original timers. Hardened future runs record setup and whole-process wall time separately; the original raw evidence is preserved with a separate retrospective attestation. These component timings are not measured FPS and must not be added to shader or capture percentages.

All nine canonical geometry/material/plan/page/ray/bake/count fields match in eighteen pressure-sequence comparisons and seven no-pressure comparisons. The earlier rejected orbit-envelope and SIMD-decoder experiments remain excluded.

The final hardened one-pair rerun also passes all canonical checks with six accepted builds and the same seventh refusal. Serial worker time including every reclaim changes 4229.163 to 3937.965 ms (6.89% less); including measured entry setup, 4238.027 to 3946.357 ms (6.88% less). Child wall time including independent oracle bookkeeping changes 4459.927 to 4158.452 ms (6.76% less). This is a single confirmation pair, separately reported from the three-pair median. All 53 compiled project inputs, both executable hashes, compiler identity/commands and 2,109 fixture outputs are verified. The isolated final O2/O0 controls separately cover invalid samples, aggregate-only refusal, largest-block-only refusal, raised floors, retained leases, revision changes and reset behavior.

## Expanded capture regression

The new foreground fixture reproduces the fresh OFF interval at ticks 138505875 through 138519468. Three alternating pairs exercise 76,172 warm calls per mode over 108 repeated camera samples. Median aggregate capture-only times below compare the historical `fd6b3a4` retention baseline against the current implementation. They validate the already-shipped retention fix under stronger inputs; they are not a new production speedup in this follow-up.

| Scenario | Historical baseline, ms | Current, ms |
| --- | ---: | ---: |
| Stable revisions | 70.6035 | 13.7525 |
| Rewritten VB, identical bytes | 152.9107 | 88.0382 |
| Rewritten VB and IB, identical bytes | 196.2153 | 140.3993 |
| Synthetic alternating buffer layouts | 435.8960 | 250.3581 |

Fixed-layout current runs have zero warm misses, evictions and rebuilds. In the VB+IB case every call revalidates both revisions, every call proves exact reuse, and index-token hits are zero. The synthetic layout case deliberately creates more cache identities and is not required to have zero misses. Every genuine-low-memory counter and retained-byte result remains identical to the historical baseline. All eight mutation controls pass their intended positive/rejection conditions.

## Fifteen investigated directions

| Area | Direction | Decision |
| --- | --- | --- |
| GI 1 | Whole-cache reset on small entry shortfall | Implement staged measured reclaim; exact MPQ pipeline comparison passes |
| GI 2 | Mandatory fallback BVH dominates local preparation | Preserve coverage; bounded WMO subset reuse remains future structural work |
| GI 3 | Whole-source decode thrash and a 26.85 MiB WMO outside the 8 MiB pool | Keep validation and caps; preserve useful identities without a stamp bypass |
| GI 4 | Changing camera regions force page repacking | Preserve page keys/material/order; avoid unsafe region reuse |
| GI 5 | Actor updates and cancellation | Secondary worker costs; keep probe quality and cancellation policy |
| Capture 1 | Incorrect 16 MiB demand tier | Prior fix verified active; no further cap increase |
| Capture 2 | Both VB and IB revisions change | New explicit exact-content rewrite coverage and counters |
| Capture 3 | Repeated index patterns consume retained bytes | Corpus has 31 patterns, but live D3D distribution is unknown; no speculative interning change |
| Capture 4 | Memory cleanup timing omits early work | Full-event and cleanup-subphase timers added |
| Capture 5 | Producer gaps and queued query work overlap phases | Recorded-gap regression added; do not count overlapping waits twice |
| GPU 1 | Repeated regional-fog node reads within one cell | Rejected: WorldFog becomes slower; FogClouds also changes FP32 output |
| GPU 2 | FogBlur reads center receiver twice | Rejected: exact output and fewer instructions, but repeated timings show no reliable aggregate gain |
| GPU 3 | AO rejection order | Workload-dependent tradeoff; no quality/filter reduction |
| GPU 4 | Detailed material profiling while disabled | Sampling overhead measured; no claim that native draw submission is removable |
| GPU 5 | Per-draw material timestamp perturbation | Keep diagnostic cost separate from raster cost; no unsupported FPS claim |

No production shader change is included. The WorldFog cache passes 640 exact native/DXVK image comparisons but is approximately 11.1% slower on native D3D9 and 2.1% slower on DXVK in the paired-median experiment. The cloud variant changes FP32 output by up to 1.1920929e-7 and fails the unchanged exact-output gate. FogBlur center reuse passes 1,188 exact comparisons, including exercised 0/2/4/8 MSAA, FP16/FP32 and nonfinite controls, and removes 43 static instructions. However, repeated ABBA timing shows native paired median ratio 1.0001 and aggregate 1.0169; DXVK ratios 0.9784 and 0.9942 with some dense-shape regressions. Reduced instruction count alone does not justify adoption. Original shaders and generated assets remain unchanged; the reusable GPU stand and failed-candidate evidence are retained.

## Final validation and delivery

The affected-source native run executes 103 scripts: 100 pass, two stop on missing WSL Zig/StormLib configuration, and one retains the obsolete unconditional-reset source pin. The pin is updated to require raw/local/full order, identical resampling floors and the final complete reset, retaining all surrounding budget/fallback checks. All three then pass in the Windows run. Fifteen Windows scripts pass, including eight portable fog-runner rejection tests and final O2/O0 entry controls. With one overlapping script, this gives 114 distinct passing automatic scripts. Sources remain unchanged during each execution group. Actual native ASan/UBSan and sampler/job/geometry-worker TSan checks pass.

The hidden system-D3D9 material gate passes single-sample and MSAA 2x/4x/8x capture/restoration, plus 120 pressure/recovery frames. The final guarded fog tool recompiles the saved rejected experiment in a fresh directory, regenerates all 112 MPQ visual outputs exactly, passes twelve direct native FP16/FP32 smoke cases with non-neutral grounded coverage, and rejects benchmarking before a full quality gate. This smoke is separate from the earlier complete candidate comparisons and carries no timing claim.

The fog runner now authenticates resumed arguments, source/tool hashes before/after compilation, both executables, shader helper/compiler DLL, copied HLSL/bytecode and exact fixture inventories. It rejects injected optional water inputs, stale output and Python `-O`; coverage is evaluated per invocation, changed shader and backend. Its source set is a conservative project superset, not per-translation-unit dependency discovery. It does not hash the complete SDK/OS closure. The older rejected GPU experiment manifests are preserved without retroactively assigning these stronger guards to them.

This is a focused regression gate, not a rerun of all 295 automatic scripts or the earlier integration gate. Existing host-specific subcase skips, including historical Apple-Clang golden hashes, remain visible in raw logs. The unavailable original contact-normal captures remain unavailable; no substitute evidence or weaker visual tolerance is introduced. Source and DLL/PDB identity are recorded with the separate delivery package after compilation. No live-game FPS uplift is established, and the observed producer-side pause remains a separate unresolved cost.

The renderer build succeeds from source commit `9be300a0147c0b8fc8cc2eefbfb4598dc5d26bab`. All source/build-script hashes remain unchanged during compilation; generated headers match the commit after line-ending normalization, and their committed bytes are restored afterward. The two existing compiler warnings remain unchanged.

| Delivered artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `d3d9.dll` | 5,619,712 | `7d08bee1cc6645b7e19f623368f00388b269e110dae19069501a5a532a8a22d5` |
| `frd9.pdb` | 45,907,968 | `e47f74ed8a996f0ad7e47b191cfe30d90b9698fd68ca749f6c37645384feb9d5` |

Separate delivery: `northlight-builds/northlight-rotation-reclaim-9be300a-20261023/`, with this report, installation notes and a hashed manifest. Installed Northlight, wow_optimize DLL/SYM/configuration and six personal client configuration files remain byte-identical to the ten-input pre-work snapshot. The previous delivery is retained unchanged. No automatic installation or game launch is performed.
