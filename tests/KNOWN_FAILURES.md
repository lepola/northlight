# Known failures

None. A test that fails at the current version for a known reason carries
`known-fail=<reason>` on its `# northlight-test:` line and gets a row here. `scripts/run_tests.py`
still runs it and reports XFAIL, which does not fail the run. If it starts passing, the runner
reports XPASS: remove the tag and the row.

| Test | Failure | Plan |
|---|---|---|

The 17 tests that failed at 0.3.157 were cleared: six obsolete
tests were deleted, six harnesses were updated to HEAD, four aborting native tests were fixed
(stale expectations and one clock-freeze harness bug; none was a production bug), and one
script bug was fixed. See the commit messages.

Not listed here: 3 tests that fail only when a prerequisite is missing. They are tagged instead,
so the runner reports a reasoned SKIP:

- `manual`, needs command-line arguments: `test_terrain_cache_capacity.py`.
