# Raw scalar facts checks

`Tests.chibi` retains accepted and malformed scalar-facts validation and LLVM
emission checks. `test/native_aot/scalar_producer/run.sh` compiles its `Tests`
entry with the native compiler and executes it, alongside the real source
producer and corpus checks. The native backend and target bundle suites execute
the resulting native code and enforce the cross-target ABI manifests.

The old prepared-host JSON batch transport, VM/SLJIT compiler recovery runner
and forwarding `Regression.chibi` module are retired. They are not alternate
compiler or recovery routes. Their historical evidence remains in prior commits
and preserved worktrees.
