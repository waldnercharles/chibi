# Native workloads

Fixed kernels for W20 in `docs/native-llvm-ledger.md`. Each is one source
that `chibi build --native` accepts, and `Main` returns a checksum modulo 251
as the program's exit status:

| Kernel | Exercises | Status |
| --- | --- | --- |
| `integrate` | game math: records of `number`, 64 bodies for 200,000 steps | 161 |
| `collide` | game math: integer records, 32 million box overlap tests | 95 |
| `lex` | a compiler pass in miniature: a union-typed state machine over 4 million characters | 140 |
| `recurse` | calls: recursive Fibonacci and Takeuchi | 138 |

The VM gives the same statuses: a `@test` asserting `Main() == status` passes
under `chibi test` for each kernel. Sizes stay within the VM's instruction
budget and call depth so that this check keeps working. A kernel over
strings can follow W3; kernels that need arrays or maps (a real tokenizer or
compiler pass) wait for W7.

`native-workload-benchmark` (`test/tools/native_workloads.c`, Linux x86-64) builds each
kernel with the launcher it is given and the LLVM tools in `CHIBI_LLVM_BIN`,
checks the status and prints one line per kernel and mode:

```sh
make build-release BUILD_TARGET='chibi chibi-build-host chibi-compiler-host test_tools'
CHIBI_LLVM_BIN=/usr/lib/llvm-18/bin CHIBI_TOOLS_BUILD_DIR=build-release/test/tools \
  python3 tools/test_policy.py tool native-workload-benchmark
```

- `cold_build_ms`: the first build, which also compiles the emitter closure.
- `build_ms`: a build that reuses the emitter, from source to linked program.
- `peak_rss_kb`: the peak resident memory of the largest process in that build.
- `size_bytes`: the linked program.
- `run_ms`: the median of three runs.

Measure on a quiet machine with a Release `test_tools`. The ledger records
each milestone's numbers; they are measurements, not targets.
