# Windows installer 0.3.217-test.20261105.1

This replaces [20261104.8](complete-installer-20261104.8.md), retaining every
preceding integrated change. It adds [WMO lighting preservation across
subdivision](stormwind-lighting-bands-20261104.md) and regression coverage for
the Stormwind lamp housing/character shadow overlap.

## Artifacts

- Installer: `releases/Northlight-0.3.217-test.20261105.1-Windows.zip`.
- Size: 20,490,269 bytes.
- ZIP SHA256: `b7aa5c668d78aee955d6cc3b28d3756bb7b7cf4c6e8a3caa63fec0a3442c647f`.
- Standalone DLL: `releases/Northlight-0.3.217-test.20261105.1/d3d9.dll`.
- DLL SHA256: `42c305bde36ea9209562138829d612cec34736ad22174811240e27da6197ee9e`.
- Renderer source identity: `d1522ab6587b8087f89fb990b8745a6adeb2d9715aa3862ad0ebc8be82038738`.
- Implementation commit: `f275e749415dd59b4a291ee9a29d70f8999cd1a4`.
- SHA256 sidecars accompany both artifacts.

## Acceptance

The production DLL builds with pinned Zig 0.15.2 and unchanged shader
freshness gates. All 72 registered stock WMO shaders pass native/DXVK2/DXVK3
tests, including the native VS2 register limit. Current-constant/cache reuse,
native/capture/lazy shader variants, old-path negative controls and independent
dependency tests pass. Existing displacement and transparent-effect suites
and eight final-DLL SDR/HDR/MSAA/Reset/Present runs pass. The investigation
report records exact coverage and the controlled performance limits; it does
not infer whole-game FPS or historical actor admission.

Canonical package verification passes: every ZIP CRC, all 48 application
files, embedded DLL/source identity and dependency pins. Bundled isolated
Python runs all six entry points from a Unicode path. Packaged-parent MPQ
ownership tests accept owned archives and reject foreign/tampered controls.

The actual packaged installer was checked read-only against
`D:\Games\335_engb_mod`: world cache is `up_to_date`, no rebuild/removal is
scheduled, and all six instance groups are retained. All six explicit cache
versions remain 1. Source/DLL changes alone keep caches; relevant version or
archive changes and an explicit force request still rebuild as intended.

Evidence: `out/installer-20261105.1/installer-acceptance.json` and
`out/installer-20261105.1/packaged-cache-plan.json`.

Extract the ZIP and run `Install.cmd`. Validation did not install the package
into the game. `Wow.exe` remains unchanged.
