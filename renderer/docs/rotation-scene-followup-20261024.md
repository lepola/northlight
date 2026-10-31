# Rotation follow-up: cache capacity, source attribution and replay inputs

This is local work. No changes are published or installed. The available runtime
log is still the user run ending at 03:28:59 on 24 October, SHA-256
`b71ba562267c13188f24219df83bd71bdf7273e9bd914210833227343d886443`.
It identifies the preceding installed renderer, not the new private source.
No game was launched. The optimizer, personal settings, MPQs and installed
renderer remain outside the write scope of this work.

## Would larger caches help?

Selectively, possibly. The evidence does not support increasing every limit or
claiming a whole-game FPS gain from a cache-only replay.

| Cache or boundary | Evidence in the available run | Decision |
| --- | --- | --- |
| Terrain content | Frame 18415: 33,553,912 / 33,554,432 bytes, 571 misses, 573 byte evictions, zero count pressure; capture maximum 8.349 ms. | A 32 versus 48 MiB comparison is justified for this working set. Raising the 8192-entry limit would not address byte pressure. Keep the current default pending a complete comparison. |
| Generated normal maps | Final counters: 7,865 generations, 7,606 evictions, 6,721,858 hits, about 31.90 MiB retained. In the final warm intervals generations and evictions stop increasing while hits grow. | Maps are reused. A larger cache may reduce cold-turn churn; it would not remove continuous pose/draw work. |
| Displacement | Final counters: 2,985 builds, 147,574 hits, about 31.09 MiB retained; builds stop increasing during the final warm intervals. | No evidence of constant regeneration while warm. Do not increase the budget without the visible working set and pressure measurements. |
| Stream snapshots and uploads | Final cumulative snapshot misses and overflows are zero; about 0.20 MiB pooled. Texture shadow has 58,353 hits and 3,748 evictions, but only five readbacks attributed to a relocked evicted image. | More snapshot or command storage cannot fill an empty producer queue. Texture capacity is a secondary measured candidate, not an explanation of the long gaps. |
| Address-space pressure | Late samples have roughly 680–700 MiB free virtual address space in the 32-bit process, despite ample physical RAM. Geometry build admission requires 768 MiB available. | Retaining another 16 MiB can worsen admission and bring forward relief. Physical RAM is not interchangeable with free process address space. |

At frame 22722 the stream is empty for 249.425 ms; only four commands arrive at
the end. Recorded, published and replayed positions agree at entry, no queue
wait flags are observed, and the measured Lock API delta is 7.7 microseconds.
This does not establish which producer routine stalled, or prove a relation to
our cache capacities. A larger queue cannot manufacture missing commands.

Memory relief is a separate directly attributed cost: 39.025 and 35.651 ms at
frames 4284 and 5671, including 21.176 and 20.186 ms in material cap transitions.
The previous private source already clears mandatory source pools before
lowering their limit, avoiding shrink-then-clear work. Its new subphase fields
still need a real runtime log before assigning the remaining cost to a specific
pool. Released cache charges are not guaranteed immediately reusable VA.

A future terrain-capacity comparison must measure exact geometry/coverage,
misses, evictions, capture CPU, actual available/contiguous VA, geometry
admission and complete frame intervals together. An isolated 48 MiB cache win
does not establish smoother full-scene rotation. No cache limit was changed.

## Three parallel investigations, five directions each

| Owner | Five directions | Adopted scope |
| --- | --- | --- |
| Stream / scene replay | Generated command schema; established D3D9 tracing; WorldRenderer context fidelity; native-only versus producer throughput; bounded snapshot transport. | Exact snapshot archive and strict imported reader; immutable private trace/sidecar preparation. No raw pointer queue dump or fabricated renderer activation. |
| Shadows / GI | Stock shader math; current-frame native pass visibility; source membership and dedup; angle/pose coverage; bounded observation. | Structural proofs for nine stock shader-model/deformation programs, cached at creation, and bounded pre-capture observation of nine directly addressed constant rows. No capture/projection gate changes. |
| Materials / lighting | Compact fixture stacking; native/particle ownership; source power/radius math; equipment upload coverage; actual-MPQ fixture receivers. | Offline compact-fixture correction and independent asset-derived receiver checks. No wider proximity suppression or guessed equipment asset. |

Detailed English audits and immutable private inputs are under
`northlight-diagnostics/rotation-scene-followup-20261024/{stream,shadows,lighting}`.

