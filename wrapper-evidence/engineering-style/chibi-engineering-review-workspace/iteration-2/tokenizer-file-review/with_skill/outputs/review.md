# Engineering review: `tools/compiler/source_tokenizer/Tokenizer.chibi`

**Scope.** The checkout is `/Users/cwaldner/Personal/chibi/.worktrees/review-eval/chibi`, branch `claude/review-eval-seeded-20261029` at `cd428b92e`. The file is identical to `origin/main`; its last change is `48f68bcd8` (2026-10-29). I read all 943 lines and the code that consumes them:

- `tools/compiler/source_parser/Parser.chibi`, `Tree.chibi`, and the terminator handling in `Expressions.chibi` and `Statements.chibi`
- the differential harness `test/language/source_tokenizer_chibi.c`
- `docs/source-tokenizer.md` and `docs/source-parser.md`
- the `text_bytes` native in `src/runtime/text_format.c`

No LSP server handles `.chibi` (cclsp returned nothing), so I found references with grep.

**How the numbers were collected.** This checkout and the workspace have no build, and the review was read-only, so nothing was timed. File, byte and line counts come from `find` and `wc`. Token counts come from a regex approximation of the tokenizer, run over the corpus the differential test reads. Keyword-table probe counts come from a re-run of the file's own FNV table. I measured `sizeof(pin_value)` by compiling `src/types/value.h` in a scratch program. All scratch files were deleted afterwards.

## Brief

- **Transform:** one source text goes in. Out come five aligned `int[]` token columns (`kinds`, `starts`, `ends`, `rows`, `columns`) ending in an EOF token, plus four error columns. The token columns include zero-width `Semicolon` terminators wherever Tree-sitter's external scanner would place them.
- **Counts:** the corpus is every `.chibi` file below `packages/`, `test/` and `tools/`, which `source_tokenizer_corpus` reads.
  - Files: 238 files, 3,221,141 bytes and 72,259 lines; the mean is 13.5 KB and about 2,500 tokens per file.
  - Worst case: `tools/compiler/package_compiler/Steps.chibi`, 330,011 bytes and about 66,000 tokens. The harness cap is 512 KiB per file.
  - Tokens: about 590,400, or 5.5 bytes per token.
    - 333,828 operators and punctuation. 305,000 of them are single bytes: `,` 63,443, `(` and `)` 43,349 each, `;` 29,544, `.` 26,286, `:` 22,018, `{` `}` `[` `]` about 14,800 each.
    - 213,359 words, of which 26,229 are keywords.
    - 20,747 numbers, 17,910 strings and characters, and 4,566 comments.
  - Bytes by class: 868,882 whitespace bytes (27%), 295,875 line-comment bytes (9.2%) and 76 block-comment bytes.
  - Terminators: the exact count is unknown; the harness prints it. Only about 2,100 lines end in a name, number or closing bracket without a `;`, against 29,544 written `;`, so there are a few thousand. 87% of the corpus is `tools/` code that writes its semicolons.
- **Consumers:**
  - `source_parser.Build` (`Parser.chibi:49-60`) reads only `errors[0]`.
  - `Construct` (`Parser.chibi:61-77`) copies every token into a 16-field `Node` record with a string kind.
  - The C harness reads the union payload columns by index (`source_tokenizer_chibi.c`, `column_at`).
  - Nothing in production calls the parser yet (`docs/source-parser.md`). The intended consumers are the compiler front end that replaces Tree-sitter and a future language server.
- **Layer:** cold today, since one test selector is the only caller. Hot by design: the compiler front end reads every compiled file, and a language server would read every edit. I reviewed it as a hot layer.
- **Minimum work:**
  - Bytes: one pass at about 3 operations per byte (a load, a class lookup and a compare), about 10M for the corpus.
  - Tokens: about 15 operations per token (a table dispatch, FNV hashing for a word, one terminator test) plus 5 appends. That is about 9M operations and 3M appends.
  - Data moved: the input is 3.2 MB read once, and the packed output is about 12 MB (590k tokens × 20 B).
  - Time: in native code, tens of milliseconds for the corpus and 1–2 ms for `Steps.chibi`. In the VM, a fraction of a second.
