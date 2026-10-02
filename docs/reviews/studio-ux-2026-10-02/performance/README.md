# Assets model preparation benchmark

`assets_benchmark.py` compares Assets list preparation from commit
`29a0e24f5e057223421018f82042f568ebfec057` with the current checkout. It reads the
old source through `git show`, extracts the measured C blocks, and compiles a
synthetic harness in a temporary directory. Generated source and binaries are
removed automatically. No editor build or third-party Python package is needed.

Run from the repository root with Python 3, Git, and a POSIX C compiler:

```sh
python3 docs/reviews/studio-ux-2026-10-02/performance/assets_benchmark.py
```

Override the compiler through `CC` or `--cc`; change the repetition count with
`--runs` (default 3). The exact command for the recorded results was:

```sh
python3 docs/reviews/studio-ux-2026-10-02/performance/assets_benchmark.py \
  --cc /opt/homebrew/opt/llvm/bin/clang \
  > docs/reviews/studio-ux-2026-10-02/performance/results.txt
```

The 2026-10-02 recording used an Apple M4 Pro, Homebrew Clang 23.1.1 targeting
arm64 macOS, and Python 3.14.7. [results.txt](results.txt) contains all three raw
runs, the current measured source hash, and the generated compiler command.
Assertions stay enabled with `-UNDEBUG`.

| Case | Before median | After median | Operation counts |
| --- | ---: | ---: | --- |
| Change filter, 1,000 assets / 10,000 references | 93.255 us | 16.680 us | Index reads 32,611 → 1,000; zero warm allocations |
| Change filter, 10,000 assets / 100,000 references | 1,754.355 us | 178.030 us | Index reads 409,600 → 10,000; zero warm allocations |
| Unchanged emission, 10,000 assets / 100,000 references | 1,569.515 us | Cache-key comparisons only | Index reads 409,513 → 0; allocations 23 → 0 |
| Completed index rebuild, 10,000 assets / 100,000 references | 1,569.515 us | 1,547.620 us | Dependency scan and sort still required |

This recording shows about 5.6× and 9.9× faster filter preparation. Timings vary
with other work on the host. Eliminated index reads and allocations are the
stronger result.

Each rebuild/filter sample averages 200 operations; warm hits average 100,000.
The fixture assigns ten references per asset and alternates filters `Asset_` and
`Asset_0`. A volatile sink consumes results. Assertions exercise filtering, kind
selection, group collapse, same-size index refresh, and cache teardown.

These are model-preparation microbenchmarks, not editor interaction latency,
browser INP, GPU frame time, or production-project measurements. Synthetic index
accessors can be inlined. Treat nanosecond warm-hit timings as compiler-specific
loop overhead, not a product claim. The baseline excludes the old visible-group
header recount, making the grouped-list comparison conservative. Both cases use
the current common substring/sort helpers and dynamic array implementation, which
are unchanged for the measured operation. Source extraction deliberately matches
this revision's layout and will fail clearly if those blocks are later moved.
