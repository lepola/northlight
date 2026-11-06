# Windows installer 0.3.217-test.20261104.3

This replaces [20261104.2](complete-installer-20261104.2.md). It retains the complete
renderer, extraction/art-layer pipeline, calibrated equipment lighting and all
earlier optimizations, including the bundled StormLib ownership correction.

The release adds [city-lamp shadow fixes](lamp-shadow-fix-20261104.md): native
Dalaran fixture qualification and exact live luminous-layer ownership, bounded
orc flame origins, and Stormwind daylight shadow composition. Source energy,
distance/shadow budgets and filtering quality are preserved. Actual-client
offline tests cover weighted character movement, articulation, independent
obstacles, metal retention and exact actor-off/removal behavior.

[Explicit cache revisions](cache-generator-versions.md) replace source hashes as
the compatibility contract. Unversioned caches upgrade once. Later DLL builds
do not trigger cache regeneration. Compatible installed prebuilt variants remain
usable even if a later installer contains no matching prebuilt archive.

The package contains all 48 current application files and the production x86
DLL. Client models, captures and generated client caches are not distributed.
The installer suite has 257 tests, with six existing skips and no failures.
The real four-tile cache integration creates 451 files, including the NLF1-v2
native fixture catalog. A second run reuses the cache and creates a byte-identical
37,970,233-byte cache ZIP.

This is a complete installer, not an update applied to the local game during
validation. `Wow.exe` and the installed game archives are read only.

## Artifact and acceptance

- File: `Northlight-0.3.217-test.20261104.3-Windows.zip`, 20,446,913 bytes.
- ZIP SHA-256: `c45c1951b84a05309d2227e8fc78f9b1849b269522630f8ba4f963a65f84bbc8`.
- DLL SHA-256: `aec215dfa9a762e3d863204492e07ba2f853f7eede461279942c9d3865c5f3c2`.
- Renderer source identity: `1fbbbf318ad5cfa8f3e970d0ba19d737b9cac8e1831bd8729331c5f20cda750c`.
- Implementation commit: `3a9a8172`.

Canonical package verification, CRC and exact byte/hash comparisons pass for
all 48 application files and the current DLL. Explicit cache revisions and
Python/StormLib/DXVK dependency pins match. All six entry points run using the
bundled isolated Python from a fresh Unicode path without Northlight environment
overrides. The real packaged parent installer recognizes both current owned
game MPQs and verifies every manifest member. Owned synthetic archives pass;
foreign, modified and merged archives remain rejected.

Acceptance evidence is in `out/lamp-shadow-fix-20261104/installer-acceptance.json`.
The current game executable, installed DLL and both art-layer archives remain
byte-identical to the pre-investigation hashes. The ZIP is a local release
artifact and has not been installed into the game during validation.