- **Actual work,** traced per token class:
  - **Before the first token:**
    - `text_bytes` puts each source byte in its own 48-byte `pin_value` (`text_format.c:489-494`). That is 15.8 MB for `Steps.chibi` and 155 MB over the corpus.
    - Tables are rebuilt on every call: `Classes` (256 iterations with `IsSpace`/`IsDigit` calls), `Endings`, `KindNames` (116 strings) and `KeywordTable` (59 FNV hashes through the `text_byte_length` and `text_byte_at` natives). That is about 1,500 calls, under 1% of a 13.5 KB file.
  - **Whitespace byte:** a class lookup, two reads of `bytes[start]` and about 4 compares.
  - **Single-byte punctuation (305k tokens):**
    - Kind dispatch costs about 30 compares and 2 calls. A `Pair` call takes 4–5 compares, the chain at lines 268-281 takes 4 more, and a `Single` call averages 20.8 compares because `,`, `.`, `;`, `:` and the braces are tests 21–26 of 26.
    - Context bookkeeping adds about 20 more compares, then come 5 appends.
  - **Word (213k tokens):**
    - A `ScanIdentifier` call, then a `Keyword` call that runs FNV hashing (xor, u64 widen, multiply, mask) over 1,223,505 bytes.
    - 65,402 probes land on an occupied slot and call `Spells`, which makes 183,837 native calls.
  - **Number, string or comment (43k tokens):** the full `Pair`/`Single` chain runs first and returns Invalid or `Quo`. Then `Scan` runs and returns a `Lexeme` record with a string field.
  - **After an ending token at statement level (about 122k times):** a `Terminates` call plus a `ByteAt` call. About 98% of them return false at the first byte, because no line break precedes it and it is not `/` or `}`.
  - **Line comment:** `ScanComment` scans it, and then the row loop scans it again for newlines it cannot contain.
  - **Overall:** the harness says calls "take under 20" steps per byte (`source_tokenizer_chibi.c:28-29`), so under 64M steps for the corpus.
- **Gap:**
  - **Overall:** about 3× in steps (under 64M against about 19M). This is an upper bound, not measured.
  - **Local gaps of 10× or more:**
    - punctuation dispatch: about 30 compares and 2 calls, against one table load;
    - input bytes moved: 48×;
    - the comment row loop: 296k bytes scanned for the 76 bytes that can hold a newline.

## Target shape

```chibi
// Built once per Tokenize from KindNames, so each spelling has one definition.
classes[256]   // Space | Letter | Digit | Scan (quote, apostrophe) | Punct
singles[256]   // kind of the one-byte token c, or Invalid      (26 entries)
assigns[256]   // kind of `c=`                                   (13 entries)
doubles[256]   // kind of `cc`                                   (7 entries)
arrows[256]    // kind of `c>`: `->`, `=>`                       (2 entries)
slots[512]     // keyword kind by FNV slot, as now
spelled: int[] // keyword bytes back to back, with offsets[kind] and lengths[kind]

loop over text[at] (or a packed u8[]):
    skip spaces, counting rows
    if insert && (newline || c == '}' || c == '/' || at == count): Terminates   // lexical rule only
    Letter -> one inline loop that scans and hashes; probe; compare bytes against `spelled`
    Punct  -> assigns/doubles/arrows chosen by the second byte, else singles[c]; keep `..<` and `>=>`
    Scan   -> Scan() as now
    append five columns
    insert = endings[kind] || dereference || (previous == Period && word) || label colon
```

