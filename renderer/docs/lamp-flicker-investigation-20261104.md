# Stormwind lantern flicker: video investigation

Input: `2026-11-04_13-36-50.mp4`, five seconds, 1920x1080 at 24 fps.
Video SHA-256: `3feecae2b237993a4277bc8f5cd8a777f0d973146dbeb151824f7ce5565e3f20`.
The clip shows two standing lanterns beside a Stormwind wall during camera movement. It supplies the displayed image, not individual light, depth, material or shadow buffers.

## Version evidence

The local renderer log last written at 13:37 records source identity `d1e4423db8aee534ac114af2935d1c4de910422aa654a320361923c7852eb093`, built at 09:40:33 UTC. This is the renderer in installers `.1` and `.2`; it predates the city-lamp corrections committed at 15:23 and the later PR #98 validation fixes. The video filename is consistent with that earlier session, but a filename alone does not prove which binary recorded a video.

At investigation time, the installed DLL already has the newer `.3` source identity `1fbbbf318ad5cfa8f3e970d0ba19d737b9cac8e1831bd8729331c5f20cda750c`. The rebuilt repository DLL has identity `3ebb2f1923c57c45c8eeed06da3e5092912e9fd3d2c1e98156c3cef997b1cdb0` and includes the PR #98 follow-up. Neither DLL nor `Wow.exe` was replaced during this investigation.

The [city-lamp fix](lamp-shadow-fix-20261104.md) removes the obsolete sunlight-relative gate on lamp shadow correction. The [PR #98 follow-up](pr98-validation-20261104.md) restores deferred material capture at depth changes and prevents shadow-bounds scheduling starvation. These are relevant existing corrections, not proof that this particular five-second clip has the same cause.

## Additional regression

The placed-lamp GPU stand previously checked live luminous-surface ownership with a fixed identity view. Extend `replayProof` in `tests/m2_lamp_self_shadow_gpu.cpp` to use model-local vertices at actual city coordinates and 180 rotating/translating camera palettes. Every pose must retain exactly the same luminous exclusions and metal draw ranges. Restore the original mesh and palette before the existing cube readback comparisons.

This exercises the production CPU ownership/deformation path inside the GPU stand. It does not render an entire new image at every pose. The existing GPU readbacks still require exact source ownership, retained foreign geometry, preserved metal, indexed/UP and list/strip behavior. No renderer code, tolerance, budget, cache revision or skip annotation changed.

## Results

- 32 stock, active-mod, renamed and scaled fixture runs pass on native D3D9 and DXVK 3. Five replay layouts per run, each with 180 poses, give 28,800 additional ownership checks with unchanged ranges. The existing cube pixel comparisons pass.
- The current direct-light shader passes 1,080 camera/animation/attachment cases on DXVK 3, including 936 views with the bulb offscreen. Maximum RGB error is `5.96046e-08`; the wrong-view and stale-pose negative controls still fail as required. All 30 native range cases also pass.
- The current multi-source correction passes 360 cases / 297,480 checked pixels on DXVK 2, with maximum error `1.335144e-05`. Selection limits, fades and transmission controls pass.
- The full city-lamp suite passes all 22 native D3D9/DXVK 3 scenarios: seven city fixtures, daylight hard/soft Stormwind shadows, real weighted-character and independent-obstacle controls, and the original physically blocked orc-floor controls.

Local evidence: `out/light-flicker-20261104/placed-camera/test-output/20261104-173551/` and `out/light-flicker-20261104/lighting-check/test-output/20261104-174041/`. Video, frames and proprietary client fixtures remain ignored local inputs.

The exact reported flicker has **not been reproduced on the current renderer**. These results rule out the tested moving-camera ownership and source-lighting failures; they do not certify the recorded scene's material, streaming, complete caster set or frame timing. No additional production fix is claimed from this video alone.
