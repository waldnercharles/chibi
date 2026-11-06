# Formatter options research

Researched on 2026-11-02. This records the design proposal and the baseline
before the expanded formatter implementation. Current implemented settings and
remaining variants are mapped in
[formatter implementation and coverage](formatter-implementation.md).
This report covers official documentation and first-party source
for Rider, CLion, clang-format, Prettier, dprint, Biome, rustfmt, Swift-format,
StyLua, odinfmt, Uncrustify and JuliaFormatter.

The useful direction for Chibi is a configurable style profile with three
visible groups: layout, syntax preferences and type-aware preferences. A profile
can compose all three, but they need different information and validation.
Adding or removing types cannot be handled safely by the current parser-only
formatter. Brace placement, brace presence and compact bodies also need separate
controls; a one-line body can retain its braces.

The earlier survey in the retained formatter worktree was useful as an index,
but its claims about Chibi's permitted line breaks are obsolete. This report
uses the current source and fresh read-only playground probes. No formatter,
runtime, grammar or playground behavior was changed for this research.

## What the tools offer

These are examples of the available design space, not claims that every tool
supports every option or that their language-specific settings transfer directly.

| Tool | Relevant behavior and lesson |
| --- | --- |
| Rider | Separates reformatting from cleanup tasks such as applying inferred-type style, adding/removing optional parentheses and braces, and converting body forms. Its type preferences distinguish built-in, simple and other types, with inferred, explicit and inferred-when-evident choices. The meaning of evident is itself configurable. [Cleanup][rider-cleanup], [type preferences][rider-var], [syntax schema][rider-syntax]. |
| Rider layout | Offers brace placement by construct, independent short-body and wrapping policies, column alignment, caps on distant alignment, operator-family spacing, and separate minimum/maximum blank-line settings. These are useful independent axes rather than one opaque style switch. [Braces][rider-braces], [wrapping][rider-wrap], [alignment][rider-align], [spaces][rider-spaces], [blank lines][rider-blank]. |
| CLion | Has a built-in formatter and a clang-format mode. In clang-format mode, CLion's own formatting settings are ignored; the selected engine and configuration precedence matter. Its built-in C/C++ settings cover indentation, wrapping, braces, spaces, alignment and blank lines. [CLion settings][clion]. |
| clang-format | The released 22.1 reference offers named styles and controls such as `BreakBeforeBraces`, `BraceWrapping`, `AlignConsecutiveAssignments` and `BreakBinaryOperations`. It covers short bodies, list packing, precedence-aware wrapping, alignment, comments and literal separators. Its development documentation contains newer options, so a versioned baseline matters. Syntax-changing settings have their own limitations. [Released options][clang22], [development options and warnings][clang-dev]. |
| Prettier | Deliberately limits the style surface. `printWidth` is advisory; options include object expansion through `objectWrap`, delimiters, quotes and punctuation. `experimentalOperatorPosition` and experimental ternaries are explicitly experimental. Arrow-parentheses settings are arrow-specific, while markup bracket placement is not general brace style. [Options][prettier-options], [option philosophy][prettier-philosophy]. |
| dprint TypeScript | Separates `bracePosition`, `useBraces` and `nextControlFlowPosition`. Its configuration also separates block padding, member gaps, hanging lists and forced multiline layout, with construct overrides. This is a close match for the breadth requested here. [Brace position][dprint-brace], [brace presence][dprint-presence], [following keyword][dprint-next], [configuration definitions][dprint-types]. |
| Biome | Has formatter settings for delimiter interiors, object/array expansion and operator break side. Removing inferable types is instead an opt-in lint rule with a restricted safe fix; its block-statement rule is a separate lint rule with an unsafe fix. [Configuration][biome-config], [type rule][biome-types], [brace rule][biome-braces]. |
| rustfmt | Combines width and per-construct thresholds, including arrays and chains. Its configuration explicitly distinguishes stable options from nightly-only options. Field alignment, brace style and comment wrapping appear in the configurable surface with stability restrictions. [Configuration source][rustfmt]. |
| Swift-format | Offers preserve-existing-breaks, one-argument-per-line, declaration-attribute breaks, chain grouping, trailing-comma policy and comment spacing. Its configuration distinguishes preserving authored arrangement from canonical reprinting. [Configuration][swift-format]. |
| StyLua | Has separate simple-statement collapse choices for functions and conditionals. Optional call parentheses are specific to Lua's single string/table argument grammar and are retained where omission would obscure the parse. [Configuration][stylua]. |
| odinfmt | Provides named brace styles, case indentation, single-line block spacing, field/value/constant alignment and import sorting. These are useful nearby syntax examples, especially for `:` and `::` declarations. [Official schema][odinfmt]. |
| Uncrustify | Separately controls arithmetic, additive, assignment, boolean and comparison spacing. This provides direct prior art for compact multiplication inside more widely spaced addition, without assuming one setting for every operator. [Official configuration][uncrustify]. |
| JuliaFormatter | Offers operator spacing inside indices and opt-in alignment that can use existing extra whitespace as a signal. Its matrix option preserves authored matrix spacing. Some syntax transforms have iteration/idempotence caveats, which Chibi should avoid adopting as routine formatting behavior. [Alignment][julia-align], [formatting options][julia-options]. |

