# Native scene replay: executable inputs and host foundation

Subsequent capture/bridge implementation and cache recommendation:
[Native capture bridge](native-capture-bridge-20261024.md).

This continues the snapshot-coherence work. The game log is unchanged and
predates the private fixes. No whole-scene FPS, stable 120 FPS, visual parity
or elimination of camera-turn stalls is claimed.

## Fixed: the private chain had no world-cache payload

`prepare_native_scene.py` previously copied the scene manifest but omitted
the files it described. WorldRenderer loads `world-cache` relative to its
DLL, so that prepared private chain could not load the available static
terrain/model/light cache.

The explicit `--static-cache-root` option now validates each listed relative
path, byte count and SHA-256 before staging. It rejects traversal, Windows
aliases, duplicate JSON/path/physical inputs, file/parent overlap, source/output
overlap and source changes during copying. Exact files go into the private
Northlight chain's `world-cache`; installed cache and personal settings are
untouched. Readiness and FPS flags stay false until execution proves them.

The focused byte/architecture-fixture regression passes. Its hardlink negative
initially hit a sandbox permission denial; the same frozen check passed with
private temporary hardlink access. No assertion was removed. One initial real
preparation invocation omitted the required explicit retracer path and was
rejected before creating output; the corrected invocation is retained.

The actual corpus was then staged successfully:

| Input/proof | Result |
| --- | --- |
| Static files | 1,660 |
| Payload bytes | 311,859,176 |
| Source hashes rechecked | 1,675 |
| Staged hashes rechecked | 1,684 |
| Protected installed/log/configuration inputs | All ten unchanged |
| Replay or GPU execution | None during staging |

Evidence is `native-chain/static-cache-staging-2/validation.json` under the
private `rotation-scene-followup-20261024` diagnostic directory. Its prepared
Northlight folder includes the exact dedicated DLL, backend, tool dependencies,
quality snapshot and actual cache. These static cache meshes retain their
existing bind-pose/simplification/content limitations; staging does not turn
them into the original animated native render stream.

## Isolated the OBS confounder; Reset failure persists

The earlier failed private capture positively loaded the OBS Vulkan hook.
Its installed layer manifest documents the process-local control
`DISABLE_VULKAN_OBS_CAPTURE=1`. One comparison used the exact existing frozen
AA0 fixture executable, exact backend and original sequence/oracle, with only
that layer control and private log/cache paths. No hook module was observed,
but the original post-Reset swapchain image still failed. Source/backend hashes
remained unchanged. Thus OBS was a real confounder, but disabling it did not
repair this failure. No game/OBS/driver settings were modified and no backend
defect is established. Evidence: `native-chain/reset-followup/one-no-obs-1`.

## Built the upstream native-call host foundation

The next host reuses apitrace's parser and D3D9/resource/upload handlers.
Pinned upstream 14.0 commit is
`18cfd388ac47a9e00978b34d6c20341f3e2cdb56`, with its eight pinned dependency
submodules. Private CMake 3.31.6 and Ninja 1.11.1.4 were prepared without a
global installation. Only the x86 `d3dretrace` dependency closure was built;
no test target, game, trace replay or GPU benchmark was run.

Zig 0.15.2 compiled all sources but rejected the GNU linker option
`--large-address-aware`. The private toolchain adapter removes that unsupported
argument, then the intended Large Address Aware bit is set and verified in
the newly generated executable. This retains the 32-bit address-space intent;
it is not dropped to make linking pass. PE checks also prove x86/PE32, ASLR
and DEP. The resulting executable starts and reports `apitrace 14.0`.

The upstream source working tree remains clean. Private output is
`northlight-builds/native-host-tools/build-base-1/d3dretrace.exe`, SHA-256
`214796c843501016e664f5b14b9faf246ce515c81d28f7c672e4fa14a132d171`.
It is a host prerequisite, not yet the Northlight snapshot bridge. Build and
toolchain failure logs, commands, dependency identities and PE-bit changes are
retained there; the immutable official binary bundle remains separate.

## Exact next bridge requirements

The official retracer records D3DPERF markers but ignores them during replay.
Its compiled callback registration can instead wrap the existing D3D9
callbacks before first dispatch, forwarding each original handler unchanged.
No dynamic plugin callback API was found.

Admission must load the actual immutable client PE and first complete packet
before the first factory call, then bind subsequent packets at independently
verified draw/device/epoch joins. Preserve parser dispatch order: a marker
completed inside a Draw has a larger call number but is dispatched before
that enclosing Draw. The first packet event must verify preload, not bind the
same draw twice. Failed calls, owner changes, multi-device/address reuse and
missing input must stay unsupported until explicitly handled.

Original resource identities inside celestial/glare snapshots also need a
typed original-object-generation to replay-proxy mapping. Snapshot heap-key
addresses must remain opaque and unchanged. A current global pointer map
cannot substitute for the correct queued-frame resource lifetime.

No genuine native game trace or snapshot journal exists in the currently
available corpus. Existing traces are authored plumbing fixtures. A faithful
scene result still needs those actual calls/resources/poses, complete corrected
runtime snapshots, settings correspondence, positive WorldRenderer activation,
color/depth/MSAA fidelity and same-backend uncapped static/rotation measurements.
The ≥120 FPS goal and the equipment/shadow/visual issue acceptance remain open.
Detailed five-direction reviews from all three agents are retained privately.
