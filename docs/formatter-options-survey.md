# Formatter options survey

A catalog of the configuration options that established formatters offer, to
choose what `tools/compiler/source_format` should support. Gathered
2026-10-30 from each tool's own documentation, schema or source. Where a
document did not state a name, value or default, the cell says "unverified".

Classes, by what the safety check in `docs/source-formatting.md` would have to
accept:

- **W**: whitespace only; the token stream is unchanged.
- **T**: changes tokens but not meaning (trailing commas, explicit semicolons,
  parentheses, literal spelling).
- **R**: reorders code (import sorting, member ordering).
- **C**: rewrites comment or string bytes.

## Sources

Abbreviations used in the tables are in brackets.

| Formatter | Document read | Version or date |
| --- | --- | --- |
| Prettier [Pr] | https://prettier.io/docs/options, /docs/ignore, /docs/rationale | Page names options up to v3.6.0; local CLI 3.9.6 |
| Black [Bl] | https://black.readthedocs.io/en/stable/usage_and_configuration/the_basics.html, .../the_black_code_style/current_style.html, .../future_style.html | Black 26.10.1 |
| clang-format [CF] | https://clang.llvm.org/docs/ClangFormatStyleOptions.html, https://clang.llvm.org/docs/ClangFormat.html | Trunk docs (options tagged up to clang-format 23); defaults from local `clang-format 23.1.1 -style=llvm -dump-config` |
| NArrange [NA] | Official site www.narrange.net is a parked domain (redirects to a lander page), so it could not be read. Used the project page https://sourceforge.net/projects/narrange/ and `DefaultConfig.xml` from the fork https://github.com/OscheibeSymbio/narrange | Assemblies 0.3.1 in that config; NuGet lists 0.3.5 |
| Artistic Style [AS] | https://astyle.sourceforge.net/astyle.html | Page states no version; news page lists 3.6 (Aug 2024) as latest |
| Atom Beautify [AB] | https://github.com/Glavin001/atom-beautify/blob/master/docs/options.md (first 100 KB of 440 KB read) | No version in doc. GitHub does not flag the repo archived (API, 2026-10-30); its README asks for a maintainer and points to Unibeautify |
| Uncrustify [Un] | https://github.com/uncrustify/uncrustify/blob/master/documentation/htdocs/default.cfg | Uncrustify 0.83.0 |
| Unibeautify [UB] | https://unibeautify.com/docs/language-javascript.html; keys and defaults from https://github.com/Unibeautify/unibeautify/blob/master/src/options.ts | Site footer 2020 |
| gofmt [Go] | https://pkg.go.dev/cmd/gofmt; `go/printer` source for blank lines and import sorting | go1.27.3 page; local go1.27.1 source |
| rustfmt [RF] | https://rust-lang.github.io/rustfmt/ renders by script; read https://github.com/rust-lang/rustfmt/blob/master/Configurations.md and local `rustfmt --print-config default` | rustfmt 1.9.0 |
| zig fmt [Zig] | https://ziglang.org/documentation/master/ (Style Guide); FAQ entry "Why does zig fmt have no configuration options?" at https://github.com/ziglang/zig/wiki/FAQ, located by search, text not read | master (0.17 development) |
| odinfmt [OF] | https://github.com/DanielGavin/ols README, `misc/odinfmt.schema.json`, `src/odin/printer/printer.odin` | ols main, last commit 2026-10-29 |
| dart format [Da] | https://dart.dev/tools/dart-format; https://github.com/dart-lang/dart_style/blob/main/CHANGELOG.md | Dart 3.13.3, page updated 2026-05-12 |
| swift-format [SF] | https://github.com/swiftlang/swift-format/blob/main/Documentation/Configuration.md, IgnoringSource.md | main branch |
| ktlint [KT] | https://ktlint.github.io/ktlint/latest/rules/configuration-ktlint/ (the old pinterest.github.io URL returns 404) and the standard rules page | ktlint 1.8.0 |
| dprint TypeScript [DP] | https://dprint.dev/plugins/typescript/config/ renders by script; read the schema https://plugins.dprint.dev/dprint/dprint-plugin-typescript/latest/schema.json | dprint-plugin-typescript 0.96.1 |
| Biome [Bi] | https://biomejs.dev/reference/configuration/, https://biomejs.dev/formatter/ | v2.6.4 |
| Ruff [Ru] | https://docs.astral.sh/ruff/settings/, /formatter/, /configuration/ | No version stated; examples cite v0.17.3 |
| EditorConfig [EC] | https://spec.editorconfig.org/; https://github.com/editorconfig/editorconfig/wiki/EditorConfig-Properties | Spec 0.17.2 |

All 19 formatters were covered. Partial reads: NArrange (official docs
unreachable, catalog taken from its shipped configuration), Atom Beautify
(options list truncated, defaults mostly unverified), zig fmt (FAQ answer not
read). Formatters with no options at all: gofmt and zig fmt; dart format has
only page width and trailing-comma handling.

## Catalog

Default values are each tool's own default (clang-format: LLVM style).
"fixed" means the behavior is built in with no option.

### Line width and wrapping

