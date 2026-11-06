# Review of 2797053cb: "Parse else-if chains iteratively, shorten nesting and name the opening bracket in errors"

Checkout: `/Users/cwaldner/Personal/chibi/.worktrees/review-eval/chibi` (read only). The commit touches 8 files, +174/−117. I read every changed file in full at the commit, plus the parser's other files (`Parser.chibi`, `Types.chibi`), its only consumer (`test/language/SourceParserDiff.chibi`), the fixture call path (`test/language/compiler_fixture.c`), the stage-zero depth limit (`src/language/semantic.h`, `src/execution/call.c`) and the docs that describe the parser and its tests.

## Brief

- **Transform:** Chibi source text goes to tokens (`source_tokenizer`), then to node rows (one leaf per non-comment token, with interior rows appended after them), then to a Tree-sitter-shaped `Tree`, or to one `Diagnostic`. The commit changes four things:
  1. The call structure. It deletes `ParseExpression`, `ParsePostfix` and `ParseArgument` and inlines their bodies, so 51 sites now spell `ParseBinary(c, n, OrPower)`.
  2. Else-if chains are built in a loop, with a range fix-up afterwards.
  3. `Expect` adds the position of the unclosed opening bracket to "missing closer" messages.
  4. Test plumbing: three `Targets.chibi` entries, `Strings.chibi` in the fixture's file list, and the tokenizer cap raised from 256 KiB to 512 KiB.
- **Counts (at the commit):**
  - Files: 234 `.chibi` files under `packages/`, `test/` and `tools/`, 2,888,323 bytes, plus 11 `syntax_corpus/*.chibi.txt`. That is about 0.5 M tokens by a regex estimate.
  - Largest files: `Steps.chibi` (313,206 B, ≈60 k tokens) and `Body.chibi` (180,122 B, ≈35 k tokens).
  - If statements: 6,505 `if (` occurrences, 464 of them `else if`, so ≈6,040 chains. The longest chain has 36 links (`Body.chibi:2728–3423`). The next longest have 17.
  - Nesting: at most 12 braces deep and 7 brackets deep (rough scan).
- **Consumers:** today only `SourceParserDiff.chibi` `Differ`, through `language-source-parser-chibi`. Per file and backend it runs `parser.Diagnose` and then `parser.Parse`, and each one does a full `Build`. The plan is for the parser to replace `src/language/src/parser.c` behind `compiler_syntax` (`docs/source-parser.md:3-9`). The compiler runs on stage-one output, which has no call limit (`docs/language-friction.md:658-669`).
- **Layer:** cold today (test only), but it is meant to become hot: the compiler front end on every file, and editor diagnostics while the buffer is invalid. So I hold it to compiler-pass cost.
- **Minimum work:**
  - Parsing: O(1) work per token and per node, ≈0.5 M tokens per corpus parse, which should take about a second on the VM.
  - Else-if chains: zero allocations; one `end` write per link.
  - A rejected parse: one backward scan over ≤ `pos` tokens to find the opener, and one message.
  - Call depth: proportional to source nesting. Any constant per level is fine natively.
- **Actual work (changed paths):**
  - **Depth, in frames per nesting level.** A nested call argument costs 3 frames: `ParseUnary`, `ParseArguments`, `ParseBinary`. Before the commit it cost 7: `ParseUnary`, `ParsePostfix`, `ParseArguments`, `ParseArgument`, `ParseIndexValue`, `ParseExpression`, `ParseBinary`. A parenthesized group costs 3 (was 4). An else-if chain costs one frame in total (was one per link). The base, from `Differ` down to the first statement of a package function, is 12 frames: `Differ` → `Diagnose` → `Build` → `Construct` → `ParseSourceFile` → `ParseDeclaration` → `ParseNamed` → `ParseNamedValue` → `ParseFunctionRest` → `ParseSignature` → `ParseCallableBody` → `ParseStatement`.
  - **Against stage zero's 64-call limit.** At block depth 4, about 5 nested calls fit before the commit and about 13 after.
  - **Corpus model.** I applied fixed frame costs per construct (block +3, call +7→+3, group +4→+3, index +5→+3, literal +5→+4, statement expression +4→+3, else-if link +1→0) to the corpus. The peak comes out at ≈78 frames before (9 files over 64) and ≈51 after (no files over 64). This is an estimate; the number to collect is under "What I would measure".
  - **Per if chain:** one `int[]` allocation, one append per extra link, and 1–2 string comparisons per link (`kind`, `field`), followed by a backward pass. That is ≈6,040 allocations per corpus parse, and ≈24 k per test run (2 builds × 2 backends).
  - **Per rejected parse:** every `Expect` reached while unwinding (one per enclosing bracket construct, about 10 for a deep spot in `Steps.chibi`) builds a full message: `Expected`, `Found`, a 120-string `KindNames()` array, and an `Opener` scan from the last token backward. `Fail` keeps the first message only. That comes to ≈10 × 60 k ≈ 0.6 M discarded token reads per failed parse of a large file.
  - **Test:** two `Build`s per accepted file per backend.
