# Windows installer 0.3.217-test.20261104.6

This replaces [20261104.5](complete-installer-20261104.5.md) and includes the
complete preceding integration: F11 help, SDR/HDR brightness, address-pressure
recovery, material reuse, extraction, city lighting/shadows and optimizations.
It adds the [Stormwind HDR follow-up](stormwind-hdr-performance-20261104.md):
correct atmospheric colour around emissive lamps, HDR receiver headroom and
less blending of empty HDR compatibility-layer pixels.

## Artifact

- File: `releases/Northlight-0.3.217-test.20261104.6-Windows.zip`.
- Size: 20,483,658 bytes.
- ZIP SHA256: `c5af5823cfa282cc368dcd297be0953146a68f876596266ff3ab65079485b6da`.
- DLL SHA256: `edc8d05da5bfcf98fd8f7d517bced617c922a8ba2da5b7d66df9119e65b32abb`.
- Renderer source identity: `b4f4ba533059ba5bda759db077cb103cb28ec4f7cf092dcc817f907fbae282bd`.
- A `.zip.sha256` sidecar accompanies the archive.

## Acceptance

The production DLL was built with pinned Zig 0.15.2 and all shader freshness
gates. Canonical package verification passes. Every ZIP member passes CRC;
all 48 application files and the embedded DLL match current sources. Python,
StormLib and both DXVK pins match the package metadata.

Bundled isolated Python passes its standard-library/extension checks and runs
all six packaged entry points from a Unicode path. Packaged-parent MPQ
ownership checks accept the real owned archives and reject foreign, tampered
and merged controls. Evidence: `out/installer-20261104.6/installer-acceptance.json`.

Renderer regression results and the external GPU-load limitation are recorded
in the linked investigation. No whole-game FPS gain is claimed from contended
timing samples.

Extract the ZIP and run `Install.cmd`. Compatible caches are reused according
to their explicit versions; this DLL rebuild does not invalidate them.
The package was not installed into the game during validation. `Wow.exe` is
unchanged.
