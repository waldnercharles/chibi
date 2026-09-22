"""Frozen resolver benchmark. Proposals only; never changes an editor."""
import argparse
import copy
import hashlib
import importlib.util
import json
import math
import statistics
import time
from pathlib import Path

ROOT = Path(__file__).parent


def load_cases(path):
    cases = json.loads(path.read_text())
    for case in cases:
        if isinstance(case['catalog'], str):
            case['catalog'] = json.loads((path.parent / case['catalog']).read_text())
    return cases


def canonical(ops):
    return sorted((json.dumps(o, sort_keys=True) for o in ops or []))


def matches(expected, actual):
    # Copy labels are local references, not editor identities. Match their use.
    def normalize(ops):
        refs = {}
        result = copy.deepcopy(ops or [])
        for op in result:
            if op.get('result'):
                refs[op['result']] = f'copy:{len(refs)}'
                op['result'] = refs[op['result']]
            if op.get('result_target'):
                op['result_target'] = refs.get(op['result_target'], op['result_target'])
        return result
    def equal(left, right):
        if left.keys() != right.keys():
            return False
        for key, value in left.items():
            other = right[key]
            if key == 'value' and type(value) in (int, float) and type(other) in (int, float) and (type(value) is float or type(other) is float):
                if not math.isclose(value, other, rel_tol=1e-6, abs_tol=1e-6):
                    return False
            elif type(value) is not type(other) or value != other:
                return False
        return True
    if len(expected or []) != len(actual or []):
        return False
    remaining = normalize(actual)
    for op in normalize(expected):
        index = next((i for i, candidate in enumerate(remaining) if equal(op, candidate)), None)
        if index is None:
            return False
        remaining.pop(index)
    return True


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def report(rows):
    supported=[r for r in rows if r['expected'] is not None]
    auto=[r for r in rows if r['result'].get('status')=='ready']
    times=sorted(r['seconds'] for r in rows)
    return {'cases':len(rows),'supported':len(supported),'correct_resolution':sum(r['correct'] for r in supported),
            'resolution_rate':sum(r['correct'] for r in supported)/max(1,len(supported)),
            'automatic_coverage':sum(r['correct'] and r['result'].get('status')=='ready' for r in supported)/max(1,len(supported)),
            'wrong_automatic':sum(not r['correct'] for r in auto),
            'unsupported_with_operations':sum(bool(r['result'].get('operations')) for r in rows if r['expected'] is None),
            'p50_seconds':statistics.median(times) if times else 0,
            'p95_seconds':times[max(0,math.ceil(.95*len(times))-1)] if times else 0}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--include-probes',action='store_true')
    parser.add_argument('--include-local',action='store_true')
    parser.add_argument('--split',choices=['development','calibration','heldout','local','all'],default='all')
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    corpus=ROOT/'evals/requests.jsonl'
    freeze=json.loads((ROOT/'evals/frozen.json').read_text())
    assert hashlib.sha256(corpus.read_bytes()).hexdigest()==freeze['sha256']
    catalog_path=ROOT/'evals/catalog.json'
    assert hashlib.sha256(catalog_path.read_bytes()).hexdigest()==freeze['catalog_sha256']
    catalog=json.loads(catalog_path.read_text())
    module=load(ROOT/'resolve.py', 'benchmark_resolver')
    cases=[json.loads(line) for line in corpus.read_text().splitlines()]
    if args.include_probes:
        probes=ROOT/'evals/direct-probes.jsonl'
        assert hashlib.sha256(probes.read_bytes()).hexdigest()==freeze['probes_sha256']
        cases += [json.loads(line) for line in probes.read_text().splitlines()]
    if args.include_local or args.split == 'local':
        cases += [{**json.loads(line), 'split': 'local'} for line in (ROOT/'evals/local-requests.jsonl').read_text().splitlines()]
    rows=[]
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open('w') as file:
        for case in cases:
            if args.split!='all' and case['split'] not in {args.split,'fresh', *(['local'] if args.include_local else [])}:continue
            packet={'catalog':copy.deepcopy(catalog), **case}
            started=time.monotonic()
            try:
                result=module.resolve(packet)
            except Exception as error:
                result={'status':'failed','message':str(error),'operations':[]}
            elapsed=time.monotonic()-started
            actual=result.get('operations',[])
            correct=not actual if case['expected'] is None else bool(actual) and matches(case['expected'],actual)
            row={**case,'seconds':elapsed,'correct':correct,'result':result}
            rows.append(row);file.write(json.dumps(row)+'\n');file.flush()
            if len(rows)%10==0:print(json.dumps({'completed':len(rows),**report(rows)}),flush=True)
    summary={split:report([r for r in rows if r['split']==split]) for split in ['development','calibration','heldout','fresh','local']}
    summary['all']=report(rows)
    summary['corpus_sha256']=freeze['sha256']
    summary['resolver']='local'
    summary['resolver_sha256']=hashlib.sha256((ROOT/'resolve.py').read_bytes()).hexdigest()
    args.output.with_suffix('.summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(json.dumps(summary,indent=2))


if __name__ == '__main__':
    from environment import activate
    activate()
    main()