- **Gap:**
  - Depth is not a work gap. The problem is that the grammar's shape is fitted to a limit of the test harness.
  - If chains: ≈6 k allocations against 0. The absolute cost is small.
  - Failure path: ≈10× one scan.
  - Differential: 2× the parse work it needs.
  - Unknown: steps per byte (the budget allows 6,000 per byte), the real peak depth, and the VM's native stack per call. All three are listed under "What I would measure".

## Target shape

Remove the cause (the 64-call limit as the fixture applies it). Then let the grammar take its natural shape again, as Zig's and Odin's parsers do:

```chibi
// Any expression: the grammar's `_expression`, as Zig's parseExpr and Odin's parse_expr.
ParseExpression :: (c: ^Cursor, n: ^(Node[])) -> int {
    return ParseBinary(c, n, OrPower);
}
```

| Piece | This commit | Target |
|---|---|---|
| Expression entry | `ParseBinary(c, n, OrPower)` spelled at 51 sites | `ParseExpression` at 51 sites, one definition |
| Postfix forms | loop inside `ParseUnary` | same; the comment moves back onto the loop |
| Unnamed argument | inline copy of the range suffix | `ParseIndexValue(c, n)` |
| Else-if | loop + `chain` array + backward pass | recursion (13 lines, ranges from `Add`), or the loop with a 5-line forward fix-up |
| `Expect` after a failure | rebuilds and discards the message | `if (c.failed) { return -1; }` first |
| Depth limit | 64 calls, so the grammar is shaped to fit | differential runs on stage-one output, or the limit is raised per the friction entry |

What goes away:
- the 51 call sites that spell the climber's internals;
- the duplicated range branch;
- the `chain` array and its string tests;
- the discarded failure work;
- the rule "keep nesting shallow", which is now an unwritten constraint on every future grammar change.

## Findings

```
[ARCH] tools/compiler/source_parser/Expressions.chibi:62 (51 call sites), Statements.chibi:192, Expressions.chibi:174 — the grammar's call structure is fitted to stage zero's 64-call VM limit
Cost: change and read — every expression entry is now spelled as the precedence climber's internals, in 51 places across three files. Any form below OrPower has to touch all of them; half-open ranges are already special-cased at three sites after the climber (Expressions.chibi:224, :391, Statements.chibi:246). The limit is still there, so the next deeper input will break again and invite another round of hand-flattening. In production (stage-one output and native code, neither with a limit) the flattening buys nothing.
Evidence: SCRIPT_CALL_DEPTH_LIMIT is 64 (src/language/semantic.h:12) and applies to functions compiled by stage zero. The fixture compiles the parser with stage zero; package_compiler_chibi.c:13984-13986 names "the stage-zero fixture's call depth limit" and runs deep_operands on the recovered compiler instead. A nested call argument went from 7 frames to 3. Body.chibi:2728-3423 is one 36-link else-if chain, which alone needed 36 ParseIf frames under the old recursion. The corpus model puts the peak at ≈78 frames before and ≈51 after, which leaves ≈13 frames (about 4 more nested calls or blocks) of headroom. docs/language-friction.md:642-656 already records this friction: package-compiler walks "became loops over an explicit stack". It suggests a deeper budget. The commit doesn't extend that entry, and its message doesn't state the cause. Zig (parseExpr → parseExprPrecedence(0)) and Odin (parse_expr → parse_binary_expr(p, lhs, 0+1)) both keep a one-line named expression entry, and both recurse for else-if.
Fix: remove the cause. Either run the differential on stage-one output, as deep_operands does, or raise SCRIPT_CALL_DEPTH_LIMIT as the friction entry suggests, after measuring the VM's native stack per call. Then restore ParseExpression (3 lines), call ParseIndexValue from ParseArguments, and either return ParseIf to recursion or keep the loop with the fix-up below. If the limit has to stay, add this parser to the friction entry, and put a comment at ParseBinary that says why there is no ParseExpression.
```

