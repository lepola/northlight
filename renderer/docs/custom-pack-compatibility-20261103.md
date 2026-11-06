# Custom pack compatibility and Telredor QA, 2026-11-03

All seven previously failing client scenarios pass on the installed custom pack, without changing MPQs or Wow.exe. The changes fix offline texture/geometry readers and distinguish stock assertions from the actual installed asset view. Native light, geometry and shadow ownership checks remain in place.

Base commit: `c4eb05c2b2d9768074fd179887489165d3fb1939`. [Machine-readable acceptance](custom-pack-compatibility-20261103.json). Private evidence: `.local/out/custom-pack-support-20261103/`. No client payload, MPQ, executable or screenshot is committed.

## Seven scenarios

| Scenario | Cause and resulting behavior | Final evidence |
| --- | --- | --- |
| Dalaran candle geometry | The audit ignored models already containing native lights. It now inspects original luminous geometry through a disposable view with the light header cleared; it preserves the original asset hash and never publishes another runtime light. Native-light preservation in the production census is unchanged. | Eight sources above the actual tables; all 72 geometric rays intersect. |
| Point transmission cache | The same audit omission yielded zero rays. | All 72 actual rays are checked; 21 binary mask hits and phase-averaged coverage 0.25239. Original visible and native-caster geometry remain separate. |
| Emitter pin report | A model-only hash pin did not prove an independently replaced SKIN compatible. Thirteen installed M2/SKIN pairs have out-of-range vertex references. The shared resolver now raises a bounded ValueError; reports continue with explicit geometry_error and the winning SKIN archive. | 98 historical batch digests and 46 luminous-center proofs pass. For conflicting SKINs, original position proofs use byte-identical M2 vertex data with the stock reference SKIN; the conflicting active geometry is explicitly not certified. |
| Model light attachments | Excluding our y/z patches still leaves the custom D models; the test incorrectly treated that view as stock. Strict palette assertions now use the actual stock view. Active and custom native static lights are also checked. | Stock Zangar/Darnassus templates, active templates, native static sources and executable live-pose regression pass. Out-of-range witness triangles are skipped; a malformed-SKIN control cannot create attachment metadata. |
| Spell asset manifest | The reader traversed spare BLP mip slots, including duplicated offsets and stale sizes beyond the useful chain. The shared mip reader bounds by dimensions and validates the usable prefix. | Eight stock and eight active texture versions decode through the production native BC codec at O0/O2. Frozen stock hashes remain exact; custom O0/O2 RGBA results are identical, with independent Python moments checked within 1/255. |
| WMO lamp emission | The custom D group collapses all 216 glass triangles into points. The old test imposed 27 stock fixtures on geometry intentionally lacking an emitting enclosure. | Strict stock fixture remains 27 lights / 216 faces. Active degenerate geometry produces no invented fallback lights or glass-shadow exemptions; native WMO lights remain preserved. |
| WMO lamp runtime | The same fixed count was imposed on the custom empty lamp cache. | Stock 27-lamp ownership, enclosure rays, foreign occlusion and cache reuse pass at O0/O2. The active empty cache loads, retains its scene/cache and creates no shadow exemptions. |

The shared SKIN checks are used by scene geometry, luminous surface analysis, point transmission and the independent Dalaran audit. Archive priority and the winning assets are preserved. This does not repair an incompatible M2/SKIN pair inside the native client: unsupported geometry is reported and rejected, and supported native sources remain available. No hidden archive fallback or stock geometry substitution was added.

## BLP boundary support

The mip iterator supports BC, paletted and uncompressed BLP2 inputs. Spare slots beyond the dimension-derived limit are ignored. Repeated EOF offsets and absent terminal sizes end a partial chain. Two observed small-tail defects also end the chain at its last complete level: paletted sub-byte alpha rounded down to zero, and undersized rectangular BC mips below one block on their short axis. Missing pixels or alpha are never invented. Missing/corrupt base data, invalid ordinary mip ranges and internal holes remain errors.

The final header corpus covers **69,243 unique installed texture names and 620,783 usable mip extents, with zero rejected textures**. This is header/range validation, not a claim of GPU-decoding every image. Nine independent synthetic boundary tests cover spare/poisoned slots, partial chains, padding, palette alpha, RGBA, rectangular BC tails, malformed extents, SKIN lookup bounds and continued reporting. The real spell corpus separately checks actual native codec output.

## General validation and unchanged client

The full runner initially recorded **259 PASS, 1 FAIL, 225 SKIP**. The failure was test_unlisted_archive.py: source changes made during its two installation calls changed the cache fingerprint, causing a legitimate full rebuild where the test expected a lights-only rebuild. Its assertion was retained. After source edits stopped, the final focused run passed **12 PASS, 0 FAIL, 0 SKIP**, including that installer test, all seven scenarios, the BLP guard, boundary tests and related cache/emitter checks.

