#!/usr/bin/env python3
"""Exercise real notice install rules and player packaging without downloading deps.

Run with Python 3 and CMake 3.25+: python3 test/tools/package_licenses.py
Use --output to retain the staged installs and inspected archives as evidence.
The executable, font and dependency artifacts are fixtures, not an engine build.
"""

import argparse
import json
import re
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.dont_write_bytecode = True
sys.path.insert(0, str(ROOT / 'tools'))
from package_sdk import package_files, package_identity
from package_player import assemble


def exercise(output, cmake, dependency_notice=None):
    source = output / 'source'
    source.mkdir(parents=True)
    # Copy exact repository notice bytes, including BOMs and CRLF where present.
    for name in ('LICENSE.md', 'COPYING', 'COPYING.LESSER', 'COPYING.EXAMPLES',
                 'docs/distribution-licensing.md', 'third_party/fmt/LICENSE',
                 'third_party/fmt/README.md'):
        destination = source / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ROOT / name, destination)
    shutil.copytree(ROOT / 'third_party/fonts', source / 'third_party/fonts',
                    ignore=shutil.ignore_patterns('*.ttf', '*.otf', '*.woff', '*.woff2'))
    # A binary sentinel must not enter a notice-only directory.
    font = source / 'third_party/fonts/nerd-fonts-symbols-only/test.ttf'
    font.write_bytes(b'not-a-real-font\0')
    builtin = source / 'src/runtime/ui/builtin_fonts.c'
    builtin.parent.mkdir(parents=True)
    builtin.write_text('int notice_fixture;\n')
    dependency = output / 'dependency'
    (dependency / 'api').mkdir(parents=True)
    (dependency / 'api/fixture.h').write_text('/* fixture public header */\n')
    (dependency / 'LICENSE').write_bytes(b'fixture license\r\n')
    (dependency / 'NOTICE').write_bytes(dependency_notice.read_bytes() if dependency_notice
                                      else b'fixture upstream attribution\r\n')
    (dependency / 'libfixture.a').write_bytes(b'!<arch>\n')
    (source / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.25)
project(notice_install_fixture LANGUAGES C)
set(CHIBI_SDK_ROOT "${{CMAKE_CURRENT_SOURCE_DIR}}")
include("{ROOT.as_posix()}/sdk/licenses.cmake")
function(chibi_sdk_headers)
endfunction()
include("{ROOT.as_posix()}/sdk/fonts.cmake")
add_library(notice_fixture STATIC IMPORTED)
set_target_properties(notice_fixture PROPERTIES
    IMPORTED_LOCATION "{dependency.as_posix()}/libfixture.a"
    INTERFACE_INCLUDE_DIRECTORIES "{dependency.as_posix()}/api")
include("{ROOT.as_posix()}/sdk/vendor_dependencies.cmake")
chibi_vendor_dependency(notice_fixture imported)
''')
    build = output / 'build'
    sdk = output / 'sdk'
    subprocess.run([cmake, '-S', str(source), '-B', str(build)], check=True)
    subprocess.run([cmake, '--install', str(build), '--prefix', str(sdk)], check=True)
    license_root = Path('share/chibi/licenses')
    expected = {}
    for name in ('LICENSE.md', 'COPYING', 'COPYING.LESSER', 'COPYING.EXAMPLES',
                 'docs/distribution-licensing.md'):
        expected[license_root / 'chibi' / name] = source / name
    for name in ('LICENSE', 'README.md'):
        expected[license_root / 'fmt' / name] = source / 'third_party/fmt' / name
    fonts = source / 'third_party/fonts'
    for path in fonts.rglob('*'):
        if path.is_file() and any(token in path.name for token in ('LICENSE', 'OFL', 'COPYING')):
            expected[license_root / 'fonts' / path.relative_to(fonts)] = path
    nerd = fonts / 'nerd-fonts-symbols-only'
    expected[license_root / 'fonts/nerd-fonts-symbols-only/NOTICES.md'] = nerd / 'NOTICES.md'
    for path in (nerd / 'licenses').rglob('*'):
        if path.is_file():
            expected[license_root / 'fonts/nerd-fonts-symbols-only' / path.relative_to(nerd)] = path
    for name in ('LICENSE', 'NOTICE'):
        expected[license_root / 'vendor/notice_fixture' / name] = dependency / name
    for relative, original in expected.items():
        assert (sdk / relative).read_bytes() == original.read_bytes(), relative
    assert not list((sdk / license_root).rglob('*.ttf')), 'Font bytes leaked into notices'
    for path in (sdk / license_root).rglob('*.md'):
        for target in re.findall(r'\]\(([^)]+)\)', path.read_text()):
            if not target.startswith(('https://', 'http://', '#')):
                assert (path.parent / target.split('#', 1)[0]).is_file(), (path, target)

    # Feed the actual player packager a valid fixture runtime package. This tests
    # its recursive notice copying, not engine compilation or license compliance.
    (sdk / 'bin').mkdir()
    binary = sdk / 'bin/chibi-player'
    binary.write_bytes(b'fixture executable, never executed\n')
    binary.chmod(0o755)
    engine = {'build_id': 'notice-fixture', 'project_version': 1}
    (sdk / 'share/chibi/engine.json').write_text(json.dumps(engine))
    manifest = {'manifest_format': 1, 'kind': 'runtime', 'package_version': 'fixture',
                'products': {'chibi-player': 'bin/chibi-player'}, 'targets': [],
                'requirements': {'project_format': 1},
                'engine': {'manifest': 'share/chibi/engine.json', 'build_id': engine['build_id']},
                'files': package_files(sdk)}
    manifest['build_id'] = package_identity(manifest)
    (sdk / 'share/chibi/package.json').write_text(json.dumps(manifest))
    player = output / 'player'
    assemble(sdk, player)
    for package in (sdk, player):
        archive = output / (package.name + '.tar.gz')
        with tarfile.open(archive, 'w:gz') as stream:
            stream.add(package, arcname=package.name)
        with tarfile.open(archive, 'r:gz') as stream:
            for relative, original in expected.items():
                member = stream.extractfile(f'{package.name}/{relative.as_posix()}')
                assert member is not None and member.read() == original.read_bytes(), relative
            assert not any(name.endswith('.ttf') for name in stream.getnames())
    summary = {'notice_files_verified_per_archive': len(expected),
               'archives': ['sdk.tar.gz', 'player.tar.gz'],
               'scope': 'Real CMake notice installs and player packaging, fixture binaries; no dependency downloads.'}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', default=shutil.which('cmake'))
    parser.add_argument('--output', type=Path)
    parser.add_argument('--dependency-notice', type=Path,
                        help='Use an already cached upstream NOTICE instead of the synthetic fixture')
    args = parser.parse_args()
    if not args.cmake:
        parser.error('CMake 3.25+ is required')
    if args.output:
        output = args.output.resolve()
        if output.exists():
            parser.error('--output must be a new directory')
        exercise(output, args.cmake, args.dependency_notice)
    else:
        with tempfile.TemporaryDirectory(prefix='chibi-notices-') as directory:
            exercise(Path(directory), args.cmake, args.dependency_notice)


if __name__ == '__main__':
    main()
