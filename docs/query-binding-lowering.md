# Bound query-variable lowering

This is the first syntax-neutral compiler specimen for the Studio language
migration. It does not establish complete Stage 0, browser readiness, Studio
parity, or performance comparable to well-written C.

## Pinned oracle and scope

The production oracle is engine `02f8453211dd4cc57965ba52c645f08a92e53c5e`.
Baseline commit `1bac710ff9a4711676950ce1859ef878c8c695b9` records a bounded C fixture of its real
`EXECUTION_query_variable_call` value-host path before replacement. The Studio
inventory used `78cb4d4c47242db9f7af2b9573653d9756050e99`; no Studio source changes
are part of this specimen.

Before replacement, a bound-variable read constructed two argument values and
one result value, passed the context through a host bank, and loaded the handle
from the result bank. It allocated no reference descriptor/arena block in this
fixture. The new `BF_QUERY_VARIABLE` instruction has a verified one-based
immediate target and a `PIN_HANDLE` result. Root variable zero remains `BF_OWNER`.
There is no source-language change, arbitrary C inlining, component-read/write
optimization, or expanded parallel-owner proof.

## Semantic and backend contract

- The builder accepts only targets 1 through `ECS_QUERY_VARS`, a handle result,
  and ordinary scalar-value provenance. Prepared readers reapply verification.
- Each execution reads the current context tuple. It returns all 64 handle bits,
  including zero. It neither borrows an ECS row nor retains a tuple pointer.
- A missing tuple fails with `BEHAVIOR_FUNCTION_HOST_DATA` and the existing
  `Query variable is not bound` message. Source/node/pin, debug publication,
  budget, branch order and unchanged results on failure are covered.
- Successful execution clears `error_message`, as the old host call did. Other
  existing failure metadata remains unchanged, including on same-context retry.
- The instruction can fail even when its result is unused; dead-code elimination
  must retain it. It is excluded from the aggressive full-function optimizer.
- Query predicates require its target in the current bound-variable mask,
  transitively through outlined calls. A bound tuple handle is not proof that a
  component write is owner-local.
- VM, SLJIT, generated Wasm and generated C implement the same operation. SLJIT
  uses reserved scratch registers. Wasm directly loads on success and delegates
  only the missing-tuple failure to the existing diagnostic helper.
- Prepared execution format version 14 rejects older opcode layouts. The context
  ABI hash now includes `query_variables`. Rebuild prepared packages with the
  matching SDK; authored project data needs no migration/version bump.

The retired intrinsic and its verifier/effect/cost cases are removed. The test
oracle uses the public context-host ABI solely for differential semantics; it is
not claimed to reproduce the old value-bank timing path.

## Bounded validation

`language-query-program` / `wasm-query-program` add at most 32 tiny definitions,
384 semantic comparisons, 24 decoded-function calls, 16 control calls, four
live-register calls, and 5,122 benchmark/warmup calls. The handle-only structural source is below 256 bytes;
prepared fixture blobs are below 64 KiB. All eight variable indexes, high-bit and
zero handles, null/recovered tuples, inspected/uninspected execution, invalid
immediates/types, unused trapping reads, branches/budgets, twelve live integer
values, prepared round trips and transitive bound-mask forgeries are covered.
The authored handle-only join asserts direct operations in both body and filter,
with no host calls or data-bank values. Scalar component expressions are a
separate future specimen and still use their existing descriptor path.

One scheduled-source fixture adds four compilations (VM/compiled, with/without
inspection), six entities and four candidate tuples per run, two accepted rows,
and eight total step/pump calls with budget 1,000. It checks live target-side
filtering across two generator variables, event payload ownership and tuple
identity, source activity, and conservative non-owner write effects. This closes
the affected integration assertions otherwise supplied by the full-engine
`source-query-rows` selector; those full-engine integrations remain unrun.

`c-export-alias` adds two four-instruction fixtures and 16 calls, including four
same-context recoveries, inside its existing single compiler child and 128 KiB
source cap. Tests assert actual native inspection code and generated Wasm entry
execution, rather than inferring backend coverage from configuration.

All execution uses `tools/test_policy.py` and its shared 60-second deadline.
Independent affected-contract review selected the native language group,
Node Wasm group, generated-C selector and prepared producer/runtime consumer.
Native correctness uses Clang 19.1.7 Debug with AddressSanitizer and
UndefinedBehaviorSanitizer. LeakSanitizer cannot start under this environment's
tracing, so `ASAN_OPTIONS=detect_leaks=0` is explicit: leak detection is unrun.

The admitted Node run is a reviewed replacement for this generated-IR semantic
contract only. Chromium cannot start because socket creation is denied; the
sanctioned elevated attempt failed before execution. Browser loader, isolation,
iframe and worker delivery are unrun, as are GPU/Studio suites and other platform
architectures. This does not waive server checks, review requirements or holds.

## Measurements

Linux x86-64; CMake 4.4.3, Ninja 1.13.2, Clang 19.1.7 native Release and
Emscripten 6.0.10 Release with bundled Node 24.19.0. Baseline and candidate use
matching toolchains/flags per target. Each function gets one warmup and five
trials of 512 fresh-context invocations; compilation is outside timing. Correct
results and actual compiled entry availability are checked. Timing never decides
whether a regression test passes.

| Native Release path | Baseline median µs/read | Direct median µs/read | Allocation calls/read, before → after | Requested bytes/read, before → after |
| --- | ---: | ---: | ---: | ---: |
| VM | 0.146119 | 0.040334 | 5 → 1 | 1,616 → 64 |
| SLJIT | 0.094869 | 0.011951 | 4 → 0 | 1,456 → 0 |

The structurally eliminated work is one host call and three data-bank values per
read. Both versions allocate zero arena blocks. These very short samples include
fresh-context setup/teardown and scheduling noise; an existing query row can
reuse host banks. They are not whole-application speedups, steady-row costs,
peak-memory measurements, or evidence of parity with hand-written C. [Raw trials](query-binding-measurements.json) retain all five samples, allocation
traffic and matching build descriptions. Exact-head test/build results and final
commit identities are recorded with the PR evidence.
