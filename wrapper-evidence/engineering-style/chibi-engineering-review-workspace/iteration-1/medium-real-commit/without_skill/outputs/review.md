# Review of 2797053cb

**Commit:** `2797053cbd4b599cce673f6b016870068b09382d` (parent `bb45c3db7`)
**Title:** "Parse else-if chains iteratively, shorten nesting and name the opening bracket in errors"
**Author / date:** Charles Waldner, 2026-10-29
**Checkout:** `/Users/cwaldner/Personal/chibi/.worktrees/review-eval/chibi` (HEAD `cd428b92e`, which descends from this commit)
**Scope:** 8 files, +174 / -117

## Verdict

The parser changes are behavior-preserving as far as I can tell from reading them. The new else-if loop builds the same rows, links and fields as the recursive version, and the range fixup is correct. `Opener` pairs brackets correctly for a parser that stops at the first error. Nothing here needs a revert.

The problems are in approach and verification, not in correctness:

1. Most of the diff hand-inlines code to fit under the runtime's 64-call depth limit. The project already lists that limit as friction. The workaround makes the code worse to read, leaves nothing in the code explaining why, and only adds some headroom; it doesn't remove the limit.
2. None of the three behavior changes has a regression test. The differential that would catch a mistake isn't in any CI group, and the commit doesn't say it was run.
3. The commit message describes about half the change. The other half quietly fixes the test harness so the parser and tokenizer differentials can build and pass, and the message never says why any of it was needed.

I reviewed by reading only. I did not build or run anything: the checkout has no `build/`, and the task was read-only.

## What the commit actually does

| Area | Change | In the message? |
|---|---|---|
| `Statements.chibi` `ParseIf` | Recursive else-if becomes a loop, followed by a pass that repairs `end` offsets | Yes |
| `Expressions.chibi` | Deletes `ParseExpression` (49 call sites become `ParseBinary(c, n, OrPower)`), folds `ParsePostfix` into `ParseUnary`, folds `ParseArgument` and the argument use of `ParseIndexValue` into `ParseArguments` | "shorten nesting" |
| `Declarations.chibi`, `Statements.chibi` | The mechanical `ParseExpression` replacement | "shorten nesting" |
| `Tree.chibi` | `Expect` adds " (to close the `(` at R:C)" via the new `Opener` | Yes |
| `test/language/source_parser_chibi.c` | Adds `core:strings` to the package set, prints the file label when a call fails, clang-format reflow | No |
| `tools/graph/engine/Targets.chibi` | Registers `source_parser_chibi.c` in the Chibi build graph (`Files`, `LanguageTestsOverSource`, `SourceDefines`) | No |
| `test/language/source_tokenizer_chibi.c` | Raises `MAX_SOURCE` from 256 KiB to 512 KiB | No |
| `test/language/main.c` | Reformatting only | No |

The unmentioned changes look like the real reason for the commit:

- At the parent, `syntax_diagnostics/Diagnostics.chibi:3` imports `core:strings`. The fixture adds only native packs (`compiler_fixture.c:170-172`), so the parser differential could not resolve that import.
- `source_parser_chibi.c` was registered in CMake (`test/language/CMakeLists.txt:284`) but not in `Targets.chibi`.
- `tools/compiler/package_compiler/Steps.chibi` is 313,206 bytes at the parent, over the old 262,144-byte cap. The tokenizer test rejects over-cap files by name (`source_tokenizer_chibi.c:452-457`), so that selector was red too.

These fixes are correct.

## Findings

### 1. The depth reductions work around the 64-call limit in authored code instead of addressing it (design, should follow up)

