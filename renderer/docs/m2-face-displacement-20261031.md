# Skinned eye shells and facial seams

The reported Draenei eye bleed and Human eye/chin deformation exposed a gap in the subdivision boundary rule. Original seam vertices and edge samples were pinned, but a triangle made entirely of seam vertices still acquired displaced interior samples. The minimum-detail centroid fan explicitly created one. Nearby native eye/glow geometry could consequently pass depth inside the altered surface.

For a skinned mesh, interpolate a relief weight of zero at boundary vertices and one at interior vertices. Scale both CPU height offsets and GPU-height direction attributes by it. All-boundary triangles retain their original topology as well as shape, avoiding unnecessary interpolation changes; connected interior triangles retain signed relief with a continuous taper at the seam. Existing original-vertex pins and varying-skin rejection remain. Normal/cavity shading, rigid/world displacement, settings, shader payloads and budgets are unchanged.

## Offline validation

- The added CPU assertion fails on the previous PR head `f4014eeb` because its thin skinned patch acquires nonzero offsets. It passes with the fix, along with existing geometry, snapshot and atlas-upload checks. A connected interior retains positive and negative relief, with matching CPU/GPU attributes.
- The hidden-device oracle passes 12 cases each on native D3D9, DXVK 2 and DXVK 3: three subdivision factors, two strengths and CPU/GPU-height paths. CPU/GPU colour outputs match exactly. An additive eye patch behind the thin surface produces zero leaked pixels. Removing the skinned boundary protection produces 1,010–2,736 leaked pixels per case, so a disabled or ineffective test cannot pass.
- Six local MPQ-derived Human/Draenei head/body/eye meshes pass CPU/GPU attribute comparisons. The thin Human and Draenei eye shells retain zero displaced samples; connected body meshes retain nonzero interior samples. These checks use actual source topology with an authored constant height field; they do not reproduce the screenshots' exact composed skin atlas, pose, lighting or complete game draw sequence.
- The existing native D3D9 M2 material matrix passes all 40 SM2/SM3, rigid/single-/multi-bone/varying-skin and combiner cases. Native animation, normal/POM shading and binding/constant restoration remain checked. Its two-triangle skinned fixture now requires the original two triangles and zero depth change rather than assuming every prepared mesh must subdivide.
- The standalone Windows x86 renderer DLL builds successfully. Generated shader payloads remain unchanged.
- `test_displacement_state_reuse.py` fails its existing unsupported-range-program assertion at `displacement_state_reuse.cpp:546`. The unchanged PR head `f4014eeb` fails the same assertion in a separate baseline checkout. This is not counted as a passing check or repaired by this geometry change.

Reproduce the portable geometry checks with `python tests/test_material_geometry.py` on Windows with Zig configured. Run the GPU oracle with:

```text
python tests/validate_m2_displacement_seams.py --backend <plain-D3D9-or-DXVK-DLL> --client
```

Omit `--client` for the authored regression alone. The script checks that WoW is closed, renders to a hidden device and writes only ignored test output. Extracted client meshes stay local. No game was launched and no installed DLL, setting, archive or world cache was changed. Screenshot-level visual acceptance remains a subsequent in-game check.

## Follow-up: pinned interior vertices (2026-11-01)

QA's material-on/off face comparison prompted another geometry check. The seam
taper above approaches one at an interior original vertex, but that vertex is
itself pinned to zero displacement. Subdivision therefore sharpened an artificial
dimple around each pin even with constant height. The new regression fails on
the previous implementation at its bound on displacement versus distance to the
nearest original vertex.

Skinned relief now also uses the continuous barycentric factor
`3 * (a*b + b*c + c*a)`. It is zero at original vertices, one at triangle centers,
and agrees on shared edges. It multiplies both CPU offsets and GPU-height
directions, retaining the existing seam taper, topology, native vertex pins and
signed interior relief. Rigid/world geometry and material shading are unchanged.

The CPU geometry/snapshot/upload suite passes, including positive/negative
height and subdivision factors 4, 8, 16 and 64. Native D3D9 and DXVK 3 each pass
12 GPU cases with exact CPU/GPU colour parity and zero eye bleed; all six local
Human/Draenei meshes retain CPU/GPU attribute parity. The Windows x86 renderer
DLL also builds successfully. Local output is in `.local/m2-pin-check/`;
the built DLL is `renderer/frd9.dll` and has not been installed.

This establishes a displacement defect and its repair, but does not establish
that it accounts for every artifact in QA's screenshots. The exact composed
skin atlas, animation and full draw sequence have not been replayed. QA still
needs to compare the same face with M2 Normal maps and Displacement + normals,
keeping the material master switch and all other settings enabled.

## Textured Human female strength sweep: pin taper alone (2026-11-01)

