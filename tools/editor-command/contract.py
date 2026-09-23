"""Ground model output in one observed editor catalog. Never interpret language."""
import math
import re
from objects import tokens
from typing import Literal

from pydantic import BaseModel, ConfigDict, Field


class Operation(BaseModel):
    model_config = ConfigDict(extra='forbid', strict=True)
    op: Literal['set', 'assert', 'duplicate', 'connect', 'open', 'select', 'frame', 'panel', 'runtime', 'save', 'undo', 'redo', 'create_project', 'create_asset', 'edit_graph', 'set_start_scene', 'input', 'capture']
    target: str = Field(default='', description='Object ID; panel ID for panel; scene or preview for runtime')
    field: str = Field(default='', description='Observed field ID for set/assert or source output pin for connect')
    value: str | bool | int | float | None = Field(default=None, description='Final typed value. Colors: #RRGGBB. Panel: boolean visibility. Runtime: play, pause or stop.')
    alpha: int | None = Field(default=None, ge=0, le=255, description='Color opacity byte. Omit to preserve opacity; supply only when explicitly requested.')
    name: str = Field(default='', max_length=100)
    kind: str = ''
    definition: dict = Field(default_factory=dict, description='Prototype components/behaviors, scene entities/physics, or graph semantic operations. Native editor validates before applying.')
    action: str = ''
    pressed: bool = False
    frames: int = Field(default=1, ge=1, le=600)
    path: str = Field(default='', max_length=1024)
    to: str = ''
    input: str = ''
    result: str = ''
    result_target: str = Field(default='', description='For edits to a duplicate, the result name from its duplicate operation. Keep target as the original object ID.')


class Proposal(BaseModel):
    model_config = ConfigDict(extra='forbid')
    decision: Literal['act', 'ask', 'unsupported']
    operations: list[Operation] = Field(default_factory=list, max_length=64)
    question: str = ''
    choices: list[str] = Field(default_factory=list, max_length=12)
    unsupported: str = ''


def typed_value(field, value, alpha=None):
    kind = field['type']
    if alpha is not None and kind != 'color':
        raise ValueError('Only colors have alpha')
    if kind == 'color':
        if not isinstance(value, str) or not re.fullmatch(r'#[0-9a-fA-F]{6}', value):
            raise ValueError('Use six-digit #RRGGBB. Omit alpha to preserve opacity.')
        return (int(value[1:], 16) << 8) | (int(field['value']) & 255 if alpha is None else alpha)
    if kind == 'number':
        if type(value) not in (int, float) or not math.isfinite(value):
            raise ValueError('Expected a finite number')
        if field.get('integer') and int(value) != value:
            raise ValueError('Expected a whole number')
        if not field.get('min', -math.inf) <= value <= field.get('max', math.inf):
            raise ValueError('Value is outside the property range')
        return value
    if kind == 'bool' and type(value) is bool:
        return value
    if kind == 'text' and isinstance(value, str) and len(value) <= 4096:
        return value
    if kind == 'choice' and any(value == c['value'] and type(value) is type(c['value']) for c in field.get('choices', [])):
        return value
    raise ValueError('Value does not match the observed property type')


