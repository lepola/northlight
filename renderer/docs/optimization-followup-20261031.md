# Shadow filter coordinates follow-up (2026-10-31)

Replace per-tap flattened coordinate reconstruction with nested rows in the shared directional PCF, actor blocker search and actor variable-reach tent filter. Preserve sample order, weights, the 81-tap cap, water exclusion and all enabled quality sizes. Regenerate this independent branch's WorldLighting, ActorShadow and dependent transition payloads from its own source; do not import combined material shaders.

The actual shadow helper/ActorShadow GPU comparisons each pass 576 exact FP16/FP32 and MSAA 0/2/4/8 cases on native D3D9 and DXVK 3, with zero unsupported cases or duration queries. Wrong-output controls are rejected by both new filter oracles on both backends. The independent x86 DLL builds. These checks establish filter equivalence, not complete game visual acceptance or an FPS gain.
