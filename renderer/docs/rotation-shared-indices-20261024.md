# Northlight camera rotation: 23 October runtime evidence and 24 October 2026 validated build

**Investigation and offline validation complete.** The adopted terrain source passes all 50 selected final scripts, both public MPQ wiring smokes, discovery/equivalence/layout checks, the final build and DLL/PDB identity checks, hidden-device material MSAA/state/pressure gates, and installed-input preservation checks. These results establish the stated offline behavior; they do not establish a whole-game FPS uplift.

Report destination: `renderer/docs/rotation-shared-indices-20261024.md`. The separate DLL/PDB/report/manifest delivery uses recorded source `2edeb0c`; its package directory is `northlight-builds/northlight-rotation-shared-indices-2edeb0c-20261024`. Publication uses existing [draft PR 98](https://github.com/lepola/northlight/pull/98). The package is separate from installed game files. PR 98 remains draft pending the user's real-client check.

The latest user-run log confirms that the previously delivered query-readiness and material-content reuse fixes are active. Remaining sampled stalls are broader than materials: the run contains terrain cache churn, slow native rendering calls and producer/query waits, including substantial stalls with materials disabled. No controlled game FPS uplift can be calculated from this run. The AO and material proposals and the first two terrain prototypes were rejected. The third terrain variant is adopted after exactness, accounting and paired CPU gates, with explicit fitting/unique-control costs below. All 50 selected final scripts and both public MPQ wiring smokes pass against the adopted production source. A discovery-only follow-up preserves exact renderer/test-source bytes. The final build, material MSAA/state and preservation checks also pass, as recorded below.

## Evidence and interpretation

The 22:08 run used installed DLL SHA256 `32133fd7a6af1049833defb2e232291bb6d93fca1575744d22b5c67695bb330a` (5,624,832 bytes), renderer identity `caaae4e`, startup source fingerprint `9c74b553вЂ¦`, and **DXVK 3.1.1**. It predates the new integration described below. These are user-generated logs; no game was launched during this investigation. `wow_optimize`, personal settings, installed DLLs, MPQs and the game world cache were not modified.

Material samples through frame 1651 are enabled with all four modes set to displacement; samples 1778вЂ“2921 are disabled. The transition between those samples is uncertain. Retrospective slow-frame rows are associated with frame brackets, not their printed position. Enabled frames 0вЂ“889 contain no active material world workload. Selected slow rows and rare query records are not an unbiased frame-time distribution.

CPU exclusive material scopes can be compared without adding their inclusive parents. Original `nativeDraw` rendering is not all material overhead. Native API wall time includes driver/GPU backpressure; it does not alone prove CPU computation or shader compilation. GPU scene, cascade, effect and stage intervals overlap and must not be summed into a new total.

Log references below are one-based lines of the frozen `northlight-renderer.log`; FreezeCatcher references belong to the separately archived optimizer log. The delivered renderer and matching PDB were inspected without executing them.

## Fifteen investigated directions

### Terrain and GI

| ID | Direction and evidence | Current conclusion |
|---|---|---|
| G1 | Terrain retained working set: material-OFF frame 2667 has 663 capture calls, 430 rebuilds, 432 evictions and **5.940 ms**, with no changed revisions. Stable frames 1397вЂ“2159 have no reads/misses and cost 0.289вЂ“0.315 ms. | Byte pressure is a recurring CPU capture cost. Slot pressure is zero; increasing the 8192-entry limit would not address it. Preserve the 32/16 MiB limits. |
| G2 | Decoded index duplication: roughly 10.95вЂ“11.91 MiB of decoded indices plus 2.07вЂ“2.25 MiB of source offsets. The existing actual-MPQ fixture census contains 31 exact canonical patterns: 14,136,672 logical bytes versus 77,856 unique bytes. | The third exact immutable decoded-block sharing variant is accepted for adoption. Position vectors, identities, current-byte proofs and every triangle remain independent/correct. The first two prototypes were rejected; final pressure gains and control costs are reported below. |
| G3 | GI cold build versus turning: only build ID 1 is published, at **640 ms**. Its 14.930 ms memory work is included in the surrounding phase timings. No later publication or entry-reclaim event appears. | The cold worker job is real; repeated GI rebuilds during later turns are not demonstrated. Do not treat the initial far cascade or loading spike as another worker rebuild. |
| G4 | Source/page preparation: the initial build has 105 reads/26 hits, 2121 preparation misses/zero hits and zero page-memo hits. | One cold build does not establish broken reuse. Preserve the existing 16+8+8 MiB optional allowance and exact file/layout/residency proofs; later spatial reuse needs its own replay. |
| G5 | Revision versus capacity instrumentation: frame 2540 has 644 revision changes, 644 exact reuses, 42 rebuilds and **1.681 ms**. Frame 2286 mixes lookup misses and changed content. | Distinguish actual changes from capacity churn. Logical/physical index, position and lifetime accounting should be measured without rehashing the entire cache in a draw hook. |

Evidence: G1 L4079вЂ“4084; G2 L1924/L3541/L4086 and the bounded MPQ fixture census; G3вЂ“G4 L918вЂ“922; G5 L3912вЂ“3917. The matching VB and IB change counters at frame 2540 are both 644; they are not 1288 independent changed draws.

### Materials

| ID | Direction and evidence | Current conclusion |
|---|---|---|
| M1 | Live verified-content reuse: capture/geometry match **566/566 shortlisted tests**, comparing 7,936,668 bytes across diagnostic frames 1143 and 1200. Every sampled OFF row has zero tests/hits/compared bytes. | The shipped fix activates and finds real repeated payloads. The 100% figure applies to shortlisted exact comparisons, not all captures or misses. Another identity layer is not justified. |
| M2 | GPU residency/probation: frame 1200 still has 173 UP draws, 106 creation-attempt deferrals and 67 probation evictions. Warm frames 1397/1524/1651 instead have 287/287/288 resident draws and **zero UP bytes, uploads, creations or evictions**. | Cold streaming pressure remains separate from a functioning warm cache. Keep the 16-creation, 32 MiB and entry limits; do not move an unmeasured creation spike into one frame. |
| M3 | Source/capture preparation: at frame 1143 snapshot CPU is **2.3331 ms**, deferred recording 1.7449 ms, with approximately 2 MiB read by each source participant. The three warm samples have zero source reads. | Cold snapshot work is substantial. Equal read totals alone do not prove that every byte was redundantly copied; a new reuse path must preserve each current resource/revision/layout proof. |
| M4 | Pose/envelope work persists while warm: approximately **0.30 ms** envelope plus **0.49 ms** fallback, 241вЂ“242 envelope time refusals and 8184 fallback vertices, including 6618 single-bone vertices. | A bounded immutable-preparation cache was tested and **rejected**: the dominant authentic one-bone combined path became slower despite an isolated four-bone improvement. No stale pose or larger budget was introduced. |
| M5 | Material GPU/visibility: sampled warm M2 deferred intervals are **0.915вЂ“1.281 ms**; measured normal generation occurs in small cold builds/mips, with no rebuild in the warm samples. Sampled OFF source/pose/capture/GPU work is zero. | Keep the current near-only occlusion distance (8 yd), normal distance (48 yd) and exact far rejection. An OFF CPU scope can still contain native game rendering; shared world-source retention is not proof of material work. |

Evidence: M1 L1278/L1281/L1666/L1669; M2 L1673/L2199/L2371/L2560; M3 L1214/L1231/L1279/L1283; M4 L2148вЂ“2149/L2197 and later warm samples; M5 L1309вЂ“1312/L2214/L2389/L2578, with OFF source/geometry/cache rows at L2721вЂ“2727.

### Command stream, native calls and AO/post

| ID | Direction and evidence | Current conclusion |
|---|---|---|
| S1 | Native draw stalls: frame 1143 spends **19.021 ms** inside 921 native indexed draws; one call takes 10.410 ms. The enclosing GPU scene is 21.676 ms. Materials-OFF frame 2667 still has 9.501 ms of native indexed draws. | Rendering/backpressure remains important independently of materials. Pipeline compilation is a hypothesis; the log lacks per-stall compilation telemetry. Exact state/shader/draw replay is needed before claiming a warmup fix. |
| S2 | Producer gaps and synchronous trips: slow frame 1127 totals 75.699 ms with 64.249 ms scene queue idle. Game-frame 1163's query trip is 45.442 ms, including 40.250 ms before replay and 5.162 ms polling. | A consumer without commands and a producer waiting for earlier work are distinct observations. Their phase intervals overlap; arbitrary query bypass cannot remove required earlier work. |
| S3 | EVENT readiness adoption: cumulative frame-2400 report has **4319 posts / 4319 hits / 0 fallbacks**, versus 55.6% success in the earlier 19:33 run. | No express fallback appears in the reported cumulative windows through frame 2400. Preserve END-generation, epoch, lifetime and wait-budget proofs. This is mechanism validation, not a game FPS percentage. |
| S4 | AO/post throughput: matched AO+AOBlur median is **0.436 ms**; the fog suite median is 1.8135 ms. AO CPU submission is about 0.0005/0.0004 ms. | These ordinary GPU costs do not explain every 40вЂ“75 ms stall. An exact AOBlur early-break experiment passed images but regressed timing and was rejected. |
| S5 | API/queue/profiling overhead: material-OFF window 1801вЂ“2400 records about 9509.8 commands/frame and 7.759 ms replay busy. A sampled frame estimates 0.413 ms of timer overhead. | Issue 104 motivates attribution of Northlight calls. Instrument missing terrain-swap calls first; do not charge sampled profiling overhead to every ordinary frame or broadly narrow SavedState without a touched-state/failure proof. |

Evidence: S1 L1132/L1308/L4014; S2 L1499/L1688; S3 L3664; S4 seven matched AO/AOBlur and fourteen matched fog samples in `analysis.json` (example GPU profile L2089); S5 L2568/L3664. Query trip, queue idle and GPU intervals are different scopes rather than additive stall causes.

The [terrain/GI audit](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/gi/audit.md), [material audit](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/materials/audit.md) and [stream/AO audit](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/ao-stream/audit.md) retain exact source references and log line numbers.

## What the captured stacks prove

CodeView GUID and age match the delivered `caaae4e` PDB. The 76 ms FreezeCatcher chain resolves to `GetData в†’ runExpress в†’ waitPumped`. Samples near the ends of the 565 ms and 210 ms loading frames resolve to `StreamSurface::LockRect в†’ lockImage`, using the ordered and express paths respectively. These identify sampled wait mechanisms; they do not attribute those entire frame durations to a lock or query. No renderer DLL was executed to resolve the symbols. See [PDB evidence](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/ao-stream/pdb-publics.json).

## Issue 104 and the accepted diagnostic change

[Issue 104](https://github.com/lepola/northlight/issues/104) proposes reducing Northlight-issued binding, constant and state-save/restore calls. **It is partially investigated, not implemented or closed by this iteration.** Existing replay binding caches, dirty constant ranges and pooled SavedState already address parts of the proposal; this iteration adds attribution for a known missing path rather than a complete batching solution.

The issue's timings come from macOS/Wine with DXVK 1.10.3. This run uses DXVK 3.1.1, whose statically indexed constant layout packs accessed ranges. The old estimate that a high coverage register necessarily uploads everything up to that register is therefore **not assumed here**. No low-register-placement benefit is claimed without measurement. See the verified primary [DXVK 3.1.1 device implementation](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/d3d9/d3d9_device.cpp), [constant-copy implementation](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/d3d9/d3d9_constant_copy.cpp) and [constant-layout contract](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/d3d9/d3d9_constant_copy.h); exact blob identities and source copies are archived in [backend evidence](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/issue104-backend-evidence.json).

None of these changes is demonstrated to remove producer gaps, GPU backpressure or required query waits. Those remain separate profiling/reproduction problems.

Commit `848b9125587fda9c78db8e6cc4d2c2f9627d4af9` adds sampled **TERRAIN_SWAP** telemetry for the mirrored terrain-shadow path previously absent from raw-call totals: getter requests versus actual native forwards, setter/register-range attempts, rollback/restoration attempts and API failures. Getter requests answered by the mirror are not mislabeled as native forwards. Setter counts include attempts, including failures; inspect those semantics before combining totals with existing raw-call rows. The patch adds no clocks, D3D calls, shader/state changes or distance/quality cuts. Profiling OFF keeps its added counters zero.

Six focused scripts passed, including 121,000 coverage/generated-bytecode cases and 260 extracted production-wrapper state/refcount/order/failure cases, plus O2 and ASan/UBSan draw-gate checks. This is accepted attribution instrumentation; its own FPS uplift is not claimed. The next user-run log can reveal actual terrain-swap volume. See [counter validation](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/ao-stream/pre-integration-and-terrain-counters.md).

## Experiments and adoption decisions

| Experiment | Offline result | Disposition |
|---|---|---|
| AOBlur break after the first failed early-out footprint | Two complete runs passed 324 bit-exact image comparisons, including MSAA 0/2/4/8. Open/sky fields were about 17.4вЂ“18.2% slower; sparse fields 2.3вЂ“3.3% slower. | **Rejected.** Production shader retained; exact pixels alone are insufficient for adoption. |
| Immutable material pose-preparation metadata | 3072 exact controlled-clock calls and 301824 submitted-vertex enclosures passed. Four-bone envelope-only cells improved, but all four authentic one-bone combined medians regressed **1.73вЂ“2.67%**, with unchanged capped fallback work. | **Rejected.** The proposed additive allowance of up to **2 MiB / 128 retained shader-layout pairs** was not justified. No production adoption or GPU gate. All 36 pairs are retained in the [disposition](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/materials/pose-metadata-disposition.md). |
| First immutable decoded-terrain sharing variant | The 16-pose, 32 MiB pressure cell saved **74.3% CPU**, but fitting-working-set and unique-pattern controls regressed. | **Rejected first variant.** Do not present the best pressure cell as an accepted optimization or game uplift. |
| Second terrain prototype: two-hit admission plus typed loops | Correctness and pressure benefits did not justify its larger fitting/unique-control overhead. Its measurements remain archived as the `first` ablation in the final summary. | **Rejected second variant.** It is not the adopted candidate. |
| Third terrain variant: two-hit sharing, typed offset validation, direct mutation guards and hot entry layout | In the 16-pose 32 MiB layout-churn cell, aggregate capture medians fall **187.0465 в†’ 54.3667 ms** and misses **9208 в†’ 0**. Fitting and all-unique controls have explicit costs below. | **Adopted.** Final integrated 50-script suite, both public MPQ smokes, build and material MSAA/state gates pass. No whole-game FPS claim. |

The rejected one-bone component results below are medians of three alternating baseline/candidate pairs, each side measuring 64 frame means of 300 calls. The stock MPQ shader is authentic; geometry, current-pose banks and near/far distributions are synthetic. Lookup/preparation, fixed-bank time charging, envelope work, per-frame cache maintenance and production single-bone fallback are included. These are not complete capture or game-frame timings.

| Pose distribution | Shader/layout pairs | Baseline ms | Candidate ms | Median paired change |
|---|---:|---:|---:|---:|
| All near | 1 | 0.441834 | 0.450228 | +1.90% slower |
| All near | 256 | 0.440950 | 0.448558 | +1.73% slower |
| Mostly far | 1 | 0.441159 | 0.452941 | +2.67% slower |
| Mostly far | 256 | 0.439937 | 0.447906 | +1.81% slower |

Every one-bone pair was slower. Each side still processed 8160 fallback vertices per synthetic frame under the unchanged 8192 cap. More envelope-ready results therefore did not reduce combined fallback work. In contrast, the isolated repeated-pair four-bone envelope cell improved from 0.222547 to 0.148459 ms; selecting only that result would have concealed the dominant-path regression. No new metadata retention is being shipped.

All fixtures distinguish authentic MPQ shader/geometry inputs from synthetic draw/palette distributions. Component gains cannot be added together or multiplied into a whole-game FPS estimate. Caps, current-source/pose proofs, MSAA and visible output remain adoption requirements.

## Adopted terrain design and final component results

Only the exact third candidate was approved. An independently decoded first occurrence remains owned; a second exact match may promote it to an immutable shared decoded block. At most 64 weak hints route candidates, and a complete typed byte comparison proves equality after size/width checks. Sample hashes are not equality proofs. Current positions, raw indices, holes, winding, source identity, source offsets and bounds remain specific to each capture. Copying retains value semantics through deep copies; mutation materializes owned storage. Allocation/retention/admission failures preserve the original exact fallback.

The fixed hint table, expired control/object overhead and live shared storage are charged under the existing **32/16 MiB** limits. Escaped snapshots retain their storage safely after eviction/reset/destruction; accounting does not silently omit it. Typed XYZ source-offset validation preserves every original ordered comparison. Hot entry layout and direct mutation guards reduce the common-path cost; layout-only ablation still has all 9208 misses in the pressure cell, so field layout alone does not resolve the capacity cause. Independent light code review found no actionable equality, ownership, exception or accounting issue, subject to the existing immutable/render-thread usage contract.

The new sampled decoded-storage gauges have overlapping scopes; **do not add them into a memory total**:

| Gauge | Interpretation |
|---|---|
| `logicalMiB` | Decoded vector capacity per cached snapshot reference; sharing repeats that payload in this logical total. |
| `physicalMiB` | Cache-owned shared decoded payload counted once, plus unshared owned payload. Excludes wrapper/control allowances and escaped-only blocks. |
| `sharedBlocks` / `sharedAllocationMiB` | Shared blocks with cache-entry owners; the allocation gauge includes their decoded payload plus a wrapper/control allowance, so it overlaps `physicalMiB`. |
| `liveBlockMiB` | Shared-block payload plus allowance for all still-live blocks, including escaped snapshots after eviction/reset. Overlaps the cache-owned gauges; it is not an additional cache charge. |
| `weakOwners` / `expiredWeakOwners` | Occupied / at least partly expired memo **slots**, each within 0вЂ“64. Every slot has two weak observations (block and snapshot; 128 weak objects total). One expired observer can count while its partner remains live. These are neither allocation counts nor counts of wholly dead objects. |
| `positionMiB` / `positionCapacityMiB` | Cached snapshot position counts / vector capacities; excludes escaped-only snapshots. |

Fixed metadata conservatively reserves both possible retained object/control allowances per slot even while empty, within the existing cap; these allowances may overlap live object allowances. The ledger also has its own fixed allowance. This budget is not an RSS reconstruction. Reset clears both weak observations, while live ledgers retain visibility of escaped strong lifetimes. The source-metadata aggregate subtracts the active shared-allocation charge before combining metadata with live source bytes. Legacy `avoidedIndexMiB` measures compact 16-bit index storage relative to wide storage; **it is not the new sharing saving**. See [exact telemetry semantics](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/gi/telemetry-semantics.md).

The following are **aggregate CPU capture milliseconds over two warm cycles**: 16 reconstructed frames / 11,318 calls for 8 poses, and 32 / 22,586 calls for 16 poses. Each implementation has three alternating-order trial rounds. Parentheses are its observed minimumвЂ“maximum; вЂњchangeвЂќ is the ratio of the two separately calculated medians, **not a median of paired changes**. The last timing column is the minimumвЂ“maximum of the three aligned candidate-minus-baseline trial differences. These are observed ranges, not confidence intervals.

Modes: **0** tracked unchanged revisions; **1** VB revision rewrite; **2** VB+IB revision rewrite; **3** revisions plus alternating reconstructed D3D base/start layout.

| Poses | Cap MiB | Mode | Baseline median ms (range) | Final median ms (range) | Change of medians | Paired delta range ms | Misses baseline в†’ final |
|---:|---:|---:|---|---|---:|---|---:|
| 8 | 32 | 0 | 2.5273 (2.4529вЂ“2.5887) | 2.7140 (2.4072вЂ“2.7620) | +7.39% | -0.0457 to +0.1867 | 0 в†’ 0 |
| 8 | 32 | 1 | 14.0201 (13.9122вЂ“14.3964) | 14.2029 (13.5791вЂ“14.5737) | +1.30% | -0.8173 to +0.5536 | 0 в†’ 0 |
| 8 | 32 | 2 | 23.0047 (21.8285вЂ“23.6087) | 21.3655 (21.1979вЂ“23.2179) | -7.13% | -1.8068 to -0.3908 | 0 в†’ 0 |
| 8 | 32 | 3 | 24.5590 (23.2742вЂ“24.7204) | 23.5690 (22.8648вЂ“26.3285) | -4.03% | -1.8556 to +3.0543 | 0 в†’ 0 |
| 8 | 16 | 0 | 46.7098 (46.5607вЂ“47.6680) | 2.4854 (2.3294вЂ“2.4855) | -94.68% | -45.1826 to -44.2243 | 2,832 в†’ 0 |
| 8 | 16 | 1 | 55.6193 (55.4255вЂ“56.5602) | 14.0377 (14.0259вЂ“14.4967) | -74.76% | -42.5225 to -40.9288 | 2,832 в†’ 0 |
| 8 | 16 | 2 | 65.6599 (64.5050вЂ“83.6814) | 24.5991 (21.9374вЂ“27.0498) | -62.54% | -61.7440 to -37.4552 | 2,832 в†’ 0 |
| 8 | 16 | 3 | 158.6760 (155.5666вЂ“161.9016) | 126.1935 (124.8988вЂ“129.8818) | -20.47% | -37.0028 to -28.7942 | 9,118 в†’ 6,462 |
| 16 | 32 | 0 | 6.3560 (5.0256вЂ“6.6527) | 5.2335 (4.9658вЂ“6.7451) | -17.66% | -1.6869 to +1.7195 | 0 в†’ 0 |
| 16 | 32 | 1 | 30.8585 (28.8147вЂ“32.2503) | 30.9151 (27.8730вЂ“32.2270) | +0.18% | -2.9855 to +3.4123 | 0 в†’ 0 |
| 16 | 32 | 2 | 54.3984 (48.3856вЂ“56.2981) | 50.5702 (45.9736вЂ“54.5497) | -7.04% | -3.8282 to -1.7484 | 0 в†’ 0 |
| 16 | 32 | 3 | 187.0465 (181.7519вЂ“188.6884) | 54.3667 (48.1566вЂ“55.3639) | -70.93% | -138.8899 to -126.3880 | 9,208 в†’ 0 |
| 16 | 16 | 0 | 101.4978 (99.9799вЂ“103.4715) | 5.1513 (5.0679вЂ“5.5058) | -94.92% | -98.4036 to -94.8286 | 5,986 в†’ 0 |
| 16 | 16 | 1 | 136.2886 (122.4753вЂ“146.0854) | 36.9309 (33.2912вЂ“42.2037) | -72.90% | -112.7942 to -85.5444 | 5,986 в†’ 0 |
| 16 | 16 | 2 | 139.2808 (136.1341вЂ“192.4354) | 56.5259 (47.5569вЂ“58.3714) | -59.42% | -144.8785 to -79.6082 | 5,986 в†’ 0 |
| 16 | 16 | 3 | 341.4313 (328.2407вЂ“341.6489) | 298.0404 (292.1477вЂ“323.9213) | -12.71% | -49.5012 to -4.3194 | 19,584 в†’ 15,848 |

In the 16-pose 32 MiB mode-3 pressure cell, the difference of medians is **в€’132.6798 ms / в€’70.93%**, with every paired trial saving **126.3880вЂ“138.8899 ms** across 22,586 calls. This is a component capacity result, not a measured reduction in game-frame duration. The 8-pose 32 MiB fitting mode-0 control instead costs **+0.1867 ms** by difference of medians, with paired changes **в€’0.0457 to +0.1867 ms**. Normalizing that difference to 691 calls gives approximately **+0.011 ms**; this is an arithmetic workload reference, not a sampled game-frame cost. Other fitting cells also vary in sign, as the table shows.

The synthetic all-unique control has **650 distinct 64-triangle draws**. Baseline median is **1.267199 ms** (1.148885вЂ“1.297899), final **1.451852 ms** (1.425003вЂ“1.568781): difference of medians **+0.184653 ms / +14.57%**. All three paired trials are slower, by **+0.184653 to +0.276118 ms**. The difference-of-medians arithmetic equivalent at 430 unique rebuilds is approximately +0.122 ms; it is not measured game performance. Adoption explicitly accepts this unique/fitting overhead for the demonstrated pressure benefit. There is no all-zero-regression claim.

The native-host CPU focused gates pass 15 program/configuration runs across O2, O0 and ASan/UBSan, covering value semantics, GPU-rebase/fence ownership oracles, lifetime and source accounting. Three additional wide proofs exercise 65,538 remapped vertices and exact 32-bit indices/XYZ; the unchanged production triangle cap rejects that oversized draw, while only the explicit test configuration permits it. Promotion, complete-equality collision rejection, allocation/retention failures, failed admission, expired weak storage, reset/reentry and post-cache-destruction snapshots are covered. An unsafe-equality mutant fails the independent XYZ oracle. Both MPQ trace families pass 16 content/revision controls each, including expected rejection of stale XYZ/index order, plus exact geometry/coverage, caps and source/helper/generated-header/executable guards.

These CPU gates use real MPQ XYZ, holes and winding and fresh recorded headings. D3D draw layout, LOD and visible draw lists are reconstructed; they are not a captured native command stream. The all-unique and oversized-wide geometry are synthetic. Native-host execution here is **not** a D3D GPU/pixel benchmark, and the result does not prove GPU speed, game FPS, cold-frame tails or final native-frame gains. Private MPQ bytes remain outside the proposed repository changes. See the complete [final GI summary](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/gi/final-summary.json) and [GI disposition](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/gi/final-result.md). Their pre-adoption status fields record when those evidence files were written; the adoption decision stated here follows the independent review.

Both rejected prototypes and every failed attempt remain archived. In particular, the first 8-heading run failed the strict required-churn guard because its 32 MiB working set fits; the guard was not relaxed. The separate predeclared fitting control asserts zero misses, while the pressure gate still requires churn. Initial shared-wide wrapper/default-cap failures remain preserved; the wrapper repair disabled no warning, cap or geometry check.

## Upstream integration and completed checks

The original local HEAD `36e3a4225e0474c72971f81407a3148df1a7b861`, accepted counter commit `848b912вЂ¦`, and latest upstream main `e38c58aa1e8ed09124575080fc6f5b9371dbf62d` are all ancestors of merge **`61d991071cacce7622a68e7e2f4e2f01dfa744d9`**. The merge was clean after resolving `shaders/shader-build.json` and `tests/test_diagnostics_switch.py`. The diagnostic allowlist retained the required union. Three incoming AO bytecodes match upstream exactly; existing AOBlur/AO/Composite/Rain controls remain byte-identical. See [integration ancestry](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/integration-after.json) and [conflict resolution](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/merge-resolution.json).

Before integration, **35 distinct selected scripts passed**: 17 Windows and 18 native, including the full command stream and EVENT variants. The first native wrapper's source guard correctly failed because two generated Zig cache artifacts landed under a Windows-style relative path in WSL. Its original failure is preserved. An exhaustive audit classified exactly those two artifacts and confirmed tracked source inputs unchanged; after isolating native cache/output paths, the affected full command-stream suite passed again with all 1130 guarded inputs unchanged. This incident was classified, not suppressed or relabeled. See the [artifact audit](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/pre-integration-native/artifact-guard-audit.json).

After merge, the upstream **99-case AO gate**, diagnostics switch, log formatting and job-system checks passed. Before merge, a deliberate negative control ran that upstream stand against the old local AO bytecodes and correctly detected three missing HBAO/GTAO fixes. That expected-negative experiment is separate from the passing 35-script baseline; no assertions were weakened to obtain the post-merge pass. The final selected regression results and remaining delivery checks follow below.

Upstream was refreshed again before finalization and remained `e38c58aa1e8ed09124575080fc6f5b9371dbf62d`, already included by merge `61d9910`; no additional upstream commit appeared during testing.

## Final selected regression suite вЂ” 50 scripts passed

The adopted production revision **`0731f4f158820bb8caddb3287d4ba2defb31032d`** passes **22 Windows + 28 native scripts, 50 distinct scripts total**, with exact inventories and no skips in those inventories. Every result row is PASS; both runner exits are zero. Each platform guard verifies all **1144** watched inputs unchanged before/after, and records no game process at its boundaries, no game launch and no installed-file writes. The Windows suite includes the 99-case upstream AO gate with zero failures. The native suite explicitly includes the new public decoded-sharing test plus full command-stream, EVENT, terrain ownership/accounting and material-content cache tests. This is the selected regression scope, not a claim that every repository test or a new full material pixel/MSAA matrix ran. See [Windows final validation](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/post-integration-decoded-windows/validation.json) and [native final validation](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/post-integration-decoded-native-wslgit/validation.json).

The first final native preflight failed before tests or compilation: WSL Git could not interpret the worktree's Windows-absolute gitdir. Only the external runner was repaired with explicit translated, read-only `GIT_DIR`/`GIT_WORK_TREE`; ancestry assertions and repository sources/tests were unchanged. The fresh `post-integration-decoded-native-wslgit` retry passes. The [original preflight failure](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/post-integration-decoded-native/preflight-failure.json) remains preserved.

The first public 16-pose smoke also failed before compilation because the supplied legacy `mpq-fixture-1277` manifest lacks the required `complete` field. No assertion or fixture data was changed. Both fresh-output retries pass using the existing complete `rotation-followup-20261023-1541/terrain-fixture-off` fixture already used by the adopted timing gates, unchanged. The [fixture preflight failure](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/final-public-mpq16/preflight-failure.json) is preserved; it is not counted as a successful public smoke.

The new source-only test ran explicitly in the 50-script suite. Commit **`2edeb0c8e0addd4930e8abafa6c105bc9243eaa5`** subsequently adds its single discovery-list line. The exact **301-test automatic inventory** and layout checks pass; the only changed input is `tests/discovery.txt`, with renderer and test-source bytes verified equal to tested `0731f4f`. This is a verified metadata-only follow-up, not a second 50-script execution on the later commit. The final build targets `2edeb0c`. See [discovery/equivalence validation](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/discovery-validation.json).

The public 16-pose pressure smoke passes **520 rows per implementation**, and the separate 8-pose fitting smoke **264 rows per implementation**; each has one round and **8 mutation controls**, including expected stale-output failures. Together these cover **1568 result rows and 16 mutation controls** across baseline/current implementations. Source/helper/generated-input/baseline/artifact guards pass, with recorded no-game boundaries. Both use the immutable `61d9910` baseline and Zig 0.15.2. They verify the public flags/SFINAE/fixture wiring and exact geometry/coverage/caps, while explicitly allowing intended storage/miss-counter changes. **One-round smoke timings are not an uplift benchmark** and do not replace the adopted three-round matrix. See [16-pose public validation](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/final-public-mpq16-complete/validation.json), [8-pose fitting validation](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/final-public-mpq8-complete/validation.json) and the [combined final summary](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/ao-stream/final-validation-summary.json). The latter also verifies cross-platform content equality after mapping five preserved legacy cache-artifact filenames whose colon escapes differ between Windows and WSL; each platform's original before/after guard passed unchanged.

## Public reproduction commands вЂ” wiring checks passed

Reproduction target: build-source revision `2edeb0c8e0addd4930e8abafa6c105bc9243eaa5`, with verified renderer/test-source byte equivalence to tested `0731f4f158820bb8caddb3287d4ba2defb31032d`. Run from that checkout; replace every angle-bracket placeholder with a real path, and give each invocation its own new/empty output directory. The source-only automatic command passed within the final native suite, and both public MPQ wiring smokes passed. The earlier three-round external gates above are separate performance evidence.

The source-only automatic test requires native `clang++` with ASan/UBSan support and needs neither MPQ data nor a Git baseline. It runs O2, O0, sanitizers and the expected-failing unsafe-equality oracle:

```text
python tests/test_terrain_decoded_sharing.py --output "<fresh-output>/decoded-cpu"
```

The manual comparison requires Windows executable support, the configured Zig toolchain, Git containing the full immutable baseline, and a previously generated **complete Northrend MPQ fixture** whose manifest/output hashes match. `<private-camera-log>` must cover all requested heading sectors within that fixture. The scripts read those private inputs; the commands do not launch the game or rebuild its world cache.

```text
python tests/validate_terrain_classification.py --fixture "<private-complete-mpq-fixture>" --camera-log "<private-camera-log>" --output "<fresh-output>/mpq-16" --baseline-ref 61d991071cacce7622a68e7e2f4e2f01dfa744d9 --camera-count 16 --storage-comparison --rounds 1
python tests/validate_terrain_classification.py --fixture "<private-complete-mpq-fixture>" --camera-log "<private-camera-log>" --output "<fresh-output>/mpq-8-fitting" --baseline-ref 61d991071cacce7622a68e7e2f4e2f01dfa744d9 --camera-count 8 --storage-comparison --fitting-control --rounds 1
```

`--storage-comparison` permits the intended storage/miss-counter differences while preserving exact geometry, coverage and caps. The explicit 8-pose fitting control requires zero baseline 32 MiB churn; the 16-pose pressure run retains its required-churn check. One round is a wiring smoke, not the three-round timing gate. Use `--rounds 3` and fresh outputs for a repeated comparison, running CPU/GPU-heavy work serially and retaining the generated evidence/source guards. No private fixture content is committed by these commands.

## Validated build, device checks and preservation

The final DLL/PDB were built from **`2edeb0c8e0addd4930e8abafa6c105bc9243eaa5`** with Zig 0.15.2. Source identity **`b9d53b5efbf996c36ac3c1e80fff7c2ff4d49c9fe74c25db15df3aa480c2ce84`** is both logged and embedded. Source guards pass, and generated outputs match the commit after line-ending normalization. The DLL's CodeView GUID/age match its PDB; that check did not load the renderer. See [build validation](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/build-final/validation.json) and [PDB match proof](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/build-final/pdb-match.json).

| Artifact | Bytes | SHA256 |
|---|---:|---|
| `frd9.dll` | 5,640,192 | `9bc13ef28e52d1331b58bea381533444becbcf67b24806ef9e3bbd33b94b6f2d` |
| `frd9.pdb` | 46,129,152 | `6d402e89b71e89170b35d739107b1563f51aa6d2af036e9e30c2be54760325d0` |

The production-header hidden-device stand passes against **Windows system x86 D3D9** for single-sample and **MSAA 2/4/8**. It verifies POM shader admission, generated-map preparation, single-sample material capture with private depth where required, and restoration of original shader/render-target/depth bindings. Its **120-frame pressure test** keeps relief active without extra shader/map generation and then verifies recovery, with full configured caps retained. Source/input guards pass and no game process is present at the run boundaries. This is a device/state/MSAA correctness gate; it does not perform a new pixel-readback equivalence matrix or establish behavior of the user's complete DXVK game frame. See [device validation](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/final-gpu/validation.json) and [complete suite output](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/final-gpu/suite.log).

`verify_untouched.py` confirms all **10 monitored installed/configuration inputs** and the previous `northlight-rotation-ready-caaae4e-20261023` delivery remain byte-identical. Those inputs include the installed renderer, `wow_optimize.dll`/symbols, optimizer and personal settings, and DXVK/profile files. No installed-file replacement or game launch occurred. See [preservation validation](D:/Games/335_engb_mod/northlight-diagnostics/rotation-next-20261023-2208/untouched-validation.json).

The separately prepared build is identified above; the 23 October 22:08 runtime log continues to identify the earlier installed DLL. It is not a runtime test of this 24 October build. Live terrain-swap volume, original native command/state behavior, the real reuse distribution and rotation FPS remain subjects for the next user-run evidence rather than extrapolations from component timings.


The investigation and testing in this report are confined to Northlight. The frozen optimizer log is diagnostic input only; `wow_optimize` and the user's configuration remain untouched.
