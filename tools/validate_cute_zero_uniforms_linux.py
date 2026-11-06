#!/usr/bin/env python3
"""Compare the existing C draw case with only Cute's empty-uniform guard changed."""
import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.parse
import urllib.request


ROOT = Path(__file__).resolve().parents[1]
TEST = ROOT / 'test/render/draw_uniforms.c'
PATCH = ROOT / 'third_party/cute_framework/draw-zero-uniforms.patch'
CUTE_COMMIT = '96e6034aa6bc44665856ee480962a9ddab0b87cc'
GUARDED = b'next->u.size && CF_MEMCMP(next->u.data, cmd->u.data, next->u.size)'
UNGUARDED = b'CF_MEMCMP(next->u.data, cmd->u.data, next->u.size)'
SUMMARY = 'Summary: Passed: 1 Failed: 0 Total: 1 Suites: 0 Asserts: 5'


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n')


def digest(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(block)
    return value.hexdigest()


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def git(*arguments):
    return subprocess.check_output(['git', *arguments], cwd=ROOT, text=True).strip()


def source_identity(args):
    require(re.fullmatch('[0-9a-f]{40}', args.source_sha), 'source SHA must be full lowercase hex')
    event = os.environ['GITHUB_EVENT_NAME']
    payload = json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_text())
    if event == 'pull_request':
        head = payload['pull_request']['head']
        require(head['repo']['full_name'] == os.environ['GITHUB_REPOSITORY'], 'same-repository PR required')
        require(head['sha'] == args.source_sha, 'source differs from the PR event head')
    else:
        require(event == 'workflow_dispatch', 'only PR and manual events are supported')
        require(args.source_sha == os.environ['GITHUB_SHA'], 'dispatch source differs from github.sha')
        require(re.fullmatch('[0-9a-f]{40}', args.source_tree), 'manual dispatch needs the exact tree')
    require(git('rev-parse', 'HEAD') == args.source_sha, 'checkout differs from frozen source')
    tree = git('rev-parse', 'HEAD^{tree}')
    require(not args.source_tree or tree == args.source_tree, 'checkout differs from expected tree')
    require(not git('status', '--porcelain', '--untracked-files=normal'), 'source checkout is dirty')
    return {'revision': args.source_sha, 'tree': tree, 'dirty': False, 'event': event,
            'workflow_trigger_sha': os.environ['GITHUB_SHA'],
            'pr': payload.get('number'), 'run_id': os.environ.get('GITHUB_RUN_ID'),
            'run_attempt': os.environ.get('GITHUB_RUN_ATTEMPT')}


def record_native(binary, receipt):
    # This process runs INSIDE xvfb-run and waits for the actual C binary.
    require(not receipt.exists(), 'native receipt already exists')
    record = {'recorder_pid': os.getpid(), 'pgid': os.getpgrp(), 'native_pid': None,
              'native_returncode': None, 'normalized_exit': None,
              'command': [str(binary), '--draw-uniforms']}
    write_json(receipt, record)
    child = subprocess.Popen(record['command'])
    record['native_pid'] = child.pid
    write_json(receipt, record)
    code = child.wait()
    record.update(native_returncode=code, normalized_exit=code if code >= 0 else 128 - code)
    write_json(receipt, record)
    return record['normalized_exit']


def inventory(prefix):
    return {str(path.relative_to(prefix)): ('symlink:' + os.readlink(path) if path.is_symlink()
            else digest(path)) for path in sorted(prefix.rglob('*')) if path.is_file() or path.is_symlink()}


