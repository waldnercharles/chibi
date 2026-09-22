"""Build a reproducible synthetic pilot, separate from command regression cases.

Ground truth is constructed from known operations and checked against the same
observation contract as the editor. It is not a substitute for human-reviewed
real requests. Splits have disjoint names and seeds; evaluation uses different
request phrasings from training.
"""
import argparse
import hashlib
import json
import random
from pathlib import Path

from contract import Observation, Operation, Proposal
from provider import output_schema
from resolve import resolve


def example(split, index):
    rng = random.Random({'train': 19000, 'valid': 29000, 'test': 39000}[split] + index)
    prefix = {'train': 'Copper', 'valid': 'Cobalt', 'test': 'Silver'}[split]
    name = f'{prefix}{index}Emitter'
    entity = f'{prefix}{index}Marker'
    document = f'scene:{split}:{index}'
    width, rate, x = rng.randint(10, 80), rng.randint(2, 50), rng.randint(-40, 40)
    value = rng.randint(81, 140)
    fields = [dict(id='width', name='width', label='Appearance Width', type='number', value=width, inherited=12),
              dict(id='rate', name='rate', label='Emitter Rate', type='number', value=rate, min=0),
              dict(id='color', name='color', label='Particle Color', type='color', value=0x12345644),
              dict(id='end_color', name='end_color', label='Particle End Color', type='color', value=0x65432188),
              dict(id='enabled', name='enabled', label='Enabled', type='bool', value=True),
              dict(id='label', name='label', label='Label', type='text', value='Idle')]
    objects = [dict(id='prototype:p', name=name, kind='prototype', document='prototype:p', fields=fields),
               dict(id='entity:a', name=entity + ' A', kind='entity', document=document, selected=True,
                    fields=[dict(id='x', name='x', label='Transform X', type='number', value=x)]),
               dict(id='entity:b', name=entity + ' B', kind='entity', document=document, fields=[]),
               dict(id='behavior:b', name=prefix + 'Flow', kind='behavior', document='behavior:b', fields=[]),
               dict(id='node:a', name=prefix + 'Clock', kind='node', document='behavior:b',
                    fields=[dict(id='out', name='tick', label='Tick', type='flow', output=True, editable=False)]),
               dict(id='node:b', name=prefix + 'Counter', kind='node', document='behavior:b',
                    fields=[dict(id='in', name='increment', label='Increment', type='flow', output=False, editable=False)])]
    for obj in objects:
        rng.shuffle(obj['fields'])
    rng.shuffle(objects)
    catalog = dict(revision=index + 1, document=document, objects=objects,
                   capabilities=list(Operation.model_fields['op'].annotation.__args__),
                   panels=[dict(id='panel:details', name='Details', visible=True)], playing=False, preview_playing=False)
    obs = Observation(catalog)
    aliases = obs.aliases
    def op(verb, target='', field='', **kw):
        result = {'op': verb, **kw}
        if target:
            result['target'] = aliases.get(target, target)
        if field:
            result['field'] = next(key for key, item in obs.fields[aliases[target]].items() if item['id'] == field)
        return result
    family = index % 16
    alternate = split != 'train'
    context = {}
    decision, extra = 'act', {}
    if family == 0:
        command = f'Set {name} rate to {value}' if not alternate else f'{name} should have rate {value}.'
        ops = [op('set', 'prototype:p', 'rate', value=value)]
    elif family == 1:
        command = f'Double the width of {name}' if not alternate else f'Make {name} width 200 percent of the current width.'
        ops = [op('set', 'prototype:p', 'width', value=width * 2)]
    elif family == 2:
        command = f'Set {name} particle color to green and end color to purple' if not alternate else f'For {name}, use green as particle color and purple as end color.'
        ops = [op('set', 'prototype:p', 'color', value='#00ff00'), op('set', 'prototype:p', 'end_color', value='#8000ff')]
    elif family == 3:
        command = f'Set {name} enabled to false' if not alternate else f'Turn off the enabled property on {name}.'
        ops = [op('set', 'prototype:p', 'enabled', value=False)]
    elif family == 4:
        text = f'{prefix} ready {value}'
        command = f'Set {name} label to "{text}"' if not alternate else f'Use "{text}" for the label of {name}.'
        ops = [op('set', 'prototype:p', 'label', value=text)]
    elif family == 5:
        command = f'Duplicate {entity} A and move the copy {value} units right' if not alternate else f'Make a copy of {entity} A, then increase only the copy\'s X by {value}.'
        ops = [op('duplicate', 'entity:a', result='copy'), op('set', 'entity:a', 'x', value=x + value, result_target='copy')]
    elif family == 6:
        command = f'Connect {prefix}Clock Tick to {prefix}Counter Increment' if not alternate else f'Wire the Tick output of {prefix}Clock into the Increment input of {prefix}Counter.'
        destination = next(iter(obs.fields[aliases['node:b']]))
        ops = [op('connect', 'node:a', 'out', to=aliases['node:b'], input=destination)]
    elif family == 7:
        command = f'Open {prefix}Flow behavior' if not alternate else f'Show the behavior named {prefix}Flow.'
        ops = [op('open', 'behavior:b')]
    elif family == 8:
        command = f'Frame {entity} B' if not alternate else f'Focus the viewport on {entity} B.'
        ops = [op('frame', 'entity:b')]
    elif family == 9:
        command = 'Hide the Details panel' if not alternate else 'Close the Details panel.'
        ops = [op('panel', 'panel:details', value=False)]
    elif family == 10:
        command = 'Play the scene' if not alternate else 'Start scene playback.'
        ops = [op('runtime', 'scene', value='play')]
    elif family == 11:
        command = 'Save the document' if not alternate else 'Save my changes.'
        ops = [op('save')]
    elif family == 12:
        command = f'Set its rate to {value}' if not alternate else f'Change its rate to {value} now.'
        context = dict(document=document, targets=['prototype:p'])
        ops = [op('set', 'prototype:p', 'rate', value=value)]
    elif family == 13:
        command = f'Set {name} rate to {value} and delete {entity} A' if not alternate else f'Remove {entity} A and give {name} rate {value}.'
        decision, ops = 'unsupported', []
        extra = {'unsupported': 'Deleting entities is unavailable.'}
    elif family == 14:
        # Both identities deliberately have the same name; neither is selected.
        for obj in objects:
            if obj['kind'] == 'entity':
                obj.update(name=entity, selected=False)
        command = f'Frame {entity}' if not alternate else f'Focus the viewport on {entity}.'
        decision, ops = 'ask', []
        extra = {'question': 'Which instance?', 'choices': [aliases['entity:a'], aliases['entity:b']]}
    else:
        command = f'Restore {name} width to its inherited value' if not alternate else f'Reset the width override on {name} to the inherited width.'
        ops = [op('set', 'prototype:p', 'width', value=12)]
    proposal = Proposal.model_validate(dict(decision=decision, operations=ops, **extra))
    obs.inspect(list(obs.objects))
    expected = obs.operations(proposal.operations) if decision == 'act' else None
    packet = dict(id=f'{split}-{index:03}', family=family, catalog=catalog, command=command, context=context,
                  expected=expected, expected_status={'act': 'needs_review', 'ask': 'needs_clarification', 'unsupported': 'unsupported'}[decision])
    if decision == 'ask':
        packet['expected_choices'] = ['entity:a', 'entity:b']
    messages = []
    def capture(instructions, prompt, schema, inspect):
        messages.extend([dict(role='system', content=instructions + '\nReturn JSON matching this schema:\n' + output_schema(schema)),
                         dict(role='user', content=prompt),
                         dict(role='assistant', content=json.dumps(proposal.model_dump(exclude_defaults=True), separators=(',', ':')))])
        return proposal, {'model': 'constructed-ground-truth'}
    result = resolve(packet, capture)
    assert result['status'] == packet['expected_status'], (packet['id'], result)
    assert result['operations'] == (expected or []), packet['id']
    return packet, {'messages': messages}


def build(output):
    output.mkdir(parents=True, exist_ok=True)
    manifest = {'kind': 'synthetic-contract-validated-pilot', 'splits': {},
                'limitations': 'Shared operation families; disjoint names, seeds and train/evaluation phrasings. Not real-user accuracy.'}
    for split, count in [('train', 128), ('valid', 32), ('test', 48)]:
        pairs = [example(split, i) for i in range(count)]
        data = output / f'{split}.jsonl'
        data.write_text(''.join(json.dumps(messages) + '\n' for _, messages in pairs))
        cases = output / f'{split}-cases.json'
        cases.write_text(json.dumps([packet for packet, _ in pairs], indent=2) + '\n')
        manifest['splits'][split] = {'count': count, 'data_sha256': hashlib.sha256(data.read_bytes()).hexdigest(),
                                    'cases_sha256': hashlib.sha256(cases.read_bytes()).hexdigest()}
    manifest['generator_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    (output / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps(manifest, indent=2))


if __name__ == '__main__':
    from environment import activate
    activate()
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    build(parser.parse_args().output)
