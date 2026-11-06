# Stormwind lighting bands and the nearby lamp shadow

## Recorded evidence

The four captures in `session-65212-219876625-0` correspond to renderer frames
4141, 10276, 10467 and 29876 of the `20261104.8` DLL. The user identified a
missing separate character shadow in capture 1, a sharp illumination boundary
in capture 2, and disappearing illumination in captures 3 and 4. They confirmed
that switching Local Lights and World Shadows off did not remove the changing
illumination. Capture 4 is an effects-off control, not evidence that the user
expected those settings to remain enabled.

The changing warm patch is already present in the native `scene` and `hdr-base`
buffers, before the local-light and shadow composites. Captures 2 and 3 have
unchanged material settings. Their current-local-light mean RGB values are
0.00492185447 and 0.00492601795. Replaying the captured ordinary HDR compositor
reproduces the wall input, and the material compositor reproduces the final
wall/ground ROIs bit for bit. Removing only the local-light input produces a
positive lighting difference through both passes. Missing stencil exports are
not treated as reconstructed actor or fixture coverage.

Original client WMO groups 284 and 280 expose the same wall and ground boundary
as the images. The adjoining triangle endpoints have identical positions,
normals, UVs and MOCV colours. They belong to separate material batches. The
wall triangles span up to 17.35 yards; the ground triangles span up to 31.48
yards. The visible lamp in capture 2 is local slot 8 at
`(-8583.86719, 962.294617, 102.153038)`, not slot 0 behind the camera.

An authentic `MapObjDiffuse_T1` vertex shader evaluates nonlinear native point
lighting per vertex, then applies MOCV and clamps. Subdividing an otherwise
unchanged surface re-evaluated that expression at new vertices. In the GPU
oracle, zero-height subdivision changed the centre from RGB 12/11/11 to
255/161/65. The displaced shader and subdivided unmodified shader agreed,
excluding a displacement shader-state corruption as the cause of that change.

This proves a rendering defect, but does not identify the historical draw's
admission decision or native light-constant bank: those were not exported. In
particular, the reconstructed batch bounds remain inside the 12-yard range in
both captures; a distance-threshold crossing is not claimed.

## Change

WMO subdivision now retains the original triangle's position, normal and
colour inputs plus barycentric weights. The generated vertex shader evaluates
native COLOR0 at those original corners using the current constants, then
interpolates their clamped results. Original position, UV and fog dependencies
are retained; the replaced colour expression is removed. Native draws,
material capture and lazily created variants share the preservation rules.

The packed payload is at most 96 bytes per generated vertex. It is prepared
once per original triangle and belongs to the existing geometry cache. There
is no new draw pass, per-frame colour upload or CPU lighting evaluation. The
8192 source-triangle, 32768 generated-vertex/triangle, two-build-per-frame and
cache-byte budgets remain in force. Additional bytes are included in cache
admission. Unsupported expressions, declaration collisions, ambiguous input
semantics, stride/register limits and incompatible replay layouts retain the
original draw instead of reintroducing inconsistent lighting.

The result preserves authored native illumination; it does not make the
artificially amplified patch in capture 2 a new brightness target. Local
lights, shadows, exposure and their settings are unchanged.

## Capture 1: overlapping shadows

The installed lamp is `stormwindstreetlamp01.m2`, source
`14933062424609180460`, at `(-8564.942383, 1011.771301, 99.731544)`.
Its installed FGS SHA256 is
`877d120a6916b13d8640aa81012023b1df3bcae0c77b778e29e8977bb78ad95e`.
The luminous shell tag and source transform are correct. Opaque bottom-plate
triangle 49 at Z 99.328745 blocks the sampled curb rays before the character.

An independent controlled fixture uses that exact housing, light and placement
with a stock HumanMale mesh, 620 triangles and 46 bones. At the reconstructed
nearby foot position, the actor's shadow lies inside the housing's larger
shadow. Moving the same actor two yards exposes its separate shadow. Removing
the actor, or disabling actor shadows, restores the housing-only result.

