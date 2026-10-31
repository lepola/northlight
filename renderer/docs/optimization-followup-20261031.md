# Local-light preparation follow-up (2026-10-31)

Four changes preserve source identity and uploaded values: sort retired source IDs once for batch lookup; binary-search individual retirement; share the direct/fog projected scissor calculation; and borrow complete direct-light constant columns while padding only incomplete batches. The borrowed batch view cannot be copied, preventing stale pointers into partial storage.

The independent branch retains its own constant registers and eight/sixteen-light shader programs. Its compile-derived LocalDirect cost is 194 slots; update the stale 202-slot expectation without copying the combined branch's different shader cost. Retirement, scissor, payload, selection and batching tests pass optimized and sanitizer checks: 18,632 retirement, 12,000 scissor and 59,321 constant payload cases. Its own x86 DLL builds. No shader source, light limit, quality, game session or new timing benchmark is changed by this follow-up.
