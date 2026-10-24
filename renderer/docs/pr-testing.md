# Independent and combined testing

The feature PRs each contain a complete buildable checkout. Shared prerequisites are included in dependent branches so testing does not require merging unrelated feature branches. Review and merge the focused PRs first; this branch is an assembled integration snapshot for combined testing, including the previously published foliage/emitter work and the catalog fix that keeps static world emitters out of patch-Z to prevent duplicate lighting. It is not the review unit for an individual feature.

| Scope | PR | Included prerequisites |
| --- | --- | --- |
| Build/test tools and explicit compiler provenance | [85](https://github.com/lepola/northlight/pull/85) | main |
| Optional DXVK 3 engine-name workaround | [86](https://github.com/lepola/northlight/pull/86) | 85 |
| World capture and UI boundaries | [87](https://github.com/lepola/northlight/pull/87) | 85 |
| SSAO/HDAO/HBAO+/GTAO techniques | [88](https://github.com/lepola/northlight/pull/88) | 85 |
| General render performance | [89](https://github.com/lepola/northlight/pull/89) | 85 |
| Celestial clouds, moon and halo composition | [90](https://github.com/lepola/northlight/pull/90) | 85 |
| Settings menu and transactional live controls | [91](https://github.com/lepola/northlight/pull/91) | 85 |
| Local-source shadows and selection budgets | [92](https://github.com/lepola/northlight/pull/92) | 85 |
| Experimental HDR presentation | [94](https://github.com/lepola/northlight/pull/94) | 85, 87 |
| Automatic normals, POM, self-shadowing and cavity | [95](https://github.com/lepola/northlight/pull/95) | 85, 87 |
| UI atlas diagnostic components | [96](https://github.com/lepola/northlight/pull/96) | 85 |
| Signed height, displacement and animated models | [97](https://github.com/lepola/northlight/pull/97) | 85, 87, 95 |

Existing related PRs cover [foliage shadows](https://github.com/lepola/northlight/pull/76), [world emitter catalogs](https://github.com/lepola/northlight/pull/82), [equipment/particle lights and installer migration](https://github.com/lepola/northlight/pull/83), and [native detailed-shadow installation policy](https://github.com/lepola/northlight/pull/84). This snapshot includes the local foliage/catalog/equipment implementations and the catalog's per-source shadow metadata. The native installation policy remains a separate review.

The combined tree connects menu AO preview, material/height controls, HDR status and local-source lighting. Water beauty, refraction, glints and SSR from the closed PR #93 have been withdrawn; another contributor owns that feature. Native liquid masks remain for AO, fog and celestial receiver correctness. Independent sheltered precipitation remains, with identical shader bytecode. The current material source retains the installed command-stream routing behavior and the separately verified log-driven optimizations. No author checkout was edited. New atlas diagnostics remain opt-in components without a game hotkey. HDR remains off by default.

Configure northlight.local.ini and follow tests/README.md. Run scripts/check_layout.py, scripts/affected_tests.py --why and the selected plain test scripts. Full runs belong to shared-tooling, release or main validation. Windows native Zig adaptations used for local host checks do not provide ASan/UBSan/TSan certification. Missing host requirements and manual in-game checks must remain explicit.

Generated HLSL headers, bytecode, assembly and manifests must agree. Generator commands use the pinned compiler by default; --compatible-compiler explicitly allows the separately verified Microsoft SDK binary and records its actual hash. Client-extracted fixtures and test-generated shaders, captures, DLLs and logs belong in ignored output. Committed GPU baselines are Northlight's own shader code.

The integrated performance fixture uses the combined shader interface with interval skipping, regional support clipping, scalar scattering accumulation and flattened taps reverted. Rebuild it with tests/regenerate_integrated_perf_baseline.py after shader changes; tests/test_five_path_perf.py verifies its source hash and paired output. The isolated performance PR retains its upstream baseline. GPU timestamps and API-call counts are fixture measurements; representative in-game visuals, LOD transitions, overlays and FPS remain manual review work.

The review follow-up runs the shared suite with the pinned Linux StormLib and LLVM libc++, including native optimized and sanitizer fixtures, then checks affected contracts on the independent branches. Combined validation includes the full native sweep and focused reruns of repaired cases. Optional material/water schemas are explicit bridge fixtures in the standalone menu tests; its ordinary schema still verifies that unsupported controls stay hidden.

CPU tests that execute Windows programs declare `windows-exe`. GPU tests declare `windows-gpu`, whose prerequisite probe creates a hidden HAL device with the system D3D9 runtime before feature backends run. Fresh Windows checks pass on native D3D9 and DXVK where the individual oracle supports them. The updated HDR matrix covers both DXVK versions, multisampling, Reset metadata and target rebinds. Fixture timings are not an in-game benchmark; representative combinations, silhouette/LOD transitions and frame cost remain draft requirements. Linux and macOS HDR have not been tested.