```
[PERF] test/language/SourceParserDiff.chibi:10,21 — Differ builds every accepted file twice per backend (pre-existing, in the consumer of the changed code)
Cost: run — Diagnose (Parser.chibi:29) and Parse (Parser.chibi:17) each call Build, which tokenizes and parses the text. For accepted files that is 2 × 2.89 MB per backend, or 4 builds per file per test run where 2 are enough.
Evidence: Differ calls parser.Diagnose(text) at line 10 and then parser.Parse(text) at line 21. docs/testing.md:1685 says the test parses each file "once on each backend".
Fix: call Parse first. A rejected parse returns the one-row ERROR tree that Failure builds (Parser.chibi:41-47), so call Diagnose only in that case, for its message. Rejections are rare (a handful of invalid inputs). This halves the differential's parse work without changing any API.
```

```
[ARCH] tools/graph/engine/Targets.chibi:755,3601,3620 — registering one test source takes three entries in the graph and two in CMake, and the package closure is kept by hand in two index-aligned arrays
Cost: change — the parent commits (5a693c4b0, bb45c3db7) registered source_parser_chibi.c in test/language/CMakeLists.txt:284-289, main.c and tools/test_policy.py, but not in Targets.chibi. Until this commit, `make build` (the graph build on Linux and macOS, Makefile:41-45) couldn't link test_language, because main.c:758 calls test_language_source_parser_chibi. In the same way, PARSER_FILES and PARSER_PATHS (test/language/source_parser_chibi.c:29-47) mirror the package import closure by index. This commit had to add Strings.chibi to both because syntax_diagnostics imports core:strings (Diagnostics.chibi:3).
Evidence: the parent's Targets.chibi has no source_parser_chibi.c entry. SourceDefines is a chain of string comparisons over four file names (Targets.chibi:3619-3623). LanguageTestsOverSource repeats the same files to add the `src` include.
Fix: one per-file record for test_language sources (file, CHIBI_SOURCE_ROOT define, `src` include) that Files, SourceIncludes and SourceDefines all read, and one {file, path} array in the C test instead of two arrays kept aligned by index. That deletes the two separate lists. The CMake copy can go once graph-reference no longer needs it.
```

```
[PERF] tools/compiler/source_parser/Tree.chibi:116-118 — after the first failure, every Expect still builds a full message, including an Opener scan, and Fail throws it away
Cost: run, on the failure path. Each rejected parse runs one Opener scan per enclosing bracket construct on the way out. Each scan starts at the last token, because Fail moved c.pos to c.final, and walks back toward 0. Each call also runs Expected/Found/format_text and builds the 120-string KindNames() array.
Evidence: Fail keeps only the first message (Tree.chibi:91-100). Every token loop exits on c.failed and then reaches its closing Expect: ParseBlock :12, ParseCallableBody :23, ParseArguments :404, ParseCollection :341/:353, ParseStructLiteral :371, ParseParameters :329, ParseSwitch :287. At a deep spot in Steps.chibi (≈60 k tokens, brace depth 8, bracket depth 5) that is ≈10 discarded scans, or ≈0.6 M token reads, against one scan of at most pos tokens: about 10×. This is cold today, because Tree-sitter rejects only a handful of corpus inputs. It becomes hot once Diagnose serves the editor, where the buffer is invalid while the user types.
Fix: make `if (c.failed) { return -1; }` the first line of Expect. End, TakeName and TakeMember also build Expected messages on the way out; give them the same guard. While you're there, Opener can use Describe(opening) instead of its own KindNames() lookup and hand-written backticks.
```

