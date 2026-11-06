# Windows installer 0.3.217-test.20261104.8

This replaces [20261104.7](complete-installer-20261104.7.md), retaining every
previous integrated change. It adds the [effects-disabled performance fix](effects-disabled-performance-20261104.md):
direct HDR transfer of water MRT shaders, safe shader-cache ownership and
terrain capture only when a consumer needs it.

## Artifacts

- Installer: `releases/Northlight-0.3.217-test.20261104.8-Windows.zip`.
- Size: 20,484,638 bytes.
- ZIP SHA256: `ee8cc836b8051f5b1576130624ae923130e5f55dffc319cfe2aa308d67f27867`.
- Standalone DLL: `releases/Northlight-0.3.217-test.20261104.8/d3d9.dll`.
- DLL SHA256: `f7567fc3ac1d5bd098bc7a423727df9093815735d58c6c88eb8409023abdae4a`.
- Renderer source identity: `d5d862160d18fdcf1199df5d36f97e4e8f39819ba4ebd708a7f10daaef78fb05`.
- Implementation commit: `925688172c0253f768b085e16041ffb26254c524`.
- SHA256 sidecars accompany both artifacts.

## Acceptance

The production DLL builds with pinned Zig 0.15.2 and unchanged shader
freshness gates. Exact water-shader GPU tests, shader-cache lifetime and
terrain-demand negative controls, existing transparent-effect regressions,
and eight final-DLL SDR/HDR/MSAA/Reset/Present runs pass. The linked report
records measured work, controlled timings and backend limitations; no
whole-game FPS is inferred from the offline workloads.

Canonical package verification passes, including every ZIP CRC, all 48
application files, embedded DLL/source identity and dependency pins.
Bundled isolated Python runs all six entry points from a Unicode path.
Packaged-parent MPQ ownership tests accept real owned archives and reject
foreign, tampered and merged controls.

Read-only planning with the actual packaged installer against
`D:\Games\335_engb_mod` reports `up_to_date`, no world rebuild, all six
instance groups retained and no removals. All explicit cache versions remain
1. A source/DLL change alone does not force cache regeneration; relevant
version or archive-input changes still trigger it.

Evidence: `out/installer-20261104.8/installer-acceptance.json` and
`out/installer-20261104.8/packaged-cache-plan.json`.

Extract the ZIP and run `Install.cmd`. Validation did not install the package
into the game. `Wow.exe` remains unchanged.
