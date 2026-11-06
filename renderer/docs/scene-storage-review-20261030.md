# Bounded scene storage review

This PR is ready for code review as an independent CPU scene-storage change against main. It does not depend on the private integration branch or another open feature PR.

## Contract

Replace large contiguous geometry allocations with bounded storage and transactional assembly. Successful geometry, indices, UVs, materials and publication remain unchanged. The existing process reserve and raised failure floors remain in force; invalid inputs and allocation failures keep the prior publication or defer safely.

## Evidence and review limits

The earlier standalone x86 build, frozen assembly comparisons, allocator-fault matrices and worker-entry tests remain the implementation evidence. Fresh functional runs cover exact mesh pages and fake-backend upload/rollback with ASan/UBSan, memory admission boundaries in optimized and sanitizer builds, and probe lifecycle/full-atlas publication. The probe tests check both 16/24 atlas layouts and 2,285,440 exact differential slots without timing; the optional equivalence microbenchmark requires an explicit --benchmark argument, keeping correctness runs usable while a client cache is being generated.

The 395/434 MiB worker-entry checks establish admission behavior only. This PR does not claim end-to-end cold GPU recovery at those thresholds or whole-game FPS acceptance. The actual-driver cold/warm matrix belongs to #98, which has additional recovery architecture. Driver allocations and hardware/game acceptance remain explicit review and integration follow-ups rather than inherited test claims. No live game or actively generating cache is used for this update.

The standalone storage foundation is complete for review. Full renderer recovery should be evaluated with the separately tested integration candidate; merging this foundation alone is not a promise that every effect will remain available under arbitrary address-space pressure.
