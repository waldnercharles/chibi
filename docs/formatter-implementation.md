# Formatter implementation and option coverage

This records the formatter source in the `formatter-wasm` worktree on
2026-11-02. The [original survey](formatter-options-survey.md) describes the
design space. This document maps each proposed family to its implementation
and identifies the variants that remain unsupported.

The [canonical catalog](../tools/compiler/source_format/options.json) defines
173 public settings, including profiles, file endings and line selection.
`generate-options.mjs` generates the native option definitions, validation,
configuration parser and browser controls from that catalog. The browser has
searchable groups and exports `NAME=VALUE` overrides. The versioned profiles
are `current_v1`, `compact_v1`, `expanded_v1` and `tabular_v1`. Explicit
overrides take precedence over a profile. These are Chibi style profiles;
they do not reproduce a vendor formatter's complete configuration or output.

Implemented below means code and behavior tests exist. It does not mean every
possible combination has been exercised or that a newly built browser artifact
has passed acceptance. See the validation record below.

## Coverage of the survey

Each row corresponds to an option family in the survey. Partial coverage names
the missing variants rather than treating an exposed setting as full support.

| Survey family | Implemented settings | Coverage and limits |
| --- | --- | --- |
| Brace placement | `brace_placement`; `function_braces`, `control_braces`, `type_braces`, `foreign_braces`, `when_braces` | Preserve, attach or next line with construct overrides. Shifted braces and a separate initializer-brace policy are absent. |
| Following keyword | `following_keyword` | `else` and `else when` can share the closing-brace line, start the next line or preserve the authored choice. |
| Empty blocks | `empty_blocks`; `empty_function_blocks`, `empty_control_blocks`, `empty_type_blocks` | Preserve, collapse or expand, with these construct overrides. |
| Short bodies | `short_functions`, `short_branches`, `short_loops`, `short_case_arms`; `keep_one_line_blocks` | Expand, preserve or inline if they fit. Generated compaction keeps comments and nested controls expanded. |
| Inline-body limits | `max_inline_statements`, `preferred_width` | Statement limit and soft width govern generated compaction. There is no independent inline-body width setting. |
| Brace presence | `branch_braces` | Preserve, require or omit a safe single body. Parser support retains a Block scope for unbraced `if`, `else`, `while` and `for`. Empty bodies, ambiguous chains, comments and nested controls retain braces when needed. |
| Simple-statement placement | `unbraced_body_placement` and `short_*` controls | Unbraced bodies can preserve authored placement, share the header line or start the next line independently of brace presence. Grammar hazards and comments retain safe breaks. |
| Author breaks | `list_layout` and per-context list policies; `binary_operator_position`, `chain_layout` | Preservation and canonical choices exist for these constructs. There is no single global switch to disregard every authored line break. |
| List layout | `list_layout`; `arguments_layout`, `parameters_layout`, `arrays_layout`, `records_layout`, `bindings_layout` | Preserve, fit/pack, always vertical or one item per line when wrapped. Return tuples share argument layout; no independent tuple-layout setting exists. |
| List limits | `max_items_per_line`; `arguments_width`, `parameters_width`, `arrays_width`, `records_width` | Shared item limit and these width overrides. No separate binding-list width. |
| Hanging layout | `list_indent`, `continuation_indent`, `align_after_open_bracket`, `closing_delimiter` | Block, align-first or fixed hanging indentation; own-line, last-item-line or preserved closer placement. |
| Binary breaks | `binary_operator_position`, `binary_layout` | Leading, trailing or preserved operators; every operator or precedence groups. Association remains unchanged. |
| Member/call chains | `chain_layout`, `chain_indent` | Preserve, fit or vertical chains, with independent continuation indentation. Optional-access punctuation remains attached where required by the grammar. |
| Declaration signatures | `signature_break_priority`, `attribute_layout`, `parameters_layout` | Prefer parameter or result breaks; preserve, inline or split attributes. A bare scalar result without `->` cannot begin a new line, so return-first falls back to parameter wrapping. Empty parameter lists can exceed the soft width. |
| Return/assignment expressions | `continuation_indent`, `align_operands`, binary and chain policies | Long expressions use the shared continuation machinery. Separate return-only and assignment-only placement policies are absent. |
| Operator spacing | `operator_spacing`; `space_multiplicative`, `space_additive`, `space_comparison`, `space_logical`, `space_assignment`, `space_bitwise`, `space_range` | Uniform, preserved or precedence-oriented spacing, with family overrides for zero, one or two spaces. Required lexical separators remain. |
| Precedence-oriented spacing | `operator_spacing=precedence`; operator-family overrides | Supports layouts such as `a*b + c*d` and wider configured family gaps. It does not classify an expression's subjective readability. |
| Keyword/call spacing | `space_before_control_parens`, `space_before_call_parens`, `space_before_declaration_parens` | Independent syntax contexts. |
| Delimiter interiors | `space_inside_*` settings; legacy `space_in_braces`, `space_in_parens`, `space_in_brackets` | Calls, parameters, groups, controls, empty parentheses, arrays, indexing, array types, empty brackets and record literals are separate contexts. |
| Colon/arrow spacing | `space_before_type_colon`, `space_after_type_colon`; corresponding field, named-argument and case settings; `space_around_arrows`, `space_around_declaration_separator` | Context-specific spacing for `:`, `->`, `=>`, `:`/`::` declaration forms. |
| Unary/postfix spacing | `space_after_unary`, `space_before_postfix` | Applies where tokenization remains unchanged. Unsafe token mergers keep a separator. |
| Block padding | `block_padding`, `keep_blank_lines_at_block_edges`; `function_block_padding`, `type_block_padding`, `control_block_padding`, `foreign_block_padding`, `when_block_padding` | Global inherit, remove or add one blank line inside nonempty blocks, with independent construct overrides. |
| Declaration and member gaps | `blank_lines_between_declarations`, `blank_lines_between_members`, `max_blank_lines` | Required declaration/member gaps are separate from the preservation cap. No separate before/after count for every declaration kind. |
| Statement gaps | `blank_lines_before_final_return`, `blank_lines_after_control`, `statement_paragraphs` | Explicit syntax rules and SEGMENT action-unit grouping. No arbitrary ordered pair-of-statement selector configuration. |
| Comment and import gaps | `blank_lines_between_import_groups`, `blank_lines_before_comments` | Collection changes can separate imports. Standalone comment groups can preserve authored gaps or use zero to three blanks before the group; documentation lines stay together and trailing comments are excluded. |
| Hand-arranged data | `data_alignment=preserve`; `// chibi fmt: off` / `on` | Preserve same-line table spacing or protect a region. Region directives provide the survey's escape hatch. |
| Alignment mode | `assignment_alignment`, `field_type_alignment`, `declaration_type_alignment`, `constant_alignment`, `record_value_alignment`, `comment_alignment`; legacy `align_*` flags | Inherit, off, preserve manual columns or align consecutive rows. Local declaration types and typed/untyped constants have anchors. |
| Group boundaries | `alignment_across_blank_lines`, `alignment_across_comments` | AST scope and indentation remain boundaries. Blank lines and standalone comments stop a group unless the relevant override permits crossing them. |
| Padding and margin limits | `alignment_max_padding`, `alignment_max_column` | Bounded padding and target column. An outlier skips its whole alignment group. Zero means unlimited within the formatter's output cap. |
| Compound operators | `alignment_compound` | Align operator starts or assignment markers without changing operators. |
| Multiline alignment | `align_after_open_bracket`, `align_operands`, `list_indent`, `chain_indent`, signature layout | Arguments, expressions and chains use their own continuation rules. There is no independent trailing-return column group. |
| Tab fill | `use_tabs`, `indent_width`, `tab_width` | Tabs indent; spaces align. Display width can differ from indentation width. Columns count UTF-8 bytes with tab expansion, not grapheme display widths. |
| Data columns | `data_alignment`, `data_column_alignment`, alignment padding/column limits | Nested arrays and compatible record rows can align left or right. CSV `auto,left,right` overrides cover at most 32 columns. Multiline, commented, protected or nested cells and incompatible record fields remain unchanged. |
| Partial formatting | `start_line`, `end_line`, `range_scope` | Select whole declarations or statements and expand affected groups where required. Bytes outside the chosen context stay exact. The API reports expansion in notes, not as structured offset fields. |
| Control-header parentheses | `control_parens`; `if_parens`, `while_parens`, `for_parens`, `switch_parens`, `when_parens` | Preserve, require or omit optional groups with a normalized syntax-tree check. |
| Expression parentheses | `expression_parens` | Preserve, omit redundant groups or add groups around mixed precedence for readability. No algebraic reassociation. |
| Parenthesis detail | `flatten_parens`, `expression_parens`, `readability_mixed_operators` | Repeated groups can flatten. Readability mode can also group different operators at the same precedence. Association remains unchanged. |
| Trailing commas | `trailing_commas`, `magic_trailing_comma`; `trailing_commas_arguments`, `trailing_commas_parameters`, `trailing_commas_arrays`, `trailing_commas_records`, `trailing_commas_tuples`, `trailing_commas_bindings` | Preserve, omit or add to multiline supported lists, with per-kind overrides. Magic-comma wrapping follows the resolved list policy. Required singleton return-tuple commas remain. |
| Semicolons | `semicolons` | Keep, omit optional or always, while retaining required separators. |
| Local type annotations | `binding_types`, `local_types`, `constant_types` | Preserve, omit redundant, explicit or inferred when evident, using actual compiler analysis. Contextual numeric annotations remain when omission changes the program. |
| Type contexts | `parameter_types`, `return_types`, `member_types`, `public_types`, `analysis_target`, `semantic_targets` | Context overrides and four target configurations. Public formatting defaults to proof on all four platforms; selecting one target is explicit. Mandatory parameter/field types, unresolved sites and unsuitable recursive or suspending results remain with notes. Generic definitions are not specialized from one use. |
| Type spelling | `type_spelling` | Preserve aliases, shortest unambiguous spelling or accessible qualified spelling. Unnameable types retain their original form. |
| Imports | `imports` | Preserve, sort inside existing groups or propose removing unused explicit aliases. Every order/deletion edit requires compiler proof. Removal retains implicit namespaces, `_`, conditional imports and uncertain/commented candidates. |
| Literal spelling | `numeric_separators`, `decimal_group`, `hex_group`, `fraction_group`, `exponent_group`, `numeric_min_digits`, `hex_case`, `exponent_case`, `numeric_base`, `string_escapes`, `char_escapes` | Checked digit grouping/casing, exact integer base conversion, minimal or ASCII escapes. String and character token kinds remain distinct. No new quote or raw-string syntax. |
| Other declaration forms | `function_body`, `function_keyword`, `return_arrow` | Block/`=>` bodies, optional `fn`, optional return `->`. Required lambda keywords remain. No unrelated qualification or modifier rewrite family exists. |

