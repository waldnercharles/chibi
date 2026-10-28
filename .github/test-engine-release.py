#!/usr/bin/env python3
"""Offline fixtures: no release creation, upload or deletion calls."""
import copy
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import tarfile
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('release', Path(__file__).with_name('engine-release.py'))
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)
ENGINE = 'a' * 40
EMSDK = '6.0.10'


class ReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name)
        self.build = {'format': 1, 'engine_sha': ENGINE, 'emsdk_version': EMSDK,
                      'platform': 'ubuntu-24.04-x64', 'web_threads': False}
        (self.path / 'build.json').write_text(json.dumps(self.build))
        for name in release.ARCHIVES:
            self.archive(name)
        self.sums()

    def archive(self, name, revision=ENGINE, dirty=False, duplicate=False):
        member = ('sdk/' if name == release.ARCHIVES[0] else './') + 'share/chibi/package.json'
        data = json.dumps({'source': {'revision': revision, 'dirty': dirty}}).encode()
        with tarfile.open(self.path / name, 'w:gz') as archive:
            for _ in range(2 if duplicate else 1):
                info = tarfile.TarInfo(member)
                info.size = len(data)
                archive.addfile(info, io.BytesIO(data))

    def sums(self):
        (self.path / 'SHA256SUMS').write_text(''.join(
            hashlib.sha256((self.path / name).read_bytes()).hexdigest() + '  ' + name + '\n'
            for name in sorted(release.MEMBERS)))

    def validate(self):
        release.validate(self.path, ENGINE, EMSDK)

    def test_complete_package(self):
        self.validate()

    def test_asset_inventory(self):
        for operation in ('missing', 'extra', 'empty', 'symlink'):
            with self.subTest(operation=operation):
                target = self.path / 'build.json'
                original = target.read_bytes()
                if operation == 'extra':
                    (self.path / 'unexpected').write_text('no')
                elif operation == 'missing':
                    target.unlink()
                elif operation == 'empty':
                    target.write_bytes(b'')
                else:
                    target.unlink()
                    target.symlink_to('SHA256SUMS')
                with self.assertRaises(ValueError):
                    self.validate()
                if target.is_symlink():
                    target.unlink()
                target.write_bytes(original)
                (self.path / 'unexpected').unlink(missing_ok=True)

    def test_modified_bytes(self):
        (self.path / 'build.json').write_text('{}')
        with self.assertRaisesRegex(ValueError, 'Checksum mismatch'):
            self.validate()

    def test_checksum_manifest(self):
        target = self.path / 'SHA256SUMS'
        original = target.read_text()
        for text in (original + original.splitlines()[0] + '\n',
                     '\n'.join(original.splitlines()[1:]) + '\n',
                     original.replace('build.json', '../build.json')):
            with self.subTest(text=text):
                target.write_text(text)
                with self.assertRaises(ValueError):
                    self.validate()

    def test_build_identity(self):
        for key, value in [('engine_sha', 'b' * 40), ('emsdk_version', 'wrong'),
                           ('web_threads', True), ('platform', 'other'), ('format', 2)]:
            with self.subTest(key=key):
                record = dict(self.build, **{key: value})
                (self.path / 'build.json').write_text(json.dumps(record))
                self.sums()
                with self.assertRaisesRegex(ValueError, 'Build record'):
                    self.validate()

    def test_each_archive_identity(self):
        for name in release.ARCHIVES:
            for kwargs in ({'revision': 'b' * 40}, {'dirty': True}, {'duplicate': True}):
                with self.subTest(name=name, kwargs=kwargs):
                    self.archive(name, **kwargs)
                    self.sums()
                    with self.assertRaises(ValueError):
                        self.validate()
                    self.archive(name)

    def states(self, published=True):
        ref = {'object': {'type': 'commit', 'sha': ENGINE}}
        record = {'tag_name': 'engine-' + ENGINE, 'draft': not published,
                  'assets': [{'name': name, 'state': 'uploaded', 'size': 1} for name in release.ASSETS]}
        return record, ref

    def inspect(self, record, ref):
        with patch.object(release, 'api', side_effect=[record, ref]):
            return release.inspect('waldnercharles/chibi', ENGINE)

    def test_absent_and_interrupted_draft(self):
        self.assertEqual(self.inspect(None, None),
                         {'complete': False, 'tag_exists': False, 'release_exists': False})
        record, ref = self.states(False)
        record['assets'] = []
        self.assertEqual(self.inspect(record, ref),
                         {'complete': False, 'tag_exists': True, 'release_exists': True})

    def test_existing_complete_release_is_noop(self):
        self.assertTrue(self.inspect(*self.states())['complete'])

    def test_unknown_or_duplicate_draft_assets_are_not_resumed(self):
        for variant in ('unknown', 'duplicate'):
            record, ref = self.states(False)
            if variant == 'unknown':
                record['assets'].append({'name': 'extra', 'size': 1, 'state': 'uploaded'})
            else:
                record['assets'].append(record['assets'][0])
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                self.inspect(record, ref)

    def draft(self):
        record, ref = self.states(False)
        for asset in record['assets']:
            content = (self.path / asset['name']).read_bytes()
            asset.update(size=len(content), digest='sha256:' + hashlib.sha256(content).hexdigest())
        return record, ref

    def verify_draft(self, record, ref):
        with patch.object(release, 'api', side_effect=[record, ref]):
            release.verify_draft('waldnercharles/chibi', self.path, ENGINE, EMSDK)

    def test_verified_remote_draft_matches_all_five_files(self):
        self.verify_draft(*self.draft())

    def test_remote_draft_must_match_exact_identity_inventory_bytes(self):
        for variant in ('published', 'wrong-tag', 'wrong-sha', 'annotated', 'missing-ref',
                        'extra', 'missing', 'duplicate', 'size', 'digest', 'no-digest', 'pending'):
            record, ref = self.draft()
            if variant == 'published':
                record['draft'] = False
            elif variant == 'wrong-tag':
                record['tag_name'] = 'engine-' + 'b' * 40
            elif variant == 'wrong-sha':
                ref['object']['sha'] = 'b' * 40
            elif variant == 'annotated':
                ref['object']['type'] = 'tag'
            elif variant == 'missing-ref':
                ref = None
            elif variant == 'extra':
                record['assets'].append(dict(record['assets'][0], name='extra'))
            elif variant == 'missing':
                record['assets'].pop()
            elif variant == 'duplicate':
                record['assets'][0] = copy.deepcopy(record['assets'][1])
            elif variant == 'size':
                record['assets'][0]['size'] += 1
            elif variant == 'digest':
                record['assets'][0]['digest'] = 'sha256:' + '0' * 64
            elif variant == 'no-digest':
                record['assets'][0]['digest'] = None
            else:
                record['assets'][0]['state'] = 'new'
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                self.verify_draft(record, ref)

    def test_artifact_attempt_identity_is_passed_between_jobs(self):
        workflow = Path(__file__).with_name('workflows').joinpath('publish.yml').read_text()
        self.assertIn('artifact=engine-$GITHUB_SHA-$GITHUB_RUN_ID-$GITHUB_RUN_ATTEMPT', workflow)
        self.assertIn('artifact: ${{ steps.candidate.outputs.artifact }}', workflow)
        self.assertIn('name: ${{ steps.candidate.outputs.artifact }}', workflow)
        self.assertIn('name: ${{ needs.packages.outputs.artifact }}', workflow)
        self.assertNotIn('\nconcurrency:', workflow)
        self.assertIn('group: engine-release-${{ needs.packages.outputs.engine }}\n      cancel-in-progress: false', workflow)

    def test_published_incomplete_extra_duplicate_empty_unuploaded(self):
        for variant in ('missing', 'extra', 'duplicate', 'empty', 'unuploaded'):
            record, ref = self.states()
            if variant == 'missing':
                record['assets'].pop()
            elif variant == 'extra':
                record['assets'].append({'name': 'extra', 'size': 1, 'state': 'uploaded'})
            elif variant == 'duplicate':
                record['assets'][0] = copy.deepcopy(record['assets'][1])
            elif variant == 'empty':
                record['assets'][0]['size'] = 0
            else:
                record['assets'][0]['state'] = 'new'
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                self.inspect(record, ref)

    def test_mismatched_missing_and_annotated_tags(self):
        for ref in (None, {'object': {'type': 'tag', 'sha': ENGINE}},
                    {'object': {'type': 'commit', 'sha': 'b' * 40}}):
            with self.subTest(ref=ref), self.assertRaises(ValueError):
                self.inspect(self.states()[0], ref)

    def test_only_404_is_absence(self):
        for status in (403, 500, None, 404):
            result = subprocess.CompletedProcess([], 1, '', f'gh: failure (HTTP {status})')
            with patch.object(release.subprocess, 'run', return_value=result) as run:
                if status == 404:
                    self.assertIsNone(release.api('waldnercharles/chibi', 'releases/tags/engine-' + ENGINE))
                else:
                    with self.assertRaises(ValueError):
                        release.api('waldnercharles/chibi', 'releases/tags/engine-' + ENGINE)
                self.assertEqual(run.call_args.args[0][1:4], ['api', '--method', 'GET'])


if __name__ == '__main__':
    unittest.main()