`Tokens` becomes a plain record with named fields: `kinds`, `starts`, `ends`, `rows` and `columns` (`int[]`), `errors` (`string[]`), and `error_offsets`, `error_rows` and `error_columns` (`int[]`).

The parser gains Odin's expression level: a counter on `Cursor` that skips hidden terminators inside `()`, `[]`, struct literals and union variants.

What disappears:

- `Pair` and `Single`: 157 lines and about 60 byte codes.
- The `contexts` stack, its six constants, `BraceHolds`, `ValueFollows`, `valued`, `annotating`, `prototyping` and `before`: about 90 lines.
- The 48-byte-per-byte `text_bytes` copy.
- The per-byte native calls in `Spells`.
- The duplicate `..<` branch.

## Findings

### 1. [ARCH] Tokenizer.chibi:133-139, 203-221, 237-251, 322-366, 492-531: the tokenizer rebuilds the parser's context to place terminators

Tree-sitter's scanner inserts a terminator only where the parser's state accepts one. To match it, the tokenizer reconstructs that state. It keeps a stack of six brace kinds and five variables (`label`, `annotating`, `valued`, `prototyping`, `before`), which feed `BraceHolds` and `ValueFollows`. The parser it predicts now exists and already corrects the guesses. Odin, which this file says it ports, does none of this: its tokenizer decides `insert_semicolon` from the token kind alone, and its parser drops newline semicolons by expression level (as `docs/source-tokenizer.md` itself says).

**Cost:** change and run.

- **Change:** every brace-bodied form needs its own `BraceHolds` rule and its own fixed source. The forms so far: prototypes, relationships, unions, `return .Name {…}`, typed constants and labels.
- **Rules held twice:**
  - The parser patches the guesses: `SkipSpuriousEnd` at `Expressions.chibi:104`, called at `:89`.
  - Labels are decided twice by different methods. The tokenizer uses a previous-token heuristic (lines 360-362). The parser uses `SkipType` lookahead (`Statements.chibi:130-148`), then takes the terminator the tokenizer guessed (`:145`).
- **Run:** about 20 compares per token (lines 238-239, 325-344, 348, 355-363). That is about 12M over 590k tokens, spent to decide a few thousand terminators.

**Evidence:**

- 7 of the harness's 19 fixed sources exist to pin this logic: label, literals, outcomes, prototypes, relationships, fields and constants (`source_tokenizer_chibi.c:495-676`).
- `docs/source-tokenizer.md` lists two divergences from Tree-sitter that the parser has to absorb.
- Three of the six context values behave identically: `Group`, `Variants` and `Embedded`. The context is read in exactly five places: lines 239, 348 and 360, and `BraceHolds` lines 497 and 503. Each read tests only Statements, Fields or Literal.

**Fix:**

- Keep the lexical rule in the tokenizer: an ending token followed by a line break unless the next line continues (`Continues`), and a terminator before `}` and EOF. Keep the label colon too, as a plain previous-token test.
- Move the bracket decision to the parser as Odin does: a level on `Cursor` that skips hidden terminators inside brackets, literals and variants.
- This deletes `contexts`, `depth`, the six constants, `BraceHolds`, `ValueFollows`, `valued`, `annotating`, `prototyping` and `before` (about 90 lines), and the context fixed sources. The parser's tree differential remains the oracle for terminator placement.
- It is cheapest now, while nothing calls the parser.
- If this is rejected, at least merge `Group`, `Variants` and `Embedded` into one value.

### 2. [PERF] Tokenizer.chibi:263-282, 754-911: every non-word token walks two if-chains through two calls before its kind is known

**Cost:** run.

