# Complete Windows test installer .11

Version: `0.3.216-test.20261031.11`. Production code snapshot: `5359962a79e4e79574bf2683a67a89095b48d88b`, published in [PR #98](https://github.com/lepola/northlight/pull/98).

| Artifact | Identity |
| --- | --- |
| Archive | `Northlight-0.3.216-test.20261031.11-Windows.zip` |
| Archive bytes | 19,755,382 |
| Archive SHA256 | `b3b013b084dccbc8ab7cde128f138beea2b4afb3862b0f2be7be77c8f0c9a72b` |
| DLL SHA256 | `56d17abd0eb3af076771ac5c5b4e1a85d48ba7b93f675ce6dae88bef3817a6ea` |
| Embedded source identity | `aacba526bb94a3abee3e25edd995c2b7745851d6a05f85341a7bb5c447fd560b` |

This supersedes the earlier complete installer and the separate moving-light test package. It includes the [complete HDR/renderer assembly](all-features-installer-20261031.md) plus the subsequent fixes for floating-name masks, displaced crystal reflection layers, camera-dependent grass shadows and material lighting, animated world-light attachments, small fixture shadows, point-shadow upload handoffs, and native skinned character receiver normals. The integration reconciles the local and published histories without changing their production code. Test discovery now includes the existing HDR-log and point-shadow-visibility checks.

The corresponding independent PRs remain open: vegetation #76, present-wait handling #86, local shadows #92, HDR presentation #94, Diagnostics #96, materials/character normals #97, fog #109, native sun #110, EVENT waits #111, actor AO #112, shadow transitions #113, emitters/animated lights #114, tooling #116, scene storage #117 and AO variants #118. PR #98 contains the complete assembly. Rejected experiments and superseded backup snapshots are not release inputs.

## Fresh package validation

- Release Windows x86 DLL build and all shader source/manifest dependency gates pass.
- `tests/verify_packages.py` passes: payload hashes, pinned Python/StormLib/DXVK, dependency closure, package allowlist and installer contracts.
- ZIP CRC validation passes; the packaged renderer bytes exactly equal the freshly built DLL and contain the expected source identity.
- All six packaged entry points return zero for `--help` using the archive's own Python with `-I -B -X utf8`.
- Runtime: Python 3.13.15, DXVK 3.1.1 and alternative DXVK 2.7.1. No proprietary client assets or generated world cache are included.

The full Windows regression run is **not green**: **198 PASS, 37 FAIL, 222 SKIP, zero timeouts**. Skips reflect missing prerequisites or manual modes. Production and test contents match the already-published `81279572` snapshot except for the two discovery entries; no production repair is introduced by this packaging step. Failures remain open and are listed below.

The installer front-end suite completes 221 cases (6 skips); the Windows installer suite completes 62 cases (2 skips). The pinned StormLib build suite also passes. Earlier component acceptance retains its original source identity and scope. This run establishes neither new gameplay acceptance nor whole-game FPS. No game session or installation is performed.

| Failing check | Observed failure |
| --- | --- |
| `test_actor_shadows_wiring.py` | Source-wiring or reference assertion failed |
| `test_ao_actor_build.py` | Stored shader/performance baseline differs from the integrated source |
| `test_ao_blur.py` | Source-wiring or reference assertion failed |
| `test_ao_fog_capture.py` | Test fixture compilation: use of undeclared identifier 'skySeen' |
| `test_ao_fold.py` | Source-wiring or reference assertion failed |
| `test_colour_tent_2d.py` | Source-wiring or reference assertion failed |
| `test_five_path_perf.py` | Stored shader/performance baseline differs from the integrated source |
| `test_fog_temporal_wiring.py` | Source-wiring or reference assertion failed |
| `test_gpu_budget_wiring.py` | Source-wiring or reference assertion failed |
| `test_grass_receiving_order.py` | Expected source anchor is absent |
| `test_material_pipeline_cpu_micro.py` | Test fixture compilation: redefinition of 'transientBindings' |
| `test_model_world_context.py` | Test fixture compilation: use of undeclared identifier 'skySeen' |
| `test_night_floor_wiring.py` | Source-wiring or reference assertion failed |
| `test_particle_light_capture.py` | Test fixture compilation: 'auto' deduced as 'std::size_t' (aka 'unsigned long long') in declaration of 'rowBytes' and deduced as 'unsigned int' in declaration of 'rows' |
| `test_particle_texture_stats.py` | Test fixture compilation: misleading indentation; statement is not part of the previous 'for' [-Werror,-Wmisleading-indentation] |
| `test_rain_mask.py` | Source-wiring or reference assertion failed |
| `test_replay_gpu_probation.py` | Test fixture compilation: type alias redefinition with different types ('unsigned long' vs 'unsigned int') |
| `test_rotation_wake.py` | Runner invocation lacks required --output argument |
| `test_shader_optimization_cpu.py` | Source-wiring or reference assertion failed |
| `test_shadow_removal_smoothing.py` | Expected source anchor is absent |
| `test_shared_mesh_preparation.py` | Test fixture compilation: no matching constructor for initialization of 'std::vector<NorthlightGI::WorldVertex>' |
| `test_shared_static_preparation.py` | Test fixture compilation: too many arguments to function call, expected single argument 'n', have 2 arguments |
| `test_solar_volume.py` | Source-wiring or reference assertion failed |
| `test_static_shadow_grouping.py` | Test fixture compilation: too many arguments to function call, expected single argument 'n', have 2 arguments |
| `test_sun_rim.py` | Source-wiring or reference assertion failed |
| `test_terrain_capture_phases.py` | Test fixture compilation: expected ')' |
| `test_terrain_capture_validation.py` | Test fixture compilation: typedef redefinition with different types ('unsigned int' vs 'unsigned long') |
| `test_terrain_indices.py` | Test fixture compilation: typedef redefinition with different types ('unsigned int' vs 'unsigned long') |
| `test_terrain_source_accounting.py` | Test fixture compilation: typedef redefinition with different types ('unsigned int' vs 'unsigned long') |
| `test_terrain_source_indices.py` | Test fixture compilation: typedef redefinition with different types ('unsigned int' vs 'unsigned long') |
| `test_ui_water_boundary.py` | Test fixture compilation: use of undeclared identifier 'fixedUiGeometry' |
| `test_water_mrt_wiring.py` | Source-wiring or reference assertion failed |
| `test_weather_effects_wiring.py` | Source-wiring or reference assertion failed |
| `test_weather_wiring.py` | Expected source anchor is absent |
| `test_wmo_lamp_runtime.py` | Test fixture compilation: no viable overloaded '=' |
| `test_world_lighting_capture.py` | Native D3D9 depth-texture creation failed, HRESULT 8876086c |
| `test_world_shader_manifest.py` | Existing combined HDR shader uses 687 slots; test limit is 512 |

The local release directory also contains `packages.json`, `SHA256SUMS.txt` and `validation.json`. Build, test and packaged-entry-point logs are retained under `.local/final-installer-20261031/`. The ZIP is a local build artifact; it has not been uploaded to GitHub Releases.
