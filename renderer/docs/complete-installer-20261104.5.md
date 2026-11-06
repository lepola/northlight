# Windows installer 0.3.217-test.20261104.5

This package includes the complete integration through `192fd28fd96dd27b2a6a815edd27e73a6773ee10`.
It replaces [20261104.4](complete-installer-20261104.4.md) and retains all preceding
SDR/HDR brightness, memory recovery, material caching, extraction, lighting and
shadow fixes.

## F11 help

Every individual setting now has an explanation: 113 quality settings, 14 HDR
settings, 22 sun/moon controls, the graphics backend and regional shadow range
(151 distinct settings). This adds explanations for 110 previously undocumented
controls. Tooltips describe the visual result, useful value meanings, dependencies
and CPU/GPU/memory tradeoffs where applicable.

Descriptions appear immediately below the setting name, before technical values.
Saved/running values, inheritance, disabled dependencies and live/restart notices
remain visible. Sun and moon share the explanation lookup while retaining their
independent values. Preset buttons and HDR calibration controls also have help.
Two raw height-setting labels now use readable names; stale local-shadow wording
about visible sources and Save-only application was corrected.

The change affects menu copy and tooltip presentation. Rendering settings,
shaders and explicit cache compatibility versions are unchanged.

## Verification

- Existing `test_settings_overlay.py` and `test_settings_service.py`: pass.
- Existing actual-menu GPU fixture: eight page cases in SDR and scRGB at 160/320
  nit UI white, plus HDR calibration and render-state preservation: pass.
- Private fixture: new GI and both sun/moon tooltips rendered and visually
  inspected; shared celestial help renders in SDR and both scRGB modes.
- One-off schema audit: all 151 controls covered, no duplicate help keys.
- Production DLL build with pinned Zig 0.15.2 and shader freshness gates: pass.
- Canonical package verification, every-member CRC, all 48 application files,
  exact DLL/source identity and pinned dependencies: pass.
- Bundled isolated Python: all six entry points run from a Unicode path.
  Packaged-parent MPQ ownership checks and foreign/tampered controls: pass.

Local evidence: `out/f11-tooltips-20261104/` and
`out/installer-20261104.5/installer-acceptance.json`.
The GPU checks use private offscreen targets, not a live Windows HDR display.
The package was not installed into the game; `Wow.exe` was not changed.

## Artifact

- File: `releases/Northlight-0.3.217-test.20261104.5-Windows.zip`.
- Size: 20,483,039 bytes.
- ZIP SHA256: `b69502014144b4f97aceaf0f5ef036e247d7875f68276010bdeba76a5d2274a8`.
- DLL SHA256: `3d6286b87bb5f8aa6cce15870c5f37f25220ab4d9974ef54b04842fd7ca19247`.
- Renderer source identity: `eb84e040664fca049124279f3dd975dc842d84ebf2e2213fbe73214aa783fb7a`.
- A `.zip.sha256` sidecar accompanies the archive.

Extract the ZIP and run `Install.cmd`. The package includes Python, StormLib and
both pinned DXVK backends. Compatible caches are reused by version; rebuilding
the DLL does not force cache regeneration.
