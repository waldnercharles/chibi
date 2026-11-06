# Engineering review: cd428b92e "Add a call-site census for cost explanations and a typed element count"

Checkout: `/Users/cwaldner/Personal/chibi/.worktrees/review-eval/chibi`, HEAD only (user-named scope).
Files: `tools/compiler/package_compiler/CallCensus.chibi` (new, 96 lines), `src/types/array.c` (+17), `src/types/array.h` (+2).
115 insertions, under 400 lines, so I ran every pass myself. Read only: I built nothing and changed nothing in the checkout.

## Brief

**Transform.**
- Chibi: a syntax forest (`snapshot.SnapshotNode[]`) and one document `text` go in. A `CallCensus` comes out: `sites: CallSite[]` (node, callee text, offset, line, column, `is_dead`) and `counts: [string: int]` keyed `"call:" + callee`. `MarkDead` sets `is_dead` for a list of names, and `LiveSites` copies out the sites that aren't dead.
- C: a `pin_array` and a type name go in. The count of elements whose `pin_value.type_name` equals that name comes out.

**Counts.** I estimated call sites by regex: an identifier or dotted name followed by `(`, minus `if/while/for/switch/return/case`, with strings and comments masked.
- `Body.chibi`: 191,198 bytes, about 1,777 call sites and about 42,000 tokens. The forest node count is unknown, on the order of 10⁵.
- `Steps.chibi`: 330,011 bytes, about 3,438 sites.
- All of `tools/compiler`: 77 files, 2,267,948 bytes, about 20,467 sites.
- `pin_array_count_type`: unknown. It has no caller.

**Consumers.** None.
- `git grep` finds no use of `CollectCalls`, `CountCallsInForest`, `MarkDead`, `LiveSites`, `CallSite` or `CallCensus` outside the new file.
- LSP `find_references` on `pin_array_count_type` returns only its definition (array.c:122).
- The doc comment says "for the cost-explanations report". That report lives in `tools/compiler/cost_explanations/Explain.chibi`, which imports only `core:strings` and keys functions by `id: u64` (Explain.chibi:5-14).

**Layer.**
- The census sits in a compiler package (`package_compiler`, a folder of sibling files: docs/code-language.md:661). It would run once per compiled document under `--explain-costs`, so I hold it to hot-pass posture. It is cold today because nothing calls it.
- The C function is in the core value runtime (`src/types`), a hot layer by location.

**Minimum work (census, one document).**
- One pass over the document's N nodes with a kind test each: about 10⁵.
- Per call site: one `Field` child walk (a few children) and one `int` append, about 1,777 × 5.
- Lines and columns cost nothing, because `SnapshotNode` already stores `row` and `column` (Snapshot.chibi:37-38, filled at :204). Sixteen sites in `package_compiler` already read `row + 1` and `column + 1`.
- Total: about 10⁵ simple operations and about 0 bytes copied. That is well under a frame natively and a few ms in the VM.

**Minimum work (C).** For a packed array (`stride > 0`), every element's `type_name` is the owner's (array.c:361-364), so the answer is one `strcmp`. For an `items` array, the answer is one `strcmp` per element on the borrowed pointer.

