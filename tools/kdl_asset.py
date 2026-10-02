"""KDL 2 reader/writer for Chibi's object/array asset representation.

The document is an implicit object. Properties and child nodes are object
members; positional values are scalars or arrays. (array) and (object) keep
empty collections and one-item arrays unambiguous. No YAML compatibility.
"""
import math
import re


class Error(ValueError):
    pass


def _string(text):
    escapes = {'"': '\\"', '\\': '\\\\', '\n': '\\n', '\r': '\\r', '\t': '\\t', '\b': '\\b', '\f': '\\f'}
    return '"' + ''.join(escapes.get(c, f'\\u{{{ord(c):x}}}' if ord(c) < 32 else c) for c in text) + '"'


def _literal(value):
    if value is None:
        return '#null'
    if isinstance(value, bool):
        return '#true' if value else '#false'
    if isinstance(value, str):
        return _string(value)
    if isinstance(value, (int, float)):
        if isinstance(value, int) and not -(1 << 63) <= value < (1 << 64):
            raise Error('Asset integer is outside the 64-bit range')
        if isinstance(value, float) and not math.isfinite(value):
            raise Error('Asset numbers must be finite')
        return repr(value)
    raise Error(f'Unsupported asset value: {type(value).__name__}')


def _name(value):
    return value if value not in ('true', 'false', 'null') and re.fullmatch(r'[A-Za-z_][A-Za-z0-9_.-]*|-', value) else _string(value)


def dumps(document):
    if not isinstance(document, dict):
        raise Error('An asset document must be an object')

    def node(name, value, depth):
        prefix = '    ' * depth
        label = _name(name)
        if isinstance(value, dict):
            if not value:
                return f'{prefix}(object){label}\n'
            if all(not isinstance(v, (dict, list)) for v in value.values()):
                properties = ' '.join(f'{_name(k)}={_literal(v)}' for k, v in value.items())
                return f'{prefix}{label} {properties}\n'
            children = ''.join(node(k, v, depth + 1) for k, v in value.items())
            return f'{prefix}{label} {{\n{children}{prefix}}}\n'
        if isinstance(value, list):
            if all(not isinstance(v, (dict, list)) for v in value):
                args = ''.join(' ' + _literal(v) for v in value)
                return f'{prefix}(array){label}{args}\n'
            children = ''.join(node('-', v, depth + 1) for v in value)
            return f'{prefix}(array){label} {{\n{children}{prefix}}}\n'
        return f'{prefix}{label} {_literal(value)}\n'

    return ''.join(node(k, v, 0) for k, v in document.items())


