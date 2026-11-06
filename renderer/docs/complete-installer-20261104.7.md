# Windows installer 0.3.217-test.20261104.7

This replaces [20261104.6](complete-installer-20261104.6.md), retaining all
previous integrated lighting, shadow, SDR/HDR, memory, material-cache,
extraction, optimization and F11-help changes. It adds the
[Telredor stutter fix](telredor-stutter-20261104.md): strict duplicate resize
filtering and preservation of surviving CPU texture data across device Reset.

## Artifact

- File: `releases/Northlight-0.3.217-test.20261104.7-Windows.zip`.
- Size: 20,484,549 bytes.
- ZIP SHA256: `3b4f2ee51e7172d619823f76327bf1af6d82dd9b55b4beb85f1a968f25549a06`.
- DLL SHA256: `b15e7c4d9bd4046fcc25863ef6c1dac29e9baac5d7f12432768ecd2b6a5d3538`.
- Renderer source identity: `e218dc7e2722fdf4b868be469e2c8ceac924b5ab2d425b5e535ae29e4f9f1c97`.
- Implementation commit: `9ed294b01d066d3e03541a787210db781cd544bb`.
- A `.zip.sha256` sidecar accompanies the archive.

## Acceptance

Production build passes with pinned Zig 0.15.2 and unchanged shader freshness
gates. Canonical package verification, every ZIP CRC, all 48 application files,
the embedded DLL/source identity and dependency pins pass. Bundled isolated
Python runs all six packaged entry points from a Unicode path. Packaged-parent
MPQ ownership checks accept the real owned archives and reject foreign,
tampered and merged controls.

The packaged installer was also checked read-only against
`D:\Games\335_engb_mod`: the world cache is `up_to_date`, all six instance
groups are retained, and no rebuild/removal is scheduled. All explicit cache
versions remain 1. A DLL/source change alone does not invalidate them; a real
cache-version change or changed archive fingerprint still does.

Evidence: `out/installer-20261104.7/installer-acceptance.json` and
`out/installer-20261104.7/packaged-cache-plan.json`. Renderer regression results,
negative controls and the limits of the historical capture are documented in
the linked investigation. No whole-game FPS gain is claimed from contended
timings.

Extract the ZIP and run `Install.cmd`. The package was not installed into the
game during validation. `Wow.exe` is unchanged.
