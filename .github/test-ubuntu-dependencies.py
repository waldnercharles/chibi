#!/usr/bin/env python3
"""Offline fixtures for the CI APT helper; never runs APT or needs root."""

import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    'ubuntu_dependencies', Path(__file__).with_name('install-ubuntu-dependencies.py'))
apt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(apt)

# configure-apt-sources.sh at runner image ubuntu24/20261012.334.
MIRRORS = ('http://azure.archive.ubuntu.com/ubuntu/\tpriority:1\n'
           'https://archive.ubuntu.com/ubuntu/\tpriority:2\n'
           'https://security.ubuntu.com/ubuntu/\tpriority:3\n')
URI = 'mirror+file:/etc/apt/apt-mirrors.txt'
NEW_URI = 'mirror+file:/tmp/fixture/apt-mirrors.txt'
DEB822 = f'''# Ubuntu cloud image source fields; only URIs changes.
Types: deb deb-src
URIs: {URI}
Suites: noble noble-updates noble-backports
Components: main restricted universe multiverse
Architectures: amd64
Architectures-Add: i386
Architectures-Remove: arm64
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg

Types: deb
URIs: {URI}
Suites: noble-security
Components: main restricted universe multiverse
Signed-By: /usr/share/keyrings/ubuntu-archive-keyring.gpg
'''
LEGACY = f'''# deb {URI} noble-proposed main
deb [arch=amd64,i386 signed-by=/usr/share/keyrings/ubuntu-archive-keyring.gpg] {URI} noble main universe
deb-src {URI} noble-updates main restricted universe multiverse
'''
UNRELATED = '''Types: deb
URIs: https://packages.microsoft.com/ubuntu/24.04/prod
Suites: noble
Components: main
Architectures: amd64
Signed-By: /usr/share/keyrings/microsoft-prod.gpg
'''


