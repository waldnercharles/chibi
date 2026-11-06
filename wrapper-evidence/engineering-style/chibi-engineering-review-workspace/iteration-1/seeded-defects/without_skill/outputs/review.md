# Review: cd428b92e "Add a call-site census for cost explanations and a typed element count"

Checkout: `/Users/cwaldner/Personal/chibi/.worktrees/review-eval/chibi`, HEAD `cd428b92e6e8d852b2fb168a7c897e10dddf67c3` (parent `38e8036b1`).

Files changed:

- `tools/compiler/package_compiler/CallCensus.chibi` (new, 96 lines)
- `src/types/array.c` (+17), `src/types/array.h` (+2)

**Verdict: request changes.** The Chibi file breaks compiler loading as soon as it lands. It also would not compile, and the callee text it computes is wrong. The C function's assertion has a side effect, so it returns a different count in debug and release builds. Nothing calls either addition and nothing tests them. The best fix is to delete the C function and rebuild the census around a real consumer.

How I checked: I read the code only and changed nothing in the checkout. That checkout has no `build/` directory, and building would write into it, so I didn't build. clang-format 22 `--dry-run` finds nothing to change in either C file. Every claim below cites the source I checked it against.

---

## Blocking

### B1. `CallCensus.chibi` isn't in `CompilerSources.txt`, so compiler load and recovery reject the whole package

`tools/compiler/asset_compiler/CompilerSources.txt` lists `package_compiler/*.chibi` files one by one (lines 1-18), and `CallCensus.chibi` isn't on the list. `src/toolchain/compiler_bootstrap.c:384-414` scans the folder of every listed source and fails with "`%s is in the package of a compiler source but %s does not list it`". `docs/self-hosting-build.md:158` says `chibi_build_compiler_load` makes the same "source completeness" check, and `test/tools/compiler_bootstrap.c:268` asserts that exact message. So every path that loads the stage-one compiler fails, including `make bootstrap-recover`, `make test-recovery` and the build-host compile. That breaks `main`.

Adding the file to the list isn't enough. A listed file is compiled with its package, and then B2 and B3 apply.

### B2. `strings.Concat` doesn't exist

`CallCensus.chibi:28` calls `strings.Concat("call:", callee)`. `packages/core/strings/Strings.chibi` exports `Concatenate(a: string[])` (line 551) and has no `Concat`. Nothing else in the tree calls `strings.Concat`. The only `Concat` in the tree is a private helper for arrays in `apps/chibi/Arguments.chibi:44`. Once the file is listed, the package fails to resolve. The compiler sources build strings with `format_text("call:{}", callee)` (for example `Behavior.chibi:221`). The prefix shouldn't be there at all, though (see M6).

### B3. `CalleeName` passes an end offset where `strings.Slice` expects a count

`CallCensus.chibi:72`:

```chibi
return strings.Slice(text, forest[function].start, forest[function].end);
```

`strings.Slice` is `(value, start, count)` (`Strings.chibi:22-25`: "count clamps to its remaining bytes"). For a callee at bytes [1000, 1003), this returns up to 1003 bytes starting at 1000, which is most of the rest of the file instead of `foo`. Every `counts` key and every `CallSite.callee` would hold this garbage. `MarkDead` would therefore never match. The mistake probably comes from `source_parser/Tree.chibi:247`, whose private `Slice` does take `(start, end)`.

The package already has the correct helper. `Tree.chibi:65` `Text(forest, text, node)` slices with `end - start` and returns `""` for `-1`. So the whole function is `Text(forest, text, Field(forest, node, "function"))` (see M4).

### B4. `CollectCalls` scans the global forest against one document's text

The snapshot forest is global. `Tree.chibi:7` says "Links are global node indices; … offsets count bytes of the node's document text". `Document` carries `node_start`/`node_count` (`source_snapshot/Snapshot.chibi:63-64`). Every per-document pass in this package walks only that document's range: `Queries.chibi:1221` `CollectQueries(... node_start, node_end ...)`, `Behavior.chibi:826-827` and `Math.chibi:109-110`.

`CollectCalls` (`CallCensus.chibi:25`) loops over `0..<length(forest)` but gets one `text`. For every node outside that document, `start`/`end` index into the wrong text, so callee names, lines and columns are wrong. A caller can't fix this by passing a sub-forest either, because `first_child` and `next_sibling` are global indices and `Field` would follow them out of the slice. The function should take the document, or `(node_start, node_end)`, the way `CollectQueries` does. The comments on lines 6 and 16 ("in a document") describe a contract the code doesn't keep.

### B5. The C `assert` has a side effect, so debug and release builds return different counts

`src/types/array.c:136`:

```c
assert(matches++ <= array->count);
return matches;
```

With assertions on, `matches` is incremented, so every debug or sanitizer build returns the true count plus one. With `NDEBUG` the increment disappears and the count is correct. Behavior differs by build type, and the debug result is always wrong. The condition is also a tautology, since the loop can't produce more than `count` matches. Delete the line; don't fix it.

