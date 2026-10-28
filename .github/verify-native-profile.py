#!/usr/bin/env python3
"""Verify the native CI profile against build commands and produced binaries."""
import argparse
import json
import os
import re
from pathlib import Path
import shlex
import subprocess


REPRESENTATIVES = {
    'engine': ('chibi_engine', 'src/runtime/engine.c'),
    'sdk': ('chibi_compiler', 'src/language/lower.c'),
    'tools': ('test_tools', 'test/tools/main.c'),
    'language': ('test_language', 'test/language/main.c'),
    'cxx': ('chibi_ui_host', 'src/ui/rml.cpp'),
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sanitizer_flags(arguments, sanitized):
    flags = [arg for arg in arguments if arg.startswith(('-fsanitize=', '-fno-sanitize='))]
    require(flags == (['-fsanitize=address,undefined'] if sanitized else []),
            f'unexpected sanitizer flags: {flags}')


def compile_flags(arguments, sanitized, assertions=False):
    require([arg for arg in arguments if arg.startswith('-O')] == ['-O1'], 'expected O1')
    require('-g' in arguments and '-g0' not in arguments, 'debug symbols missing')
    require('-fno-omit-frame-pointer' in arguments and '-fomit-frame-pointer' not in arguments,
            'frame pointers missing')
    require(not any(arg.startswith('-DNDEBUG') or arg == 'NDEBUG' for arg in arguments),
            'NDEBUG must not be defined')
    require(not assertions or '-UNDEBUG' in arguments, 'test assertions flag missing')
    sanitizer_flags(arguments, sanitized)


def argv(entry):
    return entry['arguments'] if 'arguments' in entry else shlex.split(entry['command'])


def without_dependencies(arguments):
    result = []
    iterator = iter(arguments)
    for arg in iterator:
        if arg in ('-MF', '-MT', '-MQ'):
            next(iterator)
        elif arg not in ('-MD', '-MMD'):
            result.append(arg)
    return result


def logged_commands(path):
    commands = []
    for line in path.read_text().splitlines():
        try:
            # Ninja wraps link commands in shell separators; keep each argv.
            row = []
            for arg in shlex.split(line):
                if arg in ('&&', ';'):
                    if row:
                        commands.append(row)
                    row = []
                else:
                    row.append(arg)
            if row:
                commands.append(row)
        except ValueError:
            pass  # A compiler diagnostic need not be valid shell syntax.
    return commands


def observed(arguments, commands):
    expected = without_dependencies(arguments)
    return any(expected == without_dependencies(row[row.index(arguments[0]):])
               for row in commands if arguments[0] in row)


def execution_record(output, arguments):
    require(output.is_file(), f'missing output: {output}')
    record = Path(str(output) + '.chibi-record')
    require(record.read_text().split('\n\n', 1)[0].splitlines() == arguments,
            f'Graph execution record differs: {output}')


def symbols(binary, sanitized):
    output = subprocess.check_output(['nm', str(binary)], text=True, stderr=subprocess.PIPE)
    found = {'asan': '__asan_init' in output, 'ubsan': '__ubsan_handle_' in output}
    require(all(value == sanitized for value in found.values()),
            f'{binary}: unexpected sanitizer symbols: {found}')
    return found


def verify(build, log, sanitized):
    graph = (build / 'link_commands.json').is_file()
    commands = [] if graph else logged_commands(log)
    database = json.loads((build / 'compile_commands.json').read_text())
    receipt = {'profile': 'sanitizers' if sanitized else 'fast',
               'backend': 'Graph' if graph else 'CMake', 'compile': {}, 'links': {}}
    for name, (target, source) in REPRESENTATIVES.items():
        entries = [entry for entry in database
                   if entry['file'].endswith('/' + source)
                   and any(f'/objects/{target}/' in arg or f'CMakeFiles/{target}.dir/' in arg
                           for arg in argv(entry))]
        require(len(entries) == 1, f'{name}: missing or ambiguous compile database entry')
        arguments = argv(entries[0])
        compile_flags(arguments, sanitized, name in ('tools', 'language'))
        if graph:
            execution_record(Path(entries[0]['output']), arguments)
        else:
            require(observed(arguments, commands), f'{name}: compiler argv absent from build log')
        receipt['compile'][name] = arguments
    header = build / 'test/tools/testing/include/export_compiler.h'
    exported = [json.loads(line.strip().removesuffix(','))
                for line in header.read_text().splitlines() if line.strip().startswith('"')]
    compile_flags(exported, sanitized, assertions=True)
    receipt['export_compiler'] = exported
    links = json.loads((build / 'link_commands.json').read_text()) if graph else []
    for target in ('test_tools', 'test_language'):
        if graph:
            matches = [entry for entry in links if Path(entry['output']).name == target]
            require(len(matches) == 1, f'{target}: missing or ambiguous Graph link entry')
            arguments = argv(matches[0])
            binary = Path(matches[0]['output'])
            execution_record(binary, arguments)
        else:
            # CMake's Unix Makefiles generator retains the exact linker recipe.
            paths = list(build.rglob(f'CMakeFiles/{target}.dir/link.txt'))
            if paths:
                require(len(paths) == 1, f'{target}: ambiguous CMake link recipe')
                arguments = shlex.split(paths[0].read_text())
                require(observed(arguments, commands), f'{target}: linker argv absent from build log')
                binary = paths[0].parent.parent.parent / arguments[arguments.index('-o') + 1]
            else:
                require((build / 'build.ninja').is_file(), 'unknown CMake generator')
                matches = []
                for row in commands:
                    if '-o' not in row or '-c' in row:
                        continue
                    output = row[row.index('-o') + 1]
                    if Path(output).name != target:
                        continue
                    compilers = [index for index, arg in enumerate(row)
                                 if re.fullmatch(r'(clang(?:\+\+)?|gcc|g\+\+|cc|c\+\+)(?:-[0-9.]+)?', Path(arg).name)]
                    if len(compilers) == 1:
                        matches.append(row[compilers[0]:])
                require(len(matches) == 1, f'{target}: missing or ambiguous actual Ninja link argv')
                arguments = matches[0]
                binary = build / arguments[arguments.index('-o') + 1]
            require(binary.is_file(), f'missing linked binary: {binary}')
        sanitizer_flags(arguments, sanitized)
        receipt['links'][target] = {'arguments': arguments, 'binary': str(binary),
                                    'symbols': symbols(binary, sanitized)}
    if not graph:
        cache = dict(line.split('=', 1) for line in (build / 'CMakeCache.txt').read_text().splitlines()
                     if '=' in line and not line.startswith(('#', '//')))
        require(cache.get('CMAKE_BUILD_TYPE:STRING') == 'Debug', 'CMake build is not Debug')
        require(cache.get('CHIBI_ENABLE_SANITIZERS:BOOL') == ('ON' if sanitized else 'OFF'),
                'CMake sanitizer setting differs')
    return receipt


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', type=Path, default=Path('build'))
    parser.add_argument('--log', type=Path, default=Path(os.environ.get('RUNNER_TEMP', '/tmp')) / 'native-build.log')
    parser.add_argument('--output', type=Path, default=Path(os.environ.get('RUNNER_TEMP', '/tmp')) / 'native-profile.json')
    args = parser.parse_args()
    mode = os.environ['CHIBI_CI_SANITIZERS']
    require(mode in ('ON', 'OFF'), 'CHIBI_CI_SANITIZERS must be ON or OFF')
    receipt = verify(args.build.resolve(), args.log, mode == 'ON')
    args.output.write_text(json.dumps(receipt, indent=2) + '\n')
    print(f"Verified {receipt['profile']} Debug O1 ({receipt['backend']}), argv, assertions, symbols and links")


if __name__ == '__main__':
    main()
