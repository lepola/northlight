# QA native brightness preservation

The supplied logs both ran SDR: HDR was requested but inactive with
`reason=display-sdr`. The large log identifies renderer 0.3.217, source
`d1e4423db8aee534ac114af2935d1c4de910422aa654a320361923c7852eb093`,
built 2026-11-04 09:40:33 UTC. The diagnostic buffers identify 0.3.216,
source `e4087f41df808393e9bd9a4cad3fc8df7b079937accca3639ee872e6ab5f60ad`,
built 2026-11-02 19:13:34 UTC. Both predate the current authored-prelit WMO
and early additive-spell preservation fixes.

The two ArgentTournamentDungeon captures show encoded-luma ratios of
0.76793 and 0.76449 from pre-composition scene to final scene. Contact AO
means are 0.99310 and 0.99731; the actual composition fog input is identity.
Lighting correction medians are approximately (-0.149, -0.170, -0.191),
close to the maximum missing-probe removal of (-0.157, -0.178, -0.200) for
native ambient (0.31373, 0.35686, 0.4) and GI intensity 0.5. This identifies
ambient replacement as the dominant darkening mechanism, not an SDR/HDR
transfer or exposure setting.

The captures do not export the probe SH/metadata or stencil. They cannot
prove the per-pixel split between missing and resident probes, nor which
pixels are authored-prelit WMO. They are not an exact effects-ON/OFF pair or
a complete scene replay. The analysis does not claim that every dark pixel
in these older captures is repaired by the missing-probe change.

## Shared correction

`WorldGI` previously inferred ambient sky occlusion from one directional
shadow whenever a probe was absent. A blocked moon or sun does not measure
hemispherical sky irradiance. Missing probe data could therefore subtract
up to the GI intensity times the entire native ambient, including outside
the resident probe window. Maps with unavailable or changing GI data could
receive this darkening without a measured replacement.

Missing probes now contribute zero GI correction. Resident probes continue
to replace native ambient with measured sky and bounce irradiance. Their
spatial and first-publication fades remain continuous toward the neutral
native baseline. Proven dark resident probes, directional shadows, contact
AO, native fog, authored-prelit receivers and local lights retain their
existing behavior. No exposure boost, map exception, or absolute brightness
floor was added. Twilight/night policy remains unchanged.

The old missing-probe test expected invented ambient removal. Its oracle
now requires exact unknown-probe neutrality and retains explicit resident
occlusion controls. Error tolerances are unchanged. Prior bytecode must
fail the unknown-probe controls.

## Validation

`python tests/test_gi_fallback_gpu.py --baseline-ref 6c746ad89a06e7205a224286092275e99554da4e`
reconstructs the prior bytecode directly from Git. `--benchmark` additionally
runs alternating timing blocks; supplying a baseline alone runs correctness
and negative controls at the small fixture size.

Native D3D9 and packaged DXVK 2/3 each pass:

- 769 GI/visibility/readiness cases: maximum error 0.00024415 below 0.001, 450 exact
  missing-probe/readiness neutrality controls, 20 resident-dark controls, and 60 prior
  executable-bytecode negative controls. Full resident coverage is identical
  to the prior bytecode; sky and water remain neutral.
- 240 production WorldLighting -> WorldGI -> WorldComposite cases in SDR and
  HDR: maximum error 0.00040532 below 0.0015, including the captured ambient
  palette. There are 60 native-brightness controls, 288 retained direct-shadow
  controls, 96 resident-occlusion controls, and 48 contact-AO controls.

Existing HDR frame acceptance also passes on DXVK 2 and DXVK 3: 2,893
cases per backend covering transfer, grading, exposure, headroom, legacy
alpha and UI state. Results are in `out/qa-brightness/hdr-frame/validation.json`.

The actual WorldRenderer screen fixture passes on DXVK 3 with MSAA 0 and 2:
SDR/HDR combined and separate material paths, optional shader/capability
fallbacks, prelit/unlit/name stencil isolation, resource-failure recovery and
scene-pressure controls. Final results are in
`out/qa-brightness/world-pipeline/test-output/test_world_screen_effects_gpu/world-screen-gpu/validation-final.json`.

The first MSAA 2 attempt exposed a test-setup race before composition:
delayed driver virtual-memory releases left more free space than its old
budget refusal control allowed. The fixture now establishes pressure
against the midpoint of the actual old/new budgets, retaining an 18 MiB
contiguous witness and allowing bounded rebalancing before sampling. Both
original `!admits(oldBudget)` and `admits(newBudget)` assertions remain.
Final MSAA 0/2 runs each measure 346 MiB available: the 370 MiB old budget
is refused and the 322 MiB reused-page budget is admitted. Process reserve,
page identity/content, and injected-failure recovery assertions remain intact.

The final alternating-block GI-only DXVK 3 benchmark at 1280x720, on RTX 4090
after all other builds and tests completed, measured 0.013008 -> 0.012685 ms
without resident probes and 0.029964 -> 0.029291 ms with full residency.
`out/qa-brightness/gi-isolated-final.log` also records the passing correctness
and old-bytecode controls from that run. These sub-microsecond savings in one
pass do not establish a client FPS gain, especially on the reported RTX 3060.
An earlier overlapping run is not used as performance acceptance evidence.

Shader freshness and all 44 world binary/header/assembly/device-budget checks
pass, as does the eight-variant shadow-transition build audit. The dependent
transition manifest was regenerated through the standard compiler script; its
bytecode and embedded header remain byte-for-byte unchanged.
WorldGI drops from 242 to 233 static instruction slots and 12 to 11 texture
fetch instructions, removing sampler 12; temporary registers stay at 19.
Compilation uses the repository-approved compatible Microsoft DLL
`4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3`
with the existing explicit `--compatible-compiler` option. No shader limit,
compiler approval list, numerical tolerance or test skip was relaxed.

Offline numerical checks also pass for folded AO/bloom, removal smoothing,
214,608 material-height cases and 60,000 fog-footprint/material-range cases.
Local capture analysis and diagnostic PNG exports are in `out/qa-brightness/`.
No game process, client executable or installed renderer was modified.