Consolidated final status: **260 PASS, 0 FAIL, 225 SKIP, 0 TIMEOUT**. Skips remain skips. Raw results: `.local/out/test-output/20261103-122959/results.tsv` and `20261103-125044/results.tsv`. The initial failure log remains available; it is not relabeled as an original PASS.

Before/after SHA-256 manifests match for all **37 MPQs, Wow.exe and the installed d3d9.dll**: 39 files, 36,333,778,296 bytes. All generated fixtures, cache exports and native test programs stayed outside the installed client files covered by those manifests. No game was launched.

## Latest qa_report: 32 FPS in Telredor

The current log was last written **2026-11-03 09:23:22 UTC (11:23:22 Kaliningrad)**. It identifies **renderer 0.3.217**, built 2026-11-02 23:08:10 UTC, source identity `547595df8094837b3c879a9bff2df1a0790ecb8cff6f0db867ea8ff7681ee5bc`. That is the frozen reference preserved by commit `445c1637`, before the subsequent 40 CPU / 40 GPU wave. BLP mip guard is active. The log reports an RTX 3080, DXVK 3.1.1, 2560x1440, MSAA 0. The captured scene was inspected.

Seven late ordinary 120-sample intervals (renderer frames 2044 through 2775, excluding the diagnostic capture interval) report **31.89-32.31 FPS** and **30.949-31.356 ms** average intervals. Seven late GPU samples average:

| Scope | Mean, ms |
| --- | ---: |
| GPU frame total | 30.844 |
| GameScene | 25.287 |
| Northlight Effects | 5.512 |
| PointLight within Effects | 1.429 |
| PointShadow within Effects | 1.202 |
| RemovalSmooth within Effects | 0.691 |
| AO within Effects | 0.486 |
| WorldComposite within Effects | 0.262 |

GameScene accounts for about 82% of this sampled GPU frame. CPU material profiling reports 7.03-7.98 ms in its existing scope and roughly 64-68 thousand clock reads; the separate CPU timer-overhead estimate is about 1.286 ms in two late samples. CPU scopes overlap GPU work and are not added to the GPU total. Nine long image-lock observations are logged, eight from DINPUT8.dll; observed wait intervals do not establish equivalent CPU decoding work.

**33 to 32 FPS is an increase of about 0.947 ms per frame.** There is no paired, same-scene 33 FPS baseline here, so this report does not attribute that difference to the mip guard or claim it fixed. The startup guard is not a new per-frame rendering pass. This is also not a gameplay acceptance test of 0.3.218.

An offscreen A/B composite comparison used the saved depth, AO, light and scene resources. The old 0.3.217-reference and current shader produced **exactly identical output components** on DXVK 2 and 3: 14,745,600 components per backend, zero RGB/alpha changes, zero nonfinite values. Local RTX 4090 batch medians were 0.169568 to 0.111736 ms (DXVK 2) and 0.124152 to 0.107008 ms (DXVK 3). These isolated shader timings are not RTX 3080 gameplay FPS. The helper uses the 79-register global bank and neutral auxiliary textures; per-source ownership and material inputs are incomplete. Both the capture metadata and replay explicitly say **whole_scene_replay=false** and **fps_eligible=false**. A full-frame FPS result cannot be derived from this replay.

## Runtime artifact and reproduction

These fixes change offline readers, cache metadata generation and their oracles. Production C++ and shader sources are unchanged, so the existing **0.3.218 DLL** remains source-identical and passes package verification: source identity `0258e1f76b8eb21889e0de0cdb583e8f7c67a5c8df66d90bd234f2211ba8645a`, SHA-256 `1e716bbf0be51152505c57d7da7313a8f9988991f43560d79c1da2a84c13b0de`, 9,097,728 bytes. No new installer or differently versioned DLL was produced or installed.

Configure the client and Zig 0.15.2 through northlight.local.ini. On Windows, run native compilation/tests outside the restricted sandbox as required by AGENTS.md:

```powershell
python scripts/run_tests.py test_dalaran_candle_geometry.py test_emitter_pin_report.py test_model_light_attachments.py test_point_transmission_cache.py test_spell_asset_manifest_client.py test_wmo_lamp_emission.py test_wmo_lamp_runtime.py test_custom_pack_inputs.py test_unlisted_archive.py test_world_emitter_coverage.py test_world_cache_diff.py test_blp_mip_guard.py -j 4
python scripts/run_tests.py -j 4
```
