# Current nested-pointer validation

## Reconciliation

The historical task list's “not integrated” entry is stale. All 20 saved nested-pointer hunks across 13 paths are present or equivalently evolved in current source. The archived lane patch and reviewed patch are identical (SHA-256 `ae26c624986979ab10603fa2cabfd0eeea1a701e4985e203e09bda10d9cbc7b7`). Current scoped-reference code reuses previously computed variables, and the task fixture now selects the correct platform access function through `Exists`.

No `tools/compiler` or `test/native_aot` source differs between the admitted compiler source `02c86287430def964445ec90185961e20319536d` and tested checkpoint `af1368540660106bd9b769f151f347bc4155556d`. The three PointerCells compiler tests are already included in the fresh 573-test compiler pass. No historical patch was reapplied.

The preserved family/generation attempt was a **failure**, not merely a missing receipt: status 1 after 2.014 seconds, reporting “native waiting function cannot fail, format or borrow foreign buffers yet”. Current validation permits scalar-ABI foreign calls while retaining borrowed-buffer restrictions. Historical ordinary-source attempts describe differing inputs and do not establish exact-current ordinary/native parity.

## Fresh Linux proof

Using the independently admitted compiler SHA-256 `7b17b5bfdaed144e0949afd065db723bb45c8bd55b356cb70910d6df42e6d266`, a new isolated snapshot passed:

- **24 standalone source audits:** NestedPointers Main and intentional Failure, normal/forced collection, O0/O2, with and without allocator callbacks; plus Cells and ScopedAliases controls.
- **Two exact schema rejections:** PointerArraySchema and NestedPointerSchema returned 1, identified `Keep` with the required scalar-proof diagnostic, and preserved pre-existing output bytes.
- **Two genuine old-generation completion cases:** production Host retained the old nested-pointer task through actual reload at O0/O2. Edited.Frame, not the controller, created `allow-old`; the old worker completed and wrote `old-done`. Both hosts returned the required status 7 with empty diagnostic logs.

Every source audit reported zero live allocations and peak at most 2,048. Main exercised 90,010 allocations, or 180,020 with callbacks; Failure exercised 9,003 or 18,006. Cells exercised 16,036 and ScopedAliases allocated none. Measured peak counts ranged from zero to 967.

Before the generation runs, independent artifact review verified actual ELF allocator/releaser relocations and `.fini_array` entries pointing to live-count checks that trap on imbalance. The ordinary host closes images only after its existing ownership and collection protocol. No premature unload, dangling-pointer construction or deliberately unbalanced close control was used.

All 20 build/emit/test phases passed their source/tool/termination checks. They total 18.317 seconds of measured work, excluding preparation/review. Each phase retained a 120-second limit and two-CPU affinity; compilations used at most two workers. Every owned process terminated, all 4,067 tracked source blobs and modes remained unchanged, and all input/tool hashes were preserved. Independent final readback checked the exact logs, markers, emitted-module instrumentation and receipts.

`nested-pointer-results.json` records phase identities, artifact/log hashes, inventory, allocation reports and bounds. The complete local evidence archive is SHA-256 `78bd25b126929413330b359836de8207ae9cdc5907f4d574f14235899b7c4829`, with 174 inventoried proof files. These timings are correctness diagnostics, not a benchmark.

## Limits and remaining work

- This establishes the named Linux cases, not the broad managed/task suite, actual Windows behavior, product acceptance or readiness to merge PR #1331.
- This original proof independently parses every allocation report and enforces peak ≤ 2,048. A subsequent checked-in regression now closes the assertion-path audit masking gap; see [SOURCE-AUDIT-REGRESSION.md](SOURCE-AUDIT-REGRESSION.md) for its separate baseline and corrected evidence.
- Normal close balance is verified. The deliberately broken close control was not rerun, so no new negative-control detection claim is made.
- Exact-current ordinary/native differential testing remains separate.
- Historical failures and terminal-unknown attempts retain their original status. The old union-native leak investigation and ambiguous flagged cases were not selected or declared cleared.
