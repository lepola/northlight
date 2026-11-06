These headers are the unmodified fog sources from commit 88841bd517f1a57b9917ba61c372e23a92f2d707, kept solely as independent differential-test references. The tests compare the optimized implementation against the prior behavior; the application does not include these headers.

`march-wave2-old.h`, `regional-wave2-old.h`, `noise-wave2-old.h`, and `fit-wave2-old.h` are the unmodified spell fog march, regional fog, cloud, and spell fit sources from the separate wave-two baseline, commit 4c96d30314fbe706c5bae865fe4b1cec53bd9b6d.

`cloud-generation-before.h` and `global-height-before.h` freeze the shipping generation pipeline and global field reduction from the parent revision named in each header. They reuse the unchanged scalar Perlin/Worley and texel-validity helpers; the new four-lane generator/reduction is not used by the frozen path. `test_fog_simd_preparation.cpp` checks complete volumes, lane values, field bounds, rounding and failure behavior.
