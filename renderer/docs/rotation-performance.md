# Northlight camera rotation and material performance

22 October 2026. Investigated baseline: `0be2a10`. The game was not launched during this work.

## Findings and changes

The fresh run confirms a temporary slowdown while the visible working set changes. A sampled frame window averages **35.18 FPS**, followed by **78.22 FPS** after the view settles, with the same enabled material modes. This is an observation within one run, not an old/new build comparison. Input events are not timestamped in the renderer log, so the exact start of a keyboard or mouse action cannot be reconstructed.

Three workers investigated five topics each. The strongest reproduced causes are expensive eviction of tiny material bounds and repeated eviction/recreation of unchanged terrain snapshots. The new build addresses both without reducing geometry, visibility, quality, ranges, MSAA, cache limits or source-lifetime checks. It also skips material-normal decoding where the existing distance fade is already zero.

| Implemented change | Measured proof | Limits of the result |
| --- | --- | --- |
| Constant-time material-bounds LRU instead of scanning all 4,096 records on each insertion | Frozen old/new rotation fixture: **3.607653 -> 0.035755 ms**, 99.01% less cache-maintenance CPU time | Measures cache maintenance, not whole-frame time. Warm-hit overhead is approximately 1.26 microseconds/frame. |
| Lossless adaptive 16/32-bit CPU terrain indices | Same 32 MiB cache, 5,600 warm view returns: **3,816 -> 0 misses**, **38,236,320 -> 0 bytes reread**, exact expanded geometry | A controlled pressured-cache fixture, not a guarantee that every game view fits. GPU indices remain uint32. |
| Existing material rejection before unused normal work; exact zero-fade normal early return | **576 exact old/new GPU comparisons**; stable far material fixture approximately **16% faster** | Near material paths pay approximately 2–4% branch cost. Some 4K timing cases are noisy or regress. This is a small postprocess saving, not the main rotation fix. |

The installed DLL and quality/settings files were preserved. The separate release package is for the user's next test. Actual game FPS after these changes remains unmeasured until that run.

## Inputs and what this run actually enables

The immutable input directory is `northlight-diagnostics/rotation-live-20261022-2238/` under the game directory. The complete fresh renderer log is 1,093,130 bytes, ends at device destruction, and has SHA-256 `1948f37eb25e8c716e91b55c48d04cd39df52588f811a98ba63e6f9f88f588ea`. Its `.prev.log` is the genuine preceding 20:42 run, SHA-256 `b6d586124b923178c0b69f0c1598bc61ab9c59530e1b6cbb8d287609c98a0b20`.

The installed DLL is `0be2a10`, SHA-256 `94c5625e5cc89c255ac39377e28c5ff10f703905c798925c83ff8653f1dba35e`. The quality INI hash is `dd040d75bfdb92fb6467e9facd77222e0864261c2b372d8a334e444fb597fb50`; renderer INI hash is `d809c1e46c691c952a72963fc4264c06c9cb79cbea463ba9d19365d811b06f85`.

Sampled material modes are all Off through frame 2413, M2-only **mode 3** at 2540–3048, and all four domains **mode 3** at 3175–3810. Mode 3 is Displacement + normal maps. There is **no Normals-only mode 1 interval**. Cavity and self-shadow strengths remain zero, so this log cannot establish their visual operation or enabled runtime cost. A real moon is active (`lightActive=2` is a bitmask); fog comparisons against the previous no-source run are not controlled comparisons.

Previous fixes are working: frame 3810 issues 673 selected capture setters and skips 9,841 identical ones, approximately 94%. It uses 150 GPU-resident captures, zero UP draws, zero source reads and no normal-map generation. This is not continuous regeneration of every normal texture; adding an MPQ cache would not remove current-pose evaluation or ordinary rendering.

## Where time is spent

These CPU stages are exclusive. Pose children have their own attribution and must not be added again to an inclusive parent from an older build. GPU timestamps describe a separate timeline and must not be added to CPU submission time.

| Sample | Changing view, frame 3556 | Slow sample, frame 3683 | Settled view, frame 3810 |
| --- | ---: | ---: | ---: |
| Total instrumented material CPU | 8.5256 ms | 9.7860 ms | 2.9994 ms |
| Bounds/pose, excluding separately timed pose children | 3.9958 ms | 4.1555 ms | 0.4750 ms |
| Source snapshot/cache service | 1.0831 ms | 1.2737 ms | 0.2512 ms |
| Pose envelope | 0.2839 ms | 0.3540 ms | 0.2911 ms |
| Generic current-pose fallback | 0.3802 ms | 1.2413 ms | 0.5666 ms |
| Deferred capture flush setup | 0.2394 ms | 0.2987 ms | 0.1755 ms |
| Normal-map generation | 0.0846 ms | 0.1107 ms | 0 |
| Complete GPU interval | 19.829 ms | 26.352 ms | 8.819 ms |
| GPU GameScene | 15.631 ms | 20.104 ms | 4.956 ms |
| GPU Effects | 3.071 ms | 4.550 ms | 2.930 ms |