The fixture uses a controlled pose and horizontal receiver. The historical
native actor mesh/pose was not exported, so this does not prove that specific
frame's actor admission. It establishes why a separate outline need not be
visible at this position. The opaque housing, emission anchor and material
classification were not changed to manufacture an outline.

## Validation

- All 72 registered stock WMO programs on native D3D9 and DXVK 2/3:
  2610 comparisons per backend, including 18 actual `prepare`/`drawBound`
  cases and 72 old-path negative controls. Tests use the original wall/ground
  seam attributes, CPU/GPU height paths, four current lighting banks and one
  cached geometry build. Native, capture and lazy variants restore the source
  shader, all 256 constant rows and vertex buffer.
- COLOR0 and alpha are checked against the original coarse native shader.
  Position, UV and fog are checked against the unmodified displacement path
  on the identical subdivided geometry. The one-byte bound is unchanged;
  pre-existing nonlinear fog interpolation is not relabelled as a colour fix.
- Full shader coverage found and fixed a VS2 temporary-register limit: the
  appended colour slice can reuse completed displacement/specular scratch
  within the twelve-register limit. All 36 SM3 programs' CPU/GPU variants
  remain byte-identical to the performance-tested versions.
- Independent O0/O2 tests cover 135 barycentric/current-constant cases each,
  clamped RGBA, partial temporary-register dependencies, unchanged other
  outputs, duplicate semantics, and rejection of unsupported matrix/relative
  expressions. Existing subdivision, state/cache reuse and bounds-rotation
  suites pass without changed acceptance limits.
- The housing/actor production GPU fixture passes native D3D9 and DXVK 2/3,
  both hard and soft filtering. The nearby actor adds zero cube texels or
  receiver pixels; the isolated actor casts 1137/1334 pixels. Moving it two
  yards adds 738 cube texels and 1842/2388 receiver pixels. Actor-off/removal
  controls are exact; deliberately omitting actor replay fails the positive
  control. The pose remains controlled, not a recovered historical pose.
- Pinned production compilation, existing smoke/sky/spell regressions and
  eight final-DLL SDR/HDR/MSAA/Reset/Present runs pass.

## Measured cost

On four original detail4 WMO batches, the packed/prepared payload adds
0.082–0.223 ms to a cold GPU-height geometry build. The 32768-vertex stress
case changes from 4.951 to 6.027 ms. These are cache-miss costs, not recurring
per-frame costs. Allocation-call counts, topology and common vertex bytes are
unchanged. The maximum additional vertex-buffer storage is 3 MiB per mesh,
inside the unchanged total cache budget; reduced cache density is a real
tradeoff.

An independent warm GPU component benchmark covers all three backends and
the heaviest registered SM3 variant: 203 slots, four active finite light banks,
with both colour and all six texture-coordinate outputs consumed. On DXVK3,
the representative wall changes from 0.712 to 1.568 microseconds per draw;
the 32768-vertex bound changes from 1.480 to 3.584 microseconds. Native D3D9
and DXVK2 show the same bounded increase. Each mode has eight ABBA samples of
64 draws. These use a tiny 32x32 target and warm vertex buffers/L2, so they
measure a vertex-work component, not the complete client frame or FPS.

Evidence: `wmo-native/performance-review.json`, its retained input reports and
the exact SM3 bytecode/source bridge under `out/lighting-bands-20261104`.

## Reproduction artifacts and scope

Local evidence is under `out/lighting-bands-20261104`: the frozen log,
capture previews, native/HDR/local comparison, exact compositor replay,
original WMO seam reconstruction, shader oracle, and independent housing/actor
controls. Client model and shader bytes remain local artifacts, not repository
fixtures. The captured-lamp test requires the recorded installed model hash
and refuses silently substituted geometry.

No game executable or installed data was modified. Persistent cache format
versions are unchanged; a new DLL does not force cache regeneration.