class _Reader:
    def __init__(self, text):
        self.text = text
        self.at = 0

    def fail(self, message):
        line = self.text.count('\n', 0, self.at) + 1
        column = self.at - self.text.rfind('\n', 0, self.at)
        raise Error(f'{message} at {line}:{column}')

    def skip(self, lines=False):
        while self.at < len(self.text):
            c = self.text[self.at]
            if c in ' \t\r\ufeff' or (lines and c in '\n;'):
                self.at += 1
            elif self.text.startswith('//', self.at):
                end = self.text.find('\n', self.at)
                self.at = len(self.text) if end < 0 else end
            elif self.text.startswith('/*', self.at):
                depth = 1
                self.at += 2
                while depth:
                    if self.at == len(self.text):
                        self.fail('Unterminated comment')
                    if self.text.startswith('/*', self.at):
                        depth += 1
                        self.at += 2
                    elif self.text.startswith('*/', self.at):
                        depth -= 1
                        self.at += 2
                    else:
                        self.at += 1
            elif c == '\\':
                self.at += 1
                self.skip()
                if self.at < len(self.text) and self.text[self.at] == '\n':
                    self.at += 1
                elif self.at != len(self.text):
                    self.fail('Expected line continuation')
            else:
                return

    def multiline(self, value):
        value = value.replace('\r\n', '\n').replace('\r', '\n')
        if not value.startswith('\n') or '\n' not in value[1:]:
            self.fail('Multiline strings must open and close on separate lines')
        body, margin = value[1:].rsplit('\n', 1)
        if margin.strip(' \t'):
            self.fail('A multiline closing delimiter must occupy its own line')
        lines = []
        for line in body.split('\n'):
            if not line.strip(' \t'):
                lines.append('')
            elif line.startswith(margin):
                lines.append(line[len(margin):])
            else:
                self.fail('Multiline string indentation is shallower than its closing delimiter')
        return '\n'.join(lines)

    def string(self):
        multiline = self.text.startswith('"""', self.at)
        delimiter = '"""' if multiline else '"'
        self.at += len(delimiter)
        out = []
        escapes = {'n': '\n', 'r': '\r', 't': '\t', 'b': '\b', 'f': '\f', 's': ' ', '"': '"', '\\': '\\'}
        while self.at < len(self.text):
            if self.text.startswith(delimiter, self.at):
                self.at += len(delimiter)
                value = ''.join(out)
                return self.multiline(value) if multiline else value
            c = self.text[self.at]
            self.at += 1
            if c == '\\':
                if self.at == len(self.text):
                    self.fail('Unterminated escape')
                c = self.text[self.at]
                self.at += 1
                if c == 'u' and self.at < len(self.text) and self.text[self.at] == '{':
                    end = self.text.find('}', self.at)
                    digits = self.text[self.at + 1:end]
                    if end < 0 or not re.fullmatch(r'[0-9a-fA-F]{1,6}', digits):
                        self.fail('Invalid Unicode escape')
                    value = int(digits, 16)
                    if value > 0x10ffff or 0xd800 <= value <= 0xdfff:
                        self.fail('Invalid Unicode scalar')
                    out.append(chr(value))
                    self.at = end + 1
                elif c in escapes:
                    out.append(escapes[c])
                elif c.isspace():
                    while self.at < len(self.text) and self.text[self.at].isspace():
                        self.at += 1
                else:
                    self.fail('Invalid escape')
            elif (c in '\n\r' and not multiline) or (ord(c) < 32 and c not in '\n\r\t'):
                self.fail('Unescaped control character in string')
            else:
                out.append(c)
        self.fail('Unterminated string')

    def token(self):
        self.skip()
        if self.at == len(self.text):
            self.fail('Expected value')
        if self.text[self.at] == '"':
            return self.string(), True
        raw = re.match(r'(#+)("""|")', self.text[self.at:])
        if raw:
            start = self.at + len(raw[0])
            delimiter = raw[2] + raw[1]
            end = self.text.find(delimiter, start)
            if end < 0:
                self.fail('Unterminated raw string')
            value = self.text[start:end]
            self.at = end + len(delimiter)
            if raw[2] == '"' and ('\n' in value or '\r' in value):
                self.fail('A single-line raw string cannot contain a newline')
            return self.multiline(value) if raw[2] == '"""' else value, True
        start = self.at
        while self.at < len(self.text) and self.text[self.at] not in ' \t\r\n;{}()="\\':
            if self.text.startswith(('//', '/*'), self.at):
                break
            self.at += 1
        if start == self.at:
            self.fail('Expected token')
        word = self.text[start:self.at]
        if any(c in word for c in '[]<>:,'):
            self.fail('Invalid bare token')
        return word, False

    def value(self, token):
        word, quoted = token
        if quoted:
            return word
        if word in ('#true', '#false', '#null'):
            return {'#true': True, '#false': False, '#null': None}[word]
        number = word.replace('_', '')
        if re.fullmatch(r'[+-]?0[xX][0-9a-fA-F]+|[+-]?0[oO][0-7]+|[+-]?0[bB][01]+', number):
            value = int(number, 0)
            if not -(1 << 63) <= value < (1 << 64):
                self.fail('Asset integer is outside the 64-bit range')
            return value
        if re.fullmatch(r'[+-]?(?:0|[1-9][0-9]*)', number):
            value = int(number)
            if not -(1 << 63) <= value < (1 << 64):
                self.fail('Asset integer is outside the 64-bit range')
            return value
        if re.fullmatch(r'[+-]?[0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?', number) and ('.' in number or 'e' in number.lower()):
            value = float(number)
            if not math.isfinite(value):
                self.fail('Asset numbers must be finite')
            return value
        if word.startswith('#') or word[:1] in '+0123456789' or (word.startswith('-') and len(word) > 1 and word[1].isdigit()):
            self.fail('Invalid asset literal')
        return word

    def nodes(self, child=False):
        result = []
        self.skip(True)
        while self.at < len(self.text) and self.text[self.at] != '}':
            discarded = self.text.startswith('/-', self.at)
            if discarded:
                self.at += 2
                self.skip(True)
            name, value = self.node()
            if not discarded:
                result.append((name, value))
            self.skip(True)
        if child:
            if self.at == len(self.text):
                self.fail('Unclosed child block')
            self.at += 1
        elif self.at != len(self.text):
            self.fail('Unexpected closing brace')
        return result

    def node(self):
        annotation = None
        if self.text[self.at] == '(':
            self.at += 1
            annotation, _ = self.token()
            self.skip()
            if self.at == len(self.text) or self.text[self.at] != ')':
                self.fail('Expected closing annotation')
            self.at += 1
            if annotation not in ('array', 'object'):
                self.fail('Unsupported asset annotation')
        name, quoted = self.token()
        if not quoted and (name.startswith('#') or name in ('true', 'false', 'null') or re.match(r'[+-]?[0-9]', name)):
            self.fail('Invalid node name')
        properties, args, children = {}, [], None
        while True:
            self.skip()
            if self.at == len(self.text) or self.text[self.at] in '\n;}':
                break
            discarded = self.text.startswith('/-', self.at)
            if discarded:
                self.at += 2
                self.skip()
            if self.at < len(self.text) and self.text[self.at] == '{':
                self.at += 1
                block = self.nodes(True)
                if discarded:
                    continue
                children = block
                self.skip()
                if self.at < len(self.text) and self.text[self.at] not in '\n;}':
                    self.fail('Expected node terminator')
                break
            token = self.token()
            self.skip()
            if self.at < len(self.text) and self.text[self.at] == '=':
                self.at += 1
                value = self.value(self.token())
                if not discarded:
                    if token[0] in properties:
                        self.fail('Duplicate property')
                    properties[token[0]] = value
            elif not discarded:
                args.append(self.value(token))
        if annotation == 'array':
            if properties or (args and children is not None):
                self.fail('An array must contain arguments or item nodes')
            if children is not None:
                if any(k != '-' for k, _ in children):
                    self.fail('Array items must use the - node name')
                return name, [v for _, v in children]
            return name, args
        if properties or children is not None or annotation == 'object' or not args:
            if args:
                self.fail('An object cannot contain positional values')
            for key, value in children or []:
                if key in properties:
                    self.fail('Duplicate object key')
                properties[key] = value
            return name, properties
        return name, args[0] if len(args) == 1 else args


def loads(text):
    result = {}
    for key, value in _Reader(text).nodes():
        if key in result:
            raise Error(f'Duplicate object key: {key}')
        result[key] = value
    return result
