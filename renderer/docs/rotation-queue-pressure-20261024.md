# Northlight: rotation stalls, terrain copies and cache capacity

This iteration analyzes the user run ending at approximately 02:25 on 24 October 2026. It introduces a tracked terrain cold-miss copy reduction, narrower material replay constant uploads, and bounded diagnostics for command starvation and memory-pressure transitions. Runtime cache limits and image quality are unchanged. A larger cache is evaluated only in copied test headers, never installed settings.

## Analyzed build and runtime evidence

The user run loaded source `1fda0271917e83b41e7c1bcf0214893a3fb5cfea`, DLL SHA-256 `c1054fe6298cb2a99cbb6a0697e34a0947a8a196276c4228f3d27c34af50c30e`, startup source identity `262d3f27588e572ad899b8b843140717352ff08b82ae326b21f8e34cd3124074`. The repository baseline is report-only follow-up `91b9f27a4d3931e9586febf646b37c90efd6146b`. Upstream PR #89/main `fb568b4` and all earlier accepted fixes are included. DXVK 3.1.1, 3840 x 2160 and MSAA 2x were active. Conflicting device-name/vendor metadata is not used to infer the physical GPU model.

The frozen renderer log is 2,820,164 bytes, SHA-256 `e9522ca4ee24033158358057b821f82bfa34104f00e7b10f376445ffd6b095fe`. It is preserved under `northlight-diagnostics/rotation-current-20261024-022624`. Sampled material modes remain `3/3/3/3`; a materials-OFF interval is not established by these samples. AO/settings change during the run. Different settings, motion, travel and residency prevent a controlled cross-build FPS comparison.

| Location | Fresh evidence | Meaning |
|---|---|---|
| Command-supply gaps | Frame 3355: 403.767 ms total, 386.520 ms scene, 381.407 ms scene queue idle. Frame 4822: 194.223 ms total, 182.093 ms idle. | These stalls cannot be explained by the sampled ordinary AO/material shader costs. Consumer idle alone does not identify the producer's blocking call. |
| Tail starvation | Frame 3683: 41.954 ms tail, 4.026 ms query span, 44.633 ms frame queue idle. | The tail is not interchangeable with a query wait. Keep these overlapping timing domains separate. |
| Memory-pressure transition | Frame 2340: guard work 38.990 ms and frame finish 39.096 ms; trim 12.260 ms, including world 12.228 ms. Frame 7694: guard work 37.491 ms, trim 14.374 ms. | Roughly 26.7/23.1 ms lies outside the existing trim timer. The log did not identify which cap reduction owns it; new phase timers address that gap. |
| Terrain churn | Frame 1778: 11.135 ms terrain capture, 685 misses, 739 evictions, approximately 7.17 MB reads. Frame 7620: 8.526 ms, 574 misses, 551 evictions. | Byte-capacity pressure and cold-miss work remain concrete Northlight costs. Median terrain capture is much smaller, 0.3145 ms across 58 samples. |
| Busy rendering | Sampled replay median 5.3725 ms, p95 18.5692 ms; GPU GameScene median 6.054 ms, p95 24.170 ms. | Original native draws and injected work share these scopes. CPU replay and GPU time overlap. |
| Ordinary effects | GPU Effects median 3.147 ms; AO 0.232 ms, blur 0.167 ms, FogMarch 0.5145 ms, clouds 0.3905 ms. | Fog is a larger ordinary post target than AO here. Conditional populations differ; stage medians cannot be summed into a synthetic frame. |

The final complete readiness window has 15,937 posts, 15,937 hits and zero fallbacks. The client reports `gxFixLag` configured/runtime 1; it is read-only evidence and is not changed. `QUERYFRAME` rows include periodic and slow-selected observations, so their 23.832 ms median is not an ordinary-frame latency estimate.

Sampled bump/cavity work is present. Frame 6731 has 4,405 normal-changed grid samples and 956 cavity samples. At cavity zero, frames 6858/6985 report zero cavity while changed normals remain 4,393/4,407; restoring cavity yields 957 samples at 7112. These are grid counters, not whole-image visual validation. Self-shadow remains zero with mode 3 and `sharedPOM=0`; this run does not exercise the separate POM self-shadow path. Existing normal/POM/common occlusion ranges remain 48/16/8 yards.

## Five directions per agent

