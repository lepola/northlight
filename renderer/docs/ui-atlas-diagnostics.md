# UI atlas diagnostic components

The bounded upload-proof cache hashes replayed 64-pixel tiles in 512-pixel BC1/BC2/BC3 atlases. It records attempted bytes and the exact backend failure, retains no texture bytes or COM references, and drops evidence on resource destruction. Derived surface locks resolve to their owning texture. Unobserved pass-through writes invalidate known tiles. Collection defaults off.

The capture helpers compare CPU block decoding, raw GPU texels and filtered samples, then record the actual UI draw range, UVs, shaders, constants, samplers and differences from mirrored state. Output contains client data and belongs only in ignored diagnostic folders. The standalone branch supplies components and a synthetic harness; it does not bind these helpers to the game's F12 control. Upload proof covers observed CPU uploads, not every possible GPU texture writer.

Run tests/test_atlas_upload_proof.py and tests/test_atlas_low_memory.py for bounded-cache and allocation-failure behavior. On Windows with a configured plain DXVK backend, run tests/validate_ui_atlas_gpu.py. The harness checks direct and streamed rendering without launching the game. Its optional --proxy mode requires a separately configured copied proxy/backend next to the helper executable. All captures and compiled fixtures remain in ignored out directories.
