#!/usr/bin/env python3
"""Author one converted local mesh using public MCP buildGraph/validateAsset."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def call(project, controller, tool, **arguments):
    request = {'jsonrpc': '2.0', 'id': 1, 'method': 'tools/call',
               'params': {'name': tool, 'arguments': arguments}}
    run = subprocess.run(['sh', str(ROOT/'tools/mcp/launch.sh')], input=json.dumps(request)+'\n',
                         text=True, capture_output=True, timeout=60,
                         env={**os.environ, 'CHIBI_ROOT': str(project), 'CHIBI_CTL': str(controller)})
    if run.returncode:
        raise RuntimeError(run.stderr)
    response = json.loads(run.stdout)
    if response.get('error') or response.get('result', {}).get('isError'):
        raise RuntimeError(json.dumps(response))
    return response['result']


def author(project, controller, name):
    meta = json.loads((project/'provenance.json').read_text())
    field = meta['grids'][0]
    call(project, controller, 'searchNodes', query='Model Sampled SDF')
    ops = [dict(op='remove_node', node=3), dict(op='add_node', ref='volume', type='Model Sampled SDF')]
    for pin, value in {'Image': field['image'], 'Half Size': dict(zip('xyz', meta['half_size'])),
                       'Resolution': field['resolution'], 'Range': field['range'],
                       'Lipschitz': field['lipschitz']}.items():
        ops.append(dict(op='set_input', node='volume', pin=pin, value=value))
    ops += [dict(op='add_node', ref='part', type='Model Part', reference_name=name),
            {'op': 'connect', 'from': 'volume', 'from_pin': 'Geometry', 'to': 'part', 'to_pin': 'Geometry', 'kind': 'data'},
            {'op': 'connect', 'from': 'part', 'from_pin': 'Geometry', 'to': 'output', 'to_pin': 'Geometry', 'kind': 'data'}]
    call(project, controller, 'buildGraph', kind='model', name=name, create=True, operations=ops)
    result = call(project, controller, 'validateAsset', kind='model', name=name)
    if not result['structuredContent']['result']['valid']:
        raise RuntimeError(json.dumps(result))
    return call(project, controller, 'inspectGraph', kind='model', name=name)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('project', type=Path, help='converted directory with project.kdl initialized')
    p.add_argument('--controller', type=Path, required=True)
    p.add_argument('--name', default='LocalMesh')
    a = p.parse_args()
    print(json.dumps(author(a.project.resolve(), a.controller.resolve(), a.name), indent=2))


if __name__ == '__main__':
    main()