- **Punctuation:** each single-byte punctuation token (305k, 52% of all tokens) costs `Pair` (4–5 compares), the chain at lines 268-281 (4 compares) and `Single` (20.8 compares on average), about 30 compares and 2 calls in all.
- **Numbers, strings and comments (43k tokens):** each pays the same chain before `Scan`, falling through all 26 `Single` tests.
- **Total:** about 10M compares and 700k calls over the corpus. With tables the step takes about 4 operations, roughly 10× less. That is an estimated 15% of all steps.
- **The file's own rule:** "Calls cost more than the scanning, so the hot paths read class tables inline" (lines 224-225). It is applied to whitespace but not to the most frequent token class.

**Evidence:**

- The operator distribution in the brief.
- The test order in `Single`: `,` is test 26, `.` 25, `;` 24, `:` 23, `}` 22 and `{` 21.
- `Pair` reaches `c != next` only after three other tests.

**Fix:**

- Build `singles`, `assigns`, `doubles` and `arrows` once per call from the 1- and 2-byte spellings in `KindNames`. `KeywordTable` already does the same for keywords.
- Dispatch on the second byte: `next == '='` reads `assigns[c]`, `next == c` reads `doubles[c]`, `next == '>'` reads `arrows[c]`, and anything else reads `singles[c]`. Checking `=` first keeps longest match (`==` before `=`, `>=` before `>`).
- This deletes `Pair` and `Single` (157 lines and their byte codes). It also makes `KindNames` load-bearing, so a missing spelling fails the differential (finding 4).
- Confirm with a timed corpus run on bytecode and SLJIT.

### 3. [PERF] Tokenizer.chibi:188: `text_bytes` boxes every source byte in a 48-byte value before scanning starts

**Cost:** run and memory.

- 48 bytes are written and later read per source byte, instead of 1: 15.8 MB for `Steps.chibi` (330,011 bytes) and 155 MB over the corpus.
- Every `bytes[i]` read strides 48 bytes, so a scan touches a new cache line about every byte instead of every 64.

**Evidence:**

- `text_bytes_call` (`src/runtime/text_format.c:481-497`) allocates through `pin_array_snapshot_allocate(storage, PIN_I32, "int", length)` and fills `items[i]` with a full `pin_value` per byte.
- `sizeof(pin_value)` is 48 on this macOS arm64 host (compiled against `src/types/value.h` during this review).
- The native's only other caller is `strings.Bytes` (`packages/core/strings/Strings.chibi:19`).

**Fix:**

- Index the text directly and drop `bytes` from `Tokenize`, `Scan` and the scanners. `ByteAt` keeps its -1 sentinel with `text_byte_length`.
- This works because stage zero types `text[i]` as a byte read (`src/language/semantic_expression.c:1601-1611`) and strings carry their length since #633.
- If `text[i]` is not O(1) on a backend, or stage one rejects it, make `text_bytes` return a packed `u8[]` (stride 1) instead. `text_from_bytes` already reads that form (`text_format.c:515`).
- Confirm with Tokenize time and peak memory on `Steps.chibi`, before and after.

### 4. [ARCH] Tokenizer.chibi:7-126, 171-185, 347, 569: token kinds have three hand-synchronized definitions, and they drifted

**Cost:** change and debug. Adding a kind means:

- renumbering the constants after it;
- inserting its spelling at the same index in `KindNames`;
- for a keyword, moving the `<= Emit` bound at three sites in two packages: `Tokenizer.chibi:347`, `Tokenizer.chibi:569` and `source_parser/Tree.chibi:165`.

**Evidence:**

- **The drift:** #698 (`77f728083`, 2026-10-27) removed `"ref"` from `KindNames` but kept `Ref: int : 102`. Until `48f68bcd8` (2026-10-29, "fix the tokenizer's kind table"), `KeywordTable` registered every keyword from `service` to `delete` under the constant one below it: `prototype` lexed as `Component`, `relationship` as `Prototype` and `from` as `Wildcard`. `..<` had no spelling, and `emit`, `??` and character literals had no kinds. I checked this by comparing `48f68bcd8^`'s constants with its 113 spellings.
- **Why the harness missed it:** it maps both sides through the same `KindNames` index, so a shift is invisible except where a rule reads a constant. Its only table check is `0 < count <= MAX_KINDS` (`source_tokenizer_chibi.c:102`).
- **A stale comment:** "in the order of Odin's Token_Kind" no longer holds. `..<`, `??` and `Char` were appended after the keywords (lines 123-125), so the parser lists the literal kinds one by one (`Tree.chibi:201-202`).

