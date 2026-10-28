#!/usr/bin/env python3
"""Offline regression checks for the read-only release inventory and planner."""
import base64
from copy import deepcopy
from datetime import datetime, timezone
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('retention', ROOT / '.github/release-retention.py')
retention = importlib.util.module_from_spec(spec)
spec.loader.exec_module(retention)
NOW = datetime(2026, 10, 28, 18, tzinfo=timezone.utc)
OBSERVED = '2026-10-28T18:00:00Z'


def sha(n):
    return f'{n:040x}'


def release(n, tag=None):
    tag = tag or 'engine-' + sha(n)
    return {'id': n, 'tag_name': tag, 'draft': False, 'prerelease': True,
            'immutable': False, 'published_at': f'2026-10-28T{n % 18:02}:00:00Z',
            'html_url': f'https://github.com/{retention.REPOSITORY}/releases/tag/{tag}',
            'assets': [{'id': n * 10 + i, 'name': name, 'size': 100, 'state': 'uploaded',
                        'digest': 'sha256:' + 'a' * 64, 'updated_at': OBSERVED,
                        'browser_download_url': f'https://github.com/{retention.REPOSITORY}/releases/download/{tag}/{name}'}
                       for i, name in enumerate(sorted(retention.ASSETS))]}


def snapshot(releases=None):
    releases = releases or [release(n) for n in range(1, 7)]
    return {'format': 1, 'repository': retention.REPOSITORY, 'main': sha(999), 'prs': [],
            'studio': {'main': sha(100), 'prs': []},
            'pins': [], 'absent_pin_files': [], 'blockers': [], 'observed_at': OBSERVED,
            'releases': releases, 'tags': [{'ref': 'refs/tags/' + r['tag_name'],
                                          'object': {'type': 'commit', 'sha': r['tag_name'].removeprefix('engine-')}}
                                         for r in releases],
            'deployment': {'url': retention.STUDIO_MANIFEST, 'engine': sha(6),
                           'studio': sha(100), 'observed_at': OBSERVED}}


