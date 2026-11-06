# Rotation stalls: coalescing required world-cache retirement

Private follow-up to [angle corrections](rotation-angle-corrections-20261024.md).
The closed user session ended October 24 at 11:41:11. Its preserved log is
11,757,128 bytes, SHA-256
`648056d0c12fd7dc7aa28bf844518c87c0fffa7c0b5b204f95a8b850c5c22bb5`.
It measures the installed a6d6e9f source build, before this boundary correction.
The game and optimizer are not launched or modified. Nothing is published.

## What still costs time

The full closed-session log contains 518 idle-gap records and 618 retained slow
frames. Sixty-three same-frame gaps join focused, applied slow-frame records.
Frame 23355 has a 212.1473 ms empty-queue gap and 203.125 ms producer-thread CPU;
frame 21795 has a 210.8705 ms gap and 218.75 ms CPU. Their effects stages take
1.065 and 0.300 ms, with GetData API time only 0.018 and 0.009 ms respectively.
The CPU counter has coarse accounting granularity and can exceed wall time;
these values do not identify a particular game routine or an exact percentage.
They show substantial producer CPU while the replay queue has no commands.
There is no exact rotation-event join or current native-only comparison, so
these observations are not proof of a particular input handler or optimizer defect.

Frame 9394 separately attributes 42.291 ms to memory relief: world-cap work
8.927 ms, material-cap work 20.218 ms and late trim 13.146 ms. The world trim
includes 7.377 ms terrain and 5.428 ms snapshot retirement. Removing redundant
world eviction is worthwhile, but cannot explain away the producer CPU gaps.
Cache enlargement is not a demonstrated cure.

The following table retains earlier-run stage observations for context; they
are not new measurements of this correction.

There is also directly attributed Northlight work worth removing:

| Stage | Existing runtime evidence | Scope of this change |
| --- | --- | --- |
| Memory relief | Frame 4284: 39.025 ms total, including 5.770 ms world-cap changes, 21.176 ms material-cap changes and 12.079 ms subsequent trimming. Frame 5671 totals 35.651 ms. | World cap enforcement still precedes a mandatory full world-cache clear. Avoid that redundant LRU traversal. Material clear-before-cap behavior was already corrected separately. |
| Terrain capture | Frame 18415: 8.349 ms, 571 misses and 573 byte-budget evictions. | No cap increase or geometry change. A genuine working-set comparison still needs native keys, updates and revisit order. |
| Material CPU preparation | The final 97-sample configuration reports approximately 3.55 ms exclusive sampled CPU, mainly pose/envelope/setup work. | No stale-pose substitution, reduced range or quality change. |
| GPU critical path | The final configuration has a 10.781 ms median GPU frame, above the 8.333 ms budget for 120 FPS. | No claim that this CPU boundary repair achieves the overall target. |
| Retained material atlases | MHEIGHT reports only twelve full uploads by approximately frame 1200, unchanged through the end. | No evidence supports a new lazy atlas cache as a remedy for this run. Rectangle/copy costs remain separately unmeasured. |

The five-topic audits are retained under the private diagnostic directory:
`lighting/off-mode-atlas-cost-audit-c568dea.md`,
`lighting/northlight-cpu-lua-boundary-audit.md`,
`native-chain/optimizer-native-gap-followup.md`, and the coordinator's
world/pressure review. Source behavior and historical stage times identify
the candidate; they do not establish its achievable timing reduction.

## Boundary contract

Only a boundary that already requires a full memory trim may clear the world
caches early. It must execute after completed-frame cleanup and before
lowering their limits. Its trim result and elapsed time are carried to the
remaining renderer trim, which must not clear the world a second time.

Material pressure must still precede material trimming: displacement's trim
uses its current pressure flag. Moving the entire renderer trim before all
policy updates would use the old flag and could reintroduce an unnecessary
OS memory query and different budget maintenance. Replay-copy and stream
policy updates, completion acknowledgement and the after-trim sample retain
their required ordering. Cap-only and recovery boundaries keep their existing
behavior. No draw, source admission or resource upload is introduced between
world retirement and cap enforcement.

The world still quiesces preparation before clearing state/caches. GPU COM
retirement remains on its owner thread, active and staged mesh pages remain
excluded, and escaped immutable snapshots retain valid payloads and live-byte
charges. Lower entry/byte limits, reserve thresholds, cooldown, hysteresis,
terrain pressure distinctions, actor geometry and MSAA remain unchanged.
Reported freed bytes remain estimates; clearing a pool does not imply that
an escaped owner's payload has been physically released.

## Stand fidelity evidence

The two preserved authored transport binaries now both pass their unchanged
six-frame Reset source sequence in matched fresh environments, including
the older binary that previously produced a black image. Consequently the
new optional pre-read branch is not established as the cause of success.
Earlier failures remain preserved. Evidence:
`native-chain/reset-followup/frozen-baseline-pair-1/validation.json`.

The exact original AA0 smoke executable, without recompilation, extra reads,
events, resizing or diagnostic arguments, also passes its six strict source
RGB/coverage/query frames through the pinned wrapper/backend. Evidence:
`native-chain/reset-followup/original-frozen-source-aa0-1/validation.json`.
This qualifies that specific source execution, not the original complete
capture/retrace chain matrix, raw-depth parity or same-scene performance.

No genuine game draw/resource trace or current animated actor/equipment
journal was found in the available corpus. The actual MPQ terrain/static
geometry and recorded camera observations are useful stage inputs, but do
not reconstruct native Lua/client work, dynamic visibility, skin palettes,
all original material bindings or depth/MSAA/HDR transitions. Authored
transport throughput is never reported as game FPS. The uncapped original
scene's static/rotation/native-only comparison and stable 120 FPS remain open.

## Implementation acceptance

The coalesced boundary is implemented and retained after the upstream merge.
Focused memory-guard, terrain-pressure-policy and world-pressure-transition
checks pass, including O0/O2, sanitizer coverage, ownership/limits checks and
an exact old-order negative control. The merged-source repetitions pass too.

Three alternating component pairs, with five populated boundaries per sample,
measured median old/new 0.378240/0.219899 ms (about 0.158 ms or 42% of that
component). Fitting-cache, empty-cache, cap-only and recovery medians were
0.008133/0.009357, 0.003795/0.004177, 0.014002/0.013813 and
0.008586/0.009023 ms respectively. These are authored cache workloads, not
whole-frame timings and not a promise to recover the runtime's 8.927 ms.

Evidence: `world-pressure-final-3/validation.json`, the merged CPU check reports,
and `runtime-user-a6d6e9f-final/producer-summary.json` under the private
`rotation-scene-followup-20261024` diagnostic root. No full suite or game
was run. Stable rotation FPS and attribution of the remaining producer CPU
stalls remain open.
