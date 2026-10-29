"""Private execve-only diagnostic counts; never emit trace text or paths.

Input: strace -f -s4096 -e trace=execve,execveat (no timestamps or -v).
Counts successful host execs, NOT completed compiler jobs or performance.
Malformed/incomplete exec records fail closed, as do truncated host argv.
"""

import re

CASES = ("lines", "rejection", "targets", "interrupted", "incomplete",
         "emitter-seed", "cache", "tools", "llvm", "cross", "other")
ENTRY = "0x140a9546d9e8c296"
REASONS = frozenset(('trace_invalid', 'trace_escape', 'trace_exec_shape',
                     'trace_exec_status', 'trace_argv_shape', 'trace_options',
                     'trace_paths', 'trace_case', 'trace_resume',
                     'trace_unexpected_record', 'trace_unfinished'))
_QUOTED = r'"(?:[^"\\]|\\.)*"'
_PATH = re.compile(r'\s*(' + _QUOTED + r')\s*,')
_ARGV = re.compile(r'\s*(\[(?:' + _QUOTED + r'|[^"\]])*\])\s*,')
_ARRAY = re.compile(r'\[\s*(?:' + _QUOTED + r'(?:\s*,\s*' + _QUOTED + r')*)?\s*\]')
_PREFIX = re.compile(r'^(?:\[pid\s+(\d+)\]\s+|(\d+)\s+)?(.*)$')
_SUITE = r'/tmp/chibi-native-build-[A-Za-z0-9]+'
_CASE = re.compile(_SUITE + r'/([^/]+)/game with spaces( moved)?/build/native/O[02]-[0-9a-f]{16}\.work')
_EXPORT = re.compile(_SUITE + r'/native game/\.chibi/build/native/libraries\.work')


class TraceError(ValueError):
    def __init__(self, reason):
        self.reason = reason if reason in REASONS else 'trace_invalid'
        super().__init__("invalid or ambiguous exec trace")


def _invalid(reason='trace_invalid'):
    raise TraceError(reason)


def _decode(token):
    escapes = {'a': '\a', 'b': '\b', 'f': '\f', 'n': '\n', 'r': '\r',
               't': '\t', 'v': '\v', '\\': '\\', '"': '"', '?': '?'}

    def decode(match):
        value = match[1]
        if value[0] in '01234567':
            return chr(int(value, 8))
        if value.startswith('x') and len(value) > 1:
            return chr(int(value[1:], 16))
        if value not in escapes:
            _invalid('trace_escape')
        return escapes[value]

    return re.sub(r'\\(x[0-9a-fA-F]{1,2}|[0-7]{1,3}|.)', decode, token[1:-1])


def _case(record):
    call = re.fullmatch(r'(execve|execveat)\((.*)\)\s*=\s*(.*)', record)
    if not call:
        _invalid('trace_exec_shape')
    if re.match(r'-1\b', call[3]):
        return None  # Failed PATH candidates are not executions.
    if call[3] != '0':
        _invalid('trace_exec_status')
    arguments = call[2]
    if call[1] == 'execveat':
        fd = re.match(r'(?:AT_FDCWD|-?\d+)\s*,', arguments)
        if not fd:
            _invalid('trace_exec_shape')
        arguments = arguments[fd.end():]
    path = _PATH.match(arguments)
    if not path:
        _invalid('trace_exec_shape')
    executable = _decode(path[1])
    if executable and executable.rsplit('/', 1)[-1] != 'chibi-compiler-host':
        return None
    head = _ARGV.match(arguments[path.end():])
    if not head or not _ARRAY.fullmatch(head[1]):
        _invalid('trace_argv_shape')  # Includes ellipses or pointer-only argv.
    argv = [_decode(token) for token in re.findall(_QUOTED, head[1])]
    if not any(ENTRY in value for value in argv):
        return None
    if not executable:  # execveat(AT_EMPTY_PATH) cannot identify the executable.
        _invalid('trace_exec_shape')
    options = {}
    if not argv or len(argv) % 2 != 1:
        _invalid('trace_options')
    for key, value in zip(argv[1::2], argv[2::2]):
        if not key.startswith('--') or '=' in key or key in options or value.startswith('--'):
            _invalid('trace_options')
        options[key] = value
    if options.get('--entry') != ENTRY:
        _invalid('trace_options')
    source = options.get('--input', '')
    output = options.get('--output-bytes', '')
    compile_input = source.rsplit('/', 1)[-1] == 'compile.json'
    emitter_output = output.rsplit('/', 1)[-1] == 'emitter.chibip'
    if not compile_input and not emitter_output:
        return None
    if not compile_input or not emitter_output:
        _invalid('trace_paths')
    folder = source.rsplit('/', 1)[0]
    if output != folder + '/emitter.chibip':
        _invalid('trace_paths')
    match = _CASE.fullmatch(folder)
    if match and match[1] in CASES[:-1] and (not match[2] or match[1] == 'lines'):
        return match[1]
    if _EXPORT.fullmatch(folder):
        return 'other'
    _invalid('trace_case')


def parse_counts(text):
    """Return every fixed case name, including zero counts; raise a redacted ValueError."""
    counts = dict.fromkeys(CASES, 0)
    pending = {}
    for line in text.splitlines():
        prefix = _PREFIX.fullmatch(line.strip())
        pid, record = prefix[1] or prefix[2], prefix[3]
        resumed = re.match(r'<\.\.\. (execve|execveat) resumed>(.*)', record)
        if resumed:
            if pid not in pending or not pending[pid].startswith(resumed[1] + '('):
                _invalid('trace_resume')
            record = pending.pop(pid) + resumed[2]
        elif not record.startswith(('execve(', 'execveat(')):
            # A detached/lost process could hide a candidate compilation.
            # Admit only normal signal/exit records and harmless attach notices.
            if (re.fullmatch(r'--- SIG[A-Z0-9]+ .* ---', record) or
                    re.fullmatch(r'\+\+\+ (?:exited with [0-9]+|killed by SIG[A-Z0-9]+(?: \(core dumped\))?) \+\+\+', record) or
                    re.fullmatch(r'strace: Process [0-9]+ attached', record)):
                continue
            _invalid('trace_unexpected_record')
        elif pid in pending:
            _invalid('trace_resume')
        marker = ' <unfinished ...>'
        if record.endswith(marker):
            pending[pid] = record[:-len(marker)]
            continue
        case = _case(record)
        if case is not None:
            counts[case] += 1
    if pending:
        _invalid('trace_unfinished')
    return counts