| ID | Direction | Disposition |
|---|---|---|
| G1 | Terrain byte-capacity thrash | Retain existing caps and correctness. Compare a larger cap in the final MPQ stand; no runtime increase without address-space evidence. |
| G2 | Redundant full vertex-span cold copy | Implement direct referenced-XYZ copying only for proven tracked cold misses. Unknown and revalidation paths retain full fallback. |
| G3 | Repeated worker admission/reclaim | Thresholds remain. Repeated stage-3 reclaim often finds source/preparation caches already empty. Worker admission delay is not a rendered-frame duration. |
| G4 | Worker local/load/page/shadow work | Nine publications: median build 485 ms. Worker-stage medians local 161.397, terrain load 138.578, pages 97.616, shadow planning 95.476 ms; these are not additive main-frame costs. Prepared reuse works after cold jobs; page-memo reuse needs attribution. |
| G5 | Retained terrain representation | Previous offset sharing is active; position payload reaches 24.979 MiB, spare capacity only about 0.014 MiB. Another offset-only optimization or shrink-to-fit is not the next answer. |
| M1 | Pressure teardown and batching loss | Keep pressure relief and batching policy. Add phase attribution; do not assume that the roughly 26 ms outside trim belongs to materials. |
| M2 | Fresh pose/range admission | Positive medians fallback 0.78525 ms, bounds 0.45975 ms, envelope 0.2944 ms. Preserve fresh palettes and conservative bounds; rejected one-bone metadata caching stays removed. |
| M3 | Replayed constant payload | Implement bitwise float4 outer-span trimming while preserving one setter per changed bank at the original call position. No allocation, cache or extra API call is added. |
| M4 | Cold snapshot/probation churn | Warm source/resident reuse works. Preserve bounded admission and UP fallback; no speculative reuse or larger default pool. |
| M5 | Native/material GPU work and normal generation | Normal-cache work is much smaller than some native/replay GPU intervals, which include original game work. Keep coverage and first-use attribution separate from texture generation. |
| A1 | Producer/queue-supply gaps | Add exact recorded/published/replayed boundary observations and wait state. No arbitrary query or pacing bypass. |
| A2 | Busy draw/API scene throughput | Native versus injected attribution remains necessary before another batching rewrite. |
| A3 | Synchronous EVENT and outstanding work | Preserve generation, ordering and express-readiness proofs. Selected beforeReplay waits are not proof that every query is slow. |
| A4 | AO/fog/postprocessing | Retain default High and MSAA. #105 remains included; the rejected #106 actor-mask path is not reintroduced. No unproven sample reduction or shader rewrite. |
| A5 | Profiling/state pressure | Fixed bounded long-gap reports and rare trim phase clocks replace ambiguous attribution. No per-command clocks or thread suspension are added. |

Each audit retains raw line/frame/domain evidence under `gi`, `materials` and `ao-stream` in the diagnostic directory. No agent ran intermediate tests, benchmarks or builds.

## Implementation and invariants

The terrain path previously copied a complete locked vertex span to temporary bytes and then copied referenced XYZ into retained positions. For a cold miss with a valid tracked source stamp, destinations and remap storage are now allocated before locking; referenced positions copy directly from the locked span. The lock/read/admission ranges and their charges remain the original full spans. New counters distinguish requested span bytes, actual raw temporary copies, final XYZ copies and direct misses. Geometry classification happens after unlock. Every lock failure/null pointer/unlock failure remains checked. A revision race returns the exact copied immutable snapshot without publishing an optional cache/source-identity proof. Untracked and revalidation behavior retains full-byte fallback.

Material replay retains its complete first float bank and full exit restoration. Against an already-proven previous packet, only byte-identical leading/trailing float4 registers are omitted. The complete enclosing changed span is sent, including gaps. NaN payloads and signed zero use bitwise equality. A changed or shifted PS range retains the full captured write. Setter count, order, error handling, resident/UP behavior and MSAA remain unchanged. Register reduction is structural evidence, not a measured game-FPS gain.

`CSTREAM IDLE_GAP` keeps the four worst gaps of at least 20 ms per 120 replayed Presents and logs at existing boundaries/stop. It records queue states, first/last command, wait flags, reset epoch/cooperative state, publication counters and explicitly nonexclusive producer counter deltas. Odd/even publication serials reject completed-publication ABA races. Frame IDs are labeled as stream-replayed. Producer CPU time and application phase are explicitly unknown. RenderProfile=0 adds no diagnostic clocks, snapshots, publication stores or log records; only predictable disabled branches remain. No thread is suspended, no game memory is written, and query order, wake/spin cadence and frame pacing are unchanged.

Rare memory-transition logs now split world/material/replay cap changes, stream policy and the existing trim. World trim further splits quiesce/state, terrain, snapshot/index, pool, replay GPU, mesh-pool and rigid clears. Action order, pressure limits and release semantics are unchanged. Estimated freed cache bytes are not a promise of equal immediately available virtual address space.

## Should the cache be increased?

