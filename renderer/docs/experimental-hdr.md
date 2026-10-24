# Experimental HDR presentation

Set ExperimentalHDR=1 in the Renderer section and restart. HDR requires a supported packaged DXVK backend, CommandStream=1, windowed presentation and a working HDR output. A failed capability check leaves SDR active and records the reason in the renderer log. The default is off.

The client continues to see its original 8-bit logical backbuffers. Northlight converts the completed legacy world frame to linear scRGB and converts supported UI draws separately. HDRPaperWhite controls scene white, HDRUIWhite controls UI white, and HDRMaxNits, HDRContrast and HDRExposure control the bounded artistic transfer. This is a presentation conversion of the legacy scene; it does not recover highlights already clipped by the original renderer.

Logical backbuffer counts, readbacks, swapchain queries, cooperative level, Reset/resize, original shader bindings and untouched constants are covered by the isolated integration oracle. The rare-font boundary check uses a recording device to keep world effects before UI; that host check does not rasterize water.

Run tests/verify_hdr_integration_gpu.py with the two plain DXVK DLL paths, a UI pixel shader captured from your own client, and an output directory. All generated test shaders, DLL copies and logs stay under that output directory. Captured game shaders are never committed. The desktop HDR toggle, Alt-Tab, overlays, Magnifier and visible monitor output still require manual testing.

MSAA follow-up: logical legacy buffers and FP16 presentation preserve sample type and quality. World conversion and explicit FP16 readbacks resolve multisampling first. Stream backbuffer metadata refreshes sample settings across Reset; unsupported HDR allocation falls back to SDR while retaining the requested legacy samples. The oracle includes 2x/4x/8x coverage, sample-changing Reset, full-resolution readback and logical-target rebinding after offscreen rendering. Linux and macOS remain unverified.

The learned or stock UI proof remains authoritative with the master effects switch off. Unrelated affine draws must not start HDR conversion merely because effect preparation did not establish a world frame. Shader classification precedes the matrix/state probes; the UI proof uses the vertex shader identity independently of the pixel shader variant.

HDR-only frames allocate the color copy lazily. Depth, AO textures and effect shaders are created when effects need them; already-used effect resources remain retained. At 3840x2160 this defers 63.28125 MiB of texture payload before effects start, without changing resolution or samples. This is allocation accounting, not measured process-memory or FPS improvement.

For intermittent UI trails, diagnostics include the first eight logical-backbuffer copies made after the UI boundary and a periodic HDR state/coverage summary. These observations identify copy ordering; they do not change copy semantics. The GPU oracle alternates frame colors and UI inputs across Device/SwapChain Present without rebinding RT0. Ordinary movement, camera turns, effect toggles, map travel and desktop focus changes still require a representative in-game check.
