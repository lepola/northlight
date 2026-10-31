# Telredor point-shadow visibility (2026-10-31)

The tester reported 85 FPS with effects off and 25 FPS with effects on. A static-cache stand establishes one avoidable cost: renderPointShadow built cubes for sources that LocalLighting subsequently rejected with an empty conservative sphere scissor.

Use that existing scissor before allocating or rendering the cube. Keep static resources, invalidate the dynamic schedule and reset any face cycle. A visible source then receives all six fresh faces; a skipped capture requests new input instead of publishing the old cube. Unknown views and near-plane overlap retain the original fail-open behavior. Selection, light limits, sample counts, shaders and settings are unchanged.

## Measurement inputs

The stand uses the combined #98 head 18329275 plus the separate #112 actor ownership implementation, compiled against the current High AO kernels, and the latest #97 material-persistence and #114 herb/Swamp Gas follow-ups (0d1b15a8, 6a54ec92 and 794fd3b1). The existing integration supplies #76, #86, #92, #96, #109, #110, #111, #113, #116, #117 and #118. These are reconciled implementations, not an ancestry claim for independently ported branches. The frozen measurement assembly is local commit d2152fb7 on codex/telredor-performance; the complete assembly branch is not published. Its source/cache identities remain in the private validation report, while #98 supplies the reproducible benchmark source.

The canonical checkout's 73 uncommitted HDR-related paths were inventoried and excluded. Existing HDR presentation is inactive. The game was closed for accepted GPU runs; the separate scene capture requested during the task paused the stand. No game process was launched by this task.

Telredor center: Expansion01 (278.582, 6001.27, 144.73), from [AzerothCore's game_tele data](https://github.com/azerothcore/azerothcore-wotlk/blob/master/data/sql/base/db_world/game_tele.sql). Geometry and lights come from the user's existing cache, without rebuilding it: 620,016 local triangles, 1,319,119 shadow-plan triangles, 3,907 batches. The report authenticates a conservative input superset of 746 cache files and 566 source/shader/test inputs. Proprietary assets are not committed.

The actual production WorldRenderer runs eight authored headings, 256-square cube faces and a 3840-by-2160 receiver scissor. Test both the default point limit 4 and the existing user override 128. This is a static component workload: it has no original native draw stream, animated actors or complete game frame.

## Paired result

NVIDIA GeForce RTX 4090, x86 O2. Each heading has eight ABBA blocks, 64 updates per block and two priming updates. The control disables only the new visibility proof during cube submission; it retains the same selected lights, geometry, shader payloads and quality. Completion is measured through an EVENT fence and includes CPU submission and GPU execution. It is not GPU busy time.

Percentages below are geometric means of per-heading candidate/control ratios; each heading uses the median of its four blocks per variant.

| Backend | Limit 4 completion reduction | Limit 128 completion reduction | Limit 128 CPU submission reduction |
| --- | ---: | ---: | ---: |
| DXVK 3.1.1 | 5.9% | 14.8% | 15.2% |
| DXVK 2.7.1 | 7.0% | 13.5% | 14.0% |
| Native D3D9 | 6.0% | 11.6% | 11.6% |

At limit 128, the sum of cube updates across the eight headings changes from 132 to 113. At limit 4 it changes from 32 to 30; six headings have no removable cubes, so the overall median changes very little.

All 858 visible cube faces compare byte-for-byte per backend, 2,574 comparisons total, with nonempty geometry retained. The ready-versus-visible assertion detects a removed skip; face membership/equality detects missing visible cubes. The existing independent conservative-scissor oracle additionally covers 19,766,061 shaded-candidate pixels and invalid/near-plane cases.

The measurements identify and remove wasted work. They do not reproduce the entire reported 28.24 ms effects-on difference, prove an in-game FPS increase, or establish 85 FPS in the tester's scene.

## Validation and reproduction

The independent #92 and combined x86 DLLs build. Focused point scheduling, clipping and scissor CPU tests pass, including optimized and ASan/UBSan modes. The all-PR NameMask/command-stream/stencil compatibility checks pass after reconciling the actor ownership integration; its actor-AO oracle passes 40 independent ray-truth cases / 160 production AO-and-blur renders on DXVK 3. Material persistence passes restart, corruption and bounded-queue CPU checks. These are focused gates, not a complete all-PR release certification.

The additional all-PR particle capture script validates its x86 paired outputs but reaches a pre-existing x64 compilation error in particle_texture_stats.h (a shared auto declaration deduces size_t and unsigned). That unrelated platform gate is not reported as passing.

The committed production transition oracle tests camera chunk membership, dynamic capture reuse, offscreen no-update, retained resources, skipped-capture deferral, full-face re-entry under a partial-face setting and invalid-view fail-open behavior. Run tests/test_point_terrain_coverage_gpu.py on the independent PR. The new real-cache benchmark runner and generalized scene fixture are supplied by #98:

    python tests/test_point_shadow_visibility_gpu.py --output <fresh-private-folder> --backend <backend-dll> --benchmark

The raw accepted report is .local/telredor-performance/all-pr-accepted/validation.json, SHA256 ce77e7aeafa4cd576af0867a87d9cb407a86fb136e377182fd9033f585d6fab1. It includes source/cache/backend hashes and every per-heading median; timings.tsv retains all blocks. The tested all-PR DLL source identity is 60a0fdfc183a5f436213628c647c7740d5c6a26b3807ea1a18a775036c0f3828.

