"""Paired unloaded, resident/fresh-prompt and repeated command measurements."""
import argparse
import hashlib
import json
import random
import statistics
import time
import urllib.request
from pathlib import Path

from benchmark import ROOT
from evaluate import load_cases, matches
from provider import settings
from resolve import resolve


def request(url, path, packet=None):
    data = None if packet is None else json.dumps(packet).encode()
    req = urllib.request.Request(url + path, data, {'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=120) as response:
        return json.load(response)


def run(cases, output, rounds):
    url, model = settings()
    url = url.removesuffix('/v1')
    # Import validation before timing: measure model residency, not Python startup.
    from contract import Proposal
    assert Proposal
    rows = []
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('w') as file:
        for repeat in range(rounds):
            order = list(cases)
            random.Random(20260922 + repeat).shuffle(order)
            for case in order:
                modes = ['unloaded', 'resident_fresh_prompt']
                random.Random(repeat + len(case['id'])).shuffle(modes)
                for mode in modes:
                    request(url, '/api/generate', {'model': model, 'keep_alive': 0})
                    for _ in range(100):
                        if not request(url, '/api/ps')['models']:
                            break
                        time.sleep(.1)
                    else:
                        raise RuntimeError('Model did not unload')
                    preload = None
                    if mode == 'resident_fresh_prompt':
                        preload = request(url, '/api/generate', {'model': model, 'keep_alive': -1, 'stream': False})
                    for condition in ([mode, 'resident_repeat'] if preload else [mode]):
                        result = resolve(case)
                        correct = matches(case['expected'], result.get('operations', []))
                        row = {'id': case['id'], 'repeat': repeat, 'condition': condition,
                               'correct': correct, 'seconds': result['seconds'], 'result': result,
                               'preload': preload, 'resident': request(url, '/api/ps')}
                        rows.append(row)
                        file.write(json.dumps(row) + '\n')
                        file.flush()
                        print(json.dumps({k: row[k] for k in ('id', 'repeat', 'condition', 'correct', 'seconds')}), flush=True)
    summary = {'model': model, 'rounds': rounds,
               'definition': 'Unloaded from Ollama, OS filesystem cache not flushed. Resident fresh uses empty preload after unload; repeat reuses command prefix.',
               'source_hashes': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                                 for name in ('provider.py', 'resolve.py', 'contract.py', 'objects.py')},
               'cases_sha256': hashlib.sha256(json.dumps(cases, sort_keys=True).encode()).hexdigest(),
               'conditions': {}}
    for mode in ('unloaded', 'resident_fresh_prompt', 'resident_repeat'):
        selected = [r for r in rows if r['condition'] == mode]
        summary['conditions'][mode] = {'count': len(selected), 'correct': sum(r['correct'] for r in selected),
                                       'median_seconds': statistics.median(r['seconds'] for r in selected)}
    output.with_suffix('.summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    from environment import activate
    activate()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cases', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--rounds', type=int, default=3)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error('rounds must be positive')
    run(load_cases(args.cases), args.output, args.rounds)