The survey's later comment-reflow suggestion is also implemented.
`comment_reflow=preserve|prose|docs|all`, `comment_width` and
`reflow_block_comments` distinguish standalone prose from documentation and
block comments. The conservative classifier preserves trailing comments,
fenced/indented code, Markdown tables/lists/headings, URLs, directives,
deliberately aligned text, code examples and short section labels. Block
comments with multiple paragraphs remain authored. This is a heuristic for
eligible prose, not a natural-language readability model.

Literal base conversion is limited to exact unsigned 64-bit magnitudes and
requires compiler proof in the public pipeline. Larger integers stay with a
note. Floating-point case/separator changes compare canonical spelling rather
than round-tripping through a host float. Escape styles use the compiler's
decoder and preserve decoded bytes, including NUL; unsupported non-Unicode
spellings stay with notes. Character tokens and escapes are supported, while
the existing compiler's character-value execution gap is not claimed fixed.

## Configuration import and export

The playground's [configuration module](../tools/docs/playground/config.mjs)
imports and exports Chibi `NAME=VALUE` text and one pasted EditorConfig file.
Choose the format, paste into the separate import box and select **Apply config**.
The editable document determines matching sections; repository paths are
relative to the source inventory root, and Scratch uses `scratch.chibi`.
Parent-directory configuration discovery is not implemented.

