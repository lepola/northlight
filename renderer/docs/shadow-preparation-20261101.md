# Shadow preparation and cube reuse

Large captured crowds exhausted the fixed cheap-bound budget, leaving many
draws unbounded and replayed into unrelated cascades and cube faces. Give the
cheap phase two additional microseconds per packet above 256, capped at four
milliseconds total. Heavy and cold preparation keep their reserved caps.
Warm generic envelopes with at most eight groups and 128 operations share
the existing batched clock with cheap skin draws; both paths use the same
interval evaluator and shader-local constant definitions.

Point shadows share depth, scratch and shader resources between sequential
sources. Per-source completed cubes and static caches stay independent. Use
the generation-checked terrain list, skip static candidate scans when no
static face needs rebuilding, and reuse unchanged static-only completed
faces. Terrain/live/replay changes remove that reuse eligibility. Keep the
33 ms crowd refresh floor even when a smaller refresh setting is requested.

The combined source used for installer `0.3.216-test.20261101.4` has source
identity `85bcbc0845f87955d298158304e669fdc806d03d9cc6b9a8c89269e049636a48`.
Its recorded checks include 2,048 rigid fast/timed comparisons, 196,608
enclosed rigid vertices, 894,778 real skin vertices and exact asynchronous
1,222-packet crowd output. The new regressions exercise both O0 and O2,
ownership/key changes, operation/group limits, invalid inputs, clock
accounting and reserved-budget lending. Production GPU coverage checks
exercise terrain-list fallback, static reuse, shared allocation and changes
to live/replay input; combined proxy validation covers 960 SDR/HDR cases
across DXVK 2 and 3 including Reset/resize.

These are component checks. They do not establish Stormwind FPS or fresh
in-game acceptance. PCSS filtering is a separate change; its GPU costs and
limits are described in `pcss-filter-extension-20261101.md`.