| Concept | Option names | Values and defaults | Class | Chibi? |
| --- | --- | --- | --- | --- |
| Line width | Pr `printWidth`; Bl `line-length`; CF `ColumnLimit`; AS `--max-code-length=#`; AB/UB `wrap_line_length`; Un `code_width`; RF `max_width`; OF `character_width`; Da `page_width` (also `// dart format width=N`); SF `lineLength`; KT/EC `max_line_length`; DP `lineWidth`; Bi `lineWidth`; Ru `line-length` | Pr 80, Bl 88, CF 80 (0 = no limit), AS off (50 to 200), Un 0, UB 80, RF 100, OF 100, Da 80, SF 100, KT 140 / off / 100 by code style, DP 120, Bi 80, Ru 88. Go and Zig: none. EC: not in the core spec, a wiki "universal" property | W | Yes, with note: a break is legal only where it adds no implicit terminator (after `(`, `[`, `{`, `,` or a binary operator). Archived C formatter had `preferred_width`, 0 = keep author breaks |
| Per-construct width heuristics | RF `use_small_heuristics`, `fn_call_width`, `struct_lit_width`, `array_width`, `chain_width`, `single_line_if_else_max_width`; KT `ktlint_function_signature_rule_force_multiline_when_parameter_count_greater_or_equal_than` | RF Default, 60, 18, 60, 60, 50 | W | Later, only once width wrapping exists |
| Keep author line breaks | SF `respectsExistingLineBreaks`; Pr `objectWrap`; Bi `expand`; OF `multiline_composite_literals`; DP `preferSingleLine`; Un `nl_func_leave_one_liners`, `nl_if_leave_one_liners` | SF true; Pr preserve/collapse (preserve: a newline after `{` keeps an object multi-line); Bi auto/always/never (auto); OF false; DP false | W | Yes: this is today's behavior |
| Bin-pack vs one item per line | CF `PackArguments`, `PackParameters` (clang-format 23; replace deprecated `BinPackArguments`, `BinPackParameters`), `AllowAllArgumentsOnNextLine`; RF `fn_params_layout`; SF `lineBreakBeforeEachArgument`; DP `*.linePerExpression` | CF BinPack/OnePerLine/UseBreakAfter (BinPack); RF Tall/Compressed/Vertical (Tall); SF false | W | Yes with width wrapping |
| Magic trailing comma (an existing trailing comma keeps one item per line) | Bl `skip-magic-trailing-comma`; Ru `skip-magic-trailing-comma`; Da `trailing_commas: preserve`; Zig fixed | Bl, Ru false (comma honored); Da off by default (dart_style 3.1.0); Zig always honored | W (reads the comma, adds nothing) | Yes, as a signal for width wrapping |
| Binary operator break position | CF `BreakBeforeBinaryOperators`, `BreakBinaryOperations`; RF `binop_separator`; Pr `experimentalOperatorPosition`; DP `operatorPosition`; Bi `operatorLinebreak`; Un `pos_arith`, `pos_bool`; AS `--break-after-logical`; Bl fixed (before operator) | CF None/NonAssignment/All (None); RF Front/Back (Front); Pr start/end (end); DP maintain/sameLine/nextLine (nextLine); Bi after/before (after); Un ignore | W | No: a line break before an operator terminates the statement, so the operator must end the line |
| Break long string literals | CF `BreakStringLiterals`; Bl unstable `string_processing`; RF `format_strings` | CF true; RF false (unstable) | T/C | No: one string token, copied exactly |
| Method chain breaking | RF `chain_width`; AB `break_chained_methods`; KT `ktlint_chain_method_rule_force_multiline_when_chain_operator_count_greater_or_equal_than`; DP `memberExpression.linePerExpression` | RF 60; DP false | W | With note: only where the break adds no terminator |

### Indentation and continuation

| Concept | Option names | Values and defaults | Class | Chibi? |
| --- | --- | --- | --- | --- |
| Indent width | Pr `tabWidth`; CF `IndentWidth`; AS `--indent=spaces=#`; AB/UB `indent_size`; Un `indent_columns`; RF `tab_spaces`; OF `spaces`; SF `indentation` `{spaces: N}`; KT/EC `indent_size`; DP `indentWidth`; Bi `indentWidth`; Ru `indent-width`; NA `Tabs SpacesPerTab` | Pr 2, CF 2, AS 4, UB 2, Un 8, RF 4, OF 4, SF 2, DP 2, Bi 2, Ru 4, NA 4. Fixed: Go tab, Zig 4, Da 2 | W | Yes (exists: `indent_width`) |
| Tabs or spaces | Pr `useTabs`; CF `UseTab`; AS `--indent=tab`, `--indent=force-tab`; AB `indent_with_tabs`, `indent_char`; Un `indent_with_tabs`; UB `indent_style`; RF `hard_tabs`; OF `tabs`; SF `indentation` `{tabs: N}`; KT/EC `indent_style`; DP `useTabs`; Bi `indentStyle`; Ru `indent-style`; NA `Tabs Style` | Mostly spaces. Exceptions: Go tabs (fixed), OF true, Bi tab, Un 1 (tabs for indent). CF Never/ForIndentation/ForContinuationAndIndentation/AlignWithSpaces/Always (Never) | W | Yes (exists: `use_tabs`) |
| Tab display width | CF `TabWidth`; Un `input_tab_size`, `output_tab_size`; OF `tabs_width`; SF `tabWidth`; EC `tab_width` | CF 8, Un 8, OF 4, SF 8, EC = `indent_size` | W | With note: today `indent_width` doubles as tab width |
| Continuation indent | CF `ContinuationIndentWidth`; AS `--indent-continuation=#`, `--max-continuation-indent=#`, `--min-conditional-indent=#`; Un `indent_continue`; EC wiki `continuation_indent_size` (proposed only) | CF 4 columns; AS 1, 40, 2; Un 0 | W | Yes: today one level when the bracket ends its line |
| Align after open bracket vs block indent | CF `AlignAfterOpenBracket` (boolean since clang-format 22; was Align/DontAlign/AlwaysBreak/BlockIndent), `BreakAfterOpenBracket*`, `BreakBeforeCloseBracket*`; RF `indent_style`; Un `indent_align_paren`, `indent_paren_nl`; AS `--indent-after-parens`; DP `*.preferHanging` | CF true, break options false; RF Block/Visual (Block, unstable); Un true; DP false / never | W | Yes: today aligns under the first item after the bracket |
| Align continued operands | CF `AlignOperands`; Un `indent_bool_paren` | CF DontAlign/Align/AlignAfterOperator (Align); Un 0 | W | Yes: today aligns with the first operand |
| Indent case labels | CF `IndentCaseLabels`, `IndentCaseBlocks`; AS `--indent-switches`, `--indent-cases`; Un `indent_switch_case`; OF `indent_cases`; SF `indentSwitchCaseLabels`; RF `match_arm_indent` | CF false, false; Un 0; OF false; SF false; RF true (unstable). Go fixed: not indented | W | Yes: today indented one level |
| Labels, namespaces, preprocessor, access modifiers | CF `IndentGotoLabels`, `NamespaceIndentation`, `IndentPPDirectives`, `AccessModifierOffset`; AS `--indent-labels`, `--indent-namespaces`, `--indent-preproc-*`, `--indent-classes`, `--indent-modifiers`; SF `indentConditionalCompilationBlocks` | Various | W | No such constructs |
| Indent whitespace-only lines | SF `indentBlankLines`; AS `--fill-empty-lines` | SF false | W | No: conflicts with no trailing whitespace |
| Keep author indentation of arrays | AB/UB `keep_array_indentation` | unverified | W | No |