`SCRIPT_CALL_DEPTH_LIMIT` is 64 (`src/language/semantic.h:12`). The language docs state it (`docs/code-language.md:234`), and `docs/language-friction.md:642` ("Calls nest at most 64 deep") records it as friction, with prior art and a suggestion: a deeper budget counted in frame bytes, or guaranteed tail calls. The commit message says "shorten nesting", and the new comment at `Expressions.chibi:213-214` says "to keep nesting shallow". Together they point to this limit as the reason for removing `ParseExpression`, `ParsePostfix` and `ParseArgument`. On the call-argument path, `ParseUnary -> ParsePostfix -> ParseArguments -> ParseArgument -> ParseIndexValue -> ParseExpression -> ParseBinary -> ParseUnary` shrinks to `ParseUnary -> ParseArguments -> ParseBinary -> ParseUnary`.

Concerns:

- **The friction isn't recorded.** The repository rule (CLAUDE.md, "Language design ... `docs/language-friction.md`") and the existing entry's own instructions say to add new evidence to the matching entry. A recursive-descent parser that has to hand-inline wrappers to fit a 64-frame budget is strong evidence for that entry, and it isn't there.
- **Nothing tells the next editor why.** No comment says that `ParseExpression` must not come back. Someone tidying `ParseBinary(c, n, OrPower)` at 51 sites back into a wrapper would bring the failure back with no warning. Only the misplaced comment above `IsCallTarget` hints at it (see finding 5).
- **The headroom is unknown and finite.** Blocks still recurse (`ParseBlock -> ParseStatement -> ParseIf/While/For -> ParseBlock`), and so do unary operators, `??` chains, types and parentheses. The commit records no maximum depth measured over the corpus, so nobody knows how close the deepest file is to 64. The runtime also adds a per-instruction frame depth to the call depth (`src/execution/call.c:277-283`, `src/execution/machine.c:777-783`). Removing one source function therefore doesn't necessarily save exactly one unit, which is one more reason to measure rather than estimate.
- **It runs against the stated priorities.** CLAUDE.md says to add missing features to the language or compiler rather than work around them, and to keep functions small. The bytecode VM already keeps its call frames on the heap (`calls->frames`, `call.c:676`), so a deeper budget there is mostly a policy change.

Recommendation, which can come after merge: add this parser to the friction entry with the call chains above. Decide whether to raise the limit, or to count frame bytes as the entry suggests. Once the parser no longer needs it, restore `ParseExpression`, `ParsePostfix` and `ParseArgument`. Until then, add a short comment at the top of `Expressions.chibi`, plus a line in `docs/source-parser.md`, saying that the parser has to stay within the 64-call budget and that this is why expressions call `ParseBinary(c, n, OrPower)` directly.

### 2. No regression tests, and the only covering test doesn't run in CI (testing, should fix)

The repository's commit checklist asks for an authored `@test`, or a dispatcher case, for every behavior change that can be expressed, and says "A fix needs a case that failed before it." This commit adds none:

- **Else-if chain and nesting depth.** No input shows the depth failure before the commit and success after it. Suggested fix: add a corpus file under `test/language/syntax_corpus/` (for example `deep_nesting.chibi.txt`) with an else-if chain longer than 64 links and calls nested about 15 deep. The differential then also checks the repaired `end` offsets against Tree-sitter.
- **The `Opener` message.** No test checks message text. The differential only checks that the two parsers agree on whether text is rejected (`SourceParserDiff.chibi:11-16`), so the new wording and its row/column are completely untested. Suggested fix: an authored `@test` on `Diagnose` (authored tests already exist under `tools/compiler`, for example `tools/compiler/cost_explanations/ExplainTests.chibi`). Cover `F(1, 2` at end of file, a mismatch like `a[0)` that should name the `[`, and a missing `}` in a struct literal.
- **CI.** Neither `language-source-parser-chibi` nor `language-source-tokenizer-chibi` appears in any group in `tools/test_policy.py` (`GROUPS`, lines 580-622). Under "merge first, fix forward", CI on `main` is the safety net, and it never runs the one check that covers this parser. Suggested fix: add both selectors to the `language` group, or a cheaper sharded variant if the full-repository corpus on two backends costs too much. If neither happens, state in the PR body that the selector was run locally and passed. The commit gives no such record, so I couldn't confirm that the parser at this commit passes its own differential.