```
[SIMPLIFY] tools/compiler/source_parser/Statements.chibi:211-224 — the else-if fix-up allocates an array and compares strings to rediscover a chain the loop has just built
Cost: read and run. It is 14 lines, and its comment misstates the invariant. Each if statement allocates one int[] (≈6,040 per corpus parse, ≈24 k per test run), and each link costs 1–2 string comparisons, because Node.kind and Node.field are strings.
Evidence: each link's last child is the next link. The link is added while still empty (:208), and nothing is added after it, so every link ends where the last link, `current`, ends. The comment says each link "still ends where its own last child did before the next link was added". In fact Add (Tree.chibi:75) copies the empty link's end, which is -1 (NewNode, Tree.chibi:50).
Fix (this deletes `chain`, the kind and field tests, and the backward index loop):
    // Each link got its successor while it was still empty, so every
    // link ends where the last one does.
    link := first;
    while (link != current) {
        n^[link].end = n^[current].end;
        link = n^[link].last;
    }
```

```
[SIMPLIFY] tools/compiler/source_parser/Expressions.chibi:390-397 — ParseArguments repeats ParseIndexValue's half-open range suffix
Cost: change — there are now two copies of "an expression, then an optional `..<` end", so a fix to range parsing has to land in both. ParseIndexValue's comment (:221) still says it reads "an index or an argument", but its only caller is the index branch at :187.
Evidence: lines 390-397 and 222-231 are the same algorithm. A search of the package finds one caller of ParseIndexValue.
Fix: `value := ParseIndexValue(c, n);` deletes 7 lines. It costs one frame per nested argument, so it goes together with the first finding.
```

