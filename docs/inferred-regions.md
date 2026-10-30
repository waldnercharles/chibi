# Inferred regions

Status: proposal, 2026-10-30. Not implemented and not approved. This note
explores one direction from the engineering style guide research: giving
Chibi arena-style bulk lifetimes without an allocator API or new syntax.

## Summary

Chibi values are garbage collected by a precise, non-moving collector, and
Chibi code has no allocator API. The research behind the style guide
recommends grouping allocations by lifetime (frame, pass, call) and freeing
each group at once. This note proposes that the compiler infer those groups
instead of asking programmers to name them:

- An allocation whose value provably dies before a scope exits goes into that
  scope's **region**, which is freed in bulk when the scope exits. The scopes
  are ones the language and runtime already have: a call, one `Frame :: ()`
  invocation and one behavior event (a tick). A compiler pass is a call too,
  but the measurements show passes are too large to help.
- Every other allocation goes to the managed heap, as today.
- The cost report shows where each allocation goes and why, and can fail a
  check when a named function lets an allocation escape.

The runtime already does half of this. Each program activation (one `Frame`
call or one behavior event) owns a collectable `payload` arena that is reset in
bulk when the activation finishes, and strings, records and union payloads
allocated while it runs go there. Arrays, maps and pointer cells always go to
the shared managed heap, and the compiler and build hosts put every value in
that one heap. The proposal is therefore mostly an extension of existing
storage, driven by an escape analysis the compiler doesn't have yet.

