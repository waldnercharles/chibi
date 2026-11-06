# Review: `tools/compiler/source_tokenizer/Tokenizer.chibi`

Architecture and performance review of the Chibi source tokenizer.

- Checkout: `/Users/cwaldner/Personal/chibi/.worktrees/review-eval/chibi`, commit `cd428b92e`, clean tree. The file was last changed in `48f68bcd8` (2026-10-29).
- Scope: the tokenizer file. Its one consumer (`tools/compiler/source_parser`), its test harness (`test/language/source_tokenizer_chibi.c`), its reference sources (Odin's `core:odin/tokenizer` and `core:odin/parser` at the Homebrew `odin 2026-09` install, and `src/language/grammar.js` with `src/language/src/scanner.c`) and the runtime paths the hot loop reaches were read only for context.
- Method: I read everything listed above. No build exists in the checkout, and the review had to stay read-only, so nothing was compiled or timed. To get counts for the performance section, I ported the tokenizer line by line to Python in `/tmp/tokreview`, outside the checkout. The port reproduces the harness's hand-written token expectations, and I ran it with call counters over the same corpus the differential test uses: every `.chibi` file under `packages/`, `test/` and `tools/`, which is 238 files, 3,221,141 bytes, 72,259 lines and 591,780 tokens. **These are call and byte-visit counts, not timings.**

## Summary

The port works and is easy to follow. It follows Odin closely, it checks itself against Tree-sitter on the whole repository, and it already applies the main lesson of the earlier profiling: read class tables inline instead of making calls. The four issues that matter most:

1. **A correctness bug in character literals.** `'\''` is valid Chibi. Stage zero accepts it. The tokenizer reports two errors for it and splits it into two tokens. The source parser rejects any text with a lexical error, so it rejects valid source. No test covers character literals at all.
2. **The kind table drifts, and the test can't see it.** The kinds are numbered constants plus a hand-ordered name list, and three code sites hard-code the keyword range. This has already broken once (#698 → `48f68bcd8`). The grammar keyword `view` (#694) is still missing. The differential test can't catch a missing keyword, because it builds its expected keyword set from the tokenizer's own `KindNames`.
3. **Two places decide where statements end.** About 110 lines of the hot loop — the bracket-context stack, six look-behind flags, `BraceHolds` and `ValueFollows` — imitate Tree-sitter's parser-state-dependent terminator from inside the tokenizer. The new recursive-descent parser already corrects for those guesses (`SkipSpuriousEnd`, the label terminator). Odin's split is the better model: the tokenizer inserts a semicolon at a newline after an ending token, and the parser drops it by expression level.
4. **The hot path still makes about 5.2 million calls on the corpus**, about 1.6 calls per source byte: roughly 1.7 M authored calls, 2.96 M `append` host calls and roughly 0.55 M `length` natives. Local, behavior-preserving changes remove about 1.7 M of them. Dropping the row and column columns removes another 1.1 M. Section 3 has the itemized estimates.

## Findings at a glance

| # | Area | Finding | Severity | Effort |
|---|------|---------|----------|--------|
| C1 | Correctness | `'\''` gets an "unknown escape sequence" error and is split into two `Char` tokens; stage zero accepts it | High | Small |
| C2 | Correctness / tests | `view` keyword missing; the harness takes its expected keyword set from the code under test | Medium | Small |
| C3 | Correctness | An unclosed `(`/`[`, or a `:: prototype` without a body, changes terminator placement for the rest of the file | Medium (editor and LSP use) | Covered by A1 |
| C4 | Code | Unreachable duplicate `..<` branch (lines 274–276) | Low | Trivial |
| C5 | Code | The loop-local `ends` (line 347) hides the `ends` column (line 196), which the same block appends to at line 304 | Low (hazard) | Trivial |
| C6 | Correctness | `Illegal` accepts overlong UTF-8, surrogates and code points past U+10FFFF; a leading BOM is reported as an error, where Odin skips it | Low | Small |
| A1 | Architecture | Terminator inference lives in the tokenizer and imitates parser state; the parser compensates | High (structural) | Medium |
| A2 | Architecture | Kind constants, `KindNames` and three hard-coded keyword-range sites must change in step | Medium | Small (sentinels + assert); language (enum) |
| A3 | Architecture | `Lexeme` and the `partial`/resume loop exist only so that a function can return one error | Medium | Small–Medium |
| A4 | Architecture | Output shape: a one-variant union stands in for a struct, rows and columns are stored per token, and the parser copies everything into `Node` rows | Medium | Medium |
| P1–P9 | Performance | See section 3 | — | — |
| T1–T4 | Tests | No character-literal cases, circular keyword oracle, stale workload comment, no throughput signal | Medium | Small |
| D1 | Docs | Kind counts, keyword set, character literals, `??` and message sources have drifted | Low | Small |

---

## 1. Correctness

### C1. `'\''` is rejected (High)

`ScanString` handles both quote kinds (lines 676–702), but `ScanEscape` (lines 706–731) accepts only JSON's escapes: `" \ / b f n r t u`. Stage zero decodes a character literal "with \' for its own quote, as Odin's rune literals take the string escapes" (`src/language/semantic_context.c:146-165`). Odin's `scan_escape` accepts `\'` and `\"` in every literal (`core/odin/tokenizer/tokenizer.odin:366`). The grammar accepts any escape at the token level (`grammar.js:757`).

Trace for `'\''`, bytes 0–3:

- `ScanEscape` at offset 2 sees `'`, fails with "unknown escape sequence", and leaves the `'` unread (`partial`).
- The resumed scan reads that `'` as the closing quote, so the `Char` token is bytes 0–3, `'\'`.
- The last `'` starts a new `Char` that runs to the end of the line and fails with "Character literal not terminated".

So one valid token becomes two tokens and two errors. `source_parser.Build` returns `Rejected` on the first lexical error (`tools/compiler/source_parser/Parser.chibi:49-58`).

Fix: pass the quote, or the kind, to `ScanEscape` and accept `Apostrophe` when the literal is a `Char`. Strings should keep rejecting `\'`, as stage zero's JSON decoding does. Add `'\''`, `'\n'`, `'"'`, `'é'` and an unterminated `'a` to `CASES` and `SOURCES`. Today the harness has no character literal anywhere, and the repository corpus has none outside strings and comments, so `48f68bcd8` added the `Char` kind without coverage.

### C2. `view` is a grammar keyword the tokenizer lacks, and the oracle can't notice (Medium)

`grammar.js:62` lists `view` (added by #694 on 2026-10-28) and makes it contextual (`grammar.js:67`). The tokenizer has no `View` kind, so `view` scans as `Ident`. The parser then matches it by spelling, `IsWord(c, n, "view")` (`tools/compiler/source_parser/Types.chibi:59`). Because `view` is contextual, the observable difference today is only the token kind. That may even be the behavior you want, but `docs/source-tokenizer.md` says "The keywords are those of `src/language/grammar.js`", which is no longer true.

The test can't catch this kind of drift. `word_kind` (`test/language/source_tokenizer_chibi.c:122-135`) answers "which keyword is this word?" from the tokenizer's own `KindNames`, and `leaf_kind` sends every word-typed Tree-sitter leaf through it (lines 162–163). The test can't flag a keyword missing from the tokenizer, and it can't flag a word added to the tokenizer by mistake either. Only a change in terminator placement would show.

Fix, in increasing strength:

- When the Tree-sitter leaf type is a keyword rather than `identifier`, expect `kind_named(type)` and fail if the kind is absent. Use `word_kind` only for `identifier` leaves, where a contextual keyword may legitimately appear.
- In `prepare()`, list Tree-sitter's anonymous word symbols (`ts_language_symbol_count` and `ts_language_symbol_name`). Require each one to be in `KindNames` or on an explicit allowlist of words that stay identifiers (`self`, `min`, `max` and, if you decide so, `view`).

Then decide `view` deliberately. Either add `View` (and add it to `IsName` beside the other contextual words), or document it with `self`/`min`/`max`.

### C3. Bracket state makes a local error affect the rest of the file (Medium for editor use)

Terminators are inserted only while the top context is `Statements` or `Fields` (line 239). An unclosed `(` or `[` leaves a `Group` context open until the end of the file, so no terminator is inserted after it. `prototyping` (line 363) stays set until the next `{` anywhere in the file. If the prototype has no body yet, as while someone is still typing it, the next function body becomes `Fields` and its inner braces become `Embedded`, with no terminators inside them. Valid files are unaffected, but the tokenizer is meant to serve a future language server, and there the edited text is invalid most of the time. A1 removes this class of problem. Until then, consider resetting `prototyping` at a terminator.

### C4. Dead duplicate branch (Low)

Lines 268–270 and 274–276 test the same thing, `kind == Ellipsis` followed by `<`, once through `ByteAt` and once inline. The second branch can't be reached. Delete it and keep the inline form, since the comment on lines 224–225 says calls cost more than the scanning.

### C5. Hidden `ends` (Low, hazard)

`ends := endings[kind] || ...` (line 347) declares a `bool` in the loop body that hides the `ends: int[]` column (line 196). Earlier in the same block, line 304 appends to the column. `docs/package-compiler.md:192-196` says stage zero "declares a local for its whole scope" while stage one reads names in order. This is exactly the shape where the two compilers' name resolution can disagree. Stage zero evidently resolves line 304 to the column today, or every call would fail, but it is one compiler change away from a confusing error. Rename the boolean, for example to `ending`.

### C6. UTF-8 and BOM details (Low)

`Illegal` (lines 915–943) checks only the lead byte's range and the continuation bytes' range. Overlong three- and four-byte forms (`E0 80..9F`, `F0 80..8F`), surrogates (`ED A0..BF`) and `F4 90..` therefore produce "illegal character '…': N" with a meaningless N, where Odin's `utf8.decode_rune` would report "illegal UTF-8 encoding". The outcome (an error) is the same; only the message differs.

Odin also skips a leading U+FEFF (`tokenizer.odin:120-122`). The Chibi tokenizer reports it as an illegal character, so a file saved with a BOM is rejected. Check what Tree-sitter's `/\s/` extras do with U+FEFF before changing this; the differential will show it.

---

## 2. Architecture

### What the file is today

- A batch tokenizer: `Tokenize(text) -> Tokens.Scanned(...)` returns five token columns (`kinds`, `starts`, `ends`, `rows`, `columns`) and four error columns.
- Its consumers are `source_parser`, which nothing calls yet (`docs/source-parser.md`), and the differential test. The parser is meant to replace the generated `src/language/src/parser.c` and `scanner.c` in stage one, so these 943 lines are on the path off C.
- Batch, columnar output matches Zig: `std.zig.Ast.parse` tokenizes everything first, into a `MultiArrayList` of tag and start. It suits Chibi, where calls are expensive. Odin's pull model (`scan(t) -> Token`) would cost one call per token. **Keep the batch shape.**

| Aspect | Odin `core:odin/tokenizer` | This port | Comment |
|---|---|---|---|
| Driving | Pull, one `scan` per token | Batch columns | Good for Chibi's call costs |
| Semicolon insertion | `insert_semicolon` after ending kinds; the `\n` itself becomes the `Semicolon` | Same idea, plus a bracket-context stack and six flags imitating Tree-sitter's `valid_symbols` | See A1 |
| Dropping newline semicolons in expressions | Parser: `advance_token` skips `"\n"` semicolons when `expr_level > 0` (`parser.odin:365-385`); `skip_possible_newline_for_literal` for `{`/`else` on the next line (`parser.odin:277-295`) | Tokenizer, by guessing the context | See A1 |
| Errors | `error(t, offset, msg)` handler; scanning continues | `Lexeme` record with one message, plus the `partial` resume protocol | See A3 |
| Keyword table | FNV-1a 32 into 512 slots, `assert` no collision, one probe, `1 < len < 16` | Same hash and size, linear probing (Chibi has 3 colliding pairs: `library`/`on`, `events`/`or`, `query`/`split`) | Comment on line 149 is accurate |
| Escapes | `a b e f n t v r \ ' "`, octal, `x`, `u`, `U` | JSON's set | Right for strings; wrong for characters (C1) |
| Leading BOM | Skipped | Error | C6 |

### A1. Move the bracket context to the parser (High, structural)

The terminator Tree-sitter inserts depends on parser state: `scanner.c:36` returns early unless `valid[AUTOMATIC_TERMINATOR]`. The tokenizer can't see parser state, so it rebuilds it from look-behind:

- the `contexts` stack with six context kinds (lines 133–139, 325–344);
- `previous`, `before`, `label`, `annotating`, `valued`, `prototyping`, recomputed on every non-comment token with about 20 comparisons (lines 354–366);
- `BraceHolds` and `ValueFollows` (lines 496–531), which encode grammar facts: union variants, prototype fields and components, relationship bodies, typed constants, `return .Name` outcomes.

The parser then corrects for what the tokenizer can't know. `SkipSpuriousEnd` (`Expressions.chibi:99-108`) drops a terminator after a binary operator, and `Statements.chibi:145-147` takes one after a label. `docs/source-tokenizer.md` lists further cases "the parser has to handle". So one rule is now owned by two components. Each new grammar construct risks another flag, and the per-token flag updates are paid on every token.

Odin's division is the precedent, and the parser already contains its first instance:

- **Tokenizer keeps the lexical rules:** `insert` after ending kinds (`Endings`, the member-name rule, the label colon), the look-ahead over comments and continuation characters (`Terminates`, `Continues`, mirroring `scanner.c`), the `{` on a new line, and the placement before comments. It emits a zero-width `Semicolon` wherever the lexical rule says a line can end.
- **Parser drops the terminator where the grammar can't take one.** It keeps an expression level, as Odin's `p.expr_level` does: raised inside parentheses, brackets, struct literals and union variants; skips a hidden terminator when the level is above zero; and handles `Fields`/`Embedded` in `ParsePrototype`/`ParseRelationship`, which know exactly where they are. `SkipSpuriousEnd` becomes the general rule rather than a special case.

Gains:

- About 110 lines and every per-token flag update leave the hot loop.
- C3 goes away, because one unclosed bracket no longer changes the tokenization of the rest of the file.
- Grammar knowledge has one owner.

Cost: the tokenizer differential can no longer compare terminators directly with Tree-sitter's. Terminator placement stays observable in the parser differential, `SourceParserDiff.chibi`, because node ranges include their terminators (`docs/source-parser.md`, "How it works"). The tokenizer harness would compare non-terminator tokens and the lexical terminator candidates.

Sequencing: do it when the parser becomes the stage-one front end. Until then the tokenizer-level differential is the cheapest guard.

### A2. Make the kind table self-checking (Medium)

The kinds are 117 hand-numbered constants (lines 8–126), a hand-ordered `KindNames` (lines 171–185), and three hard-coded keyword-range sites: `KeywordTable` (`kind <= Emit`, line 569), the member-name rule (`kind >= Package && kind <= Emit`, line 347) and the parser's `IsMember` (`Tree.chibi:164-166`). The parser's `IsName` (`Tree.chibi:158-162`) and the tokenizer's `Endings` (lines 464–473) each copy part of grammar.js's `CONTEXTUAL` list. History shows what this costs:

- #698 removed `"ref"` from `KindNames` but kept `Ref: int : 102`. Every kind from `service` on had the wrong name until `48f68bcd8` ("fix the tokenizer's kind table").
- #701 removed three keywords and rewrote 58 constant lines to renumber them.
- #694 added `view` to the grammar, and it is still missing (C2).

Fixes, by cost:

1. Add Odin-style sentinels, `KeywordBegin`/`KeywordEnd` (Odin's `B_Keyword_Begin`/`B_Keyword_End`), and use them at all three sites, including the parser.
2. `assert(length(names) == KindCount, ...)` once in `Tokenize`. Stage one has the `assert` intrinsic (`docs/package-compiler.md`, "Supported language").
3. Move the tail kinds that are not keywords (`HalfOpenRange`, `Coalesce`, `Char`) to before the keyword block. Then "in the order of Odin's Token_Kind" holds again, and a new keyword is added only at the end of the keyword block.
4. The real fix is the language feature already proposed in `docs/language-friction.md` ("No enumerations"): an enum with value names, enumerated arrays and `bit_set`. That makes `KindNames` and `Endings` derived data. This file is the strongest evidence for that entry, and the entry's numbers are stale (D1).

### A3. Replace the `Lexeme`/`partial` protocol with an error sink (Medium)

`Lexeme` (lines 163–169), `Done`/`Failed` (lines 533–539), the `partial` flag and the resume loop (lines 286–299) exist because a scanner can return only one error. Odin's scanners call `error(...)` and keep going. Here, a string with three bad escapes takes four `ScanString` calls. A `Lexeme` also holds a `string`, so the runtime can never pack it (`docs/package-compiler.md`, "Records and arrays"). The corpus builds 43,238 of these records, one per number, string and comment, almost all with `message: ""`.

The sibling parser already passes `^Cursor` and `^(Node[])`, so pointer parameters are in the front end's language. Have the scanners take `messages: ^(string[])` and `offsets: ^(int[])`, append as Odin's `error` does, and return only `end`. `ScanNumber` also needs to return its kind, which it can encode in the end offset or take through a pointer. `Tokenize` fills the error row and column from its own `row`/`line` for each newly appended error. That removes `Lexeme`, `Done`, `Failed`, `partial` and the resume loop, and turns `ScanEscape` into a function returning an `int`.

### A4. Output shape (Medium)

- **Single-variant union.** `Tokens` is a union with one variant because stage one's records hold no arrays (`docs/package-compiler.md:215`). The cost is a `switch` in every consumer, and union construction copies every payload array (`docs/language-friction.md`, "Calls and record snapshots are costly"), which is about 3 M element copies on the corpus. This gap is not recorded in `docs/language-friction.md`. Record it there, with Odin, Zig and Go structs holding slices as prior art, so stage one gains record fields that hold arrays and `Tokens` becomes a struct.
- **Rows and columns per token.** They can be derived. Go keeps offsets plus a line table per file (`token.File`). Zig keeps tag and start and computes locations on demand (`Ast.tokenLocation`). Emitting a `line_starts` column (72,259 appends on the corpus) instead of `rows` and `columns` (1,183,560 appends) removes 40% of the output and its copies. The parser walks tokens in order, so it can recover row and column with a cursor that only moves forward when it builds a leaf.
- **The parser copies every token** into a 16-field `Node` with three strings (`Parser.chibi:61-75`). It also calls `tk.KindNames()` again (`Parser.chibi:62`) and once per diagnostic (`Tree.chibi:265, 289`). That is the parser's business, but it means the columnar output buys nothing at its only consumer. If the parser kept token columns and referred to tokens by index, as Zig's AST does, both copies would go away.

---

## 3. Performance

### Where the calls go (corpus totals from the instrumented port)

| Site | Calls | Notes |
|---|---:|---|
| `kinds/starts/ends/rows/columns.append` | 2,958,900 | One host call each (`EXECUTION_array_push_call`) |
| `Pair` | 377,081 | 278,059 (74%) for bytes that can't start a two-byte operator |
| `Single` | 348,738 | 38,672 for digits and quotes that then go to `Scan` anyway |
| `ScanIdentifier` | 213,402 | One per word, each also calling `length(bytes)` |
| `Keyword` | 213,402 | 38,172 words (18%) don't start with a lowercase letter, so can't be keywords |
| `Terminates` | 117,142 | **116,024 (99.0%)** have no line break in the preceding spacing and a next byte other than `/`, `}` or end of text, so they must return false |
| `ByteAt` | 117,341+ | Mostly from `Terminates`; more in `ScanNumber` and `ScanEscape` |
| `Spells` | 65,351 | 39,122 (60%) compare against a different keyword in a probed slot |
| `Scan` + scanner + `Done`/`Failed` | 3 × 43,238 | Numbers 20,762, strings 17,910, comments 4,566 |
| `length(bytes)` (native) | ≈ 550,000 | Recomputed inside every helper |
| Comment row loop, byte iterations | 295,951 | 9.2% of all bytes; only 2 of the 4,566 comments are block comments |

The total is about 5.2 M calls for 3.22 MB. The `docs/language-friction.md` profile found calls and record handling dominated, so call counts are the right proxy here. Confirm each change with a timed run.

### P1. Skip `Terminates` when it can't return true (High value, a few lines)

Before calling `Terminates` (line 239), check `newline || start >= count || bytes[start] == Slash || bytes[start] == 125`. When that check is false, `Terminates` can't enter its comment loop, `c` isn't `/`, `}` or end of text, and it returns `broken && ...` with `broken == false`. So the guard preserves behavior exactly. It removes 116,024 `Terminates` calls, as many `ByteAt` calls and about 232 k `length` calls, roughly 464 k calls in all.

### P2. Byte-indexed dispatch tables instead of `Pair`/`Single` chains (High value)

`Single` is a chain of 25 `if`s, and `Pair` up to 20, called for every token that isn't a word (lines 754–911). `}` costs about 30 comparisons and two calls. A `switch` doesn't help: stage one compares one value per arm, in order (`docs/package-compiler.md`, "Supported language"). Build two more 256-entry tables with `Classes`:

- `singles[c]`: the one-byte kind, or `Invalid`;
- a `PairStart` class for `=!+-*/%&|^<>:.?`.

The loop becomes: `kind := singles[c]`, then call `Pair` only when `classes[c] == PairStart`. This removes all 348,738 `Single` calls and 278,059 of the 377,081 `Pair` calls, about 627 k calls. Quotes and digits fall straight through to `Scan`.

### P3. Inline the identifier loop and pass `count` (High value)

The comment on lines 224–225 says the hot paths read class tables inline, but the hottest path of all, identifiers, is a call (`ScanIdentifier`, lines 557–564). Inline it as the whitespace loop is: −213,402 calls, plus their `length` calls.

`length(bytes)` is a native in stage one (`docs/package-compiler.md` names it among the natives). It is recomputed in `ScanIdentifier`, `ScanMantissa`, `ScanString`, `ScanComment`, `Terminates` and `ByteAt`. Pass `count` down instead. Together this removes about 540 k calls.

### P4. Cheaper keyword lookup (Medium value)

- Add a class for lowercase letters, or test `bytes[start] >= 97`, before `Keyword`. Every keyword starts lowercase, which skips 18% of `Keyword` calls and 22% of the hashed bytes.
- Store each slot's 32-bit hash with its kind and compare hashes before `Spells`. Odin's entry already carries `hash`. This avoids the 39,122 `Spells` calls that compare against the wrong keyword.
- `Spells` (lines 610–622) makes one `text_byte_length` call plus one `text_byte_at` native per byte. For the roughly 26 k keyword hits, compare against an `int[]` of each keyword's bytes, decoded once when the table is built.
- Hash in `u32`, as `packages/core/hash/fnv/Fnv.chibi` does, so wrapping replaces the `& 0xFFFFFFFF` mask per byte (lines 574, 596). The longest keyword is 12 bytes (`relationship`), so a computed maximum (Odin computes `max_keyword_size`) instead of `< 16` skips 6,158 more words, a minor gain.

### P5. Count rows only in block comments (Low effort)

After every comment, lines 311–320 walk its bytes again to count rows. Line comments can't contain `\n`, because `ScanComment` stops before it. Guard the loop with `bytes[start + 1] == Star`. That removes 295,951 loop iterations on the corpus, each with an element read.

`Terminates` also reads ahead through comments (43,288 bytes), a third pass over those bytes. Removing it would mean holding back comment tokens until the terminator decision is made. Not worth the complexity now.

### P6. Per-call setup (Low–Medium)

Every `Tokenize` rebuilds `Classes` (256 iterations with `IsSpace`/`IsDigit` calls), `Endings`, `KindNames` (a 116-string literal) and `KeywordTable`. `KeywordTable` re-evaluates `text_byte_length(name)` in its `while` condition for each byte (line 573), on top of one `text_byte_at` per byte: about 800 native calls. That is roughly 1.2 k calls of setup, against about 6.7 k scanning calls for an average corpus file. For the short texts an editor would re-tokenize, setup dominates. Stage one rejects constant arrays (`docs/package-compiler.md`, "Anything else is rejected"), so the tables can't simply be constants. Hoist the length out of the loop now. Later, let the caller build the tables once and pass them in, in a union payload until records hold arrays.

### P7. Fewer output columns (High value, API change)

Appends are the largest single cost by count (2.96 M). Emitting `line_starts` instead of `rows`/`columns` (A4) removes about 1.11 M appends, net. Also measure a packed `Token[]` record array (a record of five scalars, which the runtime can pack, per `element_stride` in `src/types/array.c:210-230`). That would be one construct and one append per token instead of five appends, and one array to copy instead of five. I did not check whether the current columns, built with `[]` and `append`, are packed.

### P8. `text_bytes` makes one boxed value per source byte (Medium)

`text_bytes_call` (`src/runtime/text_format.c:481-495`) allocates with `pin_array_snapshot_allocate` and fills `items[i]`, so each source byte becomes a full `pin_value`. That is 48 bytes on 64-bit targets by the layout in `src/types/value.h:56-81`. The friction doc's "40-byte values" figure predates the `schema` field. The costs:

- Memory: `Steps.chibi` (330 KB) needs about 15.8 MB for `bytes` alone, and one corpus run allocates about 155 MB.
- Cache: every byte read goes through a 48-byte header, so a 64-byte cache line holds about 1.3 source bytes, against 64 in Odin, which reads `t.src[i]`.

`docs/language-friction.md` measured `strings.Bytes` at 0.07 s per MiB, so this costs memory and cache more than CPU time. Two fixes:

- In the runtime, allocate the array packed. Scalar arrays already support `stride > 0`. This also helps `core:strings.Bytes`, and `docs/package-foundations.md` already plans `Bytes` to return `byte[]`.
- Once string byte indexing is in both compilers, read `text[i]` directly and drop the copy. The runtime's `sequence_element` (`src/execution/intrinsics.c:517-531`) already serves `PIN_STR` receivers. Check the checkers before relying on it.

### P9. The union payload copy at return (Medium, runtime)

`Tokens.Scanned(kinds, ...)` copies all nine columns even though they are locals that die right after (`docs/language-friction.md`: "union constructors copying every payload array, even when the source is a temporary that is never read again"). That is about 3 M element copies on the corpus. `docs/value-sequences.md` already lists "Transfer: move a provably dead value into its destination". The tokenizer's return is a textbook case for it, and a good benchmark for it.

### Estimated effect

| Change | Calls removed (corpus) |
|---|---:|
| P1 guard | ≈ 464 k |
| P2 tables | ≈ 627 k |
| P3 inline + `count` | ≈ 540 k |
| P4 keyword | ≈ 120 k authored and native |
| P7 `line_starts` | ≈ 1.11 M appends |
| **Total** | **≈ 2.8 M of ≈ 5.2 M** |

P1–P4 change nothing outside this file and keep the token stream identical, so the existing differential checks them in full. P5, P8 and P9 reduce byte visits and copies, not calls.

---

## 4. Smaller code notes

- **Byte numbers.** `60`, `62`, `123`, `125`, `46`, `101`, `69`, `120`, `88`, `117` and the long lists in `Continues`, `Pair` and `Single` are bare byte values. Character literals exist now (this commit added the token), and stage zero compares a char with any 32- or 64-bit integer (`semantic_expression.c:653-661`). When stage one reads character literals, write `c == '{'`. That is the language-first fix the project prefers over more named byte constants. Check whether stage one supports them first.
- `Scan` takes `text` only to forward it to `Illegal` for `text_slice`. With P2, `Illegal` can be called directly from the `Invalid` path.
- `Terminates` calls `IsSpace(bytes[at])` per byte (line 432) instead of the `classes` table. It is cold now, but it is the only spacing scan that doesn't follow the file's own rule.
- `Spells` exists twice, here (lines 610–622) and in `source_parser/Tree.chibi:178-187`, with different signatures. The parser's copy re-evaluates `text_byte_length(word)` on every iteration.
- `KeywordTable` and `Keyword` implement FNV-1a 32 separately. If the hot copy stays inline, make the table builder call the same code, or assert at build time that both hash one name identically, so the two can't drift apart.

## 5. Tests (`test/language/source_tokenizer_chibi.c`)

- **T1.** Add character-literal cases to `SOURCES` (valid, compared against Tree-sitter) and `CASES` (errors): see C1.
- **T2.** Fix the keyword oracle: see C2.
- **T3.** The workload comment (lines 26–28) says "about 150 files, under 1 MiB". The corpus is now 238 files and 3.07 MiB, and `Steps.chibi` is at 64% of the 512 KiB `MAX_SOURCE` cap. Update the comment, and expect the cap to fail by name eventually (which it does, correctly).
- **T4.** The only performance guard is the 64-steps-per-byte budget. That catches superlinear behavior, not a 2× slowdown. Print the highest steps-per-byte seen, or bytes per second per backend, in the corpus summary line, so that P1–P9 show up in the routine output.

## 6. Documentation drift (D1)

- `docs/language-friction.md`, "No enumerations": "114 token kinds … `Relationship: int : 113`". The code now has 116 kinds plus `KindCount`, and `Relationship` is 109.
- `docs/source-tokenizer.md`:
  - It describes only double-quoted strings. It doesn't mention `Char`, character literals or `??`.
  - "the order of Odin's `Token_Kind`" no longer holds for the tail: `..<`, `??`, `Char`.
  - "The keywords are those of `grammar.js`": `view` is missing.
- Messages: `docs/source-tokenizer.md` says the errors follow "Odin's tokenizer and its messages". The pinned `core:odin/tokenizer` says "string literal was not terminated" and "rune literal not terminated"; this file says "String literal not terminated" and "Character literal not terminated". Name the actual source, or align the messages.

## 7. Suggested order

1. C1 with T1, then C4 and C5. These are small, independent, and test-backed.
2. C2 with T2, then A2 items 1–3: sentinels, the count assert, and the tail-kind reordering.
3. P1, P2, P3, P5, then P4. The token stream stays identical, so the existing differential checks them. Add T4 first, to see the effect.
4. A3, the error sink.
5. P7 and A4, `line_starts`, together with the parser change that reads them.
6. A1, when the parser becomes stage one's front end; it removes C3.
7. Runtime and language follow-ups that this file motivates: P8 (packed `text_bytes`, or `text[i]`), P9 (moving temporaries into union payloads), records that hold arrays, and enums (A2 item 4). Record the array-fields gap in `docs/language-friction.md`.

## What I did not verify

- No timings. Everything in section 3 is a count from a Python port over this commit's corpus. The port reproduces the harness's expected token kinds on the cases I checked, but it simplifies string escape and error paths.
- Whether `[]` + `append` columns are packed (P7), and the relative cost of an authored call, a host-call intrinsic and a native.
- Whether stage one compiles pointer parameters (A3) and character literals (section 4). The sibling parser already uses pointer parameters.
- What Tree-sitter does with a leading U+FEFF (C6).

Scratch: the Python port lived in `/tmp/tokreview`, outside the checkout, and has been deleted. The checkout was not modified (`git status` is clean at `cd428b92e`).
