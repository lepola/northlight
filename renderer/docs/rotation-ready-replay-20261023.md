# Northlight camera-rotation performance investigation

The latest user run at 19:33 on 23 October 2026 used the correct previous delivery: DLL SHA-256 `6ef4c0fd83db78df0e2fc3b12d4c3d8cd4852375f901805a78b373258c328bc4`, renderer source `5e22c5a3d32e8a7b8538ed22d9b8bb7307bc238c`, startup source fingerprint `7e8cc4775d12ffdc69f1434523cbfc884931e7c31670b8c14f56f2497e492f3e`. The 19:30 run used an older DLL. This report documents a new, separately built follow-up; its in-game FPS has not been measured.

Two changes are accepted: serve synchronous EVENT queries immediately after their captured END, and preserve material GPU residency across fully verified byte-identical buffer rewrites. A terrain-index memo was rejected because its small memory saving did not reduce rebuilds and its unique-pattern control became slower.

No game was launched. Installed Northlight, wow_optimize, personal settings, MPQs and world-cache files remain unchanged. Shader code, geometry, holes, winding, MSAA, effect distances and admission/memory limits are preserved. The new comparison work is restricted to eligible material readers; sharing their source pool does not opt world-only, Off, far or range-only readers in.

## What the fresh log establishes

The matching previous-delivery PDB maps the 19:33:40.454 freeze stack to `Event::waitPumped -> Queue::waitReplayed -> runTask<StreamQuery::GetData> -> StreamQuery::GetData`. PDB GUID and age match that exact DLL. At game-frame 1691, a 36.5371 ms query trip spends 31.4941 ms before replay, 5.0136 ms in replay and 0.0294 ms afterward. At game-frame 1971, 24.2324 of 28.0151 ms is before replay. These are queue-wait observations, not driver-only GPU timings.

The shipped express mechanism is active: cumulative counts at renderer frame 1800 are 2,808 posts, 1,562 hits and 1,246 fallbacks. The last two 600-frame windows have 591 and 597 fallbacks, approximately one per frame. Previously, an attempt made before its END reached replay failed immediately and appended the normal synchronous task behind later draws.

There are other causes. Material-Off frame 624 takes 74.919 ms and contains 69.691 ms of scene queue idle. Its 1.562 ms querySpan is a render-side pending span: it excludes producer queue waiting and cannot bound the full GetData trip. The nearby freeze sample spends most samples in game/Lua code. Separately, sampled Off frame 635 has 15.705 ms GPU scene work and one replay indexed draw taking 10.6948 ms. The scheduling fix cannot eliminate necessary native draws or producer work while no commands are available.

During movement, material snapshots and deferred recording each cost roughly 1 to 1.5 ms in sampled frames, with about the existing 2 MiB read allowance used by each source reader. Frame 2032 reaches 4,096 replay entries and has 223 UP draws / 2,783,960 bytes. Warm frames have no reads or uploads. Sampled normalBuild and mip work are zero: continual normal-map regeneration is not demonstrated.

Configuration is matched by frame brackets, not log order. Materials are sampled Off at frame 1651 and On at 1778. Frame 1690 lies in that transition; its explicit cold event states enabled, but this does not establish the complete frame state. Retrospective slow rows must not inherit a later printed configuration. Selected worst frames are not a representative FPS distribution. CPU, queue, GPU and enclosing/child spans overlap and are never added as independent frame costs.

## Fifteen reviewed optimization directions

