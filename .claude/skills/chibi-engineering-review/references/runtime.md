# Runtime, VM and data formats

Run this pass when the change touches the VM, JIT, WebAssembly or native backends, the ECS and components, asset loading, prepared artifacts, or Edit-and-Continue state.

## Interpreter and JIT

- **Per-instruction overhead.** What runs for every instruction besides the instruction itself: a call into the step function, mode checks (scope, purity, budget, tracing), reloading machine state from a struct, copying whole operand values, writing a type tag the compiler already knows? Count the instructions per handler for the hottest opcodes. Each one is paid billions of times.
- **Loop-invariant modes.** Is a mode that can't change inside the dispatch loop tested on every iteration? Checked, debug or tracing modes belong in a separate loop or a swapped dispatch table.
- **Fast paths.** Does each opcode have one fast path with cold slow paths out of line? Are operand kinds encoded in the opcode, so a handler decodes only the operands it uses? Are stack effects verified once at load, so handlers carry no bounds checks?
- **State in registers.** Do the program counter, frame base and constants stay in locals across iterations, or does every step write them back?
- **Dispatch claims.** Was the misprediction rate per bytecode measured before a dispatch rewrite (computed goto, tail calls, threading)? A switch is fine; the cost is usually around it.
- **Engines in step.** Does every engine (interpreter, JIT, WebAssembly, native) consume the same IR and pass the same differential test? Does any backend contract floating point (FMA) when the IR didn't ask?
- **JIT limits.** Is every JIT resource capped, with a clean fallback to the interpreter? Is code whose interpreted behavior is an error never compiled?
- **Temporaries.** Before tuning the allocator or collector, has the temporary been shown to be necessary? Values built in their destination (`push(f())`, returned aggregates) and small aggregates kept inline avoid the work entirely.
- **Native calls.** Do natives take unboxed scalars and bind at compile time, or is every argument marshalled into a general value? Are host APIs pull-style, without per-element callbacks into Chibi?

## Runtime data

- **Value and instruction size.** What is the byte size of a runtime value and of a bytecode instruction, and is it pinned? Per-value pointers to type names, records and schemas belong in per-type tables.
- **Bulk arrays.** Does a system keep its objects in one array with holes and a free list, with `{index, generation}` handles where 0 means none and deletion is O(1)? Does a big array regrow where fixed-size blocks would avoid spikes?
- **Per-entity work.** Does behavior run as one pass over each component table, or as a call per entity? Is per-entity state that loops filter on held as list membership?
- **Collector.** Does the collector's sweep read only its own metadata, and does the write-barrier fast path avoid touching collector state? Do hot component tables hold managed pointers the collector must trace?
- **Shared lines.** Do threads write the same cache line on a hot path, debug counters included? Is a lock held across work whose size the input controls? Lock-free code needs measured contention to justify it.
- **Uptime growth.** Does any state grow with uptime, session length or edit count without a bound?

## Formats and prepared data

- **In-place reads.** Can the artifact be used where it lies, without parsing or pointer fixups? It needs offsets or typed indices, aligned scalars and a fixed byte order.
- **Verification.** Is data from outside the process checked once at load, with depth and count limits, so the readers can trust it?
- **Versioning.** Does the artifact record its type and format version, and is a mismatch rejected so the artifact is regenerated, rather than read leniently?
- **One format per job.** Is one format stretched across storage, messaging and version-tolerant interchange?

## Edit-and-Continue state

- **Where state lives.** Does state that must survive an update live in `retain` bindings or one record per system, rather than scattered mutable globals?
- **Layout changes.** Does the change remove a retained field, insert one in the middle, change a default or change a width? Each needs a migration or a deliberate restart from the initializer.
- **No addresses.** Does retained state hold function values, addresses or pointers into code that the update replaces?
- **Coverage.** Does every reload path have a test or daily use, and do a module's tests rerun on reload?

## Determinism

- **Math.** Does gameplay math depend on FMA contraction, fast-math, platform math libraries, CPU-dispatched paths or per-thread floating-point modes? Does any float comparison use a tolerance it doesn't name?
- **Order.** Does any output (events, diagnostics, generated code, prepared data) depend on hash iteration order or addresses?
- **Hash per engine.** Does a determinism hash of a scenario run on every execution engine?