---

## Major

### M1. Dead code in both languages: nothing calls any of it

`git grep` finds no reference to `pin_array_count_type`, `CallCensus`, `CollectCalls`, `CountCallsInForest`, `MarkDead` or `LiveSites` outside their own definitions. Nothing in `tools/compiler/cost_explanations/` imports the new code, no native exposes the C function to Chibi, and no test uses either one. CLAUDE.md asks for YAGNI and says "remove dead code/abstractions". As written, this commit adds 115 lines that can't be exercised. Land each half with the change that consumes it, or drop it.

### M2. The census is in the wrong package and contradicts the cost-explanations design

- The comment says the code is "for the cost-explanations report", but that report lives in `tools/compiler/cost_explanations/` (`Explain.chibi`, `ExplainTests.chibi`). This file is in `package_compiler`, the stage-one compiler that the bootstrap seed has to compile. Putting it there couples report tooling to the bootstrap contract, which is how B1 happened.
- `docs/cost-explanations.md` says the report covers "finalized function bodies and their linked callees". It also says the native adapter "transports the existing finalized analysis facts; it does not implement another collection, ownership or borrowing analysis". A census of raw syntax, keyed by callee text, is exactly another collection. It also isn't semantic:
  - `player.Move` and `enemy.Move` become different keys.
  - An import alias and a shadowing local with the same spelling become the same key.
  - Whitespace or comments inside a member expression produce yet another key.
- If cost explanations need call counts, they should come from the finalized facts the report already consumes, with resolved callee identity. Say in the commit or PR which report entry needs them.

### M3. New C with no consumer, against the repository's top priority

CLAUDE.md: "Write new code in Chibi; write C only for what Chibi can't run yet". Review guideline: "Flag new C or C++ that Chibi could have expressed". Counting array elements by type is trivial in Chibi, and nothing native needs it. Delete `pin_array_count_type` from `array.c` and `array.h`. If you keep it anyway, the problems below also need fixing.

### M4. `pin_array_count_type` has these problems besides B5

- **`type_name` is the wrong field to compare.** Scalar values set `type_name` inconsistently:
  - `intrinsics.c:580`, `text_format.c` and `state.c:165` set `.type_name = "int"`.
  - `intrinsics.c:2453`, `2467`, `2545` and `2563` build `(pin_value){ .type = PIN_I32, .i = ... }` with `type_name` unset, which is NULL.

  So `pin_array_count_type(a, "int")` depends on which code path filled the array. The reliable discriminator is `pin_type` (`element_type` / `value.type`), plus `type_name` only for nominal `PIN_STRUCT`/`PIN_ENUM`/`PIN_UNION`.
- **The NULL-to-`""` mapping adds behavior nobody asked for** (`array.c:130`). A query for `""` matches every untyped element. The header comment doesn't mention this.
- **It allocates on the heap once per element for nothing** (`array.c:130-134`). `xstrdup` followed by `free` around a read-only `strcmp` is wasted allocator traffic. Compare `value.type_name` directly.
- **The NULL check is defensive, against fail-fast** (`array.c:124-126`). CLAUDE.md: "avoid defensive NULL checks that mask defects". The neighboring functions treat a NULL array as caller error. `type_name` is the opposite case: it is never checked, and `strcmp(name, NULL)` is undefined behavior. Use `assert(array && type_name)` and remove the early return.
- **Packed arrays don't need a loop.** When `stride > 0`, `pin_array_get` (`array.c:361-364`) gives every element `owner->type_name`. The answer is `count` or `0`, yet the loop builds a `pin_value` for every element.
- **Declaration and definition are in the wrong group.** The declaration sits among the ownership and lifetime functions (`array.h:52-53`, between `pin_array_mutation_end` and `pin_array_box_owned`). The definition sits between `pin_array_mutation_end` and `pin_array_release`. A read-only accessor belongs near `pin_array_get` and `pin_array_find` (`array.h:92-101`).

### M5. `LineOf` and `ColumnOf` recompute data the node already has, at O(sites × bytes)

`SnapshotNode` already carries `row` and `column` (`Snapshot.chibi:37-38`). `Tree.chibi:71-74` `Diagnose` turns them into 1-based `line: item.row + 1, column: item.column + 1`. `CallCensus.chibi:76-96` instead scans from byte 0 to `offset` twice per call site, once for the line and once for the column, with one native `ByteAt` call per byte. A file with k calls costs about 2·k·n native calls. Both helpers should go; use `forest[node].row + 1` and `forest[node].column + 1`. If a byte-to-line map is ever needed, build it once per document. Minor point: `10` should be a named newline constant. The compiler often writes bytes as numbers, but a name costs nothing here.

### M6. `counts` disagrees with `sites`

