# Materials, local lighting and native menu integration

October 25, 2026. Local integration with upstream main `e19ffee` (including the native settings overlay and celestial skybox changes). No game launch, optimizer modification, installation or publication is part of this acceptance.

## Preserving local work

All pre-merge work was committed as `a7b42e5`, with safety branch `safety/materials-before-upstream-20261025` and a separate patch/ZIP/hash snapshot. Upstream was fetched and merged without resetting our branch. The integration retains material profiling and distance admission, deferred capture, GPU displacement, memory retirement changes, live AO/equipment settings, HDR, source identity and the imported-input stand variant. Upstream's native settings service replaces the retired Lua/CVar bridge. The celestial shader sources and generated programs come from the merged implementation. Water/SSR appearance work remains removed.

## New menu

The production overlay opens with **Ctrl+Shift+F11** by default. All added controls use the same settings schema and transactional draft/preview/save path as the new upstream menu.

- **Materials & parallax:** M2 modes, ground/wall modes through Displacement + normals, supported grass modes, normal strength/range, terrain/object and model displacement strength/range/subdivision, height balance, POM controls, cavity and self-shadow controls, and their shared near distance.
- **Lighting & AO:** AO method and AO Quality, with live equipment/effect lighting preserved.
- **Renderer & HDR:** HDR enable/status, white point, UI white, maximum nits, contrast and exposure. Enabling HDR requires a restart; grading/white controls preview live. Actual HDR status and its reason are supplied by the renderer rather than inferred from the checkbox.

Closed-menu drawing retains the upstream fast visible check. The overlay draws after final HDR conversion and uses its own HDR UI shader in scRGB output. This preserves configured UI white instead of writing SDR encoded values directly into the floating-point target. No installed INI was changed during integration or tests.

The real panel, embedded Roboto font and DX9 backend rendered successfully on private targets for Materials, Lighting & AO and Renderer & HDR. Their SDR/160-nit/320-nit peak values are 1/2/4 in scRGB units. Captures were inspected after the async settings snapshot loaded. This checks native menu drawing and UI conversion; it does not test the Windows HDR swapchain/monitor metadata path.

## Terrain correction

The old capture normal was triangle-flat even for neutral generated detail, while final scene lighting used a smooth normal. The correction forwards the native authored terrain normal through the original view transform and uses it for terrain normal-detail lighting. POM ray geometry and native positions/colours/UVs retain their behavior. See [terrain evidence](terrain-material-seams-20261024.md).

The eight neutral/actual-texture capture cases pass with MSAA off/2x and immediate/deferred capture. Maximum neutral RGBA error is 0.0000596791506 under the original 0.002 limit; real texture detail remains nonzero (maximum change 0.399117351). Four additional complete mode runs cover bump, POM, self-shadow/cavity and GPU displacement on the folded fixture, with MSAA off/2x and the deferred option off/on. The MSAA deferred bump/POM cases actually queue and flush; non-MSAA falls back to immediate capture. These complete runs do not claim shared-POM reuse: their shared counters are zero.

No final-pass eight-depth-tap reconstruction was adopted. Failed derivative/depth candidates remain in the private evidence archive. No new terrain render target is allocated by this fix. The terrain-specific capture programs occupy 291/297 SM3 slots; the original programs for other domains retain their bytecode. This is not a claim that every ADT texture/layer/LOD seam has been reproduced.

## Shadow and coloured-light overlap

The old composite mixed negative global shadow correction and positive local illumination before enforcing its darkness floor. In shadowed receivers this could suppress both weak coloured sources. The implemented order filters/temporally stabilizes global correction first, bounds it against the original colour, then adds current local radiance. Local lights and their point-shadow subtraction stay together; retired/moving sources do not enter global temporal history. An existing dead light target is reused, with explicit unbinding and constant/state restoration.