**Fix:**

- **Now:**
  - Name the keyword range once (`pub FirstKeyword: int : Package; pub LastKeyword: int : Emit;`) and use it at the three sites.
  - Derive the operator tables from `KindNames` (finding 2).
  - Add an authored `@test` that checks `length(KindNames()) == KindCount` and that every keyword spelling tokenizes to its own kind.
- **Root cause:** the enumerations with value names that `docs/language-friction.md` ("No enumerations") proposes, as in Odin's `enum` with enumerated arrays. Once those exist, `KindNames`, `KindCount` and the hand numbering go away.

### 5. [PERF] Tokenizer.chibi:610-622: the keyword compare crosses the VM-to-native boundary for every byte

**Cost:** run. `Spells` calls `text_byte_length` once and then `text_byte_at` once per matching byte. That is 183,837 native calls over the corpus, from 65,402 probes that land on an occupied slot, 26,229 of which are real keywords.

**Evidence:** I re-ran the file's FNV table (59 keywords in 512 slots, linear probing) over every word in the corpus.

**Fix:**

- In `KeywordTable`, store each keyword's length and bytes once, in a flat `int[]` with offsets per kind. Then `Spells` compares `bytes[start + i]` with `spelled[offsets[kind] + i]`, and no native calls remain.
- Fusing the hash into the `ScanIdentifier` loop also removes one pass and one call for each of the 213k words.

### 6. [PERF] Tokenizer.chibi:239, 410-452: `Terminates` is called after about 122k ending tokens, and about 98% of those calls are decided by the first byte

**Cost:** run. About 240k calls (`Terminates` and its `ByteAt`) return false because no line break preceded the token and the byte is not `/`, `}` or the end of the text.

**Evidence:** a corpus approximation that tracks parentheses: 121,908 calls, of which 119,821 have no line break and no `/` or `}`.

**Fix:** guard the call inline with `newline || c == Slash || c == 125 || start >= count`, using the first byte that line 256 reads anyway. This keeps the behavior and leaves `Terminates` for the cases that need its comment look-ahead.

### 7. [SIMPLIFY] Tokenizer.chibi:155-158: `Tokens` is a one-variant union used as a record, with nine positional fields

**Cost:** read and change.

- Every consumer unpacks it with a `switch` (`Parser.chibi:51-59`) and binds nine fields by position.
- Eight of them are `int[]`, so swapping `rows` and `columns` at a call site still compiles.
- The C harness reads the payload by index (`column_at`).

**Evidence:**

- The union was the workaround in "Records hold only scalar fields" (`docs/language-friction.md`).
- `docs/compositional-records.md` now says that "nested records and array fields are implemented in both compiler stages".
- `CallCensus.chibi:18` already declares `sites: CallSite[]` inside a struct.

**Fix:**

- `pub Tokens :: struct { kinds: int[]; … }`, returned as a literal.
- `Build` reads `tokens.errors` and `tokens.kinds` by name.
- The harness reads the fields by name.
- This deletes the `switch` and the `.Scanned` constructor.

### 8. [PERF] Tokenizer.chibi:311-320: the row loop rescans every comment for newlines, but only block comments can hold one

**Cost:** run. The loop reads and compares 295,951 comment bytes per corpus pass. Only 76 of them (block comments) can contain `\n`, since a line comment stops before its line break (`ScanComment`, line 739). This is small in total, about 2% of steps, but it is pure waste.

**Evidence:** the byte counts in the brief.

**Fix:** run the loop only when `bytes[start + 1] == Star`.