| ID | Area and direction | Fresh evidence | Current decision |
|---|---|---|---|
| G1 | Terrain 32 MiB working-set churn | Frame 1016: 561 captures, 145 rebuilds/byte-pressure evictions, 2.523 ms. Later 561-call stable samples: 0.234 to 0.250 ms with no misses or reads. Count pressure stays 0. | Confirmed capacity cost. Reduce exact retained duplication rather than increasing caps or dropping geometry. |
| G2 | Bounded weak raw-index sharing | Native frame 1905 retains 2.753 MiB raw indices across 1232 blocks. The existing one-block memo already avoids 8.276 MiB. | **Tested and REJECTED.** Three actual-MPQ pairs showed no rebuild reduction; details below. Production header restored exactly. |
| G3 | Decoded-index width and unused capacity | Retained decoded indices cost roughly 11 to 12 MiB; source offsets another 2.1 to 2.3 MiB. All sampled storage already uses 16 bits. | Next useful census: exact unique counts, unused capacity and equal decoded patterns. No 8-bit eligibility or quantized geometry is assumed. |
| G4 | Admission and draw-key aliases | Cache fills around 4200 to 4400 entries despite an 8192-slot limit. Original native base/start/layout fields are not logged. | Preserve full resource/layout/range/revision keys. Add a miss-field census before changing key normalization or admission policy. |
| G5 | GI source, preparation and page reuse | Only one 625 ms cold geometry publication: shadow plan 193.716 ms, local 187.887 ms, terrain load 137.439 ms, pages 95.209 ms. | No repeated worker build is shown during later rotation. Keep existing charged 16 + 8 + 8 MiB budgets; avoid another rejected SAH experiment. |
| M1 | Material snapshot/read churn | Frames 1905/2032: snapshot 1.5392/1.4890 ms and deferred record 1.1282/1.1723 ms; source readers consume about their existing 2 MiB budgets. Warm 2159/2286 have zero reads and approximately 0.12 ms snapshot cost. | Preserve current reads and exact provenance. The candidate targets downstream GPU residency; it does not claim read elimination or CPU-payload deduplication. |
| M2 | GPU replay probation restarts and UP fallback | Frame 2032: 345 residents plus 3751 probation entries fill 4096 slots;223 UP draws upload 2,783,960 bytes. Frame 1905 has 216 UP draws/3,011,420 bytes. Warm frames have no uploads/evictions. | Adopted: a separate replay identity after complete byte equality across rewritten snapshots. Keep current source identity, 32 MiB GPU allowance and all promotion/upload limits. Final exact-output and timing gates pass; see measured tradeoffs below. |
| M3 | Pose/skin envelope fallback | Enabled samples spend 0.4753 to 0.5013 ms on 8156 to 8186 fallback vertices and 0.2859 to 0.4384 ms on envelopes. Frame 2032 retains 47 time deferrals. | Persistent warm-path opportunity. No stale poses, weaker bounds or reduced 8192-vertex/operation/time limits are accepted. No pose patch in this iteration. |
| M4 | Normal-map generation versus material application | Sampled ON frames show no normalBuild, mip, subdivision, height-read or geometry-upload work. Normal lookup costs 0.0056 to 0.0542 ms; frame 2032 final material application costs 0.1075 ms GPU. | Normal maps are not being rebuilt continually in these samples. Native terrain GPU 4.8328 ms includes ordinary game rendering and is not all material overhead. Visual intensity needs an image oracle. |
| M5 | Lazy displacement shader variants | Enable event 1690 registers 18 vertex sources in 5.0110 ms; no MATERIAL_VARIANT_COLD events or displacement geometry builds/draws occur. | Already-shipped lazy behavior avoids unused work. This run does not validate live first-use cost of required near displacement variants. |
| A1 | EVENT express readiness at the captured END | At 1800: 2808 posts, 1562 hits, 1246 fallbacks. The next-to-last and last 600-frame windows contain 591 and 597 misses. | Adopted scheduling waits for the captured generation/epoch boundary, then polls before unrelated later draws. Focused native/sanitizer/negative-control gate PASS; full stream suite PASS. |
| A2 | Ordered query waits and freeze attribution | Query trips reach 36.5371 ms, largely before replay. Matching DLL/PDB identifies the ordered GetData wait in the 76 ms FreezeCatcher sample. | Proven Northlight scheduling path. Preserve reset/loss/death/supersession handling, exact payloads and the existing 2 ms driver polling budget. |
| A3 | Native draw work and producer gaps | Material-OFF sampled frame 635: GPU scene 15.705 ms; replay indexed draws 14.4977 ms/1925 calls, including one 10.6948 ms call. Separately, frame 624 has 69.691 ms scene queue idle. | Keep these causes separate. Query scheduling cannot remove required draws or game/Lua work while no commands are available. |
| A4 | AO blur and submission | Six active samples: AO median 0.1235 ms/max 0.139; blur median 0.2215 ms/max 0.273. Blur setup/submission about 0.0005/0.0004 ms CPU. | Low priority for 30 to 75 ms stalls. Preserve AO quality, radius, resolution and cadence. |
| A5 | Fog/post work and sampled profiler overhead | Matched fog suites median 1.807 ms; march 0.918 ms, clouds 0.689 ms and blur 0.169 ms medians. Sampled frame 635 has 19450 timer reads/~0.472 ms estimated overhead. | Fog is a real ordinary post-effect opportunity, but not every freeze. Do not repeat rejected same-cell/center-sample reuse or treat sampled profiling as an every-frame tax. |