At frame 3683, native terrain alone occupies a **13.7605 ms GPU interval**. Several unrelated GPU stages inflate together. Timestamps alone do not separate shader execution from command starvation, resource dependencies, driver scheduling or changing coverage. MaterialDetail is ordinarily about 0.114–0.153 ms; its isolated 0.634 ms spike is insufficient evidence of a shader-arithmetic bottleneck.

Some frame windows spend approximately 12.6 ms/frame waiting for command-stream queries. The wrapper's query API work itself is only about 0.02–0.05 ms. These waits overlap outstanding required work and other measured spans. Their durations cannot be summed or eliminated by reporting an unfinished query as complete. Query/fence semantics are unchanged.

## All 15 assigned investigations

### Material CPU, sources and capture

| Topic | Evidence and decision |
| --- | --- |
| 1. Shared source working-set churn and bounds insertion | The 4,096-entry bounds cache performs a full `min_element` victim scan per cold insertion. The log reports 46,990 lifetime misses and about 4 ms in the affected stage during churn. **Replaced with the existing intrusive LRU**, preserving exact oldest-use order and weak-owner validation. Source read budgets and cache capacities are unchanged. |
| 2. GPU weak-owner expiry, promotion and UP fallback | Frame 3556 expires 180 GPU entries despite using only 2.95 MiB of its 32 MiB cap, with no capacity eviction. The upstream CPU source was evicted. Increasing the GPU cache or retaining dead geometry would miss the cause or violate memory/lifetime rules. Weak ownership and promotion remain intact. |
| 3. Pose envelopes and generic fallback | Warm M2-only samples still evaluate 8,183 current-pose vertices in 211 fallback calls; the cost reaches 1.7451 ms. This remains a useful next target. No stale animated bounds or unproved approximate cull was introduced. |
| 4. Normal/height/mipmap generation | Warm samples have zero generation. Churn samples spend about 0.08–0.11 ms here, far below the bounds spike. The address-space guard still reduces normal-cache capacity from 96 to 32 MiB when required; it must not be removed in a 32-bit process. |
| 5. Packets, state, barriers and constant copying | Exact setter reuse already skips about 94% of selected calls in a warm sample. Mutation barriers, per-draw pose constants, integer/Boolean banks and flush-local ownership remain required. No unsafe narrowing of state restoration was made. |

### Camera, terrain and world streaming

| Topic | Evidence and decision |
| --- | --- |
| 6. Camera orbit, pivot and region changes | In 12 samples the eye travels about 97.62 yards in X while the reconstructed pivot spans only 1.01 yards. Third-person rotation really moves the camera around the avatar. Travel lead is zero. Existing eye-centered coverage and GI grid updates are legitimate; anchoring them to the avatar would change the result. |
| 7. Terrain snapshots, arena/index rebuilds and rollovers | The 32 MiB CPU cache evicts 169–316 snapshots per sampled rotation frame. A sample introduces 319 owners and 6.82 MB of vertex/index uploads. **CPU indices now use uint16 when remapped values fit and promote losslessly to uint32 otherwise.** This retains more immutable views without raising the cap. |
| 8. Static page/owner churn | Seventeen staged meshes upload 410.75 MiB while reusing 296.32 MiB. Existing content/page reuse works; changed eye regions require some new pages. Cropping differently, dropping pages or retaining extra generations would change output or memory usage. |
| 9. Generation handoff/retirement waits | Twenty waits with 1.735 ms peak occur in the asynchronous region builder, not the render thread. This counter is background admission latency, not a measured foreground stall. Generation limits and safe retirement remain unchanged. |
| 10. GI retargets, cancellation and actor refresh | Of 305 request lines, 302 have reason 2, probe-grid movement. The eye actually crosses the grid; stationary tail activations reuse all 5,600 probes. Lighting cadence, actor updates and world coverage were preserved. |

### GPU and postcomposition

| Topic | Evidence and decision |
| --- | --- |
| 11. WorldComposite guided upsampling | Approximately 0.536 ms before/after M2 activation and 0.537 ms in the worst sample. Tap coordinates/depth reads are already shared while distinct weights remain necessary. No additional proven change; bytecode is identical. |
| 12. MaterialComposite rejection and out-of-range normals | **Implemented exact early rejection** for sky/water/disabled effects and the zero-fade helper return. The latter still returns `normalize(normal)`, preserving the original behavior for nonunit FP16 normals. Strength zero retains its original raw-normal result. No new distance cutoff or lower quality was introduced. |
| 13. Enabled-source fog and lighting duplication | The moon contributes real scattering in this run. Cloud extinction and WorldFog are separate work. Previous zero-source skips still apply when valid; active-source output, steps, strengths and resolutions remain unchanged. |
| 14. Temporal invalidation/copies during yaw | History is not invalidated merely by view rotation. Lighting/fog reproject and swap history resources; there is no yaw-triggered full history copy. TemporalLight and FogTemporal bytecode are unchanged. |
| 15. First-visible shader/pipeline/texture warmup | Native material capture shader variants are created on first encounter. Rotation can reveal a new variant, but current logs do not separately time creation or backend pipeline stalls. This remains an explicitly unproved secondary hypothesis. No speculative draw skipping, async compilation setting or visual warmup substitute was adopted. |