The process has a roughly 4 GiB 32-bit virtual address space. This run reaches approximately 700-734 MiB available while the GI builder requests a 768 MiB aggregate floor. That already triggers pressure cleanup and disables deferred material capture. More physical RAM does not remove this virtual-address-space constraint. A larger terrain cache may help a reusable working set fit, but can trigger other pressure costs sooner.

The final-only 48 MiB variant modifies copied headers/setters for both baseline and candidate. Production 32/16 MiB limits remain unchanged. The comparison uses complete real MPQ geometry and eight recorded heading sectors. Sixteen sectors are not claimed: sector 2 is missing. A predeclared four-layout stress reconstructs distinct logical resource identities and overlapping shifted backing ranges; it does not capture native draw lists, physical buffer lifetimes or real process VA admission. Its design is frozen before execution and is not tuned after results.

All four baseline/candidate pairs pass ordered triangle XYZ, complete coverage, current source proofs, retained caps, all 40 fixture hashes and eight mutation controls per pair (32 controls total). The complete fixture contains 36 ADTs and 4,608 authored draws. Each comparison cell below aggregates 11,310 warm capture calls. Timings are one descriptive pair per cell, not repeated statistics or game FPS. Both faster and slower cells are shown.

| Nominal cap (MiB) | Effective warm cap (MiB) | Layouts | Input changes | Baseline -> current capture CPU (ms) | Warm misses (both) | Current peak retained bytes |
|---:|---|---:|---|---:|---:|---:|
| 32 | 32 | 2 | stable | 3.2478 -> 3.0685 | 0 | 9,984,792 |
| 32 | 32 | 2 | vertex revisions | 15.9832 -> 16.7778 | 0 | 9,984,792 |
| 32 | 32 | 2 | vertex + index revisions | 23.7900 -> 24.3906 | 0 | 9,984,792 |
| 32 | 32 | 2 | layout changes | 23.3350 -> 24.5817 | 0 | 19,459,036 |
| 32 | 16 after pressure | 2 | stable | 3.0170 -> 3.3412 | 0 | 9,984,792 |
| 32 | 16 after pressure | 2 | vertex revisions | 16.7509 -> 15.3962 | 0 | 9,984,792 |
| 32 | 16 after pressure | 2 | vertex + index revisions | 23.0686 -> 23.5898 | 0 | 9,984,792 |
| 32 | 16 after pressure | 2 | layout changes | 121.4379 -> 123.1621 | 7,612 | 16,776,324 |
| 48 | 48 | 2 | stable | 2.3329 -> 2.2722 | 0 | 9,984,792 |
| 48 | 48 | 2 | vertex revisions | 14.2078 -> 13.9976 | 0 | 9,984,792 |
| 48 | 48 | 2 | vertex + index revisions | 21.3119 -> 22.7036 | 0 | 9,984,792 |
| 48 | 48 | 2 | layout changes | 23.3863 -> 22.9707 | 0 | 19,459,036 |
| 48 | 16 after pressure | 2 | stable | 2.4503 -> 2.4151 | 0 | 9,984,792 |
| 48 | 16 after pressure | 2 | vertex revisions | 14.2309 -> 14.5090 | 0 | 9,984,792 |
| 48 | 16 after pressure | 2 | vertex + index revisions | 21.1641 -> 23.3735 | 0 | 9,984,792 |
| 48 | 16 after pressure | 2 | layout changes | 123.3126 -> 121.3140 | 7,612 | 16,776,324 |
| 32 | 32 | 4 | layout changes | 22.8226 -> 23.6869 | 0 | 24,766,922 |
| 32 | 16 after pressure | 4 | layout changes | 173.1878 -> 170.3903 | 11,238 | 16,776,732 |
| 48 | 48 | 4 | layout changes | 22.7056 -> 22.9296 | 0 | 24,766,922 |
| 48 | 16 after pressure | 4 | layout changes | 164.9657 -> 168.2038 | 11,238 | 16,776,732 |

The two-layout set retains about 18.56 MiB; the four-layout stress about 23.62 MiB. Both already fit 32 MiB, so 48 MiB removes no further misses in this stand. Reducing to 16 MiB produces 7,612 misses with two layouts and 11,238 with four, unchanged by the cold-copy optimization. The stand therefore demonstrates the cost of an undersized cache, but does not reproduce the complete native 32 MiB churn observed in the game. It cannot establish that a real-game increase to 48 MiB would be useless or safe. Production remains 32/16 MiB given the observed VA/admission pressure.