- **The keys have a prefix the sites don't.** Keys are `"call:" + callee` (`CallCensus.chibi:28-29`) but `CallSite.callee` is bare. A reader has to know to add the prefix to cross-reference them. The prefix carries no information because the map holds only calls.
- **`MarkDead` leaves the counts stale.** It flips `is_dead` on sites but never decrements `counts`, so after it runs, `counts` and `LiveSites` contradict each other.
- **The map is derivable.** `counts` can be computed from `sites` whenever it's needed. Storing both invites exactly this drift. Keep one source of truth.

### M7. No tests

CLAUDE.md, "Before every commit": "Add or update an authored `@test` for every expressible behavior change, or a pico_unit case for a native boundary". There are none. Tests would have caught most of the defects above:

- An `@test` beside the code (the pattern is `cost_explanations/ExplainTests.chibi`, run by `test/tools/chibi_cli.c:951`) catches B2 and B3, plus B4 with a two-document snapshot.
- A pico_unit case in `test/tools` catches B5.

---

## Minor

- **`CountCallsInForest` is a pure alias** (`CallCensus.chibi:39-42`). It forwards to `CollectCalls` under a misleading name (it collects, not counts) with a near-duplicate comment. Delete it.
- **`MarkDead`'s loop** (`CallCensus.chibi:45-53`):
  - `name` is an index into `removed`, not a name.
  - The inner loop doesn't stop after a match.
  - Cost is O(sites × removed). Use `removed.contains(site.callee)`, or build a `[string: bool]` set once.
  - `census^.sites` doesn't follow the codebase convention. The docs say you can "access its fields directly" through a pointer (`docs/code-language.md:243`), and `source_parser/Declarations.chibi:14` writes `c.failed` for `c: ^Cursor`.
- **The index loops can iterate values.** `LiveSites` and `CollectCalls` loop over indices only to read `x[index]`. Chibi has `for site in census.sites` and the keyed `for node, index in forest` (`docs/code-language.md:440-455`, used in `Compile.chibi:548`).
- **Use the package's helper for kind checks.** Write `Is(forest, index, "call_expression")` rather than `forest[index].kind == ...` (`Tree.chibi:51`), as the rest of the package does.
- **`CallSite` stores derived data.** `offset`, `line` and `column` all follow from `node` (M5). `is_dead` is mutable state with no use case yet. A site could be `{node, callee}` until a consumer needs more.
- **The struct initializer is noisier than it needs to be.** In `census: CallCensus = {sites: [], counts: [:]}` and `is_dead: false`, the zero values are the defaults.
- **The comments are inaccurate.** Line 6 ("in a document") and line 16 ("Every call site in a document") contradict B4. Line 22 names a consumer that doesn't use the code (M1, M2).
- **Check that the bootstrap seed accepts the range loops.** This is the first `..<` range loop in `tools/compiler/*` sources; the existing loops are `while` loops or keyed array loops. `docs/language-friction.md:402-409` says stage one compiles range loops since #559 and recommends them. Still, `bootstrap.kdl` declares `minimum 6`, so confirm the pinned seed accepts them. Run `make test-recovery` and `make test-stage-one` before landing, as the stage-zero semantics memory note says. This only matters once B1 is fixed.

## Commit hygiene

- **Two unrelated changes share one commit.** A Chibi tooling feature and a C runtime accessor have no shared caller and no shared reason. The "and" in the subject shows it. Split them, or, per M1 and M3, drop the C half.
- **The message has no body.** CLAUDE.md: "Commit messages say what changed and why". Nothing says which cost-explanations entry needs a call census, or who needs a "typed element count". "Typed element count" is also vague: it doesn't say whether "type" means `pin_type` or the nominal `type_name`, which is the ambiguity behind M4.
- **The commit skipped the pre-commit checklist.** CLAUDE.md asks for a whole-diff review before committing (`/code-review`) and a focused build. Running `make test-recovery` once would have caught B1, and listing the file would then have exposed B2. The first debug-build test to call the C function would have caught B5.

## Suggested path

1. Delete `pin_array_count_type` from `src/types/array.c` and `array.h`.
2. Move the census to `tools/compiler/cost_explanations/`, or wherever its first consumer lives, and land it in the same change as that consumer. Base it on finalized callee identity, not callee text. If a syntax census really is the goal, write it as a per-document pass with this signature:

   ```chibi
   CollectCalls :: (forest: snapshot.SnapshotNode[], text: string, node_start: int, node_end: int) -> CallSite[]
   ```

   Have it use `Text(forest, text, Field(forest, node, "function"))` for the callee and `row + 1` / `column + 1` for the location. Drop `counts`, `CountCallsInForest`, `MarkDead`, `LiveSites`, `LineOf` and `ColumnOf` until something needs them.
3. If the census stays in a compiler package, add it to `asset_compiler/CompilerSources.txt` and run `make test-recovery`.
4. Add an `@test` with two documents and a member call, checking callee text, line and column.