## Accepted EVENT scheduling change

The express mailbox has an optional readiness predicate. A query publishes its already-committed END and stays pending only while that exact captured generation has not replayed. The consumer selects/acquires the next command before checking the mailbox, so a nested future END cannot overtake the old query task. It polls at the first eligible boundary before unrelated later draws.

Exact generation, reset epoch, device health, native query and lifetime checks remain mandatory. Missing/drained, reset, loss, death, superseded and invalid states retain the ordered fallback. The existing 2 ms driver polling budget and terminal-result reuse stay intact. Texture express tasks keep their default behavior.

The deterministic stand runs real production queue/GetData/replay headers with a deliberately blocked later draw. Both the previous ordered path and immediate-mailbox control retain the dependency when END has not replayed; the new path returns before that later draw, including a nested future-END case. This proves the dependency removed without claiming an arbitrary timing percentage.

Validation passes 105 explicit query/idle executions across ordered control, immediate-mailbox control, O2, ASan/UBSan and TSan, plus five reused texture-control groups. Both pending-query timed sleep and ordinary empty-queue sleep wake correctly. The full command-stream suite passes O2, query-caller attribution, ASan/UBSan, filtering disabled, direct path disabled, TSan threads and real 32-bit Windows SDK compilation. Source hashes stay unchanged during each frozen gate.

## Accepted material replay-content identity

The current source/resource/layout/declaration/draw/revision contract remains in `sourceIdentity`. A separate `replayIdentity` is inherited only after all current reads and stability/admission checks, equality of the full source contract except its five write revisions, and exact equality of counts, topology, indexed/dynamic flags, indices and all four stream strides/bytes. A cheap routing collision never proves content. GPU residency uses that proven identity; bounds and source/resource consumers keep current provenance.

The shortlist is weak and fixed at 2,048 slots. Its table and worst-case expired object/control allocations are conservatively charged inside the existing shared 112 MiB allowance. Material GPU residency stays 32 MiB / 4,096 entries, with the existing 16-creation and 4 MiB upload admission. Optional failure uses the original exact output/fallback.

Eligibility is per reader and defaults Off. Only admitted material capture and displacement source readers opt in. World-only/Off/far peers neither allocate the table nor compare or update its candidates. A peer-built current shared hit cannot invent a content proof. No extra stale source ownership is reused, and mutable-copy/deformation consumers were audited.

Cold activation is tested separately: a full world-prewarmed pool must not prevent the first eligible material use from reserving the fixed metadata allowance. A temporary allocation succeeds before any bounded LRU eviction; the existing cap and maintenance accounting apply. Failed allocation evicts nothing. A post-eviction fit check prevents optional metadata plus a new entry exceeding the cap. The CPU oracle verifies prewarm -> eligible activation -> next identical revision, exact output/read accounting, Off peers, collisions, changed contents, stale revisions, expired owners, reset and tight caps.

### Final real-device component measurements

Each backend runs 96 paired cases: weighted M2, rigid M2, terrain and WMO; MSAA 0/2/4/8; identical and changing bytes; three alternating AB/BA pairs. The full matrices pass 192 pairs and 576 complete color/normal/occlusion output-file comparisons. Another 16 pairs / 48 files verify the hardened public runner and strict cross-backend executable reuse, for **208 final pairs / 624 complete output comparisons**, with exact read/admission counters and render-state restoration. The table below summarizes the admitted MSAA path; zero-MSAA cases are also checked for exact output.

