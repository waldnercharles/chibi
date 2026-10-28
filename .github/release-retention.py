#!/usr/bin/env python3
"""Read-only Chibi release inventory and cleanup proposal. Never deletes anything."""
import argparse
import base64
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import urllib.request

REPOSITORY = 'waldnercharles/chibi'
STUDIO_MANIFEST = 'https://waldnercharles.github.io/chibi-studio/studio.json'
ENGINE = re.compile(r'engine-([0-9a-f]{40})\Z')
SHA = re.compile(r'[0-9a-f]{40}\Z')
ASSETS = {'build.json', 'SHA256SUMS', 'chibi-export-linux-x64.tar.gz',
          'chibi-sdk-linux-x64.tar.gz', 'chibi-web-sdk.tar.gz'}
POLICY = Path(__file__).with_name('release-retention.json')


def timestamp(value):
    result = datetime.fromisoformat(value.replace('Z', '+00:00'))
    if result.tzinfo is None:
        raise ValueError('timestamp must include a timezone')
    return result


def utc_now():
    return datetime.now(timezone.utc).isoformat().replace('+00:00', 'Z')


def api(path, pages=False, optional=False):
    command = ['gh', 'api', '--method', 'GET']
    if pages:
        command += ['--paginate', '--slurp']
    result = subprocess.run(command + [f'repos/{REPOSITORY}/{path}'],
                            text=True, capture_output=True, check=False, timeout=120)
    if result.returncode:
        if optional and '(HTTP 404)' in result.stderr:
            return None
        raise ValueError(f'GitHub GET failed: {path}')
    value = json.loads(result.stdout)
    if not pages:
        return value
    if not isinstance(value, list) or any(not isinstance(page, list) for page in value):
        raise ValueError(f'expected paginated arrays: {path}')
    return [item for page in value for item in page]


def pull_heads():
    return sorted([{'number': p['number'], 'head': p['head']['sha'], 'base': p['base']['sha']}
                   for p in api('pulls?state=open&per_page=100', pages=True)],
                  key=lambda p: p['number'])


def collect():
    main = api('branches/main')['commit']['sha']
    prs = pull_heads()
    pins, absent = [], []
    for revision in sorted({main, *(p['head'] for p in prs)}):
        if not SHA.fullmatch(revision):
            raise ValueError('invalid source revision')
        for path in ('tools/compiler/runtime.json', 'third_party/archives.tsv'):
            data = api(f'contents/{path}?ref={revision}', optional=True)
            source = f'{revision}:{path}'
            if data is None:
                absent.append(source)
                continue
            if data['encoding'] != 'base64':
                raise ValueError(f'unexpected contents encoding: {source}')
            text = base64.b64decode(data['content']).decode('utf-8')
            if path.endswith('.json'):
                manifest = json.loads(text)
                if manifest['repository'] != REPOSITORY:
                    raise ValueError(f'unexpected runtime repository: {source}')
                tags = {manifest['tag']}
            else:
                tags = {line.split('\t')[2] for line in text.splitlines()
                        if line and not line.startswith('#')}
            pins += [{'tag': tag, 'source': source} for tag in sorted(tags)]
    result = {'format': 1, 'repository': REPOSITORY, 'main': main, 'prs': prs,
              'pins': pins, 'absent_pin_files': absent, 'blockers': [],
              'releases': api('releases?per_page=100', pages=True),
              'tags': api('git/matching-refs/tags', pages=True)}
    try:
        with urllib.request.urlopen(STUDIO_MANIFEST, timeout=30) as response:
            manifest = json.load(response)
        if not SHA.fullmatch(manifest['engine']):
            raise ValueError('invalid deployed engine')
        result['deployment'] = {'url': STUDIO_MANIFEST, 'engine': manifest['engine'],
                                'studio': manifest['studio'], 'observed_at': utc_now()}
    except (OSError, ValueError, KeyError):
        result['deployment'] = None
        result['blockers'].append('Cannot verify the live Studio manifest; do not delete candidates.')
    if main != api('branches/main')['commit']['sha'] or prs != pull_heads():
        result['blockers'].append('Main or open PR heads changed during collection; collect again.')
    result['observed_at'] = utc_now()
    return result


def unique(items, key, label):
    values = [item[key] for item in items]
    if len(set(values)) != len(values):
        raise ValueError(f'duplicate {label}')


def complete(release):
    assets = release['assets']
    return (not release['draft'] and len(assets) == len(ASSETS)
            and {a['name'] for a in assets} == ASSETS
            and all(a['size'] > 0 and a['state'] == 'uploaded' for a in assets))