Two distinctions are especially important. Clang-format's parenthesis-removal
setting is not a general precedence-aware minimizer, and its brace/qualifier
rewrites carry incomplete-semantic-information warnings. Clang-Tidy separately
has a constrained inferred-type rewrite and an AST-based brace check. This is
evidence for keeping rewrite validation distinct from whitespace layout.
[Clang warnings][clang-dev], [use-auto][clang-auto], [brace check][clang-tidy-braces].

## Chibi baseline when the survey began

This section is a historical snapshot. Its parser-only limitations and option
count describe the starting point, not the current worktree. The implementation
now includes compiler-backed styles, optional control bodies and logical
paragraphs. See the [coverage matrix](formatter-implementation.md).

The starting formatter had 25 option fields. Some requested features already
existed, so additions needed to refine their scope rather than create aliases.
[Options and defaults](../tools/compiler/source_format/Options.chibi).

| Existing group | Current controls |
| --- | --- |
| Width | `preferred_width`, `magic_trailing_comma` |
| Indentation | `indent_width`, `use_tabs`, `continuation_indent`, `align_after_open_bracket`, `align_operands`, `indent_case_labels` |
| Interior spacing | `space_in_braces`, `space_in_parens`, `space_in_brackets`, `spaces_before_trailing_comment` |
| Columns | `align_assignments`, `align_field_types`, `align_constants`, `align_struct_literal_values`, `align_trailing_comments` |
| Blank lines | `max_blank_lines`, `blank_lines_between_declarations`, `keep_blank_lines_at_block_edges` |
| Compact blocks | `empty_blocks`, `keep_one_line_blocks` |
| Punctuation and file endings | `semicolons`, `crlf`, `final_newline`; the text configuration exposes `crlf` as `line_ending=lf|crlf` |