Common EditorConfig properties map to formatter options:

| EditorConfig property | Formatter setting |
| --- | --- |
| `indent_style` | `use_tabs` |
| `indent_size` | `indent_width` |
| `tab_width` | `tab_width` |
| `end_of_line` | `line_ending` |
| `insert_final_newline` | `final_newline` |
| `max_line_length` | `preferred_width`, with `off` mapping to zero |

`chibi_profile` selects a versioned profile. Every catalog setting can use its
`chibi_` prefix, such as `chibi_statement_paragraphs=logical` or
`chibi_semantic_targets=all`. Chibi-specific keys override the common property
for that setting regardless of their relative line order. The profile applies
first, then explicit overrides. Unspecified settings retain their current value
unless a profile supplies a new baseline.

Later matching sections win, and `unset` removes a preceding property from the
pasted file. Keys and common values are case-insensitive. Section matching
supports `*`, `**`, `?`, character classes, brace alternatives and signed numeric
ranges, with bounded matching work. Whole-line `#` and `;` comments are accepted;
inline comments are not stripped. Unsupported properties produce warnings.
An invalid recognized value aborts the whole import without changing settings.
The export includes common properties, the selected profile and Chibi-specific
overrides needed to preserve the effective configuration.

## Logical paragraphs use SEGMENT

