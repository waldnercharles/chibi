> Historical technical record, with coordination-only wording removed. Read the top-level README.md for current dependency heads, publication status, omitted reference copies and the parser admission update. Historical source-only status and earlier hashes describe preparation, not a passing build or runtime result.

# Prepared scalar producer bridge

Status: local source-only candidate. No build, authored invocation, target
invocation, runtime test, test admission, remote mutation or publication occurred.

## Files to integrate

- `candidate/src/toolchain/compiler_prepared_scalar.h`
- `candidate/src/toolchain/compiler_prepared_scalar.c`
- `candidate/test/language/compiler_prepared_fixture.h`
- `candidate/test/language/compiler_prepared_fixture.c`

Dispatcher, CMake and coverage proposals are in the test fixture packet.
These files are producer/test-only. Do not add them, their object files or the
scalar-lowering adapter to the runtime-only compiler-host dependency closure.
The driver cannot run in the current runtime-only host catalog. This checkpoint
bootstraps/runs the compiler with the stage-zero fixture, then loads and runs its
resulting user artifact with a fresh runtime-only host.

## ABI and ownership

`compiler_prepared_scalar(plan: T, module: uint64, function: uint64,
entry: string, result_storage: int) -> (bytes: byte[], diagnostic: string)`

- `T` uses the existing native generic-group transport convention. The core
  immediately uses the unchanged `scalar_lowering_decode` schema validator.
- Input module/function IDs and entry text are authored choices. Neither native
  file derives IDs, selects source declarations or constructs package names.
- `result_storage` is exactly PIN_BOOL (1) or PIN_I32 (6).
- The immutable `compiler_prepared_fixture_service` is bound using the
  `COMPILER_PREPARED_FIXTURE_SERVICE` macro (`compiler_prepared_fixture`). Its
  `ready_tag` is resolved from trusted `scalar_lowering.Plan.Ready` export
  metadata for the prepared producer. It is not a caller-supplied wire tag.
- Native catalog binding is `.pure = true` and `NODE_EFFECT_PURE`; its immutable
  service read is conservatively recorded as SCRIPT_EFFECT_READ by existing
  native-definition conversion. It is not marked parallel-safe or reusable.
- Zero arguments, zero state variables, one ordinary Bool/I32 result, one
  EXECUTION_FUNCTION_AUTHORED row, no imports, no host calls in the output body.
- The core returns one malloc-owned INNER prepared module and clears output on
  rejection. The callback converts each octet to an invocation-owned PIN_U8
  item and frees the temporary buffer. No nominal names/records appear in its
  byte array/header/items. Neither inputs nor service pointers are retained.
- A rejected plan/declaration returns empty bytes and an owned nonempty
  diagnostic. Success returns nonempty bytes and an owned empty diagnostic.
- Entry text is bounded to 240 bytes; serialized bytes are capped at 65536 before
  allocating the retained runtime byte array. The core requires output pointers
  and a nonempty error buffer, matching execution_module_finish's contract.

The exported native pack contains one definition and one binding. Tests can
copy those rows and their node_host_call to wrap `.context` with an invocation
counter while preserving canonical identity, signature, service and access.
The test wrapper should delegate the service argument unchanged.

## Callable ABI finding

No instruction adapter is needed. The PR447 evaluator's signature has no
arguments; its code writes initialized `context.results` and reaches BF_RETURN.
The existing invocation machinery in `src/execution/call.c` allocates/seeds the
callee result bank and copies those values only after `context.returned`.
`compiler_host` entry selection accepts an authored function with value-bank
arguments/results. The native wrapper therefore changes only module metadata,
without rewriting evaluator instructions or changing the PR447 interface.

Source evidence was read from PR447 head
`bdbb896c48aadd271ee63a6562447ccb95d7ffa1`: scalar_lowering_plan.{h,c},
function_builder.{h,c}, module_builder.h, module.c, call.c, definition.h,
prepared.h, native_definitions.{h,c}, capabilities/definition.{h,c}, host.h,
types/value.h, types/array.{h,c} and the scalar differential fixture. The
current target branch was also checked for the module-builder, function-builder,
module, value and native-definition interfaces. PR447's scalar header remains a
dependency absent from the target branch at the time of that check.

## Explicit transitional native dependencies

Core calls:

1. `scalar_lowering_decode`, `scalar_lowering_finish`,
   `scalar_lowering_builder_free` from PR447's execution/scalar_lowering_plan.c.
2. `behavior_function_builder_definition` and the existing function builder's
   verification, optimization, finalization/liveness/rewriter and parallel proof.
3. `execution_module_plan_new`, `execution_module_finish` with BEHAVIOR_BYTECODE,
   `execution_module_plan_free`, `execution_module_free`.
4. `execution_prepared_write` for the inner payload.
5. Existing support array/xalloc and standard C ownership/text functions.

The callback depends on the private native-definition descriptor types,
execution context/value-storage API, arena allocation and
pin_array_snapshot_allocate. The build should compile both new `.c` files only
into the producer fixture target and use its existing execution/native/array
dependencies. Core headers need the repository private src include directory.

No parser, syntax library, semantic frontend, source text, authored AST/typed IR,
source-acceptance policy, package-policy implementation or compiler_artifact
writer is imported by the bridge. CHIBICLO construction stays in authored code.

## Verification performed and still required

Performed: read interfaces and ownership paths; inspect the complete four-file
candidate; format and `--dry-run --Werror` with clang-format 22.1.8 and the
repository's `.clang-format`; generate a clean source patch.

Still required: exact integrated source build, authored generic callback
specialization, pure-service binding, rejected paths not invoking the callback,
wrong-tag/invalid declaration/type-mismatch/variable-plan rejection, prepared
write/read compatibility, producer/source teardown before loaded invocation,
BYTECODE and SLJIT result parity, actual invocation accounting, current test
admission and one shared production-supervisor ceiling. No such result is
claimed here.
