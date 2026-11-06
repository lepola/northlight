# Equipment emitter energy regression, 2026-11-04

The installed `item\objectcomponents\weapon\knife_1h_utgarde_d_03.m2`
reproduced the reported overbright light. Its three generated native lights
multiplied calibrated texture/lifetime radiance by emission rate times particle
lifetime. The source model in `Data/patch.MPQ` has no native lights; the generated
version resolved from `Data/enGB/patch-engb-z.mpq`.

| Original bone | Installed intensity | Restored intensity | Extra multiplier removed |
| --- | ---: | ---: | ---: |
| 4 | 0.04603228 | 0.01644010 | 2.8 |
| 5 | 0.07251424 | 0.00805714 | 9 |
| 6 | 0.28245661 | 0.00564913 | 50 |

The adjacent crash-investigation chat records the same increase. A later user
confirmation established that equipment emitted light, not acceptance of these
intensity values. The persistent native-light reader can expose this amplified
energy even when the visible-particle path does not replace it with a measured
light. Higher energy also brightens distant receivers under the existing falloff.

The generator now preserves the calibrated radiance for equipment, as it already
does for other attached emitters. Original bone attachment, enabled-state and
rate-relative animation remain intact. No renderer/shader logic or per-frame
work was added. Native lamp transport and its distance cutoff are unchanged.

The real client audit generated 2,568 models and validated all 6,650 native
light records with MPQ byte roundtrips. Reproducing the previous generator
matched the installed versions before replacement. Both owned Northlight
patch-Z archives received 2,279 corrected model entries; 1,274 other payloads
were unchanged in each archive. Ownership manifests were regenerated and
verified. The source MPQs, native model geometry/animation and gameplay tables
were preserved. The installed DLL was unchanged. `Wow.exe` SHA-256 stayed
`99155d4d6ed4937a5ab1f5972d11e8605b986561b9f08686ab66af2c582194ec`.
The generated source count remains 6,650, with no per-model count increase.

Validation:

- Equipment staging regressions reproduce populations 2.8, 9 and 50 and sample
  resulting native intensity throughout its animation. All three fail with the
  previous generator and pass with the fix; the focused generator suites pass
  24 tests.
- Native scene tests in O0/O2 cover calibrated light without model draws,
  particle/native replacement, far-field intensity, and unchanged lamp output.
  Actual client lamps and attachment fixtures also pass.
- Native equipment and particle O0/O2 tests pass. Native-install, mount and glue
  generator suites pass 29 tests.
- Existing offscreen D3D9 direct/fog tests pass 144 multi-light and 192
  single-light cases. These are synthetic receivers, not an in-game dagger
  screenshot or a new FPS measurement.
- Full art-layer tests pass for stock and custom clients, including repeated
  custom builds and matching payload/archive hashes. The test now classifies
  declared city/outdoor sky clones separately from attached models; unknown
  sky names remain rejected. All ownership and content assertions are retained.

Local audit artifacts and original archive backups are in
`out/equipment-energy-20261104/`; focused test logs are in
`.local/diagnostics/equipment-energy-20261104/`. Client-owned model binaries and
archives are deliberately excluded from the repository.