`statement_paragraphs=logical` runs
[Paragraphs.chibi](../tools/compiler/source_format/Paragraphs.chibi).
`syntax` adds structural gaps; `preserve` keeps authored grouping without
inferring new sections. The logical mode implements the published four-phase
SEGMENT action-unit algorithm, adapted to Chibi's AST and lexical scopes:

1. Identify same-syntax, object-related, E-SWIFT and return candidate units.
2. Resolve overlaps using the action-unit rules and statement similarity.
3. Refine same-syntax units of at least four statements with the published
   sliding-triple comparison.
4. Merge compatible singleton units using the published category pairs.

The identifier and RHS similarity rules determine categorical similarity
levels. They do not use a paragraph-length target or weighted partition score.
Function, closure and system bodies are analyzed as method bodies. A compound
statement remains one unit in its parent; nested bodies are analyzed separately
when they have at least four immediate statements. Physical wrapping and
optional punctuation do not change the statement count.

The [SEGMENT reference](formatter-segment-algorithm.md) records the published
rules, source evidence and test obligations. It distinguishes those rules from
the decisions needed for a deterministic Chibi implementation. The recovered
primary source leaves details such as overlap ordering and singleton tie
breaking unspecified. Those choices are documented adaptations; the original
Java implementation has not been recovered.

Authored blank lines, attached comments, directives and protected regions wrap
the segmentation pass. They preserve authored sections and text without
replacing SEGMENT's computed boundaries. The formatter inserts blank lines
between complete statements and retains statement order. It does not infer a
call's meaning from an input/output keyword list.

The [readability research](formatter-readability-research.md) also covers Go
wsl and evaluation methods. Human preference and comprehension experiments,
learned classifiers and per-boundary explanation UI remain future work.

## Validation contracts

[Pipeline.chibi](../tools/compiler/source_format/Pipeline.chibi) orders type,
function and import changes before literals, parentheses and layout.
Token-changing stages keep a matching parse. Configured comma insertion uses a
preliminary layout without inferred paragraphs, then punctuation changes and
the final layout. Comment reflow runs after final indentation so its width
calculation uses the output margin on the first pass. Its lexical proof retains
all executable tokens. Range selection then splices the completed result into
the original source. This is a fixed sequence, not a repeat-until-stable loop.

Layout checks tokens and implicit statement boundaries. Optional punctuation,
control braces and function forms have specific normalized-tree checks.
Compiler-backed changes compare prepared operations, conversions, effects,
signatures and lifetimes, with canonical mappings for reachable type and
generated callable identities. Public exports, foreign identities and generic
template shapes are part of the semantic check. A failed proof keeps the
affected source and returns notes. Merely compiling both variants is insufficient.

Public formatting defaults to `semantic_targets=all`. `analysis_target` selects
the initial analysis and candidate type spelling. The public pipeline then
proves each compiler-backed batch for Linux x86-64, macOS arm64, Windows x86-64
and WebAssembly 32-bit before applying it. A failed target proof retains the
affected original source and reports notes; ordinary layout remains available.
`semantic_targets=selected` explicitly limits proof to `analysis_target` for
intentional platform-specific formatting.

The lower-level `source_type_style.RewriteWith` and project rewrite APIs retain
their selected-target contract. Calling them directly does not provide the
public formatter's additional all-platform proof.

Import organization collects identifier mentions outside imports, including
uninstantiated templates and inactive conditional branches. It deliberately
treats a shadow declaration with the same spelling as a use. This can retain
an unused namespace; it cannot justify deleting one. Sorting uses decoded
paths and aliases with a stable merge sort. Blank lines, standalone comments,
directives and `when` branches prevent group crossing. Trailing comments move
with the import, while leading documentation remains anchored.

`FormatProject` and `FormatParsedProject` accept immutable dependency documents
through `Context`. Ordinary layout needs no compiler analysis. Semantic styles
need enough project source to resolve the requested program. The browser loads
that context from the static repository source inventory when available.
Context is bounded to 256 documents and 16 MiB. Missing dependencies keep the
requested semantic changes unapplied, with layout still available.

## Validation record