## Implemented changes

- `NLSNAP1` transports existing recorded self-read results with exact FIFO
  order, original-address lookup keys, failures, trigger/draw identifiers,
  strict bounds and checksum validation. Imported reads reject missing or
  exhausted records; the live production fallback remains unchanged.
- Native scene preparation hashes the actual trace, sidecars and selected
  DLLs, distinguishes capture layers and rejects applying Northlight twice.
  Parsing a manifest never sets full-scene readiness or FPS acceptance true.
- Native shadow registration caches a complete structural proof. Observation
  occurs before the main-view capture gates, uses authoritative known mirror
  rows without getters/readback, and preserves missing data as unknown. Limits
  are 64 observations per device session, two per frame and eight per profile.
  The relative bone palette and native world-space provenance remain unproved.
- A bounded dynamic-light ledger joins published source IDs to reordered
  selected slots, exact position/colour/fog constants, direct and fog batch
  rectangles, and actual draw submission results. It runs on at most 32 regular
  diagnostic frames at 127-frame intervals, plus explicit capture frames.
  Diagnostics and RenderProfile must both be enabled. Pixel contribution and
  upstream particle-cluster identity are explicitly unmeasured. A normal debug
  view that skips direct/fog passes leaves those stages marked not-reached.
- The last unpaired eight-light batch no longer forms an out-of-range reference
  to a nonexistent neighbouring rectangle at the 136-slot capacity. This is a
  bounds fix; it does not prove the cause of disappearing light at lower limits.
- Four complete compact authored fixtures become one light each: 20 sources
  become four; the nine multi-emitter profiles become 27 sources instead of 43.
  Every pair must be within three model-local yards and share a shadow policy.
  Other native lights, larger fixtures and separate instances retain their
  existing behavior. This deliberately corrects stacked illumination; it is
  not pixel-equivalent to the previous overbright result. Regenerated lighting
  outputs are required; a DLL replacement alone does not apply this asset fix.

## Validation and remaining work

Nine selected changed-path scripts pass: four offline lighting/MPQ checks,
strict snapshot archive and existing snapshot regression, native-shadow
observation, per-source submission/bounds, and existing light batches. Native
checks include O2/O0 and ASan/UBSan where supported by each fixture. Shadow
controls cover all nine authentic programs, nine main-view rejects, 924
truncations, 906 token mutations and four incorrect operand-order variants.
The original unpaired-rectangle expression is rejected independently. The
asset stand checks four fixtures, eight inn scenes and 3,500 authored receivers.
Layout and discovery pass; discovery executes no tests.

Failed attempts are preserved: sandbox process enumeration, a below-Northlight
planner branch that attempted to copy an absent Northlight DLL, compiler
indentation diagnostics, and a strict matcher that initially required SM2 MUL
operand order for SM3. The planner now stages the backend for baked traces;
the matcher requires the exact operand order for each shader model. No warnings,
negative controls, image requirements or input proofs were relaxed.

No full suite, old 69-test list, game, GPU benchmark or whole-scene FPS
measurement is run in this slice. A separate build identity is recorded in the
private delivery manifest after integration.

The archive/preparation bridge is not yet a faithful, activated full-scene
replayer. It still needs actual ordered D3D trace data, verified context/static
proof inputs and resource/draw identity joins. A trace cannot reproduce the
original client's missing CPU preparation merely by inserting recorded waits.
No 120 FPS result, elimination of rotation stalls, or complete resolution of
the reported shadow/equipment artifacts is claimed.


## Private build

Renderer source commit `a2512934097647317dee0a2a3cf82f28934eeea7` builds successfully with
Zig 0.15.2; BUILD identity is `d2c187721d5b7a0396a9da728edb37d9dfab5754a4101131179462c92c751458`. The DLL is
5,751,808 bytes, SHA-256
`02c10596adb55d62d2ef816ccf38bcb3453ba83500f2c0b942d09a7ed09cdf59`. PE/PDB GUID and age match without
loading the renderer. Production sources remain byte-identical through the
build; the two pre-existing displacement indentation warnings remain.

All nine selected checks are bound to their unchanged relevant source inputs;
protected installed/configuration inputs match their initial hashes. No game,
renderer DLL, optimizer or replay was executed. No source was published.
The separate manual package includes the DLL, PDB, this report and manifests.
The dynamic-light ledger is emitted after successful composite; aborted frames
retain the existing immediate HRESULT error log instead of a completed ledger.
