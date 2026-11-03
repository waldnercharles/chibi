`run.sh COMPILER LLVM [ORDINARY_COMPILER]` checks the unchanged authored pattern
suites, `&&` short circuit and source mutation, shadowing, tag-guarded payloads,
loop refresh, break/continue/return, plain suspension, and malformed facts.
The existing managed-values runner also poisons freed storage, requires balanced
allocations, and observes that a continued loop releases captures before its next
subject call.

`rejected/OwnedWaiting.chibi` records accepted ordinary behavior still blocked by
the native task frame's managed-value domain. It must become a positive case when
that domain opens; the normal owned captures and plain waiting cases already run.
