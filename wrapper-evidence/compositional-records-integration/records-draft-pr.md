Allow authored records to contain nested records and array fields in both compiler stages, with explicit aggregate initialization. Record and array copies preserve value isolation; pointer fields keep shared target identity, including field addresses and replacement through a pointer. Pointer recursion remains legal. Recursive containers, map/union fields and full LLVM aggregate lowering remain deferred.

This is a **source-only review checkpoint; prepared-27 runtime, bootstrap and SDK publication remain held**.

## Source identity

- Head: `696719e1f9aae8346a7c29af31b990685ec81490`
- Tree: `3741bfab1d2f4e27256bffa56bb9f02b1c227d48`
- Actual source parent: accepted #694 merge `7c6ca2f5d2c0fd10b4577f2c319ab5680b66ad62`
- Target: `main`. The source has not been rebased onto subsequent accepted changes. The checks below describe the recorded `7c6ca2f5` combination and do not establish compatibility with current main.

The source uses prepared format **27** and extends `type_field` with `container`. Checked `bootstrap.json`, `compiler.chibip` and `runtime.json` remain identical to the format-26 parent. They were not regenerated for this checkpoint.

[Implementation contract](https://github.com/waldnercharles/chibi/blob/696719e1f9aae8346a7c29af31b990685ec81490/docs/compositional-records.md) · [Source integration evidence](https://github.com/waldnercharles/chibi/blob/696719e1f9aae8346a7c29af31b990685ec81490/docs/evidence/compositional-records/integration-7c6ca2f5.md) · [Earlier frozen-candidate evidence](https://github.com/waldnercharles/chibi/blob/696719e1f9aae8346a7c29af31b990685ec81490/docs/evidence/compositional-records/README.md)

## Recorded validation

Independent runtime/prepared and compiler/registration reviews found no remaining source blocker in the forward port. No new code or artifact was produced to open this draft.

| Check on the recorded source combination | Release | ASan/UBSan |
| --- | --- | --- |
| Composition, both compiler stages and bytecode/SLJIT target VMs | 9/9, 9,702 assertions; 38.414s | 9/9, 9,702 assertions; 413.679s |
| Prepared record boundaries | 18/18, 6,126 assertions; 1.374s | 18/18, 6,126 assertions; 1.770s |
| Native type contracts | 42/42, 5,103 assertions; 0.430s | 42/42, 5,103 assertions; 2.476s |

The ledger also records selected runtime, semantics, query, program, compiler-types/collections and dispatch checks. Test times exclude external configuration/build time. Earlier selected checks are labeled separately from final-snapshot checks.

These used compiler-only SDK language and standalone tooling harnesses with Clang 23 build-only warning exceptions: `missing-field-initializers`, `string-concatenation`, and `unused-but-set-global`. No permanent warning exception or portability-fixture change is included. These results do not establish a normal strict product build or installed SDK acceptance.

The exact unchanged `7c6ca2f5` control reproduces the two record-state failures (1/3) and two authored prepared-writer comparisons (1/3). Prepared records (16/16), values (15/15) and compiler-only compatibility (1/1) pass on that control. Older failures at `19ad570dd` remain historical evidence in the separate ledger; they are not attributed to this feature or to current main.

## Remaining integration and release gate

Keep this draft held until the coordinator completes the ordered integration on then-current accepted main after the earlier library lane. That candidate must reconcile later native arena/host/runtime changes, canonical fixtures and the writer cache once eligible. Writer reuse must preserve recursive nominal child identity, container metadata, first child-closure validation, shared work budgets, sticky errors and independent reader/finalizer charges. Reconcile the old repeated-nested-validation exhaustion test with reuse success, distinct uncached-alias exhaustion and malformed-child controls.

The dedicated prepared-27 gate must build matching native headers/libraries/hosts, recover and validate a fresh authoritative compiler fixed point and bootstrap, update the public runtime pin, validate installed SDK consumers, and assess host admission against the then-current protocol and Make gate with stale/current and warm/cold routing fixtures. Do not select a provisional protocol number or clear another feature's hold through this draft.

Normal Graph/product builds, fresh artifact recovery, installed SDK consumers, full test aggregates, other platforms, full LLVM aggregate execution, GPU and Studio acceptance remain unrun for this checkpoint. No artifact or seed publication is authorized here.

