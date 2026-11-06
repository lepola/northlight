# Camera rotation: MPQ reproduction and terrain retention

The latest delivered renderer still repeats terrain capture work when its memory guard enters a geometry-demand episode. The fix preserves the normal 32 MiB terrain cache during that episode while retaining the one-shot trim, all other demand-time savings and every actual low-address-space safeguard. An independent MPQ-backed reproduction now exercises the recorded camera orbit and both stable and rewritten vertex buffers.

## Reproduction inputs

The frozen baseline is `fd6b3a4bb7853f36abfd299079b34e37d12c7fad`; its renderer matches the previously delivered `79dd6da` binary. The private fixture is freshly extracted from the active client archive chain, including patch precedence:

- 36 Northrend ADTs and 6,689 source assets; zero missing or unsupported assets.
- 4,608 terrain draws, each containing up to two adjacent nonempty MCNKs with exact original XYZ, triangle winding and holes.
- Independent FGS3 static geometry/material/placement data produced by the existing world builder.
- Fourteen recorded camera samples at ticks 128064984 through 128068265.
- A 1,277-unit candidate range, matching the farclip recorded in the client's configuration at extraction.
- Original ADTs, camera samples, generated outputs and member/archive provenance in a hashed manifest.

The scene and terrain corpus are private generated artifacts, not committed game assets. Preparation opens the archives read-only and writes to a separate fixture directory. Installed settings, DLLs and the installed world cache are not used as output destinations.

The original D3D draw stream was not recorded. The CPU replay therefore uses fake D3D buffers and an explicit 110-degree horizontal candidate cone, without client occlusion/LOD or GPU/driver waits. This reproduces the demonstrated cache-pressure mechanism with actual MPQ geometry and captured motion; it is not an exact replay of game frame 4318 or a live FPS measurement.

## Production change

The memory guard already distinguishes a geometry-demand episode from its own low-address-space thresholds. Stream and replay-copy caps followed this distinction, but terrain incorrectly followed any pressure episode. In the captured run a 755 MiB aggregate sample, 237 MiB largest block and 768 MiB geometry demand selected `caps=full` while still shrinking terrain from 32 to 16 MiB. The 237 MiB block then prevented recovery past the existing 256 MiB threshold.

The world setter now receives separate episode-pressure and terrain-halving states. Terrain follows `Guard::capsHalved()`. Updates occur on both episode edges and cap changes inside an existing episode. Real low-address-space samples still halve terrain to 16 MiB; normal recovery restores 32 MiB. Snapshot/GPU/pool savings, near-reserve handling, auto-normal pressure, one-shot trimming, cooldown, recovery hysteresis and synchronous allocation rejection remain unchanged. The normal terrain cap is not increased.

Diagnostics now distinguish `caps`, `worldCaps` and `terrainCaptureMiB`, so a demand-only episode can no longer misleadingly print only `caps=full`.
The startup marker `MEMORY policy terrain=demand-full/address-space-half normalMiB=32 lowMiB=16` identifies this policy in the next client log without relying on the shared upstream release banner.

## MPQ comparison

The x86 Windows harness compiles the frozen and current production capture headers and world-cap setter. A separate automatic test executes the actual renderer wiring and memory guard. Three alternating baseline/current rounds use Zig 0.15.2. Each result below is the median aggregate capture time over 28 warm recorded-camera frames and 19,746 capture calls, not a single-frame time.

| Mode | Baseline | Candidate | Capture-path reduction |
| --- | ---: | ---: | ---: |
| Unchanged buffer revisions | 94.9388 ms | 4.2086 ms | 95.57% |
| VB rewritten every frame, identical XYZ | 111.3069 ms | 22.4270 ms | 79.85% |

Both modes eliminate 4,412 warm misses and 4,412 evictions in the demand-only case. Stable revisions eliminate 44,201,616 repeat-read bytes. Rewritten buffers still require exact vertex revalidation: candidate reads are 137,191,848 bytes versus 150,739,560 before, with all 19,746 calls proving exact reuse. These required reads are not described as free or skipped.

Across the 84 warm samples per mode, the largest measured capture time is 9.3878 to 0.2014 ms with stable revisions and 9.3674 to 0.9028 ms with rewritten buffers. These CPU-path timings exclude the per-triangle oracle and do not include real driver lock latency.

Every emitted triangle position is compared bit for bit with its original MPQ vertex reference, including hole-dependent index sequences. All per-frame geometry fingerprints match. The real low-address-space scenario retains identical hits, misses, exact reuses, read bytes, evictions and retained bytes in both implementations, including its 16 MiB limit. No wall-clock performance threshold is used to weaken the deterministic work assertions.

The evidence records all 266 source inputs before/after (unchanged), compiler version/commands, both executable hashes, baseline revision and the fixture manifest checksum. Validation output directories must be fresh, preventing a failed rerun from retaining a stale success marker.

