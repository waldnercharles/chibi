// The formatter's options, described once for the page. `key` is the field of source_format.Options
// (tools/compiler/source_format/Format.chibi); keep the two in step. A `planned` option is one the
// formatter does not have yet, shown so the interface does not look complete when it is not.
const OPTION_GROUPS = [
  { name: 'Indentation', items: [
    { key: 'use_tabs', kind: 'choice', label: 'Indent with', default: false,
      choices: [{ value: false, label: 'Spaces' }, { value: true, label: 'Tabs' }],
      help: 'Tabs write one tab per level. Lines continued inside brackets still align with spaces.' },
    { key: 'indent_width', kind: 'number', label: 'Indent width', default: 4, min: 1, max: 16,
      help: 'Columns per level. With tabs, the width a tab counts for when aligning continued lines.' },
  ] },
  { name: 'Blank lines', items: [
    { key: 'max_blank_lines', kind: 'choice', label: 'Keep at most', default: 1,
      choices: [{ value: 0, label: '0' }, { value: 1, label: '1' }, { value: 2, label: '2' }, { value: 3, label: '3' }],
      help: 'A longer run of blank lines shrinks to this many.' },
  ] },
  { name: 'Line endings', items: [
    { key: 'crlf', kind: 'choice', label: 'Line break', default: false,
      choices: [{ value: false, label: 'LF' }, { value: true, label: 'CRLF' }],
      help: 'Between lines only. Bytes inside comments and strings are copied exactly.' },
  ] },
  { name: 'Wrapping', items: [
    { key: 'preferred_width', kind: 'number', label: 'Preferred width', default: 0, min: 0, max: 4096, planned: true,
      help: 'Not implemented yet. Breaks you wrote inside brackets are kept as they are; 0 will mean the same after wrapping lands.' },
  ] },
];

function defaultOptions() {
  const options = {};
  for (const group of OPTION_GROUPS) for (const item of group.items) options[item.key] = item.default;
  return options;
}

function optionByKey(key) {
  for (const group of OPTION_GROUPS) for (const item of group.items) if (item.key === key) return item;
  return null;
}

// The options the formatter takes, as the host expects them: planned ones are left out.
function formatterOptions(options) {
  const sent = {};
  for (const group of OPTION_GROUPS) for (const item of group.items) if (!item.planned) sent[item.key] = options[item.key];
  return sent;
}
