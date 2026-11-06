# Chibi

Chibi is a game language with its own compiler, runtime, engine and SDK. The stage-one compiler (`tools/compiler`), build graph (`tools/toolchain`, `tools/graph`), CLI (`apps/chibi`) and core packages (`packages/`) are written in Chibi. The runtime, VM, stage-zero compiler (`src/language`) and test drivers (`test/`) are C.

## Engineering rules

Programs transform data. Each rule below lowers a cost of writing, reading, changing, running or debugging code. Running cost is paid on every user's machine, so it breaks ties. When following a rule's wording would raise the cost it exists to lower, do the cheaper thing and say why.

### Performance

- Non-pessimize everything: do only the work the problem requires. No allocation, copy, hash, string building, re-parse, repeated traversal, dynamic dispatch or layer that the problem doesn't need. This is how all code is written, not a later optimization step.
- Bound growth first. Assume any size a user controls reaches a thousand times what you tested. Inside loops over nodes, tokens, entities or assets, lookups by name or kind take constant or logarithmic time, or a comment states the bound on the count.
- Before writing or changing hot code, estimate. Count the items and the work each one truly needs, and compare that with what the machine can do: a core runs on the order of a billion simple operations per second, eight times that across 8-wide SIMD lanes, and a trip to main memory costs hundreds of cycles. Say whether the work should take a frame, a second or a minute. When the measured cost is significantly more than the estimate, find out why.
- Remove cost in this order: work that needn't happen, then serial dependency chains and unpredictable branches, then SIMD width, then bytes moved through the caches, then threads. The first step is usually the largest. Use branchless code, SIMD or threads only in a measured hot loop, with the timing or hardware counter that shows the gain, and keep the plain version as the reference.
- Know what reused code executes. Before calling a general routine from a hot loop, trace one call through everything it runs. When it does work this caller doesn't need, write the specific routine; specific code is usually smaller than general code.
- Keep per-element loops free of allocation, hashing, string comparison, map insertion and parsing. Gather loop-invariant data once before the loop, build lookup tables once per process, reserve capacity before appending, and reuse large buffers across frames and files.
- Hot loops never re-test a mode that can't change inside the loop (budgets, purity, debugging, tracing). Run checked or instrumented modes through a separate loop or dispatch table.
- Specialize to what the caller knows (static data, a fixed direction, bounded counts, a closed set of kinds). Precompute a representation shaped for the query when data is read far more often than it changes.
- Return only what the caller needs, and encode "absent" in a sentinel when that is free.
- Handle the common case on the main path. Detect rare and legacy cases and send them down a separate path.
- Keep a path to fast code open. A per-object heap graph where a flat array would do, a per-item call where a batch would do, or dispatch that blocks inlining in a hot loop closes it, and later fixes become rewrites.
- Design hot layers (the frame loop, VM, compiler passes, asset import, and the build and test loop developers wait on) for cost from the start. One-off tools and probes may start simple, and get their second pass once people wait on them.
- Hand-tune measured hot spots. A performance change states its baseline and a numeric prediction before the work, then the measured result; a speed tip without a measurement is folklore. Deleting obvious work needs no measurement.

### Know the data

- Before changing a data structure, a compiler pass, or a hot loop, find out what it transforms, how many records exist in the typical and worst case, and who reads the result.
- Give each closed set of kinds one definition (an enum where the language has one, or named constants declared once), and generate its name table and dispatch from that definition. Dispatch on it with an exhaustive `switch`. Intern identifiers when lexing and deduplicate types, so names, kinds and types compare by id. Write bytes as character literals, or as named constants in code that stage one compiles.
- Refer to long-lived, serialized or hot-reloadable records by a typed index or a generational handle in which 0 means none. Pointers are for pass-local use.
- Keep one representation per concept. Store each fact once, and recompute what is cheap to recompute (line and column, end offsets). Store optional or rare facts in a side table keyed by the owner's index, not as a nullable field or `has_` flag on every record.
- Represent state as membership. When a hot loop filters records on a `bool`, keep them in separate lists instead; "needs work" is a worklist that gets drained, not a flag checked on every pass. Group records by kind before the loop so no per-item dispatch remains.
- Make each record's all-zero value valid and empty, and return expected absence as `none` or an empty value.
- Give each high-count record (token, syntax node, IR instruction, bytecode instruction, runtime value) a byte budget pinned with a static assert and a one-line reason. Rare payloads go to side arrays.
- Choose a layout (SoA, packing, handles, SIMD) from the loop it serves, and name that loop's access pattern and batch size.

### Formats and reload

- Serialized and prepared data use offsets or typed indices, never pointers, with aligned scalars of fixed byte order, so it can be read in place. Verify data from outside the process once, at load.
- State that survives an Edit-and-Continue update lives in `retain` bindings or one record per system. Every reload path has a test or is used daily.

### Structure

