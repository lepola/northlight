# Point-transmission GPU regression

Recover the unfinished standalone point-transmission oracle on the existing independent emitter PR head. No production source or shader changes are required. The oracle uses the real cube rasterization and local-shadow bytecode to check authored alpha transmission and restored sampler state on a hidden device.

The x86 test executable builds and DXVK 3 passes 49,152 checked pixels, exact 20/64 mask coverage and sampler restoration. A deliberately incorrect linear-filter control produces 2,412 errors and is rejected. Production shader/manifest hashes are verified before execution. No client assets or game session are required.

Reproduce with `python tests/test_point_transmission_gpu.py --run-gpu --backend <32-bit-d3d9.dll>`. This is an authored production-shader regression, not complete Halls of Reflection, emitter-catalog, camera/FPS or other-backend acceptance. The PR remains draft.