### 9. [SIMPLIFY] Tokenizer.chibi:274-276: the second `..<` branch can never run

**Cost:** read. A reader has to prove the second branch dead. The first branch also tests the same byte through a `ByteAt` call where the second reads it inline.

**Evidence:**

- Line 268 (`kind == Ellipsis && ByteAt(bytes, start + 2) == 60`) and line 274 (`kind == Ellipsis && start + 2 < count && bytes[start + 2] == 60`) test the same condition, so line 274 is never reached.
- `git blame` shows independent additions in `a147a3269` (2026-10-24) and `5d57cd8c7` (2026-10-27), which looks like a merge artifact.

**Fix:** delete lines 268-270 and keep the inline form at 274-276, or the reverse.

### 10. [STYLE] Tokenizer.chibi:196, 347: `ends` names both the token-end column and the "can end a statement" flag in the same loop

**Cost:** read and change.

- `ends: int[]` (line 196) is appended at lines 242 and 304. Line 347 then shadows it inside the same loop body with `ends := endings[kind] || …`, a `bool`.
- Nearby names add to the confusion: `end` is the token end offset and `endings` is the table.
- Any `ends.append` added below line 347 binds to the flag.

**Evidence:** lines 196, 242, 304 and 347. Also, `line` (line 209) holds the offset where the current line starts, not a line number.

**Fix:** rename the flag `ending` and the offset `line_start`.

### 11. [STYLE] Tokenizer.chibi:268-911: 111 comparisons against raw byte codes, while the file names 7 bytes and spells some of them both ways

**Cost:** read. A reader has to decode `c == 124` or `c == 94`. The same byte also appears in two forms: `Slash` and `Star` are named, yet `c == 47` and `c == 42` appear at lines 456, 768, 771, 853 and 856.

**Evidence:** I counted the numeric comparisons with grep (UTF-8 bounds excluded). The comparisons are in `Pair`, `Single`, `Continues` (18 codes), `ScanNumber`, `ScanEscape`, `Terminates` (123 and 125) and lines 268-274 (60 and 62).

**Fix:**

- Finding 2 deletes most of them.
- Build `Continues` as a `bool[256]` from the string `"+-*/%&|^!<>=.,?:(["`, the same list its comment gives.
- Name the remaining bytes as the file already does for `Quote` and `Slash`.
- Or use character literals, if stage one does not compile this package (see Correctness notes).

### 12. [TEST] test/language/source_tokenizer_chibi.c:702-817: the hand-written expectations live in C, and nothing checks the kind table

**Cost:** debug and change.

