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
on Linux x86-64. Other hosts report the benchmark as unsupported, preserving
the original selector's platform contract. It builds each kernel with the supplied public
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
process RSS in KiB, including descendants it waited for. An isolated Linux
worker owns subreaper and signal-mask state, restores
the command's original mask, and kills/reaps adopted descendants across groups
and sessions before returning. It enforces a 300-second build deadline and a
60-second run deadline. The caller's ordinary process state is unchanged.
Grouped and `setsid`-detached timeout/early-exit controls, plus parent-only and
worker-only TERM controls, prove cleanup before timing begins.

Measure on a quiet machine with a Release native compiler. Results include
process startup and are measurements, not performance targets. The independent
`native-build` suite also checks each kernel's checksum at O0 and O2.