The actual legacy pipeline is retained as a negative control. It loses coloured contributions on all 128 shadowed receivers in the fixture; the fixed pipeline loses none. Sixteen lighting/order/overlap/temporal combinations pass, maximum superposition error is 0.000488281, order error is zero, and retired-light trail error is zero. The fixture uses authored orange/blue sources, not a capture of the exact dagger/Cold Blood assets or their MSAA silhouettes. The fix adds current-local sampling to WorldComposite (363 slots versus 344 in the earlier program); no GPU-time saving is claimed for this visual repair.

## Rotation performance: what the latest log establishes

The final user session log ends October 24 at 11:41:11, SHA-256 `648056d0c12fd7dc7aa28bf844518c87c0fffa7c0b5b204f95a8b850c5c22bb5`. It contains 518 idle-gap records and 618 retained slow frames. The largest focused/applied joined gaps are dominated by producer-thread CPU while replay has no queued commands. For example, frame 23355 has a 212.1473 ms gap, 203.125 ms producer CPU, 1.065 ms effects and 0.018 ms GetData API time. This is evidence against attributing that whole stall to material GPU passes or query API execution. Coarse CPU accounting does not identify the specific game routine.

A separate Northlight memory boundary costs 42.291 ms in frame 9394. The implemented correction avoids evicting world entries to a smaller cap immediately before an already mandatory complete retirement. The authored populated-cache component median improves from 0.378240 to 0.219899 ms across three alternating pairs. This is a component result, not 42% game FPS uplift or a promise to recover the entire runtime boundary cost. See [memory boundary evidence](rotation-memory-boundary-20261024.md).

The available MPQ assets and recorded camera observations do not reconstruct current native producer CPU work, animated actor journals and complete draw/state transitions. Stable rotation FPS and a same-scene native-only comparison remain open. Enlarging caches is not supported as the principal remedy by this evidence.

## Focused acceptance and evidence index

All evidence below is under `D:/Games/335_engb_mod/northlight-diagnostics/rotation-scene-followup-20261024/`.

| Check | Result and evidence |
| --- | --- |
| Settings service, live preview and INI preservation | Pass; `merged-cpu-checks-1`. Existing transactional/error assertions retained; local HDR and material controls added. |
| Menu input/draft/font, GI dependencies, memory guards, horizon state restore | Pass after updating integration-specific harness assumptions; `merged-cpu-checks-2`. |
| Diagnostic gates | Pass; `merged-cpu-checks-3`. Explicit predicates/caps added for current instrumentation; diagnostics-off exclusion retained. |
| Shadow smoothing, local-light batches, rain-mask and removed water features | Pass; `merged-cpu-checks-1`. |
| Terrain pressure, world transition and celestial phase recovery | Pass; `merged-cpu-checks-1`; component/sanitizer/negative-control evidence also in `world-pressure-final-3`. |
| Merged shader compilation | Pass; `merged-shaders-2`, `merged-material-shaders-3`. Pinned compatible Microsoft compiler; no SM3 limit bypass. |
| Terrain neutral/real detail and state/depth | Pass for eight cases; `merged-native-terrain-fold-4` and `merged-native-terrain-fold-6`. |
| Terrain complete POM/displacement modes | Pass; `merged-native-terrain-modes-1`. |
| Actual legacy/current light overlap | Pass; `merged-local-overlap-2`. |
| Actual loaded native menu in SDR/scRGB | Pass; `merged-overlay-gpu-4`. |
| Normal and stand DLL/PDB build | Recorded in `merged-final-builds-1/validation.json`; final delivery requires both builds and exact PE/PDB identity to pass. |

Initial failed runs remain archived. No full test suite or whole-game FPS benchmark was run. Focused checks are not presented as proof of every native scene or driver combination. Generated header newline normalization changes no shader bytecode; the final build records a normalized source identity and checks unchanged source across compilation.

The deliverable is a separate DLL/PDB package with source identity and validation manifest. The stand DLL is for imported-input tests only and is not the game DLL. Installed DLL, user settings, log, MPQs, optimizer and client caches remain untouched; no push or PR update was made.
