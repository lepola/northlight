# Native snapshot capture and private replay bridge

Subsequent typed-alias implementation, actual transport acceptance and cache
audit: [Native texture aliases](native-texture-aliases-20261024.md).

The runtime log is unchanged: 10,192,799 bytes, last write October 24 at
03:28:59 local time, SHA-256
`b71ba562267c13188f24219df83bd71bdf7273e9bd914210833227343d886443`.
It still identifies the older installed build. The work below has not run in
the game. No stable 120 FPS result, scene parity or FPS uplift is claimed.

## Cache recommendation

Increasing every cache is not supported by this log. A private terrain-cache
comparison of 32 versus 48 MiB is justified: frame 18415 reached the 32 MiB
limit, with 571 misses, 573 byte-budget evictions and 8.349 ms capture cost.
However, other cold misses occurred without eviction; frame 762 had 137
misses, no eviction and only 9.59 MB resident. Capacity cannot remove all
first-use work.

The later normals interval added 279,874 hits without generation or eviction;
displacement added 1,200 hits without builds. Larger caches are unlikely to
help those intervals. The texture eviction count also cannot be equated with
readback cost: only five readbacks were explicitly attributed to re-locking
an evicted texture.

There was also a 249.425 ms producer gap with an empty command queue, no
reported queue-wait flags and only four commands. Its exact producer routine
remains unidentified. Increasing retained memory is not a demonstrated fix.
This is a 32-bit process; the logged 686–700 MiB available virtual address
space was already below the 768 MiB geometry-admission threshold. Additional
retention can therefore increase memory relief elsewhere. Defaults and the
user's settings remain unchanged; no cache-size uplift measurement exists.

## Implemented capture transport

Normal rendering constructs no capture session unless
`NORTHLIGHT_NATIVE_CAPTURE_ROOT` is explicitly set. A session additionally
requires absolute `TRACE_FILE` and `APITRACE_FORCE_MODULE_PATH` values, an
existing output parent/downstream directory, and a distinct loaded outer
`d3d9.dll` exporting `D3DPERF_SetMarker`. The export's owning module is checked
and reference-held until capture teardown. These are capture prerequisites,
not a tested game-launch recipe; no launch or installed-file replacement was
performed.

The stream's completion callback copies one bounded archive and returns its
actual encoded byte count and trailer CRC. It performs no allocation, file
I/O or waiting. A separate explicitly enabled observer forwards the canonical
`NLSNAP1` marker, including session, record, original device, producer frame,
Reset epoch, source OS thread, trigger and draw ordinal. The external recorder
may serialize during that observer; this cost is not misrepresented as a
wait-free completion callback.

A bounded writer stores the packets and journal in an exclusively created
session folder. Capture status is written to an exclusive pending file,
successfully closed, then atomically published without replacing an existing
final status. Ring/record/byte limits, missing observers, metadata mismatch,
reentrancy and I/O failures make the session incomplete. Status and host
metadata reservations remain within the total 256 MiB session budget.

The worker and failure-owner callbacks retain the session through replay
thread shutdown; the writer drains only after producer callbacks are no
longer possible. All explicit capture attempts exclude the device's frames
from ordinary `FRAME` interval FPS, including initialization failure and a
disabled command stream. Marker forwarding and clean status alone do not
prove trace association. GPU aggregates from a capture process must also be
treated as diagnostic, not compared as ordinary performance runs.

## Implemented private host and plan compiler

The distinctly named `northlight-native-retrace.exe` reuses pinned apitrace
14.0 source `18cfd388ac47a9e00978b34d6c20341f3e2cdb56`, its official parser
and generated D3D9 handlers. The original vendor tree and official binary
bundle remain unchanged. Checked preparation copies the source, installs
three integration hooks and records exact helper/source identities. No
generic dynamic plugin mechanism or parser behavior patch was introduced.

The bridge validates the selected trace, immutable PE bytes, stand DLL,
payload SHA-256 and every archive before factory dispatch. It opens the
import provider and first packet before `Direct3DCreate9`, then validates
markers and binds subsequent packets at their exact enclosing Draw. It
preserves parser dispatch order: a nested marker has a larger call ID but
appears before the parent Draw. It validates device/thread/lifetime,
producer-frame/Reset domains, total calls and final device retirement.
Inactive plans retain the stock callback table.

Stock D3D9 retrace ignores `IDirect3DQuery9::GetData`; its
`--query-handling=run` option does not enable D3D9 query polling. The bridge
implements actual EVENT/OCCLUSION calls with supported sizes/flags, actual
HRESULT/output checks and a bounded genuine completion poll when the
original returned ready. Earlier actual readiness is counted rather than
delayed artificially. This callback has compiled but has not yet passed an
integrated positive GPU transport scenario. It is not a measured fix for
the original Reset failure.

