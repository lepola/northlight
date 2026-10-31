# High GTAO footprint preparation

Move the existing zero-weight footprint rejection before the two depth reads and horizon calculations. Rejected samples already contributed exactly zero; sample count, useful positions, bloom, radius, strength and integration remain unchanged. Medium and Low source/bytecode are retained because their exploratory candidates did not improve performance consistently.

The independent PR head passes 18 exact FP16-output cases on DXVK 2 and 18 on DXVK 3, including odd dimensions and FP16/FP32 depth inputs. Both backends reject a deliberately disabled AO shader. The original nine authored silhouette/contact cases also pass on each backend. The High bytecode is identical to the combined validated build, which additionally passed 36 cases per backend up to 4K plus 99 AO-method and 48 grazing cases on each of native D3D9, DXVK 2 and DXVK 3. The independent Windows x86 DLL builds.

Earlier isolated-pass ABBA measurements on that same bytecode show 6.69%/6.77% less elapsed query time at 1080p/4K on DXVK 3, and 3.44% at 1080p on DXVK 2. DXVK 2 at 4K has no reliable benefit (new/old ratio 1.0063). These intervals are not isolated GPU-busy time or whole-game FPS gains. No further game measurement is required or claimed.

Reproduce with `python tests/validate_gtao_footprint_gpu.py --baseline-ref 3977bd70 --backend <32-bit-d3d9.dll> --output <new-directory>`; add `--benchmark` for the 1080p/4K offline ABBA cases. This runner uses a hidden device, refuses a running game, and verifies that the production artifact matches the compiled candidate. The small shared Toolhelp guard is included so the standalone runner has no private integration dependency. The grazing runner also creates its executable next to its requested report on clean checkouts.