- The 13 `CASES` (Odin's errors with their offsets, and terminators placed directly after a token) are pure text-to-tokens checks. An authored `@test` in the tokenizer package expresses the same oracle, which is what `CLAUDE.md` prefers.
- The differential could not see the two-day kind shift (finding 4).
- No benchmark covers the tokenizer. The only performance guard is the 64-steps-per-byte cap, more than 3× the claimed cost of under 20.

**Fix:**

- Move `CASES` into an authored `@test`, together with the kind-table invariants from finding 4.
- Keep the Tree-sitter differential in C, since it needs Tree-sitter.
- Add a corpus-time `*-benchmark` selector, or record before-and-after numbers when findings 1–3 land.

## Correctness notes

I didn't hunt for bugs, and I noticed no tokenizer behavior bug in the current file. These are documentation and harness defects:

- **The docs disagree on whether stage one compiles this package.** `docs/source-tokenizer.md` says "Stage one has not compiled it yet". `docs/language-friction.md:309-313` says stage one "compiles the compiler, `core:strings` and the tokenizer". The only selector that runs the tokenizer uses stage zero's two backends (`tools/test_policy.py:266`; `BACKENDS = 2` in the harness). The answer decides whether character literals and constant arrays are allowed here.
- **The workload is stale.** `source_tokenizer_chibi.c:27` says "about 150 files, under 1 MiB", and `docs/testing.md:1668-1669` says "about 145 files under 1 MiB". The measured corpus is 238 files and 3.22 MB, and `docs/testing.md` says "ten hand-written cases" where there are 13. The per-byte step budget still holds, but `Steps.chibi` is already 64% of `MAX_SOURCE`.
- **A friction entry is stale.** `docs/language-friction.md` ("Records hold only scalar fields") contradicts `docs/compositional-records.md`, and its count "114 token kinds" is now 116.

## What I would delete

- `Pair` and `Single`, after finding 2.
- `BraceHolds`, `ValueFollows`, `contexts`/`depth`, the six context constants, and `valued`, `annotating`, `prototyping` and `before`, after finding 1. Also the context-specific fixed sources in the harness, once the parser's tree differential covers them.
- Lines 268-270 or 274-276, now.
- The `bytes` copy and the hot-path `ByteAt` calls, after finding 3.
- The `Tokens.Scanned` wrapper and the harness's index-based `column_at`, after finding 7.
- `KindNames` and `KindCount`, once enumerations have value names.
- At the end of this path, Tree-sitter itself: `src/language/src/parser.c`, `scanner.c` and `grammar.js`, once `source_parser` replaces `compiler_syntax.Parse`. Most of the terminator-matching logic in finding 1 exists only to reproduce `scanner.c`.

## What I would measure

- The real steps per byte, and wall time on bytecode and SLJIT (average and maximum over the corpus, and `Steps.chibi` alone), before and after findings 1, 2, 3 and 5. The "under 20" claim is unverified here.
- The exact terminator count (the harness's `terminators` line) and the token-class distribution from the tokenizer itself. Mine comes from a regex approximation.
- The cost of a Chibi call and of a native call against an inline compare on each backend. This sizes findings 2, 5 and 6.
- Whether `text[i]` is O(1) on both backends and accepted by stage one, and its cost against `bytes[i]`.
- Whether the `int[]` columns built by `append` are packed (4 B) or boxed (48 B) on each backend. The corpus output is 12 MB in one case and 142 MB in the other.
- Peak memory of one `Tokenize` call on `Steps.chibi`.
- Whether stage one compiles this package, which settles the documentation conflict.
- The whitespace loop's second read of `bytes[start]` per byte (869k bytes): change it only if a profile shows it.

## What I would leave alone

- **Odin's FNV keyword table with linear probing.** At 59 of 512 slots filled, a perfect hash or a `switch` would save little and depart from the prior art. Only the byte compare needs fixing (finding 5).
- **`Lexeme` and the partial-resume loop in `ScanString`.** They serve 43k scans (7% of tokens) and give Odin's exact error offsets.
- **The tables rebuilt on every call** (`Classes`, `Endings`, `KindNames`, `KeywordTable`). They cost about 1,500 calls per call, under 1% of an average file. Package-level constant arrays are not available in stage one (`docs/package-compiler.md:184`). Revisit when a per-keystroke caller exists.
- **The `rows` and `columns` columns.** They could be derived from `starts` plus a line-start table (72k entries against 2 × 590k). But every Tree-sitter-shaped row the parser emits carries a row and column (`Parser.chibi` `Emit`), so the saving moves cost rather than removing it.
- **Five parallel token columns.** This is the right shape. The parser copying each token into a 16-field `Node` with a string kind (`Parser.chibi:61-77`, `Tree.chibi:200-226`) is the parser's cost, and it deserves its own review.
- **The length of `Tokenize`.** It is a lexer loop. Its defect is the context state of finding 1, not its size.
- **`Terminates`' comment look-ahead.** The continuation rule needs the first byte after any comments. Go's scanner does the same look-ahead (`findLineEnd`).
- **The four error columns, of which the parser reads only the first.** They are rare and cheap, and the harness checks them all.
