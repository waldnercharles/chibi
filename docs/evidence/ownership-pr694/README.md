# PR694 bounded ownership diagnostic checkpoint

Reporting correction (2026-10-25): the published first-checkpoint receipt is
**0.27 s process elapsed, 0.371 s policy wall time, 2,080,768 bytes maximum RSS
(about 1.98 MiB)**. Do not substitute the 0.07 s from a separate local recheck
mentioned in the initial handoff. Original raw receipts remain unchanged.
[The text follow-up](text-followup.md) also records corrected author telemetry;
it supersedes the earlier informal >11 GB/14 GB OOM characterization below.
[The authored Slice attribution](slice-attribution.md) traces one compiler-used
string operation and records a held, ABI-affecting follow-up proposal.

Measured 2026-10-25 on exact source `f4458b21f67badda787970218e3f8058d44cccf9`
plus this checkpoint's instrumentation/tests. No behavioral optimization.
The requested managed-pointer source `537d400bade97ef03779b7ff4007b4942ce53161`
is **not an ancestor** of this source: both commits have parent
`7a82e552fa279c2aac51f0ce622e212b702ab6dd`. This checkpoint stays on f4458b21;
it does not merge or cherry-pick the sibling.

## Measured results

All four new pico_unit cases passed, with 1,537 assertions. These exercise the
production collection APIs, native array kernels and current
`EXECUTION_array_mutate_place_call` append intrinsic. Values use independent
literal oracles, including after source retirement and explicit rooted
collection at the existing type-test boundary. No synchronous execution is
active there. No collection is inserted into allocation or runtime execution.

| Probe | Observation |
| --- | --- |
| Raw boxed outer array, capacity 4/count 1, packed two-int child, first VALUE copy | Two backing payload allocations: 168 bytes (160 outer + 8 child); 8 packed bytes copied and one boxed child VALUE copied. Two owner shells. **Zero existing header moves.** |
| Detach copied outer, then mutate its child | Outer detach allocates 160 payload bytes and copies one boxed child; child refs reach 3. Child mutation detaches/copies 8 packed bytes. Original remains `[10, 11]`; copy becomes `[99, 11]`. |
| Before/after rooted collection, after all calls return | Original child refs fall 2 → 1; occupied arena usage falls 1,555 → 1,283 bytes. Unique external backing bytes remain 816. This tiny graph directly demonstrates an unrooted constructor shell retaining a nested reference until collection. No borrowed original is explicitly released. |
| Native readonly length → production append | No detach; one element header move. Result `[10, 11, 12]`. |
| Explicit VALUE copy → drop → production append | One owner shell, no detach; one element header move. |
| Genuinely retained VALUE copy → production append | One owner shell and one 8-byte packed detach. Retained copy stays `[10, 11]`, appended value is `[10, 11, 12]`. |
| Repeated native first-element slice, concat, then slice-pop; 4 → 8 elements | Existing buffer-copy counter rises 80 → 288 bytes (3.6×); header moves 10 → 36; cumulative arena allocation accounting 1,328 → 2,784 bytes. Final arrays match literal `[1,2,3,4]` and `[1,2,3,4,5,6,7,8]`. |
| 36 snapshots, 2 flat layouts | 34 cache hits; 412 descriptor bytes copied. |
| 36 snapshots, 9 flat layouts | 0 cache hits; 7,416 descriptor bytes copied (eight-entry cache). |
| 36 snapshots, 2 frozen layouts | 0 accepted hits; 34 found entries bypassed by the frozen-source guard; 7,416 descriptor bytes copied. |
| 36 snapshots, 9 frozen layouts | 0 hits or found-entry bypasses; 7,416 descriptor bytes copied. |

The repeated slice/concat case has quadratic copy work even at these tiny
sizes. These observations identify work to investigate; they do not establish
compiler-scale memory or runtime. The author's reported >11 GB self-compile
and 14 GB OOM were **not reproduced or independently measured here**.

## Counter definitions and storage accounting

`adopt` and `detach` are printed as
`allocations/payload_capacity_bytes/packed_copy_bytes/boxed_child_values`.
Capacity allocation includes unused capacity, excludes backing metadata/anchor,
and records attempted construction even if a later child copy fails. Packed
bytes are successful memcpy work; boxed values count completed child VALUE
copies, not memcpy bandwidth. Child backing construction records its own
allocation event. On this host `sizeof(pin_value)=40`, `sizeof(pin_array)=104`.
A boxed child slot is one 40-byte descriptor, but recursively retained payload
work is counted separately. Growth/realloc work is outside these new counters.

`array_owner_shells` counts shared VALUE-copy shells only, excluding ordinary
constructors, views and windows. `record_descriptor_bytes` covers
`copy_record` only, not native closure construction. A frozen cache bypass
counts a found entry rejected by that guard; it need not imply an allocation
if the source is already owned by the destination arena. Counts use the
existing thread-local copy scope and saturate with its sticky overflow flag.
They do not change the semantics of existing header/buffer counters.

`external_unique` is `arena.retained_bytes`, deduplicated per external resource,
not a sum over attached shells. `arena_used` and `arena_capacity` come from
`arena_usage` and already include unique external bytes once: **do not add
external_unique again**. Used bytes include uncollected arena allocations;
capacity also includes reusable allocations. Neither includes all allocator
metadata or equals process RSS. `arena_allocated` is a cumulative work delta,
including external resource accounting/growth, not a live-memory measurement.
Zero external bytes for slice/concat still leaves arena-owned arrays live.

## Reproduction and bounded validation

