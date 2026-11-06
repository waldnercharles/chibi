# Engineering review: cd428b92e "Add a call-site census for cost explanations and a typed element count"

Checkout: `/Users/cwaldner/Personal/chibi/.worktrees/review-eval/chibi`, HEAD only (3 files, +115 lines):
`tools/compiler/package_compiler/CallCensus.chibi` (new, 96 lines), `src/types/array.c` (+17), `src/types/array.h` (+2).

Nothing was built or modified. There is no build directory in the checkout, so the build-break finding is traced through source, not run. The one experiment (the `assert` side effect) ran in a scratch directory under `/tmp`, which I deleted afterwards.

## Brief

- **Transform.**
  - `CollectCalls`: snapshot forest (`snapshot.SnapshotNode[]`, global node indices, document-local byte offsets) plus one document's `text` → `CallCensus { sites: CallSite[], counts: [string: int] }`. Each `CallSite` holds node index, callee text, byte offset, 1-based line and column, and `is_dead`. `counts` is keyed `"call:" + callee`. `MarkDead` and `LiveSites` then filter by callee name.
  - `pin_array_count_type`: `pin_array *` plus a type name → the number of elements whose `pin_value.type_name` equals it.
- **Counts.** The compiler closure (`asset_compiler/CompilerSources.txt`: 31 files, 1,502,707 bytes) has about 14,294 call sites, counted as `name(` matches that aren't keywords. The largest document, `package_compiler/Body.chibi`, has 191,198 bytes and about 1,779 sites. Snapshot limits set the worst case: 524,288 bytes per file and 8,388,608 in total (`Snapshot.chibi:134-141`), at most 256 files (`Snapshot.chibi:270`). `pin_array_count_type` has no caller, so its counts are **unknown**.
- **Consumers.** None.
  - The 9 new Chibi declarations (`CallSite`, `CallCensus`, `CollectCalls`, `CountCallsInForest`, `MarkDead`, `LiveSites`, `CalleeName`, `LineOf`, `ColumnOf`) have 0 references outside the file (git grep; cclsp has no Chibi index).
  - For `pin_array_count_type`, LSP `find_references` and git grep find only the declaration and the definition.
  - The consumer the comment names, `cost_explanations/Explain.chibi`, imports only `core:strings` (line 3). It receives `Function`/`Cost` records that already carry line and column, through `asset_compiler/Compile.chibi:11`.
- **Layer.**
  - The census is cold: report tooling. But it sits in `package_compiler`, which every compiler load compiles.
  - `pin_array_count_type` is in the runtime types layer, in a public SDK header. Whether it is hot or cold depends on a caller that doesn't exist: **unknown**.
- **Failure classes.**
  - Bug: the `assert` side effect, silent guards and wrong slicing.
  - Invalid external input: the forest comes from user source text. Error trees can lack the `function` field.
  - Environment: none.
- **Twins.** No C call census and no Chibi count-by-type-name exist. The nearest relative is `src/authoring/source_navigation.c:303`, which walks `call_expression` nodes for navigation, a different consumer.

**The most important fix is deletion.** Nothing calls either addition, and the Chibi file breaks compiler loading just by being in the package folder. Every finding below the first one is latent until a consumer exists. They are listed so that the version that eventually lands is right.

## Findings

```
[BUG] tools/compiler/package_compiler/CallCensus.chibi:1 — new file in a compiler package that CompilerSources.txt doesn't list; every compiler load rejects the tree
Cost: run, debug — the SDK build's asset-compiler packaging step, `make bootstrap-recover`, `make test-recovery`, `make bootstrap-artifact` and the compiler-recovery workflow all stop before compiling anything
Evidence: src/toolchain/compiler_bootstrap.c:373-425 `packages_complete` scans the folder of each listed source and fails with "%s is in the package of a compiler source but asset_compiler/CompilerSources.txt does not list it". It is called from `disk_arguments` (:706) inside `validated` (:836). Both `compiler_bootstrap_load` (:868, used by `chibi_build_compiler_load`, src/host/build_host.c:563-571, reached from build_main.c:311) and `compiler_bootstrap_check` (:854) go through `validated`. sdk/asset_compiler_host.cmake:28-29 runs the build host with `--compiler tools/compiler` during the SDK build. package_compiler/ now has 19 .chibi files; CompilerSources.txt:1-18 lists 18, and test/language/package_compiler_chibi_source.h.in:51-69 doesn't list the new one either. Traced, not run.
Fix: delete the file (no consumer, see the SIMPLIFY finding). If it must stay, add it to CompilerSources.txt and package_compiler_chibi_source.h.in in the same commit, after fixing the next finding.
```

