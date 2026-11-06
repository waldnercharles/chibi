# Review of 2797053cb: "Parse else-if chains iteratively, shorten nesting and name the opening bracket in errors"

Checkout: `/Users/cwaldner/Personal/chibi/.worktrees/review-eval/chibi`. The commit changes 8 files (+174 / −117).

How I reviewed it:

- **What I read.** I read `Tree.chibi`, `Expressions.chibi`, `Statements.chibi`, `Parser.chibi`, `SourceParserDiff.chibi` and `source_parser_chibi.c` in full. For `Declarations.chibi`, `source_tokenizer_chibi.c`, `main.c` and `Targets.chibi`, I read the changed hunks and their surroundings.
- **Callers.** I found callers with grep, because the LSP has no server for `.chibi` files.
- **Which version I read.** The checkout's HEAD is `cd428b92e`, not this commit. The parser and test files are byte-identical at both commits (`git diff 2797053cb HEAD` is empty for them). I read every other file with `git show 2797053cb:<path>`.
- **What I ran.** Nothing was built or run, because the review is read-only. The counts come from small scripts I ran over `git archive 2797053cb` in a scratch directory, which I have since deleted.

## What the commit does

- It removes `ParseExpression`. Its 49 callers now call `ParseBinary(c, n, OrPower)` directly, so 51 call sites spell it out, counting the two the commit inlined.
- It folds `ParsePostfix` into the end of `ParseUnary`.
- It inlines `ParseArgument` into `ParseArguments`, with its own copy of `ParseIndexValue`'s range parse.
- `ParseIf` now loops over the links of an else-if chain and patches their end offsets afterwards.
- On a missing `)`, `]` or `}`, `Expect` adds `" (to close the `(` at L:C)"`. A new `Opener` function finds that position with a backward bracket scan.
- It also makes three test-infrastructure changes that the commit message doesn't mention:
  - it raises the tokenizer differential's file cap from 256 KiB to 512 KiB;
  - it registers `source_parser_chibi.c` in `Targets.chibi`;
  - it adds `core/strings` to the parser fixture.

I traced each rewrite against the parent, and the trees come out the same:

- `New` creates nodes in the same order, and the children and fields are the same.
- The node ranges match once the `ParseIf` fixup has run.
- The inlined argument code matches `ParseArgument` plus `ParseIndexValue` branch for branch.
- All 51 new calls pass `OrPower`, which is what `ParseExpression` passed.

So the Tree-sitter differential is still a valid check, and I found no BUG.

## Brief

- **Transform:** The input is Chibi source text. `source_tokenizer` turns it into tokens, and the tokens become `Node[]` rows: the tokens first, then the interior nodes the parser appends. The output is `Tree.Parsed` rows in Tree-sitter's order, or a single `Rejected` diagnostic. This commit changes how three parse functions build those same rows, and it changes the wording of missing-closer diagnostics.
- **Counts:** The corpus has 245 files: 234 `.chibi` files under `packages/`, `test/` and `tools/`, plus 11 `syntax_corpus/*.chibi.txt`.
  - The largest file is `package_compiler/Steps.chibi`: 313,206 bytes, about 62k tokens.
  - It holds about 6,100 if statements (6,571 `if` keywords). Only 212 of them have an else-if.
  - The longest else-if chain has 36 links (`Body.chibi:2728`); the next longest have 17, 17 and 15. The syntax corpus contains a single else-if.
  - The deepest bracket nesting is 13 levels (`union_members/Members.chibi:508`).
  - The parser makes 51 entry calls at `OrPower` and has 78 `Expect` call sites.
  - The number of interior rows per file is unknown.
- **Consumers:** Today the only consumer is the differential: `test/language/SourceParserDiff.chibi`, driven by `source_parser_chibi.c`. `docs/source-parser.md` says "Nothing calls it yet". The planned consumer is `compiler_syntax`, which will drop Tree-sitter's generated parser. People read the `Diagnose` messages; no test reads them.
- **Layer:** The parser is meant for a hot layer (the compiler front end) but is cold today, since only a test calls it. In the fixture it runs on both VM backends under the language-wide limit of 64 nested calls.
- **Failure classes:**
  - Invalid external input: a source program that `Rejected` names. This is where the message change applies.
  - An environment limit: the VM's call-depth limit (`BEHAVIOR_FUNCTION_DEPTH`). Source that is valid but deeply nested fails the call instead of getting a diagnostic.
  - No new bug paths.
- **Twins:** Tree-sitter's generated `src/language/src/parser.c` (1.69 MB), with its scanner and `grammar.js`, implements the same parse. This parser replaces it, and the differential compares them row by row. The new opener clause has no twin.

## Findings

```
[DESIGN] tools/compiler/source_parser/Expressions.chibi:214 — the parser is flattened by hand to fit the VM's 64-call depth limit; the commit says "to keep nesting shallow" and doesn't name the limit
Cost: read and change now. 51 call sites spell "an expression" as ParseBinary(c, n, OrPower). There is a copied range parse (finding 3) and an end-patching pass in ParseIf (finding 2), and ParseUnary now holds both the prefix and the postfix forms. Debug later: about 16 frames of headroom remain, and the next file that nests deeper fails with "call depth exceeded" instead of a parser diagnostic.
Evidence:
- src/language/semantic.h:11-12: `#define SCRIPT_CALL_DEPTH_LIMIT 64`, commented "One language-wide recursion budget; authored source cannot override it". Both fixture backends enforce it (src/execution/call.c:278-281, src/execution/machine.c:777-781).
- Frames per nesting level, read from the parser's call graph:
  - a call argument: 7 → 3. Before: ParsePostfix→ParseArguments→ParseArgument→ParseIndexValue→ParseExpression→ParseBinary→ParseUnary. Now: ParseUnary→ParseArguments→ParseBinary.
  - an index: 5 → 3.
  - a parenthesized group: 4 → 3.
  - each else-if link: 1 → 0.
  - a nested if, while or switch: unchanged at 2-3.
- Depth at the deepest corpus sites, counted from Differ at one frame per Chibi call, ±2 (method in the appendix):
  - Body.chibi:3505, `return Body.Failed(Diagnose(… format_text(… Text(… Field(item.node…)))))` inside six blocks: about 70 frames at the parent, about 48 now.
  - Body.chibi:2728, the 36-link chain: at the parent, any call inside its later links passes 64.
  - Members.chibi:508, the 13-bracket maximum: about 56 → 47.
- So about 16 frames are left, which is roughly 5 more levels of calls or blocks. A failing Expect also adds about 4 frames (Expect→Expected→Found→Slice).
- docs/language-friction.md:642-656, "Calls nest at most 64 deep", records two earlier stage-one walks that "became loops over an explicit stack". This commit is the third case, and it doesn't extend that entry.
Fix: Follow that entry's suggestion: a deeper limit, counted in frame bytes rather than calls. Then restore ParseExpression, ParsePostfix, ParseArgument and the recursive ParseIf. While the limit stays at 64:
- add the source parser to the friction entry;
- replace "to keep nesting shallow" with the limit's name;
- pin the nesting depth the parser supports with a corpus case (finding 5).
```

```
[SIMPLIFY] tools/compiler/source_parser/Statements.chibi:211-224 — the ParseIf end fixup allocates an array and walks the chain a second time, and its comment misstates the state it repairs
Cost: read: 14 lines, and a comment that sends a debugging reader the wrong way. Run: an array allocation for every if statement, though few have a chain.
Evidence:
- Add sets `parent.end = child.end` (Tree.chibi:75). Line 208 adds `chained` while it is still empty, and NewNode sets `end: -1` (Tree.chibi:50).
- So until the fixup, every link ends at -1. The comment says each link "still ends where its own last child did", which is wrong.
- Every link except the last has the next link as its last child, so after the fixup all links end at n^[current].end.
- Only 212 of about 6,100 if statements in the corpus have an else-if. The rest still build `[first]` and make one string comparison on `kind`.
Fix: drop the array and the string comparisons:
    // Links were added before their children, so each still ends at -1.
    link := first;
    while (link != current) {
        n^[link].end = n^[current].end;
        link = n^[link].last;
    }
```

```
[SIMPLIFY] tools/compiler/source_parser/Expressions.chibi:390-397 — ParseArguments copies ParseIndexValue's range parse, and ParseIndexValue's comment still mentions arguments
Cost: change. The half-open range rule now has two copies (222-232 and 390-397), and a change to range syntax must land in both. Read: line 221 says "An index or an argument that may be a half-open range", but the function's only caller is the index at line 187.
Evidence: lines 390-397 repeat the logic of lines 223-231, with the guard inverted and `first` renamed to `value`. `git grep ParseIndexValue` finds one call site (Expressions.chibi:187).
Fix: call ParseIndexValue at line 390. That costs one frame, which finding 1 removes the reason for. While the depth limit forces the copy, drop "or an argument" from line 221 and add a comment at line 390 saying why the copy exists.
```

```
[TEST] tools/compiler/source_parser/Tree.chibi:118 — no test checks the new opener clause, the commit's only user-visible change
Cost: debug. A wrong position or a broken scan would ship without any test failing.
Evidence:
- SourceParserDiff.chibi:11-15 returns "" whenever both parsers reject the text, without reading the message.
- `git grep "to close the"` finds only Tree.chibi.
- test/language/syntax_corpus/invalid2.chibi.txt is exactly this case (`x := (1 + 2`, then `return x`), but nothing compares its message.
Fix: add an authored @test beside the parser (the way tools/compiler/cost_explanations/ExplainTests.chibi does) that asserts:
- Diagnose on the invalid2 text returns "Expected `)`, found `return` (to close the `(` at 4:10)";
- `f([a, b)` names the inner `[`;
- a missing `;` gets no clause.
```

```
[TEST] test/language/syntax_corpus — only the current shape of Body.chibi exercises the depth fix
Cost: change. If EmitBody is split (language-friction.md suggests it) or someone shortens the 36-link chain, a reverted ParseIf or a restored wrapper passes the differential again without anyone noticing.
Evidence: The longest chains are Body.chibi:2728 (36 links), then 17, 17 and 15. The whole syntax corpus has one else-if. Nothing states or tests how deep the parser must go.
Fix: add syntax_corpus/nesting.chibi.txt with:
- an else-if chain of about 100 links, which fails at the parent at one ParseIf frame per link;
- calls nested to a depth the parser promises.
Name that depth in docs/source-parser.md.
```

```
[NIT] commit 2797053cb message — it doesn't mention the three test-infrastructure fixes
Cost: debug. Bisecting the tokenizer selector or the Targets registration lands on a commit titled after else-if parsing, and the reason for "512 KiB" is never recorded.
Evidence:
- At the parent, Steps.chibi is 313,206 bytes, over the 262,144-byte cap, so source_tokenizer_chibi.c:453-455 failed that file by name. The new cap leaves 1.6× headroom.
- At the parent, main.c:757 calls test_language_source_parser_chibi, but Targets.chibi doesn't list the file. `make build` on macOS and Linux uses that build graph (Makefile:41).
- At the parent, Diagnostics.chibi already imports "core:strings", which the fixture didn't provide.
- So the differential could not pass at the parent, and this commit is what makes it run.
Fix: name the three fixes and their reasons in the message, or put them in their own commit.
```

```
[NIT] docs/source-parser.md:19-20 — the example message is out of date
Cost: read. The doc shows a message the parser no longer produces.
Evidence: the doc shows "Expected `)`, found `return`", which is invalid2's error. After Tree.chibi:118, that message ends with " (to close the `(` at 4:10)".
Fix: update the example and mention the clause.
```

```
[NIT] tools/compiler/source_parser/Expressions.chibi:213-214 — the comment about postfix forms sits above IsCallTarget
Cost: read. ParseUnary's postfix loop (174-203) has no explanation, and IsCallTarget's own contract (which node kinds can be called) has no comment.
Fix: move the two lines above `left := ParsePrimary(c, n);` at line 174.
```

```
[NIT] tools/compiler/source_parser/Expressions.chibi:383 — `following > 0` is always true, and the doc of SkipArgumentName (408-409) promises a 0 it never returns
Cost: read. A reader looks for a "not a name" case that doesn't exist.
Evidence: SkipArgumentName (410-416) starts with `at += 1` from c.pos ≥ 0 and only ever adds, so it returns at least 1. This commit moved the check into ParseArguments.
Fix: drop `following > 0` and the sentence "0 when it is not one". The Colon test is what decides.
```

```
[NIT] test/language/source_parser_chibi.c:29-48 — PARSER_FILES and PARSER_PATHS are parallel arrays, and PARSER_COUNT is computed from only one of them
Cost: change. This commit had to edit both. If an entry is ever missing from PARSER_PATHS, line 71 reads past its end.
Fix: one array of {file, path} pairs, or `_Static_assert(sizeof PARSER_PATHS == sizeof PARSER_FILES, "one path per file")`.
```

## What I would delete

- **`Statements.chibi:213-224`:** the `chain` array and the second walk. The four-line loop in finding 2 replaces them.
- **`Expressions.chibi:390-397`:** the copied range parse, once the depth limit no longer forces it, and "or an argument" at line 221.
- **`Expressions.chibi:383` and the end of line 409:** the dead `following > 0` check and the "0 when it is not one" contract.
- **`test/language/CMakeLists.txt:284-287` at this commit:** a second registration of `source_parser_chibi.c` alongside `Targets.chibi`. The file is already gone at the checkout's HEAD (`cd428b92e`).
- **Tree-sitter's parser, once `compiler_syntax` switches to this one:** `src/language/src/parser.c`, its scanner, `grammar.js`, `grammar.json` and `node-types.json`. `docs/source-parser.md` plans this. This commit doesn't make it reachable yet.

## What I would measure

- **Actual call depth.** Record the highest call depth reached for each corpus file, at the parent and at this commit; the fixture context already tracks `call_depth`. This replaces my hand counts (about 70 → 48 at `Body.chibi:3505`) and shows how much headroom finding 1 really leaves.
- **Cost of a deeper limit.** Find out what raising `SCRIPT_CALL_DEPTH_LIMIT` costs on each backend (memory per level) and what the 64 guards against beyond runaway recursion. That decides between raising the limit and keeping the flattening.
- **`Opener` cost after a failure.** `Fail` keeps only the first message (`Tree.chibi:92`). Yet every closing `Expect` that runs while the parse unwinds builds a message with a backward scan from EOF.
  - My estimate for an early error in `Steps.chibi`: about 10 such `Expect` calls × about 62k tokens ≈ 0.6M token visits. That is below the cost of one full parse, so I didn't raise it.
  - If the editor ever reparses invalid text on every keystroke, measure it. `if (c.failed) { return -1; }` at the top of `Expect` would remove the cost.
- **`ParseIf` fixup cost.** Measure the VM steps the fixup spends across the corpus (about 6,100 arrays). I expect a small number; finding 2 stands on simplicity alone.
- **Interior rows per file.** Unknown. Needed before any representation work on `Node` (which has 16 fields per row).

## What I would leave alone

- **`Opener`'s backward scan.** The tokenizer already tracks bracket depth (`Tokenizer.chibi:326-341`), but carrying a matching-opener index would add a field to every token row just for an error path that runs once per failed parse.
- **Formatter reflows in `main.c` and `source_parser_chibi.c`.** clang-format decides these.
- **The `fprintf` before `REQUIRE(called)`** (`source_parser_chibi.c:96-101`). It names the file, which the fixture's own failure line (`compiler_fixture.c:583`) doesn't.
- **The path `/sdk/core/strings/Strings.chibi`.** Other compiler inputs use the same SDK path form (for example `docs/evidence/m2-stagezero/self-reproduction-25cad/request.json`).
- **The three `Targets.chibi` lists naming this test** (`Files`, `LanguageTestsOverSource`, `SourceDefines`). They record three different facts: sources, private `src/` includes, and the source-root define. Merging them would mean redesigning the build graph.
- **`ParseUnary` reusing `token`** for the postfix loop. Nothing after the prefix branches reads the first value.
- **`Node.row`/`column` stored on every row.** These could be derived, but they predate this commit and the differential compares them directly.

## Appendix: depth count at Body.chibi:3505

I counted one frame per Chibi call, starting at `Differ`.

- **Same at both commits:**
  - Frames 1-12: `Differ`, `Diagnose`, `Build`, `Construct`, `ParseSourceFile`, `ParseDeclaration`, `ParseNamed`, `ParseNamedValue`, `ParseFunctionRest`, `ParseSignature`, `ParseCallableBody`, `ParseStatement`. That reaches the `while (work_top…)` statement.
  - Frames 13-28 cover the enclosing statements, from `while` to `return`. Each `while` adds `ParseBlock` + `ParseStatement`, each `if` adds `ParseIf` + `ParseBlock` + `ParseStatement`, the `switch` arm adds `ParseSwitch` + `ParseStatement`, and frame 28 is `ParseReturn`.
- **After this commit:** each of the five nested calls adds `ParseBinary` + `ParseUnary` + `ParseArguments` (frames 29-43). Then `item.node` adds `ParseBinary`, `ParseUnary`, `TakeMember`, `Take` and `Add`, for about 48 frames.
- **At the parent:** each call level instead adds seven frames: `ParseExpression`, `ParseBinary`, `ParseUnary`, `ParsePostfix`, `ParseArguments`, `ParseArgument`, `ParseIndexValue`. That puts `ParseExpression` for `Field`'s argument at frame 64 and the leaf at about 70.

The ±2 margin covers whether the `switch` arm has braces and how the VM counts the root frame.