class DrawingRun:
    def __init__(self, args):
        self.args, self.evidence, self.work = args, args.evidence, args.work
        self.results = {'status': 'incomplete', 'phases': {}}
        self.env = dict(os.environ)
        self.env.pop('CHIBI_RUNNER_PROCESS_GROUP', None)
        self.env.update(CC='/usr/bin/clang-18', CXX='/usr/bin/clang++-18',
                        CFLAGS='', CXXFLAGS='', CPPFLAGS='', LDFLAGS='',
                        CCACHE_DIR=str(self.work / 'ccache'),
                        SDL_VIDEODRIVER='x11', SDL_VIDEO_DRIVER='x11',
                        SDL_GPU_DRIVER='vulkan', SDL_AUDIODRIVER='dummy', SDL_LOGGING='gpu=info',
                        ASAN_OPTIONS='abort_on_error=1',
                        UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1:external_symbolizer_path=/usr/bin/llvm-symbolizer-18',
                        XDG_RUNTIME_DIR=str(self.work / 'runtime'))

    def command(self, name, arguments, check=True, cwd=ROOT):
        arguments = [str(argument) for argument in arguments]
        started = time.monotonic()
        with (self.evidence / (name + '.log')).open('wb') as output:
            result = subprocess.run(arguments, cwd=cwd, env=self.env, stdout=output, stderr=output)
        with (self.evidence / 'commands.jsonl').open('a') as output:
            output.write(json.dumps({'name': name, 'arguments': arguments, 'cwd': str(cwd),
                                    'returncode': result.returncode,
                                    'elapsed_seconds': time.monotonic() - started}) + '\n')
        if check:
            require(result.returncode == 0, name + ' failed; see full retained log')
        return result.returncode

    def compile_entry(self, build, source):
        entries = json.loads((build / 'compile_commands.json').read_text())
        found = [entry for entry in entries if Path(entry['file']).resolve() == source.resolve()]
        require(len(found) == 1, 'expected one compile command for ' + str(source))
        entry = found[0]
        arguments = entry.get('arguments') or shlex.split(entry['command'])
        require('-fsanitize=address,undefined' in arguments, 'actual object is not ASan+UBSan instrumented')
        require(not any(a.startswith(('-include', '-imacros', '-fno-sanitize')) for a in arguments),
                'forced declarations or sanitizer overrides are forbidden')
        output = Path(entry.get('output') or arguments[arguments.index('-o') + 1])
        if not output.is_absolute():
            output = Path(entry['directory']) / output
        require(output.is_file(), 'compiled object is missing')
        return entry, arguments, output

    def draw(self, phase, binary, line):
        native_path = self.evidence / (phase + '-native.json')
        code = self.command(phase + '-draw', [sys.executable, ROOT / 'tools/test_policy.py', 'native', '--',
                            'xvfb-run', '-a', sys.executable, Path(__file__).resolve(),
                            '--record-native', '--binary', binary, '--native-receipt', native_path], check=False)
        native = json.loads(native_path.read_text()) if native_path.exists() else None
        log = (self.evidence / (phase + '-draw.log')).read_text(errors='replace')
        remaining, inspected = None, False
        if native and native.get('pgid') and native['pgid'] != os.getpgrp():
            rows = subprocess.check_output(['ps', '-eo', 'pid=,pgid='], text=True)
            remaining = [int(pid) for pid, group in (row.split() for row in rows.splitlines())
                         if int(group) == native['pgid']]
            inspected = True
        cleanup = bool(native and native['normalized_exit'] is not None and
                       code == native['normalized_exit'] and inspected and not remaining and
                       'FAIL:' not in log and 'Traceback (most recent call last)' not in log)
        result = {'native_result': 'unknown' if not native or native['native_returncode'] is None
                  else 'success' if native['native_returncode'] == 0 else 'failure',
                  'native': native, 'supervisor_exit': code,
                  'cleanup_result': 'success' if cleanup else 'failure-or-unknown',
                  'remaining_owned_processes': remaining,
                  'inspection_scope': 'recorded supervised process group after supervisor exit; Linux subreaper cleanup remains owned by test_policy.py',
                  'log': phase + '-draw.log'}
        self.results['phases'][phase] = result
        write_json(self.evidence / 'results.json', self.results)
        require(cleanup, phase + ' supervisor/cleanup did not match the independently recorded native result')
        if phase == 'red':
            require(native['native_returncode'] != 0, 'unguarded draw did not fail')
            required = [f'cute_draw.cpp:{line}:', 's_process_command', 'cf_render_layers_to',
                        'adjacent_draw_commands_without_uniforms', 'SUMMARY: UndefinedBehaviorSanitizer']
            require(all(text in log for text in required) and
                    re.search(r'null pointer passed as argument [12], which is declared to never be null', log),
                    'red failure is not the intended Cute empty-uniform comparison')
            result['native_result'] = 'expected-null-argument-regression'
        else:
            require(native['native_returncode'] == 0 and code == 0, 'guarded draw failed')
            require(re.search(r'^' + re.escape(SUMMARY) + r'\s*$', log, re.MULTILINE), 'expected one case and five assertions')
            require('runtime error:' not in log and 'ERROR: AddressSanitizer' not in log,
                    'guarded draw reported a sanitizer failure')

    def phase_provenance(self, phase, deps, build, cute, prefix, library):
        entry, arguments, obj = self.compile_entry(deps, cute / 'src/cute_draw.cpp')
        test_entry, test_arguments, test_obj = self.compile_entry(build, TEST)
        require('-UNDEBUG' in test_arguments, 'C test assertions must remain enabled')
        write_json(self.evidence / (phase + '-compile.json'), {'cute': entry, 'test': test_entry})
        self.command(phase + '-object-symbols', ['llvm-nm-18', '--undefined-only', obj])
        self.command(phase + '-archive-symbols', ['llvm-nm-18', '--print-file-name', '--undefined-only', library])
        symbols = (self.evidence / (phase + '-object-symbols.log')).read_text()
        archive_symbols = (self.evidence / (phase + '-archive-symbols.log')).read_text()
        require('__ubsan_handle_nonnull_arg' in symbols, 'Cute draw object lacks nonnull instrumentation')
        require(any('cute_draw.cpp.o' in row and '__ubsan_handle_nonnull_arg' in row
                    for row in archive_symbols.splitlines()), 'Cute archive lacks the instrumented draw member')
        self.command(phase + '-link-commands', ['ninja', '-C', build, '-t', 'commands', 'test_tools'])
        links = (self.evidence / (phase + '-link-commands.log')).read_text()
        require(str(prefix / 'lib/chibi-vendor/libchibi_vendor_cute.a') in links,
                'built driver link does not name the private Cute archive')
        require('-fsanitize=address,undefined' in links, 'driver link lacks sanitizer runtime flags')
        shutil.copyfile(obj, self.evidence / (phase + '-cute_draw.cpp.o'))
        shutil.copyfile(test_obj, self.evidence / (phase + '-draw_uniforms.c.o'))
        binary = build / 'test/tools/test_tools'
        record = {'cute_source': digest(cute / 'src/cute_draw.cpp'), 'cute_object': digest(obj),
                  'cute_archive': digest(library), 'private_cute': digest(prefix / 'lib/chibi-vendor/libchibi_vendor_cute.a'),
                  'test_source': digest(TEST), 'test_object': digest(test_obj), 'driver': digest(binary),
                  'cute_compile': entry, 'test_compile': test_entry,
                  'prefix_inventory': inventory(prefix)}
        write_json(self.evidence / (phase + '-provenance.json'), record)
        require(record['private_cute'] == record['cute_archive'], 'private Cute differs from the built archive')
        if phase == 'red':
            preprocessing = arguments[:]
            del preprocessing[preprocessing.index('-o'):preprocessing.index('-o') + 2]
            preprocessing.remove('-c')
            self.command('real-glibc-cute-preprocessed.ii', [*preprocessing, '-E'], cwd=entry['directory'])
        return record

    def run(self):
        require(sys.platform == 'linux' and platform.machine() == 'x86_64', 'Ubuntu x86_64 required')
        require(not self.work.exists(), 'private work directory must be new')
        self.work.mkdir(parents=True)
        (self.work / 'runtime').mkdir(mode=0o700)
        shutil.copyfile(ROOT / 'third_party/CMakeLists.txt', self.evidence / 'dependency-CMakeLists.txt')
        shutil.copyfile(PATCH, self.evidence / 'draw-zero-uniforms.patch')
        write_json(self.evidence / 'patch-file-hashes.json', {
            str(path.relative_to(ROOT)): digest(path)
            for path in sorted((ROOT / 'third_party').rglob('*.patch'))})
        write_json(self.evidence / 'compiler-file-hashes.json', {
            str(Path(shutil.which(name)).resolve()): digest(Path(shutil.which(name)).resolve())
            for name in ['clang-18', 'clang++-18', 'llvm-symbolizer-18']})
        for name, command in [('clang', ['clang-18', '--version']), ('clangxx', ['clang++-18', '--version']),
                              ('cmake', ['cmake', '--version']), ('glibc', ['ldd', '--version']),
                              ('system', ['uname', '-a']), ('packages', ['dpkg-query', '-W', 'libc6-dev', 'mesa-vulkan-drivers', 'xvfb', 'llvm-18'])]:
            self.command('version-' + name, command)
        shutil.copyfile('/usr/include/string.h', self.evidence / 'system-string.h')
        icds = sorted(Path('/usr/share/vulkan/icd.d').glob('lvp_icd*.json'))
        require(len(icds) == 1, 'exactly one lavapipe ICD is required; no backend fallback')
        self.env['VK_ICD_FILENAMES'] = str(icds[0])
        shutil.copyfile(icds[0], self.evidence / 'lavapipe-icd.json')
        self.env['CHIBI_EXECUTION_PROFILE'] = 'gpu-check'
        write_json(self.evidence / 'render-environment.json', {key: self.env[key] for key in
                   ['SDL_VIDEODRIVER', 'SDL_VIDEO_DRIVER', 'SDL_GPU_DRIVER', 'SDL_AUDIODRIVER',
                    'SDL_LOGGING', 'VK_ICD_FILENAMES', 'XDG_RUNTIME_DIR', 'ASAN_OPTIONS', 'UBSAN_OPTIONS',
                    'CHIBI_EXECUTION_PROFILE']})
        self.command('vulkan-inventory', ['xvfb-run', '-a', 'vulkaninfo', '--summary'])

        lines = (ROOT / 'third_party/archives.tsv').read_text().splitlines()
        repositories = [line.removeprefix('# repository ') for line in lines if line.startswith('# repository ')]
        pins = [line.split('\t') for line in lines if line.startswith('linux-x64\taddress-undefined\t')]
        require(len(repositories) == 1 and re.fullmatch(r'[\w.-]+/[\w.-]+', repositories[0]), 'invalid archive repository')
        require(len(pins) == 1 and len(pins[0]) == 4 and re.fullmatch('[0-9a-f]{64}', pins[0][3]), 'one exact Linux sanitizer pin required')
        _, _, release, pinned = pins[0]
        require(re.fullmatch(r'[\w.-]+', release), 'invalid archive release')
        archive = self.work / 'baseline.tar.gz'
        url = f'https://github.com/{repositories[0]}/releases/download/{urllib.parse.quote(release)}/third-party-linux-x64-address-undefined.tar.gz'
        urllib.request.urlretrieve(url, archive)
        require(digest(archive) == pinned, 'baseline archive SHA256 mismatch')
        write_json(self.evidence / 'baseline-archive.json', {'repository': repositories[0], 'release': release,
                   'sha256': pinned, 'bytes': archive.stat().st_size, 'manifest_sha256': digest(ROOT / 'third_party/archives.tsv')})
        pristine = self.work / 'pristine'
        pristine.mkdir()
        with tarfile.open(archive) as bundle:
            bundle.extractall(pristine, filter='data')
        prefix = self.work / 'private-cache' / pinned
        prefix.mkdir(parents=True)
        self.command('private-copy', ['cp', '-a', '--reflink=auto', str(pristine) + '/.', prefix])
        baseline = inventory(pristine)
        require(inventory(prefix) == baseline, 'private copy differs from verified baseline')
        write_json(self.evidence / 'baseline-inventory.json', baseline)

        deps, build = self.work / 'dependencies', self.work / 'engine'
        self.command('dependency-configure', ['cmake', '-S', ROOT / 'third_party', '-B', deps, '-G', 'Ninja',
                     '-DCMAKE_C_COMPILER=clang-18', '-DCMAKE_CXX_COMPILER=clang++-18',
                     '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DCHIBI_SANITIZERS=address,undefined',
                     '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DCMAKE_CXX_SCAN_FOR_MODULES=OFF',
                     '-DCMAKE_PROJECT_INCLUDE=', '-DCMAKE_PROJECT_INCLUDE_BEFORE='])
        cute = deps / '_deps/chibi_cute_framework-src'
        self.command('cute-source-revision', ['git', '-C', cute, 'rev-parse', 'HEAD'])
        require((self.evidence / 'cute-source-revision.log').read_text().strip() == CUTE_COMMIT, 'Cute source pin changed')
        source = cute / 'src/cute_draw.cpp'
        green = source.read_bytes()
        require(green.count(GUARDED) == 1, 'expected the single reviewed guard')
        self.command('remove-guard', ['git', '-C', cute, 'apply', '--reverse', PATCH])
        red = source.read_bytes()
        require(red == green.replace(GUARDED, UNGUARDED, 1), 'red source changed beyond the guard')
        line = next(n for n, text in enumerate(red.decode().splitlines(), 1) if UNGUARDED.decode() in text)
        (self.evidence / 'red-cute_draw.cpp').write_bytes(red)
        (self.evidence / 'green-cute_draw.cpp').write_bytes(green)
        (self.evidence / 'guard-source.diff').write_text(''.join(difflib.unified_diff(red.decode().splitlines(True), green.decode().splitlines(True), 'red/cute_draw.cpp', 'green/cute_draw.cpp')))
        self.command('red-cute-build', ['cmake', '--build', deps, '--target', 'cute', '--parallel', '2'])
        library = deps / 'cute_framework/libcute.a'
        shutil.copyfile(library, prefix / 'lib/chibi-vendor/libchibi_vendor_cute.a')
        self.command('driver-configure', ['sh', ROOT / 'tools/configure_native.sh', build, '-G', 'Ninja',
                     '-DCMAKE_C_COMPILER=clang-18', '-DCMAKE_CXX_COMPILER=clang++-18', '-DCMAKE_BUILD_TYPE=Debug',
                     '-DCHIBI_ENABLE_SANITIZERS=ON', '-DCHIBI_SANITIZERS=address,undefined', '-DCHIBI_ENABLE_WARNINGS=ON',
                     '-DLIBCLANG_ROOT=/usr/lib/llvm-18', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                     '-DCHIBI_THIRD_PARTY_CACHE=' + str(self.work / 'private-cache'),
                     '-DCMAKE_PROJECT_INCLUDE=', '-DCMAKE_PROJECT_INCLUDE_BEFORE='])
        self.command('red-driver-build', ['cmake', '--build', build, '--target', 'test_tools', '--parallel', '2'])
        red_proof = self.phase_provenance('red', deps, build, cute, prefix, library)
        cute_key = 'lib/chibi-vendor/libchibi_vendor_cute.a'
        require({k: v for k, v in red_proof['prefix_inventory'].items() if k != cute_key} ==
                {k: v for k, v in baseline.items() if k != cute_key}, 'non-Cute baseline files changed')
        self.draw('red', build / 'test/tools/test_tools', line)
        self.command('restore-guard', ['git', '-C', cute, 'apply', PATCH])
        require(source.read_bytes() == green, 'guarded source was not restored exactly')
        self.command('green-cute-build', ['cmake', '--build', deps, '--target', 'cute', '--parallel', '2'])
        shutil.copyfile(library, prefix / cute_key)
        self.command('green-driver-build', ['cmake', '--build', build, '--target', 'test_tools', '--parallel', '2'])
        green_proof = self.phase_provenance('green', deps, build, cute, prefix, library)
        for key in ['test_source', 'test_object', 'cute_compile', 'test_compile']:
            require(red_proof[key] == green_proof[key], 'red/green changed ' + key)
        for key in ['cute_object', 'cute_archive', 'driver']:
            require(red_proof[key] != green_proof[key], 'guard did not change ' + key)
        require({k: v for k, v in red_proof['prefix_inventory'].items() if k != cute_key} ==
                {k: v for k, v in green_proof['prefix_inventory'].items() if k != cute_key}, 'other private dependencies changed')
        self.draw('green', build / 'test/tools/test_tools', line)
        self.results['status'] = 'passed-linux-glibc-draw-comparison'
        self.results['renderer_observation'] = 'Requested X11/Vulkan with sole lavapipe ICD; native backend/device is unknown unless identified in the retained native log. Discovery inventory is separate.'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-sha')
    parser.add_argument('--source-tree', default='')
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--work', type=Path)
    parser.add_argument('--verify-source-only', action='store_true')
    parser.add_argument('--record-native', action='store_true')
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--native-receipt', type=Path)
    args = parser.parse_args()
    if args.record_native:
        return record_native(args.binary, args.native_receipt)
    args.evidence.mkdir(parents=True, exist_ok=True)
    try:
        initial = source_identity(args)
    except Exception as error:
        write_json(args.evidence / 'source-error.json', {'error': str(error)})
        raise
    write_json(args.evidence / 'source.json', initial)
    if args.verify_source_only:
        return 0
    require(args.work and args.work.is_absolute() and not args.work.is_relative_to(ROOT), 'private work must be outside the source checkout')
    run = DrawingRun(args)
    try:
        run.run()
    except Exception as error:
        run.results.update(status='failed', error=str(error))
    finally:
        try:
            require(source_identity(args) == initial, 'source identity changed during validation')
            run.results['source_unchanged'] = True
        except Exception as error:
            run.results.update(status='failed', source_unchanged=False, source_error=str(error))
        write_json(args.evidence / 'results.json', run.results)
    return 0 if run.results['status'] == 'passed-linux-glibc-draw-comparison' else 1


if __name__ == '__main__':
    sys.exit(main())
