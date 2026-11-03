# Native package generations

On Linux x86-64 or macOS arm64, run the real native compiler and host regression with LLVM 22
or later, using absolute paths:

```sh
sh test/native_aot/generations/run.sh /path/to/chibi /path/to/llvm/bin
sh test/native_aot/generations/run.sh /path/to/chibi /path/to/llvm/bin 2
```

The runner compiles `apps/host/Host.chibi` and actual shared generations through
the native compiler. It compiles edits while the original generation runs and
publishes each edit by renaming `next.so`. No VM build or compiler host runs.
The optional third argument selects optimization level 0 or 2. Package fixtures
signal readiness after three frames; the test publishes edits after that signal.
The pure legacy library fixture verifies scalar conversion after the host's
mandatory first Frame, without a timing delay or foreign I/O.

The package swaps check Main running once, numeric conversion, retained record
fields surviving reordering, new fields keeping their initializers, and
retained strings surviving the old mapping. Library and sibling-import cases
exercise both shared compilation routes. Runtime cases cover arrays, formatted
strings, assertions, recursive retained writes, and process arguments.

Numeric/bool retained arrays migrate as complete leaves with a strict element
schema, including array fields of plain records. Their initializers must be
empty. The suite rejects an incompatible array edit while the original keeps
running, then applies a compatible edit. Foreign writes and scoped mutations
must reach the next Frame. `ArrayProbe.chibi` checks independent snapshot
backing and balanced repeated state release with poisoned frees at O0/O2. The
allocation counter bounds 4,096 appends to at most 16 allocations for a retained
array and a direct record array field. Nested record array leaves are copied,
edited and assigned back; direct nested array mutation remains rejected.
Retained chars preserve their identity across an edit;
int/char changes reject the candidate before any state is copied. The native
setter also rejects invalid Unicode scalar values without changing prior state.
Source record char fields remain rejected by the ordinary checker; malformed
facts tests check nominal member metadata independently.
The old generation's yielded task mutates its original retained array after the
swap; the incoming snapshot must stay independent. Managed and nominal scalar
arrays, including `char[]`, remain rejected.

Invalid Main or Frame signatures and waiting entry functions must produce
specific diagnostics while preserving the prior shared object byte for byte.
An incompatible retained schema must leave the original generation running.

Tasks started by Main must advance before any edit arrives, and an active
task assertion must propagate status 134.

The task failure case keeps a worker suspended until the edited Frame signals
that it is running. The worker then resumes an old-only helper that records a
marker and fails an assertion. The host must return 134, proving that a retired
generation's task failure reaches the caller. The CLI watch regression owns
the corresponding successful old-generation continuation case.

The shared-family cases also keep typed cyclic record cells in an old worker
while a new generation changes the record layout and churns its own heap cells.
A direct image probe checks that generated metadata holds a live code lease,
and that cancellation and collection release every lease before image close.
