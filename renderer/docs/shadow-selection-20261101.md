# Visible local-shadow selection and bounded handoffs

The reported [Stormwind corridor recording](https://youtu.be/q3Ib5gRctcM) still
shows transitions with four shadow sources, and fewer with thirty. Increasing
the pool masks two selection defects: wholly offscreen sources consume slots
before the draw pass skips them, and rank changes replace a full correction
with another full correction in one frame.

Apply the existing conservative light-sphere scissor before ranking sources.
Invalid views retain the previous fail-open behavior. Keep source identities
attached to their cube slots, fade outgoing corrections over 200 ms, then reuse
the released slot and fade in the replacement. No extra cube, render target or
pass is allocated for a transition. Missing direct-light sources are discarded;
unready cubes restart at zero rather than revealing a correction after a
capture deferral. The current source color, attenuation and sunlight relevance
are preserved; the fade multiplies the final correction using c2.z.

`PointShadowLimit` now accepts 16..128 and defaults to 64 in every preset, the
native menu and the shared packaged quality profile. Older explicit values
below 16 follow the parser's existing invalid-value fallback to 64. Valid saved
overrides remain effective. The direct-light selection continues to reserve at
least the configured shadow budget at every GPU budget level.

## Validation

- The portable production selector passes optimized and unoptimized Windows
  x86 checks: limits 4/16/30/64/128, offscreen competition, 250 bounded source
  transitions per run, source zero, reordered inputs, live shrink, missing
  sources, capture refusal and tick wrap. Four/thirty remain internal regression
  inputs; the user-facing range starts at sixteen.
- Actual WorldRenderer pool submission passes on native D3D9 and DXVK 3:
  sixteen visible sources produce sixteen ready cubes, and sixty-four produce
  sixty-four, despite equally large sets of brighter offscreen competitors.
  The previous terrain-membership, six caster distances / 54 capture states,
  upload handoff, skipped-capture, map-change and visibility regressions pass.
- The compiled correction passes 360 scenarios / 297,480 receiver samples on
  each of native D3D9 and DXVK 3, with 128 verified shadow sources. Zero,
  quarter, half and full correction strengths match the reference. Maximum
  absolute error is below 0.000023. Replacing only the corrected shader with
  the previous bytecode fails 28,944 checks on each backend.
- Existing source-selection/direct-light and single/batched local-light GPU
  checks pass. The native quality parser/preset check and overlay draft/routing
  check pass. Linux-oriented settings-service fixtures still do not compile
  with Windows wide filesystem paths; no settings-service implementation is
  changed here. The larger previously recorded suite has not been recertified.
- Offline Microsoft shader compilation passes with the explicitly accepted
  compatible compiler distribution, with its actual SHA recorded in the
  manifest. LocalLighting uses 467 SM3 slots and 13 temporary registers.

Private logs and negative controls are under `.local/shadow-selection-20261101/`
and `.local/out/test-output/20261101-023049/` / `20261101-023344/`. No game was
launched, installed package replaced, or client configuration/cache modified.
These checks reproduce the selection defects and correction discontinuity;
the exact gameplay route still needs a user retest.