`FormatParsed` then laid out the parsed source, retokenized the result and
rejected changed token streams. It permitted the existing explicit/implicit
terminator equivalence and used a tree comparison for the relevant terminator
differences. Comments and literal bytes were preserved. Adding type annotations,
braces or parentheses would require a new, specific rewrite contract rather
than weakening that check globally.
[Formatter validation](../tools/compiler/source_format/Format.chibi#L63).

The initial grammar and read-only probes established the following:

- Next-line function/control braces already parsed; the formatter then
  preserved tested Allman layouts. New brace placement can be a layout policy.
- A newline before a binary operator was not categorically forbidden. The
  tokenizer recognized continuation tokens, and a tested leading `+` survived
  formatting. Each proposed wrapping form still needs its own parser check.
- Parenthesized `if` and both parenthesized/unparenthesized `for` headers parsed.
  The tested forms were preserved.
- `if` and `while` bodies required a block. Tested unbraced returns were rejected
  with an expected-opening-brace diagnostic. Omitting those braces was then
  a language change, even when the body is one short statement.
- Parameter type clauses and return annotations were grammatically optional,
  but parsing a missing annotation does not prove that every such declaration
  can infer a valid type. A type rewrite must not specialize a generic definition
  from one call site's concrete types. Iterator bindings had no type
  clause in their grammar.
- Double-quoted strings and apostrophe-delimited character literals are distinct
  token kinds. A generic single-versus-double quote preference would be wrong.

Sources: [termination rules](../tools/compiler/source_tokenizer/Tokenizer.chibi#L418),
[control-flow parsing](../tools/compiler/source_parser/Statements.chibi#L194),
[while parsing](../tools/compiler/source_parser/Statements.chibi#L35),
[for headers](../tools/compiler/source_parser/Statements.chibi#L232),
[grouped expressions](../tools/compiler/source_parser/Expressions.chibi#L290),
[parameters](../tools/compiler/source_parser/Declarations.chibi#L435),
[results](../tools/compiler/source_parser/Declarations.chibi#L358),
[literal tokens](../tools/compiler/source_tokenizer/Tokenizer.chibi#L618).

The starting browser entry imported the parser, formatter and highlighter. It did not
have the compiler's resolved types, imports or overload decisions. Type-aware
style therefore needed an additional semantic input/service, which could still
run locally or in the browser. That baseline could not offer it as a working source-only setting.
[Browser entry](../tools/docs/playground/wasm/Main.chibi).

## Proposed option catalog

The following tables retain the original Chibi proposal, with its original
status descriptions. Names and values were illustrative. Many are now
implemented with catalogued names; the coverage matrix records those mappings.
The prior-art links above motivate the axes; Chibi's grammar and semantic
contracts decide which are valid.

The classes describe the required proof, not whether the user must invoke
different commands:

| Class | Required information and check |
| --- | --- |
| L: layout | Token spelling is unchanged. Retokenization must preserve implicit statement boundaries too. |
| T: syntax normalization | Tokens change, but a narrowly defined normalized syntax tree and comment associations must remain equivalent. |
| S: semantic style | Resolved types/bindings/conversions/effects are needed. Recheck the transformed program, not just whether it parses. |
| G: grammar-dependent | The requested output is not currently accepted in the relevant context, or acceptance remains unverified. Resolve the language question first. |

### Braces, bodies and layout

| Option family | Suggested choices and scope | Class / status |
| --- | --- | --- |
| Brace placement | `preserve`, `attach`, `next_line`, optionally `shifted`; default plus overrides for functions, types, controls and initializers | L; new policy, some forms already preserved |
| Following keyword | `same_line`, `next_line`, `preserve` for `else` and any other supported continuation keyword | L; tested `else` currently joins its preceding brace |
| Empty blocks | Preserve, collapse or expand, optionally by construct | L; global control exists |
| Short bodies | `expand`, `preserve`, `inline_if_fits`, separately for functions, branches, loops and case arms | L; refine existing global control |
| Inline-body limits | Maximum statements and width for a compact body; prohibit compacting bodies with attached line comments or nested controls unless explicitly supported | L; new |
| Brace presence | Preserve or require everywhere; omit only where the grammar permits, with consistency across all `if`/`else` arms | T/G; unbraced `if`/`while` need grammar work |
| Simple-statement placement | Same line as the header versus next line, independently of whether braces exist | L/G; depends on the chosen body form |

Rider, dprint and clang-format all make distinctions in this area. The proposal
should not collapse these into a boolean named `compact_braces`.
[Rider body syntax][rider-optional-braces], [dprint brace presence][dprint-presence],
[clang-format short constructs][clang22].

### Wrapping and horizontal arrangement

| Option family | Suggested choices and scope | Class / status |
| --- | --- | --- |
| Author breaks | Preserve or canonicalize by construct; explain how this interacts with `preferred_width=0` | L; refine current behavior |
| List layout | Fit, pack, one item per line when wrapped, always vertical or preserve; separate call arguments, parameters, arrays, records and tuples | L; new detail over existing width wrapping |
| List limits | Maximum items per line and optional per-construct width thresholds | L; new |
| Hanging layout | Align to first item, fixed continuation indent, or block indent; closing delimiter on last-item line or its own line | L; refine existing alignment |
| Binary breaks | Leading, trailing or preserve; break each operator or at precedence-group boundaries | L; parser-validated contexts |
| Member/call chains | Fit, one call/member per line, or preserve; define treatment of multiline calls inside a chain | L; new |
| Declaration signatures | Parameter-list versus return-type break priority; attributes inline or one per line | L; new |
| Return/assignment expressions | Continuation placement and alignment for long right-hand sides, independent of list layout | L; new |

Separate fit-dependent wrapping from unconditional vertical layout. Also expose
the difference between preserving an existing trailing comma as a wrapping
signal and inserting a new comma. The former exists; the latter is a token
rewrite. Per-construct thresholds and preserve-versus-canonical choices have
prior art in [rustfmt][rustfmt], [Swift-format][swift-format] and [Prettier][prettier-options].

### Whitespace for readability

| Option family | Suggested choices and scope | Class / status |
| --- | --- | --- |
| Operator spacing | Uniform spaces, preserve, or a profile by arithmetic/additive/comparison/logical/assignment/bitwise/range operator family | L; new |
| Precedence-oriented spacing | For example `a*b + c*d`; optionally wider separation between outer groups, with an explicit fixed rule rather than subjective cleverness | L; experimental proposal |
| Keyword/call spacing | Spaces before control parentheses, function-call parentheses and declaration parameter lists, independently | L; new |
| Delimiter interiors | Separate nonempty/empty round, square and record delimiters; distinguish a record literal from a statement block | L; current booleans are coarse |
| Colon/arrow spacing | Declaration types, named arguments, field values, case labels and return arrows need separate syntax contexts | L; new |
| Unary/postfix spacing | Optional spacing where lexically legal; never merge or split operators while compacting | L; new, conservative |
| Block padding | Remove, preserve or add a bounded blank line just inside nonempty blocks, optionally by construct | L; extends current preservation control |
| Declaration and member gaps | Minimum blank lines around functions, types and groups of members, separately from the maximum preserved count | L; extends current declaration-gap control |
| Statement gaps | Deterministic rules before a final return, after control transfers, or around multiline control statements | L; new |
| Comment and import gaps | Preserve attached documentation, separate import groups and control blank lines before standalone comments | L; new, no sorting implied |
| Hand-arranged data | Preserve or align explicit rows in nested arrays/record tables; provide an ignore-next or region escape hatch | L; bounded, opt-in |

Uncrustify and Rider provide direct operator-family spacing precedent. Extra
variable-width spaces derived from arbitrary expression precedence are an
original Chibi proposal, not a claimed feature of clang-format. JuliaFormatter's
preserved matrix spacing and dprint's block padding show that readability can
include intentional whitespace rather than only minimizing it.
[Uncrustify][uncrustify], [Rider spaces][rider-spaces], [Julia matrices][julia-options],
[dprint definitions][dprint-types].

Do not infer semantic paragraphs from names such as `setup` or `cleanup`.
Prefer rules tied to syntax kinds, and preserve a developer's existing grouping
when the configured rule does not require a change.

### Alignment

The five existing column-alignment switches are a useful start. They should
share a consistent grouping policy before more alignment targets are added.

| Option family | Suggested choices and scope | Class / status |
| --- | --- | --- |
| Alignment mode | Off, preserve manual columns, or align consecutive rows, independently for assignments, types, constants, record values and comments | L; extend current booleans |
| Group boundaries | Same AST scope/indent only; stop at blank lines and standalone comments by default, with explicit opt-ins to cross them | L; new |
| Padding and margin limits | Maximum added padding and target column; avoid one long identifier pushing an entire group out of view | L; new |
| Compound operators | Align operator starts or the assignment marker; optionally pad different-width assignment operators consistently | L; new |
| Multiline alignment | Arguments, operands, chained calls, trailing return types and continuation rows | L; extend current controls |
| Tab fill | Tabs for indentation with spaces for alignment, or an explicit alternative; separate tab display width from indentation width | L; new detail |
| Data columns | Left/right alignment by explicit column in an array-of-records or nested-array table, with a maximum padding budget | L; new |
| Partial formatting | Expand to the whole alignment group or leave neighboring lines untouched; report the actual affected range | Workflow; must be explicit |

Rider documents distant-alignment and selection-expansion controls. Clang-format
supports group boundaries and compound-operator padding. odinfmt separately
aligns fields, values and constants. These support bounded group policies rather
than alignment across an entire function.
[Rider alignment][rider-align], [clang-format alignment][clang22], [odinfmt][odinfmt].

### Syntax preferences and types

| Option family | Suggested choices and scope | Class / status |
| --- | --- | --- |
| Control-header parentheses | Preserve, always or omit where optional; independently for supported `if`, `while`, `for` and `switch` headers | T; verify each grammar context |
| Expression parentheses | Preserve, remove redundant grouping, or add grouping to clarify selected operator families | T; normalized expression-tree equivalence |
| Parenthesis detail | Preserve readability groups, flatten repeated groups, and choose whether same-precedence mixed operators should be made explicit | T; no algebraic reassociation |
| Trailing commas | Preserve, add to multiline lists, or remove where optional; scope by list kind | T; new |
| Semicolons | Preserve, omit optional or always, with protected required separators | T; already exists |
| Local type annotations | Preserve, omit redundant, explicit, or inferred only when evident | S; new |
| Type contexts | Separate locals, constants, parameters, return types, members and public API; only offer modes legal in each declaration form | S/G; new |
| Type spelling | Preserve aliases, shortest unambiguous spelling, or qualified spelling; define a policy for unnameable/anonymous types | S; new |
| Imports | Preserve, sort within existing groups, or organize/remove unused imports | T/S; establish ordering and initialization semantics first |
| Literal spelling | Optional numeric digit grouping, exponent/hex casing, escape normalization and equivalent quote forms | T/S/G; only supported grammar forms with exact value/byte preservation |
| Other declaration forms | Expression/block bodies, qualification and modifier normalization only if Chibi has equivalent legal forms | S/G; lower priority, not imported wholesale from C# |

For type omission, borrow the constrained idea from Rider and Clang-Tidy, not a
textual rule that deletes every annotation. Biome likewise keeps some apparently
redundant annotations because their wider type is intentional.
[Rider inferred types][rider-var], [Clang-Tidy inference][clang-auto], [Biome type rule][biome-types].

For example, Rider's actual `csharp_for_built_in_types`,
`csharp_for_simple_types` and `csharp_for_other_types` settings offer `use_var`,
`use_var_when_evident` and `use_explicit_type`. The proposed Chibi policy is
similarly context-specific, without copying C#'s type categories blindly.
[Rider syntax schema][rider-syntax].

An explicit `i8` annotation can provide numeric context that disappears if it is
removed. A type annotation can also influence overload selection, union/callable
interpretation or conversions. Inserting an inferred type needs a valid,
accessible source spelling and must preserve constness, storage, visibility and
ABI. Recheck both the inferred type and the selected operations/effects.
Chibi's mutable and constant declarations have different spellings; a style
rewrite must not turn one into the other.
[Local declarations](../tools/compiler/source_parser/Statements.chibi#L153),
[constant declarations](../tools/compiler/source_parser/Declarations.chibi#L302),
[numeric storage context](../tools/compiler/package_compiler/Scalar.chibi#L2200).

Define evident mechanically, such as a constructor whose written name uniquely
identifies the resolved result type. Do not use capitalization as a proxy. For
missing imports, erroneous drafts, unresolved generics or unnameable types,
preserve the declaration and show what was skipped. The playground should not
claim that an explicit-types policy fully applied when it could not resolve the
program.

## Examples of the proposed styles

These illustrate independent choices. They do not establish implementation or
semantic equivalence for every similar-looking program.

Brace placement, with braces retained:

```chibi
Update :: () {
    if ready {
        Step();
    }
}
```

```chibi
Update :: ()
{
    if ready
    {
        Step();
    }
}
```

Whitespace by operator family, with the same operator tree:

```chibi
distance := dx * dx + dy * dy;
distance := dx*dx + dy*dy;
```

Alignment within a bounded group:

```chibi
position: f32 = 0;
speed: f32 = 12;

position: f32 = 0;
speed:    f32 = 12;
```

The second pair is an alternative rendering, not additional declarations to
paste alongside the first. The blank line represents a group boundary in this
example; it should not be crossed accidentally by a whole-function aligner.

Type preferences, subject to semantic checks:

```chibi
count := CountItems();
count: int = CountItems();
```

These are alternatives only when the resolved return type is exactly `int` and
the annotation does not change the selected operation. This example does not
justify replacing `small: i8 = 1` with an unconstrained inferred declaration.

Parentheses for readability, without changing association:

```chibi
accepted := ready && fast || forced;
accepted := (ready && fast) || forced;
```

Removing or adding expression groups needs a normalized tree comparison.
Reassociating floating-point arithmetic, short-circuit operations or effectful
calls is outside formatting. Lua's optional call parentheses and JavaScript's
single-arrow-parameter rules are not evidence that Chibi call parentheses can
be omitted. [StyLua grammar-specific rule][stylua], [Prettier arrow scope][prettier-options].

## Profiles, interactions and safeguards

Offer a small set of previewable profiles such as current, compact, expanded
and tabular, with the effective options visible. These are proposed Chibi
profiles, not promises to reproduce a foreign formatter byte-for-byte. Pin a
profile version so future defaults do not silently reformat a repository.
Explicit overrides should win over a profile, and unknown/unsupported options
should produce a diagnostic. EditorConfig can supply common indentation and
file-ending preferences, but its core standard does not define the whole
language-style vocabulary. [EditorConfig specification][editorconfig].

Keep precedence rules explicit:

1. Apply permitted type-aware rewrites, then syntax normalization, then layout,
   wrapping and alignment. Reanalyze after a rewrite that invalidates facts.
2. Let a declared compact-body policy decide whether a brace-placement rule
   applies to that body. Do not let two switches fight on successive runs.
3. Decide wrapping from the final spelling and the alignment padding budget.
   Alignment must not repeatedly force a wrap that later removes alignment.
4. Comments, documentation, formatter directives and blank-line group boundaries
   remain attached to their original constructs unless a specifically enabled
   comment transform owns the change.
5. Preserve defaults until an option or named profile asks for a different
   behavior. Preserve cannot mean keeping invalid syntax or ignoring a stronger
   explicitly configured rule.
6. Compact spacing must retain every lexically required separator. In particular,
   do not join adjacent symbols into a different operator or a comment opener.

Every supported combination must reach a fixed point after one application.
Do not hide oscillating rules behind repeated formatting passes. Prettier's
preservation heuristics and JuliaFormatter's transform caveats are useful reasons
to define this contract early. [Prettier rationale][prettier-rationale],
[JuliaFormatter transformations][julia-options].

Validation should include:

- Exact tokens and implicit terminators for layout-only changes.
- A narrowly normalized syntax tree for each permitted punctuation, parenthesis
  or brace rewrite, plus comment/directive ownership.
- Type, binding, conversion, effect and public-signature equivalence for typed
  rewrites. Merely compiling both programs is insufficient.
- `format(format(source, options), options) == format(source, options)` across
  profile/override combinations and boundary widths.
- Tabs, Unicode, comments, raw literal bytes, partial drafts and range formatting.
- Counterexamples involving numeric context, aliases, shadowed names, nested
  conditionals, short-circuit expressions and temporary/storage lifetimes.

Comment reflow deserves its own later option family. It changes comment bytes,
can damage Markdown examples or directives, and should distinguish prose from
code blocks, URLs and deliberate tables. Literal reflow has a similarly stronger
contract. The existing byte-preservation guarantee should remain the default.

## Suggested implementation order

| Batch | Scope | Why |
| --- | --- | --- |
| 1. Layout breadth | Brace placement, following-keyword placement, per-construct short bodies, list layout, break side, operator-family spacing, bounded alignment and deterministic blank-line rules | Closest to the current parser/formatter contract and directly addresses house-style preferences. |
| 2. Checked syntax | Optional control parentheses, redundant expression groups and trailing commas; range formatting with declared alignment scope | Needs rewrite-specific normalized-tree checks and comment tests. |
| 3. Type-aware style | Preserve/omit-redundant/explicit, context policies, type spelling and honest skipped-site diagnostics | Requires semantic facts and a printer for resolved types. This is a valuable separate capability, not a reason to delay layout work. |
| 4. Language-dependent forms | Braceless `if`/`while`, new expression-body forms or new literal syntaxes | Evaluate as language proposals first. Do not add syntax solely to imitate a formatter from another language. |

For a first iteration, I would prioritize brace placement, short-body scope,
list wrapping, alignment limits, operator-family spacing and vertical gaps.
Alongside that, specify the type-aware rewrite contract and the meaning of
evident. That gives the playground visible new choices without offering type
transformations it cannot yet validate.

## Primary sources

The links identify the documents used on the research date. Vendor main/latest
pages can change. Clang 22.1 is the released reference; development-only behavior
is called out above rather than silently treated as released behavior.

[rider-cleanup]: https://www.jetbrains.com/help/rider/Settings_Code_Cleanup.html
[rider-var]: https://www.jetbrains.com/help/rider/Using_var_Keyword_in_Declarations.html
[rider-syntax]: https://www.jetbrains.com/help/rider/EditorConfig_CSHARP_CSharpCodeStylePageImplSchema.html
[rider-braces]: https://www.jetbrains.com/help/rider/EditorConfig_CSHARP_BracesPageSchema.html
[rider-optional-braces]: https://www.jetbrains.com/help/rider/Braces_for_Single_Line_Statements.html
[rider-wrap]: https://www.jetbrains.com/help/rider/EditorConfig_CSHARP_LineBreaksPageSchema.html
[rider-align]: https://www.jetbrains.com/help/rider/EditorConfig_CSHARP_CSharpIndentStylePageSchema.html
[rider-spaces]: https://www.jetbrains.com/help/rider/EditorConfig_CSHARP_SpacesPageSchema.html
[rider-blank]: https://www.jetbrains.com/help/rider/EditorConfig_CSHARP_BlankLinesPageScheme.html
[clion]: https://www.jetbrains.com/help/clion/settings-code-style-c-cpp.html
[clang22]: https://releases.llvm.org/22.1.0/tools/clang/docs/ClangFormatStyleOptions.html
[clang-dev]: https://clang.llvm.org/docs/ClangFormatStyleOptions.html
[clang-auto]: https://clang.llvm.org/extra/clang-tidy/checks/modernize/use-auto.html
[clang-tidy-braces]: https://clang.llvm.org/extra/clang-tidy/checks/readability/braces-around-statements.html
[prettier-options]: https://prettier.io/docs/options
[prettier-philosophy]: https://prettier.io/docs/option-philosophy
[prettier-rationale]: https://prettier.io/docs/rationale
[dprint-brace]: https://docs.rs/dprint-plugin-typescript/latest/dprint_plugin_typescript/configuration/enum.BracePosition.html
[dprint-presence]: https://docs.rs/dprint-plugin-typescript/latest/dprint_plugin_typescript/configuration/enum.UseBraces.html
[dprint-next]: https://docs.rs/dprint-plugin-typescript/latest/dprint_plugin_typescript/configuration/enum.NextControlFlowPosition.html
[dprint-types]: https://github.com/dprint/dprint-plugin-typescript/blob/main/src/configuration/types.rs
[biome-config]: https://biomejs.dev/reference/configuration/
[biome-types]: https://biomejs.dev/linter/rules/no-inferrable-types/javascript/
[biome-braces]: https://biomejs.dev/linter/rules/use-block-statements/javascript/
[rustfmt]: https://github.com/rust-lang/rustfmt/blob/main/Configurations.md
[swift-format]: https://github.com/swiftlang/swift-format/blob/main/Documentation/Configuration.md
[stylua]: https://github.com/JohnnyMorganz/StyLua/blob/main/README.md
[odinfmt]: https://github.com/DanielGavin/ols/blob/master/misc/odinfmt.schema.json
[uncrustify]: https://github.com/uncrustify/uncrustify/blob/master/documentation/htdocs/default.cfg
[julia-align]: https://github.com/JuliaEditorSupport/JuliaFormatter.jl/blob/main/docs/src/custom_alignment.md
[julia-options]: https://github.com/JuliaEditorSupport/JuliaFormatter.jl/blob/main/docs/src/formatting_options.md
[editorconfig]: https://spec.editorconfig.org/
