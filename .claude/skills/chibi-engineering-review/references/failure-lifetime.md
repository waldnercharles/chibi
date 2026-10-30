# Failure and lifetime

## Failure

Classify each failure path first. A bug gets an `assert`. Invalid external input (source programs, KDL assets, public API arguments) gets a diagnostic, with outputs left untouched. An environment failure gets an error value. Out of memory aborts through `xmalloc` on purpose.

- **Assert misuse.** Does an `assert` check input, or have a side effect (a call, increment or assignment inside it)?
- **Silent guards.** Does the code return early, or substitute a default value, on a NULL or a state that only a bug can produce? *Patterns:* `if (!x) return`, `?? 0` on an invariant. *Fix:* replace the guard with an `assert`.
- **External input.** Is invalid external input rejected with a diagnostic that names the input? Are outputs left untouched? Is the rejection tested?
- **Collapsed errors.** Are distinct failures collapsed into a `bool` or a catch-all value? Does one handler cover a block of different fallible calls?
- **Re-threaded diagnostics.** Does every caller re-wrap and return the callee's diagnostic payload (`case .Failed(d): return X.Failed(d)` chains)? *Fix:* record diagnostics in a list, and return one failure marker.
- **Lost partial success.** Is useful partial work thrown away on the first error (for example, the other files of a multi-file compile)?
- **Diagnostics with output.** Does a front-end pass or asset load return its output (partial where needed) together with a diagnostics list checked at phase boundaries, or does each caller re-wrap a failure payload?
- **Nil placeholders.** Where a lookup returns a placeholder for expected absence, is the placeholder read-only, and do states only a bug can produce still assert?
- **Trapping asserts.** Does an assert stop at the failure site and report file and line? An assert defined as "unreachable" without a trapping build turns a bug into silent undefined behavior.
- **Root cause.** Does the fix address the cause that was actually understood, or does it mask a symptom with a retry, sleep, clamp, extra check or widened tolerance? *Evidence:* the explanation of the cause, in the commit or PR.
- **Accessor asserts.** Do new core data structures assert tags, bounds and sentinels at their accessors?
- **Released-build behavior.** Does correctness depend on an assert firing, even though asserts may compile out in some builds?

## Lifetime

For every allocation and every retained pointer or reference, determine who keeps it reachable or owns it, its lifetime, its reset point, whether it can move, and whether aliases outlive a mutation. Chibi values are collected; C memory outside the collector is owned explicitly.

### Chibi values

- **Traced records.** Does a high-count record (component row, syntax node, IR instruction) gain a managed pointer, so the collector must trace every row? An index or handle into a plain-record array avoids it.
- **Rebuilt arrays.** Is a large array rebuilt every pass or frame where clearing it and reusing its capacity would do?
- **Lingering references.** Does a reference to a large table outlive the pass or frame that needed it, keeping it reachable?
- **Hidden copies.** Does a mutation copy a whole array or map because the value isn't uniquely held (copy-on-write)? `chibi check --explain-costs` shows copies per function.
- **Cleanup by collection.** Does a file, handle or other external resource depend on collection to close? Collection timing is not a cleanup guarantee.
- **Collector cost.** On a hot path, has the collector's cost been checked against the frame budget?

### C memory

- **Lifetime group.** Does an allocation the collector doesn't own lack a lifetime group (program, compile, pass or frame)? Are objects freed one by one where an arena fits?
- **Destination arena.** Does a function that returns allocations take the destination arena? Is its scratch arena guaranteed not to be the destination? Is every temporary scope closed on every path, and every pooled object re-zeroed before reuse?
- **Allocating lookups.** Is a lookup that allocates or fills a cache named and treated as a mutation?
- **Orphans.** Does destroying an entity or object leave rows behind in other tables?
- **Growable storage.** Is a pointer into an `apush` array, or other growable storage, held across an append or resize?
- **Documented ownership.** Is returned memory documented with its owner and what invalidates it?
- **Temporary memory.** Does scratch memory come from the frame or pass arena?
- **Ownership cascade.** Does the parent own the child? A child that holds a back-pointer to its parent needs a reason.
- **Debug poisoning.** Do debug builds poison reset arenas and freed memory?
- **Globals.** Is a new mutable global anything other than a registry of top-level objects or a process-wide hook? Could it be `static const`?
- **Cleanup paths.** On a `goto fail` or other cleanup path, is it clear who frees what, and is nothing freed twice?
- **NULL checks after aborting allocators.** Checking for NULL after `xmalloc` is dead code; `xmalloc` aborts.