| Backend | Workload | Median full capture + submission CPU | Change | p95 of run means |
|---|---|---:|---:|---:|
| System D3D9 | identical | 0.169925 -> 0.120943 ms | -28.83% | 0.241125 -> 0.191688 ms |
| System D3D9 | changing | 0.165762 -> 0.167225 ms | +0.88% | 0.228975 -> 0.225112 ms |
| DXVK | identical | 0.318263 -> 0.118913 ms | -62.64% | 0.446300 -> 0.189012 ms |
| DXVK | changing | 0.323562 -> 0.324862 ms | +0.40% | 0.391525 -> 0.389100 ms |

GPU times cover the complete fixture batch, including ordinary draws; they are not isolated material-shader cost:

| Backend | Workload | Median GPU batch time | p95 of GPU run means |
|---|---|---:|---:|
| System D3D9 | identical | 0.155648 -> 0.131776 ms | 0.215936 -> 0.181376 ms |
| System D3D9 | changing | 0.161280 -> 0.157888 ms | 0.206975 -> 0.190336 ms |
| DXVK | identical | 0.065472 -> 0.064640 ms | 0.132224 -> 0.150144 ms |
| DXVK | changing | 0.064640 -> 0.065856 ms | 0.123264 -> 0.124416 ms |

For admitted identical rewrites, observed UP submissions fall from 384 to zero over each 12-render-call sequence. Changing-byte controls remain all-UP with zero verified matches and unchanged current reads. The changing-data cost is a measured tradeoff; it is not described as neutral.

These GPU stands use **32 draws of one indexed 16x16 grid per frame**, authored palettes, MPQ-authentic shader binaries and fixed texture inputs. They do not reproduce 223 distinct live meshes or the original client LOD/draw stream. A separate CPU/fake-device oracle covers 220 distinct draws per frame across 48 rewritten generations under the existing budgets; its optimized identical case reduces UP bytes by about 75%, preserving accepted draws and reads. Its counts and timings are not real-driver scene FPS.

CPU values are eight-call submission averages after four warm calls. Reported p95/max describe run means, not individual-frame or cold latency. GPU timings vary by cell; preliminary native/DXVK outliers and their bounded repeats are retained. No GPU-tail neutrality or cold-tail improvement is claimed. The full pre-repair matrix and quick/repeat history contains another 246 pairs / 738 file comparisons; those are historical evidence, not extra distinct final cases.

The full world-prewarm metadata-eviction branch is exercised by the production-reader/fake-device oracle. The real-device matrix starts from a fresh source pool; it is not a captured historical GPU world-prewarm test. The public runner was hardened after the full native matrix with baseline/compiler reuse checks and complete source inventory; its full DXVK and native reuse confirmation use those stricter guards. Renderer C++ and fixtures are unchanged between these matrices.

Runtime benefit depends on actual byte-identical rewrites. New sampled `MATERIAL_REPLAY_CONTENT` capture/geometry tests, hits and payload-byte-volume counters allow the next user-run log to establish the real match rate and comparison work. Snapshot/read elimination and CPU-payload deduplication are not claimed.

## Rejected terrain experiment

A bounded 64-slot weak raw-index memo passed exact geometry/coverage, collision, holes, mutation, lifetime, cap and O2/O0/ASan/UBSan tests. Its unsafe-equality negative control failed the independent live-geometry oracle as required.

Three real-MPQ pairs still perform 6,776 rebuilds from 11,310 warm captures and read 113,309,304 bytes. Capture times are 118.6567 -> 117.5914 ms, 118.1490 -> 117.8562 ms and 118.6553 -> 123.2434 ms. The median changes only -0.67%, reverses in the last pair and does not justify adoption. The 2,600-pattern no-sharing control regresses 5.386345 -> 5.631280 ms (+4.55%).

Production terrain code is restored to HEAD byte for byte. The rejected patch and test remain archived outside the repository. The reusable manual stand retains immutable baseline selection and an explicit storage-comparison mode; default strict comparison and all geometry/coverage/mutation/cap/provenance assertions remain. Its geometry is real MPQ data with explicitly reconstructed visibility/layouts, not the unavailable original command stream.

## Final integration checks and offline reuse

Thirty selected automatic scripts pass in the final Windows/native regression gate; with the two focused/full command-stream scripts this is **32 distinct automatic scripts**. There are 300 discoverable automatic scripts; the entire suite was not rerun. This does not replace the still-unavailable four historical contact-normal captures. Existing checks were not disabled or weakened.

