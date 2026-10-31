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
- The prepared schema fingerprint includes the actual stable-name/ordinal map,
  including `QUERY_VARIABLE`, and the context ABI hash includes `query_variables`.
  Main's manual payload/semantic version remains 13: this change adds an opcode
  without changing any existing instruction payload or execution meaning. Older
  layouts and the retired intrinsic are rejected by the schema identity. Rebuild
  prepared packages with the matching SDK; authored project data needs no
  migration/version bump.

The retired intrinsic and its verifier/effect/cost cases are removed. The test
oracle uses the public context-host ABI solely for differential semantics; it is
not claimed to reproduce the old value-bank timing path.

## Bounded validation (2026-10-14)

These selectors were deleted with the stage-zero front-end tests
(`archive/README.md`, "Deleted once replaced"); this section records what they
covered.

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

All execution used the `tools/test_policy.py` supervisor, since deleted.
Independent affected-contract review selected the native language group,
Node Wasm group, generated-C selector and prepared producer/runtime consumer.
Native correctness uses Clang 19.1.7 Debug with AddressSanitizer and
UndefinedBehaviorSanitizer. LeakSanitizer cannot start under this environment's
tracing, so `ASAN_OPTIONS=detect_leaks=0` is explicit: leak detection is unrun.

The selected Node run is a reviewed replacement for this generated-IR semantic
contract only. During the original validation, Chromium could not start because
socket creation was denied; the sanctioned elevated attempt failed before
execution. Browser execution was not attempted during the main/schema refresh.
Browser loader, isolation, iframe and worker delivery remain unrun, as do
GPU/Studio suites and other platform architectures. This does not waive server
checks, review requirements or holds.
