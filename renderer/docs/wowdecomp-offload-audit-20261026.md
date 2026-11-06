# CPU offload to Vulkan and background workers

Historical audit: 26 October 2026, Northlight and the WoW 3.3.5a client. Published in English during the 27 October PR coverage audit. Versions, line references and observations below describe the original audit snapshot, not a new performance measurement.

The most practical next step for this installation is to reduce serial preparation of Northlight material packets and remaining game-thread waits for replay. Moving native CPU skinning, animation, culling or particle simulation requires access before D3D9 submission. A Vulkan shader in the proxy cannot undo work the client has already completed.

These sources cannot establish Warden compatibility. Work confined to Northlight-owned snapshots and resources requires no new client-code changes; that defines an intervention boundary, not anti-cheat certification.

## Examined versions and scope

| Source | Version and limits |
|---|---|
| Isolated Northlight checkout | `8b9d0b77284caca18499b27f7e1a4330b17c9655`, detached during the original audit |
| [SatyPardus/wowdecomp](https://github.com/SatyPardus/wowdecomp/tree/20f9120e0a8f064338ecd9f2dd6cbc01642bf695) | `20f9120e0a8f064338ecd9f2dd6cbc01642bf695`; 3,565 tracked files and 2,732 code files outside `ThirdParty/`, including libraries located inside Engine |
| [saty.dev/git/clank/wowdecomp](https://saty.dev/git/clank/wowdecomp) | Observed HEAD `65511470d40e37c75ee6c68feac68f6844987205`. Its README describes builds 15890 x64 and 15595 x64/x86. The requested `Engine/Source/Model2/Posix/M2GpuSkin.cpp` returned HTTP 404; the reported implementation was not verified there either. |
| DXVK | Interop contracts examined in [v3.1.1](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/d3d9/d3d9_interop.h), matching the backend version in the audited log |
| Historical log snapshot | SHA-256 `7637ac7df8f3744d61f548333b1ab16420dbc07fe3170c7ea9d08809ea848421`. A source fingerprint is present, but correspondence between that installed DLL and the audited checkout was not established. |

The audit inventoried the GitHub tree and reviewed subsystem architecture, reading M2, world rendering, particles, loading and Northlight command-stream paths in detail. This is static analysis of offload boundaries, not verification of every function or a new benchmark. Neither examined checkout contained `.codegraph/`; no index was created.

Private evidence retains `evidence.json`, `collect_evidence.py`, the full log snapshot, `saty-path-check.json` with the README/404 results, and `installation-verification.json` with protected-file hashes. Machine-specific paths and raw logs are intentionally not published. The evidence filter excludes early frames and Destroy, but does not prove gameplay or focus: its 161 events cannot be treated as a frame-time distribution.

## Interpreting the M2GpuSkin claim

The examined GitHub revision contains no `Model2/Posix/M2GpuSkin.cpp`, `WOWPORT_GPU_SKIN` string or Vulkan backend. The search covered all tracked text files, including ThirdParty. Thus modes `0|1|ab`, default enablement, the reported "25% main thread" and the supplied FPS figures remain claims from the supplied comparison table, not confirmed behavior of these sources. Adding an environment variable to this installation does nothing without its implementation.

The supplied 228 to 274 FPS figures correspond to 4.386 to 3.650 ms per frame: approximately 0.736 ms saved, or 20.2% more FPS. This is not a Northlight forecast. Our path is original Windows `wow.exe` -> Northlight D3D9 proxy -> replay -> DXVK -> Vulkan. A rebuilt client with its own Vulkan backend has different preparation costs.

The decompilation already distinguishes two M2 paths:

- With shaders enabled, [CM2SceneRender::SetBatchVertices](https://github.com/SatyPardus/wowdecomp/blob/20f9120e0a8f064338ecd9f2dd6cbc01642bf695/Engine/Source/Model2/CM2SceneRender.cpp#L168) uses a shared mesh. [DrawBatch](https://github.com/SatyPardus/wowdecomp/blob/20f9120e0a8f064338ecd9f2dd6cbc01642bf695/Engine/Source/Model2/CM2SceneRender.cpp#L429) supplies bone palettes in vertex constants starting at c31; the vertex program performs deformation.
- In the fallback, [CM2Model::SetVertices](https://github.com/SatyPardus/wowdecomp/blob/20f9120e0a8f064338ecd9f2dd6cbc01642bf695/Engine/Source/Model2/M2Model.cpp#L2695) allocates and locks a streaming buffer, invokes `s_transformVertices`, then submits completed vertices to Gx. The proxy receives them after that computation.
- [DrawBatchDoodad](https://github.com/SatyPardus/wowdecomp/blob/20f9120e0a8f064338ecd9f2dd6cbc01642bf695/Engine/Source/Model2/CM2SceneRender.cpp#L588) also separates shader batching through palettes and expanded indexed geometry from per-model CPU transforms in its fallback. It does not prove that native doodads lack batching.

Replacing another port's active CPU fallback or changing its instancing could explain a large gain. Without `M2GpuSkin.cpp`, that is a hypothesis. Measure CPU-transformed sections, vertex counts and time in this build rather than classifying all M2 work as CPU skinning.

## Work already offloaded

| Work | Evidence at the audited revision | Consequence |
|---|---|---|
| Native M2 animation on another thread | `M2Scene.cpp:1687`, `M2Cache.cpp:109`: some `AnimateMT` work uses BeginThread/WaitThread, followed by `AnimateST` | A second M2 thread is not a new architecture. A default value does not prove that the thread is active in the installed binary. |
| Native doodad and particle batching | `WoW/Source/Client_CVar.cpp:696` registers `M2UseThreads`, `M2BatchDoodads`, `M2BatchParticles` with default `1` | Absence from Config.wtf does not establish disablement. Do not change personal settings to enable an existing default. |
| D3D9 submission and Northlight draw hooks | `src/stream/stream_device.h:2`; the audited installed configuration has `CommandStream=1` | Much of Northlight rendering already runs outside the game thread. Another D3D thread cannot remove native gameplay work. |
| Large dynamic uploads | `src/stream/stream_proxies.h:1060`, `1187`: zero-copy from 4096 bytes, DISCARD ring, NOOVERWRITE protection and bounded fallback | Reuse this mechanism. Attribute remaining copies and waits before adding another implementation. |
| Actor preparation and replay bounds | `src/replay/prepare_worker.h:2`, `replay_bounds_job.h:12` | Bounds workers already exist. Separate material admission still performs its own computation. |
| Actor decode and GI | `src/replay/actor_scene_job.h:8`, `src/gi/gi_solve_pool.h:2` | Decode/resolve already use the GI worker, with helper threads for probes. Faster GI primarily reduces publication latency. |
| Normal-map generation | `src/water/auto_normal_cache.h:106` creates a pixel shader and render target | Already GPU work through D3D9/DXVK. A compute rewrite alone does not establish CPU savings. |
| Static shadow instancing | `src/shadows/static_shadow_gpu.h:63` onward | Already implemented. Additional compatible material/capture groups are a possible next candidate. |

## Candidates within the current Northlight boundary

This is an order for engineering investigation, not a measured ranking. "Main" means direct game-thread work; lower replay CPU helps it only during backpressure or synchronous waits.

| Priority | Candidate | Potential benefit | Boundary and required proof |
|---|---|---|---|
| 1 | Move pure material decode, exact pose/bounds and subdivision metadata to the existing job pool after inputs are frozen | Replay CPU; indirectly main when waiting for replay | `auto_displacement_renderer.h:298`, `435`; `auto_bump_renderer.h:243`. Keep D3D reads/captures on the owner. Workers receive immutable mesh, declaration/program, current constants and epoch. Avoid an immediate join at every draw. |
| 2 | Remove unnecessary preparation between an ordered game-thread request and its required answer; examine upload fallbacks | Main wait/latency, sometimes replay CPU | `stream_proxies.h:1110`, `1523`, `stream_device.h:637`. Express EVENT, ring and shadows already exist. Separate waitMax/waitBudget/overlap, readback and actual GPU completion. Preserve query results. |
| 3 | Instance compatible Northlight material/capture groups | Replay submission CPU and Vulkan command count | `auto_bump_renderer.h:355` submits individual packets. Reuse the shadow approach only after proving mesh/shader/texture/constants/alpha/depth/stencil/sampler/viewport keys and ordering. Native beauty remains unchanged; arbitrary transparency cannot be regrouped. |
| 4 | Use direct Vulkan for an owned pass if D3D9 submission overhead is measured to dominate | Replay CPU, possibly GPU bandwidth | Start with one owned image/buffer pass. Measure flush, barrier and interop cost separately. First check whether the existing D3D9 shader path achieves the same result. |
| 5 | Cache GPU skin results across Northlight shadow/material passes using the identical pose | GPU vertex work; CPU only when replacing an owned evaluator without readback | Key by mesh, pose/constants, program and frame/epoch. A single native draw already skins in its VS, so added compute may make it slower. Immediate GPU reduction readback is not a replacement for CPU bounds. |
| 6 | Solve GI or visibility on the GPU for the Northlight-owned scene | GI job/publication latency and some worker/GPU cost | Preserve scene/BVH ownership, alpha coverage, seeds/precision, cancellation and budgets. This is a separate backend project, not a promised increase in native FPS. |

For priority 1, `nearestMaterialDepth` reads D3D declaration/constants and obtains a snapshot before CPU envelope/bounds evaluation. `prepare()` below it may evaluate up to 8,192 fallback vertices. Only the pure part after the snapshot is suitable for a worker; live D3D state must not be passed to a worker while it changes. If the result is required before the same native draw or to choose that draw's displacement mesh, the work stays serial without restructuring phases. Deferred capture provides a wider overlap window.

The historical log contains a `poseFallback` sample of 0.8454 ms for 8,186 vertices (`renderer-log-at-audit.txt:1223`). It is one sampled frame from a mixed session, not an average, game-thread attribution or measured worker benefit. It supports profiling this path, not a promise to recover 25% CPU.

For priority 2, user-pointer copies cannot be removed by queuing a borrowed pointer: D3D9 permits the application to reuse its memory after the draw returns. `DrawIndexedPrimitiveUP` needs the index and vertex range to remain owned; copying can be avoided only with a proven immutable owner. Budget refusal currently falls back synchronously. The x86 virtual-address limit remains; a larger pool is not free CPU offload.

## Direct Vulkan backend considerations

DXVK 3.1.1 exposes `ID3D9VkInteropDevice`: Vulkan handles, submission queue, flush and queue lock/release, plus texture image information, transitions and CreateImage. That is a real extension point. However, `StreamDevice::QueryInterface` (`stream_device.h:364`) accepts only IUnknown/IDirect3DDevice9. Requesting an unknown interface from the game-facing proxy cannot create direct compute access. An internal replay-owner bridge must preserve lifetime and Reset behavior.

The experiment should be one owned image pass, an internal DXVK capability check, correct usage/layout/visibility synchronization, and the existing D3D9 fallback. Texture interop does not prove that an arbitrary D3D9 vertex buffer can be aliased as storage. A separate Vulkan device with copies every frame has a different cost that must be measured.

Parallel Vulkan recording requires separate command pools or external synchronization, with one coordinated submission-queue owner. A worker cannot safely issue vkCmd/vkQueue calls independently beside DXVK. See [Khronos Threading](https://docs.vulkan.org/guide/latest/threading.html). Compute does not require a separate compute queue; another queue neither guarantees overlap nor eliminates dependencies.

## Other wowdecomp subsystems

| Subsystem and entry points | Candidate in an owned client | Why a proxy-only change is insufficient |
|---|---|---|
| M2 bone animation: `M2Scene.cpp:1687`, `M2Model.cpp:5033`, `2486` | Job independent attachment-root trees and apply single-thread side effects separately | MT already exists; ST updates emitters/lights and scene state. Per-model jobs without hierarchy ownership race. D3D9 sees final matrices/vertices. |
| World culling/portal/sort: `CWorldSceneRender.cpp:71`, `World.cpp:3587`, `3965`, `4121` | Snapshot scene/view for parallel visibility jobs; later GPU culling/indirect in a new backend | Native traversal mutates sortTable, portals/frusta and software occlusion buffers before draw submission. Proxy culling saves replay/GPU work, not the completed client traversal. |
| M2 sorting: `M2Scene.cpp:1038`, `1263` | Sort immutable render items on workers, separating opaque state keys from transparent order | Native items retain live model/material links. Reordering D3D calls does not remove native sorting and can break alpha/depth semantics. |
| Particles/ribbons: `Services/ParticleSystem2.cpp:1042`, `1539`, `2018`; `M2Model.cpp:5695` | Job independent emitters, then GPU billboard/ribbon expansion and suitable compute simulation | `ProjectParticle:475` calls world height; child/model particles and substeps exist. Preserve callbacks, seeds, sorting, attachments and visual timing. The proxy receives completed vertices. |
| Doodads/detail vegetation: `CM2SceneRender.cpp:588`, `WorldClient/DetailDoodad.cpp:333`, `713` | Persistent geometry with instance transforms/material data instead of per-instance expansion | Stock shader batching already exists. Owned capture can be tested locally; native preparation needs changes above Gx/D3D9. |
| Async loading: `Services/AsyncFile.cpp:125`, `484`; `WorldClient/MapObjRead.cpp:413`, `597`; `MapArea.cpp:392` | Worker decode/build of immutable data, short owner-thread publication | File IO is already asynchronous, but postload callbacks run Load/CreateMaterials and publication. Moving the whole callback can violate Gx/scene ownership, outside proxy control. |
| BLP/textures: `Services/Texture.cpp:643`, `2161`, `2230`; `BLPFile/blp.cpp` | Job decode/conversion/mip preparation, owner-thread upload; preserve suitable compressed formats | Some preparation precedes D3D Lock/UpdateTexture. GPU mip generation and compressed sampling are not new Vulkan capabilities. |
| Gx/shader effects/FFX and native queries: `Gx/CGxDeviceD3d/CGxDeviceD3d.cpp:708`, `3241` | Pipeline preparation, batching, resource ownership and asynchronous queries with explicit semantics | Northlight already offloads D3D execution. End-frame EVENT verifies completion; omitting it without a contract analysis changes behavior. |
| Frame/FrameXML/FrameScript/Lua: `FrameScript/FrameScript.cpp:24`, `FrameScript_Scripts.cpp:327` | Profile handlers/layout/addons; possibly prepare renderer-owned UI packets separately | Shared Lua state and synchronous game APIs prevent arbitrary OnUpdate jobs/GPU execution from retaining state/order. The D3D proxy does not remove this work. |
| SoundEngine, networking, object update, collision/movement | Independent jobs only after dependency/profile analysis | Changing the graphics backend does not make these GPU tasks. Audio/IO already have threads; gameplay and CPU hit tests need results at the correct phase. |

Picking/collision still need CPU data: `M2Scene.cpp:497`, `593` separately deform hit-test geometry, and `M2Model.cpp:2728` computes region bounds. Removing render CPU skinning does not remove every CPU transform. Synchronous readback of GPU positions for each operation can erase any gain.

## Warden compatibility boundary

On the installed original client, the first stage should remain confined to Northlight-owned immutable snapshots, CPU buffers and GPU resources. No new client-function detours, `wow.exe` changes, object-layout changes, security/network response changes or disabled checks are needed. This preserves the architectural boundary; server policy can still consider loaded modules and environment behavior.

Northlight already reads process memory (`world_context.h:83`) and contains a backend-specific import hook (`dxvk_engine_name.h:89`). The latter changes the DXVK backend import slot, not `wow.exe` code. Therefore, claiming no client interaction and guaranteed Warden acceptance would already be incorrect. The audit changed none of this.

The examined `WoW/Source/Game/GameClient/Win64/WardenClient.cpp:18` receives a payload without complete handling, and `WardenClient_Initialize:48` returns false. This x64 source does not validate Warden behavior in original x86 `wow.exe` or certify a modified client. This is a source limitation, not a recommendation to use that behavior.

Without server checks, module/configuration and operator confirmation of permitted modifications, one can assess whether client patches are added, not guarantee acceptance. Compatibility testing belongs in an operator-authorized environment. Concealment and check substitution are outside this plan.

## Implementation order and acceptance criteria

1. Pin the installed DLL/source build and compare the same stationary, rotation/streaming and crowd/spell scenes. Separate game CPU, game waits, replay CPU, worker completion and GPU intervals. Never sum overlapping intervals into saved FPS.
2. Use the recorder/standalone replay for the first material candidate. Compare job inputs/outputs with serial behavior across source rebuild, DISCARD/NOOVERWRITE, texture-write barriers, Reset/loss, allocation failure and cancellation. Preserve current pose, coverage, MSAA, distances and budgets. Fewer visible objects or stale animation are not acceleration.
3. Verify whether main-thread waits shrink or only replay time changes. A busy producer and empty queue require CPU attribution: the idle-gap log cannot distinguish Lua, loading, animation and culling. Investigate remaining stream fallbacks before rewriting the existing ring/express path.
4. Test one bounded instanced material-capture group. Verify state/order by image oracle, including alpha, overlapping geometry and the first frame after off-to-on.
5. Introduce direct Vulkan/compute only after comparing complete CPU/GPU pass costs including interop, flush, barriers and fallback. Keep native animation/culling/particles as a separate owned-client project.

The original work was analysis and planning. It made no new optimization, game-FPS or Warden-certification claim. No game launch, DLL build/installation or runtime-source change occurred during that audit. Hashes of `wow.exe`, `d3d9.dll`, `wow_optimize.dll`, both Northlight INIs and `WTF/Config.wtf` matched the saved pre-audit values. The integration checkout remained at `8b9d0b7` at that time. Subsequent optimization and validation are tracked separately in PR #98.