### 3. The `ParseIf` range fixup is more complex than it needs to be (minor)

`Statements.chibi:211-224` allocates an `int[]` for every `if` statement, including ones without `else`. It finds the links by matching the strings `kind == "if_statement"` and `field == "alternative"`, then walks backwards. But each non-final link's last child is the next link: `Add(n, current, chained, "alternative")` is the last `Add` on that link. So every link ends where the final link ends, and `current` is the final link when the loop exits. A forward walk is enough, with no array and no string tests:

```chibi
    // Each link was added to its parent before it had children, so every
    // link ends where the last one does.
    end := n^[current].end;
    link := first;
    while (link != current) {
        n^[link].end = end;
        link = n^[link].last;
    }
    return first;
```

This also holds when the parse fails partway, because nothing is added to a link after `current = chained`. Only `end` needs repair: `Add` copies `start`, `row` and `column` from the first child only (`Tree.chibi:64-77`), and that child is the `if` token, which is correct.

The current comment, "each link's range still ends where its own last child did before the next link was added", is hard to parse. The wording above says what is actually true.

### 4. `Opener`: rescans after a failure, and the wording is ambiguous (minor)

- **Wasted rescans.** `Fail` moves `c.pos` to `c.final` (`Tree.chibi:100`). Each enclosing frame then runs its trailing `Expect` for `)`, `]` or `}` as the stack unwinds. Each of those still builds `Expected(...)`, a step the code already wasted before this commit, and now also runs `Opener`. `Opener` scans backwards from the end of the file, which can mean every token row, once per unwinding `Expect`, and the result is thrown away because `Fail` keeps only the first message. The cost is bounded by the call depth, so it isn't a budget risk. But a follow-up (`760534556`) puts this parser inside the compiler, where text with errors is common while someone is typing. A guard at the top of `Expect` costs nothing and doesn't change what users see, since the tree is discarded after a failure:

  ```chibi
  if (c.failed) {
      return -1;
  }
  ```

