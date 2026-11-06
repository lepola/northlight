# Windows installer 0.3.217-test.20261104.4

This package replaces [20261104.3](complete-installer-20261104.3.md) and contains
the complete integration through commit `84689f198f00b7408dbded8a6a852d3d163f4720`.
It includes all preceding extraction, lighting, shadow and performance changes,
the [spell/prelit fixes](spell-prelit-visibility-20261104.md), and the latest
[SDR/HDR ambient preservation](qa-native-brightness-20261104.md) and
[bounded memory recovery/material reuse](qa-memory-materials-20261104.md).

The packaged production DLL is byte-identical to the tested DLL. Its embedded
source identity matches the current source tree. Cache compatibility remains
based on explicit generator versions: this DLL/package rebuild does not
invalidate compatible installed caches.

## Artifact

- File: `releases/Northlight-0.3.217-test.20261104.4-Windows.zip`.
- Size: 20,462,091 bytes.
- ZIP SHA256: `8ecc7d66520b1631acf6f97e982636cb550fea1940794b525a203dfc8063365d`.
- DLL SHA256: `d6c48652fdabbcb856cfd926f915468104d5f29bf7aa17e401559ab83a26939a`.
- Renderer source identity: `59f0eef99c3ff2404bde8683afd2e7583392ea05ce749eaf2784db0b62330add`.
- A `.zip.sha256` sidecar accompanies the archive.

Extract the archive, then run `Install.cmd`. Python, StormLib and both pinned
DXVK backends are bundled. The art layer and any required client caches are
built from the target client's assets by the included installer pipeline.

## Acceptance

Canonical package verification, every-member ZIP CRC, exact byte/hash checks
for all 48 application files and the production DLL, cache versions and all
dependency pins pass. The copied release artifact has the same SHA256 as the
verified build artifact.

The bundled isolated Python loads its required extensions and runs all six
entry points from a fresh Unicode path without Northlight environment overrides.
The packaged parent installer verifies the complete ownership inventories of
both current game art-layer MPQs. Owned synthetic archives are accepted;
foreign, tampered and merged archives remain rejected.

`test_windows_installer.py`: 62 tests, two existing skips, no failures.
`test_northlight_install.py`: 257 tests, six existing skips, no failures.
Local evidence is in `out/installer-20261104.4/installer-acceptance.json` and the
adjacent test logs. This is package acceptance; earlier rendering validation is
recorded in the linked implementation reports.

The ZIP was not installed into the game during validation. Before/after hashes
confirm that `Wow.exe`, the installed renderer and both game art-layer MPQs are
unchanged. Client assets and generated client caches are not bundled in the ZIP.
