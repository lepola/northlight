# Camera rotation: exact wait attribution and demand-created material shaders

Date: 23 October 2026. Scope: Northlight only; the game was not launched. Installed binaries, private settings, MPQs and world caches were not written. This iteration uses the frozen 17:28–17:29 run, not the older 17:20 run.

## Findings from the correct DLL

The installed DLL SHA-256 is `7d08bee1cc6645b7e19f623368f00388b269e110dae19069501a5a532a8a22d5`, matching the previous `9be300a` delivery. Its generic startup version is insufficient to distinguish integration builds. The new build adds a source fingerprint at startup.

Two captured freezes, at game frames 2708 (119.459 ms) and 4016 (70.191 ms), resolve against the exact local PDB to:

```
StreamQuery::GetData
  -> runTask<StreamQuery::GetData lambda>
    -> Queue::waitReplayed
      -> Event::waitPumped
        -> MsgWaitForMultipleObjectsEx
```

`pdb-publics.json` records the four RVAs and preceding public symbols. The DLL was read as data, never executed. This locates the producer's synchronous query wait; it does not identify every operation blocking replay or prove that the entire wait is native GPU execution. In the last 600-frame query window, 5,813.293 ms of query trips divides into 3,765.517 ms before replay, 2,030.549 ms in replay, and 17.227 ms after replay. These are aggregate query intervals, not an FPS prediction. Existing per-frame query reporting samples every 127th frame; reporting slow trips separately will expose the partition for individual freezes.

The sampled material switch is off through frame 2667 and first on at 2794; cold enable work occurs at 2709. GI, shadows and fog remain on in the sampled fields. The evidence supports **materials off**, not **all effects off**. Off-window material capture, generation and pose work are zero. Native scene rendering and query waits remain.

The largest remaining measured costs are:

| Cost | Evidence | Interpretation |
| --- | --- | --- |
| Query/replay synchronization | Exact PDB call chain above; 64.8% of the final aggregate query trip time precedes execution | Waiting on replay progress; large before-replay time must not be attributed entirely to the driver's GetData call |
| Native scene rendering | GameScene GPU 23.152/24.629/20.742 ms at frames 2921/3937/4064; hooked native DIP CPU 23.952/23.610/19.104 ms | CPU call duration can include driver backpressure; these enclosing intervals must not be added together |
| Terrain capture cache churn | Frame 3937: 10.715 ms terrain CPU, 536 misses, 496 byte evictions, 5.08 MB read; frame 4064: 8.510 ms, 364 misses, 371 byte evictions, 3.67 MB read | Actual 32 MiB capacity pressure, despite unchanged VB/IB inputs; foreground rebuild work is still important |
| Material enable | Frame 2709 registers 21 vertex sources in 29.7564 ms, before additional model/static shader work | Eager creation of unused geometry variants is removable for Normals/POM/far geometry |
| GI worker | Median build 516 ms, maximum 1062 ms across 15 published builds | Background duration, not automatically a synchronous frame stall; spatial workload differs from the older run |
| Post effects | AO median 0.1355 ms, AO blur 0.2955; fog march 0.902 ms, clouds 0.627 ms | Different sparse sample populations; these effects do not explain the largest native-scene intervals |

No memory trim/reclaim event occurs in this run, so the previous reclamation optimization's triggered subphases cannot be measured here. Prepared GI geometry reuse is visible, but comparing different game runs is not a controlled FPS A/B.

## Accepted material change

Keep original shader bytecode in its existing retained COM object. Validate CPU patch admission at registration, preserving unsupported-program rejection before pose budgets are spent. Create CPU/GPU displacement device variants only after an actual geometry draw passes its existing exact distance proof. Late capture variants use the same first-consumption path. No new bytecode cache, geometry fade, reduced quality, altered MSAA, or expanded memory cap is introduced.

For 21 authentic MPQ vertex programs, device shader creations at registration fall **105 -> 21 (-80%)**. In the full native-D3D9 three-pair registration benchmark, median Normals registration is **4.3888 -> 1.4052 ms (-67.98%)**, and displacement registration **3.6110 -> 0.6431 ms (-82.19%)**. These are isolated, driver-cache-sensitive registration measurements, not game FPS or total first-near pipeline savings. Normals/POM/far draws do not allocate the unused displacement variants.

The first eligible near displacement draw still creates required variants synchronously. Repeated first-near observations contain small differences in both directions; initial native runs also contain 100–259 ms full-submission outliers in both builds. The oracle includes independent later geometry/depth replays, so summing one submit marker with the frame aggregate would double-count or misattribute work. A latency-neutral cold first-near transition is **not established**. `MATERIAL_VARIANT_COLD` now records source reads, created CPU/GPU variants, failures, bytes and total CPU creation time for the actual production frame. This limitation is retained explicitly.

