# Allocation and copy explanations

The front end, `chibi check --explain-costs`, was archived with the
game-project compile path on 2026-10-29 (e8bc71909); the CLI now reports
`check` as an unknown command. The report is formatted by
`tools/compiler/cost_explanations`. This page records what the command did.

`chibi check /path/to/project --explain-costs` validated a project and
printed its static allocation, copy and retention evidence. `--json` gave a
versioned JSON report on stdout. Invalid projects fail normally and emit no
report. Project checks do not run game functions, migrate assets or write
prepared artifacts. The cost formatter disables persistent caching.

The report covers finalized function bodies and their linked callees. Each
entry records the first evidence for every cost kind and certainty, its reason,
and its originating source range when available. Imported evidence can point
into a different document. Missing ranges are explicit. Binding, subscription
and query evaluator roots and backend/activation setup are outside this report.

`present` means an operation exists in the finalized compiled body. A branch
may never execute it. `possible` covers conditions such as nullable values,
capacity, caches and native calls without a verified cost contract. Both can
appear for one cost kind. `traversal_complete` says the bounded analysis visited
the graph it covers; it does not establish that native calls have known costs.

Allocation means **every dynamic storage request, including arena and pool
requests**. Backing allocations are a separate measure. This report measures
neither. Its static copy evidence does not give payload bytes, and retaining a
value can prolong backing storage without copying its payload. Runtime traffic,
GC costs and detailed argument-lending causes remain unavailable in this slice.
The report provides no allocation-free or frame-time guarantee.

The SDK exposes raw per-function facts in `authoring_source.costs` and
`cost_count`, declared in `authoring/source_prepare.h`. Their evidence pointers
borrow the immutable source generation and its retained imported modules. Keep
that generation alive while using them. Native authoring clients can call
`authoring_sources_cost_report` from `authoring/source_costs.h`, provided by
`Chibi::project_compilation`, for the same text or JSON formatter used by the
CLI. The caller frees the returned text. Invalid source collections and reporter
failures return an error instead of an empty success report.

Report interpretation and formatting live in
`tools/compiler/cost_explanations/Explain.chibi`. The native adapter transports
the existing finalized analysis facts; it does not implement another collection,
ownership or borrowing analysis. Later enforcement must establish transitive
coverage before claiming that an entry makes no dynamic storage requests.
