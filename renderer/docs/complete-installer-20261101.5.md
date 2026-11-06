# Complete Windows test installer 2026-11-01.5

Version: `0.3.216-test.20261101.5`. Renderer commit:
`d0b3dbedb5f695d7e2bd356d315f76497217515f`. This includes the complete PR #98
integration, the near-shadow interval correction, and the following optimizations:

- Lend unused bounds-preparation time to cold mesh construction within the existing
  total time and operation limits. The synthetic 632-mesh crowd reaches complete
  bounds in 58 frames instead of 238; cumulative cube submissions fall from
  8,173,386 to 1,985,796. Conservative fallbacks remain available.
- Recognize 170 additional stock SM2/SM3 weighted pose variants with r0 output.
  Preserve arithmetic order, aliased inputs, failure outputs, and the existing
  envelope and attachment eligibility. For 8192 vertices, paired median CPU time
  falls from 2.214 to 0.283 ms (SM2) and 2.082 to 0.268 ms (SM3).
- Feed pending EVENT/FLUSH GPU time and command-queue waiting separately into
  the GPU/CPU budget signals, including with Diagnostics disabled. Rebase counters
  on startup/reset and retain the first zero-to-positive wait or skip transition.

| Artifact | Identity |
| --- | --- |
| Archive | `Northlight-0.3.216-test.20261101.5-Windows.zip` |
| Archive bytes | 20,147,764 |
| Archive SHA256 | `473b3fa545ff7af2a5b26a3385c80094f55cf4d0a21d2a7fe47df7420bc5ed67` |
| DLL bytes | 9,007,104 |
| DLL SHA256 | `33cf781a5479932e73f316807c7eef969f32cce57d23c86f924dfb1182ab37fb` |
| Embedded source identity | `513f1dc43129a9102c76540ed388633b472101be84a5c085261ebfeb5bb738ba` |

The packaged DLL is byte-identical to the validated build, and the committed
renderer inputs match its embedded source identity. Package hashes, dependency
closure, runtime pins and ZIP CRC pass. All six packaged entry points return zero
for `--help` with bundled isolated Python under a Unicode path. Installer checks:
239 front-end cases (6 skips) and 62 Windows cases (2 skips). Python 3.13.15,
DXVK 3.1.1 and the alternative DXVK 2.7.1 retain their existing pins.

Pose validation covers 5136 supported shaders from 5594 stock programs, with
714,752 comparisons per O2/O0 build against the prior implementation and scalar
oracle. Material pipeline, displacement state, query polling and actual renderer
budget-method regressions pass. The command-stream fake-device checks pass O2/O0,
the ordered fallback control and the expected short-deadline negative control,
including unprofiled GPU and queue waits. Windows runs used the portable test
pump; no sanitizer certification is claimed.

Reproduce the focused Windows checks with `tests/test_actor_pose_kernels.py
--benchmark --baseline-ref 95e0492a`, `tests/test_event_budget.py`,
`tests/test_query_poll_latency.py` and `tests/test_material_pipeline_cpu_micro.py`.
Package with `renderer/build_packages.py --platform windows --version
0.3.216-test.20261101.5 --dll renderer/frd9.dll --out releases`, then run
`tests/verify_packages.py` on the resulting ZIP.

These are CPU/component and package checks, not a new in-game FPS measurement or
full-suite acceptance. The ZIP is a local artifact in `releases/`; no GitHub
Release upload or game installation was performed.
