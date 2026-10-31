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
