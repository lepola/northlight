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

Legacy scene pixels are decoded once from sRGB. Northlight lighting, atmosphere, celestial sources and composition retain relative linear radiance in FP16; relative scene white is 1. Bloom reads this unclipped input. Exposure and a hue-preserving soft shoulder map the completed world to scRGB, where 1 represents 80 signal nits. Sun/moon assets are decoded before their authored emission; source gains are independent of monitor peak. Native legacy effects remain SDR-origin content. Capturing their original pre-clamp radiance is a later phase.

Legacy expansion precedes Northlight additions. Material albedo is inferred from the unexpanded legacy input so expansion does not increase Northlight's lighting coefficients. The separate UI transfer follows world presentation. For DISCARD swapchains, the client's 8-bit scene mirror is generated on its first logical copy/readback/partial-write request. COPY/FLIP retain the eager compatibility MRT. This mirror precedes HDR exposure, contrast and bloom. The game's logical formats, buffer count and Reset parameters stay unchanged.

HDR uses an additional FP16 swapchain and never resets the game's device merely to toggle output. Windows output state is queried separately from Vulkan scRGB support. The latter can remain available while Windows HDR is off. Game-initiated Reset still recreates resources normally. HDR shaders, target sizes, samples and ownership are bounded; the diagnostics show HDR frame/output payload separately from driver allocations and residency.

## Verification

See [the agreed specification](hdr-pipeline-spec.md) and [the validation record](hdr-validation-2026-10-31.md).

Run `python tests/verify_hdr_pipeline.py --output .local/hdr-check` for the standalone build, CPU boundary checks, production composition graph and DXVK 2/3 Frame/proxy acceptance. It uses authored inputs and UI by default, requires no captured game assets, and never launches or modifies WoW. Supply explicit backend paths when the local client is not configured. Optional `--capture-prefix` imports only native scene/depth from a local capture; `--captured-ui` adds a local UI shader. The report separates GPU timestamps, CPU submission, structural draw/allocation counts and numerical output checks. Sky, expansion and stencil cases exercise the conservative fallback.

`tests/verify_hdr_lifecycle_gpu.py --cycle-hdr --proxy renderer/frd9.dll` exercises Windows HDR toggles with live resources and restores the original Windows state. Supply `--dxvk2`, `--dxvk3` and an isolated `--output` directory.

`tests/verify_hdr_integration_gpu.py` checks both Present paths, legacy buffers, captured UI shader/state restoration, graded transfer, MSAA, Reset/resize and alternating scene/UI generations. Supply the two backends, `--captured-ui` from your client, and an isolated output directory. Captured game shaders are never committed.

`tests/verify_hdr_frame_gpu.py` checks actual GPU composition, headroom, legacy expansion, adaptation timing and resource loss. Its `--benchmark` option measures post passes with GPU timestamps at 1080p/1440p/4K; it does not measure full-client FPS.

Two-display movement is deferred because this workstation has one physical display. Representative in-game day/night/interior visual tuning, Alt-Tab/hotplug and full-frame performance acceptance still require a client session. Wine/Linux, macOS and exclusive fullscreen are outside this phase's verified support.
