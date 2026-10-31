# Stable HDR sky admission

Small camera rotations could change the brightness of the background behind mountains. The supplied captures 2 and 3 differ by about 0.36 degrees in pitch. Their fog constants and resolved fog are nearly unchanged, but their linear world-composite outputs already differ before material grading and final HDR exposure. This rules out final exposure as the sole cause.

Previously, the HDR sky target was initialized from the celestial callback only after an owned sun or moon passed the visibility test. That made capture start and the treatment of earlier native sky layers depend on source visibility. A layer could be decoded after SDR blending in one view and replayed in linear light in another.

The first native sky draw on the main backbuffer now initializes the HDR sky target, before celestial ownership and visibility decisions. Later layers reuse it. World, opaque horizon, UI and offscreen render targets cannot establish this sky path. Bootstrap failure retains the native path and permits a later retry. The existing raw-depth horizon handling and UI boundary remain intact.

The CPU contour executes the extracted production admission method. It checks first-layer initialization without a visible source, one initialization per frame, target/depth rejection and failure recovery. Removing first-layer initialization reproduces the expected assertion failure. Optimized Windows and Linux builds pass; Linux ASan/UBSan pass. The complete offline HDR acceptance passes on DXVK 2.7.1 and 3.1.1: nine composition workloads, 2782 Frame cases, 43 sun cases and the HDR/SDR/MSAA8 production proxy matrix per backend.

Explicit scene captures additionally export the actual HDR base and record whether it is encoded. The renderer capture marker records sky validity and inline-base selection. These readbacks are confined to requested diagnostic captures.

Tested and installed DLL SHA-256: `c3664e15b22abb1394905998289d14e60675d31ac660e8aac0bcc3866ec63931`. Renderer settings retain SHA-256 `3b9c1ee874711e77c08f679914a2edf58cfac1bc18fefe7853d57b2baa251f84`. The account owner subsequently confirmed that the sky is repaired. Private captures, logs, build output, negative controls and acceptance remain under `.local/hdr-camera-20261031` and the client diagnostic directory; they are not distributed.

This is a correctness fix. It adds no accepted performance optimization and establishes no new whole-client FPS result. Representative day/night/interior coverage, the HDR overhead target and physical two-monitor migration retain their previously documented limits.
