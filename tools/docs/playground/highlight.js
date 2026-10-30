// Syntax highlighting for Chibi source: one HTML string per line. It reads text, not the parser's
// tree, so it colors what the language's tokens look like and never judges validity.
const CHIBI_KEYWORDS = new Set(('package import behavior library system state retain action event disabled on after as bind ' +
  'events union struct switch is not and or macro entry exit query if else while for in case sequence branch return reduce ' +
  'unordered goto break continue await yield cancel true false nil null plain service setting wildcard from split component ' +
  'prototype relationship pub delete foreign emit').split(' '));

const CHIBI_TOKEN = new RegExp([
  '\\/\\/[^\\n]*',                                   // line comment
  '\\/\\*[\\s\\S]*?(?:\\*\\/|$)',                    // block comment
  '"(?:\\\\[\\s\\S]|[^"\\\\])*"?',                    // string
  "'(?:\\\\.|[^'\\\\\\n])*'?",                        // character
  '@[A-Za-z_]\\w*',                                  // attribute
  '0[xX][0-9a-fA-F_]+|\\d[\\d_]*(?:\\.\\d[\\d_]*)?(?:[eE][+-]?\\d+)?',  // number
  '[A-Za-z_]\\w*',                                   // word
  '::|:=|->|=>|\\.\\.<|\\.\\.|[+\\-*\\/%&|^~<>=!?]+', // operator
].join('|'), 'g');

function escapeHtml(text) {
  return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function highlightChibi(source) {
  const text = source.replace(/\r\n/g, '\n');
  const lines = [];
  let line = '', plain = '';
  const flush = () => { if (plain) { line += escapeHtml(plain); plain = ''; } };
  const emit = (cls, piece) => {
    const parts = piece.split('\n');
    for (let i = 0; i < parts.length; i++) {
      if (i > 0) { flush(); lines.push(line); line = ''; }
      if (parts[i] === '') continue;
      if (cls) { flush(); line += `<span class="t-${cls}">${escapeHtml(parts[i])}</span>`; }
      else plain += parts[i];
    }
  };
  let at = 0, match;
  CHIBI_TOKEN.lastIndex = 0;
  while ((match = CHIBI_TOKEN.exec(text)) !== null) {
    if (match.index > at) emit('', text.slice(at, match.index));
    const token = match[0], first = token[0];
    let cls = '';
    if (first === '/' && (token[1] === '/' || token[1] === '*')) cls = 'comment';
    else if (first === '"' || first === "'") cls = 'string';
    else if (first === '@') cls = 'attr';
    else if (first >= '0' && first <= '9') cls = 'number';
    else if (/[A-Za-z_]/.test(first)) {
      if (CHIBI_KEYWORDS.has(token)) cls = 'keyword';
      else {
        const rest = text.slice(CHIBI_TOKEN.lastIndex, CHIBI_TOKEN.lastIndex + 24);
        const upper = first >= 'A' && first <= 'Z';
        if (upper && /^\s*::/.test(rest)) cls = 'decl';
        else if (/^\s*\(/.test(rest)) cls = 'call';
        else if (upper) cls = 'type';
      }
    } else cls = 'op';
    emit(cls, token);
    at = CHIBI_TOKEN.lastIndex;
    if (token === '') CHIBI_TOKEN.lastIndex++;
  }
  if (at < text.length) emit('', text.slice(at));
  flush();
  lines.push(line);
  // A trailing newline ends the last line; it does not start another.
  if (lines.length > 1 && lines[lines.length - 1] === '') lines.pop();
  return lines;
}

// Marks leading indentation: a dot per space, an arrow per tab.
function showIndentation(html) {
  return html.replace(/^[ \t]+/, (run) => `<span class="ws">${run.replace(/ /g, '·').replace(/\t/g, '→')}</span>`);
}