### Braces and blocks

| Concept | Option names | Values and defaults | Class | Chibi? |
| --- | --- | --- | --- | --- |
| Opening brace placement | CF `BreakBeforeBraces`, `BraceWrapping.*`; AS `--style=allman/java/kr/stroustrup/...`; AB/UB `brace_style`; Un `nl_if_brace` and other `nl_*_brace`; RF `brace_style`, `control_brace_style`; OF `brace_style`; DP `bracePosition`; EC wiki `curly_bracket_next_line`, `indent_brace_style` (proposed) | CF Attach; UB collapse/collapse-preserve-inline/expand/end-expand/none (collapse); RF SameLineWhere, AlwaysSameLine; OF _1TBS/Allman/Stroustrup/K_And_R (_1TBS); DP sameLineUnlessHanging | W | No: a newline before `{` ends the statement |
| `else` after `}` | CF `BraceWrapping.BeforeElse`; AS `--break-closing-braces`, `--break-elseifs`; Un `nl_brace_else`; RF `control_brace_style`; DP `nextControlFlowPosition`; SF `lineBreakBeforeControlFlowKeywords` | CF false; DP sameLine; SF false | W | No: `} else` must share a line |
| Short blocks on one line | CF `AllowShortBlocksOnASingleLine`, `AllowShortFunctionsOnASingleLine`, `AllowShortIfStatementsOnASingleLine`, `AllowShortLoopsOnASingleLine`, `AllowShortCaseLabelsOnASingleLine`, `AllowShortEnumsOnASingleLine`; AS `--keep-one-line-blocks`, `--keep-one-line-statements`; Un `nl_func_leave_one_liners`, `nl_if_leave_one_liners`; RF `fn_single_line`, `single_line_if_else_max_width`; OF `inline_single_stmt_case`; DP `singleBodyPosition` | CF Never, All, Never, false, false, true; Un false; RF false, 50; OF false; DP maintain | W | With note: joining or splitting may add or drop implicit terminators before `}`; today only empty blocks stay on one line |
| Empty blocks and items | CF `AllowShortRecordOnASingleLine`, `BraceWrapping.SplitEmptyFunction`, `SpaceInEmptyBraces` (replaces deprecated `SpaceInEmptyBlock`); RF `empty_item_single_line`; Un `nl_collapse_empty_body`, `sp_inside_braces_empty` | CF EmptyAndAttached, true, Never; RF true (unstable); Un false, ignore | W | Yes: today `{}` |
| Spaces inside one-line block braces | OF `space_single_line_blocks`; Un `sp_inside_braces` | OF false (`{return 0}`); Un ignore | W | Only with short blocks |
| Insert or remove braces | CF `InsertBraces`, `RemoveBracesLLVM`; AS `--add-braces`, `--add-one-line-braces`, `--remove-braces`; Un `mod_full_brace_if`; DP `useBraces`; OF `convert_do` | CF false, false; DP whenNotSingleLine; OF false | T | No, unless Chibi gains a brace-less body (unverified that none exists) |

### Spacing

