# Settings, HDR and optimization integration (2026-10-31)

This document records the earlier optimization-only snapshot. The [complete `.3` installer](all-features-installer-20261031.md) supersedes its release scope and includes the new HDR pipeline, actor AO, persistent material maps and later emitter follow-ups. Historical source identities and test counts below remain evidence for their original snapshots.

This candidate combines the 31 changes listed in optimization-batch-20261031.md. It preserves the existing settings and experimental HDR feature. This is a frozen release candidate, not a claim that every concurrent development chat or all future PR tips have been integrated.

## Published ownership

| Area | Review unit | Integration |
| --- | --- | --- |
| Experimental scRGB, world/UI transfer and logical backbuffer ownership | #94 | Same hdr_settings_contract.h, hdr_color.h, hdr_presentation.h and hdr_ui_patch.h as the independent HDR head a456e70d |
| Native settings UI, preview/save and Diagnostics; reused caller state block | #96 | Full menu uses the combined HDR contract |
| Material relief controls | #97 | Matching runtime fields and menu controls retained |
| Optional AOQuality High/Medium/Low | #118 | Now includes the complete parser, native enum, live shader selection, shipped defaults and six compiled variants; High remains default |
| Fog/cloud preparation | #109 | Seven output-preserving preparation changes |
| Capture/material/GI preparation | #117 | Thirteen changes, with bounded rather than exact GI trig acceptance |
| Local-light preparation | #114 | Four changes; retain each branch's own shader registers and cost model |
| Shadow filters | #113 | Three changes; independent exact filter and transition checks |

All 96 quality keys are present in the combined candidate. Ninety-one names also occur in the corresponding feature review units after AOQuality is added to #118. Five controls are currently carried by #98 only: BlobShadows, RainLens, SnowLens, RainLensStrength and SnowLensStrength. Their consumers, settings schema/native labels and shipped examples are retained in #98; they are not represented as independently ported features. This table makes that review boundary explicit.

The independent #96 build intentionally disables HDR fields when the HDR consumer is absent. Combining #94 enables them through the same contract; listing a disabled field alone is not treated as HDR implementation. Optional quality levels remain deliberate visual tradeoffs, while the new AO loop optimization compares each level to its own unoptimized algorithm.

## Functional compatibility

The combined production source identity is a0fc947391be2bff0a221d913ebc0f30c3f86201cb645bba737d226255181a11. The x86 DLL builds from the frozen source, independently of uncommitted HDR development in the canonical checkout.

The actual menu passes six SDR cases on native D3D9 and eighteen SDR/scRGB cases on DXVK 3. All requested pages render from a loaded snapshot. Sentinel state and world/view/projection transforms survive rendering, and the caller's existing state block replaces the redundant nested allocation. On private DXVK targets, SDR/UI160/UI320 peaks are 1/2/4. These private-target tests do not certify physical display luminance. Native scRGB remains outside this driver's accepted matrix; the explicit SDR-only gate does not relabel an empty native HDR output as success.

The same combined DLL passes 5,882 checks in 24 HDR presentation modes across DXVK 2 and 3, including captured UI shader handling, MSAA, multiple backbuffers, grading, stream-off, resize/Reset and Present. The original shader and constant223 are restored. The archived UI shader fixture has SHA256 5ec305472c91c20db02b71c988a846d4519a8869250e0fb0b0c57b3277411a45. No private capture is published.

Independent AO comparisons cover six kernels and 3,456 exact cases per native/DXVK backend. Shadow comparisons cover 576 directional and 576 actor cases per backend, including FP16/FP32 inputs and MSAA0/2/4/8. Wrong-output controls fail all three new oracles on both backends. The independent transition oracle also passes its scalar, production geometry, endpoint, moving-caster, ambient and transmittance checks on both backends.

No game or generating cache is used. Functional checks do not establish a whole-game FPS increase or arbitrary-driver allocation success. One legacy replay-state test was discovered to run an implicit CPU microbenchmark; it is now gated by an explicit --benchmark argument. Its unrequested sample is excluded from all performance evidence. Normal follow-up runs check correctness without that timer.

## Separate work in progress

The new HDR pipeline specification and automatic SDR/HDR output migration are being implemented in a separate active chat. Those unfinished working-tree files are not silently added to this candidate or claimed as completed here. The currently published opt-in HDR implementation remains included and tested. Reconcile and validate any later HDR changes as their own follow-up.

The optimization-only portable functional suite at 2a1784cd finishes with 233 PASS, zero failures/timeouts and 169 explicit prerequisite/manual omissions. The listed Windows GPU gates above run separately; skips are not treated as passes.

## Completed fixes reconciled before packaging

Integrate the independently published indoor-light bound and complete point-shadow reuse fixes from #98, plus normal-map cache stability and skinned eye/seam preservation from #97. The final source snapshot is 7d87985d before documentation updates. These are correctness follow-ups and are not counted as four additional optimizations.

The final combined source passes focused CPU checks for capture skip, point scheduling, normal-cache state/collisions, settings transactions and shader provenance. Displacement geometry checks pass on actual Windows x86 at O0/O2. Native D3D9 and DXVK 3 each pass 1,512 indoor-brightness cases (including rejected old-clipping controls), 180 exact normal-cache reuse cases, 96 visible-set frames without transitions, 12 CPU/VTF eye/seam cases and six read-only Human/Draenei client geoset fixtures. Point shadows pass 54 dynamic capture transitions at six distances, incomplete-publication rejection and four camera-membership states per backend. No game or live cache is used.

The final DLL repeats and passes all 5,882 HDR checks in 24 DXVK 2/3 modes. The menu, settings and HDR contract sources are unchanged by these four correctness follow-ups. Earlier full-suite results are not relabeled as a second full run after them.

Final screen-effect integration also passes on native D3D9 and DXVK 3 at MSAA0/2, including unlit receiver radiance, retained atmosphere, material invariance, names, absent geometry and restored state. The standalone High AO method additionally passes its original 99 assertions per backend. The installed DLL matches the verified installer payload; all five existing configuration files are byte-identical and the generating cache is untouched.
