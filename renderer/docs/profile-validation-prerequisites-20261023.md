# Personal profile validation and missing historical prerequisites

This follow-up resolves two of the three automatic checks without a PASS in the 23 October integration gate. It preserves personal settings, the original historical assertions and every dependency binary pin. The historical contact-normal capture remains unavailable. The game was not launched and the renderer DLL was not rebuilt.

## Personal configuration: resolved

`test_client_config.py` previously required the installed profile files to match the repository byte for byte. That contract contradicted both the package builder, which reads factory files from `client-config/`, and the installer, which preserves personal profiles.

The test now compiles a small validator against the actual `NorthlightCelestialProfiles::load` and `NorthlightRegionalShadow::load` implementations. Both the factory and personal files must pass the renderer's encoding, syntax, key, range and duplicate checks. Different valid personal values are accepted. Missing and malformed files still fail.

Factory profiles also must agree with the compiled settings-reset text. The test verifies that packaging takes these files from `client-config/` and marks both as preserved during installation. A legal edit to a factory value fails until its embedded reset default agrees. The explicit developer export checker retains its strict byte comparison and optional `--update`; neither is invoked to overwrite the user's settings.

The real personal shadow profile omits the Elwynn override, so the production parser returns its existing 928-unit fallback. The factory profile retains 2048. This difference is reported as a personal override rather than an error. Both files remain unchanged.

Validation:

- Five Windows script suites pass: profile validation, actual-addon delivery, generic installer, Windows installer and zone-tuner export. The installer suites retain their existing six and two platform/privilege subcase skips; these are not hidden.
- The profile validator passes real Linux ASan/UBSan with the same read-only factory and personal inputs.
- Twenty-four malformed or missing inputs are rejected in temporary copies. Positive cases include UTF-8, UTF-8 BOM, UTF-16LE, changed values, omitted regional overrides and empty override files using production defaults.
- The existing celestial regression passes optimized and ASan/UBSan runs, including malformed inputs, energy, actual zone IDs, interpolation and temporal transitions.
- A copied-validator negative control that bypasses the factory/reset comparison fails the same regression fixture. The production validator passes.
- Hashes of installed DLL/configuration inputs and tested source files remain unchanged across each run. No renderer or profile default was edited.

The test now explicitly declares its Zig compiler prerequisite. Missing tools cannot produce a false PASS. These are focused follow-up results, not a claim that the entire integration suite was rerun after the test change.

## Historical contact-normal capture: original data unavailable

The unchanged `test_contact_normal_endpoints.py` requires four original files below `<NORTHLIGHT_ARCHIVE>/renderer/runtime-diagnostics-0.3.69/`:

- `capture-1-constants.f32`
- `capture-1-distance.fgr`
- `capture-1-normals.fgr`
- `capture-1-light.fgr`

The search inspected 1,284 readable local ZIP/TAR archives, 60 constants-file locations, all reachable Git object paths, LFS listings, 21 official releases, public searches and workflow records. The first public release archive was also downloaded and verified against its published checksum. None contained the historical capture. One unrelated corrupt Zig ZIP was recorded as an inspection error.

Public repository history begins at version 0.3.166, after this capture, and already excludes runtime diagnostic archives. The user has no additional archive location. The original data therefore remain unavailable in the searched sources; this is not proof that no private copy exists.

No newer capture, generated scene or synthetic image was substituted. The original angular-error, seam-shadow and smoothing assertions are unchanged. This check remains unexecuted until the original four files can be obtained from the archive owner. A new independent regression cannot retroactively certify this historical capture.

## StormLib: reproducible official source recovered

The old source-tree hash `e9d2ec908c1419beb1b142ea2d76103a36cdd070ae621bc3baebf41ec1a11be4` was not found in 710 official commits, 419 distinct relevant source trees or the available local copies. It is not represented as recovered.

The authentic official [commit 44ebfbfc109d76e2a85bbd5d8b0c949df7e65c6f](https://github.com/ladislav-zezula/StormLib/commit/44ebfbfc109d76e2a85bbd5d8b0c949df7e65c6f) instead reproduces every originally pinned binary byte for byte with the unchanged build commands and Zig 0.15.2:

| Target | Unchanged binary SHA-256 |
| --- | --- |
| Windows | `dc1e133965432b55e0a016d0302399bfa57770c423d93db76e7dcfa45fd41b15` |
| macOS | `80e591bac7e8a85958fe25ef640a5ff83fcc0a2915c9e27e511e85b91be26859` |
| Linux | `c88159a4d5c4a570daf554648253d9dd45ad7e9bdb55b251bfd9cdffaa0eb689` |

The package pins now identify that exact public commit and its archive URL, root directory and SHA-256 `f41099372958a149134751090047105d75020f5c0ad0003505151b4b5d6c2fac`. The verified source-tree pin is `cefb234d2ded2e971f856e02f906f93229ba5a18f6b5c02ba9a019ad4f02ca37`. Only the source provenance changes; the three existing artifact pins remain intact. This is neither a library upgrade nor replacement of the expected binaries with new outputs.

The source hasher previously sorted host `Path` objects, which fold case on Windows. It now sorts the original relative path components case-sensitively on every host, keeping every previously checked path and content hash. A fixed known-answer fixture distinguishes Windows case folding from canonical ordering and rejects changes to content, file names and file membership. Windows source-pin and known-answer checks pass.

The verified tree is restored to the repository tools directory and the already configured tools location. No machine-local INI or installed game file is edited.

The complete repository test passes on Linux with Zig 0.15.2:

```sh
python3 scripts/run_tests.py --require zig,stormlib-src --jobs 1 test_stormlib_build.py
```

The runner reports one PASS, zero failures, zero timeouts and zero missing-prerequisite skips. Seven of nine unittest cases pass; the two original arm64-Mac-only runtime cases remain skipped (`test_dylib_writes_and_reads_an_mpq` and `test_art_layer_with_new_dylib_matches_known_layers`). All six artifacts, covering normal and empty-cache builds on Windows, macOS and Linux, match the three original binary pins. ABI checks and rejection controls pass. The three tracked build/test inputs remain byte-identical throughout the run, and all 310 source files still match the verified official archive manifest afterward.

The Windows known-answer test also rejects the old host-dependent hasher in a negative control. No expected binary checksum, ABI requirement or cold-rebuild assertion was relaxed.

## Evidence and remaining prerequisite

Evidence paths below are relative to `northlight-diagnostics/test-prerequisites-20261023/` in the client workspace:

| Evidence | Path |
| --- | --- |
| Five Windows suites and preserved inputs | `config-windows/validation.json` |
| Actual ASan/UBSan and factory negative control | `config-linux/validation.json` |
| Historical capture search inventory | `contact/search-report.md` |
| Official StormLib provenance and artifact equivalence | `stormlib/report.md` |
| Full normal/cold-build gate and original hashes | `stormlib/full-test/summary.json` |
| Verified evidence copies and source preservation | `stormlib/full-test/copy-and-source-verification.json` |

The remaining check requires the four original version 0.3.69 capture files listed above. Its test is unchanged and is not reported as passing. These focused results close the configuration failure and StormLib prerequisite gap; they do not replace the earlier full-suite coverage record or its other platform-specific subcase skips.

Raw search, provenance and validation evidence is retained under `northlight-diagnostics/test-prerequisites-20261023/`.
