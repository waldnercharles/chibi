"""Compare ColBERT and lexical shortlists with the same Laya decision policy."""
import argparse
import hashlib
import json
import math
import os
import random
import statistics
import time
from pathlib import Path
from urllib.parse import urlsplit

from evaluate import load_cases, matches
from shadow import candidates, choose, shortlist


class Colbert:
    def __init__(self, model_path):
        import torch
        from sentence_transformers import MultiVectorEncoder
        torch.set_num_threads(4)
        started = time.monotonic()
        self.model = MultiVectorEncoder(str(model_path), device='cpu', local_files_only=True)
        self.documents = {}
        self.model.encode_query(['Warm up'], show_progress_bar=False)
        self.load_seconds = time.monotonic() - started
        self.index_seconds = 0

    def index(self, actions):
        key = tuple(a['description'] for a in actions)
        if key not in self.documents:
            started = time.monotonic()
            self.documents[key] = self.model.encode_document(list(key), show_progress_bar=False)
            self.index_seconds += time.monotonic() - started
        return self.documents[key]

    def rank(self, actions, request):
        embeddings = self.index(actions)
        started = time.monotonic()
        query = self.model.encode_query([request], show_progress_bar=False)
        scores = self.model.similarity(query, embeddings)[0].tolist()
        order = sorted(range(len(actions)), key=lambda i: (-scores[i], i))
        return [actions[i] for i in order], time.monotonic() - started


def summarize(rows):
    chosen = [r for r in rows if r['operations']]
    times = sorted(r['seconds'] for r in rows)
    eligible = sum(r['eligible'] for r in rows)
    return {'cases': len(rows), 'eligible': eligible,
            'retrieval_covered': sum(r['covered'] for r in rows),
            'correct_actions': sum(r['correct'] for r in chosen),
            'wrong_actions': sum(not r['correct'] for r in chosen),
            'correct_deferrals': sum(r['correct'] and not r['operations'] for r in rows),
            'missed_actions': sum(r['eligible'] and not r['operations'] for r in rows),
            'errors': sum('error' in r['answer'] for r in rows),
            'p50_seconds': statistics.median(times), 'p95_seconds': times[math.ceil(.95 * len(times))-1],
            'retrieval_p50_ms': statistics.median(r['retrieval_seconds'] * 1000 for r in rows),
            'thresholds': {str(t): {
                'correct_actions': sum(r['correct'] and r['answer'].get('confidence', 0) >= t for r in chosen),
                'wrong_actions': sum(not r['correct'] and r['answer'].get('confidence', 0) >= t for r in chosen)}
                for t in (.8, .9, .95, .99)}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cases', type=Path, required=True)
    parser.add_argument('--model', type=Path, default=Path('.chibi/local-model/colbert-small'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    url = os.environ.get('LAYA_URL', 'http://127.0.0.1:8768/v1/decide')
    parsed = urlsplit(url)
    if parsed.scheme != 'http' or parsed.hostname not in {'localhost', '127.0.0.1', '::1'} or parsed.username or parsed.password:
        raise ValueError('Use local loopback Laya')
    cases = load_cases(args.cases)
    model = Colbert(args.model)
    for case in cases:
        model.index(candidates(case['catalog']))
    rows = []
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w') as file:
        for index, case in enumerate(cases):
            actions = candidates(case['catalog'])
            eligible = any(matches(case['expected'], c['operations']) for c in actions) if case['expected'] else False
            colbert, colbert_seconds = model.rank(actions, case['command'])
            started = time.monotonic()
            lexical = shortlist(actions, case['command'], len(actions))
            lexical_seconds = time.monotonic() - started
            configs = [(method, k) for method in ('lexical', 'colbert') for k in (1, 2, 3, 5)]
            random.Random(20260921 + index).shuffle(configs)
            for method, k in configs:
                offered = (colbert if method == 'colbert' else lexical)[:k]
                retrieval_seconds = colbert_seconds if method == 'colbert' else lexical_seconds
                covered = any(matches(case['expected'], c['operations']) for c in offered) if eligible else False
                started = time.monotonic()
                try:
                    answer = choose(case['command'], offered, url)
                    ops = [] if answer['choice'] == 'defer' else offered[int(answer['choice'])]['operations']
                except Exception as error:
                    answer, ops = {'choice': 'defer', 'error': str(error)}, []
                row = {'id': case['id'], 'command': case['command'], 'method': method, 'k': k,
                       'eligible': eligible, 'covered': covered,
                       'correct': matches(case['expected'], ops) if ops else not eligible,
                       'operations': ops, 'answer': answer, 'candidates': offered,
                       'retrieval_seconds': retrieval_seconds, 'seconds': time.monotonic() - started + retrieval_seconds}
                rows.append(row)
                file.write(json.dumps(row) + '\n')
                file.flush()
            print(json.dumps({'completed': index + 1, 'of': len(cases)}), flush=True)
    summary = {'configurations': {f'{m}-{k}': summarize([r for r in rows if r['method'] == m and r['k'] == k])
                                  for m in ('lexical', 'colbert') for k in (1, 2, 3, 5)},
               'model': str(args.model), 'model_load_seconds': model.load_seconds, 'index_seconds': model.index_seconds,
               'cases_sha256': hashlib.sha256(json.dumps(cases, sort_keys=True).encode()).hexdigest(),
               'execution_enabled': False}
    args.output.with_suffix('.summary.json').write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
