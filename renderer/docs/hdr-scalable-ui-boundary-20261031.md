# Scalable UI boundary correction

The player-frame background and portrait could be drawn before HDR finishing. The boundary recognized native physical-pixel text such as Ctrl+R but missed the earlier UI shader's logical coordinates. The captured 3840x2160 transform has X/Y scales `2.29469/4.07946`, rather than `2/3840` and `2/2160`. World composition could consequently process or replace those early UI pixels.

The shared boundary now accepts full-window, axis-aligned orthographic UI in either coordinate system. It retains the aspect/origin checks, full depth range, disabled depth writes, non-world shader classification, main-target validation and actual sampled-texture ownership checks. Scene/death/glow render-target inputs cannot establish this boundary. Both HDR admission and the ordinary effect boundary use the same geometry predicate; no client shader hash is added.

Portable tests reproduce the observed transform, retain the old pixel-only gate as a counterfactual, exercise multiple logical scales and reject malformed transforms/world depth. The extracted production HDR methods additionally check atlas versus scene inputs and retain the logical-copy negative control. Windows tests and Linux ASan/UBSan pass.

The complete offline graph passes on DXVK 2.7.1 and 3.1.1: nine composition scenarios, 2782 Frame cases and 43 sun cases per backend, plus production HDR/SDR/native-MSAA proxy controls. Tested and installed DLL SHA-256: `382c39655b6e1545488fee2173aa82778ee1d9267a8d1a4de87a7f428456afd5`. Renderer settings retain SHA-256 `3b9c1ee874711e77c08f679914a2edf58cfac1bc18fefe7853d57b2baa251f84`.

The subsequent client log identifies the earlier `orthographic-ui` boundary and the account owner confirms that the interface is repaired. Bounded Diagnostics triage records the matrix and sampled-input eligibility; it remains absent with Diagnostics off and stops after 64 unique entries.

Private evidence: `.local/hdr-ui-background-20261031`, including the pre-fix trace, installed-byte identities, offline acceptance and sanitizer output. This is a correctness fix, not an additional accepted performance optimization. The subsequent [camera-dependent background correction](hdr-sky-admission-20261031.md) is separately tested and confirmed by the account owner.
