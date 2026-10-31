// The formatter's options, described once for the page. `key` is the NAME of a
// NAME=VALUE pair that source_format.ParseOptions reads
// (tools/compiler/source_format/Options.chibi); keep the two in step. `also`
// names the same setting in other formatters (docs/formatter-options-survey.md).
// A `planned` option is one the formatter does not have yet, listed so the pane
// does not look complete when it is not.
const OPTION_GROUPS = [
  { name: 'Width', items: [
    { key: 'preferred_width', kind: 'number', label: 'Preferred width', default: 0, min: 0, max: 400, step: 10, zero: 'Off',
      help: 'The column wrapping aims for. Off keeps your line breaks inside brackets. On, a bracket group that fits stays on one line; one that does not breaks after the bracket, after each comma and before the closer.',
      also: 'Prettier printWidth, Black line-length, clang-format ColumnLimit, rustfmt max_width' },
    { key: 'magic_trailing_comma', kind: 'bool', label: 'Trailing comma forces a break', default: true,
      help: 'With wrapping on, a group whose last item ends in a comma always breaks, even when it would fit.',
      also: 'Black and Ruff magic trailing comma, dart format preserve' },
  ] },
  { name: 'Indentation', items: [
    { key: 'use_tabs', kind: 'choice', label: 'Indent with', default: false,
      choices: [{ value: false, label: 'Spaces' }, { value: true, label: 'Tabs' }],
      help: 'Tabs write one tab per level. Alignment past the levels, such as arguments under an open parenthesis, still uses spaces, so it survives any tab width.',
      also: 'Prettier useTabs, clang-format UseTab, rustfmt hard_tabs, EditorConfig indent_style' },
    { key: 'indent_width', kind: 'number', label: 'Indent width', default: 4, min: 1, max: 16,
      help: 'Columns per level. With tabs, the width a tab counts for when aligning continued lines.',
      also: 'Prettier tabWidth, clang-format IndentWidth, rustfmt tab_spaces, EditorConfig indent_size' },
    { key: 'continuation_indent', kind: 'number', label: 'Continuation indent', default: 0, min: 0, max: 16, zero: '1 level',
      help: 'How far a continued line is indented when it is not aligned: after a bracket that ends its line, or always when alignment is off.',
      also: 'clang-format ContinuationIndentWidth, Artistic Style --indent-continuation' },
    { key: 'align_after_open_bracket', kind: 'bool', label: 'Align after open bracket', default: true,
      help: 'Lines continued inside a bracket line up under its first item. Off, they take the continuation indent.',
      also: 'clang-format AlignAfterOpenBracket, rustfmt indent_style Visual' },
    { key: 'align_operands', kind: 'bool', label: 'Align operands', default: true,
      help: 'A continued operand of a binary expression lines up with the first operand. Off, it takes the continuation indent.',
      also: 'clang-format AlignOperands' },
    { key: 'indent_case_labels', kind: 'bool', label: 'Indent case labels', default: true,
      help: 'case labels sit one level inside their switch. Off, they line up with the switch.',
      also: 'clang-format IndentCaseLabels, odinfmt indent_cases, swift-format indentSwitchCaseLabels' },
  ] },
  { name: 'Spacing', items: [
    { key: 'space_in_braces', kind: 'bool', label: 'Spaces inside braces', default: false,
      help: 'Struct literals as { a: 1 } instead of {a: 1}. An empty {} stays tight.',
      also: 'Prettier bracketSpacing, Uncrustify sp_inside_braces_struct' },
    { key: 'space_in_parens', kind: 'bool', label: 'Spaces inside parentheses', default: false,
      help: 'Calls, parameter lists and grouping as ( a, b ). An empty () stays tight.',
      also: 'clang-format SpacesInParens, Uncrustify sp_inside_paren' },
    { key: 'space_in_brackets', kind: 'bool', label: 'Spaces inside brackets', default: false,
      help: 'Array literals as [ 1, 2 ]. Index expressions and array types stay tight.',
      also: 'clang-format SpacesInSquareBrackets, Uncrustify sp_inside_square' },
    { key: 'spaces_before_trailing_comment', kind: 'number', label: 'Spaces before trailing comment', default: 1, min: 1, max: 4,
      help: 'Spaces between code and a comment that ends the line.',
      also: 'clang-format SpacesBeforeTrailingComments, swift-format spacesBeforeEndOfLineComments' },
  ] },
  { name: 'Blank lines', items: [
    { key: 'max_blank_lines', kind: 'choice', label: 'Keep at most', default: 1,
      choices: [0, 1, 2, 3].map((n) => ({ value: n, label: String(n) })),
      help: 'A longer run of blank lines shrinks to this many.',
      also: 'clang-format MaxEmptyLinesToKeep, rustfmt blank_lines_upper_bound' },
    { key: 'blank_lines_between_declarations', kind: 'choice', label: 'Between declarations', default: 0,
      choices: [{ value: 0, label: 'Keep' }, { value: 1, label: '1' }, { value: 2, label: '2' }],
      help: 'The fewest blank lines between top-level declarations when either spans lines. Runs of one-line constants stay together, and a comment directly above a declaration belongs to it.',
      also: 'clang-format SeparateDefinitionBlocks, rustfmt blank_lines_lower_bound' },
    { key: 'keep_blank_lines_at_block_edges', kind: 'bool', label: 'Keep blank lines at block edges', default: true,
      help: 'Blank lines just inside a block’s braces stay. Off, they go.',
      also: 'clang-format KeepEmptyLines.AtStartOfBlock' },
  ] },
  { name: 'Blocks', items: [
    { key: 'empty_blocks', kind: 'choice', label: 'Empty blocks', default: 'keep',
      choices: [{ value: 'keep', label: 'Keep' }, { value: 'collapse', label: '{}' }, { value: 'expand', label: '{ ↵ }' }],
      help: 'An empty block as you wrote it, collapsed to {} or expanded over two lines.',
      also: 'clang-format AllowShortBlocksOnASingleLine: Empty' },
    { key: 'keep_one_line_blocks', kind: 'bool', label: 'Keep one-line blocks', default: false,
      help: 'A block you wrote on one line, such as if (x) { return y; }, stays on one line.',
      also: 'clang-format AllowShortBlocksOnASingleLine, Artistic Style --keep-one-line-blocks' },
  ] },
  { name: 'Alignment', items: [
    { key: 'align_assignments', kind: 'bool', label: 'Assignments', default: false,
      help: 'The =, := or compound operator of assignments on consecutive lines lines up.',
      also: 'clang-format AlignConsecutiveAssignments, Uncrustify align_assign_span' },
    { key: 'align_field_types', kind: 'bool', label: 'Field types', default: false,
      help: 'Types of consecutive struct and component fields line up.',
      also: 'gofmt (always), odinfmt align_struct_fields, clang-format AlignConsecutiveDeclarations' },
    { key: 'align_constants', kind: 'bool', label: 'Constant types', default: false,
      help: 'Types of consecutive typed constants, Name: T : value, line up.',
      also: 'odinfmt align_constant_definitions, clang-format AlignConsecutiveMacros' },
    { key: 'align_struct_literal_values', kind: 'bool', label: 'Struct literal values', default: false,
      help: 'Values of a struct literal written one field per line line up.',
      also: 'odinfmt align_struct_values, clang-format AlignArrayOfStructures' },
    { key: 'align_trailing_comments', kind: 'bool', label: 'Trailing comments', default: false,
      help: 'Comments that end consecutive lines start in the same column.',
      also: 'clang-format AlignTrailingComments, odinfmt align_comments' },
  ] },
  { name: 'Punctuation and line ends', items: [
    { key: 'semicolons', kind: 'choice', label: 'Semicolons', default: 'keep',
      choices: [{ value: 'keep', label: 'Keep' }, { value: 'omit', label: 'Omit' }, { value: 'always', label: 'Always' }],
      help: 'Explicit ; at the end of statements as you wrote them, dropped where a line break ends the statement anyway, or always written. Imports and package lines never take one.',
      also: 'Prettier semi, Biome semicolons, dprint semiColons' },
    { key: 'line_ending', kind: 'choice', label: 'Line break', default: 'lf',
      choices: [{ value: 'lf', label: 'LF' }, { value: 'crlf', label: 'CRLF' }],
      help: 'Between lines only. Bytes inside comments and strings are copied exactly.',
      also: 'Prettier endOfLine, clang-format LineEnding, EditorConfig end_of_line' },
    { key: 'final_newline', kind: 'bool', label: 'Final newline', default: true,
      help: 'The file ends with a line break.',
      also: 'EditorConfig insert_final_newline, clang-format InsertNewlineAtEOF' },
  ] },
  { name: 'Not supported yet', planned: true, items: [
    { key: 'trailing_commas', kind: 'planned', label: 'Trailing commas', planned: true,
      help: 'Add or remove a comma after the last item of a multi-line list. It changes tokens, so the token check needs to allow exactly that change first.',
      also: 'Prettier trailingComma, rustfmt trailing_comma' },
    { key: 'sort_imports', kind: 'planned', label: 'Sort imports', planned: true,
      help: 'Order import lines. It reorders code, which the token check does not allow yet.',
      also: 'goimports, rustfmt reorder_imports, clang-format SortIncludes' },
    { key: 'remove_redundant_parens', kind: 'planned', label: 'Remove redundant parentheses', planned: true,
      help: 'Drop parentheses that do not change meaning, such as around an if condition.',
      also: 'clang-format RemoveParentheses, rustfmt remove_nested_parens' },
    { key: 'line_range', kind: 'planned', label: 'Format a line range', planned: true,
      help: 'Format only some lines of a file, for editors that format a selection.',
      also: 'Prettier rangeStart, Black --line-ranges, clang-format --lines' },
  ] },
];

function eachOption(visit) {
  for (const group of OPTION_GROUPS) for (const item of group.items) visit(item, group);
}

function defaultOptions() {
  const options = {};
  eachOption((item) => { if (!item.planned) options[item.key] = item.default; });
  return options;
}

function optionByKey(key) {
  let found = null;
  eachOption((item) => { if (item.key === key) found = item; });
  return found;
}

// The options that differ from the defaults, as .chibi-format lines.
function configText(options) {
  const lines = [];
  eachOption((item) => {
    if (!item.planned && options[item.key] !== item.default) lines.push(`${item.key}=${options[item.key]}`);
  });
  return lines.join('\n');
}
