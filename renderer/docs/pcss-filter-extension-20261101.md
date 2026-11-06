# PCSS extension — 1 November 2026

The renderer now applies contact-hardening filtering to both directional cascades and every ready selected local-light cube. Existing fine actor PCSS remains enabled for all admitted moving casters, including other characters and captured grass. Grass still requires `GrassShadows=1` and `ActorShadows=1`; filtering does not add uncaptured geometry, increase capture range, or give an unselected light a shadow map.

`SoftShadows=1` enables this filtering. `0` keeps the original directional/local PCF and a fixed-width actor filter; it skips the new directional and local resolve passes. Quality/Balanced/Performance defaults are `1/1/0`. The live menu control is **Shadows → Shadow filtering → Contact-hardening shadows**. It invalidates lighting history on change and releases the optional scratch when switched off, while preserving actor maps, fog tiles and cube caches.

## Pipeline

- `DirectionalShadow` resolves existing near/far maps at half screen dimensions. Sun and moon occupy separate R/G channels. `WorldLightingSoft` consumes them, retaining the original actor union, weather, material, baseline-alpha and coverage math.
- After the directional consumer, the same RGBA16F scratch is reused by each `LocalShadowResolve` / `LocalLightingSoft` pair. Only completed `slot.cube` publications are read; the existing per-light scissor bounds both draws. No extra caster submission, face refresh or duplicate light correction is introduced.
- Optional shader/target failure falls back to ordinary filtering. The scratch costs 3.96 MiB at 1920×1080, 15.82 MiB at 3840×2160. The directional resolve is separately visible as `ShadowFilter` in GPU profiling; local resolve and correction remain within `PointLight`.

Directional PCSS preserves all 25/16 baseline taps, searches 16 wider locations and grows a continuous tent to at most 4.25 texels (81 final taps). Receiver-plane correction determines blocker separation; the half-width scale is 0.04 world units per unit of separation. A sparse blocker miss retains base PCF. Local PCSS uses 4 baseline taps, 16 blocker taps and at most 16 final taps. It reconstructs ray distances from nonlinear cube depth, retains snapped cube-texel receiver-plane comparisons across seams, and uses an artistic emitter radius of 0.08 world units, independent of attenuation range. Maximum angular reach is four times the baseline.

## Validation

- Production shaders fit SM3 limits: DirectionalShadow 399 slots, WorldLightingSoft 236, LocalShadowResolve 470 (cube cost included), LocalLightingSoft 246. All 40 world binary/header/manifest entries validated. Original WorldLighting bytecode is unchanged.
- Existing actor PCSS is pixel-exact against the pre-change shader in 576 DXVK cases, including MSAA variants.
- Local receiver-plane GPU tests: 648 cases on each native D3D9 and DXVK backend; 41,640,480 lit pixels and 114,048 blocked pixels per backend, no failures.
- Authored directional depth maps cover near/far cascades, two slopes and three blocker separations. Contact output remains exact; larger separation widens penumbra. The production local submission test verifies Off/On/Off/On stability, changed soft-edge pixels, render-target restoration, unchanged cube contents and zero caster draws.
- Live Off releases only the optional scratch and invalidates lighting history. Simulated allocation backoff and unavailable optional shaders reproduce baseline PCF exactly on both backends; actor-map shaders and ready cube publication survive.
- Shared build with the Stormwind bounds optimization passed 960 proxy cases across DXVK 2/3 and SDR/HDR, including shader/constant restoration and reset/resize.
- Quality parsing/presets and actor-map wiring checks pass. The repository-wide quality-doc audit currently reports missing entries only for 13 weather settings being developed separately; the new SoftShadows entry passes that audit.

## GPU cost

Measured on NVIDIA GeForce RTX 4090 (10de:2684), 1920×1080 output, 960×540 filtering. GPU timestamps include the resolve **and** lighting consumer; eight measured blocks per mode after warm-up, alternating order, eight repetitions per block. The lamp stress cases publish an immutable authored cube to 1/18/64 ready slots and give every source full-screen coverage. Cubemap generation, actor filtering and total game-frame time are outside these measurements.

| Work | Native Off → On, median ms | DXVK Off → On, median ms |
| --- | ---: | ---: |
| One directional source, with consumer | 0.014 → 0.052 | 0.025 → 0.094 |
| One full-screen local source | 0.008 → 0.048 | 0.007 → 0.043 |
| 18 full-screen local sources | 0.126 → 0.902 | 0.139 → 0.805 |
| 64 full-screen local sources | 0.446 → 3.698 | 0.445 → 3.335 |

These are synthetic pass measurements, not Stormwind FPS. The all-lights stress case exceeds the earlier proposal's 0.5 ms target; that proposal included only 1–2 local PCSS lights. Actual cost depends strongly on screen coverage and light count. Performance preset leaves PCSS off. Moving-camera/animated-grass visual acceptance and an in-game capture remain to be done; no game DLL was replaced by this task.

Artifacts: `.local/pcss-extension-20261101/` (GPU checks and timing), `.local/rigid-bounds-20261101/` (combined DLL, source identity and proxy validation). The implemented scope was coordinated with chat `01a19774-6639-7923-bcfa-62ee6e6e2dd1`.
