# Native grass shadow capture

## Report and cause

The client had GrassShadows, ActorShadows and ActorShadowDetail enabled. Sampled frames contained DetailDoodad candidates but no snapshot attempts or accepted grass draws. The generic blended-model rejection ran before grass material validation. The stock DetailDoodad pixel shader also has no TEXKILL: pure alpha blending could never satisfy the former cutout-only gate.

## Change

Allow only the verified native DetailDoodad VS/PS pairing and ordinary SRCALPHA/INVSRCALPHA/ADD blending. The complete pixel bytecode identity table covers all 16 stock PS permutations (10 distinct byte sequences). Other blended actors, additive vegetation and unknown programs retain conservative rejection. Native alpha-test cutoffs remain intact.

For a verified blended material without alpha testing, shadow mode 3 multiplies texture alpha by the existing native distance fade and applies UV-anchored 4x4 Bayer coverage. Zero alpha never casts; partial opacity fades through coverage instead of becoming a solid rectangle. Both directional and local shadow pixel shaders use the same rule. Mode 2 keeps its existing vertex-only semantics. Native vertex/pixel programs, geometry, color, UVs and draw order are unchanged. The original 64-yard, 32-draw, 8,192-vertex and 512-KiB policy was retained by the initial alpha fix. Dense-field follow-up separates the per-draw and per-frame limits; see grass-field-budget-20261030.md. Grass does not enter actor GI/readback work. A separate grass blend-rejection counter makes future material failures observable.

## Functional validation

The new offscreen x86 production-renderer oracle calls the actual registerShader, registerPixelShader and captureUP path with native shaders extracted read-only from the tester's stock MPQs. Authored geometry and a binary-alpha texture isolate the shadow result; the live world cache and the game are not used.

Native D3D9 and DXVK 3 each pass all 12 vertex/16 pixel variants, 384 directional/local mask checks, legacy alpha modes 0/1/2, two native alpha-test capture controls, unknown/no-mask/disabled rejection, opacity 0/0.5/1, fully transparent texels, native state preservation and 16 additive-blend rejection controls. Optimized/debug CPU tests and Linux ASan/UBSan cover native fade/projection/UV behavior, conservative bounds, budget limits and unknown/material rejection. Existing alpha-mask and vegetation ordering checks pass. Standalone Windows x86 DLL build passes, with regenerated shader source/binary/header manifests; unrelated shader payloads remain unchanged.

This proves capture and shadow-map mask generation on the tested drivers. Actual vegetation appearance, low-resolution dither quality, other GPUs and the older Crystalsong tree report still need visual acceptance; they are not reported as newly verified gameplay. No FPS or GPU-duration measurement was made by the new GPU oracle. Embedded CPU timers have been removed from the grass correctness test.

Reproduce with tests/test_grass_shadow_gpu.py, test_detail_doodad_shadow.py, test_grass_receiving_order.py and test_shadow_alpha.py, plus scripts/build_renderer.py. Configure local client/tool paths using northlight.local.ini; no client shader bytecode is committed.
