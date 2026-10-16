# Ordinary row-array construction: owned callback results

This is a bounded first reduction for roadmap L04 and the future Outliner
consumer. It removes a redundant deep copy from the existing `array_add` and
`append` CALL wrappers. Prefix construction and loop-local snapshots remain
quadratic. Amortized construction, the rest of L04/P01 and Studio translation
are still open. Studio's existing native row producer already allocates once;
the measured loop is the proposed authored construction path.

## Ownership contract

The two kernels already allocate a fresh array in the invocation's arena,
validate and own its layout/name, and snapshot every retained item, payload and
string. The CALL wrapper now publishes that complete successful value header
instead of immediately deep-copying it again into the same arena. Selection is
by the exact compiled kernel identities, after the existing owned-result and
error checks. All other CALL wrappers retain their existing snapshot path.

Inputs remain read-only borrows. Retained prefixes and returned values keep
value semantics; this does not infer uniqueness from arena membership. Kernel
validation order, error text and unchanged caller output on rejection are
preserved. Rejected calls may still leave completed-copy counters or arena
scratch advanced. The removed successful allocation is no longer a potential
allocation-failure point. No API, ABI, grammar, prepared format, backend opcode
or project migration changes.

## Measured selected copies

Baseline: `0132aec4ae98cbfeb646b9490c90e7fc131eaf54`, with unchanged production
source from main `718b8ddc34c8a22e37aaaf71c60555abd6e54ba3`.
Optimized source/fixture checkpoint:
`04a164db52acd2e9fe823e29fe3b0af48b40fa3c`.

At sixteen twelve-field rows, native and Wasm normalized counts agree, as do
bytecode and the actual compiled backend within each target:

| Path | Row-payload copies before → after | Text bytes before → after | Header moves before → after |
|---|---:|---:|---:|
| Direct HOST add / singleton append | 136 → 136 | 6,120 → 6,120 | 136 → 136 |
| Direct CALL add / singleton append | 272 → 136 | 12,240 → 6,120 | 288 → 136 |
| Whole authored add body | 457 → 321 | 21,240 → 15,120 | 685 → 533 |
| Whole authored singleton-append body | 473 → 337 | 21,960 → 15,840 | 701 → 549 |
| One-pass value snapshot reference | 16 → 16 | 720 → 720 | 17 → 17 |

Row-payload copy equivalents divide attributed record bytes by the target's
layout size: 56 native bytes or 36 Wasm bytes. These are selected copy counters,
not all memory traffic. Whole authored measurements include constructor,
local/argument retention, callbacks and return work; caller preparation/binding
is outside the scope. No estimated overhead is subtracted. The one-pass
snapshot is a reference operation, not an otherwise identical authored call.

Metadata bytes are excluded from record payload attribution. One retained
layout closure is 1,308 native bytes or 908 Wasm bytes; this is not total
metadata-allocation traffic. Arena backing-block changes are measured deltas,
while retained arena used/capacity/storage figures are totals. Functional-check
wall times are not isolated benchmarks or timing-speedup evidence.

## Exact bounded validation

All four optimized commands ran serially on the same clean checkpoint, under
the unchanged whole-command 60-second production supervisor:

| Route | Cases | Assertions | Seconds |
|---|---:|---:|---:|
| `language-row-array` | 5 | 16,109 | 0.168 |
| `wasm-row-array` | 5 | 16,109 | 0.229 |
| `language-record-compatibility` | 1 | 1,380 | 0.072 |
| `wasm-record-compatibility` | 1 | 1,380 | 0.138 |

The row-array route fixes construction sizes at 0/1/4/16 and retained-prefix
sizes at 2/16: 84 direct successes, eight rejects, four reference snapshots,
24 unmeasured setup snapshots, six analyses/module preparations and twenty
authored calls. Internal authored calls contain 84 construction callbacks,
36 alias adds and at most eight alias length callbacks. Each target entered
ten bytecode and ten actual compiled calls, with zero fallback: SLJIT natively
and generated Wasm in the Wasm build. Each call has a 10,000-instruction budget.

The fifth case adds exactly four direct CALLs over empty/one-row arrays and two
collections. Complete headers, layout/payload/string ownership and all twelve
fields are checked. Producer text is mutated and its arena retired before a
collection rooted only by raw array pointers. A rootless collection then
requires zero used bytes. No test-side repair snapshot hides borrowing.
Existing malformed-layout/nominal rejection and retained-prefix mutation
controls remain intact.

Each compatibility route is the unchanged 48-callback array case over int,
string and native reflected struct, with at most five items. These builds omit
native services, so the unrelated globals case is absent. Aggregates and
supervisor implementations are unchanged; no renderer, worker or I/O workload
was added. Independent postflight matched all 6,182 recorded identities,
including source, current builds and the preserved passing-baseline artifacts.

Native Debug used Clang 19, assertions, ASan and UBSan with
`ASAN_OPTIONS=detect_leaks=0:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1`. Wasm used Emscripten 6.0.10
and pinned Node 24.19.0. Leak detection is untested. Independent CUT62 review
found no additional diff-specific leak-enabled requirement for removing this
redundant same-arena allocation; this does not reuse another PR's waiver or
clear another PR's hold. Ordinary published-head, integration, review and
server-required checks remain separate release obligations.

Reproduce admitted routes against coherent builds using
`python3 tools/test_policy.py tool <route>`, with `CHIBI_LANGUAGE_BUILD_DIR`
and `CHIBI_WASM_BUILD_DIR` selecting the matching native/Wasm build and the
pinned Node first on PATH. Do not infer full-suite coverage from these routes.
Machine-readable selected measurements and evidence hashes are in
[evidence/row-array-construction/results.json](evidence/row-array-construction/results.json).

## Preserved baseline correction

The first fixture checkpoint `fbbd4cfd090aeee2001862f5e811be0f2e2ec513`
passed its two direct cases but failed both authored lowering attempts because
three inferred F64 counters reached integer modulo. It completed 9,557
assertions in 0.098 seconds; no authored call or Wasm baseline ran. The documented
language contract requires `int` for integer semantics. The passing successor
changed only those three declarations to `i:int=0`, retaining modulo and every
value/alias assertion. Native and Wasm then each passed four cases and 15,737
assertions (0.163 / 0.199 seconds). Failed and passing artifacts remain preserved
separately; neither failure was removed from the evidence history.
