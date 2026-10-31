# Camera-dependent material lighting

The report identifies wood in Dolanaar's inn and the gold piles in Darnassus's
bank, with relief/shading changing during camera movement. An offline GPU
reproduction establishes a material-normal defect; it does not replay the
reported game draws.

Material capture rebases a generated normal relative to its surface reference.
Final lighting rebases that detail onto the reconstructed scene normal, which
can differ from the capture reference. Performing these operations in view
space makes the resulting world normal depend on camera rotation. Terrain
already used a fixed frame; WMO, M2 and grass retained the view-space path.

All material domains now encode the relative perturbation in world space.
The draw hook supplies the current validated camera inverse, independently of
the WMO/M2 model-view transform. Terrain retains its draw-local view rotation.
Immediate and deferred capture save, remap and restore all nine control rows.
The existing signed-depth marker selects world-space decoding.

Walls and undersides also require a safe reference near the codec's -Z pole.
The octahedron's lower hemisphere records the reference inversion, with an
equator tolerance preventing numerical sign changes on vertical walls. Neutral
detail still preserves the reconstructed scene normal and its magnitude.

Capture POM uses the original explicit previous-height fetch. Native POM keeps
its exact-coordinate reuse. This preserves the reference ray result while
keeping two-layer capture within the 512-slot SM3 limit: 494 ordinary and 501
shared slots. Material detail, POM and displacement remain enabled.

Validation:

- Native D3D9 and DXVK each pass 1,476 codec cases: six surface orientations,
  41 yaw/pitch poses, neutral and non-neutral detail, matching and differing
  references. The legacy control changes a normal component by up to
  0.312035203; the fixed codec changes by zero. Maximum neutral error is
  1.1920929e-7 and matching-reference error is 0.000843546528, under the
  unchanged 0.0015 tolerance.
- The active client's `mm_ne_trim_03.blp` and `goldpilebase01.blp` each pass
  275 production material/capture/state cases on both backends. These use
  authored fixture geometry, not a complete inn/bank scene.
- Both native and capture POM paths pass 55 paired ray cases each against the
  independent full-fetch reference, including coordinate-rounding and
  zero-iteration controls. Maximum error is zero.
- Native D3D9 accepts all 896 mounted M2 pixel variants, preserving native
  alpha and opaque coverage; worst alpha error is 2.98023e-8.
- Material variant ownership, capture target/failure restoration, constant
  spans and private-depth checks pass.
- An isolated Windows x86 DLL builds from `66d0e6ed` plus this repair. This
  avoids including concurrent renderer work in the test DLL.

Private inputs, logs and the isolated build are under
`.local/wmo-material-flicker-20261031/`. The permanent camera and POM regression
runners are `tests/test_material_frame_rotation_gpu.py` and
`tests/test_material_ray_work.py`. No game was launched and no installed client
file was replaced. The exact reported locations still need in-game acceptance;
this does not establish that every shadow, displacement or cache-related
flicker in those scenes has the same cause.