Raw delivery evidence: `northlight-diagnostics/rotation-implementation-20261023/mpq-delivery/validation.json` and its six round logs. Earlier independent runs under `mpq-capture-rewrites-3rounds/` and `mpq-final/` show the same deterministic work reduction and approximately 79% less rewritten-buffer capture time. Reproduction commands are in [the tests README](../../tests/README.md#mpq-backed-camera-rotation-reproduction).

## Focused regression validation

Seventeen native Ubuntu/Clang suites and six Windows/Zig suites pass, with no execution failures. The one native platform skip (`test_terrain_indices.py`) is executed successfully in the Windows group. This is 23 distinct automatic scripts, not a rerun of the earlier complete integration gate.

Coverage includes production pressure wiring, in-episode cap transitions, one-shot trim/recovery/admission, exact terrain snapshots and indices, mutable-buffer revisions, shared holders, retention/accounting, capture bounds, terrain shadow coverage, near reserve and mesh pool trimming. Memory/policy cases execute ASan/UBSan; the actual sampler/guard handoff also executes TSan. Both intentional policy regressions are rejected at their expected assertions. Before/after snapshots match.

The existing hidden-device D3D9 test also passes single-sample and MSAA 2x/4x/8x capture/replay binding and depth restoration, followed by 120 demand-pressure frames and recovery. This uses the Windows system x86 D3D9 runtime and does not start the game. Source hashes before/after match. Eight tool controls reject reused output directories, incomplete inputs, unreadable MPQ overrides and modified fixture files without overwriting prior evidence. The MPQ replay checks every generated output against its manifest before use.

Evidence directories are `memory/final-native/`, `final-windows/`, `final-gpu/` and `tool-controls/` beneath the same diagnostics root. Python syntax, repository layout and diff-whitespace checks pass.

## Quality gate: rejected camera-anchor experiment

Moving the geometry region to the stationary player with the old crop can lose GI transport geometry. An expanded 60-unit envelope was therefore prototyped and tested before runtime integration. Although every old triangle remained present and a sparse screen looked correct, the complete transport oracle rejected it.

The full test uses 78,400 probes at 64 rays and three bounces, a broader MPQ reference with 6,769,092 triangles, and 14,424,607 recorded multibounce/direct-shadow queries. Twenty-four probes have worse SH error, and seven changed path queries are not independently reference-exact. The experiment is not shipped. Its helper was removed from production; camera centers, crop bounds, GI/shadow distances, ray counts and the 64/96/160-unit region predicates remain unchanged.

The rejected reference and manual oracle are retained for reproducibility; their JSON explicitly records `quality_gate_pass=false`. Mechanical execution success is not a rendering-quality pass. Raw evidence is under `orbit-envelope-transport-full/` in the same diagnostics directory.

An exact SIMD decoder prototype was also rejected. Its public-load differential agrees with the frozen decoder on 996 valid/corrupt/missing-file cases at seven allocation-denial positions, including NaNs, unsigned-index boundaries, truncation and transactional failures. Although decoder-only time decreased 3.66%, the genuine complete preparation/plan/pages/rays/5,600-probe bake comparison showed no repeatable total gain: orbit preparation plus bake changed 325.012 to 324.079 ms, while warm median changed 317.880 to 321.645 ms. Production GI source was restored; the prototype and raw evidence remain external under `fgs-exact-validation/`.

Large-WMO preparation remains a separate structural opportunity. The observed 26.85 MiB model exceeds the existing 8 MiB decoded-source pool; whole cropped acceleration data plus full material banks can also exceed the 16 MiB pool while an older generation remains alive. Reusing bounded chunks correctly needs exact subset/material identities, alpha remapping and live-allocation accounting. This unresolved worker cost is not presented as fixed by the foreground terrain correction.

## Other module and live-validation scope

The user is installing a newer `wow_optimize`. Work against its previously installed version was stopped, remains isolated and unshipped, and is not part of this Northlight delivery. No optimizer DLL was installed or is supplied. Earlier Lua-stack findings describe the old run and must be reassessed on the user's newer version before changes to that module are considered.

The game has not been launched by this work. The measured reductions apply to offline capture work, not measured game FPS; actual rotation smoothness and appearance require the user's next run. The original historical contact-normal capture coverage gap remains unrelated and is not silently marked as passed.

## Built artifact

The tested renderer source commit is `ae6a879ecadbf1c87948398c1d5d57e6b1f55704`. `scripts/build_renderer.py` succeeds with the existing offline shader-manifest gates. The final DLL is 5,616,128 bytes, SHA-256 `e47705e79b395659a2a5650c68ac3ea732f4c49f2f8bc79adf905191f3a57255`; its matching 45,895,680-byte PDB has SHA-256 `6a56c74da49510f563681876cacb01b5268d8ca0cfb86d652c5d898408aa2501`.

The first build succeeds but its evidence wrapper rejects raw source equality because Windows generators convert five generated headers from LF to CRLF. Each generated file is proved identical to the committed source after newline normalization. A second `build-verified/` run succeeds with unchanged before/after inputs and the same DLL/PDB hashes; generated files are then restored to the exact committed bytes. The first build's two existing material-header warnings are recorded in `build/build.log`; no unrelated material changes are included. The publication follow-up only updates this report and normalizes newline encoding in the rejected offline reference/wrapper.

Installed `d3d9.dll`, all six snapshotted client configuration files, and the installed world cache are not output destinations. The installed DLL and all six configuration hashes remain unchanged. The final preservation snapshot observes only the user's concurrent replacement of `wow_optimize.dll` and `wow_optimize.sym`; those files are neither restored nor included in this delivery. New-optimizer runtime behavior has not yet been evaluated.

The separate `northlight-builds/northlight-rotation-pressure-ae6a879-20261023/` directory contains the replacement `d3d9.dll`, matching symbols, this English report and a hashed manifest. Installation remains a manual user action.