Final hidden system-D3D9 validation also passes single-sample/MSAA 2x/4x/8x and 120 pressure/recovery frames. The renderer build succeeds with unchanged generated-source equivalence, frozen source/compiler hashes and an embedded/logged stable source identity. Sandbox launch/tooling retries and the repaired test executable-name collision are retained in the diagnostic history.

Reusable sources:

- [EVENT dependency and wakeup oracle](https://github.com/aphelion105/northlight/blob/caaae4ef4c006098bcabbee474260e070e01babd/tests/test_event_query_express.py), [full command-stream suite](https://github.com/aphelion105/northlight/blob/caaae4ef4c006098bcabbee474260e070e01babd/tests/test_command_stream.py).
- [History-free material CPU/cap/provenance oracle](https://github.com/aphelion105/northlight/blob/caaae4ef4c006098bcabbee474260e070e01babd/tests/test_material_replay_content.py), [real-device material runner](https://github.com/aphelion105/northlight/blob/caaae4ef4c006098bcabbee474260e070e01babd/tests/validate_material_replay_content.py), [MSAA/pressure suite](https://github.com/aphelion105/northlight/blob/caaae4ef4c006098bcabbee474260e070e01babd/tests/validate_static_material_msaa.py).
- [MPQ fixture preparation](https://github.com/aphelion105/northlight/blob/caaae4ef4c006098bcabbee474260e070e01babd/scripts/prepare_rotation_fixture.py), [terrain camera/coverage comparison](https://github.com/aphelion105/northlight/blob/caaae4ef4c006098bcabbee474260e070e01babd/tests/validate_terrain_classification.py). Fixtures are private read-only inputs; none are published.

Run the material device stand against a full immutable baseline, prepared fixture root and a new evidence directory, with the game closed:

```text
python tests/validate_material_replay_content.py --repo <repository> --baseline-ref 26280b277b27109e29bfb442ccf363a5d16e6a3a --fixture-root <prepared-material-fixtures> --backend C:/Windows/SysWOW64/d3d9.dll --output <new-evidence-directory> --pairs 3
```

The fixture root contains the existing `model-materials-dxvk` and `material-msaa-release` prepared folders. Use the configured DXVK DLL for the second backend. The runner checks full baseline identity, copied inputs, source/helper/compiler outputs and exact pixel/state/read/residency controls; it rejects stale evidence directories and incompatible executable reuse.

## Separate delivery

- Renderer source commit: `caaae4ef4c006098bcabbee474260e070e01babd`.
- Startup `BUILD identity source`: `9c74b5534ecb3082558d16f751f08c8dfc9bf5be4465ff90b8f122bdf4de2442`.
- DLL: 5,624,832 bytes, SHA-256 `32133fd7a6af1049833defb2e232291bb6d93fca1575744d22b5c67695bb330a`.
- Matching PDB: SHA-256 `b33ecab9f7f87ea0c7153a86ccb29525fd25d7368056af8293a38c41e7f6dccf`.
- Package: [northlight-rotation-ready-caaae4e-20261023](D:/Games/335_engb_mod/northlight-builds/northlight-rotation-ready-caaae4e-20261023).

The package contains DLL, matching PDB, this English report and a hash manifest. Nothing is automatically installed. The next user-run log must confirm the new identity, EVENT hit/fallback behavior, material content matches and remaining producer/native GPU costs before attributing any in-game uplift.

## Inspectable evidence

- [Frozen log analysis and frame brackets](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/analysis.json), [matching previous-DLL PDB](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/pdb-publics.json).
- [Query proof and full stream results](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/ao-stream/readiness-verified/results.json), [case rows](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/ao-stream/readiness-verified/case-results.json).
- [Final native material matrix](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/materials/final-native/validation.json), [final DXVK matrix](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/materials/final-dxvk/validation.json), [material safety audit](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/materials/audit.md).
- [Rejected terrain measurements](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/gi/terrain-candidate-result.json), [exact restoration](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/gi/rejected-candidate-disposition.json).
- [Final Windows gate](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/regression-windows/validation.json), [final native gate](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/regression-native/validation.json), [final MSAA gate](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/final-gpu/validation.json), [build](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/build-final/validation.json).
- [All 10 installed/private inputs and prior delivery unchanged](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-1933/untouched-validation.json).