- Write the calling code first (the test, scene or call site), then the API it needs. Keep the common case to one call, and return handles rather than pointers into internal storage.
- Add a helper, option, parameter or module once a second real caller exists. A generic container, type parameter or interface waits for a third. With two callers, compare them before merging, and leave copies apart when they are likely to diverge. When shared code doesn't fit a new caller, add a layer above it instead of a mode flag inside it.
- Write passes as transforms: read the input without changing it, produce a new output, and write no globals.
- Organize code by operation: one procedure handles every kind for one operation, so the pattern across kinds stays visible. Run behavior as one pass over each component table, never as a callback per entity.
- Dispatch with a direct call, then tagged data and `switch`, then a constant table indexed by kind. Function pointers belong at allocator, OS, I/O, task and user-extension boundaries. Return arrays or event lists instead of taking callbacks.
- Count the layers between a caller and the work, and delete layers that only forward (wrappers, managers, adapters, pass-through registries). Code volume is a cost.
- There is no length or parameter quota. Keep single-use sequential code in one procedure, and split out a block when it earns its own name, gains a second caller or crosses a real boundary. A procedure over about 100 lines should be a lexer or state machine, a `switch` over a closed set, or a pipeline of commented blocks.
- When the same values pass into and out of several procedures of one pass, make them one state record passed by reference.

### Failure

Classify every failure:

- A bug (a broken internal invariant) gets an `assert`. Asserts have no side effects and never check input. A state that only a bug can produce stops the program; it is never answered with an early return or a substitute value.
- Invalid external input (source programs, assets, prepared artifacts, public API arguments) is rejected with a diagnostic that names the input, leaving outputs untouched. Test the rejection.
- An environment failure (files, processes, the OS) returns an error value. Out of memory aborts; that is deliberate.

Then:

- A `switch` over an internal kind is exhaustive, and its `default` arm asserts it is unreachable.
- Front-end passes and asset loads return their output, partial where needed, together with a list of diagnostics. The driver checks the list at phase boundaries; callers never re-wrap diagnostic payloads.
- Keep error results precise within a package. Distinct failures stay distinct values.

### Lifetime

- Chibi values are managed: the runtime's collector reclaims unreachable values at safe points, and arrays and maps copy on write. Design their lifetimes through reachability. Keep high-count data (component rows, syntax nodes, IR) as arrays of plain records linked by index or handle rather than managed pointers, so the collector has nothing to trace inside them.
- Treat such an array as the arena for its lifetime (program, compile, pass, frame): size it once, clear it at the boundary instead of rebuilding it, and drop the last reference when the lifetime ends.
- Collection is not cleanup. Files and other external resources close at an explicit point.
- In C, memory the collector doesn't own belongs to a lifetime group and is freed with its group. A function that returns such memory takes the destination arena as a parameter named for its lifetime; its temporaries use a scratch arena that is never the destination. Document who owns returned memory and what invalidates it, and re-fetch pointers into a growable array after every append.
- Mutable globals are limited to a registry of top-level objects or a process-wide hook. Tables that never change are constant.

### Determinism

- Output order (diagnostics, symbols, generated code, prepared data) follows source order, never hash iteration order or addresses.
- Gameplay math doesn't depend on fused multiply-add contraction, fast-math, platform math libraries or per-thread floating-point modes. Float `==` stays exact; an approximate comparison names its tolerance.

### Replace, don't layer

- Keep one implementation per behavior. Where two exist, add features only to the one that will remain, and delete the other in the same change series once the survivor covers it.
- Every execution engine (interpreter, JIT, WebAssembly, native) consumes the same IR and passes one differential test.
- A replacement is finished when the old path and its aliases, flags, tests and docs are gone.
- Register each test selector, build target or component in one place and derive every other list from it.

### Language changes

A language feature must pass this gate:

- a program needs it;
- inference or an existing form can't express it;
- the case is common;
- it adds no second way to do something;
- it hides no allocation, branch or control flow;
- it parses with the stage-one tokenizer;
- it has been tried on real code, with its uses counted.

Count each feature's uses over time, and remove one that real programs don't use or that costs more upkeep than it saves.

### Names and comments

- Names state the capability. Skip filler words such as `Manager`, `Data`, `Info`, `Util`, `Helper` and `Misc`, and don't repeat the package name. Wider scopes get longer names; short names stay local.
- Comments state contracts and reasons: who owns, what allocates or invalidates, what is asserted, units, and what may be `none`. The code says what it does.

### Chibi that stage one compiles

- The compiler, the `core:` packages and the build graph may use only what both compilers accept. `docs/language-friction.md` lists what stage one still lacks, and notes that `Build.chibi` must parse with the build host's own syntax.

## Tests

- Every fixed bug gets a regression case that fails without the fix.
- Test each pure transform with tables of inputs and expected outputs, without mocks.
- Run new language and engine features through a real scene or program, not only through the compiler compiling itself. Test behavior and invariants, not the shape of the implementation.
- Check an optimized path against a simple reference implementation on the same inputs.
- Reproduce a flaky failure in a loop before changing anything; a workaround states what remains unexplained.
- Change a reproducibility tripwire only on purpose, with the reason in the commit message.

## Review

Before committing, review the whole diff with the `chibi-engineering-review` skill (`.claude/skills/chibi-engineering-review/SKILL.md`).
