# Complete Windows test installer 2026-11-01.1

Version: `0.3.216-test.20261101.1`. Production source: [a24ee60d](https://github.com/aphelion105/northlight/commit/a24ee60de842ab84f88b37870e19dfda07ab8ce3), published in [PR #98](https://github.com/lepola/northlight/pull/98).

| Artifact | Identity |
| --- | --- |
| Archive | `Northlight-0.3.216-test.20261101.1-Windows.zip` |
| Archive bytes | 20,122,153 |
| Archive SHA256 | `24117cacafa1e984ee9ddd5307613096c9a41a2794d6b77fb59249b2d96efb1a` |
| DLL SHA256 | `02f16170168bddf13838f1b64bbe39d5f7cbe26ad59138d856a097b8b6f17dd5` |
| Embedded source identity | `106ba0b71a597c25f39da095d2fd2802ef464cb810d1b8badc3f37b82645a02b` |
| DLL build time | `2026-11-01 00:16:52 UTC` |

This package includes the complete integration: cache reuse and art ownership,
Unicode paths, source/build identification, HDR/fog optimizations, ASSAO quality
variants, the facial-displacement correction and the new lens weather effects.
The previous `.12` installer predates the latter renderer updates.

Weather includes configurable rain rivulets, continuous snow morphing and merge
fades, fixed-pattern frost and persistent sand deposits in 2D lens coordinates.
Wind supplies lens forces and arrival pressure; optional camera-turn response
supplies bounded velocity impulses. The standalone developer lab and capture
replay are available in the source checkout; the player installer carries the
runtime effects and settings in the renderer. See the [Weather Lab guide](weather-lab.md).

## Publication audit

After publishing weather, 161 modified or new files in the original working tree
match PR #98 byte-for-byte. The remaining difference is ordering in
`tests/discovery.txt`; test membership is identical. No functional changes or new
files from that working tree remain unpublished. Existing independent feature
reviews are linked from PR #98; the combined PR contains the complete assembly.

## Fresh verification

- The Windows x86 release renderer builds with all shader freshness gates passing.
- CPU lens O0/O2, quality/settings, capture import/export, exterior dispatch,
  lens wiring and repository layout checks pass.
- Native D3D9 lens fixtures pass for SDR/FP16/FP32 and MSAA0/4, including snow morph
  continuity, fixed frost landmarks, sand pinned across view rotations and
  preservation of state, alpha and names.
- All 29 lab smoke cases, the mushroom/roof depth fixture and 3,072 precipitation
  cases across 24 render targets pass.
- `tests/verify_packages.py` passes payload hashes, runtime pins, package allowlists,
  dependency closure and installer contracts.
- ZIP CRC passes. Packaged DLL bytes equal the fresh build and contain its exact
  source identity. All six packaged entry points return zero for `--help` using
  the archive's own Python with `-I -B -X utf8`.
- Runtime pins remain Python 3.13.15, StormLib, DXVK 3.1.1 and alternative DXVK 2.7.1.

Current weather material verification uses native D3D9, not a fresh DXVK or game
run. The exact Zangarmarsh cap-only rain report remains unreproduced; the separate
global camera-shelter gate was corrected, and synthetic occlusion fixtures pass.
Static capture replay resets lens history and approximates resolved MSAA coverage.

This is a local build artifact; no installation, game run or GitHub Release upload
was performed. The historical full-suite result of 198 PASS, 37 FAIL and 222 SKIP
from the [.11 release record](complete-installer-20261031.11.md) remains open;
focused checks here do not replace that suite. Package manifests, SHA256SUMS and
validation logs are retained beside the local archive.