| Concept | Option names | Values and defaults | Class | Chibi? |
| --- | --- | --- | --- | --- |
| Around binary and assignment operators | Un `sp_arith`, `sp_assign`, `sp_bool`, `sp_compare`; AS `--pad-oper`; CF `SpaceBeforeAssignmentOperators`; DP `binaryExpression.spaceSurroundingBitwiseAndArithmeticOperator`; RF `spaces_around_ranges`; SF `spacesAroundRangeFormationOperators`; EC wiki `spaces_around_operators` (proposed) | Un ignore; CF true; DP true; RF false; SF false. Fixed one space in Go (by precedence), Zig, Bl, Pr | W | Yes, fixed one space; no option needed |
| Inside parentheses | CF `SpacesInParens`, `SpacesInParensOptions`; AS `--pad-paren=in/out/none`, `--pad-first-paren-out`, `--pad-empty-paren`; Un `sp_inside_paren`, `sp_inside_sparen`, `sp_paren_paren`; AB/UB `space_in_paren`, AB `space_in_empty_paren`; DP `*.spaceAround` | CF Never; UB false; DP false | W | Yes, fixed none |
| Before `(` after keyword or name | CF `SpaceBeforeParens`, `SpaceBeforeParensOptions`; AS `--pad-header`; Un `sp_before_sparen`; AB/UB `space_after_anon_function`, AB `space_before_conditional`; DP `ifStatement.spaceAfterIfKeyword`, `functionDeclaration.spaceBeforeParentheses` | CF ControlStatements; DP true, false | W | Yes, fixed: `if (x)`, `f(x)` |
| Square brackets | CF `SpacesInSquareBrackets`, `SpaceBeforeSquareBrackets`; AS `--pad-brackets`; Un `sp_inside_square`; DP `arrayExpression.spaceAround` | CF false, false; DP false | W | Yes, fixed: `[1, 2]` |
| Inside braces of literals | Pr `bracketSpacing`; Bi `bracketSpacing`; UB `object_curly_spacing`; AB `object_curly_spacing`; CF `Cpp11BracedListStyle`, `SpacesInContainerLiterals`; Un `sp_inside_braces_struct`; DP `spaceSurroundingProperties`; EC wiki `spaces_around_brackets` (proposed) | Pr true, Bi true, UB true (`{ a: 1 }`); CF Block/FunctionCall/AlignFirstComment (AlignFirstComment), true; DP true | W | Yes: today `{a: 1}` |
| Commas | AS `--pad-comma`; Un `sp_before_comma`, `sp_after_comma` | Un remove, ignore. Fixed elsewhere | W | Yes, fixed: none before, one after |
| Colons | RF `space_before_colon`, `space_after_colon`; OF `spaces_around_colons`; DP `typeAnnotation.spaceBeforeColon`; CF `SpaceBeforeCaseColon`, `BitFieldColonSpacing`, `SpaceBeforeRangeBasedForLoopColon`; Un `sp_before_case_colon`, `sp_cond_colon` | RF false, true (unstable); OF false (`foo: bar`); DP false; CF false, Both, true | W | Yes, fixed: `a: int`, `case .A:`; `::`, `:=` spaced |
| Semicolons | AS `--pad-semicolon=none`; Un `sp_before_semi`; DP `forStatement.spaceAfterSemiColons` | Un remove; DP true | W | Yes, fixed none before |
| Pointers, unary, casts | CF `PointerAlignment`, `SpaceAfterLogicalNot`, `SpaceAfterCStyleCast`; AS `--align-pointer`, `--pad-negation`; Un `sp_after_type` | CF Right, false, false | W | No: fixed prefix syntax |
| Space after `//` | CF `SpacesInLineCommentPrefix`; DP `commentLine.forceSpaceAfterSlashes`; Un `sp_cmt_cpp_start`; Bl fixed (space after `#`) | CF Minimum 1, Maximum -1; DP true | C | No: comment bytes are copied |
| Trailing whitespace | EC `trim_trailing_whitespace`; UB `remove_trailing_whitespace`; AS `--squeeze-ws` | UB false. Fixed removal in Go, RF, Pr, Bl | W | Yes, fixed removal |

### Blank lines

| Concept | Option names | Values and defaults | Class | Chibi? |
| --- | --- | --- | --- | --- |
| Maximum consecutive blank lines | CF `MaxEmptyLinesToKeep`; RF `blank_lines_upper_bound`; SF `maximumBlankLines`; OF `newline_limit`; AB/UB `max_preserve_newlines`, AB `preserve_newlines`; Un `nl_max`, `nl_max_blank_in_func`; AS `--squeeze-lines=#`, `--delete-empty-lines`; NA `LineSpacing RemoveConsecutiveBlankLines` | CF 1; RF 1 (unstable); SF 1; OF 2 newlines (one blank line); UB 10; Un 0 (no change); NA true. Fixed: Go 1 (`maxNewlines = 2`), Pr 1, Bl 1 in functions and 2 at module level | W | Yes (exists: `max_blank_lines`, 0 to 3) |
| Blank lines between declarations | CF `SeparateDefinitionBlocks`; RF `blank_lines_lower_bound`; AS `--break-blocks`, `--line-between-members`; Un `nl_after_func_body`, `nl_before_func_body_def`; DP `enumDeclaration.memberSpacing`; Bl fixed (2 around top-level definitions) | CF Leave/Always/Never (Leave); RF 0 (unstable); Un 0; DP maintain | W | Yes, default leave |
| At block start and end | CF `KeepEmptyLines.AtStartOfBlock`, `.AtEndOfFile`, `.AtStartOfFile`; Un `eat_blanks_after_open_brace`, `eat_blanks_before_close_brace`; KT rules `no-empty-first-line-in-class-body`, `no-blank-line-before-rbrace` | CF true, false, true; Un false, false | W | Yes: today kept, up to the maximum |
| Between struct fields | OF `preserve_struct_blank_lines` | OF false | W | Yes: today kept, up to the maximum |
| Inside brackets | CF `RemoveEmptyLinesInUnwrappedLines`; Bl fixed (removed inside parentheses) | CF false | W | With note: today kept |
| After imports, import groups | CF `IncludeBlocks`; RF `group_imports`; Ru `lint.isort.lines-after-imports` (via lint, default unverified) | CF Preserve/Merge/Regroup (Preserve); RF Preserve/StdExternalCrate/One (Preserve) | W/R | With note: only alongside import sorting |

### Alignment

