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

`native-workload-benchmark` runs `Benchmark.chibi` through the native compiler
on Linux x86-64 and macOS arm64. It builds each kernel with the supplied public
native CLI and LLVM, checks every exit status, and prints the same eight
`NATIVE_WORKLOAD` rows, one per kernel and mode:

```sh
sh test/native_aot/workloads/run.sh /path/to/native/sdk/bin/chibi /path/to/llvm22/bin
```

- `cold_build_ms`: the first native build in the kernel's fresh directory.
- `build_ms`: a build after removing that mode's published native output.
- `peak_rss_kb`: the peak resident memory of the largest process in that build.
- `size_bytes`: the linked program.
- `run_ms`: the median of three runs.

The worker samples the monotonic clock around the actual child, excluding its
own startup and the supervisor's polling. `wait4` reports that child's largest
process RSS, including descendants it waited for. macOS's byte count is converted
to KiB. The parent owns the child group through the existing foreground scope,
with a 300-second build deadline and a 60-second run deadline. Real timeout,
early-exit and parent-only TERM controls prove group cleanup before timing begins.

Measure on a quiet machine with a Release native compiler. Results include
process startup and are measurements, not performance targets. The independent
`native-build` suite also checks each kernel's checksum at O0 and O2.