## Query correction and diagnostics

The larger change removes an unnecessary queue dependency. For valid EVENT/FLUSH waits, a task at the next replay command boundary proves that the captured END generation has already replayed in the same reset epoch and that the device/query are valid. It then reads that exact terminal result or uses the original bounded 2 ms driver-poll loop. Commands queued after that EVENT need not retire before it can be checked. If END has not replayed, the epoch/cooperative state changed, or the request shape is invalid, the original ordered task remains the fallback. The currently executing command is never interrupted and rendering commands retain their order.

A failed boundary proof adds one boundary trip before the original ordered fallback. Its live frequency and cost are not established by the synthetic stand. `queryExpressPosts`, `queryExpressHits`, `queryExpressFallbacks` and `queryExpressTotalMs` distinguish attempted/used/fallback work without attributing it to texture readbacks. Explicit fallback counts avoid treating an in-flight attempt as a proof miss.

The original FLUSH polling contract is preserved. Microsoft's [GetData reference](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dquery9-getdata) distinguishes pending results from completion and warns that repeated non-FLUSH polls alone need not make progress. The boundary callback retains real FLUSH polls, device-loss handling and the existing bounded polling budget.

A temporary COM hold only across the pumped path fixes an existing lifetime gap: a sent-message handler can release the last external query reference while GetData is waiting. The hold keeps both query and device alive through the final member accesses; millions of local cache probes do not pay this reference-count cost.

Also reuse a terminal EVENT result already published for the exact issue generation and current device-reset epoch instead of making a redundant driver GetData call in the pending-query loop. Pending, reissued, reset, occlusion and invalid/dead queries keep their original paths. Producer-private state is not read by replay. Query cadence, wait budgets and rendering command ordering are preserved. Neither change establishes removal of every long synchronous wait.

Emit the existing `CSTREAM QUERYFRAME` phase partition for trips at least 20 ms, in addition to the normal 127-frame cadence. Use existing accumulated timers; add no per-draw clock reads, GPU queries or synchronization. Boundary-query counts/wait time are separated from texture readback accounting. The startup `BUILD identity` fingerprints source/shaders/build inputs, allowing future logs to identify the exact renderer source without relying on a generic version string.

## Accepted terrain reconstruction change

The same-chunk fast path is local to one triangle-sequence classification. A centroid strictly more than the existing 0.03 tolerance inside the previous proven cell reuses its double-precision bounds; all boundary cases run the original double floor and grid checks. Finite/area checks, every vertex check, holes/winding, XYZ bounds, disjoint chunks and sorting retain their original semantics. The persistent cache policy and 32/16 MiB limits are unchanged.

Three alternating MPQ-backed pairs over eight recorded angular sectors show **204.7007 -> 167.1375 ms (-18.35%)** median warm capture time for the synthetic two-layout 32 MiB churn workload (11,300 calls across the measured sequence). Each pair improves, by approximately 18.0%, 17.0% and 20.5%. Both versions perform the same **5,998 rebuilds and 6,000 evictions**; the optimization reduces reconstruction cost rather than curing capacity churn. Reads, retained bytes, caps and every non-timing counter match exactly. These are aggregate CPU component times, not frame times or game FPS.

The stable / VB-rewrite / VB+IB-rewrite controls are 4.8885 -> 4.5653 / 25.2828 -> 24.4130 / 36.8346 -> 34.8848 ms. These small differences are reported for transparency, not presented as a proven improvement to already-cached draws. Native O2/O0/ASan/UBSan each passes 4,343 differential coverage comparisons against the frozen original; a wrong-cell mutation fails the specific independent oracle. Every actual-MPQ captured XYZ, chunk and AABB matches; the oracle runs outside capture timing. All eight source/index/stale-token mutation outcomes pass. Recorded camera directions and authentic MPQ geometry/holes are used, but the alternating native buffer layouts and visibility cone are explicitly synthetic.

The foreground investigation adds five further ranked directions in `gi/terrain-churn-audit.md`: exact classification cost (accepted), bounded weak raw-index sharing, exact integer/capacity representation, key-alias tracing, and charged admission under an oversized working set. The latter four remain hypotheses; no cap expansion, lossy compression or unaccounted strong retention is introduced.

## Fifteen ranked hypotheses investigated by three agents

