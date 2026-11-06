# HDR pipeline

Windows HDR requires packaged DXVK 2 or 3, `CommandStream=1` and windowed or borderless presentation. Enable HDR for the current display in Windows. Northlight follows that display and switches SDR/scRGB output live. A missing capability or failed HDR allocation selects SDR and exposes the reason in Renderer & HDR.

On the first launch of this pipeline, one atomic migration resets all earlier HDR tuning, requests HDR output, and selects Auto exposure and Auto peak. It preserves every non-HDR renderer setting. The migration marker is `HDRPipelineVersion=2`; later launches preserve the new values.

Renderer & HDR contains:

- **White point**: ordinary scene white, default 203 nits.
- **UI brightness**: independent interface white, default 160 nits.
- **Peak mode**: Auto uses current Windows calibration/display metadata; invalid metadata falls back to 1000 nits. Manual uses the saved peak. Effective scene/UI whites cannot exceed the selected peak.
- **Exposure mode**: Auto meters world luminance before bloom and UI. Exposure is compensation in Auto and the complete EV adjustment in Manual.
- **Bloom strength**: 0 removes the halo while preserving HDR source energy; default 1.
- **Legacy highlight expansion**: optional 0–2 EV, default 0. It selectively expands bright legacy SDR details, with limited gain and bright-area suppression. A uniform white field is unchanged. It cannot reconstruct clipped details or reliably distinguish every bright surface from an emitter.

White point, UI white, Manual peak and peak mode belong to the current display's stable Windows identity (`HDR.<id>` INI section). Artistic settings remain global in `[Renderer]`. Unsaved display calibration drafts stay separate; saving the current display does not commit a hidden display's draft.

Auto exposure defaults to −2…+1 EV, with T90 of 0.5 seconds when reducing exposure and 2 seconds when raising it. Fine tuning exposes these values. Loading or a confirmed camera/map cut starts a fresh evaluation; normal motion adapts smoothly. Display changes and resource recreation preserve adaptation. The authored ambient and NightBrightness contribute to the reference level, so night and interiors are not forced to daylight brightness.

The calibration mode generates direct scRGB signals for 80/203 nits, a test peak, a peak gradient, colored highlights and a small/wide field comparison. Its test signal bypasses scene exposure, contrast, bloom and the scene peak limit. The displayed nits describe the application's signal, not a physical measurement of the panel.

## Rendering contract

Legacy scene pixels are decoded once from sRGB. Northlight lighting, atmosphere, celestial sources and composition retain relative linear radiance in FP16; relative scene white is 1. Bloom reads this unclipped input. Exposure and a hue-preserving soft shoulder map the completed world to scRGB, where 1 represents 80 signal nits. Sun/moon assets are decoded before their authored emission; source gains are independent of monitor peak.

Deferred native particles with known Particle/ParticleUnlit vertex identities, a proven single-texture pixel combiner and ADD blending (ONE/ONE or SRCALPHA/ONE) retain shader RGB above 1 before exposure and bloom. RGB stays nonnegative and source alpha stays in [0,1]; authored shader saturation is preserved. This extends the existing sRGB decode, not physically calibrated emission. A separate cached transfer variant keeps UI, native sky, non-additive effects and unproven shaders on the existing bounded path. Early hazard drains and SDR rendering retain their previous behavior.

Legacy expansion precedes Northlight additions. Material albedo is inferred from the unexpanded legacy input so expansion does not increase Northlight's lighting coefficients. The separate UI transfer follows world presentation. For DISCARD swapchains, the client's 8-bit scene mirror is generated on its first logical copy/readback/partial-write request. COPY/FLIP retain the eager compatibility MRT. This mirror precedes HDR exposure, contrast and bloom. The game's logical formats, buffer count and Reset parameters stay unchanged.

HDR uses an additional FP16 swapchain and never resets the game's device merely to toggle output. Windows output state is queried separately from Vulkan scRGB support. The latter can remain available while Windows HDR is off. Game-initiated Reset still recreates resources normally. HDR shaders, target sizes, samples and ownership are bounded; the diagnostics show HDR frame/output payload separately from driver allocations and residency.

## Verification