class RetentionTests(unittest.TestCase):
    def plan(self, data, keep=2, protected=()):
        return retention.plan(data, {'keep_engine_snapshots': keep, 'protected_tags': list(protected)}, NOW)

    def test_newest_complete_are_sorted_by_publication_not_tag_or_api_order(self):
        data = snapshot()
        data['releases'].reverse()
        data['releases'][0]['published_at'] = '2026-10-27T00:00:00Z'
        result = self.plan(data)
        self.assertEqual([r['tag'] for r in result['candidates']], ['engine-' + sha(n) for n in (1, 2, 3)])
        self.assertTrue(result['ready_for_review'])
        self.assertFalse(result['deletion_authorized'])
        self.assertEqual(result['candidate_bytes'], 1500)
        self.assertEqual(result['candidates'][0]['assets'][0]['id'], 10)
        self.assertEqual(result['candidates'][0]['tag_sha'], sha(1))

    def test_deployed_main_open_pr_and_explicit_rollback_are_protected(self):
        data = snapshot()
        data['deployment']['engine'] = sha(1)
        data['main'] = sha(2)
        data['prs'] = [{'number': 9, 'head': sha(3), 'base': sha(4)}]
        result = self.plan(data, keep=1, protected=['engine-' + sha(5)])
        self.assertEqual(result['candidates'], [])
        self.assertEqual(len(result['retained']), 6)

    def test_all_dependency_and_stable_releases_remain_out_of_scope(self):
        data = snapshot()
        for n, tag in ((20, 'compiler-runtime-old'), (21, 'third-party-old'),
                       (22, 'compiler-bootstrap-old'), (23, 'v0.1.0')):
            data['releases'].append(release(n, tag))
        data['releases'][0]['prerelease'] = False
        data['pins'] = [{'tag': 'compiler-runtime-old', 'source': 'main:runtime.json'}]
        result = self.plan(data)
        self.assertEqual(len(result['candidates']), 3)
        self.assertTrue(result['ready_for_review'])

    def test_draft_incomplete_extra_and_unuploaded_assets_are_held(self):
        mutations = [lambda r: r.update(draft=True), lambda r: r['assets'].pop(),
                     lambda r: r['assets'].append(dict(r['assets'][0], id=9999, name='unexpected')),
                     lambda r: r['assets'][0].update(size=0),
                     lambda r: r['assets'][0].update(state='new'),
                     lambda r: r['assets'][0].update(name=r['assets'][1]['name'])]
        for mutate in mutations:
            with self.subTest(mutate=mutate):
                data = snapshot()
                mutate(data['releases'][0])
                result = self.plan(data)
                self.assertNotIn('engine-' + sha(1), [r['tag'] for r in result['candidates']])

    def test_incomplete_newest_does_not_replace_a_complete_rollback(self):
        data = snapshot()
        data['releases'][-2]['assets'].pop()
        result = self.plan(data)
        self.assertEqual([r['tag'] for r in result['candidates']], ['engine-' + sha(n) for n in (1, 2, 3)])

    def test_future_or_invalid_tag_does_not_displace_a_valid_rollback(self):
        for problem in ('future', 'missing-time', 'wrong-sha', 'annotated', 'missing-tag'):
            with self.subTest(problem=problem):
                data = snapshot()
                if problem == 'future':
                    data['releases'][-1]['published_at'] = '2026-10-29T00:00:00Z'
                if problem == 'missing-time':
                    data['releases'][-1]['published_at'] = None
                if problem == 'wrong-sha':
                    data['tags'][-1]['object']['sha'] = sha(77)
                if problem == 'annotated':
                    data['tags'][-1]['object']['type'] = 'tag'
                if problem == 'missing-tag':
                    data['tags'].pop()
                result = self.plan(data)
                self.assertEqual([r['tag'] for r in result['candidates']],
                                 ['engine-' + sha(n) for n in (1, 2, 3)])

    def test_mismatched_annotated_missing_and_immutable_tags_are_held(self):
        data = snapshot()
        data['tags'][0]['object']['sha'] = sha(99)
        data['tags'][1]['object']['type'] = 'tag'
        data['tags'] = [t for t in data['tags'] if t['ref'] != 'refs/tags/engine-' + sha(3)]
        data['releases'][3]['immutable'] = True
        data['tags'].append({'ref': 'refs/tags/orphan', 'object': {'type': 'commit', 'sha': sha(77)}})
        result = self.plan(data)
        self.assertEqual(result['candidates'], [])
        self.assertEqual(result['tag_only_holds'], ['orphan'])

    def test_unknown_stale_or_missing_deployment_blocks_review_readiness(self):
        for deployment in (None, dict(snapshot()['deployment'], engine=sha(77)),
                           dict(snapshot()['deployment'], observed_at='2026-10-27T18:00:00Z')):
            with self.subTest(deployment=deployment):
                data = snapshot()
                data['deployment'] = deployment
                result = self.plan(data)
                self.assertFalse(result['ready_for_review'])
                self.assertFalse(result['deletion_authorized'])
                self.assertTrue(result['blockers'])

    def test_stale_future_inventory_and_missing_pin_block_review(self):
        for observed in ('2026-10-28T16:59:59Z', '2026-10-28T18:00:01Z'):
            data = snapshot()
            data['observed_at'] = observed
            self.assertFalse(self.plan(data)['ready_for_review'])
        data = snapshot()
        data['pins'].append({'tag': 'missing', 'source': 'main:runtime.json'})
        self.assertFalse(self.plan(data)['ready_for_review'])
        self.assertFalse(self.plan(snapshot(), protected=['missing'])['ready_for_review'])

    def test_bad_policy_duplicate_inventory_and_wrong_repository_fail_closed(self):
        for count in (0, -1, True, 2.5):
            with self.assertRaises(ValueError):
                self.plan(snapshot(), keep=count)
        for field in ('releases', 'tags'):
            data = snapshot()
            data[field].append(deepcopy(data[field][0]))
            with self.assertRaises(ValueError):
                self.plan(data)
        data = snapshot()
        data['releases'][0]['assets'][0]['id'] = data['releases'][1]['assets'][0]['id']
        with self.assertRaises(ValueError):
            self.plan(data)
        data = snapshot()
        data['repository'] = 'someone/else'
        with self.assertRaises(ValueError):
            self.plan(data)

    def test_read_transport_is_get_only_and_consumes_every_page(self):
        completed = subprocess.CompletedProcess([], 0, json.dumps([list(range(100)), [100, 101]]), '')
        with patch.object(retention.subprocess, 'run', return_value=completed) as run:
            self.assertEqual(retention.api('releases?per_page=100', pages=True), list(range(102)))
        command = run.call_args.args[0]
        self.assertEqual(command, ['gh', 'api', '--method', 'GET', '--paginate', '--slurp',
                                   'repos/waldnercharles/chibi/releases?per_page=100'])
        for status in (403, 404):
            failure = subprocess.CompletedProcess([], 1, '', f'gh: error (HTTP {status})')
            with patch.object(retention.subprocess, 'run', return_value=failure):
                if status == 404:
                    self.assertIsNone(retention.api('contents/old-file', optional=True))
                else:
                    with self.assertRaises(ValueError):
                        retention.api('contents/old-file', optional=True)

    def test_collector_captures_pins_and_rechecks_source_heads(self):
        data = snapshot()
        calls = []

        def api(path, pages=False, optional=False, repository=retention.REPOSITORY):
            calls.append((repository, path))
            if path == 'branches/main':
                return {'commit': {'sha': data['main']}}
            if path.startswith('pulls?'):
                return []
            if path.startswith('contents/'):
                if repository == retention.STUDIO_REPOSITORY:
                    return None
                text = json.dumps({'repository': retention.REPOSITORY, 'tag': 'runtime-pin'})
                if 'archives.tsv' in path:
                    text = '# repository waldnercharles/chibi\nlinux-x64\trelease\tarchive-pin\tabc\n'
                return {'encoding': 'base64', 'content': base64.b64encode(text.encode()).decode()}
            if path.startswith('releases?'):
                return data['releases']
            if path == 'git/matching-refs/tags':
                return data['tags']
            self.fail(path)

        with patch.object(retention, 'api', side_effect=api), \
                patch.object(retention.urllib.request, 'urlopen', side_effect=OSError('offline')):
            result = retention.collect()
        self.assertEqual({p['tag'] for p in result['pins']}, {'runtime-pin', 'archive-pin'})
        self.assertIsNone(result['deployment'])
        self.assertTrue(result['blockers'])
        for repository in (retention.REPOSITORY, retention.STUDIO_REPOSITORY):
            self.assertEqual(calls.count((repository, 'branches/main')), 2)
            self.assertEqual(calls.count((repository, 'pulls?state=open&per_page=100')), 2)

    def test_intended_studio_pin_is_held_before_deployment(self):
        data = snapshot()
        data['pins'] = [{'tag': 'engine-' + sha(1), 'source': 'chibi-studio@main:.github/engine-release'}]
        result = self.plan(data)
        self.assertNotIn('engine-' + sha(1), [r['tag'] for r in result['candidates']])
        self.assertTrue(result['ready_for_review'])
        data['releases'][0]['assets'].pop()
        self.assertFalse(self.plan(data)['ready_for_review'])
        del data['studio']
        self.assertFalse(self.plan(data)['ready_for_review'])

    def test_collector_reads_studio_main_and_pr_pins_and_rechecks_them(self):
        studio_main, studio_head = sha(100), sha(101)
        reads = []

        def api(path, pages=False, optional=False, repository=retention.REPOSITORY):
            if path == 'branches/main':
                return {'commit': {'sha': studio_main if repository == retention.STUDIO_REPOSITORY else sha(999)}}
            if path.startswith('pulls?'):
                return ([{'number': 8, 'head': {'sha': studio_head, 'repo': {'full_name': repository}},
                          'base': {'sha': studio_main}}]
                        if repository == retention.STUDIO_REPOSITORY else [])
            if path.startswith('contents/'):
                if repository != retention.STUDIO_REPOSITORY:
                    return None
                reads.append(path)
                pin = sha(1) if 'engine-release' in path else sha(2)
                return {'encoding': 'base64', 'content': base64.b64encode((pin + '\n').encode()).decode()}
            return []

        with patch.object(retention, 'api', side_effect=api), \
                patch.object(retention.urllib.request, 'urlopen', side_effect=OSError('offline')):
            result = retention.collect()
        self.assertEqual({p['tag'] for p in result['pins']}, {'engine-' + sha(1), 'engine-' + sha(2)})
        self.assertEqual({p['tag']: p['release_required'] for p in result['pins']},
                         {'engine-' + sha(1): True, 'engine-' + sha(2): False})
        self.assertEqual(reads, [f'contents/.github/engine-release?ref={studio_main}',
                                 f'contents/.github/engine-release?ref={studio_head}',
                                 f'contents/.github/engine-ref?ref={studio_head}'])

    def test_unreleased_source_pair_does_not_require_a_release(self):
        data = snapshot()
        data['pins'] = [{'tag': 'engine-' + sha(77), 'source': 'Studio PR .github/engine-ref',
                         'release_required': False}]
        self.assertTrue(self.plan(data)['ready_for_review'])
        data['pins'][0]['tag'] = 'engine-' + sha(1)
        self.assertNotIn('engine-' + sha(1), [r['tag'] for r in self.plan(data)['candidates']])

    def test_only_same_repository_studio_prs_allow_engine_ref(self):
        prs = [{'number': n, 'head': {'sha': sha(n), 'repo': repo}, 'base': {'sha': sha(99)}}
               for n, repo in enumerate(({'full_name': retention.STUDIO_REPOSITORY},
                                         {'full_name': 'someone/fork'}, None), 1)]
        with patch.object(retention, 'api', return_value=prs):
            self.assertEqual([p['allow_pin'] for p in retention.pull_heads(retention.STUDIO_REPOSITORY)],
                             [True, False, False])

    def test_snapshot_fingerprint_changes_with_asset_inventory(self):
        data = snapshot()
        before = self.plan(data)['snapshot_sha256']
        data['releases'][0]['assets'][0]['size'] += 1
        self.assertNotEqual(before, self.plan(data)['snapshot_sha256'])

    def test_cli_is_offline_and_has_no_apply_or_delete_mode(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'snapshot.json'
            path.write_text(json.dumps(snapshot()))
            result = subprocess.run([sys.executable, str(ROOT / '.github/release-retention.py'),
                                     'plan', str(path)], text=True, capture_output=True, check=True)
            self.assertFalse(json.loads(result.stdout)['deletion_authorized'])
        result = subprocess.run([sys.executable, str(ROOT / '.github/release-retention.py'), 'delete'],
                                text=True, capture_output=True, check=False)
        self.assertEqual(result.returncode, 2)

    def test_policy_and_ci_wiring(self):
        self.assertEqual(json.loads(retention.POLICY.read_text()),
                         {'keep_engine_snapshots': 10, 'protected_tags': []})
        self.assertIn('python3 .github/test-release-retention.py',
                      (ROOT / '.github/workflows/ci.yml').read_text())


if __name__ == '__main__':
    unittest.main()
