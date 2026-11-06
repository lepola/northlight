## CodeGraph

If this repository has a `.codegraph/` index, use `codegraph_explore` or
`codegraph explore` before searching or reading code. Do not create an index.

## Windows builds

On Windows, run Zig compilation, renderer builds, native/GPU test runners and
package verification with `exec_command` and `sandbox_permissions: "require_escalated"`, through the normal
approval mechanism. The restricted execution sandbox makes Zig 0.15.2 fail with
`error: Unexpected`, even for `int main(){return 0;}` and fresh repository-local
caches. The identical compiler command succeeds outside that sandbox.

Use `python scripts/build_renderer.py` for the production DLL. It checks the
compiler before the expensive renderer build. Keep the existing Zig version,
shader freshness gates and release-package verification.