See [the agreed specification](hdr-pipeline-spec.md) and [the validation record](hdr-validation-2026-10-31.md).

Run `python tests/verify_hdr_pipeline.py --output .local/hdr-check` for the standalone build, CPU boundary checks, production composition graph and DXVK 2/3 Frame/proxy acceptance. It uses authored inputs and UI by default, requires no captured game assets, and never launches or modifies WoW. Supply explicit backend paths when the local client is not configured. Optional `--capture-prefix` imports only native scene/depth from a local capture; `--captured-ui` adds a local UI shader. The report separates GPU timestamps, CPU submission, structural draw/allocation counts and numerical output checks. Sky, expansion and stencil cases exercise the conservative fallback.

`tests/verify_hdr_lifecycle_gpu.py --cycle-hdr --proxy renderer/frd9.dll` exercises Windows HDR toggles with live resources and restores the original Windows state. Supply `--dxvk2`, `--dxvk3` and an isolated `--output` directory.

`tests/verify_hdr_integration_gpu.py` checks both Present paths, legacy buffers, captured UI shader/state restoration, graded transfer, MSAA, Reset/resize and alternating scene/UI generations. Supply the two backends, `--captured-ui` from your client, and an isolated output directory. Captured game shaders are never committed.

`tests/verify_hdr_frame_gpu.py` checks actual GPU composition, headroom, legacy expansion, adaptation timing and resource loss. Its `--benchmark` option measures post passes with GPU timestamps at 1080p/1440p/4K; it does not measure full-client FPS.

`python tests/verify_hdr_particle_gpu.py --output <isolated-output>` reads stock client particle shaders and a texture without modifying the client. It exercises the production replay callback and Color transfer with controlled sprite geometry, including eligibility, cache isolation, failed-draw state restoration, early/SDR drains, SM2/SM3, fog, MSAA and bloom. Native shader/texture data and raw results remain in the local output directory.

The 2026-11-01 particle integration passes on DXVK 2.7.1 and 3.1.1 (RTX 4090): 1,512 scalar cases, 2,304 admission/state cases, 60 native shader cases and six replay-gate cases per backend. Linear scene outputs match across backends; the maximum post-transfer difference is 0.001953125 scRGB. The controlled Mod2x scene's linear peak changes from 1.0703125 to 3.025390625 with identical alpha. Existing HDR shader binaries, including UI, remain byte-identical. Full regression also passes: CPU UI/sky boundaries, 11 composition scenarios, 2,869 Frame cases, 43 sun cases and 734 production-proxy checks per backend. The release DLL SHA-256 is `d8daabd528068be8c827f967a60a5b7a327d392d02275125a7c2bd44776f3675`.

For 64 sprites in one SM3 draw at 4K, median GPU intervals are 0.037755 to 0.039899 ms on DXVK 2 and 0.038232 to 0.038384 ms on DXVK 3; paired confidence intervals span zero. Including exposure/bloom gives 0.230560 to 0.232485 ms and 0.278366 to 0.282005 ms respectively, also within measurement variability. The transfer adds 17 D3D9 instruction slots versus the prior 24 (SM2) or 23 (SM3), with no added textures or render passes. GPU intervals can include submission stalls; these results do not measure client FPS or physical HDR display output. Evidence is retained under `.local/hdr-particle-integration-20261101`.

Publication on the current #98 branch retains byte-identical HDR test inputs. Its own fresh x86 release build and full HDR regression pass on both backends with the same 11 composition, 2,869 Frame, 43 sun and 734 proxy cases per backend. The publication DLL SHA-256 is `92982ff0f6b2dd5b5671ca2e1ba87a2fec346318243fda32b3ce7e2c3137180f`; source identity is `7abb740e97e8028bcad5b0bfe5398749bb56060d493f630dfec957329b6aa703`. Evidence is retained under `.local/hdr-particle-publication-20261101/regression`. This HDR acceptance does not recertify unrelated full-suite checks or gameplay.

Two-display movement is deferred because this workstation has one physical display. Representative in-game day/night/interior visual tuning, Alt-Tab/hotplug and full-frame performance acceptance still require a client session. Wine/Linux, macOS and exclusive fullscreen are outside this phase's verified support.