The cold first-view copy counters provide a separate structural result: 703 direct misses request 4,884,444 vertex-span bytes, copy zero full-span temporary bytes and copy 2,446,440 XYZ bytes. The previous path additionally copied the full 4,884,444 bytes before XYZ extraction. Requested reads and budget charges remain unchanged. This is fewer logical copied bytes; cache-line traffic, GPU time and FPS are not inferred. Warm revalidation still follows its existing path, and retained accounting increases by the explicitly charged 32 bytes of new diagnostic fields. The mixed CPU timings do not establish a general capture-CPU speedup.


## Final acceptance and delivery

All **69 selected automatic scripts pass: 39 native and 30 Windows**. The full 305-script inventory is registered, not fully executed. Coverage includes existing command-stream/EVENT ordering and lifetime controls, real O2/O0/ASan/UBSan/TSan executions, exact terrain geometry/provenance/accounting controls, material state/restoration, live settings/ABI, the existing AO quality/method oracles and all 12 paired fog/composite GPU comparisons at the existing 1e-5 error bound.

Material replay passes the original 24 binding cases plus 52 constant-bank cases in optimized and unoptimized actual-production-path controls. Four deliberately unsafe variants are rejected. For the authored single-register cases, requested VS registers fall from 768 to 513 and PS registers from 244 to 240 across initial writes/draws/restoration; setter count and call sequence remain equal. These are fixture workload counts, not measured native hit rates or FPS.

The new idle gate passes nine groups with actual Queue/Replayer operations: unpublished/published/empty states, complete-publication ABA, producer sync/backpressure/express waits, bounded reporting, reset/device loss, interruption and profiling disabled. Allocation, lifetime and payload ordering remain checked under sanitizers. Terrain positive modes pass direct-copy sparse/duplicate/degenerate indices, both index widths and list/strip, full fallback, racing revisions, failures and no allocation under the vertex lock.

The first native attempt passed 36/39. Three fixture failures were retained and repaired without changing production or dropping assertions: the XYZ mutant originally became degenerate before reaching the exact-XYZ oracle; concurrent chunk recycling invalidated a scheduling-dependent heap-allocation assumption; and extracted pressure wiring lacked ambient stubs for new clocks. The new mutant changes height while preserving valid XY, the queue deterministically preloads three chunks before a concurrent tail, and pressure wiring receives clock locals/stubs. All original exact-geometry markers, payload/allocation assertions, policy thresholds and negative controls remain. Only those three fixtures were repeated to PASS. Production hashes are identical across acceptance attempts.

The existing hidden system-D3D9 material stand passes single-sample and MSAA 2x/4x/8x plus 120 pressure/recovery frames. It verifies state, admission and draw behavior, not exhaustive per-sample color/depth readback. No shader or default High-quality change is introduced here. Historical unavailable contact-normal capture inputs remain unavailable; no baseline is fabricated. The previously rejected #106 actor-mask path remains absent; #105 opt-in AO quality remains available.


The x86 renderer builds successfully from **`e15cc6dbf914166c697bfdb743509f55b8c3e263`** with pinned Zig 0.15.2. All build source hashes remain unchanged. Generated outputs are verified equivalent to HEAD apart from line endings, then exact committed bytes are restored. The two existing misleading-indentation warnings in unchanged displacement code remain. PE/PDB GUID and age match without loading the DLL.

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `d3d9.dll` | 5,707,776 | `10505551519920cea3db18ed5c37ff7f6c21ff64eacdc9da0b6a12e10d4b1825` |
| `frd9.pdb` | 46,211,072 | `a37ec6b7646b114bc2d81ce80c3220fce748ede64f6943dd4befd4f0fac043f8` |

Startup BUILD source identity: **`46b7faf3fdc17f62391fd2a025f428752a37641a8809f69a6e7c4abe2be59747`**. A later report-only publication commit does not change the built source. The manual-install package is `northlight-builds/northlight-rotation-queue-pressure-e15cc6d-20261024`, containing DLL, matching PDB, full addon, report and hash manifest. Existing settings require no INI edits; the addon includes the prior AO-quality control. Nothing is automatically installed.

Final evidence is retained locally in `adopted-public-validation.json`, the original/repair native and Windows acceptance folders, `mpq-matrix.json` and its four `gi/final-cap*-layouts*-headings8` folders, `final-gpu/validation.json`, `build-final/validation.json`, `build-final/pdb-match.json`, `protected-validation.json` and timestamped protected observations. `pre-final` preserves exact initial pending files and a binary patch. These records support component correctness and the stated limits; they do not prove that the 403 ms native producer stall is eliminated.


Fourteen installed/settings inputs, including `wow_optimize` binary/symbol/config, current renderer, DXVK backend, Northlight settings and addon, are guarded against change. Previous `2edeb0c` and `1fda027` delivery packages remain immutable. Private MPQs and world cache are not modified. No game process is launched/stopped, no DLL is automatically installed, and no personal setting is restored or rewritten.
