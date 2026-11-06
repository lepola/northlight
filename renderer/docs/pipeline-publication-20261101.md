# Pipeline, fog and ASSAO publication (2026-11-01)

This follow-up is based on combined integration `b1f27997`. It removes discarded
bloom work from HDR AO, decodes native geometry in the HDR world composite,
retains the captured sky during partial-composition fallback, and avoids an
unused full-resolution FP16 material target. Fog keeps base-air attenuation
while clipping the cloud-march tail, and skips empty/unreachable passes while
maintaining neutral temporal history and independent local lamps.

ASSAO adds High/Medium/Low choices (12/5/3 sample pairs) through the existing AO
settings. It adapts Intel's fixed Q2/Q1/Q0 kernel to Northlight's normals,
rotation and blur. Adaptive Q3, deinterleaving and depth mips are not included;
reproduction of WoW Forever's integration is not claimed. Defaults remain
SSAO/High. Existing AO shader payloads are unchanged.

## Validation of the publication checkout

- The Windows x86 release DLL builds; layout and actor shader provenance pass.
- On each of DXVK 2.7.1 and 3.1.1, `test_ao_quality.py` passes 549 basic AO
  cases, 288 GTAO/ASSAO grazing cases and 3,456 ASSAO grid comparisons. The
  grid covers odd sizes, FP16/FP32, both depth partitions, radius 0.1/2/10 yd
  and MSAA0/2/4/8. Alpha and fallback RGB comparisons are exact.
- The built DLL passes 734 production proxy checks per backend: SDR, HDR,
  and HDR with MSAA8/triple buffering, including Reset/resize, original
  shader/constants, UI state restoration and presentation (1,468 total).
- Quality parsing passes optimized and ASan/UBSan runs; settings service and
  overlay checks also pass, including all five methods' saved/active menu
  round trips. The AO lifecycle fixture passes 200 transitions, including
  allocation failure/retry, lazy HDR allocation and format changes. AO fold
  and bilateral-filter checks pass.

The [detailed audit](pipeline-performance-audit-20261101.md) retains the exact
snapshot attribution for the earlier shader inventory, HDR/fog differential
oracles and microbenchmarks. Those timings are not fresh game FPS measurements.
The publication checks above do not replace the previously reported full-suite
failures. No game session, installation or new installer package was produced;
the existing `0.3.216-test.20261031.12` ZIP predates these changes.

## Related reviews

- [#98](https://github.com/lepola/northlight/pull/98): combined linear HDR,
  cloud-tail optimization, actor integration and ASSAO.
- [#118](https://github.com/lepola/northlight/pull/118): independently buildable
  AO/ASSAO and alpha-only fallback, retaining that branch's original blur.
- [#109](https://github.com/lepola/northlight/pull/109): independently validated
  host-side empty-fog skipping and temporal history. Its older cloud kernel
  failed exact comparison after a trial tail-clipping port, so its shader
  sources, payloads and GPU constant contract remain unchanged.
- [#94](https://github.com/lepola/northlight/pull/94): independent HDR
  presentation; the full linear world pipeline belongs to #98.
- [#112](https://github.com/lepola/northlight/pull/112): independently reviewed
  actor-thickness correction, with its own acceptance limits.