| Direction | Five topics and disposition |
| --- | --- |
| GI | 1. Exact SAH single-axis binning: rejected after full-pipeline regression. 2. BLAS/preparation reuse: visible in this run, retain. 3. Raw source bank: post-startup hits zero and a WMO source exceeds the bank; no unproved cap change. 4. Terrain mesh/page planning: large worker subphases, retain exact coverage checks. 5. Page memoization: zero hits, needs workload reproduction before redesign. |
| Materials | 1. Eager unused displacement variants: accepted demand creation. 2. Snapshot revision/capacity churn: measured, unresolved. 3. Replay probation entry churn: roughly 220 UP draws/evictions in sampled frames, needs identity-sequence reproduction. 4. Skin pose/envelope budgets: measured bounded work, preserve exact current-pose enclosure. 5. Normal-map cache churn: exists, but tiny generation GPU samples do not explain the largest stalls. |
| AO/post/stream | 1. Native scene rendering/backpressure: leading scene cost. 2. EVENT synchronization: exact-generation boundary polling, terminal reuse and rare slow-trip reporting. 3. Producer gaps: keep effects-not-applied queue-idle rows separate. 4. Fog: leading sampled post effect; no new shader change without equivalence/timing proof. 5. AO blur: measured, much smaller than native scene cost; preserve quality. |

Detailed five-topic audits and source/log references are in `gi/audit.md`, `materials/audit.md`, and `ao-stream/audit.md` under this iteration's diagnostic directory.

## Rejected GI experiment and reusable test environment

The single-axis SAH candidate preserved exact trees, ordering and ray outputs but made the complete seven-region MPQ preparation+bake workload **3.70% slower** (median 6161.807 -> 6389.732 ms over three alternating pairs); BVH work was 4.14% slower. Production GI code was restored to exact HEAD bytes. The candidate source and focused O2/O0/ASan/UBSan controls are preserved outside the repository for audit.

The reusable `tests/validate_gi_sah_pipeline.py` stand remains: seven recorded camera positions, authentic private MPQ extraction, recursive tree/order hashes, scene/page/ray outputs and 5,600-probe comparisons, with source/input/compiler fingerprints. No private game assets are published.

Material validation uses authentic MPQ shader programs and synthetic geometry/palettes. Native D3D9 passes 40 fixture/MSAA pairs (terrain, wall and SM2/SM3 rigid/skinned/weighted/four-influence models, MSAA 0/2/4/8); DXVK passes 16 representative pairs. Every output byte is compared. Additional controls cover far-to-near admission, reset/toggle, retained-source reference counts, read/CreateVertexShader failures and re-registration recovery. A repeated native first-near run adds 16 pairs (32 process invocations) across two selected fixtures. Including the earlier 16-pair quick native gate, 88 paired cases and 1,200 paired output-file comparisons pass. Compilation failures before the corrected harness entry point and a sandbox compiler error are preserved; they are not counted as successful gates.

## Final integration validation

The full query algorithm gate passes O2, query-caller attribution, ASan/UBSan, filtering disabled, direct replay disabled, TSan thread cases and compilation against the real x86 Windows SDK (413.74 s). All 1,122 watched source/script/test/shader inputs remain unchanged during that gate. The subsequent diagnostic-only counter changes and canonical build-identity ordering are recorded separately; the final focused ordered-control/O2/ASan/UBSan/TSan gate passes in 136.70 s with unchanged inputs. Initial helper/setup failures remain preserved and are not counted as passed checks.

The terrain candidate passes the three-pair component gate described above. The final affected regression run passes **15 Windows scripts and 11 native scripts, with zero failures, timeouts or skips**; watched sources remain unchanged during each run. Including the full command-stream, EVENT-boundary and terrain-classification scripts, **29 distinct automatic scripts** pass across this iteration's focused gates. Hidden system-D3D9 capture/POM state restoration passes single-sample and MSAA 2/4/8 plus 120 pressure/recovery frames. This is focused validation; the complete 299-script suite was not rerun. The previously unavailable historical contact-normal captures are not replaced or claimed as tested.

The final terrain stand metadata is strengthened after the production gates: its independent frozen oracle is bundled for source-only/shallow checkouts; helper, baseline archive/full commit/extracted source, generated-input and output-artifact fingerprints are explicit. Final O2/O0/ASan and wrong-cell controls pass without Git history. A fresh one-pair MPQ confirmation preserves every output/cap/counter and takes 171.247 -> 149.3763 ms (-12.77%) in the target churn case; fitting-hit controls vary between runs and no broad cached-hit benefit is claimed. Generated-header tampering is rejected before compilation. The final run checks 531 source inputs, seven helpers before import, ten generated inputs and four compiler outputs. Capture timing and production algorithms are unchanged by these tooling edits.

