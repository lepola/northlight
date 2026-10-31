# Terrain material seams: correction and evidence

Updated October 25, 2026 after integration with upstream `e19ffee`.

## Cause and implemented correction

The supplied Crystalsong screenshots show large facet boundaries when terrain material detail is enabled. The capture shader formerly built its lighting normal from triangle-flat `cross(ddx(p), ddy(p))`, whereas the final lighting pass used a smooth scene normal. Even a neutral generated normal therefore changed broad lighting across triangle boundaries. The preserved production-composite counterexample measured maximum RGB error 0.0505633354. This establishes a real defect, without identifying every boundary in the screenshots as an ADT chunk seam.

Recognized terrain vertex shaders now forward their authored NORMAL0 through the native c0..c2 view-vector transform into a free TEXCOORD5. Terrain capture constructs its normal-detail lighting frame around that interpolated normal. Position, native colour, UVs and the geometric POM ray frame retain their existing behavior. Shared POM capture retains the new varying. Missing normal inputs or occupied varyings keep the existing shader variant rather than corrupting the native program. Native SM2 and SM3 terrain disassembly was checked against the actual extracted MPQ shaders, including the original multiply/add ordering.

This adds three vertex instructions and terrain-specific capture programs; it adds no render target and no final-pass depth reads. Other material domains retain their compiled capture programs. The terrain capture programs occupy 291 SM3 slots, or 297 for shared POM. These are static instruction counts, not GPU timing measurements.

## Rejected alternatives

Final-pass derivative reorientation passed an aligned synthetic join but failed an odd join (0.0504828) and silhouette (0.18). An eight-depth-tap reconstruction also remained ambiguous on the actual native folded-mesh crease. Both approaches and their failing outputs are retained privately. Neither is in the delivered shader. Tolerances were not relaxed and surface detail was not disabled to pass the oracle.

## Focused GPU acceptance

Pinned backend: DXVK 3.1.1, SHA-256 `265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa`, on RTX 4090. The fixture uses extracted native terrain VS/PS and preserved MPQ-derived source texture/mips. Geometry is an authored folded mesh with continuous UVs and smooth authored normals, not a recorded game draw.

Eight cases passed: neutral/actual source, immediate/deferred capture, and MSAA off/2x. Neutral maximum RGBA change is 0.0000596791506, below the unchanged 0.002 limit. Actual-source maximum change is 0.399117351, proving visible detail remains. MSAA deferred cases queued and flushed one capture; depth and device-state restoration checks passed. Evidence: `merged-native-terrain-fold-4/validation.json` (the four non-MSAA cases) and `merged-native-terrain-fold-6/validation.json` (four MSAA cases), under the private diagnostic root below.

Four additional complete-mode runs passed in `merged-native-terrain-modes-1`: bump, POM, self-shadow/cavity, GPU displacement, MSAA off/2x and immediate/deferred options. Their shared-POM reuse counters are zero; they do not establish shared-reuse coverage.

The test explicitly binds the reference multisample colour/depth pair before RESZ. DXVK exposes RESZ with the same AMD identity used by Northlight's production backend configuration. Earlier tests without that identity silently failed to resolve depth; the nonzero-detail assertion detected the problem. Those failed runs remain preserved.

Reproduction: `tests/validate_terrain_material_seams.py --fixture <prepared-terrain-fixture> --backend <pinned-d3d9.dll>`. The runner uses private caches, hashes inputs, and rejects a running game.

## Limits

These checks cover the confirmed neutral-frame defect, real texture detail and capture/MSAA behavior. They do not establish every adjacent-ADT layer ordering, texture-edge/wrap combination, displacement LOD slope, animated model, or screenshot-specific native draw. Displacement still pins boundary vertices to preserve shared-edge positions; this normal correction does not prove all subdivision transitions are visually continuous. A same-scene game comparison remains necessary before claiming complete removal of the reported artifacts or any FPS uplift.

Private evidence root: `D:/Games/335_engb_mod/northlight-diagnostics/rotation-scene-followup-20261024/`. Installed files, MPQs and caches were not modified by these checks.
