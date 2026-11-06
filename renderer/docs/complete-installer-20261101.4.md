# Complete Windows test installer 2026-11-01.4

Version: `0.3.216-test.20261101.4`. The installer contains the complete current
renderer, including directional/local PCSS, crowd-bound preparation, shared
point-shadow resources, HDR additive particles and earlier published fixes.

| Artifact | Identity |
| --- | --- |
| Archive | `Northlight-0.3.216-test.20261101.4-Windows.zip` |
| Archive bytes | 20,146,797 |
| Archive SHA256 | `ad0521dfbd82c15d2bc546b7757a217c69e72f804ba05057bfdab0a03d99b319` |
| DLL bytes | 9,001,984 |
| DLL SHA256 | `750d4272d3456b48c9c48d03013e25a18b1aa1f6f7fc23b466083f32c91a4f16` |
| Embedded source identity | `85bcbc0845f87955d298158304e669fdc806d03d9cc6b9a8c89269e049636a48` |
| DLL build time | `2026-11-01 16:11:29 UTC` |

The published integration has the same renderer source identity as this
installer. Generated text line endings are normalized in that identity;
the shader bytecode and build inputs are preserved. New source changes are
separated into shadow-preparation and PCSS commits. The preparation changes
also have an independent port in PR #92; the full PCSS pipeline belongs to
the combined renderer in PR #98.

Fresh installer validation passes source/build identity checks, all 40 world
shader entries, the release-build guard, 239 installer-front-end cases
(6 skips), 62 Windows-installer cases (2 skips), package hashes, dependency
closure, runtime pins and ZIP CRC. The packaged DLL is byte-identical to the
fresh build. All six entry points return zero for `--help` using the bundled
isolated Python beneath a Unicode path. Python 3.13.15, StormLib, DXVK 3.1.1
and alternative DXVK 2.7.1 remain pinned.

The archive is a local artifact in `releases/`, not a GitHub Release upload.
No game was launched or installation performed. The full regression suite
was not rerun; historical suite failures and in-game acceptance remain open.
PCSS costs and limits are documented in `pcss-filter-extension-20261101.md`.