## Correctness and measured tradeoffs

The bounds test compares 60,000 old/new draw operations and 19,725 misses, exact numerical bounds, timestamps, retained sets and victims. It covers weak-owner expiry, address reuse, map rehashing, reset/pressure and six allocation-failure cases. Limits remain 4,096 records / 8 MiB. The record grows from 64 to 80 accounted bytes on x86: at capacity this adds 64 KiB of charged metadata. Resident-hit maintenance increases from 0.007630 to 0.008890 ms in the fixture; rotation insertion drops from 3.607653 to 0.035755 ms.

The terrain fixture retains 2,800 rather than 2,323 snapshots in the same 32 MiB / 4,096-entry limits. Accounted bytes are 31,874,368 versus 33,549,012. Both expand to digest `e6a64536c883ee30`. CPU test elapsed time is illustrative (about 158 -> 87 ms including setup and checks), not GPU time. Logical indices, triangles, normals, chunk membership, source write/lifetime verification, immutable snapshot ownership, native draws and all GPU fence rules remain exact. Custom captures above 65,536 unique vertices retain a genuine wide path. The GPU arena still writes 32-bit indices, including a tested rebase to 65,540.

All six production consumers of the shared normal helper were rebuilt: WorldLighting, LocalDirect, LocalDirect16, MaterialComposite, MaterialAudit and LocalLighting. Their integrated hashes match the independently tested binaries. Other world/local shader entries remain identical; AO/post binaries are unchanged. Shader Model 3 limits pass.

GPU validation uses production FP16 captures/normals/HDR textures, FP16/FP32 and 8-bit outputs, odd/even grids, MSAA 0/2/4/8, fade boundaries, nonunit stored normals, sky/water, absent or depth-mismatched capture, disabled effects and active cavity/self-shadow test inputs. All 576 old/new cases have zero changed bytes and no nonfinite output. An additional 100 material capture checks pass, including 40 genuine previous-build comparisons, animation, mutation, state restoration and alpha coverage.

Stable far MaterialComposite fixtures improve from 0.018688 to 0.015744 ms at 1080p and 0.072192 to 0.060288 ms at 4K. Active near paths add about 0.0005–0.0006 ms at 1080p and 0.0019 ms at 4K. Some later 4K cavity/disabled/fade cases have large variability and old/new ratios up to 1.38; all raw results are retained. There is no uniform GPU speedup claim.

Seven existing terrain/live-arena regressions pass with assertions enabled. New adaptive-index tests pass O2 and O0, including boundary values, concurrent immutable readers, tracked/unknown revisions and 3,600 randomized fake-D3D arena frames. This environment lacks native ASan/UBSan; O0 debug results are not described as sanitizer results. Release build and DLL/export validation are recorded alongside the packaged DLL.

## Reproduce and evaluate the next run

From the repository root:

```powershell
python tests/test_displacement_bounds_rotation.py
python tests/test_displacement_state_reuse.py
python tests/test_material_geometry.py
python tests/test_terrain_indices.py
python tests/test_terrain_rotation_cache.py --baseline-ref 0be2a10
python tests/validate_material_post_rejection_gpu.py --baseline-ref 0be2a10 --backend D:/Games/335_engb_mod/renderer-backends/dxvk/dxvk_d3d9.dll
python tests/validate_material_deferred.py --baseline-ref 0be2a10 --alpha-test --texture-guard --backend D:/Games/335_engb_mod/renderer-backends/dxvk/dxvk_d3d9.dll
python scripts/build_renderer.py
```

Use the same location, mode, settings and orbit path for the next comparison. Check `boundsPose`, source bytes/misses, terrain owner changes/evictions/uploads/rollovers and the new sampled `TERRAIN index storage` line. Verify that capture counts and quality settings are comparable before interpreting lower times. Keep current-pose fallback, native terrain/GameScene and cold pipeline creation as separate remaining targets. Do not infer success solely from one FPS number.

Evidence in the frozen diagnostics directory includes the input hashes, parsed stages, all 15 assignments, each worker's detailed report, bounds differential results, terrain differential results, raw GPU timing cases (including regressions), functional image comparisons, integrated shader identities and release/DLL validation. These files distinguish reproduced causes from unproved hypotheses and retain the actual before/after baselines.