Measured on 2026-10-30 ([Opportunity](#opportunity)):

- A stage-one self-compile allocates about 396 million values (52 GB) per
  compile of 36,415 source lines, and spends 31% of its run time in 299
  collections that each re-trace up to 605 MB of live values. In a sample
  weighted by allocation, 85% of values die before the call that allocated
  them returns and 98% before its caller returns. Half die in calls that
  allocate less in total than the heap allocates between two collections.
  Passes are too large to be useful regions: one call holds nearly a whole
  compile.
- No game scene could be measured: game projects are archived on `main`, and
  every in-repo scene fails to prepare at the last commit before the archive.
  A stand-in `Frame` program over 1,000 bodies allocates 6,430 values per
  frame. 56% already go to the activation payload; of the rest, 65% die within
  the frame and 35% survive one frame through `retain` state. Collection costs
  2% of its frame time.

The allocation counts matter more than where the values go. Removing
allocations comes first; regions are the second step, and their main payoff
in the compiler is fewer collections.

## What exists today

Verified against `main` at b8babd3c3 on 2026-10-30.

| Storage | What goes there | Freed |
| --- | --- | --- |
| Callee frame arena, `invocation_frame.storage` (`src/execution/call.c`) | The copied argument array, results, aggregate result slots and watch storage of a non-inlined call. A bump arena. | `arena_reset` when the call returns (`frame_retire`). |
| Activation payload, `execution_activation.payload` (`src/execution/program_activation.c`) | Strings, records and union payloads that a program activation and its callees allocate (`call.c` binds callee value storage to it when it is collectable). | In bulk when the activation finishes: "All escaping values already have their own storage." Also swept by program collections while it is live. |
| Managed heap, `behavior_managed_heap.storage` (`src/execution/managed.h`) | Array and map shells, pointer cells, and records or unions that contain collections (`array_storage` in `src/execution/function.c`). In the compiler and build hosts, every value. | Collection only. Programs collect between runs once 4 KiB is allocated and at least as much as the last collection kept. The compiler and build hosts collect at call boundaries once 64 MiB is allocated and twice what was kept. |
| `malloc` | Array and map backings (atomically reference counted for copy-on-write) and string bytes (`pin_text_owner`, reference counted). The heap holds only their shells and anchors. | When the last reference is released, usually from an arena cleanup during a sweep or reset. |

The compiler already has several analyses that this proposal builds on:

- **Scoped `^T` parameters.** A pointer parameter the callee only
  dereferences, reads fields of, or forwards to another scoped parameter is
  scoped. A caller's `&local` passed there stays a frame slot instead of
  becoming a heap cell. Stage zero implements it in
  `src/language/callable_headers.c` and stage one in
  `tools/compiler/package_compiler/References.chibi`. Both are a syntactic walk
  per parameter, optimistic on recursion, and treat any callee they can't
  resolve (imported or qualified) as retaining.
- **Argument sharing** (`execution_prepare_argument_sharing`,
  `src/execution/module_effects.c`). A whole-call-graph analysis decides that a
  callee "leaves storage it did not allocate untouched", so `call.c` lends
  arguments without copying them.
- **Results built in the caller's storage.** Intrinsics flagged
  `EXECUTION_INTRINSIC_IN_RESULT` allocate into `result_storage`, which a call
  binds to its caller's value storage, so a returned value needs no copy. Only
  stage one emits the flag (`InResult` in `Body.chibi`). This is Fleury's
  "pass the destination arena" pattern, inferred.
- **Union scalarization** (`src/execution/scalarize.c`). A per-function escape
  analysis replaces union values that never escape with their tag and fields
  and drops their construction. Any non-intrinsic call is a boundary.
- **Read-only borrowing** (`src/execution/borrow.c`, `opt.c`). Proven read-only
  callees and loops alias arrays instead of copying them.

None of these tracks where an allocation's value can flow, which is what a
region needs.

## Mechanism

### The rule

An allocation site gets a region when every value it creates is dead, on every
path, before some enclosing scope exits. It goes into the region of the
**innermost** such scope. Otherwise it goes to the managed heap.

A value **escapes** a scope when, before the scope exits, it can be:

1. returned from the scope's function, unless the function builds that result
   in its caller's storage (then it is the caller's allocation);
2. stored into storage that outlives the scope: `retain` and behavior state,
   a global, a field or element of a value that outlives the scope, a managed
   pointer cell that outlives the scope, a queued event, a task;
3. passed to a parameter that may do 1 or 2, a native that isn't marked
   `borrows`, or a callee the analysis can't see;
4. captured by a delayed continuation or a suspended activation.

Scoped `^T` parameters are the special case of rule 3 for pointers that already
exists. The proposal generalizes their one bit, "this parameter's argument
doesn't outlive the call", to every parameter of every type, and adds the
missing half: per allocation site, "this value doesn't outlive the scope".

### Where the analysis runs

In stage one only, as a pass over each function after lowering and inlining,
before the prepared form is written. Each allocating instruction gets a
placement:

- `heap`, the default and what stage zero keeps emitting;
- `region`, the innermost non-inlined call or activation whose exit the value
  can't survive.

The prepared form carries the placement as a flag on the allocating
instruction, the same way `InResult` (256) already marks intrinsics that build
in the caller's storage. Parameter facts ("lends" versus "retains") are a
per-function summary computed bottom-up over the call graph, with recursion
handled optimistically as the scoped `^T` walk does today and the argument
sharing verdict does with its pending state. A callee's placements don't depend
on its caller, so no function is specialized by placement, unlike Lobster,
which specializes by ownership.

Stage zero doesn't change. Its output stays correct, only slower, which is
consistent with deleting stage zero rather than extending it.

### What a region is

A region is a collectable arena owned by a scope instance:

- **A call region** is a collectable arena the call's frame owns, created on
  the first region allocation and reset when the frame retires or fails. It is
  separate from `invocation_frame.storage`, which stays a bump arena of
  argument and result slots.
- **An activation region** is the existing activation `payload`. Strings,
  records and unions already go there; the change is that arrays, maps and
  cells proven not to escape the activation go there too, with their backing
  storage (below), instead of to the shared heap.

Using a collectable arena rather than a bump allocator keeps two properties:

- Region values can hold references into the heap (a local record holding a
  heap array). A collection lists live regions among its arenas, as it lists
  activation payloads today, so it traces through them.
- A long-running scope that allocates and drops values in a loop doesn't keep
  them all until it exits; a collection still reclaims dead region values, so
  memory use is never worse than the heap's. The bulk reset is an addition, not
  a replacement.

The win is that most short-lived values never wait for a collection. The heap
allocates fewer bytes, so collections, which trace every live value, run less
often, and region slabs are reused hot in the cache from one scope instance to
the next. Regions don't shrink the live set a collection traces. A
bump-allocated region with no per-value headers or sweeping is a second step,
for scopes whose allocations the analysis can also bound (no allocation in a
loop), and only if measurement shows slab allocation and reset cost matter.

### Stores into longer-lived storage

The analysis is static and sound: a site whose value **may** reach longer-lived
storage on any path is a heap site. There is no write barrier, no runtime
escape check and no promotion by moving (the collector is non-moving and the
heap never points into a region).

The exception is storage that already copies. `retain` and behavior state
assignment (`state_assign` in `src/execution/program.c`), queued event
payloads, and component writes copy the value into their own storage today. A
store there doesn't make the source escape, provided the copy is deep enough
that nothing it keeps points back into the region. Today's state copy shares an
array's backing (`pin_value_collection_copy` increments its reference count),
so storing a region array into state must copy the backing. That is a real
O(size) cost at a visible store, and the cost report shows it as a copy at
that line. The alternative, falling back to the heap for any value ever stored
into state, keeps the cost model of today and loses the region for the common
"build a fresh array each tick, then keep it" pattern.

The first version should take the fallback and measure how many sites it
loses. LuaJIT's experience is that most objects escape through some rare path,
and its answer was to move the allocation onto that path (allocation sinking)
rather than to give up. The Chibi equivalent is promotion by copy at the one
escaping store, which is the same copy described above, chosen per site where
the report shows it pays.

## Interactions

### Copy-on-write arrays and maps

An array or map value is a shell in an arena plus a `malloc`ed backing with an
atomic reference count. A region array keeps its backing alive through the
region's cleanups, as heap arrays do through the heap's. Two rules follow:

- A value copy (`b := a`) shares the backing. If either copy may escape, the
  backing escapes, so the analysis tracks a backing's sharing set, not only the
  shell. In practice the site of `a` is a heap site whenever any copy of it
  escapes.
- Detaching (`pin_array_make_unique`) allocates a new backing for the copy
  being written. The new backing follows that copy's placement.

Moving backings themselves into the region (no `malloc`, no reference count)
is the larger saving and needs the stronger fact that no copy of the value
leaves the scope. Growth needs care there: a bump region abandons the old
block on every reallocation, which Wellons measures at about twice the memory
of a fixed array. The collectable region's size classes avoid that by
reusing freed slots.

Strings follow the same pattern: the descriptor is region-placed and the bytes
stay in a reference-counted `pin_text_owner` until a later step puts short
region strings' bytes in the region.

### `retain` bindings and Edit and Continue

`retain` bindings are state, so a store into one is the copying store
described above. Edit and Continue applies an update only between `Frame`
calls (`docs/edit-and-continue.md`). At that point every call region and the
`Frame` activation's region are empty, so a replacement never sees a region
value and `program_migrate_value` needs no region case. A changed function body
gets new placements; the old body's frames finish with the old placements.

Suspended activations (an `await` that spans frames) keep their payload, and
with this proposal their region, until they resume. A replacement already keeps
old code alive for frames that are still executing; their regions belong to
that code.

### Component storage

ECS components can't hold managed pointers, queries, tasks or unions
(`field_size_valid` in `src/ecs/ecs.c`), and writes go through
`store_component` into native layouts. A component therefore never references
Chibi value storage, and a component write is a copying store. Query component
references (`component: ^T`) are heap cells that resolve rows on every access;
they never point into a region.

### Native calls and `borrows` kernels

Natives marked `borrows` (the array kernels in `src/runtime/arrays.c`) only
read their arguments during the call, so they lend, like a scoped parameter.
Other natives get their arguments after the managed-boundary walk
(`behavior_function_host_arguments`), and nothing states whether they keep a
copy. The first version treats every non-`borrows` native as retaining. Each
native that the report shows blocking a region can earn the `borrows` mark with
a contract test, the way the array kernels did.

### The collector

The collector in `src/support/arena.c` is precise and non-moving, and a
collection already takes a list of arenas (activation payloads plus the heap
family). Live regions join that list. Nothing in the heap may point into a
region, which the analysis guarantees; a debug build should also check it.
Because nothing moves, there is no forwarding and no promotion by evacuation.

Debug and sanitizer builds should fill a reset region with a poison pattern, as
Blow recommends for reset arenas, and check at each reference store that the
stored value doesn't live in a region younger than its destination. That turns
an analysis bug into an immediate failure instead of a use after free.

### Safe points

Collections run at safe points: between runs in program hosts, and at call
entry and return in the compiler and build hosts, never while native or JIT
code holds untraced C locals. A region reset needs no roots: it happens when a
frame retires or an activation finishes, after its results have been copied or
built in the caller's storage, and it frees values that nothing outside the
scope can reach. It is therefore not a safe point and doesn't need one.

Every engine (VM, JIT, WebAssembly and native) reads the placement. The VM and
JIT already read allocation storage from the function context, so a region is
one more storage field there. An engine that ignores the flag allocates in the
heap and stays correct, so a differential test covers placement: the same
program must give the same results with regions on and off.

The runtime half of this is C: the collector, call frames and activations have
no Chibi replacement yet. The analysis, the report and the requirement are
stage-one Chibi, and none of it is added to stage zero.

## Reporting

The cost report records, per function, the first piece of evidence for each of
three kinds (allocation, copy, escape) at each certainty
(`execution_cost_summary` in `src/execution/function.h`, formatted by
`tools/compiler/cost_explanations/Explain.chibi`). Its front end,
`chibi check --explain-costs`, went to `archive/` with the game-project compile
path on 2026-10-29 (e8bc71909): the CLI still parses the flag, but `check`
fails with "game projects are archived". The report belongs to whatever `check`
becomes for packages and programs. Regions need two changes to it:

1. **A placement per allocation site**, not one first site per kind. Each
   entry gives the source range, the placement (`region: call F`,
   `region: activation`, `heap`) and, for a heap site, the first escape the
   analysis found: the store, return or call that keeps the value, with its
   source range, following Goose's errors that name the root and the
   destination. Promotion copies, if adopted, are reported as copies at the
   store. The JSON report gains a version.
2. **A requirement.** `chibi check --explain-costs --require-scoped NAME`
   fails when a named function lets an allocation it makes, other than its
   results, escape, and prints the escape witness. This needs no syntax. If
   real programs use it in their checks, a later proposal can turn it into a
   declaration attribute such as `@scoped`, an existing attribute form.

## Opportunity

Measured on macOS arm64 with a Release build of `main` at b8babd3c3 and a
temporary probe in `src/support/arena.c` that was not committed. The probe
stamps each collectable allocation with its allocation order, call depth and
allocating call, and keeps a shadow stack of non-inlined calls. When a sweep
frees a value, the deepest call still running that was entered before the
value was born is a call the value died inside. A sweep only sees a death at
the next collection, so the probe forces collections at the call returns it
needs to see exactly. Inlined calls don't count as calls, which matches the
scope a call region would have. `malloc`ed array backings and string bytes
aren't counted; their shells and descriptors are.

### Stage-one self-compile

`chibi-compiler-host --bootstrap tools/compiler/bootstrap --compiler
tools/compiler --core packages/core --recover FILE` recovers the compiler from
the checked-in seed. It compiles the 39 compiler and core sources listed in
`CompilerSources.txt` (36,415 lines, 1.72 MB) twice: once with the seed, and
once with the result, to check that it reproduces itself. Figures are for the
whole run; one compile is about half.

| Measure | Whole run |
| --- | --- |
| Wall time, probe counting only | 402 s |
| Values allocated | 792.2 million, 104.4 GB |
| Value sizes | 76% between 65 and 256 bytes; 0.1% over 1 KB |
| Collections | 299, taking 123.3 s (31% of the run) |
| Live after a collection | up to 605 MB |
| Peak resident memory | 4.8 GB, including the probe's 24 extra bytes per slot header |

That is about 10,900 allocations and 1.4 MB per source line in each compile.
By the style guide's estimate rule this is orders of magnitude more than a
compiler needs, and the first fix is to find which compiler code creates those
values and stop creating them, not to make them cheaper.

**Passes.** A collection at the return of every call `CompileInput` makes
shows that 99.8% of the values allocated inside those calls die before the
call returns. That doesn't make passes useful regions: one of those calls holds
95% of the first compile's allocations and nearly all of the second's, so a pass
region would free its 50 GB once, at the end of the compile.

**Calls.** Every 1,500,000th allocation was tracked, 528 values in all, and a
collection ran at the return of the call that allocated it and of the two calls
above. This samples values, not calls, so each call counts in proportion to
what it allocates, and the result is exact for the three levels:

| A tracked value dies before | Share of the 528 |
| --- | --- |
| the call that allocated it returns | 84.7% (447) |
| that call's caller returns | 97.9% (517) |
| the call two up returns | 99.4% (525) |

The 95% confidence interval on the first share is about ±3 points. A value
returned to its caller counts at the second level here, even when the caller
builds it in place.

How large would those regions be? Each tracked value is counted once, against
the call it died in, by that call's total allocation from entry to return:

| The call it died in allocated | Share of the 528, cumulative |
| --- | --- |
| under 2 KiB | 20% |
| under 64 KiB | 29% |
| under 512 KiB | 38% |
| under 64 MiB | 49% |
| under 512 MiB | 57% |
| under 1 GiB | 92% |

About half the values die in calls that allocate under 64 MiB, the least the
heap allocates between collections. A call region frees those at return,
usually before a collection would have run. The other half die in calls that run
across several collections, where a collectable region behaves like the heap.

The prediction for the compiler, if the analysis placed every one of those
values: the heap allocates about half as many bytes, collections fall from 299
to about 150, and collection time from 123 s to about 62 s, which is about 15%
of the run. Collections cost what they trace, and they trace the live heap,
which regions don't shrink. A generational collector would get the same effect
without the analysis (see [Alternatives](#alternatives)). A bump region of
64 KiB per call would hold 29% of the values.

Two earlier passes are not used. A shadow-stack bound computed at the natural
collections understates the shares, because a value's death is only seen at
the next collection, when the short call it died in has long returned.
Sampling one call in 500,000, rather than one value in 1,500,000, missed the
few calls that allocate most: 670 calls held only 461 values.

### Per frame

A per-frame count needs a real scene. None could be run:

- Game projects are archived on `main` (e8bc71909, 2026-10-29): `chibi run`
  and `chibi-compile` refuse them.
- At its parent, 291496304, every in-repo scene with behaviors
  (`test/fixtures/terrain-lab`, `feature-coverage`, `studio-workspace` and the
  five `test/stories`) fails to prepare with "Built-in math type 'vec2' has an
  incompatible host layout".
- No in-repo program has a `Frame :: ()`. The retired game repositories are
  read-only and not tested.

Instead, a stand-in program (not committed) runs
600 frames through `chibi run`: each `Frame` integrates 1,000 bodies into a
fresh array that a `retain` binding keeps, builds a visibility list and a
formatted label that die in the frame, and calls a helper whose scratch array
dies in the call. It stands for a common game pattern and nothing more. The
probe made a program collection run after every frame; without it the
admission rule (4 KiB allocated and at least as much as was kept) would run
one every frame anyway, since each frame allocates far more than survives.

| Measure | Per frame |
| --- | --- |
| Values allocated | 6,430 (465 KB), about six per body |
| In the activation payload, freed in bulk at the end of the frame | 3,578 (56%) |
| In the heap, dead by the end-of-frame collection | 1,850 (29%) |
| In the heap, alive for one more frame through `retain` | 1,001 (16%) |
| Collection | 1 per frame, 73 µs |
| CPU | 3.5 ms |

Regions would move the 29% from the heap to the activation's region, which
saves the per-frame collection's sweep but little time, because collection is
2% of the frame. The 16% that survive one frame are the "build a fresh array,
keep it" pattern: under the fallback rule they stay heap values; promotion by
copy at the `retain` store would need a 1,000-element copy every frame and
saves nothing here. What would save time is allocating less: the program
constructs one record per body in its source and allocates about six values
per body. The `Frame` runner loop itself allocates nothing per frame (a
trivial `Frame` allocated 140 values in 600 frames).

## Alternatives

- **Allocate less.** The counts above (six values per body per frame, about
  10,900 per compiled line) are the first target. Records held inline in
  arrays, results built in place (Goose guarantees construction at the final
  destination through any depth of calls), and reads that don't copy remove
  allocations outright. Pall's rule applies: a removed allocation beats any
  allocator. Union scalarization and in-result building already do this for
  some values. The style guide's lifetime rule, which treats an array of plain
  records as the arena for its lifetime, is the same idea applied by hand to
  high-count data such as syntax nodes and IR.
- **Generational collection.** The measurements are the generational
  hypothesis: most values die young. A nursery collection would reclaim them
  without tracing the 600 MB of older values and without any compiler
  analysis, and it would cover stage-zero output and dynamic patterns that a
  static analysis must treat as escaping. It needs a write barrier on every
  store of a reference into an older value, plus remembered sets. Pall's
  LuaJIT 3.0 design is non-copying and generational, with a barrier that rarely
  fires, which fits Chibi's non-moving collector. Copy-on-write values make
  old-to-young references rarer than in a pointer-heavy language. Before
  building regions, a nursery prototype in `src/support/arena.c` should be
  measured on the same self-compile.
- **An explicit allocator API** (Odin, Zig, Fleury, Wellons). Deferred by
  `docs/package-foundations.md` until consumers exist. In Chibi it would need
  either a lifetime checker or a runtime check on reference stores to keep a
  reset from freeing a live value, since the language promises memory safety.

## Plan

Each step is measured against the numbers above before the next one starts.

1. **Report only.** Stage one computes placements and the cost report shows
   them, with no runtime change. Compare the sites it places with the probe's
   dynamic deaths; the report also names the sites that allocate most, which
   serves the "allocate less" work directly.
2. **Allocate less** at the sites the report ranks highest, then measure the
   self-compile again: fewer allocations change what regions can save, and may
   make them unnecessary.
3. **Call regions in the compiler and build hosts**, where every value is a
   heap value today and the predicted saving is largest. Sanitizer builds turn
   on the poison fill and the store check, and the differential test runs with
   regions on and off.
4. **Arrays, maps and cells in activation regions**, for `Frame` and behavior
   events.
5. **Backings and string bytes in regions, then bump regions**, each only if
   the previous step's profile shows `malloc`, reference counting or slot
   sweeping among the remaining costs.

## Prior art

These come from the research behind the engineering style guide (October
2026). Each row names the person and the primary source.

| Source | What it does | What Chibi takes |
| --- | --- | --- |
| Wouter van Oortmerssen, Lobster ([memory management](https://aardappel.github.io/lobster/memory_management.html), [HN 24652674](https://news.ycombinator.com/item?id=24652674)) | Infers a single owner for each heap allocation with no annotations, elides most reference-count operations, and inserts a runtime increment where the analysis can't decide. The analysis stays tractable because Lobster has no interior pointers, no pointers to stack objects and no shared-memory threads. | Infer, fall back to the existing runtime cost (the heap), and report the share placed. Chibi has interior pointers through managed cells and field projections, which is one reason the first version treats every store conservatively. |
| Van Oortmerssen, Goose ([README](https://github.com/aardappel/goose), [prior art](https://github.com/aardappel/goose/blob/master/docs/design/prior_art.md)) | Has no heap: the compiler assigns growable values to a compile-time number of data stacks by liveness, as registers are assigned, infers each reference's root, and rejects a reference that could outlive it, naming the root and the destination. Its own write-up names the costs: copies are real, and recursion can't keep growable data across the recursive call. | Region discipline can be fully automatic. Van Oortmerssen found explicit regions too demanding in earlier languages; the objection was the annotations, not the regions. Chibi falls back instead of rejecting, except under `--require-scoped`, whose message takes Goose's shape. |
| Mike Pall, LuaJIT ([allocation sinking](https://web.archive.org/web/2020/http://wiki.luajit.org/Allocation-Sinking-Optimization)) | Escape analysis fails for dynamic languages because nearly every object escapes on some rare path, so LuaJIT moves the allocation onto the exit paths that need it. Pall's point: removing a temporary allocation beats any faster allocator or collector. | Expect a sound per-site rule to lose sites to rare escapes, and measure how many. Promotion at the one escaping store is the sinking analogue. Removing an allocation (union scalarization) beats placing it, and stays first. |
| Jonathan Blow, Jai ([HN 26451692](https://news.ycombinator.com/item?id=26451692); [temporary storage](https://jai.community/t/temporary-storage/133), a secondary source) | Sorts data into static, long-lived, arena (per frame or task, untracked by local code) and stack. Most allocations in his game are bulk. Temporary storage is a bump allocator reset once per frame, and debug builds fill reset arenas with a pattern. | The activation payload is already Blow's third kind. Regions extend it to collections and calls, and adopt the poison fill. |
| Ginger Bill, Odin ([context](https://www.gingerbill.org/article/2025/12/15/odins-most-misunderstood-feature-context/), [arenas](https://www.gingerbill.org/article/2019/02/08/memory-allocation-strategies-002/)) | Allocating procedures take an allocator parameter that defaults to `context.allocator`; scratch work uses `context.temp_allocator`, freed in bulk. A pointer kept past `free_all` is the programmer's bug. | The same lifetime grouping without the unsafety. Chibi promises memory safety, so a reset may only free what the compiler proved dead. |
| Andrew Kelley, Zig ([choosing an allocator](https://ziglang.org/documentation/master/#Choosing-an-Allocator), [why Zig](https://ziglang.org/learn/why_zig_rust_d_cpp/)) | No hidden allocations: allocating functions take an `Allocator`, and the language reference recommends an arena freed at the end of each frame of a game loop. | Chibi already allocates implicitly. Its answer to "where are the bytes?" is the report, not a parameter. |
| Ryan Fleury ([untangling lifetimes](https://www.dgtlgrove.com/p/untangling-lifetimes-the-arena-allocator)) | A function that returns allocations takes the destination arena; its temporaries use a per-thread scratch arena chosen so it never conflicts with the destination. | Results built in the caller's storage are the destination, and a call region is the scratch. The conflict rule falls out of the analysis: a result is never placed in the callee's region. |
| Chris Wellons ([arena tips](https://nullprogram.com/blog/2023/09/27/), [dynamic arrays](https://nullprogram.com/blog/2023/10/05/)) | Passes the permanent arena by pointer and the scratch arena by value, so scratch allocations vanish on every return path. Growing a dynamic array in an arena wastes about twice the memory of a fixed one. | A call region is freed on every return path, failures included. Growing arrays use size-class slots, not a bump allocator. |

## Language-feature gate

The gate from `AGENTS.md`, "Language changes":

- **A program needs it.** The stage-one compiler: 31% of a self-compile is
  collection. The stand-in `Frame` program doesn't need it (2%), and no real
  game scene could be measured.
- **Inference or an existing form can't express it.** This proposal is
  inference; it adds no syntax and no allocator values. An explicit API would
  need a lifetime checker, which the managed-pointer contract avoids (users
  never choose ownership or write `move`), or a runtime check on every
  reference store, to keep a reset from freeing live values.
- **The case is common.** 85% of the compiler's values die before the call
  that allocated them returns, and 98% before its caller returns. In the
  stand-in frame, 29% of all values are heap values that die in the frame.
- **No second way to do something.** Values keep one spelling and one
  semantics; placement is invisible except in the report.
- **No hidden allocation, branch or control flow.** Regions remove work. The
  costs they can add are named: a promotion copy at a visible store, if
  adopted, which the report shows; region retention inside a long scope, which
  collections bound; and the reset itself, at a scope exit that already exists.
- **Stage-one tokenizer.** No syntax. The analysis is stage-one code, and the
  prepared form gains a placement flag; stage zero keeps emitting heap
  placements, so its output and `Build.chibi` are unaffected. The flag changes
  the prepared ABI and needs a regenerated bootstrap seed in the usual way.
- **Tried on real code with uses counted.** The report counts region and heap
  sites, and the values they allocate, per program. Step 1 of the plan runs it
  on the compiler before any runtime change.

**Removal plan.** Placement is an optimization bit per site.

- If step 1 places fewer than half of the self-compile's allocations in a call
  region (the probe found 98% dying by the caller's return), stop: the
  analysis is too weak to pay for its upkeep, and the report stays only if the
  "allocate less" work uses it.
- If step 3 doesn't cut the self-compile's collection time by at least 25%
  (predicted: 50%), delete the analysis and the runtime region path. Every site
  becomes a heap site again, and the placement flag goes at the next prepared
  ABI change.
- If a nursery prototype gets the same collection savings, prefer it: it needs
  no compiler analysis.

## Open questions

- **Loop-iteration regions.** A value created and dropped in each iteration
  dies within the iteration. Making loop bodies region scopes captures it
  without a collection; the collectable region already bounds memory without
  them. Measure before adding a scope kind.
- **Promotion or fallback** for values stored into state, decided per site
  from the report.
- **Backings in the region.** Region-placed backings and string bytes remove
  `malloc`, `free` and atomic reference counts, and need the stronger
  "no copy leaves the scope" fact.
- **Bump regions** without headers or sweeping for scopes with bounded
  allocation.
- **Native retention contracts.** Which natives besides the array kernels can
  be marked `borrows`.
- **Suspended activations** keep their region across frames; check that an
  `await`-heavy behavior doesn't pin more memory than the heap would.
- **Threads.** Parallel tests each own a heap; regions are per activation or
  frame, so they don't cross threads, but the debug store check must know the
  owning thread.