- **Wording.** "Expected `)`, found `;` (to close the `(` at 4:10)" puts the note after "found `;`", so it can read as if the `;` closes the `(`. Moving the note into `wanted` reads better and is a smaller change: `Fail(c, n, Expected(c, n, format_text("{}{}", Describe(token), Opener(c, n, token))))`, with `Opener` returning " to close the `(` at 4:10". That gives "Expected `)` to close the `(` at 4:10, found `;`". Clang's "to match this '('" note is the nearest prior art.
- **Future recovery.** The backward token scan is right only because the parser stops at the first error. `docs/source-parser.md:15-16` says recovery is planned. When recovery skips tokens, the nearest unmatched opener will no longer be guaranteed to be the one being closed. Passing the opener's row from the caller (the caller usually just took it) would stay correct. This isn't needed now, but it's worth a comment on `Opener`.

What I checked and found fine: `KindNames()` spells the brackets `(`, `[`, `{` (`Tokenizer.chibi:175`). Brackets are always single-character tokens. Brackets inside strings and comments belong to those tokens and don't affect the count. The scan never leaves the token rows. The `row + 1`/`column + 1` byte positions match how `Diagnose` reports positions (`Parser.chibi:35`).

### 5. Duplicated range logic and comments that no longer match (minor)

- `ParseArguments` (`Expressions.chibi:376-405`) copies the half-open range construction from `ParseIndexValue` (`:222-232`) to save one frame. The two copies now have to stay in sync. The loop body is also four levels deep, which conflicts with the small-functions rule. If the depth budget really requires this, say so in a comment. Otherwise call `ParseIndexValue`.
- `ParseIndexValue`'s comment (`:221`) still says "An index or an argument that may be a half-open range"; arguments no longer use it.
- The comment that described `ParsePostfix` now sits above `IsCallTarget` (`:213-214`), which it doesn't describe. Move it to the postfix loop in `ParseUnary`, just above `left := ParsePrimary(c, n);` (`:174`).
- The `token` variable that `ParseUnary` declares at the top is now reused in the postfix loop (`:176`, `token = Cur(c, n);`). That's correct, because every earlier branch returns, but a fresh `postfix := Cur(c, n);` would be clearer.

### 6. The commit message leaves out half the change and every reason (process, minor)

The checklist says messages state "what changed and why". This one leaves out the build-graph registration, the `core:strings` fix, the tokenizer cap and its cause (`Steps.chibi` is about 306 KiB), the reformatting of two C files that failed clang-format at the parent, and the new failure label. It also never names the 64-call limit as the reason for the nesting changes. Given the merge-first policy, splitting the commit isn't worth it, but the message (or the PR body) should list these. Suggested form: "Make the parser and tokenizer differentials build and pass: register ..., add core:strings, raise the cap past Steps.chibi; keep the parser within the 64-call limit by ...; name the opening bracket in missing-bracket errors."

### 7. Stale workload comment in the tokenizer test (nit)

`source_tokenizer_chibi.c:27`: the commit edited this line but kept "about 150 files, under 1 MiB". The corpus is now 234 `.chibi` files and 2,888,323 bytes (`git ls-tree` at this commit). Update both figures, or drop them in favour of the per-file cap.

### 8. Docs don't match the new behavior (nit)

`docs/source-parser.md:20-21` gives ``Expected `)`, found `return` `` as an example diagnostic, but missing-bracket messages now carry the opener note. The "How it works" section should also mention the 64-call constraint from finding 1.

### 9. Small formatting points (nits)

- `test/language/main.c`: the usage string is split by hand so that `"--package-paths-chibi | "` sits alone on a line. clang-format doesn't reflow string literals, so this is pure churn.
- `Targets.chibi:755`: the new entry sits alone in a list that otherwise has two entries per line.

## What I verified and found correct

- **Else-if equivalence.** I traced `if a {A} else if b {B} else {C}` and a three-link chain. Rows are created in the same order as before: `New` for the chained `if` still follows `Take(else)`. The `first`/`next`/`parent` links and the `alternative` fields match the recursive version. After the fixup, every link's `end` equals the final block's end, and the caller's `Add` copies that value because the fixup runs before `return first`. The differential compares rows by index, so the unchanged creation order matters.
- **`ParseArguments` matches the old `ParseArgument` + `ParseIndexValue` path,** including the `following > 0` / `named >= 0` sentinel.
- **The `ParseUnary` postfix loop is the old `ParsePostfix` body unchanged.**
- **Nothing still calls `ParseExpression`, `ParsePostfix` or `ParseArgument`** anywhere in the tree (`git grep` at this commit). `ParseIndexValue` is still used for indexes.
- **The C files pass clang-format 22.1.8** with the repository's `.clang-format`. At the parent, `main.c` and `source_parser_chibi.c` did not, which explains the reflow.
- **`"/sdk/core/strings/Strings.chibi"`** follows the convention used by other language tests (`asset_packaging_chibi.c`, `callable_headers_chibi.c`, `compiler_artifact_chibi.c`).
- **The new "did not finish" line complements the fixture's own failure report.** The fixture already prints `failure=depth|budget` (`compiler_fixture.c:568-600`) but doesn't name the corpus file.
- **The `Targets.chibi` registration matches CMake's,** including the `CHIBI_SOURCE_ROOT` define (`CMakeLists.txt:284-286`).

## Not verified

- I didn't build or run anything, so I haven't confirmed that `language-source-parser-chibi` and `language-source-tokenizer-chibi` pass at this commit, or how much depth headroom the deepest repository file has.
- I didn't check whether the Chibi sources at this commit pass `chibi_ctl --format`.
