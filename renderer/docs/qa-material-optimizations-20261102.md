# Material optimization from the 2 November QA capture

## Implementation ledger: 60 / 60

The user requested all 60 changes before the next general test/build. The first
four received preliminary checks before that instruction; changes 5 onward
await the final validation batch. The already-running preliminary DLL build
finished before it could be cancelled. That DLL is not the final deliverable.

1. Resolve single-bone control DEF without copying the API constant bank.
2. Group single-bone envelopes by the actually used index lane.
3. Upload persisted normal maps without a full state/VS-bank round trip.
4. Refuse protected normal-cache misses before disk-source capture.
5. Replace atomic weak promotions in snapshot/GPU residency lookups with
   address plus live control-block proofs, reusing one shared helper.
6. Copy only live declaration elements when caching decoded layouts.
7. Borrow uploaded height statistics rather than copying their RGBA image.
8. Charge pending/completed disk jobs for their actual map dimensions/storage.
9. Pass the already-read source revision into disk lookup.
10. Retain immutable normal-map dimensions and reuse source mip-count metadata.
11. Decode source-envelope inputs without clearing all 16 unused registers.
12. Remove the duplicate input vector from prepared vertex bindings.
13. Validate each referenced binding stream's byte extent once per lookup.
14. Reserve exact binding capacity before filling immutable metadata.
15. Store shader/declaration metadata directly in hash nodes, removing its
    separate Entry heap allocation and pointer chase.
16. Reuse the scene-source lookup stamp/lock on a cold get instead of repeating
    the recursive lookup and filesystem query.
17. Remove the second revision-list reconciliation before assigning a scene ID.
18. Reject local-light cache candidates in Y/Z slabs before sphere arithmetic.
19. Return immediately for a zero local-light selection limit.
20. Remember the current worst local-light pick; rescan it only when replaced.
21. Skip unchanged point-transmission sampler setters/restores.
22. Skip unchanged POM texture and shader bindings/restores.
23. Cache the last immutable POM pixel-shader entry across support checks/draws.
24. Skip identical POM constant uploads/restores by exact byte comparison.
25. Bound BC edge loops once per block instead of checking every texel.
26. Load BC3's packed alpha selectors with one bounded eight-byte copy.
27. Sum BC2 alpha in byte pairs with a constexpr nibble-sum table.
28. Decode contiguous BGRA/BGRX mip rows as one flat SIMD span.
29. Initialize the sRGB conversion table only when sRGB sampling uses it.
30. Reuse the located texture-cache slot directly after owner replacement.
31. Query immutable mip counts once per resource in memory/art mip walks.
32. Allocate terrain retirement metadata only when there are ranges to retire.
33. Reject impossible terrain free-range requests before walking fragments.
34. Search large point-shadow history sets by sorted IDs; keep small sets linear.
35. Reject a point-shadow candidate worse than the retained tail before insertion.
36. Short-circuit a zero-limit point-shadow tracker update, preserving its clock.
37. Prepare the desired point-shadow ID set once for resident membership tests.
38. Stop scanning point-shadow residents at the first retained-ID match.
39. Finish existence-only point-shadow prediction before history/scoring work.
40. Do not copy captured projection registers that are immediately overwritten.
41. Skip replay packet compaction when every packet remains selected.
42. Collect unbounded shadow-policy counts in one pass without budget selection.
43. Reserve actor triangle output for an accepted packet before appending it.
44. Skip metadata work for successful self-copies of whole textures/surfaces.
45. Reuse proven surface descriptors when propagating a full mip copy.
46. SIMD-reduce vertical BGRA/BGRX boxes four destination pixels at a time.
47. Skip redundant opaque-alpha sums/divisions in scalar material reduction.
48. Use each BC format's alpha rule directly in complete-block sums.
49. Skip cube-plane preparation when static point-shadow records are empty.
50. Validate the directional signature matrix once for all records.
51. Skip known-zero terms in prepared cube clipping planes/overflow bounds.
52. Share AABB absolute extents across affine shadow clipping axes.
53. Skip bulk light-ID forgetting when the sorted ID ranges are disjoint.
54. Zero only the inactive tail of partial direct-light constant batches.
55. Copy contiguous fog/light batch columns in bulk.
56. Use the exact geometric series for square power-of-two normal-map mip bytes.
57. Dispatch probe-atlas modulus specialization once per atlas export.
58. Merge sorted attachment/light IDs instead of repeated tree/binary lookups.
59. Reuse current-call attachment bone transforms for repeated reference/source bones.
60. Validate shared-POM stream extents once before expanding triangle corners.

## Inputs

The newest log in `qa_report` is `northlight-renderer.log`, modified
2026-11-02 21:35:54 (local time). SHA-256:
`ef2be5fc4edc8e7bb1ef74e92b3c3a81b787a667921c557543231bb9b6c0d487`.
It identifies renderer 0.3.216, source
`e4087f41df808393e9bd9a4cad3fc8df7b079937accca3639ee872e6ab5f60ad`.

The latest complete lighting export is
`northlight-diagnostics/session-12260-29051343-0/capture-2-*`, modified
21:33:15: renderer frame 3693, world frame 3416, ArgentTournamentDungeon,
1920x1080, MSAA 8. The log and export agree. The later session
`session-12260-29117828-0` contains only an unavailable weather export
(`no-active-weather`) for the same renderer frame.

