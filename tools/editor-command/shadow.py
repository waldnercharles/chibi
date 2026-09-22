"""Offline Laya fast-action experiment. Never routes or executes editor requests."""
import argparse
import collections
import hashlib
import json
import math
import os
import statistics
import time
import urllib.request
from pathlib import Path
from urllib.parse import urlsplit

from contract import Observation, Operation
from evaluate import load_cases, matches
from objects import words

ROOT = Path(__file__).resolve().parents[2]


def candidates(catalog):
    """Enumerate complete, finite actions from observed capabilities and identities."""
    observation = Observation(catalog)
    result = []

    def add(description, **op):
        if op['op'] not in catalog['capabilities']:
            return
        operations = observation.operations([Operation(**op)])
        result.append({'description': description, 'operations': operations})

    for alias, obj in observation.objects.items():
        name = f"{obj['name']} {obj['kind']}"
        if obj['kind'] == 'entity':
            add(f'Select {name}', op='select', target=alias)
            add(f'Frame {name} in the viewport', op='frame', target=alias)
        elif obj['kind'] != 'node':
            add(f"Open {name} {Path(obj.get('path', '')).stem}", op='open', target=alias)
    for panel in catalog.get('panels', []):
        for visible, verb in [(True, 'Show'), (False, 'Hide')]:
            add(f"{verb} the {panel['name']} panel", op='panel', target=panel['id'], value=visible)
    for target in ('scene', 'preview'):
        for state in ('play', 'pause', 'stop'):
            add(f'{state.title()} the {target}', op='runtime', target=target, value=state)
    for op, description in [('save', 'Save the current document'), ('undo', 'Undo the last edit'), ('redo', 'Redo the last undone edit')]:
        add(description, op=op)
    return result


def shortlist(actions, request, limit=5):
    # Generic lexical retrieval of descriptions; no intent rules or value parser.
    documents = [set(words(a['description'])) for a in actions]
    frequencies = collections.Counter(t for doc in documents for t in doc)
    query = set(words(request))
    def score(index):
        doc = documents[index]
        return sum(math.log(1 + len(documents) / frequencies[t]) for t in doc & query) / math.sqrt(max(1, len(doc)))
    ranked = sorted(range(len(actions)), key=lambda i: (-score(i), i))
    return [actions[i] for i in ranked[:limit] if score(i) > 0]


def choose(request, choices, url):
    if len(request) > 400 or not choices or any(len(c['description']) > 160 for c in choices):
        return {'choice': 'defer', 'reason': 'outside bounded menu budget', 'seconds': 0}
    criteria = {str(i): c['description'] for i, c in enumerate(choices)}
    criteria['defer'] = 'Defer: none of these actions alone completes the entire request, or the target is ambiguous'
    packet = {'model': 'laya', 'state': {'request': request}, 'questions': {'action': {
        'type': 'choice', 'instructions': 'Choose one action only if it fully satisfies the entire request. Otherwise defer. Never do just part.',
        'criteria': criteria}}}
    key = os.environ.get('LAYA_API_KEY', '')
    key_path = Path(os.environ.get('LAYA_API_KEY_FILE', ROOT / '.chibi/laya-api-key'))
    if not key and key_path.is_file():
        key = key_path.read_text().strip()
    headers = {'Content-Type': 'application/json'}
    if key:
        headers['Authorization'] = 'Bearer ' + key
    started = time.monotonic()
    wire = urllib.request.Request(url, json.dumps(packet).encode(), headers)
    with urllib.request.urlopen(wire, timeout=5) as response:
        data = json.load(response)
    answer = data['answers']['action']
    probabilities = answer.get('probabilities', {})
    if set(probabilities) != set(criteria) or answer.get('choice') not in criteria:
        raise ValueError('Incomplete Laya choice distribution')
    if any(type(p) not in (float, int) or not math.isfinite(p) or not 0 <= p <= 1 for p in probabilities.values()):
        raise ValueError('Invalid Laya probabilities')
    if abs(sum(probabilities.values()) - 1) > .02 or probabilities[answer['choice']] + 1e-6 < max(probabilities.values()):
        raise ValueError('Inconsistent Laya choice')
    confidence = answer.get('confidence')
    if type(confidence) not in (float, int) or not math.isfinite(confidence) or not 0 <= confidence <= 1:
        raise ValueError('Invalid Laya confidence')
    return {**answer, 'seconds': time.monotonic() - started, 'usage': data.get('usage', {})}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cases', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    url = os.environ.get('LAYA_URL', 'http://127.0.0.1:8768/v1/decide')
    parsed = urlsplit(url)
    if parsed.scheme != 'http' or parsed.hostname not in {'localhost', '127.0.0.1', '::1'} or parsed.username or parsed.password:
        raise ValueError('Shadow evaluation requires local loopback Laya')
    cases = load_cases(args.cases)
    rows = []
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w') as file:
        for case in cases:
            actions = candidates(case['catalog'])
            offered = shortlist(actions, case['command'])
            eligible = any(matches(case['expected'], c['operations']) for c in actions) if case['expected'] else False
            covered = any(matches(case['expected'], c['operations']) for c in offered) if eligible else False
            try:
                answer = choose(case['command'], offered, url)
                ops = [] if answer['choice'] == 'defer' else offered[int(answer['choice'])]['operations']
            except Exception as error:
                answer, ops = {'choice': 'defer', 'error': str(error)}, []
            row = {'id': case['id'], 'command': case['command'], 'eligible': eligible, 'covered': covered,
                   'correct': matches(case['expected'], ops) if ops else not eligible, 'operations': ops,
                   'answer': answer, 'candidates': offered}
            rows.append(row)
            file.write(json.dumps(row) + '\n')
            file.flush()
    chosen = [r for r in rows if r['operations']]
    times = sorted(r['answer']['seconds'] for r in rows if r['answer'].get('seconds'))
    summary = {'cases': len(rows), 'eligible': sum(r['eligible'] for r in rows), 'retrieval_covered': sum(r['covered'] for r in rows),
               'proposed': len(chosen), 'correct_proposals': sum(r['correct'] for r in chosen),
               'wrong_proposals': sum(not r['correct'] for r in chosen),
               'high_confidence_wrong': sum(not r['correct'] and r['answer'].get('confidence', 0) >= .95 for r in chosen),
               'errors': sum('error' in r['answer'] for r in rows),
               'p50_seconds': statistics.median(times) if times else None,
               'p95_seconds': times[math.ceil(.95 * len(times))-1] if times else None,
               'cases_sha256': hashlib.sha256(json.dumps(cases, sort_keys=True).encode()).hexdigest(), 'execution_enabled': False}
    args.output.with_suffix('.summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