class Sources(unittest.TestCase):
    def test_mirror_priority_only(self):
        self.assertEqual(apt.prefer_https(MIRRORS),
                         'http://azure.archive.ubuntu.com/ubuntu/\tpriority:3\n'
                         'https://archive.ubuntu.com/ubuntu/\tpriority:1\n'
                         'https://security.ubuntu.com/ubuntu/\tpriority:2\n')
        comment = '# ' + MIRRORS.splitlines()[0] + '\n\n'
        self.assertEqual(apt.prefer_https(comment + MIRRORS), comment + apt.prefer_https(MIRRORS))

    def test_invalid_mirrorlists(self):
        for text in ('', MIRRORS * 2, MIRRORS.replace('\t', ' '),
                     MIRRORS.replace('priority:2', 'priority:1'),
                     MIRRORS.replace('https://archive', 'http://archive'),
                     MIRRORS + 'https://example.org/ubuntu/\tpriority:4\n',
                     MIRRORS.replace('priority:2', 'priority:2 arch:amd64'),
                     MIRRORS.replace('priority:2', 'priority:2 suite:noble'),
                     MIRRORS.replace('priority:2', 'priority:2 component:main')):
            with self.subTest(text=text), self.assertRaises(ValueError):
                apt.prefer_https(text)

    def test_deb822_fields_and_other_repositories_preserved(self):
        text = (DEB822 + '\n' + UNRELATED).replace('\n', '\r\n')
        rewritten, count = apt.rewrite_source(text, '.sources', NEW_URI)
        self.assertEqual(count, 2)
        self.assertEqual(rewritten, text.replace(URI, NEW_URI))

    def test_legacy_options_and_comments_preserved(self):
        rewritten, count = apt.rewrite_source(LEGACY, '.list', NEW_URI)
        self.assertEqual(count, 2)
        self.assertEqual(rewritten.splitlines()[0], LEGACY.splitlines()[0])
        self.assertEqual(rewritten.splitlines()[1:], LEGACY.replace(URI, NEW_URI).splitlines()[1:])

    def test_unsupported_source_shapes(self):
        for text, suffix in ((f'URIs:\n {URI}\n', '.sources'),
                             (f'URIs: {URI} https://example.org/\n', '.sources'),
                             (f'URIs: {URI}/suffix\n', '.sources'),
                             (f'URL: {URI}\n', '.sources'),
                             ('URIs: http://azure.archive.ubuntu.com/ubuntu/\n', '.sources'),
                             (f'deb [unclosed {URI} noble main\n', '.list')):
            with self.subTest(text=text), self.assertRaises(ValueError):
                apt.rewrite_source(text, suffix, NEW_URI)

    def test_private_sources_and_fresh_lists(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            original = root / 'etc-apt'
            parts = original / 'sources.list.d'
            parts.mkdir(parents=True)
            files = {'apt-mirrors.txt': MIRRORS, 'sources.list': LEGACY,
                     'sources.list.d/ubuntu.sources': DEB822,
                     'sources.list.d/microsoft.sources': UNRELATED,
                     'sources.list.d/ignored.list.save': 'ignored by APT\n'}
            for name, content in files.items():
                (original / name).write_text(content)
            (original / 'lists').mkdir()
            (original / 'lists/stale-index').touch()
            destination = root / 'private'
            destination.mkdir(mode=0o700)
            options = apt.prepare_sources(original, destination)
            self.assertEqual(list((destination / 'lists').iterdir()), [])
            self.assertEqual((destination / 'sources.list.d/microsoft.sources').read_text(), UNRELATED)
            self.assertFalse((destination / 'sources.list.d/ignored.list.save').exists())
            self.assertEqual(destination.stat().st_mode & 0o777, 0o755)
            self.assertEqual((destination / 'apt-mirrors.txt').stat().st_mode & 0o777, 0o644)
            self.assertIn(f'Dir::State::lists={destination}/lists', options)
            self.assertIn(f'Dir::Etc::sourcelist={destination}/sources.list', options)
            self.assertIn(f'Dir::Etc::sourceparts={destination}/sources.list.d', options)
            self.assertIn('APT::Update::Error-Mode=any', options)
            for name, content in files.items():
                self.assertEqual((original / name).read_text(), content)

    def test_missing_mirror_source_fails(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'sources.list.d').mkdir()
            (root / 'sources.list.d/other.sources').write_text(UNRELATED)
            (root / 'apt-mirrors.txt').write_text(MIRRORS)
            destination = root / 'private'
            destination.mkdir()
            with self.assertRaisesRegex(ValueError, 'no supported'):
                apt.prepare_sources(root, destination)

    def test_deb822_only_runner_and_no_reused_destination(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / 'sources.list.d').mkdir()
            (root / 'sources.list.d/ubuntu.sources').write_text(DEB822)
            (root / 'apt-mirrors.txt').write_text(MIRRORS)
            destination = root / 'private'
            destination.mkdir()
            apt.prepare_sources(root, destination)
            self.assertEqual((destination / 'sources.list').read_text(), '')
            self.assertEqual((destination / 'sources.list.d/ubuntu.sources').read_text(),
                             DEB822.replace(URI, 'mirror+file:' + str(destination / 'apt-mirrors.txt')))
            with self.assertRaises(FileExistsError):
                apt.prepare_sources(root, destination)

    def test_install_uses_same_configuration_and_records_versions(self):
        options = ['-o', 'Dir::State::lists=/tmp/fresh', '-o', 'APT::Update::Error-Mode=any']
        packages = ['clang-18', 'libclang-18-dev', 'mesa-vulkan-drivers']
        for no_recommends in (False, True):
            with self.subTest(no_recommends=no_recommends), patch.object(apt.subprocess, 'run') as run:
                apt.install(options, packages, no_recommends)
                commands = [call.args[0] for call in run.call_args_list]
                self.assertEqual(commands[0], ['sudo', 'apt-get', *options, '-o',
                                              'Debug::Acquire::gpgv=true', 'update'])
                self.assertEqual(commands[1], ['apt-cache', *options, 'policy', *packages])
                flags = ['--no-install-recommends'] if no_recommends else []
                self.assertEqual(commands[2], ['sudo', 'apt-get', *options, 'install', '-y', *flags, *packages])
                self.assertEqual(commands[3], ['dpkg-query', '-W', '-f=${binary:Package}\t${Version}\n', *packages])
                self.assertTrue(all(call.kwargs == {'check': True} for call in run.call_args_list))

    def test_update_failure_never_installs(self):
        # Models a source/index/signature error. Actual signature verification
        # is APT's responsibility and is exercised by the hosted Linux jobs.
        with patch.object(apt.subprocess, 'run', side_effect=subprocess.CalledProcessError(100, 'apt-get')) as run:
            with self.assertRaises(subprocess.CalledProcessError):
                apt.install([], ['clang-18'], False)
            self.assertEqual(run.call_count, 1)
            self.assertEqual(run.call_args.args[0][-1], 'update')


if __name__ == '__main__':
    unittest.main()