| Concept | Option names | Values and defaults | Class | Chibi? |
| --- | --- | --- | --- | --- |
| Consecutive assignments | CF `AlignConsecutiveAssignments` (`Enabled`, `AcrossEmptyLines`, `AcrossComments`, `AlignCompound`, `PadOperators`); Un `align_assign_span`, `align_assign_thresh`; AB/UB `align_assignments` | CF Enabled false; Un 0 (off); UB false | W | Yes, default off |
| Declarations and struct fields | CF `AlignConsecutiveDeclarations`; Un `align_var_def_span`, `align_var_struct_span`; RF `struct_field_align_threshold`; OF `align_struct_fields`, `align_struct_declarations`; Go fixed (field types aligned) | CF false; Un 0; RF 0 (unstable); OF true, false | W | Yes, default off |
| Constant definitions | OF `align_constant_definitions`; CF `AlignConsecutiveMacros`; Go fixed (const and var blocks aligned) | OF false; CF false | W | Yes, default off: `Name: int : 4;` runs |
| Enum and union members | RF `enum_discrim_align_threshold`; Un `align_enum_equ_span`; CF `AlignConsecutiveAssignments.EnumAssignments` | RF 0 (unstable); Un 0; CF false | W | With note: union variants with payloads |
| Values in struct literals and arrays of records | OF `align_struct_values`; CF `AlignArrayOfStructures`; Un `align_struct_init_span` | OF true; CF None/Left/Right (None); Un 0 | W | Yes, default off |
| Trailing comments | CF `AlignTrailingComments` (`Kind`, `OverEmptyLines`); Un `align_right_cmt_span`; OF `align_comments`; Go fixed (aligned) | CF Always, 0; Un 0; OF false | W | Yes, default off |
| Short case statements | CF `AlignConsecutiveShortCaseStatements` | CF false | W | Only with one-line cases |
| Tabs in alignment | Un `align_with_tabs`, `align_on_tabstop`, `align_keep_tabs`; CF `UseTab: AlignWithSpaces` | Un false | W | No: alignment stays spaces so any tab width works |

### Comments

| Concept | Option names | Values and defaults | Class | Chibi? |
| --- | --- | --- | --- | --- |
| Spaces before a trailing comment | CF `SpacesBeforeTrailingComments`; SF `spacesBeforeEndOfLineComments`; Un `sp_before_tr_cmt`, `sp_endif_cmt`; Bl fixed | CF 1; SF 2; Un ignore; Bl 2 | W | Yes: today 1 |
| Reflow or wrap comments | CF `ReflowComments`; RF `wrap_comments`, `comment_width`; Un `cmt_width`, `cmt_reflow_mode` | CF Never/IndentOnly/Always (Always); RF false, 80 (unstable); Un 0, 0 | C | No |
| Normalize comment style | RF `normalize_comments`, `normalize_doc_attributes`; Un `cmt_cpp_to_c`, `cmt_star_cont`; AS `--remove-comment-prefix` | RF false; Un false | C | No |
| Indent own-line comments with code | AS `--indent-col1-comments`; AB/UB `indent_comments`; Un `cmt_indent_multi` | UB true; Un true | W (line comments), C (block comment interiors) | Yes for the comment's first line; block comment interiors stay |
| Format code inside comments | RF `format_code_in_doc_comments`; Ru `docstring-code-format`; Pr `embeddedLanguageFormatting` | RF false; Ru false; Pr auto | C | No |
| Closing comments after blocks | NA `ClosingComments Enabled`; Un `mod_add_long_function_closebrace_comment`; CF `FixNamespaceComments` | NA false; Un 0; CF true | C | No |

### Line endings and final newline

| Concept | Option names | Values and defaults | Class | Chibi? |
| --- | --- | --- | --- | --- |
| Line ending | Pr `endOfLine`; CF `LineEnding`; AS `--lineend=windows/linux/macold`; AB `end_of_line`; Un `newlines`; RF `newline_style`; OF `newline_style`; EC `end_of_line`; DP `newLineKind`; Bi `lineEnding`; Ru `line-ending` | Pr lf/crlf/cr/auto (lf); CF LF/CRLF/DeriveLF/DeriveCRLF (DeriveLF); Un auto; RF Auto/Native/Unix/Windows (Auto); OF CRLF/LF (source: LF except on Windows; schema says CRLF); EC lf/cr/crlf; DP auto/crlf/lf/system (lf); Bi lf; Ru auto/lf/cr-lf/native (auto). Zig strips CR | W | Yes (exists: `crlf`) |
| Final newline | CF `InsertNewlineAtEOF`; EC/KT `insert_final_newline`; AB/UB `end_with_newline`; Un `nl_end_of_file`, `nl_end_of_file_min` | CF false; UB false; Un ignore, 0. Fixed in Go, RF, Pr, Bl | W | Yes, fixed: exactly one |
| Encoding and BOM | EC `charset`; Un `utf8_bom`, `utf8_force`; NA `Encoding CodePage` | Un ignore; NA Detect | n/a | No |
| Form feeds | CF `KeepFormFeed` | false | W | No |

### Punctuation

