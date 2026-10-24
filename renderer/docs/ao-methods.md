# AO methods for the D3D9 Northlight renderer

Set these keys in `[Quality]` in `northlight-quality.ini`, or use Lighting & AO
in the Northlight menu and Apply. Changes apply between frames:

```ini
ContactAO=1
AOMethod=GTAO
AOQuality=High
AOStrength=100
AORadius=200
```

| AOMethod | Implementation |
| --- | --- |
| SSAO / 0 | Original Northlight 8-tap SSAO. Default; unchanged at strength 100 and radius 200. |
| HDAO / 1 | AMD AOFX-style paired valley detection, 16 pairs. D3D9 adaptation. |
| HBAOPlus / HBAO+ / 2 | D3D9 adaptation of the NVIDIA HBAO+ coarse kernel: 8 directions, 4 radial steps, normal bias, squared-distance falloff. |
| GTAO / 3 | Cosine-weighted horizon integration, 4 slices and 4 paired steps. |

Names are case insensitive. Invalid values retain the previous value and produce a
QUALITY warning. `AOStrength` is 0..200 percent; 100 preserves the existing
strength scale. `AORadius` is 10..1000 hundredths of a yard; 200 means 2 yards.
`ContactAO=0` disables any selected method and its filter; bloom remains enabled.

`AOQuality` accepts High/0, Medium/1 and Low/2. Every preset keeps High, preserving
the existing sample counts. Lower levels are explicit choices that reduce depth
taps; SSAO ignores the quality setting.

| Quality | GTAO slices x paired steps | HBAO+ directions x steps | HDAO pairs |
| --- | --- | --- | --- |
| High (default) | 4 x 4 | 8 x 4 | 16 |
| Medium | 3 x 4 | 6 x 3 | 8 |
| Low | 2 x 4 | 4 x 4 | 8 |

Lower counts can increase fine noise and shimmer during camera motion. The blur
and half-resolution targets are unchanged. Quality selection is saved on Apply;
it does not use the method selector's unsaved live preview. No measured whole-game
FPS gain is implied by the lower sample counts.

All methods use the existing half-resolution targets and the depth/normal-aware
AO filter. Sky and water receivers are excluded. Each method runs in both the
world-GI and fallback paths; the fallback keeps the original simple colour bounce.
GI continues to supply colour bounce when the world renderer is ready.
The advanced methods need one extra alpha-only draw in the fallback path to
retain its original colour bounce within the Shader Model 3 instruction limit.
The startup log records the selected method, strength, radius and enabled state.

These are D3D9 adaptations, not the complete NVIDIA HBAO+ SDK or Intel XeGTAO
pipelines. There is no DX11 deinterleaving, depth-mip hierarchy or extra temporal
AO pass. Each quality level has fixed sample counts. Quality and GPU cost still need
comparison in actual gameplay; a synthetic device test is not an in-game FPS test.

VXAO is not implemented: NVIDIA VXAO requires voxelizing scene geometry and
tracing the resulting volume, rather than reading the screen depth buffer.
It cannot be enabled through this screen-space method selector. An unsupported
`AOMethod=VXAO` is rejected instead of silently selecting a different algorithm.

Validation: `python tests/test_ao_methods.py` compiles an independent x86 hidden
D3D9 test device and exercises the production bytecode through DXVK 2. It checks
flat and tilted planes, concave corners, a separated foreground silhouette,
sky, water and strength zero in both composition paths. It also checks named
and numeric INI values, rejection of unsupported values, distinct kernel
outputs, and byte-identical original SSAO/filter/composite payloads.

Algorithm references:

- [AMD AOFX HDAO kernel](https://github.com/shadercoder/AOFX/blob/master/amd_aofx/src/Shaders/AMD_AOFX_Kernel.hlsl)
- [NVIDIA HBAO+ coarse kernel](https://github.com/NVIDIAGameWorks/HBAOPlus/blob/master/src/shaders/src/CoarseAO_PS.hlsl)
- [Jimenez et al., Practical Real-Time Strategies for Accurate Indirect Occlusion](https://www.activision.com/cdn/research/Practical_Real_Time_Strategies_for_Accurate_Indirect_Occlusion_NEW%20VERSION_COLOR.pdf)
- [Intel XeGTAO implementation](https://github.com/GameTechDev/XeGTAO)
- [NVIDIA VXAO requirements](https://developer.nvidia.com/vxao-voxel-ambient-occlusion)

Third-party notices for the adapted kernels are in `licenses/ao-methods.txt`.
