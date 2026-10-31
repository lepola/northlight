# Complete Windows test installer - 2026-10-31

Current complete package: [0.3.216-test.20261031.11](complete-installer-20261031.11.md), including the subsequent shadow, material, animated-light and character-normal fixes. The identities and acceptance below describe the earlier `.9` snapshot.

Version: `0.3.216-test.20261031.9`. Production code snapshot: `82a644ae`.

| Artifact | Identity |
| --- | --- |
| Archive | `Northlight-0.3.216-test.20261031.9-Windows.zip` |
| Archive bytes | 19,736,691 |
| Archive SHA256 | `2b5f708cfc3cd4cfb7a5250a84c3cde5d1211833b9fde9346cc7a885b9c0e29e` |
| DLL SHA256 | `c3664e15b22abb1394905998289d14e60675d31ac660e8aac0bcc3866ec63931` |

## Included changes

This installer supersedes `.8` and includes the complete capture-driven HDR/glare fixes, the two subsequent optimization waves, CPU attribution and native-MSAA presentation stand, the alternate FP16 veil correction, and the subsequent scalable-UI and camera-dependent sky fixes. It combines the published #98 integration, the previously local all-PR assembly and the new linear HDR pipeline. The merge preserves native actor stencil ownership on original draws and keeps HDR sky replay separate.

- Full HDR world/material composition, display-aware live SDR/scRGB output, automatic exposure, independent UI brightness and peak calibration, optional legacy highlight expansion and bloom. Include the distant-horizon and native Ctrl+R boundary fixes, direct screen-blend geometry, deferred SDR mirrors and eligible sampler-based legacy decoding.
- Finish world effects before scalable portrait/name backgrounds as well as physical-pixel text. Initialize linear sky capture at the first main-backbuffer native sky layer, independently of sun/moon visibility, to prevent camera-dependent transfer changes. Explicit diagnostic captures export the HDR base. The account owner confirmed both the interface and sky repairs.
- Existing vegetation/grass, native present-wait workaround, local-source shadows, settings/Diagnostics, material relief, fog recovery, native sun/cloud-order recovery, bounded EVENT waits, shadow-cache transitions, bounded scene storage, AO quality variants and the 31 previously validated preparation/kernel optimizations.
- Actor ownership AO from #112, with the combined GTAO compatibility work; asynchronous persistent material maps from #97; native herb/gameobject/prop emission from #114, including the latest World models without GameObjectDisplayInfo rows (`ed22e9a9`); and the Telredor point-shadow visibility work.
- The corresponding installer, cache/art builders and dependency closure. Proprietary game assets, generated world caches and private captures are not distributed.

PR #94 retains the independently buildable presentation/UI subset and receives the shared scalable-UI boundary backport. PR #110 retains the shared raw-depth horizon occlusion fix (`1ce59953`); it has no linear HDR sky replay to change. PR #98 carries this complete integration. Previously recorded exclusions of the new HDR pipeline and actor AO describe older snapshots and do not describe this package.

## Validation

- Release Windows x86 DLL builds; shader manifests pass the build's provenance checks.
- Offline HDR graph: nine scenarios on each DXVK backend, using controlled native MSAA inputs, actual world/material bytecode, projected effects and final UI. Finite output, unchanged alpha, UI over clear sky/horizon, zero warm target allocations and reduced eligible decode draws pass. Maximum reference difference is below 0.012 scRGB. Timings are component measurements, not whole-game FPS.
- Raw-depth celestial occlusion, corrected authored-shoulder transfer and bounded HDR halo energy and omission of the duplicate HDR veil; native alpha-only UI attenuation and optional combined world/material application. All nine workload timings pass the unchanged regression gate. Actual WorldRenderer tests pass caps, creation-failure, diagnostic and expansion fallbacks on DXVK 2/3 with MSAA0/2. The combined shader requires 676 slots; unsupported devices retain two passes. Legacy expansion also retains two passes after a measured DXVK 2 regression.
- Sun oracle: 43 cases per backend, including horizon/energy negative controls, 12 SDR/HDR glare-transfer comparisons, 12 late-veil transfer comparisons and hidden-source controls. Five celestial replay families pass on both backends. See [capture findings and exact component timings](hdr-glare-performance-20261031.md); these do not establish whole-game FPS or panel luminance.
- HDR Frame: 2782 numerical cases per backend, including 2520 independent grading comparisons, 56 filter controls, exhaustive ARGB8/XRGB8 transfer controls, exposure timing, headroom, legacy expansion, logical SDR mirror, MSAA and fixed-function transfer.
- Production proxy: 1,468 checks across HDR, SDR and MSAA8/triple-buffer modes on DXVK 2.7.1 and 3.1.1, including Reset/resize, both Present paths, authored UI, logical readbacks and partial writes.
- Carried-forward feature acceptance from `.3` (unchanged feature implementations): actor AO has 40 independent ray-truth/color/contact cases and 160 production AO/blur renders per backend. Actor stencil: 50 cases per backend, including MSAA, occupied native MRTs, NameMask and failure handling.
- Carried-forward material-map acceptance from `.3`: separate-process GPU produce/consume, corruption repair, settings invalidation and equivalent-input reuse on both backends. CPU store tests cover restart, bounded queues, corruption and unavailable storage.
- NameMask, celestial veil, sun hue and command-stream checks pass. Earlier actor-stencil sanitizer, eleven emitter-staging cases and transactional settings acceptance remain associated with their original snapshots. Package verification passes payload hashes, pinned runtimes/backends, packaged entry points and installer contracts.

The [autonomous client follow-up](hdr-autonomous-validation-20261031.md) adds direct two-texture UI composition, preserves the one-texture shader, and records stationary SDR/HDR evidence. Fresh capture analysis and acceptance are in `.local/hdr-autonomous-20261031`; the preceding component evidence remains in `.local/hdr-glare-20261031`. Earlier feature acceptance is retained under the publication checkout's `.local/all-feature-hdr-check` and `.local/all-feature-tests`. The archive's BUILD-INFO.json and payload-manifest.json authenticate the packaged files.

This is a test installer. Day/night/interior tuning and the HDR full-frame overhead target remain incomplete; the extra actor-AO cost retains #112's documented tradeoff. Physical two-monitor migration remains deferred. The autonomous follow-up separately entered the requested character, collected fresh logs and installed the tested renderer; the packaging command itself does not launch the game.

## Latest acceptance and client evidence

The exact packaged DLL passes the production x86 build, nine-workload composition graph on both DXVK backends, 2782 HDR Frame cases and 43 sun cases per backend, and the HDR/SDR/MSAA8 proxy matrix. The inherited sun replay implementations retain their earlier five-family acceptance. Scalable-UI and extracted sky-admission tests pass optimized and sanitizer runs; disabling first-layer initialization reproduces the expected failure. Package hashes and installer contracts pass. The installed payload is byte-identical to this offline-tested DLL. Private current evidence: `.local/hdr-camera-20261031`.

The preceding `.7` stationary client comparison measures about 112 FPS in HDR versus 120 in SDR at 4K/MSAA8. Neither `.8` nor the correctness fixes in `.9` establish a new whole-client performance result. [Client measurements](hdr-optimization-wave4-20261031.md), [solar follow-up](hdr-solar-followup-20261031.md), [scalable UI correction](hdr-scalable-ui-boundary-20261031.md) and [sky admission correction](hdr-sky-admission-20261031.md) state the evidence and limits. Sixty independently validated optimizations per direction have not been established.