The requested default-face check uses race 1, sex 1, skin 0 and face 0 from the
mounted client's `CharSections.dbc`, composed with its base, upper-face and
lower-face textures. Both native base geosets are included; hair is hidden.
The production height inference, subdivision and CPU/GPU displacement patches
are exercised at model subdivision 2 with strengths 0, 10, 25, 50, 75 and 100,
from front and +/-25-degree views. Previous and current geometry implementations
were rendered through native D3D9 and DXVK 3: 144 images total.

**The pin-taper fix is insufficient for the full strength range.** At 10 and 25
the inspected face remains visually clean in this fixture. At 50 the eye region
starts clipping; 75 and 100 show clear eye, brow and lip distortion. Displaced
facial skin changes occlusion against the separate, undisplaced eye geoset.
The current fix reduces this effect but does not prevent it.

| Strength | Front-view eye pixels exposed/hidden versus zero, current fix |
| --- | --- |
| 0 | 0 / 0 |
| 10 | 1 / 0 |
| 25 | 1 / 0 |
| 50 | 13 / 59 |
| 75 | 89 / 189 |
| 100 | 207 / 302 |

The zero-strength front view has 1,641 visible eye pixels. At 100 the previous
code changes 852 eye-coverage pixels; the current fix changes 509. All 72 colour
images match exactly between native D3D9 and DXVK. CPU/GPU geometry attributes
agree within 1e-6, but raster outputs are not bit-identical: the current sweep
has a maximum 33/255 channel difference at one pixel in the -25-degree, 75%
view (the remaining differences in that pair are at most 2/255).

Inputs, source harness, raw readbacks, `validation.json`, `strength-sweep.png`,
`before-after.png` and `eyes.png` are local in
`.local/m2-face-strength-20261101/`. Reproduce with its `prepare.py`, then
`run.py --backend <plain-D3D9-or-DXVK-DLL> --name native` (or `dxvk`), then
`summarize.py` after both backends have run.

This is a bind-pose geometry-isolation test with neutral fixture shaders. It
does not include the live animation, generated normal/POM shading, shadows,
AO or complete native draw stream. In particular, it does not reproduce QA's
artifact at the pictured 25% setting and cannot certify that setting in-game.
No game files or settings were changed and WoW was not launched.

## Completed geometry fix: relief bounded by local feature size

The strength sweep exposed a second cause: inferred texture contrast was allowed
to move small anatomical features by the same absolute distance as large model
surfaces. At 100%, the default Human female body mesh had offsets up to 0.0144
game units, enough to cross its separate eye geometry. Smoothing the original
vertex pins alone could not prevent this.

Skinned geometry now limits relief using each vertex's shortest incident edge.
At the maximum model span (0.12 at 100%), the height-map peak is bounded to 5%
of that edge length. These weights interpolate continuously with the seam and
pin tapers in both CPU and GPU-height paths. Strength remains linear; displacement
is not disabled. Rigid geometry retains its previous relief. The default Human
female body's maximum offset is now 0.00091065; its eye shell stays undisplaced.

The final check renders the default skin/face through actual client SM2/SM3
vertex shaders and `NorthlightAutoDisplacement::prepare`/`draw`, including the
production atlas upload, height generation, mesh cache and bindings. All 72
cases pass: 0/10/25/50/75/100%, front/+25/-25 degrees, native D3D9/DXVK 3 and
CPU/GPU-height paths. Every nonzero strength changes the rendered surface.
Native and DXVK colour outputs match exactly. No baseline eye pixels disappear;
at most two edge pixels become visible. The same acceptance rule rejects the
old code, which changes 852 eye pixels in the front view at 100%. Visual checks
also show the lip and brow distortions removed in these views.

Final regression results: geometry/snapshot/atlas-upload checks pass; both
backends pass all 12 seam cases and six actual Human/Draenei meshes; the native
40-case model-material/animation matrix passes. The fine-feature relief bound
fails on the previous implementation and passes with the fix at subdivision
factors 2/4/16/64, positive/negative heights and CPU/GPU paths. The renderer DLL
builds successfully.

Final evidence is in `.local/m2-face-strength-20261101/fixed-validation.json`,
`fixed-before-after.png` and `fixed-strength-sweep.png`; run `validate_fix.py`
there to check the saved render results. Runtime captures can be reproduced
with `run.py --backend <DLL> --name runtime-<backend>-sm<2-or-3>
--native-model <2-or-3>`. These remain offline bind-pose renders with texture-only
pixel shading; they do not replay QA's full animated game scene, normal/POM
shading or AO. The completed fix resolves the reproduced geometry corruption
across the requested strengths. The built `renderer/frd9.dll` is not installed;
no game files or settings were changed.
