# Portal coverage at the effects boundary

The [2026-11-04 follow-up](spell-prelit-visibility-20261104.md) extends this
alpha-only design with additive RGB, one conditional packing pass and prelit
WMO composition. The measurements below describe the original alpha path.

The Stormwind portal centre uses three unlit alpha-blended, non-depth-writing
layers. The winning local `stormwindmageportal01.m2` has material flags/blend
`21/2` for `twistednether2.blp`, `twistednether8.blp` and `twistednether4.blp`.
An early spell drain (including the native glow copy) draws these into Scene
before Northlight composition. Scene depth still describes the room behind the
portal, so its lighting, AO and fog can become visible over the portal.

`src/proxy/effect_coverage.h` records alpha coverage for identified unlit M2 and
particle overlays in the same draw,
using a free MRT and the complete original pixel program. Source colour,
alpha/depth tests, blending, geometry and native depth writes are preserved.
Straight and premultiplied alpha accumulate `A = alpha + (1-alpha)*A`;
subsequent opaque depth-writing draws erase coverage where they pass. Ordinary
lit alpha receivers, including banners and projected shadows, reduce existing
coverage proportionally instead of becoming exempt from lighting and fog.
The shared raw draw path and early spell replay callback both participate.
Post-composition replays and extension capture draws do not participate.

Final world, unlit, material and legacy AO compositors attenuate their correction
by `1-A`. Fully covered pixels retain Scene; partial coverage scales the
correction continuously. SDR/HDR and combined HDR material composition use the
same rule. Sampler 7 and constant 79 are rebound immediately before final
composition; the normals stage's use of c79 is unaffected.

There is no extra geometry draw or fullscreen pass. Consecutive early-replay
layers retain the MRT binding until `spellSync`; native shaders and colour-write
state are restored after each draw. Opaque-only frames allocate nothing and issue
no coverage device calls. The mask is allocated on the first admitted overlay,
matches RT0's format/sample count, and is retained for reuse. MSAA adds a matching
multisample surface and one resolve. Clear/copy writes to the tracked target,
frame end and device reset invalidate coverage. Unsupported shader/state/resource
cases retain native drawing. Sampled `EFFECT_COVERAGE` lines report draws, MRT
bindings and validity.

Native D3D9 MSAA failed the exact native-pixel control when MRT capture was
enabled. It therefore keeps the original path without coverage. MSAA coverage is
enabled only for the packaged DXVK backends. This limitation is explicit; the fix
does not certify native-D3D9 MSAA portal composition.

## Verification

`tests/test_effect_coverage_gpu.py` exercises real GPU draws and all nine changed
production shader entries. It checks SM2/SM3, straight/premultiplied alpha,
three-layer accumulation, depth rejection, alpha-test rejection, later opaque
overwrite, coexistence with an existing MRT, state restoration, target overwrite
invalidation and frame reuse. Native colour bytes are compared exactly with an
unmodified draw. Unmasked compositors supply negative controls. A three-layer
replay binds once; an opaque-only control makes 10,000 submissions with a null
device, proving no capture calls or allocations are attempted.

Run on a free GPU (these are correctness checks, not timings):

```powershell
$env:NORTHLIGHT_TEST_OUTPUT_DIR = 'D:/dev/Projects/Northlight/.local/portal-pipeline-20261103/dxvk3'
$env:NORTHLIGHT_TEST_D3D9 = 'D:/Games/335_engb_mod/renderer-backends/dxvk/dxvk_d3d9.dll'
$env:NORTHLIGHT_TEST_MRT_MSAA = '1'
python tests/test_effect_coverage_gpu.py
```

For DXVK 2 use `renderer-backends/dxvk2/dxvk2_d3d9.dll`. For native D3D9 use
`C:/Windows/SysWOW64/d3d9.dll` and set `NORTHLIGHT_TEST_MRT_MSAA=0`; its MSAA
cases verify the unchanged compatibility path.

Local evidence is in `.local/portal-pipeline-20261103/`. The renderer translation
unit compiles for Windows x86. Full shader rebuilds passed the existing slot and
freshness gates; only the nine intended entries changed bytecode. The final
combined DLL passed HDR/SDR/MSAA and package checks in the coordinated capture
review. These coverage tests use authored inputs, not a captured game frame;
in-game screenshot acceptance and FPS measurement remain to be checked.

Original portal correctness runs (2026-11-04; native run 2026-11-03):

| Backend | Native cases | Composite cases | Negative controls | MSAA coverage |
| --- | ---: | ---: | ---: | --- |
| Native D3D9 | 256 | 1,152 | 19,200 | Disabled; unchanged native path verified |
| Packaged DXVK 2 | 256 | 2,304 | 38,400 | Enabled, verified at 2x |
| Packaged DXVK 3 | 256 | 2,304 | 38,400 | Enabled, verified at 2x |

All runs passed. Native colour comparisons were byte-exact. Maximum composite
error against expected coverage attenuation was `0.000475407` (limit `0.003`).
Logs are `native/gpu.log`, `dxvk2/gpu.log` and `dxvk3/gpu.log` under the evidence
directory. Source wiring assertions passed on both final DXVK runs. The renderer
object compile, repository layout check and whitespace checks also passed.

The subsequent lit-receiver regression run adds banner and projected-shadow
counterfactuals: native D3D9 passes 512 native cases, 2,304 composites and 30,720
negative controls; packaged DXVK passes 512, 4,608 and 61,440 respectively.
Native pixels remain exact and the maximum composite error remains `0.000475407`.
Evidence: `out/capture-review-20261104/performance/validation-summary.json`.