The SEGMENT replacement passed **256 native formatter tests** (32 private and
224 public) in 118.06 seconds while the Wasm build ran concurrently. Coverage
includes 13 listing/rule-derived SEGMENT examples, lexical scopes and complete
references, similarity/refinement/merging, library-list exclusion, protected
text, partial selections, other formatter styles and repeated-format stability.
Log: `.chibi/formatter-segment-native-accepted.log`.

Generator consistency and all **44 JavaScript catalog/configuration/context/client
tests passed in 0.107 seconds**. They cover the 173-setting shared browser/native
catalog, request framing, profile overrides, the native default fixture and
configuration interchange. These JavaScript checks do not execute SEGMENT.
Log: `.chibi/formatter-segment-javascript.log`.

The rebuilt Wasm artifact passed **25 tests in 1.598 seconds**. These include
SEGMENT's overlap/refinement/singleton examples, all four profiles, semantic
styles, protected source and fixed points over six project files plus a
5,000-statement uniform run. The project/large-run case took 0.163 seconds
including both formatting passes and highlighting. It is not an isolated
algorithm-only benchmark. Log: `.chibi/formatter-segment-wasm.log`.

The coherent build took 283.89 seconds, including the concurrent native run.
It contains a 4,501,913-byte Wasm module with SHA-256
`e77c5cc933c99ef656c563e917ec8a213798cb060c7be4f3ce7be951a9b913b5`
and input digest
`9503c2657a5bd65b6572bd4bf4a01d07a5c8c8beaee245028f92bca0a6f46c3e`.
HTTP build receipts and Wasm hashes match at both local URLs. Browser acceptance
at port 8768 rendered the 173-setting catalog, selected Logical groups (SEGMENT),
and showed the two expected inserted lines for the object-overlap and
identifier-similarity examples. No console warnings or errors were observed.
Screenshot: `.chibi/formatter-segment-browser.png`.

The main URL `http://127.0.0.1:8766/chibi/formatter/` and the example preview at
`http://127.0.0.1:8768/chibi/formatter/` now serve
`.chibi/formatter-segment-final`; existing tabs need reloading. The earlier
`.chibi/formatter-full-final-reflow` build is retained. Full acceptance receipt:
`.chibi/formatter-segment-acceptance.json`. Changes remain local and uncommitted;
GitHub Pages has not been deployed.

### Earlier formatter checkpoints

The native, Wasm and browser results below precede the SEGMENT replacement.
They retain evidence for the earlier formatter and its 174-setting catalog;
they do not validate the current paragraph algorithm or 173-setting catalog.

The prior accepted browser build covered the smaller brace/type feature set.
Its 502-test compiler run and browser acceptance are historical evidence, not
validation of the complete current formatter.

The broad native checkpoint passed **718 of 718 tests in 173.26 seconds**.
It includes 222 formatter tests, comprising 12 private and 210 public cases,
51 semantic-style cases, 49 parser cases and 15 tokenizer cases. Five formatter
fixtures explicitly exercise default all-platform preservation, selected-target
opt-in, portable omissions, equivalent platform types and retained contextual
numeric conversions.

During acceptance, a Wasm case exposed overly conservative treatment of mixed void and
unresolved/value-returning function bodies. The correction queries prepared
expression results, selects eligible void bodies and proves the edited batch.
The focused formatter suite passed **222 of 222 tests in 56.45 seconds**
before the final broad run. Logs are
`.chibi/full-pipeline-mixed-void-tests.log` and
`.chibi/void-offsets-style-tests.log`; the broad checkpoint log is
`.chibi/formatter-compiler-final-void-tests.log`.

A later comment fixed-point regression showed that reflow before indentation
could wrap an initially unindented comment differently on a second run. Reflow
now measures the final indentation. The current public formatter suite passed
**211 of 211 tests in 31.10 seconds**, including that regression. The log is
`.chibi/formatter-comment-final-indent-tests.log`.

The counts use different scopes. The previous 222-test formatter result was
210 public cases plus 12 private function-style cases. The new directory-only
run covers 211 public cases; it does not rerun those 12 private cases. The
current source inventory therefore contains 223 formatter tests. The 718-test
broad result predates this localized ordering fix and is not a new broad run
against it.

The current fresh driver passed these native execution checks:

| Native check | Result | Combined elapsed time |
| --- | --- | --- |
| Function forms and scoped single `for` bodies | Six cases each at O0 and O2 | 1.62 seconds |
| Grouped numeric literals | Two cases each at O0 and O2 | 1.40 seconds |
| Unbraced control bodies, scopes and branch targets | Seven cases each at O0 and O2 | 1.20 seconds |

