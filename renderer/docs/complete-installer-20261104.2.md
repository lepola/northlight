# Windows installer 0.3.217-test.20261104.2

This replaces [20261104.1](complete-installer-20261104.1.md) and retains its complete
renderer, optimizations, extraction/cache pipeline and calibrated equipment light
generators. The renderer DLL and dependency versions are unchanged.

## Art-layer ownership correction

The previous installer passed the bundled StormLib path to builder subprocesses,
but did not configure it for its own MPQ ownership checks. Without a developer
configuration or a matching historical transaction hash, opening a patch raised
`northlight_paths.Missing`. The ownership helper conservatively returned false,
so a valid Northlight `patch-z` was reported as another mod and art-layer updating
was skipped. The installed archives themselves were intact.

Commit `48d16888` initializes the same bundled library when constructing the
installer. Full member-inventory and payload-hash checks remain unchanged;
no client-specific hash was added to the ownership allowlist. The two reported
archives have SHA-256
`96dfbfb920385e3504aef20928d76093ed97dbde8733ea38c78b9ae13e862b10`
and contain the earlier equipment-energy correction. The `.1` run skipped their
subsequent full art-layer regeneration. Rerunning `.2` recognizes them and allows
the normal rebuild/update path, provided art-layer installation is enabled.

The new regression recreates a packaged app without `northlight.local.ini` and
runs fresh isolated Python processes with clean and stale StormLib environments.
It fails on the old constructor and passes after the correction. Valid owned
archives are recognized; modified, merged and markerless archives remain foreign.
The client fixture stays byte-identical. The installer suite passes 248 cases
with six existing skips and no failures.

The preceding `.1` acceptance only exercised library loading with an injected
path and `--help`, so it missed the parent ownership path. The `.2` acceptance
additionally constructs the real packaged installer in a clean environment and
checks both reported game archives read-only, before priming its ownership cache.

## Artifact

- File: `Northlight-0.3.217-test.20261104.2-Windows.zip`, 20,419,927 bytes.
- ZIP SHA-256: `7fe76d2e5221c424268823c7f85e4f62b442fc2c2419961d40b66377ec5a5cf7`.
- DLL SHA-256: `e8d420fea3e1c5f6503b24800ca180ebddc78bb1148d46bd7cb5d72fcfd6c24e`.
- Renderer source identity: `d1e4423db8aee534ac114af2935d1c4de910422aa654a320361923c7852eb093`.

Package validation checks ZIP CRC, all 46 application files, the exact DLL,
dependency pins and the six entry points using bundled isolated Python under a
Unicode path. Build and acceptance evidence is under `out/art-ownership-20261104/`.
The package was not installed into the game during these checks. Neither
`Wow.exe` nor the current game archives were modified.
