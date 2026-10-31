# Actor AO compatibility validation

The compatibility follow-up was recovered from an unfinished local worktree during the publication audit. Add a compile-time isolated-horizon environment path to actor GTAO and a frozen MIT project-source reference from PR #118 (`3977bd70`). The normal independent actor-AO build does not enable this macro. Regeneration proves every production shader payload is byte-identical to the existing PR head; the current production algorithm and quality are unchanged.

The independent Windows x86 DLL builds. The hidden DXVK 3 matrix compiles nine entries in each of legacy and isolated-horizon modes, keeping SM3 limits and all expected unchanged kernels. Both modes pass 40 independent ray-truth/color/contact/dispatch cases and 160 production AO+blur renders: 80 cases and 320 renders total. The runner accepts a separately verified compiled directory and refuses a running game.

Reproduce with `python tests/validate_actor_gtao_compatibility.py --run-gpu --backend <32-bit-d3d9.dll>`. This validates compatibility with the frozen base algorithm; it does not establish game FPS parity, arbitrary future GTAO variants, or the complete game acceptance needed to make this draft ready.
