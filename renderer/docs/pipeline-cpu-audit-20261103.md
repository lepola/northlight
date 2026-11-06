# Pipeline CPU audit, 2026-11-03

Version 0.3.219 removes work without an active consumer and shortens resource classification. Baseline: `039252509610f35019fdbad4a82c1e4a509c4fe4`. Private evidence and frozen baseline: `.local/out/pipeline-cpu-20261103/`. [Acceptance data](pipeline-cpu-audit-20261103.json).

## Why the menu and F10 differ

F10 clears `Device::enabled`, bypassing draw preparation and effect capture. Turning off the individual quality checkboxes does not describe the same image:

| Control | What disabling it leaves active |
| --- | --- |
| Character shadows | Static world shadows; the separate F9 component switch controls those. |
| Local light shadows | Local light illumination and fog scattering. Previously the lamp limit also clamped to at least eight. |
| Drifting fog banks | Regional/atmospheric fog; the F7 component switch controls the fog component. |
| GI strength = 0 | Night brightness and independent ambient illumination. These must survive a transport pause. |
| Material effect controls | Other enabled world lighting, presentation and HDR output. |

The latest supplied `qa_report/northlight-renderer.log` identifies **0.3.217**, with the runtime BLP mip guard active; it is not a capture of 0.3.219. Its late regular GPU intervals average approximately 30.84 ms, with GameScene 25.29 ms and Effects 5.51 ms. These are GPU timestamp intervals, not additive CPU measurements; stalls and submission gaps can contribute to them. They do not establish either a CPU-only or a GPU-only bottleneck, or predict a new frame rate.

## Changes

| Pipeline stage | Change and retained behavior |
| --- | --- |
| Command/resource translation | A typed D3D interface tests its known proxy vtable first, before the general 13-kind classifier. The general classifier/private-IID fallback remains for other objects. No added binding cache or lifetime ownership. |
| Actor capture admission | Hidden GI and zero GI strength no longer allocate actor jobs or force model captures just for GI. Dynamic-probe and quality switches still apply. |
| GI request/publication | Requests carry the effective consumer state. GI component changes and strength transitions through zero invalidate the request. Positive-to-positive strength changes remain display-only. In-flight publication compatibility rejects a disabled consumer. |
| Probe upload | A paused consumer retains the CPU atlas and skips uploads. Reactivation uploads the pending serial. GPU atlas validity requires a matching uploaded serial and active transport, preventing reads of an uninitialized/stale atlas. |
| Shadow preparation | Disabled shadows skip static-caster requests and replay selection. Other consumers of shared geometry remain enabled. |
| Material/terrain capture | Inactive material processing no longer requests its optional shared terrain path. The original terrain/world capture is preserved. |
| Local lighting | `LocalLightLimit=0` is now supported in the menu/INI. It skips equipment capture, attachment observation, list transforms/copies, light selection and scissor preparation. Re-enabling lamps restores selection. Default limits and enabled-light ordering remain unchanged. |
| Offline validation | Windows Zig runners now execute quality, effect-switch and live-settings assertions rather than skipping for a missing host clang++. The EVENT fixture checks actual flush completion while allowing the existing interleaved non-flushing polls. |

Moving BVH construction/tracing to the GPU was not justified by this evidence. BVH/mesh data also supports still-visible world effects; removing it globally would change the image. The changes stop work at the consumer boundary without discarding reusable geometry or caches.

## Measurements

All measurements are offline on the same machine with O2, 32-bit Windows Zig 0.15.2. Baseline and candidate use identical harnesses. These are bounded component benchmarks, not frame times or FPS estimates.

| Workload | Baseline | 0.3.219 source | Result |
| --- | ---: | ---: | --- |
| 1.8 million repeated resource Set calls, median of 27 samples | 6.7414 ms | 6.6352 ms | 1.6% less CPU time; exactly 1.8 million filtered calls and zero recorded commands in both. |
| 1.8 million proxy classifications, median of 27 samples | 3.6820 ms | 2.9990 ms | 18.5% less CPU time; identical resource identities. |
| Hidden GI, 100,000 deliberately due actor-admission frames | 100,000 actor jobs | 0 actor jobs | Model capture can be skipped when no other consumer/demand exists. |
| GI strength zero, same admission fixture | 100,000 actor jobs | 0 actor jobs | Positive strength still produces all 100,000 jobs. This fixture does not time vertex skinning or solver throughput. |
| 200 atmosphere preparations, 10,000 source lights, limit forced to zero | 2.6336 ms | 0.0386 ms | Both select zero lamps. Baseline uses an internal zero limit solely for the equivalent-output benchmark; its old public setting clamped to eight. |
| Same scene, 48 lamps enabled | 9,600 selected over 200 frames | 9,600 selected over 200 frames | Identical position digest `15294485561452661635`; no claimed enabled-light speedup. |

Two pointer-comparison/cache variants were rejected after repeat runs showed regressions; their initial favorable run was not reproducible. They are absent from the final implementation.

## Image and state checks

The actual `WorldRenderer` GPU stand passes on DXVK 3.1.1 with MSAA 0 and 2. It checks lazy geometry/probe restoration, allocation failures, screen-only rendering, fog history, state restoration, HDR combination/fallback, material normals and unlit receivers. With GI strength zero, night brightness still changes pixels by 0.0544434; the night image digest is **12954036118236719192** for both baseline and candidate, at both MSAA settings.

The production command stream passes its full existing native scenarios, including lifetime, reset/state blocks, replay failures, query waits, frame skipping, uploads and four randomized 20,000-step equivalence runs. The complete test-suite outcome, DLL identity and verified Windows package are recorded in the acceptance JSON. No game was launched and no installed MPQ or Wow.exe was changed.

The complete suite initially reported 264 PASS, 1 FAIL and 221 SKIP. The failure was the old source-predicate fixture for replay shadow selection, which did not model `effects.shadows`. Its updated exhaustive component/actor/geometry checks pass in the recorded targeted rerun. Consolidated result: **265 PASS, 0 FAIL, 221 SKIP**, no timeouts. Skips reflect unavailable declared dependencies, primarily the separate host clang++/c++ toolchain; the new Windows Zig checks run with assertions enabled.
