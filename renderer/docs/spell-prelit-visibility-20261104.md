# Spell radiance and prelit WMO receivers

The changes apply by native draw/shader semantics, without map IDs, instance
names or a global brightness increase. They address two independent faults.

## Early spell composition

The native bloom copy legitimately drains queued spell draws before world
composition. The original coverage path protected alpha overlays, but omitted
additive `ONE/ONE` and `SRCALPHA/ONE` draws. Their scene depth belongs to the
surface behind them, so surface AO, shadows, GI and fog darkened their radiance.
The native bloom copy and draw order remain intact.

`effect_coverage.h` now records additive RGB separately from alpha coverage in
the original geometry submission. Alpha layers attenuate earlier radiance;
opaque receivers erase it; multiplicative layers modulate it in native order.
The final compositor attenuates surface corrections by the native contribution
of each colour channel. Additive RGB does not become opacity. Lit alpha banners,
projected shadows, unknown shaders and water ripples retain receiver behaviour.
The real wrapper admits additive Model/Projected families as well as particles
and proven unlit models. It uses the shader actually bound during replay.

On frames with early additive draws, one alpha-only fullscreen pass packs the
existing coverage into emission alpha. Composition reuses sampler 7 and c79;
the combined HDR/material shader shares the weight rather than fetching it
twice. Consecutive batches rebind on blend-class transitions, avoiding SM2 MRT
holes and duplicate surface bindings. Derived shaders are forgotten before
their source objects can be reused.

Read-only inspection of this client's 1,722 `spells/*.m2` assets found material
blend modes 0–6 and particle modes 1–5, including 894 ribbons. Additive mode 4
accounts for 2,553 material records and 4,644 emitters. The actual
`deathanddecay.m2` material is flags/blend `17/4`; its rune model combines mode 4
with an alpha layer. This is asset provenance, not a recording of every cast.
The supplied ON/OFF screenshots are different combat moments and cannot serve
as an exact pixel-pair oracle.

## Prelit buildings and instances

Some WMO vertex programs use authored vertex colours without reading native
global lighting. Applying the outdoor direct/GI replacement to these pixels
subtracted light that was never present, darkening rooms unnecessarily.

Registration now proves prelit RGB using the exact known WMO identity and a
backward dependency slice: vertex colour must contribute; normals, relative
constants and external global-light constants c10–c12 must not. A failed
outdoor-light decoder alone is not considered proof. Classification happens
once at shader creation, not by scanning assets during frames.

A separate bit in the existing world stencil selects SDR/HDR prelit composition.
This preserves authored light, AO, native and added fog, and bounded shadowed
local-light contribution. It excludes inappropriate outdoor directional/GI/night
replacement and material relighting. It allocates no additional full-size image
and replays no geometry. Name, unlit and prelit marks overwrite each other
correctly; native stencil ownership disables the mask path. Ordinary receivers
keep their previous composition.

The read-only client test inspects 15 scenes: ICC, Shattered Halls, Shadowfang,
Stockades, Deadmines, Onyxia, Blackwing Lair, Karazhan, Ramparts, Utgarde Keep,
Utgarde Pinnacle, Nexus, Naxxramas (`Stratholme Raid`), Ulduar and the ordinary
Stormwind city WMO. It audits 1,179 registered WMO programs available across
stock archives, including superseded versions: 108 pass the prelit proof and
all 255 independently proven outdoor-light programs are rejected. The catalog
also contains 788 historical identities whose bytecode is absent here; those
were not tested. No client assets or compatible versioned caches are rewritten.

## Validation

Local evidence is under `out/spell-visibility-20261104/` (not committed):

| Check | Result |
| --- | --- |
| Effect coverage, native D3D9 | 1,024 native draws, 5,632 compositor cases, 32 mixed-order cases; exact native RGBA |
| Effect coverage, each DXVK 2/3 | 1,024 native draws, 11,264 compositor cases, 32 mixed-order cases; SM2/3, MSAA 0/2; max error 0.00071013 < 0.003 |
| Actual wrapper / smoke sky | 56 family-admission and 160 smoke cases per backend/sample configuration; native + DXVK 2/3, MSAA 0/2/8 where supported |
| Prelit/local brightness, native + DXVK 3 | 1,512 cases each, 94 old-darkening negative controls, 1,260 shadowed-local-light controls; max error 0.000937462 < 0.0015 |
| Full world pipeline, DXVK 3 MSAA 0/2 | SDR/HDR, combined/two-pass/fallback, prelit material invariance, isolated name/unlit classes, no extra full-size images |
| HDR frame, DXVK 2/3 | 2,893 cases per backend; exposure, headroom, fog/material and fixed-function compatibility |
| AO methods/quality | Existing numerical, native/HDR and grazing controls pass; approved Composite hash updated after GPU validation |
| Host regressions | Spell ordering/hazards, unlit/prelit proofs, world/model context, stencil, grass, fog, rain, shadow smoothing and UI/water boundary pass; sanitizer variants where supported |

No numerical tolerance, resource budget, shader instruction limit or existing
skip was relaxed. The new optional client executable test explicitly requires
Windows. Rain-manifest checks now include the two new prelit shader entries;
grass wiring checks track the added output parameter. Historical unrelated
shader hashes remain checked.

An alternating-block 1920×1080 benchmark with 64 native quads (16 additive,
48 alpha) measured median capture cost on packaged DXVK 3: GPU 0.262112 →
0.316544 ms (+0.054432), CPU submission 0.020275 → 0.040969 ms (+0.020694).
This measures this component on this machine, not whole-game FPS. The extra
emission image is approximately 7.9 MiB at 1080p/32-bit, plus a matching MSAA
surface when needed. Opaque-only frames still make no capture device calls.

Native D3D9 MSAA retains the existing compatibility path; capture there is not
certified. Unsupported pixel programs/resource failures retain native drawing.
Early in-place fixed-function draws without a pixel program cannot use this
MRT patch; ordinary deferred fixed-function replays retain their tested path.
No full in-game sweep of every spell or every instance was performed.

Production DLL build and shader freshness gates pass with pinned Zig 0.15.2.
Source identity: `1273241b13cf747d26bb76c0e35a2ca8c879a0b76e1859c66357f7ba94ec20b2`.
DLL SHA-256: `c249ee9dadb7050d41cb2510e579aee31be6e521dd3cb4df192b58a640c98661`.
`Wow.exe` remains SHA-256
`99155d4d6ed4937a5ab1f5972d11e8605b986561b9f08686ab66af2c582194ec`.
This follow-up builds the DLL, without replacing the installed client or
repackaging the previous installer.