```
[TEST] tools/compiler/source_parser/Tree.chibi:270 — the new "(to close the `(` at r:c)" text has no case
Cost: debug — the message is user-facing, and nothing checks the opener's position, the bracket pairing across kinds, or the empty suffix for tokens that aren't brackets. Differ only checks that both parsers reject the same texts (SourceParserDiff.chibi:11-16). It prints a message only when the parser wrongly rejects valid text.
Evidence: "to close the" appears nowhere outside Tree.chibi. tools/compiler/cost_explanations/ExplainTests.chibi shows that a tools/compiler package can hold @test cases.
Fix: authored @test cases in source_parser for Diagnose on three inputs: an unclosed `(` inside a call on line 2 (the message names its row and column), a `[` nested in a `(` (the message names the `[`), and a missing statement end (no suffix).
```

```
[TEST] tools/compiler/source_parser/Statements.chibi:192 — only the current shape of Body.chibi pins the iterative chain
Cost: change — if Body.chibi's 36-link chain is ever split, nothing in the corpus exercises a chain longer than 17 links. A regression back to recursion, or to a broken fix-up, would then pass.
Evidence: the longest chains in the corpus are 36 (package_compiler/Body.chibi), 17 (prepared_writer/Writer.chibi, llvm_native/Emit.chibi), 15 and 14. None of the 11 syntax_corpus files comes close.
Fix: add test/language/syntax_corpus/else_if_chain.chibi.txt with a chain of about 100 links, so the differential checks every link's range against Tree-sitter regardless of how the repository's own code is written.
```

```
[STYLE] tools/compiler/source_parser/Expressions.chibi:213-214 — the postfix comment now sits on IsCallTarget
Cost: read — the comment describes the loop in ParseUnary ("ParseUnary reads them after the primary"). That loop, at :174-203, now has no comment, and the comment sits above a predicate about call targets.
Fix: move the two lines to just above `left := ParsePrimary(c, n);` at :174.
```

```
[STYLE] test/language/source_tokenizer_chibi.c:27, docs/testing.md:1692-1693, docs/source-parser.md:20 — the workload statements and the message example are stale
Cost: read — the commit edits the very sentence that says "about 150 files, under 1 MiB". At this commit the corpus is 234 files and 2,888,323 bytes (2.75 MiB): 1.6× the files and 2.75× the bytes. docs/testing.md says "about 145 files under 1 MiB". These lines are how a reader sizes the test's budget and run time. docs/source-parser.md still shows ``Expected `)`, found `return` `` without the new suffix.
Fix: update the three lines. Better, describe the workload by its roots and the per-file cap rather than by counts that drift.
```

## Correctness notes

- **The parent was red, and this commit quietly fixes it.**
  - At bb45c3db7 the graph build of `test_language` had no `source_parser_chibi.c`, so linking failed on the `main.c:758` call.
  - The tokenizer differential rejected `Steps.chibi`, which at 313,206 bytes is over the 262,144-byte cap.
  - The parser fixture lacked `Strings.chibi`, which `syntax_diagnostics` imports (`Diagnostics.chibi:3`), so the fixture's compile presumably failed too. I didn't run it.

  The commit message mentions none of this. A reader looking for when `main` went green won't find it here.
- **`test/language/source_parser_chibi.c:85`: the budget overflows `int` for files below the cap.** The expression is `(int)(size * STEPS_PER_BYTE) + 100000`, which leaves `int` range above 357,897 bytes. `MAX_SOURCE` is 524,288. When that happens, `compiler_fixture.c:528` fails `REQUIRE(... budget > 0)`, and the message added at `:97-99` then reports "the comparison did not finish". `Steps.chibi` is 313,206 bytes, which leaves 44,691 bytes of headroom. It just grew past the tokenizer's old cap. Fix: compute the budget in `size_t` and cap `MAX_SOURCE` at `INT_MAX / STEPS_PER_BYTE`, or make the call budget `uintptr_t`, as `compiler_fixture_tests_budget` already is.

## What I would delete

- `Statements.chibi:213-224`: the `chain` array, its `kind`/`field` string tests and the backward loop. Five lines replace them, or nothing at all if `ParseIf` goes back to recursion.
- `Expressions.chibi:390-397`: the copied range branch.
- After the depth cause is fixed, the 51 spelled-out `ParseBinary(c, n, OrPower)` calls, in favor of `ParseExpression`.
- `source_tokenizer_chibi.c`'s `MAX_SOURCE` check, unless it is meant as a file-size policy. It sizes no buffer: it is used only at lines 33 and 453-455, and the per-byte step budget already bounds each call. This commit's only contact with it was raising it because a file grew. If it is a policy, the commit should say why 512 KiB.
- `SourceDefines`' chain of file names and `LanguageTestsOverSource`'s separate list, in favor of one per-file table.

## What I would measure

- **Peak call depth** per corpus file on each backend at this commit, by recording the maximum `ctx->call_depth` in the fixture. My model says ≈51 of 64 at the peak (`union_members/Members.chibi`, `toolchain/StageOne.chibi`) and ≈78 before the commit. This decides how close the cliff is.
- **Native stack per VM call level** on the bytecode and SLJIT backends. This is what a raised `SCRIPT_CALL_DEPTH_LIMIT` would cost, and it is what the friction entry's "budget counted in frame bytes" would count.
- **The parser's steps per byte** on each backend. The budget allows 6,000 per byte; the tokenizer takes under 20. Split the steps between the `Diagnose` and `Parse` builds to confirm the 2× in the second finding, and look for a quadratic: `Attach`'s per-comment descent from the root (`Parser.chibi:94-118`) is the first candidate.
- **Wall time** of `language-source-parser-chibi` before and after the single-build `Differ`.
- **Steps for `Diagnose`** on `Steps.chibi` with an unclosed `(` near its end, with and without the `Expect` guard.
- **Whether dropping the three wrapper frames changed parse steps.** The commit claims no speedup, so this matters only if someone keeps the inlining for speed.

## What I would leave alone

- **`Opener`'s backward scan for the first failure.** It is one O(pos) scan per rejected parse. A bracket stack on `Cursor` would add work to every successful parse to save it. The tokenizer keeps a bracket stack (`Tokenizer.chibi:203-205, 326-343`) without positions, and adding positions just for diagnostics isn't worth it.
- **Folding `ParsePostfix` into `ParseUnary` and `ParseArgument` into `ParseArguments`.** Each had one caller, and a single loop reads well once the comment is back and the range branch calls `ParseIndexValue`.
- **The three-way closer-to-opener `if` chain in `Opener`.** There are three cases. Deriving the opener with arithmetic on token numbers would be a bit trick.
- **`Strings.chibi` in the fixture.** `syntax_diagnostics` needs it for its Tree-sitter `Diagnose`. Splitting that package to avoid compiling 16 KB once per run would cost more than it saves.
- **String `Node.kind` tests in `IsCallTarget`.** These are pre-existing, and the parser has to emit those strings anyway. Tagging kinds is a representation change that belongs with the switch away from Tree-sitter.
- **The reflowed lines in `main.c` and `source_parser_chibi.c`.** They are clang-format output.
