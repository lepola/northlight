# Effects-disabled HDR performance investigation

The November 4 Stormwind harbour capture exposes a water/HDR interaction that
remains expensive with GI, shadows, local lights, fog, AutoBump and contact AO
disabled. The capture runs at 3840×2160, MSAA 2, with scRGB HDR active.

## Evidence

The input is the game log from build source identity
`e218dc7e2722fdf4b868be469e2c8ceac924b5ab2d425b5e535ae29e4f9f1c97`,
saved privately as `out/effects-disabled-20261104/renderer-input.log`, and
`northlight-diagnostics/session-43820-217880031-0/capture-1-*`.
Settings changes establish that the listed consumers are off after frame 2989.
The diagnostic capture is frame 3368 and is excluded from performance evidence.

At frames 3429, 3556 and 3683, GPU scene time is 23.116–23.439 ms; effects take
1.028–1.032 ms. The two complete focused intervals ending at 3489 and 3609
report 36.65 and 36.54 FPS. Background intervals are excluded: they include
the client's 30 FPS background limit. `GameScene` includes Northlight's
injected draws and must not be interpreted as unmodified game cost.

Both 600-frame windows from 3600 to 4800 contain exactly 332 HDR compatibility
layers per frame, each covering all 8,294,400 pixels. Frame 3429 records 335
clears and 341 `StretchRect` calls. Persistent layer work, rather than the
single diagnostic export, accounts for the identified waste.

The logged rejected shader hash `e79938975a92b584` matches the production
water-MRT transformation of `psliquidwater.bls`, PS 3 variant 0. The original
client shader has hash `7d4f078fa1876a09` and resolves from `Data/patch.MPQ`.
The derivative's extra `oC1` output carries the water mask. During HDR
foreground replay the auxiliary targets are explicitly unbound, but the shader retains
that output. The colour-transfer patch rejects it and invokes the full-screen
compatibility path for every affected water draw. The log label `path=sky`
also covers world-alpha foreground replay; it does not establish that water
was rendered in the sky depth range.

Private evidence is in `out/effects-disabled-20261104/log-evidence.json` and
`hot-shader/identity.json`. Client shader bytes and capture buffers are not
included in the repository. The renderer fix does not require changing the
client executable or regenerating world caches.

## Changes

The shared HDR colour-transfer patch preserves native auxiliary colour
outputs. It still rejects collisions with a requested new HDR output, depth
writes, incomplete primary colour, malformed bytecode and resource-limit
violations. Foreground replay can therefore transfer the water shader
directly on its geometry instead of clearing, resolving and compositing a
full-screen layer. The water mask's original instructions remain unchanged.

The bounded HDR shader banks now retain each original shader until removal
or reset. Internal water shaders can be replaced without passing through the
game shader-registration hook; keeping their owner prevents a recycled COM
address from selecting an old cached transfer. Cache hits add no reference,
bytecode read or compilation. The existing limit remains 128 entries per bank.

Terrain snapshots are also skipped when shadows are disabled and no
AutoBump position-bounds consumer exists. All live terrain GPU consumers
already require shadows; GI uses cached scene geometry. The CPU/GPU caches
remain warm, native buffer changes still invalidate them, and turning
shadows back on resumes capture immediately.

## Regression checks

- Portable HDR patch tests cover both shader models, all auxiliary-output
  combinations, exact preservation of masks/modifiers, output collisions,
  depth writes and incomplete primary colour. The old condition fails the
  new acceptance assertion.
- Native D3D9, DXVK 2 and DXVK 3 terrain tests cover eight dormant frames,
  zero device/source reads, retained cache entries and GPU buffers, exact
  material bounds, zero-upload re-entry, buffer mutation, both user-pointer
  routes and unchanged model routing. Removing only the two demand gates
  makes the same test fail its zero-work assertion.
- Seven WSL HDR/water/capture tests pass, as do the existing smoke/sky and
  spell-family GPU regressions on native D3D9 and both DXVK versions,
  including MSAA 0/2/8 where supported. Native scene pixels remain exact.
- The freshly built DLL passes eight presentation runs: DXVK 2/3 with SDR,
  HDR, MSAA 2 HDR and triple-buffered MSAA 8 HDR. Shader/constant restoration,
  UI transfer, resizing, Reset and Present checks remain enabled.

Terrain evidence: `out/effects-disabled-terrain-20261104/terrain-capture-demand/validation.json`.
Smoke/sky evidence: `out/effects-disabled-20261104/smoke-sky-final.log`.
Production-DLL evidence: `out/effects-disabled-20261104/proxy-final/validation.json`.

The dedicated water test passes 972 admitted cases on the final headers:
108 authored dual-output cases on native D3D9 without MSAA, and 216 cases
each at MSAA 0/2 on DXVK 2 and DXVK 3. DXVK tests use both an authored
fixture and the exact client shader with the production water transformation;
the latter's hash matches the captured rejection. Original/derivative native
RGBA and water-mask bytes remain exact. Numeric mask distance/coverage and
independent HDR/alpha bounds also pass. Every admitted transfer submits one
source draw with zero full-screen clears, resolves or composites.

Native D3D9 rejects the unmodified production derivative at shader creation;
its valid authored MRT/MSAA 2 control also fails before HDR conversion. These
unsupported controls are recorded explicitly, not counted as admitted HDR
tests. The native HDR test uses a valid authored dual-output shader at MSAA 0.
No production water-transformer or backend-admission behavior is changed.

The CPU-only shader-cache lifetime test verifies deterministic address reuse,
both banks, failed entries, the unchanged capacity, reset/forget/UI-white
cleanup and zero additional work on repeated hits. Its old-behavior control
fails the retained-owner assertion.

## Controlled GPU cost

The private test submits 332 water draws at 3840×2160, MSAA 2, using the actual
client pixel shader and production water derivative. Warmed ABBA runs collect
16 timestamp samples per mode. Sparse and dense geometry are controlled
workloads, not a replay of the whole Stormwind frame.

| Backend | Geometry | Before, ms | Direct transfer, ms |
| --- | --- | ---: | ---: |
| DXVK 2 | Sparse | 30.289 | 0.518 |
| DXVK 2 | Dense | 65.945 | 13.308 |
| DXVK 3 | Sparse | 32.330 | 0.382 |
| DXVK 3 | Dense | 67.783 | 12.920 |

Each case removes 332 full-resolution clears, 332 MSAA resolves and 332
composite draws. Timings cover the HDR replay segment and may include CPU
submission gaps; they do not predict whole-game FPS. Timing was completed
before the source-owner retention guard, which changes cache insertion and
cleanup only. Final correctness was rerun with that guard enabled.

Evidence: `out/effects-disabled-20261104/hdr-water-transfer/validation.json`
and `benchmark-validation.json`, including backend hashes, shader provenance,
GPU/process load, per-run samples and source hashes.
