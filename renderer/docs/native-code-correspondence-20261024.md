# Native replay input correspondence and cache decision

This is private implementation evidence, not an original-scene performance
result. The game was not launched. Installed DLLs, optimizer, logs, MPQs,
world-cache and personal settings were not changed. Nothing was published.

## Cache capacity

The latest available log still ends at October 24, 03:28:59, SHA-256
`b71ba562267c13188f24219df83bd71bdf7273e9bd914210833227343d886443`.
It contains the older installed renderer, not these private builds.

| Cache / phase | Actual log evidence | Decision |
| --- | --- | --- |
| Terrain CPU snapshots | Frame 18415: 571 misses, 573 byte-budget evictions, zero count-budget pressure, 33,553,912 of 33,554,432 bytes retained; capture 8.349 ms. Lines 34888–34897. | A private 32/48 MiB comparison is justified. Per-key eviction-to-revisit attribution is still missing; a full cache alone does not prove a miss is avoidable. |
| Generated normals | Lines 50478–52848 add 279,874 hits while builds and evictions remain 7,865 and 7,606. | More storage cannot remove already-absent builds in this warm interval. |
| Displacement | Lines 50475–52845 add 1,200 hits, with 2,985 builds unchanged. | No evidence of capacity as the late-interval bottleneck. |
| Process memory | Line 51560: 685 MiB available virtual address space, 438 MiB largest block. Line 53084: geometry admission refuses below its 768 MiB free-space floor. | Raising all caps could worsen admission. Preserve pressure trims, floors and source lifetimes. |
| Empty producer queue | Line 42538: 249.4252 ms wait, queue empty at entry, record/publish/replay equal, only four commands arriving by exit. Sync, Present, backpressure and snapshot deltas are zero. | Cache capacity does not identify the application routine consuming this interval. Do not attribute it to decoding or shaders without CPU samples. |

Cache defaults remain unchanged. A useful terrain experiment must preserve
the ordered scene, source bytes/revisions, camera motion and quality; measure
actual revisits, exact outputs, complete frame costs, peak memory and admission
failures. A cache-only microbenchmark cannot establish original-game FPS.
The earlier [cache and alias report](native-texture-aliases-20261024.md)
contains the previous transport acceptance.

## Why raw executable bytes were insufficient

Readonly PE sections are not necessarily identical to loaded memory. The
loader updates imports, and runtime components can patch instructions. The
actual client's import directory lies inside readonly `.rdata`. Importing
the raw file without checking used spans could select a different signature
path in the stand and invalidate image or performance comparisons.

An explicitly requested diagnostic capture now observes bytes already returned
by the real `readSelfLive` call. It does not perform another process read or
change the original result. The observer installs before Northlight device
and world-renderer construction. Any earlier read prefix, concurrent observer
contention, failed static read, changed duplicate, overflow or mismatch makes
the correspondence incomplete. There is no patch overlay or raw-file fallback
after proof admission.

The fixed collector admits at most 128 exact spans, 512 bytes per span and
64 KiB of payload. Owner-side finalization writes a checksummed `NLCODE1`
packet and closed status with exclusive creation and no-clobber publication.
It reserves metadata space within the existing capture byte cap. The hot
callback performs no allocation, file IO or blocking lock acquisition.
Without diagnostic opt-in there is no session, writer or capture file IO;
normal self reads gain one atomic null-callback check. No zero-overhead claim
is made.

The plan compiler requires closed correspondence for genuine captures. The
private host independently verifies the proof against the immutable client
file before factory execution, then admits it through
`NorthlightStandCodeProof` before snapshots. Imported static reads require an
exact observed address/length pair. Repeated, late or corrupt admission
invalidates the context. Explicitly authored transport fixtures may retain
their old raw-file path and remain excluded from scene/FPS acceptance.

## Focused evidence

Evidence paths below are relative to
`northlight-diagnostics/rotation-scene-followup-20261024/` outside this repo.

- `native-chain/plan-code-proof-checks-2/validation.json`: 45 plan/format
  checks pass; the earlier missing-import attempt is retained.
- `native-chain/code-proof-cpu-acceptance-1/validation.json`: collector
  and provider checks pass at O2, O0 and ASan/UBSan. The provider executes
  24 modes in each build; the duplicate-patch mutant fails as intended.
- `native-chain/observer-windows-2/validation.json`: four actual Windows
  RPM/lifecycle cases pass in separate owned helper processes: no opt-in,
  refused prior read, matching duplicate and changed duplicate. A reversible
  write touches only the helper's own readonly data; the patched bytes still
  reach the source caller, restoration is verified, and no proof is published.
  The helper creates no graphics device and never executes imported Wow code.
- `native-chain/observer-windows-1/`: preserves the unsupported GNU linker
  option failure. The corrected fixture sets and verifies LAA/no-ASLR flags
  only on its own PE and checks every child's actual loaded base.
- `native-chain/code-proof-dll-api-1/validation.json`: four new modes pass
  through the actual freshly built DLL: valid, repeated, corrupt and late
  after a snapshot. This CPU helper creates no graphics device.
- `native-chain/code-proof-host-preflight-1/validation.json`: five exact
  refusals pass in the actual compiled host: missing required proof flag,
  missing tail, bad CRC, raw-file mismatch and nonzero prior-read prefix.
  An independently nonexistent trace argument prevents factory/device
  execution if an intended guard regresses; that safety refusal never counts
  as a passing intended check. Negative proofs are authored fixture data.
- `native-chain/reset-followup/matched-client-size-2/validation.json`:
  matched controls complete with identical binaries/configuration and unchanged
  inputs, but both fail the strict post-Reset swapchain RGB oracle. The resized
  control now proves the actual client is exactly 112 by 96; Windows' observed
  136-pixel outer minimum is overridden only during the owned fixture resize.
  Correct client sizing alone does not repair the black swapchain readback.
  The earlier pre-Reset size failure remains preserved in `matched-client-size-1`.

Final DLL/API, host preflight, PE/PDB and package results are recorded in
`native-code-acceptance.json`. Only changed-path checks are run.

Both renderer variants compile with source identity
`6545c6cc38b1457d7b61d94e68a6b2761e7d8a87d6a49e12b2f19a44074c2a2d`.
Exact PE/PDB matches and the normal package guard pass; the dedicated stand
is refused by the player package guard. The separate normal package is
`northlight-builds/northlight-native-code-6545c6c-20261024`, not installed.
Its DLL is 5,804,032 bytes, SHA-256
`dfe284375b17d09b39a056029d714a1b7fafd08a5e4c9dd27e8e8eb6a4fba333`.

## Remaining acceptance limits

No genuine native scene trace, current actor/bone state or complete runtime
input journal was found in the available capture corpus. The 1,660 staged
static world-cache files are useful inputs, but do not reconstruct the
original dynamic scene, native draw/update order or CPU producer workload.
The original six-frame Reset, raw-depth and MSAA image gates remain separate
requirements; a correct offscreen target is not a replacement for a failed
swapchain oracle. Reset window-sizing controls retain failed attempts and
exact size/RGB assertions.

This proof covers observed self-read spans only. Direct CVar reads, effective
runtime settings and complete scene timing still require correspondence.
The 249.4252 ms producer gap remains unattributed. Original-scene uncapped
static/rotation/native-vs-Northlight comparisons, visual issue acceptance and
stable 120 FPS remain open. No uplift percentage is claimed.
