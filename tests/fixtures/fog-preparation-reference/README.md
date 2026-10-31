The two headers are unmodified source from upstream commit e19ffee601acbad2b991917109bcb6d8b2f7d5ca. Tests include them under renamed namespaces and compare complete generated outputs with the optimized code. They contain project source only, with the repository MIT license.

The runnable differential test compares four noise seeds (1,048,576 bytes), 49,152 complete field texels, and 10,000 randomized water-mask queries. Optimized, ASan/UBSan and x86 Windows builds passed. These construction checks do not establish game FPS improvements.