| Concept | Option names | Values and defaults | Class | Chibi? |
| --- | --- | --- | --- | --- |
| Trailing commas | Pr `trailingComma`; Bi `trailingCommas`; DP `trailingCommas` (and per construct); RF `trailing_comma`, `match_block_trailing_comma`; SF `multilineTrailingCommaBehavior`, `multiElementCollectionTrailingCommas`; KT `ij_kotlin_allow_trailing_comma`, `ij_kotlin_allow_trailing_comma_on_call_site`; CF `InsertTrailingCommas`, `EnumTrailingComma`; Un `mod_enum_last_comma`; UB `end_with_comma`; Da automatic; Bl added when it splits | Pr all/es5/none (all); Bi all/es5/none (all); DP never/always/onlyMultiLine (onlyMultiLine); RF Always/Never/Vertical (Vertical, unstable), false; SF keptAsWritten, true; CF None/Wrapped (None), Leave/Insert/Remove (Leave); Un ignore; UB false | T | Yes with note: the token check must accept a comma before a closing bracket; default leave |
| Statement semicolons | Pr `semi`; DP `semiColons`; Bi `semicolons`; RF `trailing_semicolon`; CF `RemoveSemicolon`; Un `mod_remove_extra_semicolon`; UB `end_with_semicolon`; SF rule `DoNotUseSemicolons` | Pr true; DP always/prefer/asi (prefer); Bi always/asNeeded (always); RF true (unstable); CF false; Un false; UB false | T | Yes with note: explicit `;` vs the zero-width implicit terminator; default leave |
| Redundant parentheses | CF `RemoveParentheses`; RF `remove_nested_parens`; Un `mod_paren_on_return`; Bl fixed (removes optional ones); Pr `arrowParens` | CF Leave/MultipleParentheses/ReturnStatement (Leave); RF true; Un ignore | T | With note: Chibi sources write `if (x)`; default leave |
| Quote style | Pr `singleQuote`, `quoteProps`; Bl `skip-string-normalization`; Ru `quote-style`; Bi `quoteStyle`, `quoteProperties`; DP `quoteStyle`, `quoteProps`; UB `quotes`; EC wiki `quote_type` (proposed) | Pr false; Ru double/single/preserve (double); Bi double; DP alwaysDouble; UB none/double/single (none) | C | No: one string syntax |
| Numeric literal spelling | CF `IntegerLiteralSeparator`, `NumericLiteralCase`; RF `hex_literal_case`, `float_literal_trailing_zero` | CF 0 (off), Leave; RF Preserve, Preserve | T | Not now |
| Shorthand rewrites | Go `-s`, `-r 'pattern -> replacement'`; RF `use_field_init_shorthand`, `use_try_shorthand`, `condense_wildcard_suffixes` | off | T | No |

### Ordering

| Concept | Option names | Values and defaults | Class | Chibi? |
| --- | --- | --- | --- | --- |
| Sort imports | Go fixed (within blank-line groups, `ast.SortImports`); RF `reorder_imports`; OF `sort_imports`; CF `SortIncludes`; SF `orderedImports`; KT `ij_kotlin_imports_layout` (rule `import-ordering`); DP `module.sortImportDeclarations`; Bi `assist.actions.source.organizeImports` (exact key not on the page read); Un `mod_sort_import`, `mod_sort_include`; NA `Group`/`Sort` on `Element Type="Using"`; Ru not in the formatter (`ruff check --select I --fix`) | RF true; OF true; CF Enabled true; SF shouldGroupImports true; DP caseInsensitive; Un false | R | With note: the safety check must accept a permutation of whole `import` lines, comments attached |
| Sort names inside one import | DP `importDeclaration.sortNamedImports`; CF `SortUsingDeclarations`; RF `imports_granularity` | DP caseInsensitive; CF LexicographicNumeric; RF Preserve (unstable) | R | No: one package per `import` line |
| Member and declaration order | NA `Elements` with `Region Name`, `Element Type`, `ElementRef Id`, `Sort By` / `Direction`, `Group By`, `Filter Condition`; RF `reorder_modules`, `reorder_impl_items`; CF `QualifierAlignment` | NA default sorts by type, access, then name (StyleCop compatible); RF true, false (unstable); CF Leave | R | No, not now |
| Attributes | RF `merge_derives`, `inline_attribute_width`; CF `BreakAfterAttributes`; SF `lineBreakBetweenDeclarationAttributes` | RF true, 0; CF Leave; SF false | T/W | No: `@attribute` lines stay as written |

### "Format off" regions, range formatting, file selection

| Concept | Option names | Values and defaults | Class | Chibi? |
| --- | --- | --- | --- | --- |
| Disable a region | CF `// clang-format off` / `// clang-format on`, `OneLineFormatOffRegex`; AS `*INDENT-OFF*` / `*INDENT-ON*`, line tag `*NOPAD*`; Un `disable_processing_cmt`, `enable_processing_cmt`; Bl and Ru `# fmt: off`, `# fmt: on`, `# fmt: skip`; RF `#[rustfmt::skip]`; Zig `// zig fmt: off` / `// zig fmt: on`; OF `// odinfmt: disable` / `// odinfmt: enable`; Da `// dart format off` / `// dart format on` (dart_style 2.3.7); SF `// swift-format-ignore` (next node); Pr `// prettier-ignore` (next node); DP `ignoreNodeCommentText`; Bi `// biome-ignore format: <reason>`; KT `@Suppress("ktlint:<rule>")` (`ktlint-disable` removed in 0.50.0); Go none | Un `*INDENT-OFF*` / `*INDENT-ON*`; DP `dprint-ignore` | W (emits the original bytes) | Yes |
| Disable a whole file | Pr `requirePragma`, `insertPragma`, `checkIgnorePragma`; CF `DisableFormat`; RF `disable_all_formatting`, `format_generated_files`; SF `// swift-format-ignore-file`; DP `ignoreFileCommentText`; Bi `// biome-ignore-all format: <reason>`; UB `pragma_insert`, `pragma_require` | Pr false; CF false; RF false, true; DP `dprint-ignore-file`; UB false | W (C for inserting a pragma) | With note: a file-level directive covers generated files |
| Range formatting | Pr `rangeStart`, `rangeEnd`; Bl `--line-ranges START-END`; CF `--lines=START:END`, `--offset`/`--length`, `git clang-format`; Ru `--range LINE:COL-LINE:COL` | Pr 0, Infinity | W | Yes: editor and `--staged` use |
| File selection | Pr `.prettierignore`; Bl `--exclude`, `--extend-exclude`; RF `ignore`; Bi `includes`; Ru `exclude` | | n/a | `chibi fmt` already scopes to git-tracked files |

