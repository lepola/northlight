# Prepared point-shadow clipping

One cube's matrices stay fixed while its update visits static, terrain, live and replay bounds. Prepare the six double clip planes, absolute matrix components and conservative cancellation bounds once per cube instead of repeating them for every AABB. Static content signatures use the same preparation. Keep the independently reviewed scalar predicate as an unchanged reference.

The independent branch's optimized and ASan/UBSan builds each match all 174,335 masks exactly, including real cube projections, near/range nextafter boundaries, unknown bounds, NaN/infinity and arbitrary extreme finite matrices. Existing point-light shadow and refresh/face-cycle suites also pass, including 72,000 legacy face decisions and 55,207 enclosure checks. The Windows x86 renderer builds independently.

Overflow checks, invalid-input fail-open behavior, sphere broad phase, face order, candidate ownership and shadow quality remain unchanged. Preparation uses 3,120 bytes of temporary stack and adds no heap allocation or retained per-light memory. The independent port validates bounds locally rather than importing the integration branch's separate scalar refactor.

The earlier combined Windows x86 ABBA microbenchmark measured 20.320 to 11.411 ms per 163,840 clipping candidates (43.84% less clipping CPU time). This excludes sphere broad phase, draw submission and GPU work; it is not a whole point-shadow-pass or game FPS claim. The independent branch's retained scalar implementation differs in preparation overhead, so its exact speedup is not inferred from that number.

Reproduce correctness with `python tests/test_point_clip_prepared.py`, `python tests/test_point_light_shadow.py` and `python tests/test_point_shadow_schedule.py`. The optional C++ benchmark argument runs the isolated clipping workload; no game is needed.