```
[BUG] tools/compiler/package_compiler/CallCensus.chibi:28 — `strings.Concat` doesn't exist, so the file can't compile once it is listed
Cost: write, debug — the first build that lists the file fails. Nothing has compiled the file yet, because no source list includes it.
Evidence: packages/core/strings/Strings.chibi (the only file of core:strings) has `pub Concatenate :: (a: string[])` at :551 and no `Concat`. git grep finds no other use of `strings.Concat(`.
Fix: drop the prefix (see the DESIGN finding), which removes the call. Otherwise write `strings.Concatenate(["call:", callee])`.
```

```
[BUG] tools/compiler/package_compiler/CallCensus.chibi:72 — CalleeName passes `end` where Slice takes a byte count
Cost: run — every callee text is wrong, so every `counts` key and every MarkDead comparison is wrong
Evidence: Strings.chibi:22-23 declares `Slice(value, start, count)`, and "count clamps to its remaining bytes". The package's own helper, Tree.chibi:66-70 `Text`, passes `end - start`. Worked example: the call at cost_explanations/Explain.chibi:34 has callee `parts.append` at bytes 552..564. CalleeName returns 564 bytes ("parts.append(strings.Slice(digits, int(rest % 10), 1));\n    ...") instead of the 12-byte `parts.append`. The result is right only when start == 0, and no callee starts there, because every document opens with its `package` header. The likely source of the confusion is source_parser/Tree.chibi:247, a `Slice(text, start, end)` helper in another package that takes an end offset.
Fix: `return Text(forest, text, Field(forest, node, "function"));` and delete the body. Also assert that the field exists. grammar.js:604-610 makes `function` required, so -1 means an error tree or a bug. Returning "" files that call under the key "call:" without saying anything.
```

```
[BUG] tools/compiler/package_compiler/CallCensus.chibi:25 — iterates the whole multi-document forest but reads every offset from one document's text
Cost: run — on a multi-document snapshot, calls from every document except the one whose text was passed get the wrong callee, line and column
Evidence: Tree.chibi:7-8: "Links are global node indices ... offsets count bytes of the node's document text". Snapshot.chibi:334 appends every document's nodes to one array, and `Node` (:203) rebases only the links, never start or end. Every other pass pairs a document's nodes with that document's text: Compile.chibi:977-978 (`documents[file].text` with `syntax_root`) and Behavior.chibi:826-827 (`node_start ..< node_start + node_count`). In the compiler snapshot (31 documents), 30 of them read from the wrong text. Past the end, Slice clamps and ByteAt returns -1 (Strings.chibi:11), so the census gives wrong values without failing. The struct comment at line 16 says "in a document".
Fix: take a `snapshot.Document` and loop `document.node_start ..< document.node_start + document.node_count` against `document.text`.
```

```
[BUG] src/types/array.c:136 — `assert(matches++ <= array->count)` has a side effect, so the result depends on the build mode
Cost: debug — every build with asserts enabled returns one more than the true count, while release builds return the true count, so a test can't agree with release
Evidence: a scratch copy of the loop and the assert returned 3 for 2 matches when compiled with asserts, and 2 when compiled with -DNDEBUG. Release configurations compile with `-O3 -DNDEBUG` (docs/evidence/core-math-20261028-integration/native-308.json:204). Test profiles force `-UNDEBUG` (.github/verify-native-profile.py:37-39). Without the `++`, the condition still can't fail, because the loop increments `matches` at most `count` times.
Fix: delete the assert, or delete the whole function (see the SIMPLIFY finding on array.h below).
```

```
[DESIGN] tools/compiler/package_compiler/CallCensus.chibi:28-29 — `counts` is keyed by `"call:" + callee text`, while sites store the bare callee and MarkDead never updates counts
Cost: change, debug — a consumer that reads `counts[site.callee]` silently gets 0. After MarkDead, `counts` and `LiveSites` disagree. One string allocation per site (about 14,294 on the compiler closure) builds a constant prefix.
Evidence: a missing key reads as zero (docs/code-language.md:477-480), so the key mismatch never fails. The map holds one kind of key, so the prefix carries no information. MarkDead (:45-53) writes only `sites[i].is_dead`. Keys are source text, not callables: `Field` names three different functions (package_compiler/Tree.chibi:38, asset_compiler/Compile.chibi:49, signature_reader/Reader.chibi:40), which a whole-snapshot census counts under one key. Meanwhile, whitespace or a different import alias splits one callable across several keys.
Fix: key by the bare callee, or derive counts from the sites when they are rendered. If the report needs callee identity, key by the resolved callable instead of its spelling.
```

```
[PERF] tools/compiler/package_compiler/CallCensus.chibi:30-31, 76-96 — LineOf and ColumnOf rescan the text from byte 0 for every call site to recompute what each node already stores
Cost: run — O(sites × document bytes) native ByteAt calls, against zero work for the stored fields
Evidence: SnapshotNode stores `row` and `column` (Snapshot.chibi:37-38), both 0-based. Tree.chibi:73 `Diagnose` reports `row + 1` and `column + 1`, the same 1-based byte line and column that LineOf and ColumnOf compute. Estimated as the sum over sites of 2 × offset: about 391 million ByteAt calls for Body.chibi alone (1,779 sites, 191,198 bytes), and about 2.28 billion over the compiler closure.
Fix: delete LineOf and ColumnOf, and read `forest[node].row + 1` and `forest[node].column + 1` where the report renders a location.
```

```
[PERF] src/types/array.c:130-133 — xstrdup and free for every element, only to compare a string the loop already holds
Cost: run — two allocator calls per element for a strcmp. For packed arrays the answer is constant, so the loop is pure waste.
Evidence: array.h:91 says pin_array_get returns a borrowed element, so `value.type_name` can be compared in place. For `stride > 0` arrays, pin_array_get builds every element with `owner->type_name` (array.c:361-364), so the result is either `count` or 0.
Fix: `strcmp(value.type_name, type_name)` without the copy. For packed arrays, compare `array->type_name` once and return `count` or 0.
```

```
[SIMPLIFY] tools/compiler/package_compiler/CallCensus.chibi (whole file) — a census with no consumer, plus a forwarding alias and a filter API nobody calls
Cost: read, change, run — 96 lines that every compiler build must compile once listed, and that every reader must rule out as a caller of anything
Evidence: 0 references outside the file for all 9 declarations. None is `pub`, so cost_explanations can't import them, and the comment's "for the cost-explanations report" (:22) names a link that doesn't exist. Explain.chibi already receives finished line and column records. `CountCallsInForest` (:39-42) only forwards to `CollectCalls`. `MarkDead` and `LiveSites` have no callers. `is_dead` is written `false` at :33 and read only by `LiveSites`, so a stored bool stands in for list membership. MarkDead does sites × removed string compares.
Fix: delete the file. When the report needs a census, write it in cost_explanations next to its consumer, as a list of call-expression node indices per document, with the rest derived when the report is rendered.
```

```
[SIMPLIFY] tools/compiler/package_compiler/CallCensus.chibi:7-14 — CallSite stores four fields that its `node` field already determines
Cost: change — offset, line, column and callee are copies of forest facts that must stay in agreement with the forest
Evidence: `offset` is `forest[node].start` (:32). `line` and `column` are `row + 1` and `column + 1` (Snapshot.chibi:37-38). `callee` is `Text(forest, text, Field(forest, node, "function"))`. `is_dead` is covered by the finding above.
Fix: if the census survives, a site is its node index (`int[]`).
```

```
[SIMPLIFY] src/types/array.h:52-53, src/types/array.c:122-138 — a new public SDK C entry point with no caller
Cost: change — once it ships in an installed header, its semantics are fixed (NULL array → 0, NULL element name matches ""), and the project's C exit gets one more function to port
Evidence: LSP find_references and git grep find only the declaration and the definition. sdk/CMakeLists.txt:166 installs types/array.h as a PUBLIC chibi_types header. The repository rules say to write new code in Chibi and to write C only for what Chibi can't run yet, and no C or Chibi caller needs this.
Fix: delete the declaration and the definition.
```

```
[TEST] tools/compiler/package_compiler/CallCensus.chibi, src/types/array.c:122 — neither addition has a test
Cost: debug — each of the four BUG findings in the census, and the assert side effect in C, would have failed a minimal case
Evidence: the diff contains no `@test` and no pico_unit case. The census is pure over a forest and a text, and cost_explanations/ExplainTests.chibi shows authored `@test`s in this area. Two calls in one document would catch the Slice bug. Two documents would catch the whole-forest bug. A test that lists the file would catch the missing `strings.Concat` and the unlisted source. A pico_unit case in test/tools, built with asserts on, would have returned count + 1.
Fix: if the code survives, add an authored `@test` beside its consumer for the census and a pico_unit case for the C function. Otherwise delete both and add nothing.
```

```
[NIT] src/types/array.c:124-126, 130 — silent guards hide the bugs they meet
Cost: debug — a NULL array reads as "0 matches"; an element with a NULL type name matches a "" query; a NULL `type_name` argument goes straight into strcmp (undefined behavior)
Evidence: the repository rules say to avoid defensive NULL checks that mask defects. pin_array_get already asserts that its array is valid (array.c:352).
Fix: `assert(array && type_name)`, and skip elements whose type name is NULL instead of replacing it with "". This is moot if the function is deleted.
```

```
[NIT] commit cd428b92e message — two unrelated changes in one commit, and no reason given for either
Cost: read, change — a reader can't tell who needs the census or the count, and one can't be reverted without the other
Evidence: the message names two features with no shared code. The repository rules ask commit messages to say what changed and why.
Fix: split it into two commits, each naming its consumer. This is moot if both changes are deleted.
```

## What I would delete

- `tools/compiler/package_compiler/CallCensus.chibi`, the whole file. This also clears the build break.
- `pin_array_count_type`: the definition at `src/types/array.c:122-138` and the declaration at `src/types/array.h:52-53`.
- If the census is kept and moved next to its consumer, delete these parts of it:
  - `CountCallsInForest`, `LineOf`, `ColumnOf` and the body of `CalleeName` (use `Text`).
  - `CallSite.offset`, `line`, `column` and `is_dead`.
  - `MarkDead`, `LiveSites`, and the `"call:"` prefix with its `Concat` call.

## What I would measure

- Run `make build` or `make bootstrap-recover` on this commit to confirm the rejection message from `packages_complete`. I traced it through source and didn't run it, because the checkout has no build.
- Count the real number of call sites by counting `call_expression` nodes in the compiler snapshot. The 14,294 and 1,779 figures above are regex estimates.
- Time LineOf and ColumnOf on Body.chibi under bytecode, to put seconds on the estimated 391 million ByteAt calls.
- Check whether stage one accepts the bare `for index in 0..<n` header and the `census: ^CallCensus` / `census^.sites` forms. No compiler source uses either today: they write `for (...)`, and no package_compiler file takes a `^` parameter. `packages/core/bytes` uses the bare `for` form. Compiling the file once it is listed would settle this.
- Find out, for any future caller of `pin_array_count_type`, whether it passes items arrays or packed arrays, and whether element type names ever differ within one array. That decides whether a per-element loop is needed at all.
- If `MarkDead` survives, measure the size of `removed`, which is the second factor of its sites × removed string comparisons.

## What I would leave alone

- `forest[index].kind == "call_expression"` (`:26`) is a string comparison per node, but the forest stores every kind as a string, and `Is` (Tree.chibi:51) and its callers rely on that. Interning kinds is a forest-wide change, not a fix for this commit.
- `ByteAt(...) == 10` is a raw byte code where a character literal would read better. Stage one rejects character literals (docs/language-friction.md:307-316), and the compiler's convention is raw codes, as in Explain.chibi:56. The question goes away once LineOf and ColumnOf are deleted.
- `Field(forest, node, "function")` looks up a child by name string. It is the package's existing helper, and a `call_expression` has two children, so the scan is negligible.
- The NULL-tolerant neighbors in array.c (`pin_array_mutation_end`, `pin_array_release`, `pin_array_equal`) follow an older convention. Changing them is out of scope for this commit.
- Untyped `int` forest indices are used throughout the compiler. A typed index for this file alone would make it the odd one out.