Toolchain: macOS 26.7.1 (25G241), arm64; Homebrew Clang and clang-format 22.1.8;
CMake 4.4.3; Ninja 1.13.2. Debug (`-g`), C23, no sanitizers, test assertions
`-UNDEBUG`, `-Wl,-dead_strip`. At most two build jobs. The pinned prebuilt
third-party archive is macos-arm64 release `third-party-2026-10-22.2`, SHA-256
`602d82c6d59563b5af00c5be2d69c0447654ff00728ddcd94acf179c27c1092b`.
An existing local cache of that exact archive was read without modifying it.

```sh
# THIRD_PARTY_CACHE is an existing directory containing the pinned SHA directory.
cmake -S test/tools -B scratch-profile/build22 -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm@22/bin/clang \
  -DCHIBI_SDK_BUILD_COMPILER=OFF -DCHIBI_SDK_BUILD_NATIVE=OFF \
  -DCHIBI_SDK_BUILD_DESKTOP=OFF \
  -DCHIBI_THIRD_PARTY_CACHE="$THIRD_PARTY_CACHE" \
  -DCMAKE_EXE_LINKER_FLAGS=-Wl,-dead_strip \
  '-DCMAKE_C_FLAGS=-Wno-error=missing-field-initializers -Wno-error=unused-parameter -Wno-error=unused-function -Wno-error=unused-variable'
cmake --build scratch-profile/build22 --target test_tools --parallel 2
python3 tools/test_policy.py --timeout 15 native /usr/bin/time -l \
  scratch-profile/build22/test_tools --ownership-probes
python3 tools/test_policy.py --timeout 20 native /usr/bin/time -l \
  scratch-profile/build22/test_tools --types
python3 tools/test_policy.py --timeout 20 native /usr/bin/time -l \
  scratch-profile/build22/test_tools --arena
# The named selector is also registered:
CHIBI_TOOLS_BUILD_DIR=scratch-profile/build22 \
  python3 tools/test_policy.py --timeout 15 tool ownership-probes
```

Builds were wrapped by `subprocess.run(..., timeout=180)` with elapsed time and
`resource.getrusage(RUSAGE_CHILDREN).ru_maxrss` recorded. Initial standalone
configuration took 1.33 s (RSS unavailable in its first sandboxed time call).
The untouched focused driver built in 6.587 s; maximum child RSS 173,883,392
bytes. The instrumented build took 6.525 s; maximum child RSS 176,521,216 bytes.
These are separate from test execution and are not aggregate simultaneous
build-process memory. The final small incremental rebuild took 0.617 s,
65,257,472 bytes maximum child RSS.

The standalone SDK re-adds `-Wall -Wextra -Werror` after global flags. Initial
LLVM23 Release and LLVM22 Debug attempts failed on existing
`types/scalar.h:99` positional `pin_string` initializers (Release also reported
an assert-only local in `types/text.c:143`). Root CMake already excludes these
warning classes; the standalone invocation above demotes them using
`-Wno-error=...` so their warnings remain visible. No warning-site source was
changed. This is not a warning-clean build claim. No authored asset check was
disabled, no compiler seed was generated, and no stale bootstrap was consumed.

| Check | Result | Process maximum RSS | Process elapsed (`time`) | Policy wall time |
| --- | --- | ---: | ---: | ---: |
| New probes | 4 passed / 1,537 assertions | 2,080,768 bytes | 0.27 s | 0.371 s |
| Existing types | 27 passed / 4,861 assertions | 3,424,256 bytes | 0.00 s (rounded) | 0.113 s |
| Existing arena | 3 passed, 1 failed / 367 assertions | 3,375,104 bytes | 0.00 s (rounded) | 0.101 s |

Raw observations: [probes](probes.txt), [types](types.txt),
[arena candidate](arena-candidate.txt), [arena baseline](arena-baseline.txt).
`time -l` measures the actual test process, not the Python policy runner.
Checkout path prefixes are replaced by `$CHECKOUT`/`$BASELINE` and trailing
line whitespace is stripped in these logs; values and diagnostics are retained. Timing is one diagnostic run, not a
stable performance benchmark.

The arena failure was reproduced in a separate untouched f4458b21 worktree:
`test/support/arena.c:155` expects the first source to be evicted after a
second distinct source, while `ARENA_COPY_CACHE` is 8. The exact same assertion
fails before and after this checkpoint. Neither arena implementation nor that
test is modified here. The baseline build took 6.374 s and maximum child RSS
176,357,376 bytes. This remains an upstream test/contract reconciliation item.

Independent review found and corrected a test-helper coverage gap: append now
uses `execution_reference_value` and the current production place-mutation
intrinsic. The final review found no blocking ownership, lifetime, accounting,
or GC-policy issue. Touched C files were formatted with clang-format 22 and
`git diff --check` passed.

## Limits and handoff

- This measures native kernels/intrinsics and collection APIs, not authored
  identity functions, VM call-frame ownership, compiler lowering, full
  self-hosting, sanitizer coverage, or production-size workloads.
- New copy counters extend public `pin_copy_counts`/`pin_copy_scope` layout;
  rebuild linked consumers. This is a source checkpoint, not an ABI-compatible
  binary distribution. No query-schema carrier integration was attempted.
- No GC policy, allocation quotas, PR688 adoption, pins, seeds, releases or
  memory architecture changes. Frozen `pin_record` versus query `pin_schema`
  reconciliation and prepared21 compiler delivery remain separate lanes.
- Full native/material/project/language/tool suites were not run. Validation
  intentionally used the existing standalone focused driver without authored
  generation, under the delegated two-job and tiny-workload constraints.
- The development branch is a reviewed diagnostic checkpoint, not a merge
  recommendation. The baseline arena regression remains open.