def plan(snapshot, policy, now):
    if snapshot['format'] != 1 or snapshot['repository'] != REPOSITORY:
        raise ValueError('unsupported snapshot format or repository')
    count = policy['keep_engine_snapshots']
    if type(count) is not int or count < 1:
        raise ValueError('keep_engine_snapshots must be a positive integer')
    releases, tags = snapshot['releases'], snapshot['tags']
    unique(releases, 'id', 'release ID')
    unique(releases, 'tag_name', 'release tag')
    unique(tags, 'ref', 'Git tag')
    unique([a for r in releases for a in r['assets']], 'id', 'asset ID')
    by_tag = {r['tag_name']: r for r in releases}
    refs = {t['ref'].removeprefix('refs/tags/'): t['object'] for t in tags}
    blockers = list(snapshot['blockers'])
    age = (now - timestamp(snapshot['observed_at'])).total_seconds()
    if not 0 <= age <= 3600:
        blockers.append('Inventory is future-dated or older than one hour; collect again.')
    protected = {}

    def protect(tag, reason):
        protected.setdefault(tag, []).append(reason)

    protect('engine-' + snapshot['main'], 'current main')
    for pr in snapshot['prs']:
        for side in ('head', 'base'):
            protect('engine-' + pr[side], f'open PR #{pr["number"]} {side}')
    for pin in snapshot['pins']:
        protect(pin['tag'], 'dependency pin ' + pin['source'])
        if pin['tag'] not in by_tag:
            blockers.append('Pinned release is missing: ' + pin['tag'])
    for tag in policy['protected_tags']:
        if not isinstance(tag, str) or not tag:
            raise ValueError('protected_tags must contain nonempty tag names')
        protect(tag, 'explicit rollback or supported checkpoint')
        if tag not in by_tag:
            blockers.append('Explicitly protected release is missing: ' + tag)
    deployment = snapshot['deployment']
    if deployment is None:
        blockers.append('Live Studio engine is unknown; do not delete candidates.')
    else:
        if deployment['url'] != STUDIO_MANIFEST or not SHA.fullmatch(deployment['engine']):
            raise ValueError('invalid Studio deployment identity')
        age = (now - timestamp(deployment['observed_at'])).total_seconds()
        if not 0 <= age <= 3600:
            blockers.append('Live Studio observation is future-dated or older than one hour.')
        tag = 'engine-' + deployment['engine']
        protect(tag, 'live Studio deployment and matching game export tools')
        if tag not in by_tag or not complete(by_tag[tag]):
            blockers.append('Live Studio release is missing or incomplete: ' + tag)

    # A held malformed/newer release must not displace a usable rollback.
    engines = [r for r in releases
               if (match := ENGINE.fullmatch(r['tag_name'])) and complete(r)
               and r['published_at'] is not None and timestamp(r['published_at']) <= now
               and refs.get(r['tag_name'], {}).get('type') == 'commit'
               and refs[r['tag_name']]['sha'] == match[1]]
    for r in sorted(engines, key=lambda r: (timestamp(r['published_at']), r['id']), reverse=True)[:count]:
        protect(r['tag_name'], f'newest {count} complete engine snapshots')
    candidates, retained = [], []
    for r in sorted(releases, key=lambda r: r['tag_name']):
        tag = r['tag_name']
        reasons = list(protected.get(tag, []))
        match = ENGINE.fullmatch(tag)
        if not match:
            reasons.append('non-engine release; dependency and stable pruning is out of scope')
        if not r['prerelease']:
            reasons.append('stable release')
        if r['draft'] or not complete(r):
            reasons.append('draft or nonstandard/incomplete asset inventory; manual review')
        if r['immutable']:
            reasons.append('immutable release; manual review')
        if tag not in refs or refs[tag]['type'] != 'commit' or (match and refs[tag]['sha'] != match[1]):
            reasons.append('missing, annotated or mismatched tag; manual review')
        if r['published_at'] is None or timestamp(r['published_at']) > now:
            reasons.append('missing or future publication time; manual review')
        if reasons:
            retained.append({'tag': tag, 'reasons': reasons})
            continue
        candidates.append({'release_id': r['id'], 'tag': tag, 'tag_sha': refs[tag]['sha'],
                           'release_url': r['html_url'], 'published_at': r['published_at'],
                           'assets': [{key: a[key] for key in ('id', 'name', 'size', 'digest',
                                      'browser_download_url', 'updated_at')} for a in r['assets']]})
    encoded = json.dumps(snapshot, sort_keys=True, separators=(',', ':')).encode()
    return {'format': 1, 'repository': REPOSITORY, 'deletion_authorized': False,
            'ready_for_review': not blockers, 'blockers': sorted(set(blockers)),
            'snapshot_sha256': hashlib.sha256(encoded).hexdigest(),
            'observed_at': snapshot['observed_at'], 'policy': policy,
            'release_count': len(releases), 'tag_count': len(tags),
            'candidate_count': len(candidates),
            'candidate_bytes': sum(a['size'] for r in candidates for a in r['assets']),
            'candidates': candidates, 'retained': retained,
            'tag_only_holds': sorted(set(refs) - set(by_tag)),
            'warning': 'Proposal only. Recollect and recheck consumers before explicit deletion approval. '
                       'Tags and Git source do not preserve uploaded binaries. Historical and external '
                       'consumers are not exhaustively proven unused.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    commands.add_parser('collect', help='GET-only inventory; requires authenticated gh and public HTTPS')
    p = commands.add_parser('plan', help='offline proposal; never deletes or generates deletion commands')
    p.add_argument('snapshot', type=Path)
    p.add_argument('--policy', type=Path, default=POLICY)
    args = parser.parse_args()
    try:
        result = collect() if args.command == 'collect' else plan(
            json.loads(args.snapshot.read_text()), json.loads(args.policy.read_text()),
            datetime.now(timezone.utc))
        print(json.dumps(result, indent=2))
    except (OSError, ValueError, KeyError, TypeError, IndexError, subprocess.TimeoutExpired) as error:
        parser.exit(2, f'retention inventory failed: {error}\n')


if __name__ == '__main__':
    main()