### Other

| Concept | Option names | Values and defaults | Class | Chibi? |
| --- | --- | --- | --- | --- |
| Style presets and editions | CF `BasedOnStyle`; AS `--style=`; KT `ktlint_code_style`; RF `style_edition`; Bl `--preview`, `--unstable`, `--enable-unstable-feature`; Ru `preview`; Da style chosen by language version (3.7 or later: tall style); DP `deno` | KT ktlint_official/intellij_idea/android_studio (ktlint_official); RF 2015 | n/a | No: one style |
| Required formatter version | Bl `required-version`; RF `required_version`; SF `version` | | n/a | Later, if options ship in a project file |
| Read `.editorconfig` | Bi `useEditorconfig`; KT and Pr read it | Bi false | n/a | With note: only `indent_style`, `indent_size`, `end_of_line`, `max_line_length` map |
| Format source with errors | Bi `formatWithErrors` | false | n/a | No: rejected today |

## Recommended option set for Chibi

Defaults are chosen so that code the current formatter already produces stays
byte-identical. Ordered by how many of the surveyed formatters offer the
concept. Items 1, 2, 3, 5 and 6 exist today or existed in the archived C
formatter; their names are kept.

| # | Name | Values (default) | Class | Why, and who has it |
| --- | --- | --- | --- | --- |
| 1 | `preferred_width` | 0, or 1 to 4096 columns (0 = keep author line breaks) | W | Every configurable formatter has a width (Pr `printWidth`, Bl, CF `ColumnLimit`, RF `max_width`, OF, SF, Da, Bi, Ru, DP); 0 = none follows CF `ColumnLimit: 0`, and Go and Zig have no width at all |
| 2 | `indent_width` | 1 to 16 (4) | W | Exists. Pr `tabWidth`, CF `IndentWidth`, RF `tab_spaces`, OF `spaces`, EC/KT `indent_size`, Bi, DP, Ru, AS, Un, UB, SF |
| 3 | `use_tabs` | bool (false) | W | Exists. Pr `useTabs`, CF `UseTab`, RF `hard_tabs`, OF `tabs`, EC `indent_style`, Bi `indentStyle`, Ru, DP, AS, Un, UB, SF |
| 4 | `// chibi fmt: off` / `// chibi fmt: on` | comment directives | W | 14 of 19 tools have a region or node directive; Zig, Dart and odinfmt name the tool in the comment the same way |
| 5 | `line_ending` | `lf`, `crlf` (lf) | W | Exists as the bool `crlf`; keep it unless an `auto` value is added. Pr `endOfLine`, CF `LineEnding`, RF `newline_style`, OF, EC `end_of_line`, Bi, DP, Ru, AS, Un |
| 6 | `max_blank_lines` | 0 to 3 (1) | W | Exists. CF `MaxEmptyLinesToKeep`, RF `blank_lines_upper_bound`, SF `maximumBlankLines`, OF `newline_limit`, UB/AB `max_preserve_newlines`, Un `nl_max`, AS `--squeeze-lines`; Go and Pr fix it at 1 |
| 7 | `trailing_commas` | `leave`, `multiline`, `never` (leave) | T | Pr, Bi, DP, RF, SF, KT, CF, Un, UB; Dart and Black do it implicitly |
| 8 | `sort_imports` | bool (false) | R | Go (always), RF `reorder_imports`, OF, CF `SortIncludes`, SF `orderedImports`, KT, DP, Bi, Un. Most default on; false keeps today's output |
| 9 | `semicolons` | `leave`, `always`, `never` (leave) | T | Pr `semi`, DP `semiColons`, Bi `semicolons`, RF `trailing_semicolon`, CF `RemoveSemicolon`, Un, UB, SF rule |
| 10 | `short_blocks` | `empty`, `keep` (empty: only `{}` stays on one line) | W | CF `AllowShortBlocksOnASingleLine`, AS `--keep-one-line-blocks`, Un `nl_*_leave_one_liners`, RF `fn_single_line`, OF `inline_single_stmt_case`, DP `singleBodyPosition` |
| 11 | `indent_case_labels` | bool (true) | W | CF `IndentCaseLabels`, AS `--indent-switches`, Un `indent_switch_case`, OF `indent_cases`, SF `indentSwitchCaseLabels`, RF `match_arm_indent` |
| 12 | `bracket_spacing` | bool (false: `{a: 1}`) | W | Pr and Bi `bracketSpacing`, UB/AB `object_curly_spacing`, DP `spaceSurroundingProperties`, Un `sp_inside_braces_struct`, CF `Cpp11BracedListStyle` |
| 13 | Range formatting (`--lines START:END`, API byte range) | CLI and API | W | Pr `rangeStart`/`rangeEnd`, Bl `--line-ranges`, CF `--lines`, Ru `--range`; the archived C formatter returned an edit map |
| 14 | `align_struct_fields` | bool (false) | W | OF `align_struct_fields`, CF `AlignConsecutiveDeclarations`, Un `align_var_struct_span`, RF `struct_field_align_threshold`; Go always aligns |
| 15 | `align_trailing_comments` | bool (false) | W | CF `AlignTrailingComments`, OF `align_comments`, Un `align_right_cmt_span`; Go always aligns |
| 16 | `magic_trailing_comma` | bool (true) | W | Bl and Ru `skip-magic-trailing-comma`, Da `trailing_commas: preserve`, Zig, SF `keptAsWritten`. Only acts when `preferred_width > 0` |
| 17 | `blank_line_between_declarations` | `leave`, `always` (leave) | W | CF `SeparateDefinitionBlocks`, RF `blank_lines_lower_bound`, AS `--break-blocks`, Un `nl_after_func_body`; Black fixes it |
| 18 | `spaces_before_trailing_comment` | 1 to 8 (1) | W | CF `SpacesBeforeTrailingComments` (1), SF `spacesBeforeEndOfLineComments` (2), Un `sp_before_tr_cmt`; Black fixes 2 |
| 19 | `align_after_open_bracket` | bool (true: align under the first item; false: always hang) | W | CF `AlignAfterOpenBracket`, RF `indent_style` Visual/Block, Un `indent_align_paren`, AS `--indent-after-parens`, DP `preferHanging` |
| 20 | `remove_redundant_parens` | bool (false) | T | CF `RemoveParentheses`, RF `remove_nested_parens`, Un `mod_paren_on_return`; Black always |
| 21 | `align_assignments` | bool (false) | W | CF `AlignConsecutiveAssignments`, Un `align_assign_span`, AB/UB `align_assignments` |
| 22 | `align_struct_literal_values` | bool (false) | W | OF `align_struct_values`, CF `AlignArrayOfStructures`, Un `align_struct_init_span` |
| 23 | `continuation_indent` | 1 to 4 levels (1) | W | CF `ContinuationIndentWidth`, AS `--indent-continuation`, Un `indent_continue`; EC proposal `continuation_indent_size` |
| 24 | `align_constants` | bool (false) | W | OF `align_constant_definitions`, CF `AlignConsecutiveMacros`; Go aligns const blocks |
| 25 | `keep_blank_lines_at_block_edges` | bool (true) | W | CF `KeepEmptyLines.AtStartOfBlock`, Un `eat_blanks_after_open_brace` / `eat_blanks_before_close_brace`; ktlint has rules for it |
| 26 | `align_operands` | bool (true) | W | CF `AlignOperands`; elsewhere fixed or absent. Describes today's behavior, so keep it as a constant until a user needs it off |