`compile_native_replay_plan.py` independently joins a closed official trace
dump to the journal, packets and capture-host identities. It verifies the
official outer-wrapper hash, actual client and trace, archive CRCs, ordering,
first Draw, final device release and exact marker metadata, then emits the
256-byte-header/80-byte-record `NLJOIN1` plan. It accepts official property
headers, rejects incomplete/duplicate calls and limits dump output to 512
MiB, stderr to 1 MiB and retained call IDs to two million. Crossing a limit
refuses the capture rather than certifying partial input.

Typed native celestial/glare texture identities are checked independently by
Python and source-derived authoritative C++ readers. Missing FIFO keys or
any nonzero terminal COM identity are refused. Address fields are never
globally rewritten. A future typed, per-snapshot resource-alias capability is
still required for ordinary scenes with those identities. Multiple devices,
Ex, unproved swap-chain Presents, object address reuse and unsupported query
types also remain rejected. The current host therefore cannot be described
as a validated complete game-scene replay.

## Focused validation and build artifacts

- Repaired packet/observer/status fixture: 25 groups each in O2, O0 and
  ASan/UBSan. Independent parsing verifies marker contents against actual
  archive headers/trailers and zlib CRC. Producer allocations remain zero.
- Plan parser/identity/order/bounds fixture: 28 checks passed. Its inputs are
  explicitly authored contract cases, not a captured game scene.
- Normal and import-only renderer variants built successfully from source
  identity `63e804d81e100d50ae5e4f6a7e4b4fdbe2b86d21177365f12ed66a49231ce95a`.
  Source guards and generated-file equivalence checks passed. Two preexisting
  material-source warnings remain.
- Six actual stand-DLL C ABI cases passed in a private Windows CPU host:
  valid import, before-open, before-snapshot, wrong thread, corrupt input and
  ordering. They created no graphics device and did not execute the client.
- Both PE/PDB GUIDs and ages match. The real saved official dump's 27,906 calls
  parse successfully with the new property-header handling. This remains the
  previously failed authored Reset fixture, not a positive full-scene trace.
- The private host compiled and linked successfully. Large Address Aware is
  explicitly restored and verified after Zig's unsupported GNU linker option
  translation. The copied tree has no Git metadata, so its current CLI version
  string is `0.0-unknown`; the pinned source/helper hashes and distinct filename,
  rather than that string, identify it.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Normal `frd9.dll` | 5,796,352 | `b3a77ec2d8c28926cf78d4d5cd17e35ef9edf1c9173ea8c32cd30c62feb956dd` |
| Stand `frd9-stand-replay.dll` | 5,777,408 | `8405462f73d88c6801f8f9ce421229d47cd9127fe6406aef62cb05347ac5e00d` |
| Private `northlight-native-retrace.exe` | 3,841,536 | `70dd6dfb7f9b6be1942585f40639732a2a8e1d596f7dae95da0d26a1b458223a` |

Evidence is under `northlight-diagnostics/rotation-scene-followup-20261024`:
`native-capture-build-normal`, `native-capture-build-stand`,
`native-chain/plan-compiler-check-3`, and
`native-chain/native-snapshot-final-20261024-074716`. Private host evidence is
`northlight-builds/native-host-tools/build-native-1`.
The separate normal manual package is
`northlight-builds/northlight-native-capture-63e804d-20261024`; it contains only
the normal DLL and matching PDB, manifest and README. It is not installed.
Final artifact/protected-input evidence is `native-capture-acceptance.json`.

Retained incidents: the first parser fixture encountered sandbox temporary-file
permissions; its guarded private-temp run passed. A discovery-only edit by the
coordinator invalidated the capture fixture's broader wrapper guard; its six
actual fixture dependency hashes and all 75 group executions passed unchanged.
The private host initially failed because upstream disabled exceptions and
because its single-entry callback function was inline-only. The bridge TU
alone now enables exceptions and uses the exported callback-table API; both
failed logs are preserved. An initial stand-build wrapper targeted an existing
evidence directory and refused before compilation; the fresh target passed.

No full test suite, old 69-case list or game was run. No settings, MPQs, world
cache, installed DLL, wow_optimize or GitHub state were changed.

## What remains before performance acceptance

The official diagnostic replay of recorded post-Reset call 27889 produced
exactly the intended RGB bytes, whereas the original authored source still
failed its post-Reset backbuffer oracle. Stock query omission and different
HWND sizing/thread behavior prevent attributing that difference to one cause.
The original six-frame gate remains failed, not replaced by this diagnostic.

Next work is a genuine positive trace/packet transport scenario, typed resource
aliases with queued-snapshot lifetimes, raw-depth/MSAA admission, then the
actual full scene and camera trajectory with unchanged quality. Native-only
and Northlight measurements must use those same inputs. Static-cache staging,
CPU contract tests, compilation and the correct diagnostic snapshot do not
substitute for that comparison.
