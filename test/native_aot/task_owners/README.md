# Owned coroutine frames

The canonical `test/native_aot/tasks/run.sh` also runs this suite.

Family generations support force unwrap through their context failure path.
A nil unwrap reports `UnwrappedNil` and returns through synchronous callers or
waiting parents before the host releases roots and closes every image. Calls
that can propagate a context failure reuse the active owner entries, including
fresh arguments and historical loop containers. Ordinary libraries without an
execution-family owner keep their existing rejection.

`TypedGeneration.chibi` checks successful unwraps and controlled failures in
Main, Frame, retired workers, yielding and immediately completed child awaits,
and an immediately failed spawn. Markers after unwrap, await and spawn must
remain absent on failure. The same image-close poison and allocation-balance
audit covers these paths at O0 and O2.

`RunWindows.ps1` runs these seven typed-generation modes at O0/O2 with the
production Host body and DLL link plan. Its temporary Host overlay checks each
image's poison allocator immediately before the real `FreeLibrary`, then
requires successful close and deletion of every generation file. The explicit
check replaces the POSIX unload destructor because Windows DLLs keep `/noentry`.
A phantom allocation must produce the illegal-instruction trap at this boundary;
the normal typed failure status134 cannot satisfy that control. Actual Windows
execution is required in addition to cross-linking these fixtures.

Program and family coroutine frames can own strings, arrays, maps and records/unions
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
`MapPromises.chibi` covers direct and observed map results, repeated independent
joins, returned parameters, empty/nested maps, arrays of maps and cancellation.
`map_programs/TaskMaps.chibi` preserves the original direct/observed array-of-map
promise fixture under the same poisoned allocation audit.
`MapPromiseFailure.chibi` checks a failing child with an owned map parameter while
the caller still holds a completed map result. Generation tests mutate one joined
map and require the original completed promise to remain unchanged after reload.
`MapJoinAllocations.chibi` joins one 4096-entry promise 128 times through the
real allocation audit. Read-only joins allocate no backing; the first mutation
detaches the entries and hash index once, and the second reuses them. The audit's
read-only counter is a test-only LLVM symbol, not a runtime API.

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

Maps use their existing counted payload lifecycle. Joins acquire copies, inline
awaits move and zero the child promise, and frame destruction clears the promise
before dropping it once. Borrowed waiting reference parameters remain closed;
backing-derived array/map references cannot cross suspension or escape their
immediate access region. Collector-bearing result roots have separate coverage
in `managed_pointers/run.sh`.

`TaskObservers.chibi` preserves inferred arrays of eight-byte task observers,
including nested arrays, indexed and loop reads, source replacement while
awaiting, cancellation and discarded arrays whose children still finish.
Physical backing copies acquire observers; last backing release drops them.
Sharing a descriptor counts its backing and does not visit task elements.
`TaskObserverAllocations.chibi` requires 128 copies of 256 completed observers
to allocate nothing, then one backing detach for the first mutation and none
for the next. Invalid forgiving replacements must release all completed
payloads at shutdown. Original-generation completed observers survive reload
and close with a balanced allocator. `TaskObserverFailure.chibi` covers failure
after transferring an indexed observer out of its array.

Direct task-valued record fields and explicit task-array parameters/results
retain the ordinary compiler's rejection. Native typed task-array promises
reuse the existing array copy/drop contract; source syntax does not gain a new
parameter or result type.
