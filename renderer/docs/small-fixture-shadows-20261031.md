# Preserve small fixture shadows at the character triangle threshold

The Zangarmarsh report loses the small sections of ZangarLamppost01 at a minimum of 100. M2 fixtures share skinned shaders with characters. Prove that every captured vertex uses one bone before exempting a small section; for the audited single-influence shader, read only the consumed index lane. Multi-bone sections retain the threshold. A separate bounded render-thread cache avoids sharing mutable preparation state. Existing snapshot/admission limits remain.

The production capture oracle retains a 99-triangle rigid section at 100, rejects its multi-bone variant at 100 and admits that variant at 99. The independent x86 DLL builds, and the production capture/cube regression and fade audit pass on native D3D9 and DXVK 3. No gameplay replay or FPS result is inferred.

Animated world-light positions and FGA1 cache metadata belong to #114 and combined #98. They are not imported into this local-shadow branch.
