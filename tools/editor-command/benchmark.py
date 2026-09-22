"""Matched, warmed command benchmarks. Produces proposals, never editor edits."""
import argparse
import hashlib
import json
import os
import random
import time
from pathlib import Path

from evaluate import load_cases, matches, report
from resolve import resolve

ROOT = Path(__file__).parent


def run(cases, output, label, rounds):
    # Warm the exact request path; keep the load time separate from request latency.
    started = time.monotonic()
    warmup = resolve(cases[0])
    metadata = {'label': label, 'rounds': rounds, 'warmup_seconds': time.monotonic() - started,
                'warmup_status': warmup['status'],
                'settings': {k: v for k, v in os.environ.items() if k.startswith(('CHIBI_MODEL', 'CHIBI_REASONING', 'OLLAMA_')) and 'KEY' not in k},
                'source_hashes': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest()
                                  for name in ('provider.py', 'resolve.py', 'contract.py', 'objects.py')},
                'cases_sha256': hashlib.sha256(json.dumps(cases, sort_keys=True).encode()).hexdigest()}
    rows = []
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('w') as file:
        for repeat in range(rounds):
            order = list(cases)
            random.Random(20260921 + repeat).shuffle(order)
            for case in order:
                result = resolve(case)
                actual = result.get('operations', [])
                correct = not actual and result['status'] in {'unsupported', 'needs_clarification'} if case['expected'] is None else matches(case['expected'], actual)
                if case.get('expected_status'):
                    correct = correct and result['status'] == case['expected_status']
                if 'expected_choices' in case:
                    correct = correct and set(case['expected_choices']) == {choice['id'] for choice in result.get('choices', [])}
                row = {k: v for k, v in case.items() if k != 'catalog'}
                row.update(repeat=repeat, seconds=result['seconds'], correct=correct, result=result)
                rows.append(row)
                file.write(json.dumps(row) + '\n')
                file.flush()
                print(json.dumps({'label': label, 'id': case['id'], 'repeat': repeat, 'correct': correct,
                                  'seconds': round(result['seconds'], 3), 'status': result['status']}), flush=True)
    summary = {**metadata, **report(rows), 'correct_all': sum(r['correct'] for r in rows),
               'failures': [{'id': r['id'], 'repeat': r['repeat'], 'result': r['result']} for r in rows if not r['correct']]}
    output.with_suffix('.summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({k: v for k, v in summary.items() if k != 'failures'}, indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cases', type=Path, required=True, help='JSON array of packets with catalog and expected operations')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--label', required=True)
    parser.add_argument('--rounds', type=int, default=2)
    args = parser.parse_args()
    if args.rounds < 1:
        parser.error('rounds must be positive')
    run(load_cases(args.cases), args.output, args.label, args.rounds)


if __name__ == '__main__':
    from environment import activate
    activate()
    main()
