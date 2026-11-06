# Validated optimization test installer

Version: 0.3.216-test.20261031.2. The batch contains 31 new validated changes; the original target of 60 new changes is not reached. Four independently completed correctness fixes are reconciled before packaging and do not pad the optimization count.

- ZIP bytes: 19618967
- ZIP SHA256: 46251a2428f1c3d16ea94bacc7b82b4c94d3cc05bfa8342d80ecd13578c14097
- DLL SHA256: 1099c1d40be633e107a0e6ef9702345bafaa9fae7e1bec98cc8a7015b0959ace
- Embedded production identity: a0fc947391be2bff0a221d913ebc0f30c3f86201cb645bba737d226255181a11

The installer payload passes verify_packages.py. It includes the existing settings and opt-in HDR implementation together with the validated optimization batch; see settings-hdr-integration-20261031.md for ownership, exact test scope and the five controls retained only in the combined PR. The new in-progress HDR pipeline is separate.

The optimization-only CPU suite is 233 PASS with zero failures/timeouts and 169 explicit prerequisite omissions. Final focused CPU/Windows geometry and GPU checks cover the four reconciled correctness fixes; final HDR presentation repeats 5,882 checks across 24 DXVK 2/3 modes. No game cache is packaged or used for validation. Existing configuration files and the generating cache remain preserved on DLL-only installation. The one discovered legacy implicit CPU benchmark is excluded from performance evidence and now requires an explicit flag.
