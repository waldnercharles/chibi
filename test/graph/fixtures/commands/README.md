These expected recipes are independent fixture data. They are not captured from
the planner during a test. Review changes to the complete files with changes to
the owning command policy.

The real Driver executes a release dry-run with jobs 2 and the default three
native products. Its success status, empty stdout and complete stderr are
checked separately. A missing compiler must return 1 with the exact diagnostic
and empty stdout. `core:os.Print` writes the plan and diagnostic to stderr.
The compiled Graph and third-party entry paths are prepared
inputs to that Driver invocation.

The real Graph executes commands-only with explicit Release tools and the five
selected native product and C fixture targets. Its compile inventory covers
those selected targets and their dependencies. `compile.json` contains all four C fixture sources.
Each platform's link table contains the native compiler, asset service, facade
builder, facade archive and C fixture executable, in order. Tool paths with
spaces, flags, includes, sources, outputs and every argument remain compared.

Only the observed build-root spelling is replaced with `/BUILD`. Static data
tokens substitute explicitly supplied checkout, compiler, LLVM and prepared
entry paths. No recipe is obtained from the planner to form an expectation.
Both distinct build roots must match; a third real Graph invocation with a
different C compiler must disagree in both complete tables.

This checks the current native products independently of the historical
selected C compiler-host comparison. That optional comparator remains during
the transition; its 88 C compile entries and 33-argument link describe the old
product, while these fixtures describe the native products and test-owned C
fixtures.
The old raw tables and VM comparison evidence remain in the workspace's
`evidence/parity-1f86bd5a6` and `parity-reference-handoff.md`. This fixture does
not establish language/runtime or Windows execution parity.