Fixed behaviors that most formatters hard-code, kept without an option: one
final line end (Go, RF, Pr, Bl fix it; CF `InsertNewlineAtEOF`, EC
`insert_final_newline` are options); no trailing whitespace; one space around
binary operators and after commas, none before; no spaces inside `()` and `[]`;
`{}` for empty braces; `if (x)` and `f(x)` keyword and call spacing; `a: int`
colon spacing.

Notes on adopting these:

- Items 7, 9 and 20 (class T) and item 8 (class R) need the token check
  widened to "same stream apart from the permitted change", with a
  rejection test for each.
- Items 1, 10 and 16 move line breaks. In Chibi a line break after a token
  that can end a statement inserts a terminator, so each candidate break
  needs the same terminator check the formatter already runs after `}`.
- Each item should wait for a real request: `AGENTS.md` adds an option once a
  second real caller exists. Items 14, 15, 21, 22 and 24 are one alignment feature
  applied to different runs; if adopted, build the run detection once.

## Options that do not apply to Chibi

- **Brace placement** (CF `BreakBeforeBraces`, AS `--style=allman`, RF
  `brace_style`, OF `brace_style`, UB `brace_style`, DP `bracePosition`): a
  line break before `{` inserts an implicit terminator, so only attached braces
  parse.
- **`else` on its own line** (CF `BraceWrapping.BeforeElse`, RF
  `control_brace_style`, DP `nextControlFlowPosition`, SF
  `lineBreakBeforeControlFlowKeywords`): a break after `}` ends the `if`.
- **Leading binary operators** (CF `BreakBeforeBinaryOperators`, RF
  `binop_separator`, Pr `experimentalOperatorPosition`, Bi `operatorLinebreak`,
  DP `operatorPosition`): a break before the operator ends the statement after
  the left operand.
- **Quote style and string normalization** (Pr `singleQuote`, Bl
  `skip-string-normalization`, Ru `quote-style`, Bi `quoteStyle`, UB
  `quotes`): Chibi has one string syntax, and string bytes are copied.
- **Comment rewriting** (CF `ReflowComments`, `SpacesInLineCommentPrefix`, RF
  `wrap_comments`, `normalize_comments`, Un `cmt_*`, DP
  `commentLine.forceSpaceAfterSlashes`): class C; comment bytes are copied
  exactly.
- **Breaking string literals** (CF `BreakStringLiterals`, Bl
  `string_processing`, RF `format_strings`): a string is one token.
- **Brace insertion or removal** (CF `InsertBraces`, AS `--add-braces`, DP
  `useBraces`, OF `convert_do`): Go-style bodies always have braces
  (unverified for every Chibi statement form).
- **Indenting blank lines** (SF `indentBlankLines`, AS `--fill-empty-lines`):
  contradicts trailing-whitespace removal.
- **Tabs for alignment** (CF `UseTab: ForContinuationAndIndentation`, Un
  `align_with_tabs`): alignment must survive any tab width, so it stays spaces.
- **Constructs Chibi lacks**: preprocessor, namespace and access-modifier
  indentation, pointer alignment (`int* p`), JSX, arrow-function parentheses,
  templates, Objective-C, include categories, closing comments (NA
  `ClosingComments`, CF `FixNamespaceComments`).
- **Member reordering** (NArrange `Elements`/`Sort`/`Group`, RF
  `reorder_impl_items`): class R over whole declarations, which moves comments
  and attributes with them; not needed while files are hand-ordered.
- **Code rewrites and presets** (gofmt `-s`/`-r`, RF `use_field_init_shorthand`,
  `use_try_shorthand`, CF `BasedOnStyle`, KT `ktlint_code_style`, Bl
  `--preview`): Chibi has one style, and rewrites belong to a fix tool, not the
  formatter.
