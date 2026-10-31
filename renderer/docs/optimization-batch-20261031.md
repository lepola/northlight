# Validated optimization integration, 2026-10-31

This publication consolidates 31 new changes relative to integration commit `b9077f0d` and DLL 20261030.9. The requested target of 60 new changes is not reached by this publication. Test repairs and rejected experiments are not counted. No game run, new FPS or CPU/GPU duration benchmark was performed; the generating world cache was not read or changed.

| Change | Functional evidence / reduced work |
| --- | --- |
| cloud-quantile-rank | x86 frozen quantile tables and 4 rounding modes |
| cloud-l8-normalization | 16 exact frozen volumes; shipping x86 conversion-loop instructions inspected |
| cloud-feature-padding | padding initialized from poisoned bytes; frozen Worley and complete volumes |
| cloud-cube-interleave | all cube bytes against frozen hash; complete Perlin fields and volumes |
| cloud-axis-integer-grid | all axis records and 16 volumes in 4 rounding modes |
| directional-pcf-grid | 576 exact FP16/FP32 cases per native/DXVK backend; production WorldLighting 398->406 static slots, 17->16 temps |
| snapshot-retained-vertex-copies | 64 poisoned retained-capacity VB/UP frames; snapshot O2/sanitizer/hash/counterfactual suite |
| memcmp-large64 | exact unsigned result, randomized comparisons, guard pages, sanitizers, four negative controls |
| memcmp-short8 | 8..15-byte comparisons, exact unsigned result and guarded loads; same exhaustive suite |
| overlay-single-state-capture | one stateblock per actual menu; DXVK18 exact prior frames, native SDR6 states/transforms; native HDR baseline unsupported |
| cloud-shared-dense-weight | 360000 frozen pair/single cases, four rounding modes and sanitizers; one expf instead of two |
| lights-batch-retirement | 18632 old linear vs stable merge cases, duplicate multiplicity and sanitizer coverage |
| lights-binary-retirement | 18632 old linear vs lower_bound cases; selection/particle lifecycle suites and sanitizers |
| lights-paired-scissor | 12000 exact old direct/fog rect sets, sanitizers; project each sphere once rather than twice |
| actor-blocker-grid | 576 exact actual ActorShadow cases per native/DXVK backend independently from original stage |
| actor-tent-grid | 576 exact actual ActorShadow cases per native/DXVK backend independently from blocker stage |
| fog-water-set-bits | 10000 frozen water-mask regions; ASan/UBSan; visit occupied cells only in authored order |
| snapshot-wide-simd | 17792 exact remap/range cases, scalar and ASan/UBSan; four INDEX32 values per validation step |
| probe-task-borrowed-dispatch | 10000 pool generations, no task heap allocation; O2/ASan/UBSan/TSan; full GI quality suite |
| probe-atomic-task-claim | 10000 pool generations; exact-once and exceptions across 0..4 helpers, TSan; no claim mutex |
| material-alpha-lanes | 65552 alignment/length cases, scalar and sanitizers; packed byte minima preserve alpha |
| material-bgra-repack | 65552 exact/in-place/guarded-size cases, scalar and sanitizers; mesh-plan hashes unchanged |
| direct-constants-borrow | 59321 exact shader payloads; full batches borrow contiguous columns, partial batches zero-pad |
| probe-packed-moments | 205909 exact pairs in four rounding modes, scalar/sanitizers; x86 scalar-division count reduced |
| gi-paired-trig | 1000000 authored-domain angles per x86 build, four rounding modes; max difference 5.97e-8; GI tolerance applies |
| probe-order-axis-squares | 4224 exact solve-order cases across layouts/cameras/rounding modes, sanitizers |
| probe-grid-integral-fast-path | 1220000 key/modulo cases, four rounding modes, scalar/sanitizers; aligned keys avoid round() |
| probe-atlas-fixed-remainders | 1220000 key/modulo cases including INT32 extrema and custom layouts; normal widths avoid variable division |
| hbao-direction-grid | Three quality kernels; 576 exact FP16/FP32/MSAA cases each per native/DXVK backend; reuse angular frame across radial taps |
| hdao-ring-grid | Three quality kernels; same exact 576-case/backend oracle; avoid per-tap row division/floor |
| hbao-zero-displacement-gate | Three quality kernels independently verified against grid stage; skip zero-delta normal/falloff work |

The shared directional PCF and actor filters retain every tap and accumulation order. Their actual shader oracles each cover 576 cases per D3D9/DXVK backend; the final AO matrix covers six kernels and 3,456 cases per backend. FP16/FP32, MSAA 0/2/4/8, odd/even sizes, authored blockers, sky, disabled controls and boundaries are explicit inputs. Static shader slots can grow with nested loop control even while dynamic coordinate/transcendental work falls; this is not a measured GPU speedup.

GI paired trigonometry intentionally follows the permitted GI accuracy budget. One million authored-domain samples in four rounding modes differ from separate float sin/cos by at most 5.97e-8. Packed distance moments, integer grid keys, modulo indices and solve scheduling remain exact against their references. Pool tests cover exact-once dispatch, exceptions and reuse under ThreadSanitizer as well as ASan/UBSan; heap-allocation assertions run in non-TSan builds because TSan supplies its own new/delete interceptors.

The menu uses its caller's existing full SavedState guard to avoid a duplicate stateblock. Explicit transform backups remain. DXVK passes all 18 SDR/scRGB menu modes with identical images to the original path; native D3D9 passes six SDR modes. Native scRGB output is empty in the unchanged baseline too and is not certified by this menu test.

The installer suite passes 221 cases on Windows (six platform skips) and on Linux/ext4 (two platform skips). Linux-on-NTFS output paths caused environment-only failures; those results are not accepted as installer behavior. Two old source audits now recognize the existing audited grass-blend and Bayer-alpha paths while retaining failure/state-order checks.

The standalone PRs receive only their own source and tests, are built separately, and retain their existing main-based boundaries. The combined integration PR collects their verified changes. Independent validation does not inherit the combined renderer's GPU or memory-pressure results.

Rejected: local-fog intersection/zero-integral gates, medium/low GTAO footprint pruning, and HDAO rejected-pair pruning changed strict reference pixels and are excluded.
