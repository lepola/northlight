# Complete Windows installer 0.3.217-test.20261104.1

Superseded by [20261104.2](complete-installer-20261104.2.md): this package's
parent process did not initialize bundled StormLib before checking art-layer
ownership. Valid Northlight patches without a matching install receipt could
therefore be classified as foreign and skipped. The runtime/help acceptance
below injected the library path and did not cover that clean-launch behaviour.

Implementation commit: `59ccf16b81e07a35babf39280725e5656174ce4a`.
This package contains the complete current integration, including the earlier
CPU/GPU optimizations, HDR/AO/material/weather changes, camera-independent native
lights, moving-bone/attachment support, directional shadow transitions, Unicode
paths and the MPQ extraction/cache/art-layer pipeline. No feature subset was used.

The offline generator fixes from `4b3af8eb` are included. In particular, the
packaged `build_spell_emitters.py` and `attached_emitter_sources.py` match the
current tree and retain calibrated equipment-light energy. These files were
older in the preceding installer, even though its renderer source identity was
already current. DLL identity alone does not cover the packaged Python modules.

The city-lamp GPU tests and their [audit](city-lamp-audit-20261104.md) are published
with this release. Their confirmed Dalaran native-light shadow-admission gap and
orc floor self-occlusion cases remain unresolved; this package does not claim
those findings were fixed. No NPU implementation, dependency or optional runtime
was found in the current integration. MPQ extraction is included.

## Build and artifact

Built with the normal `python scripts/build_renderer.py` production command,
pinned Zig 0.15.2, compiler preflight and all shader/catalog freshness gates.
The build has no developer-tuning or imported-input stand flag.

- DLL version: `0.3.217`; build time: `2026-11-04 09:40:33 UTC`.
- Source identity: `d1e4423db8aee534ac114af2935d1c4de910422aa654a320361923c7852eb093`.
- DLL SHA-256: `e8d420fea3e1c5f6503b24800ca180ebddc78bb1148d46bd7cb5d72fcfd6c24e`.
- Archive: `Northlight-0.3.217-test.20261104.1-Windows.zip`.
- Archive size: 20,419,879 bytes; 100 entries.
- Archive SHA-256: `1ef43f0e1498b400ba685dd5df3a56eb64d6e080c5d4fd729ab9a2cd0888a879`.

The archive is generated under `releases/`; binaries are not committed to Git.
It bundles Python 3.13.15, pinned StormLib, DXVK 3.1.1 and fallback DXVK 2.7.1.
World caches and proprietary game data are generated from the user's client,
not distributed in the installer.

```text
python scripts/build_renderer.py
python renderer/build_packages.py --platform windows --version 0.3.217-test.20261104.1 --dll renderer/frd9.dll --out releases
python tests/verify_packages.py releases/Northlight-0.3.217-test.20261104.1-Windows.zip
```

## Acceptance

- Explicit-archive package verification and every ZIP member's CRC pass.
- All 46 packaged application files match the current repository and the
  package's build manifest byte for byte. The packaged DLL equals the fresh DLL,
  and its embedded source identity equals the current source identity.
- Python, StormLib and both DXVK payloads/metadata match their pins.
- The bundled isolated Python imports its standard extension modules and loads
  StormLib from a repository-local Unicode directory. All six packaged entry
  points pass `--help` using that Python, with UTF-8 mode enabled.
- Windows installer suite: 62 cases, 2 skips, no failures. Shared installer
  suite: 248 cases, 6 skips, no failures. Cache installer checks pass; the stock
  manifest migration check is skipped without `NORTHLIGHT_STOCK_CLIENT`.
- The preceding lamp audit passes the unchanged 16-case GPU matrix and three
  explicit weighted-character/obstacle tests through the production soft-shadow
  resolve and consumer. The negative city findings remain recorded separately.

The production source identity is unchanged from the extensively validated
[complete integration](publication-audit-20261104.md). No new whole-game FPS or
gameplay acceptance claim is made. The installer was not deployed to the game;
`Wow.exe`, the installed DLL and game archives were not modified.

Detailed package/source/runtime proofs and test logs are in
`out/publication-20261104-next/installer-acceptance.json` and adjacent logs.