The first renderer build correctly rejects volatile date/time macros in the new identity marker under the existing reproducible-build compiler policy. The marker is corrected to source hash only; the compiler check remains enabled. Four focused source-identity, developer-build, diagnostic and log-format checks are rerun for this startup-only correction. The failed build is retained under `build-final`; only a successful fresh `build-final-02` may supply the delivery.

The final x86 renderer build succeeds with unchanged source fingerprints during compilation. Its startup identity is both printed by the build and embedded in the resulting PE32 DLL. Generated headers match committed content; only their platform line endings are restored after proving equivalence. The four startup/build/log-format follow-up checks pass. All ten installed/private binary/configuration inputs and the previous immutable delivery remain byte-identical. The two existing compiler warnings remain; no compiler check or test assertion is disabled.

The EVENT dependency stand uses the actual queue/GetData/replay implementation and two controllably blocked draw calls. The ordered control disables only the new boundary branch and retains the same lifetime fix. Fourteen scenarios require opposite dependency outcomes: a proven prior END returns while the later draw remains blocked, whereas an unissued/unseen END, reset epoch, malformed request or cooperative-state loss retains ordered progress. Cached FALSE/error, real driver error, pending budget, sent-message reissue/nested calls and final external release are checked. All 56 final case executions pass: the current path bypasses the blocked later draw in nine cases; five correctly retain the ordered dependency. This proves removal of that queue dependency, not a millisecond/FPS estimate for the game. A separate production-loop test proves zero duplicate driver calls for 8,192 terminal EVENTs and rejects the original loop with the specific negative-control exit code 77.

## Remaining limits

The offline stands reproduce specific production paths using MPQ inputs; they do not replay the game's complete recorded native command sequence or reproduce an exact driver scheduling state. No whole-game FPS uplift or removal of all camera-turn freezes is claimed. Foreground terrain capacity churn and large native scene intervals remain targets; the new slow-trip records will separate their impact from synchronous EVENT polling in the next user-run log.


## Reproduction entry points

Configure the existing `northlight_paths.py` environment/local paths for Clang, Zig 0.15.2, the client and StormLib. CPU tests never start the game. GPU stands use hidden standalone native-D3D9/DXVK devices. Output directories must be new and empty; the private corpus is extracted separately and never committed.

```text
python tests/test_event_query_express.py
python tests/test_terrain_classification.py
python tests/validate_material_lazy_variants.py --backend <backend-dll> --baseline-ref 9be300a --output <new-output>
python tests/validate_terrain_classification.py --fixture <complete-terrain-fixture> --camera-log <frozen-log> --output <new-output>
python tests/validate_gi_sah_pipeline.py --fixture <complete-MPQ-fixture> --camera-log <frozen-log> --output <new-output>
```

The terrain automatic oracle is bundled and needs no Git history. Full pipeline A/B stands require the explicit baseline commit in the local repository. The diagnostic run is `northlight-diagnostics/rotation-next-20261023-1728`; raw evidence includes `analysis.json`, `pdb-publics.json`, `materials/*/validation.json`, `gi/*/validation.json`, `ao-stream/handoff-summary.json`, `regression-*/validation.json`, `final-gpu/validation.json`, `identity-final/test-output`, and `build-final-02/validation.json`. Each experiment's failed setup attempts remain separate. The latest live log still ends at 17:29:51; no later run is silently substituted.

## Separate delivery

Renderer source commit: `5e22c5a3d32e8a7b8538ed22d9b8bb7307bc238c`.

Startup `BUILD identity source=`: `7e8cc4775d12ffdc69f1434523cbfc884931e7c31670b8c14f56f2497e492f3e`.

`d3d9.dll`: 5,623,808 bytes; SHA-256 `6ef4c0fd83db78df0e2fc3b12d4c3d8cd4852375f901805a78b373258c328bc4`.

`frd9.pdb`: 45,920,256 bytes; SHA-256 `743990f7bfbd894c6069b9ecabfce4fe2b343fde69266453922af19599295a69`.

Build toolchain: Zig 0.15.2, SHA-256 `d408dd38eed3e5204af841bcebf70502a4dbbb8399a3a3262be55059370bc018`.

Package: `northlight-builds/northlight-rotation-queries-5e22c5a-20261023`. It contains DLL, matching PDB, this report, README and hashed manifest. It is prepared separately for manual replacement with the game closed; nothing is automatically installed. Publication history is recorded in the manifest.
