# Complete Windows test installer 2026-11-01.2

Version: `0.3.216-test.20261101.2`. Production source:
[a232d34e](https://github.com/aphelion105/northlight/commit/a232d34e2953dc8a4770a42741c537e6301c41f7),
published in [PR #98](https://github.com/lepola/northlight/pull/98).
The independent shadow port is
[3e7545cb](https://github.com/aphelion105/northlight/commit/3e7545cb) in
[PR #92](https://github.com/lepola/northlight/pull/92).

| Artifact | Identity |
| --- | --- |
| Archive | `Northlight-0.3.216-test.20261101.2-Windows.zip` |
| Archive bytes | 20,123,054 |
| Archive SHA256 | `7cecddbbdbeb1929e01711604e8879ef68bc5cf746d3ee3bf420de8530f26976` |
| DLL SHA256 | `fc9837401389e754aa494e55015fc976162314f36164dfe02f19061e8de2ae28` |
| Embedded source identity | `0830e0305c5c97fe7309aa14e21a62ec31ffebac83b9a7746ff538ea7910836c` |
| DLL build time | `2026-11-01 00:38:46 UTC` |
| Main baseline | `e19ffee601acbad2b991917109bcb6d8b2f7d5ca` |

This supersedes [.1](complete-installer-20261101.1.md). It retains the complete
published integration above main, including HDR, materials and character
normals/displacement, AO/ASSAO variants, vegetation, fog, emitter/animated-light
support, weather-lens effects, Unicode paths, cache/art reuse and ownership, and
all intervening renderer fixes. The production source identity equals the
original working tree, and all 44 installer application inputs match it exactly.
The source-only developer Weather Lab remains in the checkout; its runtime
effects and controls are included in the DLL and quality profile.

The new shadow correction excludes wholly offscreen sources before spending
the budget and smooths source handoffs within the existing hard cube limit.
`PointShadowLimit` is 16..128, default 64 in every preset. See
[implementation and acceptance](shadow-selection-20261101.md).

## Fresh verification

- Both the complete and independent Windows x86 release DLLs build.
- Production selector, local-light correction/batching and terrain/pool/capture
  regressions pass. Native D3D9 and DXVK 3 each pass the 360-case fractional
  correction matrix, with 128 verified sources. The previous shader fails the
  same negative control.
- Actual sixteen/sixty-four-source pools each produce exactly the configured
  number of ready cubes despite brighter offscreen competitors. The real
  Telredor static scene (1,319,119 triangles) passes eight camera poses at
  limits 4/16/64/128 on both backends, including 1,950 / 1,956 exact shared-face
  comparisons. These are static component checks, without an FPS claim.
- Package verification passes payload hashes, dependency closure, runtime pins,
  allowlists and profile-preservation contracts. ZIP CRC passes. The packaged
  DLL equals the freshly built DLL and contains the expected source/build IDs.
- All six shipped entry points return zero for `--help` using the archive's own
  Python with `-I -B -X utf8`, extracted beneath a Unicode path.
- Runtime pins remain Python 3.13.15, StormLib, DXVK 3.1.1 and alternative DXVK
  2.7.1. No proprietary client files or generated world cache are distributed.

The historical full-suite result of 198 PASS / 37 FAIL / 222 SKIP remains open;
the focused checks do not recertify that suite. The exact recorded gameplay
route still requires a user retest. No game was launched or installation/client
configuration changed, and no GitHub Release was uploaded.

The local ZIP is also exposed in `releases/`. Private validation, package JSON
and SHA256SUMS are in `.local/releases/0.3.216-test.20261101.2/`; detailed logs
and negative controls are in `.local/shadow-selection-20261101/`.