RenderProfile and Diagnostics are disabled. This input supplies work counters
and saved products, not CPU/GPU stage timings or an original draw-stream replay.
The diagnostic frames explicitly exclude themselves from FPS measurements.

## Findings and changes

At frame 11400 the log reports 83,253,760 fallback pose vertices, including
8,044 single-bone vertices out of the sampled frame's 8,192 fallback vertices.
The envelope cache reports 623 time deferrals and zero cold visits in that
sample: the 2,048-entry count alone does not prove capacity caused the deferrals.

1. Single-bone envelopes no longer copy the entire 4 KiB API constant bank
   just to override the shader's c0 literal. The existing single-bone control
   resolver applies that literal directly. Palette DEF overrides retain the
   copied effective bank; the generic fallback still resolves unapplied DEFs.
2. The audited single-bone program reads only the X component of its blend
   index. Its source groups now use X as their key. Unused YZW no longer create
   duplicate bone groups or exhaust the 512-group limit. Weighted programs
   retain all four index components. Merging boxes can make bounds more
   conservative; all-vertex enclosure, current poses and failure-open behavior
   are preserved.

The same log reports 3,810 normal-map evictions, 3,718 disk uploads and only
157 generated maps. Those uploads previously captured/restored all D3D state
and the VS bank despite executing no state-changing draw.

3. Ready disk maps now upload and generate mipmaps without capturing device
   bindings or reading/writing VS constants. Actual normal generation retains
   its complete state restoration.
4. When the normal cache cannot replace any protected visible entry, it now
   refuses admission before reading/snapshotting/hashing a disk source. It
   retains the existing memory budgets, two-build limit and visibility hold.

The capture already skips fog and the invisible second local-light batch.
Material height occupies 18.14% of pixels; material occlusion occupies 17.69%.
These coverage figures do not establish a profitable new GPU pass. No shader,
sample count, render distance, quality default or cache budget was changed.

## Validation

- `test_material_envelope_fast.py`: O2/O0; 256 posed enclosure checks per
  configuration with 768 vertices and 768 distinct full index tuples collapse
  to four used bones. They fit the existing group cap, spend 24 deformation
  operations per warm evaluation and copy zero control-bank bytes. Palette
  overrides still copy the bank. The independent oracle deforms every vertex.
- Existing single-skin interval tests: 25,000 cases per configuration, exact
  values/errors against the interpreter, including numeric failure paths.
- Pose metadata/reuse, displacement state, constant epoch, palette override
  and pressure transition tests pass. Metadata checks also pass ASan/UBSan.
- Native D3D9 and DXVK 3.1.1: disk restart/corruption/settings/retry/reset tests
  across eight source formats; generated/uploaded pixels and five mip samples
  match exactly. Disk hits perform zero state-target reads and zero VS-bank
  reads/writes. One hundred protected-cache misses produce zero disk snapshots.
- Normal reuse tests on both backends: 180 exact maps; leases, bound targets,
  overflow, resize, failed draws and reset remain safe; the visible set has
  zero admission transitions over its stability interval.

These are standalone regression checks, not a live gameplay FPS comparison.
The game and installed client were not changed by the tests.

## Final acceptance

The full runner discovered 481 tests: 256 were executable in this environment,
225 were skipped for missing host dependencies or explicit manual prerequisites.
The first run had 246 passes and ten failures. Eight were process-presence guards
because WoW was already running; the user explicitly authorized running the GPU
stands beside it. All eight passed in isolated reruns, with only those guards
bypassed in the test process's memory. The committed guard files are unchanged.

The other two failures caught an outdated disk-adapter test stub and a real
ownership regression: separately owned live shared pointers to the same mesh
must retain the original cache behavior. The rare weak-promotion fallback was
restored while retaining the ordinary control-block fast path. All 18 selected
regressions passed afterward. The additional local-light batching acceptance
also caught stale cost-model constants; they now match the existing compiled
shader manifest (293 / 405 / 397 slots), without changing any shader.

Final result for the 256 available suite entries, including those reruns:
**256 PASS, 0 FAIL, 0 TIMEOUT**. The 225 original prerequisite/manual skips are
listed separately in the machine-readable report, rather than counted as passes.
Additional portable bounds, envelope, skin-safety and light-selection checks ran
through Zig's Win32 Clang frontend with assertions and ASan/UBSan. Their temporary
POSIX stubs/paths were adapted to the Windows ABI; production sources and numeric
assertions were unchanged. Both native D3D9 and DXVK 3 passed the final disk-map
and 180-map reuse stands. These checks do not measure live game FPS.

Evidence:

- `.local/out/test-output/20261102-230043/results.tsv`: complete initial run.
- `.local/out/test-output/20261102-231451/results.tsv`: 18/18 focused rerun.
- `.local/out/qa-20261102-final-validation.json`: consolidated per-test results
  with explicit rerun provenance and final DLL identity.

Production DLL: `renderer/frd9.dll`, x86 PE DLL, 9,067,520 bytes, built with Zig
0.15.2 and the unchanged shader freshness gates after the general tests.
Source identity:
`2644d9a246daed47fc3aa1eacd5187dbd43a08a732121102954d6da03df7e9e2`.
DLL SHA-256:
`48304cb40630daf626059b2f2d8041bae8786d549e3b98f8e97c4558429c837b`.
The DLL was built in the repository; it was not installed into the running game.
