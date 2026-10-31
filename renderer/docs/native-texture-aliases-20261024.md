# Native texture identities and cache capacity follow-up

The next private slice adds [strict observed client-code correspondence](native-code-correspondence-20261024.md)
and preserves the separate Reset/image and original-scene FPS requirements.

The game log remains the October 24, 03:28:59 recording: SHA-256
`b71ba562267c13188f24219df83bd71bdf7273e9bd914210833227343d886443`.
Its installed renderer is the older `e15cc6d` build, not the private work
described here. No game was launched, no installed DLL, MPQ or personal
setting was changed, and no FPS uplift or 120 FPS result is claimed.

## Cache decision

Increasing all caches is not justified. A private terrain retention comparison
of **32 versus 48 MiB** is worth measuring: frame 18415 has 571 misses,
573 byte-budget evictions and 8.349 ms capture cost at the 32 MiB limit.
Other misses occur below capacity, so they cannot all be removed by a larger
budget. The log lacks per-key eviction-to-revisit attribution.

Generated normals also churn earlier as their adaptive allowance falls from
96 to 32 MiB. However, frames 27000–28200 add **279,874 normal hits with no
new builds or evictions**. Displacement adds 1,200 hits with no new builds in
the corresponding late interval. More capacity cannot remove those already
absent builds. Warm material capture, current-pose work and view-dependent
AO still have to execute.

Memory is a constraint: approximately 685–702 MiB of virtual address space
is free in this 32-bit process, below the 768 MiB geometry entry threshold.
More retained data can worsen admission or pressure cleanup. The 249.425 ms
empty producer-queue gap is not explained by cache capacity. Keep all
pressure floors, invalidation rules, image quality and personal settings.
No capacity default was changed and no capacity A/B result exists yet.

The three private cache audits cover five directions each:
`materials/cache-capacity-followup.md`, `shadows/cache-capacity-followup.md`
and `native-chain/cache-memory-followup.md`, under
`northlight-diagnostics/rotation-scene-followup-20261024`.

## Implemented replay changes

Imported snapshots now have a private, bounded table of up to ten typed
texture aliases. At the exact joined marker, the host resolves source
identities through the official retracer object map and checks the current
canonical resource and lifetime. The table seals once and is copied into
the queue-owned snapshot after recording finishes. Worker lookup uses that
snapshot's table and the registry generation, so a later producer snapshot
cannot replace aliases needed by an earlier queued Draw.

There are no extra COM references, cross-thread Release calls, serialized
host pointers or global pointer substitutions. Original resource commands
keep their ordering. These fields, lookups and generation counters are
compiled only into the stand; normal rendering has no alias-table cost.

Explicit creation after a recorded final Release establishes a new source
lifetime, even if the allocator returns the same numeric address. Creation
at a live address still fails. Getters cannot revive dead entries or old
interface aliases. Implicit getter/QueryInterface address revival remains
unsupported rather than guessed.

`DrawIndexedPrimitiveUP::pIndexData` contains copied index bytes, unlike
`SetIndices::pIndexData`, which identifies a COM object. Admission now follows
the actual API domain; malformed SetIndices input is still rejected.

The stand resolves configuration, backend and logs relative to its **own
module**. The official retracer virtualizes the original executable name;
using that name could otherwise select the original game directory.
Unavailable module paths fail without file logging or configuration reads.
Missing backends and unsupported capabilities return failures without modal
dialogs in the stand. Normal game path and dialog behavior are preserved.

## Focused validation

The queue/alias CPU acceptance passed 28 cases in each of O2, O0 and
ASan/UBSan: **84 case executions**. It checks producer-ahead ordering,
retired producer ownership, Reset ordering, address generation reuse,
missing/dead/wrong-family entries, bounds, sealing and failure handling.
Some rejection cases run before device construction; these are not 84
rendering scenarios. The changed plan collector/order/bounds checks also
passed **34 cases**. Actual source and raw client dependencies remained
unchanged during these runs.

The authored producer successfully recorded three frames, twelve Draw calls,
nonzero texture identities, real EVENT/OCCLUSION queries and strict RGB
controls. Initial replay failures are preserved: the virtualized executable
directory selected a missing backend and timed out; a later attempt exposed
the index-byte admission error; the next reached legitimate readback-surface
address reuse. None was accepted as a passing replay or hidden by relaxing
the image comparison.

The final private host and stand **pass** replay of that exact frozen source
recording: all three RGB images match byte for byte, all twelve Draw calls
complete, all three joined packets carry actual nonzero texture aliases, and
EVENT/OCCLUSION output checks pass. The bridge observes 98,192 retraced
GetData calls and 91,908 cases where replay is already ready while the source
was not; these are diagnostic callback counts, not additional GPU passes or
game FPS measurements. The complete original capture directory remains
unchanged. Logs and backend loading stay inside the new stand directory.

The actual retracer object-map CPU fixture passes **11 cases**, including
explicit dead-address recreation, live recreation refusal, dead getter and
stale interface refusal, copied index bytes and invalid SetIndices inputs.
All **25** selected corrupted-plan/dispatch controls reject for their exact
expected reason. They use a separate relocated stand, leaving the positive
evidence unchanged. No full suite was run.

Both normal and stand builds succeed from renderer source identity
`75fe11a25f1e40f9c79c66225c09415c536b97327578391841663c841cf51745`.
Their source guards are unchanged. Private evidence is in
`native-chain/positive-native-transport-acceptance-4`,
`native-chain/negative-native-alias-acceptance-1`,
`native-chain/plan-alias-checks-final` and
`native-chain/stand-alias-acceptance-20261024-082738` under the diagnostic
directory above. All authored results remain explicitly ineligible for FPS.

Exact PE/PDB GUID and age checks pass for both artifacts. The normal DLL
passes the package guard; the stand is correctly refused from a player
package. A separate normal DLL/PDB package is staged at
`northlight-builds/northlight-native-alias-75fe11a-20261024` and remains
uninstalled. Protected installed DLLs, optimizer, logs and settings retain
their original hashes. The acceptance manifest is
`native-alias-acceptance.json` in the diagnostic directory.

## Remaining fidelity requirements

The authored transport is explicitly flagged and requires a separate opt-in;
it is never FPS eligible. It does not reproduce the original MPQ scene,
animated poses, camera path, producer visibility/decompression work, raw
depth or MSAA quality. A genuine original-scene trace and coherent dynamic
inputs are still required for the static/rotation and native/Northlight FPS
comparison. File hashes alone also do not establish that live patched client
code equals the immutable PE used by the imported provider. The original
six-frame Reset/image gate remains a separate unresolved check.