The preceding native checkpoint also passed one grouped-record return case
each at O0/O2 and optional/managed-value ownership checks in 31.88 seconds.
Those logs are `.chibi/formatter-native-final-records.log` and
`.chibi/formatter-native-final-optionals.log`.

The current producer receipt records 87 compiler inputs, including the new
prepared void-call query, with identical pre/post-build hashes. The fresh driver
is `.chibi/formatter-final-void-driver`, 6,682,640 bytes, SHA-256
`ca55770532ba76ce037809149e04e7e908afd477c3245ea8473c22a2ef7c5b27`.
It built in 57.56 seconds. The producer receipt is
`.chibi/formatter-final-driver-inputs.json`; current execution logs are
`.chibi/formatter-native-void-*.log`. The exact repository Host context
probe omitted four safe annotations, retained 25 required/contextual sites and
reported no analysis error.

The earlier suite included 11 paragraph-grouping cases and 12 import-candidate
cases. It covered expected output, fixed points, comments, protected bytes,
labels, wrapping, terminators and commas, decoded imports, conditional groups
and conservative shadowing/template/inactive-use handling. SEGMENT replaces
that paragraph grouping and needs its own evidence. A separate native parser/helper probe
over 5,000 declarations completed in 0.22 seconds including process startup.
That measurement excludes compiler semantic proof and is not browser latency.

The JavaScript configuration/context/catalog/client checks passed **44 of 44
tests in 0.094 seconds**. The log is `.chibi/formatter-javascript-reflow-final.log`.
Browser configuration acceptance covered applying an EditorConfig import and
showing validation errors without changing settings.

The final reflow-fixed coherent Wasm artifact passed **24 of 24 checks in
1.497 seconds**, including the indented width-40 comment fixed-point regression.
`.chibi/formatter-full-final-reflow` built in 196.89 seconds and contains the
4,389,641-byte Wasm module with SHA-256
`d0781fa04e3be993dd8c99656fcf4ee69b1b2f4b6300ddd003e466b329172bcd`.
Its input digest is
`3ceeeb55cca3181e488677ac40e9beac24b2626887d55c5e6cdc0f26fb9adb5b`.
All shipped assets match the build receipt. The test log is
`.chibi/formatter-wasm-reflow-final.log`.

Prior browser UI checks used the 174-setting module with all-platform validation
as the default. The Bytes fixture applied 18 type-style changes and retained
29 sites with `return_types=inherit`. The Host fixture applied 23 changes and
retained 80 sites without an analysis error; foreign/when indentation and
function keyword/return-arrow controls also worked. These are results for the
tested configurations, not counts every profile should reproduce.

Browser editing now retains a tracked file's project identity and marks its
label `, edited`. A manual regression edited Bytes, resolved its imports and
matched a filename-specific EditorConfig section after the edit. Explicitly
opening Scratch still clears the file identity. Valid EditorConfig import and
invalid atomic import checks passed.

The final build is available at
[the local formatter preview](http://127.0.0.1:8768/chibi/formatter/).
Actual prefixed HTTP checks verified the static assets and Wasm hash. Real
project-context loading and real Wasm formatting over HTTP handled edited Bytes
with a two-document context; a second edit reused cached dependency context
and supplied the latest edited document. The receipt is
`.chibi/formatter-final-http-context.json`.

Final browser acceptance ran the reflow-fixed build with function expression
bodies, required `fn`, omitted return arrows, inferred bindings, logical
paragraphs and prose wrapping together. The resulting code and all 174 controls
rendered correctly, and the browser reported no warnings or errors. The saved
screenshot is `.chibi/formatter-full-browser.png`.

The main [local playground](http://127.0.0.1:8766/chibi/formatter/) now serves this
accepted build. HTTP checks matched its shipped page, assets and Wasm module
exactly; a fresh browser tab loaded Bytes and reported it already formatted.
The previous site remains at `.chibi/formatter-styles-final`. The final local
acceptance receipt is `.chibi/formatter-full-acceptance.json`. Changes remain
local and uncommitted; GitHub Pages has not been deployed.

## Remaining survey variants

The coverage table identifies the unimplemented variants within supported
families. Per-boundary paragraph explanations, interactive pairwise preference
collection and learned/readability classifiers remain follow-ups. They are not
required to run the deterministic paragraph formatter. EditorConfig supports
single-file interchange; automatic ancestor discovery remains outside this
iteration. Arbitrary optional call parentheses, replacing a
string with a character literal, and foreign-language modifiers would require
different Chibi grammar or semantics; they are not formatter options.