class Observation:
    def __init__(self, catalog):
        self.catalog = catalog
        self.objects = {f'o{i}': obj for i, obj in enumerate(catalog.get('objects', []))}
        self.aliases = {obj['id']: alias for alias, obj in self.objects.items()}
        self.fields = {alias: {f'f{i}': f for i, f in enumerate(obj.get('fields', []))}
                       for alias, obj in self.objects.items()}
        self.seen = set()

    def summary(self, alias):
        obj = self.objects[alias]
        return {'id': alias, **{k: obj[k] for k in ('name', 'kind', 'path', 'prototype', 'selected', 'components') if k in obj}}

    def inspect(self, aliases, query=''):
        result = []
        for alias in aliases:
            if alias not in self.objects:
                raise ValueError('Unknown object ID')
            fields = []
            available = self.fields[alias]
            terms = tokens(query)
            # Property retrieval uses reflected names, not command-specific grammar.
            matched = {key: field for key, field in available.items()
                       if terms & tokens(field.get('name', ''))}
            chosen = matched if len(available) > 12 and query and matched else available
            for key, field in chosen.items():
                self.seen.add((alias, key))
                row = {'id': key, 'label': field.get('label', field['name']),
                       **{k: field[k] for k in ('type', 'value', 'choices', 'inherited', 'editable', 'integer', 'min', 'max', 'output') if k in field}}
                if field['type'] == 'color':
                    row['value'] = f"#{int(field['value']) >> 8:06x}"
                    row['alpha'] = int(field['value']) & 255
                    if 'inherited' in row:
                        row['inherited_alpha'] = int(row['inherited']) & 255
                        row['inherited'] = f"#{int(row['inherited']) >> 8:06x}"
                if row.get('integer') is False or field['type'] != 'number':
                    row.pop('integer', None)
                if row.get('editable') is True:
                    row.pop('editable', None)
                fields.append(row)
            detail = {**self.summary(alias), 'fields': fields}
            if len(chosen) < len(available):
                detail['more_fields'] = [{'label': field.get('label', field['name']), 'type': field['type']}
                                         for key, field in available.items() if key not in chosen]
            result.append(detail)
        return result

    def operations(self, proposed):
        operations, documents, kinds, writes, results = [], set(), set(), set(), {}
        for item in proposed:
            op = item.model_dump(exclude_defaults=True)
            verb, target = item.op, item.target
            obj = self.objects.get(target)
            if verb not in self.catalog.get('capabilities', []):
                raise ValueError('The editor does not support this operation')
            if verb in {'set_start_scene', 'input', 'capture'}:
                if len(proposed) != 1:
                    raise ValueError('Submit this operation separately')
                if verb == 'set_start_scene':
                    if obj is None or obj['kind'] != 'scene':
                        raise ValueError('Choose an observed scene asset')
                    operations.append({'op': verb, 'target': obj['id']})
                elif verb == 'input':
                    if not item.action or not self.catalog.get('playing'):
                        raise ValueError('Input needs a playing scene and action name')
                    operations.append({'op': verb, 'action': item.action, 'pressed': item.pressed, 'frames': item.frames})
                else:
                    if not item.path:
                        raise ValueError('Capture needs a PNG path')
                    operations.append({'op': verb, 'path': item.path})
                continue
            if verb in {'create_project', 'create_asset', 'edit_graph'}:
                if len(proposed) != 1 or self.catalog.get('playing') or self.catalog.get('preview_playing'):
                    raise ValueError('Run one authoring operation while stopped')
                if verb == 'edit_graph':
                    if obj is None or obj['kind'] not in {'behavior', 'library', 'event', 'material', 'shape', 'field', 'shader-library'}:
                        raise ValueError('Choose an observed graph asset')
                    operations.append({'op': verb, 'target': obj['id'], 'definition': item.definition})
                else:
                    if not re.fullmatch(r'[A-Za-z0-9 _-]{1,100}', item.name) or not item.name.strip():
                        raise ValueError('Use a valid unique name')
                    if verb == 'create_asset' and item.kind not in {'prototype', 'scene', 'behavior', 'library', 'event', 'material', 'shape', 'field', 'shader-library'}:
                        raise ValueError('Choose a supported asset kind')
                    operations.append({'op': verb, 'name': item.name, **({'kind': item.kind, 'definition': item.definition} if verb == 'create_asset' else {})})
                continue
            if verb in {'save', 'undo', 'redo', 'runtime'} and len(proposed) != 1:
                raise ValueError('Run playback, history and save commands separately')
            if verb in {'set', 'assert', 'duplicate', 'connect', 'select', 'frame', 'open'}:
                if obj is None:
                    raise ValueError('Unknown target')
                op['target'] = obj['id']
            if verb in {'set', 'assert', 'duplicate', 'connect'}:
                documents.add(obj['document'])
                kinds.add(obj['kind'])
                if self.catalog.get('playing') or self.catalog.get('preview_playing'):
                    raise ValueError('Stop playback before editing')
            if verb in {'set', 'assert'}:
                field = self.fields[target].get(item.field)
                if (target, item.field) not in self.seen or not field or not field.get('editable', True):
                    raise ValueError('Inspect an editable property before changing it')
                value = typed_value(field, item.value, item.alpha)
                op.pop('alpha', None)
                if verb == 'assert' and value != field['value']:
                    raise ValueError('The requested condition is not satisfied')
                key = (target, item.field, item.result_target)
                if key in writes:
                    raise ValueError('Conflicting property changes')
                writes.add(key)
                op.update(field=field['id'], value=value)
            if item.result_target and (verb not in {'set', 'assert'} or results.get(item.result_target) != target):
                raise ValueError('Unknown duplicate result')
            if verb == 'duplicate':
                if obj['kind'] != 'entity' or (item.result and item.result in results):
                    raise ValueError('Only scene instances can be duplicated with unique result IDs')
                if item.result:
                    results[item.result] = target
            if verb == 'connect':
                other = self.objects.get(item.to)
                source = self.fields[target].get(item.field)
                dest = self.fields.get(item.to, {}).get(item.input)
                if not other or obj['kind'] != 'node' or other['kind'] != 'node' or obj['document'] != other['document']:
                    raise ValueError('Connections need two nodes in the same graph')
                if (target, item.field) not in self.seen or (item.to, item.input) not in self.seen or not source or not dest or not source.get('output') or dest.get('output'):
                    raise ValueError('Connections need observed output and input pins')
                op.pop('to', None)
                op.pop('input', None)
                op.update(field=source['id'], destination=other['id'], pin=dest['id'])
            if verb in {'select', 'frame'} and obj['kind'] != 'entity':
                raise ValueError('Choose scene instances')
            if verb == 'open' and obj['kind'] in {'entity', 'node'}:
                raise ValueError('Choose an asset')
            if verb == 'panel' and (type(item.value) is not bool or not any(p['id'] == target for p in self.catalog.get('panels', []))):
                raise ValueError('Unknown panel or visibility')
            if verb == 'runtime' and (target not in {'scene', 'preview'} or item.value not in {'play', 'pause', 'stop'}):
                raise ValueError('Unknown playback action')
            allowed = {'op'} | ({'target'} if verb not in {'save', 'undo', 'redo'} else set())
            allowed |= {'set': {'field', 'value', 'result_target'}, 'assert': {'field', 'value', 'result_target'},
                        'duplicate': {'result'}, 'connect': {'field', 'destination', 'pin'},
                        'panel': {'value'}, 'runtime': {'value'}}.get(verb, set())
            if set(op) - allowed:
                raise ValueError('Unexpected operation arguments')
            operations.append(op)
        if len(documents) > 1 or len(kinds) > 1:
            raise ValueError('Changes span multiple documents')
        if documents and any(o['op'] not in {'set', 'assert', 'duplicate', 'connect'} for o in operations):
            raise ValueError('Run navigation separately from content changes')
        return operations