**Actual work (one call site, traced into the helpers).**
1. `forest[index].kind == "call_expression"`: a string compare per node.
2. `CalleeName` → `Field` walks the children, comparing field names as strings. Then `strings.Slice(text, start, end)` → `text_slice` takes a *count*, not an end (Strings.chibi:23), so it returns about `min(end, L - start)` bytes. That is 53 KB on average in `Body.chibi`, about 94.6 MB over its 1,777 sites. The host-call path copies those bytes (`text_slice_call` → `pin_value_text`, text_format.c:463-479).
3. `strings.Concat("call:", callee)`: one more allocation and copy of that string. (This function doesn't exist; see Correctness notes.)
4. `counts[key] += 1`: hashes the whole key.
5. `LineOf(text, start)`: `start` `strings.ByteAt` host calls. Each call validates its arguments, decodes the string and builds a `pin_value` (text_format.c:409-460).
6. `ColumnOf(text, start)`: another `start` `ByteAt` host calls.
7. `sites.append` of a 6-field record holding a string.

Multiplied out, `Body.chibi` needs Σ 2 × start = **390,526,400 `ByteAt` calls**, plus about 95 MB sliced, about 95 MB concatenated and about 95 MB hashed. The compiler sources need **2,488,538,616 `ByteAt` calls** and about 628 MB sliced. At an assumed 10 ns per host call (unmeasured), that is about 4 s for `Body.chibi` and about 25 s for the compiler sources.

**Actual work (C, one element).**
- `pin_array_get`: `pin_array_owner` twice and `pin_array_offset`. In debug builds `pin_array_view_valid` adds a third owner read and `pin_value_layout_same` for views.
- `xstrdup`: `strlen` + `malloc` + `memcpy`.
- `strcmp`.
- `free`.

**Gap.**
- Census: about 5 × 10⁸ operations against about 10⁵, so about 5,000×, before any per-call overhead. The structure has to change, not a loop.
- C: one malloc/free pair per element against zero. That is ≥10× per element on the `items` path and O(count) against O(1) on the packed path.

## Target shape

The best result is that both additions disappear. Nothing consumes them, and the Chibi file breaks the build (see Correctness notes).

If the cost report later needs call counts, count them where the report's facts already come from, the finalized bodies. Count resolved callees rather than source text:

```
// built while a body is finalized, in source order
call_callee: int[]   // resolved callable index (FindCallable, Body.chibi:1451)
call_node:   int[]   // the call_expression node, same order
// per-callee count: one dense int[] histogram sized by length(callables)
// line/column at print time: forest[node].row + 1, forest[node].column + 1 (as Diagnose, Tree.chibi:71-74)
```

A census that has to stay syntactic shrinks to node indices over one document's range:

```
CallNodes :: (forest: snapshot.SnapshotNode[], node_start: int, node_end: int) -> int[] {
    calls: int[] = [];
    for (node in node_start..<node_end) {
        if (Is(forest, node, "call_expression")) { calls.append(node); }
    }
    return calls;
}
// callee text on demand: Text(forest, text, Field(forest, node, "function"))
```

Either shape deletes the following:
- `CallSite`, `CallCensus`, `counts`, `is_dead`
- `LineOf`, `ColumnOf`, `CalleeName`, `CountCallsInForest`, `MarkDead`, `LiveSites`
- the C function and its header line

## Findings

```
[PERF] tools/compiler/package_compiler/CallCensus.chibi:30-31,76-96 — LineOf and ColumnOf rescan the document from byte 0 for every call site
Cost: run — two scans of `start` bytes per site, each byte a VM-to-native ByteAt host call; 390,526,400 calls for Body.chibi and 2,488,538,616 across tools/compiler, against zero
Evidence: SnapshotNode stores row and column (Snapshot.chibi:37-38, copied from the parser at :204), and 16 sites in package_compiler already read `row + 1` / `column + 1` (e.g. Body.chibi:420, Tree.chibi:73). Call counts and Σ start come from the regex census above. ByteAt is a host call (Strings.chibi:13 → text_byte_at_call, text_format.c:447-460)
Fix: read forest[node].row + 1 and forest[node].column + 1 at the point of use. Delete LineOf, ColumnOf, the line/column/offset fields of CallSite and the two raw `10` newline tests with them. Confirm with a timed CollectCalls over Body.chibi before and after (expect seconds → milliseconds)
```

```
[PERF] tools/compiler/package_compiler/CallCensus.chibi:28-29 — builds a prefixed string per call site only to use it as a map key
Cost: run — one allocation and copy per site (1,777 in Body.chibi) plus a hash over the whole key. With CalleeName's slice as written, that is about 95 MB copied and about 95 MB hashed for Body.chibi. Even with the slice fixed, it is still one allocation per site to add five constant bytes
Evidence: every key starts with the same "call:", so the prefix distinguishes nothing. `counts` is derivable from `sites`, which already holds every callee (one fact stored twice)
Fix: drop `counts`. A consumer that needs per-callee counts gets them from a histogram over resolved callee indices (Target shape). If text keys must stay, key by `callee` alone
```

```
[SIMPLIFY] tools/compiler/package_compiler/CallCensus.chibi:1-96, src/types/array.c:122-138, src/types/array.h:52-53 — neither addition has a caller or a consumer
Cost: change, read and build — 113 lines and a new public runtime entry point to maintain, with no behavior anyone reaches. The Chibi file also breaks the stage-one compiler build (Correctness notes)
Evidence: git grep finds none of CollectCalls, CountCallsInForest, MarkDead, LiveSites, CallSite or CallCensus outside the file. LSP find_references on pin_array_count_type returns only its definition. Explain.chibi imports only core:strings. The repo rules ask for YAGNI and no new C where Chibi can do the job, and flag new C with no Chibi need
Fix: delete CallCensus.chibi, pin_array_count_type and its header line. Write the census when the cost report has a concrete need, in the shape that need dictates. CountCallsInForest (:40-42) is a forwarding wrapper with a duplicate doc comment and goes regardless
```

```
[ARCH] tools/compiler/package_compiler/CallCensus.chibi:27-29,67-73 — call identity is source text in the syntax layer, while the report it claims to serve keys functions by resolved id over finalized bodies
Cost: change and debug — `strings.Slice` and an aliased `s.Slice` count as different callees, same-named functions in different packages merge, and member calls key on whole expressions such as `census.sites.append`. Every consumer has to re-resolve names the compiler already resolved
Evidence: the compiler resolves calls to callable indices (FindCallable, Body.chibi:1451). Explain.chibi:5-14 identifies functions by `id: u64`. docs/cost-explanations.md says the report covers finalized function bodies, and that the adapter "does not implement another collection" analysis. A syntactic census also counts calls the finalized body may not contain
Fix: if counts are needed, record the resolved callee index per call where bodies are finalized (Target shape). Delete CalleeName and the string keys
```

```
[SIMPLIFY] tools/compiler/package_compiler/CallCensus.chibi:7-14,45-64 — CallSite stores five facts derivable from `node`, and `is_dead` encodes list membership that LiveSites filters and copies
Cost: run and change. Per site, the record carries a string and four ints that duplicate the forest: offset = forest[node].start, line and column = row and column + 1, callee = Text(Field(node, "function")). Each duplicate can disagree with the forest. MarkDead does sites × removed string compares, and LiveSites then allocates and copies every live record
Evidence: Snapshot.chibi:33-48 (start, row, column), Tree.chibi:38-50 (Field) and :65-70 (Text). No caller of MarkDead or LiveSites exists, so the filter's batch size is unknown
Fix: the census is `int[]` of node indices. A consumer that drops callees filters once while it builds its own list, with a `[string: bool]` set as in asset_source/Write.chibi:153 if it needs one. Delete is_dead, MarkDead and LiveSites
```

```
[PERF] src/types/array.c:128-135 — duplicates every element's type name onto the heap only to compare it, and loops even when the answer is one compare
Cost: run — per element: strlen + malloc + memcpy + strcmp + free, against one strcmp on the borrowed pointer. For packed arrays (stride > 0), O(count) against O(1)
Evidence: pin_array_get synthesizes each packed element's `type_name` from the owner (array.c:361-364), so a packed array's elements all share `array->type_name`. strcmp only reads, and both strings are const. Element count unknown (no caller)
Fix: if the function survives deletion, compare `value.type_name` in place. For packed owners, return `strcmp(owner->type_name, type_name) ? 0 : array->count` before the loop. Delete the xstrdup/free pair
```

```
[SIMPLIFY] tools/compiler/package_compiler/CallCensus.chibi:67-73 — CalleeName re-implements Tree.chibi's Text and gets the slice wrong
Cost: change and debug — a second copy of a one-line reader in the same package, and the copy that diverged is the buggy one (Correctness notes)
Evidence: Tree.chibi:65-70 `Text(forest, text, node)` handles node < 0 and passes `end - start`. CalleeName passes `end`
Fix: `Text(forest, text, Field(forest, node, "function"))`, and delete CalleeName
```

```
[STYLE] src/types/array.c:124-126, src/types/array.h:52-53 — silent NULL guard and a contract comment that leaves out the edge cases
Cost: debug and read. A NULL array, which only a caller bug produces, returns 0 instead of stopping. The comment restates the signature and never says that elements with a NULL name match "", that `type_name` must not be NULL, or that "type" means the nominal name rather than `pin_type`. A reader of `pin_array_count_type(a, "int")` can easily take it as a pin_type filter
Evidence: the guard is at array.c:124. strcmp(name, NULL) is undefined (array.c:131). The NULL → "" substitution is at :130
Fix: if it survives, assert(array && type_name), name it `pin_array_count_type_name`, and state the NULL-element rule in the comment
```

```
[TEST] tools/compiler/package_compiler/CallCensus.chibi, src/types/array.c:122 — no @test and no pico_unit case for either addition
Cost: debug — five defects listed under Correctness notes shipped. One authored test over a two-document snapshot would catch the slice length and the whole-forest walk, and a single pico_unit case catches the debug-build off-by-one
Evidence: no test file changed in the commit. The repo rules require a case for every expressible behavior change, and cost_explanations already keeps authored tests (ExplainTests.chibi, 6 @test)
Fix: moot if both are deleted. Otherwise add the authored test next to the census and a pico_unit case through test/language that checks the packed and items paths, in debug and NDEBUG
```

## Correctness notes

1. **Build break (stage-one compiler).** CallCensus.chibi:28 calls `strings.Concat`, but `core:strings` has no `Concat`; its only concatenation is `Concatenate(a: string[])` (Strings.chibi:551). A package is a folder of `.chibi` files whose functions are shared (docs/code-language.md:661-662), so this file compiles with every `package_compiler` build. I didn't run `make build`, so the failure itself is unconfirmed.
2. CallCensus.chibi:72 passes `forest[function].end` as `Slice`'s *count* (Strings.chibi:23 takes `start, count`). Each callee therefore runs about `min(end, L - start)` bytes past its start (53 KB on average in Body.chibi), so every key is unique and every count is 1.
3. CallCensus.chibi:25 walks the whole forest against one document's `text`. The forest is global across documents: nodes are rebased by `node_start` (Snapshot.chibi:328-334), and Tree.chibi:7-8 notes that offsets are per document. Nodes from other documents get sliced and line-counted against the wrong text. The sibling collectors take `[node_start, node_end)` (Queries.chibi:1221) or loop per document (Math.chibi:109-110).
4. array.c:136 `assert(matches++ <= array->count)` has a side effect. Debug builds return `matches + 1`; NDEBUG builds return `matches`. The condition holds by construction anyway.
5. array.c:130-131: a NULL `type_name` argument reaches `strcmp(name, NULL)`, which is undefined. Elements without a name match `""`.

## What I would delete

- `tools/compiler/package_compiler/CallCensus.chibi`, the whole file:
  - `CallSite`, `CallCensus`
  - `CollectCalls`, `CountCallsInForest` (forwarder)
  - `MarkDead`, `LiveSites`
  - `CalleeName` (duplicate of `Text`)
  - `LineOf`, `ColumnOf` (duplicates of stored `row`/`column`)
- `pin_array_count_type` (array.c:122-138) and its declaration (array.h:52-53).
- If any census survives, delete the `counts` map, the `"call:"` prefix, the `offset`/`line`/`column`/`callee`/`is_dead` fields and the raw `10` byte tests.

## What I would measure

- Forest node count for Body.chibi and Steps.chibi. I estimated about 10⁵ from 42,000 tokens. A snapshot dump of `length(nodes)` per document settles it.
- The cost per `strings.ByteAt` host call in the VM, and in the native stage-one build. Time a 191 KB scan in a benchmark selector to turn 3.9 × 10⁸ calls into seconds.
- Whether `strings.Slice` copies on the path the compiler uses. The host call `text_slice_call` copies through `pin_value_text`; the intrinsic shares through `pin_value_text_slice` (intrinsics.c:437). `chibi check --explain-costs` on a probe would show it.
- Whether `census.sites.append(...)` on a field of a local record copies the array, using `--explain-costs` on CollectCalls.
- My call-site counts come from a regex. A tree-sitter count of `call_expression` nodes would replace them; I expect the same order.
- For `pin_array_count_type`, should a caller ever appear: its array sizes and the packed-to-items ratio.

## What I would leave alone

- `forest[index].kind == "call_expression"` (CallCensus.chibi:26) compares kinds as strings. That is the package-wide representation (`SnapshotNode.kind: string`, `Is` at Tree.chibi:51, hundreds of callers). Interning kinds belongs in its own change, and one compare per node is about 10⁵ operations, small next to 3.9 × 10⁸.
- `strcmp` on `type_name` instead of pointer identity in the C function. It matches `element_schema` (array.c:381). Interning nominal names is a separate change with its own consumers.
- The existing `!array` early returns in the neighboring `pin_array_mutation_end` and `pin_array_release` (array.c:112, 141). They predate this commit; changing them means auditing their callers.
- The `census^.sites` explicit dereferences (CallCensus.chibi:46-49), where the house form is direct field access (`c.failed`, source_parser/Declarations.chibi:14). The code goes away with MarkDead.
