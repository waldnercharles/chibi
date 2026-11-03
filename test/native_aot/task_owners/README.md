# Owned coroutine frames

The canonical `test/native_aot/tasks/run.sh` also runs this suite.

Program and family coroutine frames can own strings, arrays and records/unions
whose lifecycle has no collector-bearing leaves. Each distinct owning local or
fresh slot has one frame-resident active address, eight bytes on desktop. Calls
reuse that address entry. Before cancellation/failure drops a value, it clears
both the canonical descriptor and the active entry. Ordinary scope cleanup
continues to own normal returns. By-value waiting parameters acquire independent
owners before body calls can fail or suspend.

`Main.chibi` covers reassignment, borrowed call inputs, array traversal snapshots,
discarded observers, repeated awaits, timed waits and parent/child cancellation.
The promoted pattern fixture preserves a dynamic union payload after replacing
its source and yielding. `Failure.chibi` mutates a canonical record through a
scoped reference before propagating an assertion. `Mixed.chibi` combines the new
owners with existing collector pointer-array roots and real formatting.
`Promises.chibi` exercises owned string, array and record results, repeated
observed joins, independent result mutation, returned parameters, inline-await
moves, discarded results and cancellation with a completed promise still held.
`PromiseFailure.chibi` checks propagated failure after copying and changing an
owned result. Generation tests keep a completed owned promise across the edit
and read it again through its original image.
`NamedPromises.chibi` preserves direct async named outputs with a dynamic string.

The standalone allocator audit preserves the real context and formatter, poisons
freed storage and requires zero outstanding allocations at return. An imbalance
clears the context failure and returns99, so assertion status134 cannot hide a
leak. Both optimization levels execute the actual source programs.

`generations.sh` runs the real native host. The replacement removes the old
Worker and Payload declarations; the old task must finish using its original
code. Completion, shutdown cancellation and old-task failure run at O0/O2.
Each image's real allocation callbacks are audited, and its unload destructor
requires balance. A deliberately outstanding allocation must trap, proving that
the unload check runs. Start modules pass directly through `llc`, so their audit
is injected there; program bodies pass through `opt`.

The new owner path does not admit maps, views, borrowed waiting parameters or
collector-bearing inline aggregates. Existing direct pointer and pointer-array
roots remain separate. Task promises can own the same pointer-free values as
frames. Joins acquire copies, inline awaits move and zero the child promise,
and frame destruction clears the promise before dropping it once. Collector
pointers, views, maps, multiple results and task-handle parameters remain
unsupported. These are functionality gates, not
evidence that VM parity is complete.
