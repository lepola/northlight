# Combined render recovery validation, 2026-10-30

The release combines bounded scene preparation/upload with correction of the premature HDR UI boundary. The pressure path admits the real scene at 307 MiB, retains its replay source, and survives repeated DEFAULT-resource loss without publishing partial geometry. The HDR correction prevents death/glow scene copies from being mistaken for UI merely because they reuse a learned vertex shader. See [memory policy and exactness](scene-entry-pressure-20261029.md) and [HDR ordering](experimental-hdr.md).

The shared CPU run reports 286 PASS, one failure and 140 prerequisite/manual SKIPs. The failure occurred after the mirror runtime, sanitizer and counterfactual checks passed: its parity check tried to overwrite a generated production header while Windows/WSL compilation held it open. The check now compares the generator's returned text without writing source files. Its complete rerun passes unchanged runtime and sanitizer controls. Final coverage is 287 validated passes and zero unresolved failures; no known-failure exemption was added. The two TSVs and original failure log are preserved.

All 18 memory backend/storage groups pass on native D3D9, DXVK 2 and DXVK 3, including actual 307/395/434 MiB entries, old-generation rollback, three real Reset cycles per scene, controlled 248 MiB recovery, interrupted replay, probe rewrite and exact material/raster comparisons. All 28 HDR DXVK modes pass 6,890 checks against the packaged DLL. The separate native direct-code HDR oracle passes 231 checks. The adapter rejects the native full proxy's RESZ capability check; this was not bypassed.

## Paired upload measurements

One verified executable, three ABBA rounds per backend, resident/spooled/spooled/resident order, six cold and six warm samples per path. CPU tests and the game were idle. The fixture uses modeled CPU pressure and synthetic upload data, not a running client or a rendered-frame benchmark. Numbers below are medians in milliseconds.

| Backend | Path | Cold CPU submission | Warm CPU submission | Cold upload wall | Warm upload wall | Cold Reset replay | Warm Reset replay |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| dxvk3 | resident | 8.739 | 4.808 | 9.612 | 55.034 | 1.360 | 1.260 |
| dxvk3 | spooled | 13.801 | 7.229 | 19.003 | 62.103 | 10.785 | 9.287 |
| dxvk2 | resident | 6.534 | 4.742 | 8.357 | 54.665 | 1.087 | 1.046 |
| dxvk2 | spooled | 10.895 | 7.035 | 16.686 | 60.651 | 10.619 | 9.018 |
| native | resident | 15.962 | 13.116 | 16.505 | 58.721 | 12.994 | 13.005 |
| native | spooled | 16.318 | 14.072 | 16.798 | 60.416 | 33.151 | 33.245 |

The file-backed path trades a small one-time packing/I/O cost for bounded address-space use. This does not establish a steady-state FPS difference. GPU timestamp intervals include CPU/Present gaps and cannot be reported as GPU busy time; native D3D9 timestamps were unavailable. Complete samples, ranges, commands, source hashes and backend hashes are in `combined-upload-replay-final-performance-20261030/validation.json` under `.local/review-completion`.

## Release identity

- Renderer source identity: `990e0a687af71158c5cd3634057ad4cb070a1c9882e269e8feaf61a3590fe8ae`.
- DLL SHA256: `300033e02fac396ee0e91dc851ff99dd3bc71aea070228ced31933fe2863ce69`.
- Installer: `releases/Northlight-0.3.216-test.20261030.2-Windows.zip`, 19,618,787 bytes.
- ZIP SHA256: `a986fba9213cd52b42abd819694acda2e9bb3d1ce4331cd32b0d8c9a40a9fff0`.

Package verification passes and confirms the exact tested DLL. Existing renderer settings are preserved by the installer. `release-replay-final-20261030.json` links the complete private evidence index. Client login, representative in-game appearance and effects-on/off FPS are the next validation stage; no claim about them is made here.
