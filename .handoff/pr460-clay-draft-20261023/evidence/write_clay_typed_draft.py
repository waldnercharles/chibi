"""Write a hand-mapped source draft; this script does not parse old assets."""
from dataclasses import dataclass
import json
from pathlib import Path

ROOT = Path('/tmp/chibi-integration-20261022/pr460')
DRAFT = ROOT / 'clay-typed-draft'
ORIGINAL = ROOT / 'clay-source-aa7/test/fixtures/clay-lab'


@dataclass(frozen=True)
class Scalar:
    kind: str
    text: str


def u(number):
    assert isinstance(number, int) and number >= 0
    return Scalar('UInt', str(number))


def i(number):
    assert isinstance(number, int)
    return Scalar('Int', str(number))


def r(number):
    # Explicit authored real spelling, including integral real values.
    text = str(number)
    if '.' not in text:
        text += '.0'
    return Scalar('Real', text)


def render(value, level):
    pad = ' ' * level
    if isinstance(value, Scalar):
        return f'data.Value.{value.kind}({value.text})'
    if isinstance(value, bool):
        return f'data.Value.Bool({str(value).lower()})'
    if isinstance(value, str):
        return f'data.Value.Text({json.dumps(value)})'
    if isinstance(value, list):
        if not value:
            return 'data.List([])'
        entries = [(' ' * (level + 4)) + render(item, level + 4) for item in value]
        return 'data.List([\n' + ',\n'.join(entries) + '\n' + pad + '])'
    assert isinstance(value, dict), value
    if not value:
        return 'data.Object([])'
    fields = []
    for name, item in value.items():
        fields.append((' ' * (level + 4)) + 'data.Field.Named(' +
                      json.dumps(name) + ', ' + render(item, level + 4) + ')')
    return 'data.Object([\n' + ',\n'.join(fields) + '\n' + pad + '])'


def source(value):
    return ('package asset\n\nimport data "game:assets"\n\n'
            'pub Build :: () -> byte[] {\n    return data.Encode(\n        ' +
            render(value, 8) + '\n    );\n}\n')


def write(path, value):
    target = DRAFT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(source(value))


def vector(x, y, z):
    return {'x': r(x), 'y': r(y), 'z': r(z)}


def node(number, kind, x, y, fields=None):
    result = {'id': u(number), 'type': kind, 'x': r(x), 'y': r(y)}
    result.update(fields or {})
    return result


def edges(pairs):
    return [{'from': left, 'to': right} for left, right in pairs]


def graph(name, counter, description, nodes, connections, functions=None, domain='model'):
    return {'version': u(3), 'name': name, 'domain': domain,
            'id_counter': u(counter), 'description': description,
            'graphs': [{'kind': 'event', 'name': '', 'nodes': nodes,
                        'edges': edges(connections)}] + (functions or [])}


def expression_graph(number, formula, entry):
    return {'id': u(number), 'kind': 'function', 'math_expression': formula,
            'expression_mode': u(1), 'entry_id': u(entry)}


write('project.chibi', {'version': i(33), 'name': 'Clay Material Lab',
                       'content': 'content', 'start_scene': 'scenes/MaterialLab.chibi'})
write('content/input.chibi', {
    'Form': {'sources': ['F']},
    'Fingerprints': {'sources': ['G']},
    'Variation': {'sources': ['V']},
    'Oiliness': {'sources': ['O']},
    'Reset': {'sources': ['R']},
})

write('content/models/ClaySphere.chibi', graph(
    'ClaySphere', 1000, 'Radius-18 calibration sphere in authored units.',
    [node(100, 'Model Sphere', 0, 0, {'inputs': {'Radius': r(18)}}),
     node(200, 'Model Output', 300, 0)],
    [('100.Geometry', '200.Geometry')]))

write('content/models/ClayCapsule.chibi', graph(
    'ClayCapsule', 1000,
    'Y-axis capsule; radius 12 and half segment 12 authored units.',
    [node(100, 'Model Capsule', 0, 0,
          {'inputs': {'Radius': r(12), 'Half Segment': r(12)}}),
     node(200, 'Model Output', 300, 0)],
    [('100.Geometry', '200.Geometry')]))

write('content/models/ClayBlock.chibi', graph(
    'ClayBlock', 1000,
    'Rounded calibration block; half size (18,16,16), outer radius 4.',
    [node(100, 'Model Rounded Box', 0, 0,
          {'inputs': {'Half Size': {'default': vector(18, 16, 16)}, 'Radius': r(4)}}),
     node(200, 'Model Output', 300, 0)],
    [('100.Geometry', '200.Geometry')]))

lobe = 'vec3(14 * form, 3 * form, 2 * form)'
cut = 'vec3(-7, 38 - 15 * form, 6)'
write('content/models/ClaySculpt.chibi', graph(
    'ClaySculpt', 2000,
    'Bounded primitive sculpt: a softened lobe and thumb cut. Form 0 recovers ClayBlock; Form 1 shows the worked silhouette.',
    [node(100, 'Model Rounded Box', 0, 0,
          {'inputs': {'Half Size': {'default': vector(18, 16, 16)}, 'Radius': r(4)}}),
     node(200, 'Model Parameter', 0, 260,
          {'reference_name': 'Form', 'inputs': {'Minimum': r(0), 'Maximum': r(1)}}),
     node(300, 'Behavior Expression', 280, 230,
          {'reference_name': lobe,
           'inputs': {'form': {'id': u(301), 'type': 'float'}},
           'outputs': {'Result': {'id': u(302), 'type': 'struct', 'type_name': 'ShaderVec3'}}}),
     node(400, 'Model Sphere', 280, 430, {'inputs': {'Radius': r(12)}}),
     node(500, 'Model Translate', 580, 230),
     node(600, 'Model Smooth Union', 880, 0, {'inputs': {'Smoothness': r(3)}}),
     node(700, 'Behavior Expression', 280, 650,
          {'reference_name': cut,
           'inputs': {'form': {'id': u(701), 'type': 'float'}},
           'outputs': {'Result': {'id': u(702), 'type': 'struct', 'type_name': 'ShaderVec3'}}}),
     node(800, 'Model Sphere', 280, 850, {'inputs': {'Radius': r(10)}}),
     node(900, 'Model Translate', 580, 650),
     node(1000, 'Model Difference', 1180, 0),
     node(1100, 'Model Output', 1480, 0)],
    [('100.Geometry', '600.A'), ('200.Value', '300.form'),
     ('200.Value', '700.form'), ('300.Result', '500.Offset'),
     ('400.Geometry', '500.Geometry'), ('500.Geometry', '600.B'),
     ('600.Geometry', '1000.A'), ('700.Result', '900.Offset'),
     ('800.Geometry', '900.Geometry'), ('900.Geometry', '1000.B'),
     ('1000.Geometry', '1100.Geometry')],
    [expression_graph(1200, lobe, 1201), expression_graph(1300, cut, 1301)]))

fingerprints = ('0.06 * sin(2 * pi * length(vec2(position.x + 5, position.z - 3)) / '
                'max(spacing, 0.5) + 0.8 * sin(position.y * 0.15))')
roughness = ('clamp(roughness + variation * sin(position.x * 0.13) * '
             'sin(position.y * 0.17 + position.z * 0.11), 0.2, 0.9)')
write('content/materials/ClaySurface.chibi', graph(
    'ClaySurface', 2000,
    'Metallic-zero clay study. Object-attached curved fingerprint height and lower-frequency roughness vary independently; neither changes geometry or pigment.',
    [node(100, 'Shader ObjectPosition', 0, 0),
     node(200, 'Shader Parameter', 0, 240,
          {'reference_name': 'BaseColor', 'outputs': {'Value': {
              'type': 'struct', 'type_name': 'ShaderVec3',
              'default': vector('0.46', '0.43', '0.38')}}}),
     node(300, 'Shader Parameter', 0, 420,
          {'reference_id': u(1), 'reference_name': 'Roughness',
           'outputs': {'Value': r('0.68')}}),
     node(400, 'Shader Parameter', 0, 600,
          {'reference_id': u(2), 'reference_name': 'Variation',
           'outputs': {'Value': r('0.06')}}),
     node(500, 'Shader Parameter', 0, 780,
          {'reference_id': u(3), 'reference_name': 'Fingerprints',
           'outputs': {'Value': r(1)}}),
     node(600, 'Shader Parameter', 0, 960,
          {'reference_id': u(4), 'reference_name': 'Spacing',
           'outputs': {'Value': r('2.4')}}),
     node(700, 'Behavior Expression', 340, 0,
          {'reference_name': fingerprints,
           'inputs': {'position': {'id': u(701), 'type': 'struct', 'type_name': 'ShaderVec3'},
                      'spacing': {'id': u(702), 'type': 'float'}},
           'outputs': {'Result': {'id': u(703), 'type': 'float'}}}),
     node(800, 'Behavior Expression', 340, 420,
          {'reference_name': roughness,
           'inputs': {'position': {'id': u(801), 'type': 'struct', 'type_name': 'ShaderVec3'},
                      'roughness': {'id': u(802), 'type': 'float'},
                      'variation': {'id': u(803), 'type': 'float'}},
           'outputs': {'Result': {'id': u(804), 'type': 'float'}}}),
     node(900, 'Shader Bump', 700, 0, {'inputs': {'Distance': r('0.12')}}),
     node(1000, 'Shader Surface Output', 1040, 240, {'inputs': {'Alpha Mode': u(1)}})],
    [('100.Value', '700.position'), ('100.Value', '800.position'),
     ('200.Value', '1000.Base Color'), ('300.Value', '800.roughness'),
     ('400.Value', '800.variation'), ('500.Value', '900.Strength'),
     ('600.Value', '700.spacing'), ('700.Result', '900.Height'),
     ('800.Result', '1000.Roughness'), ('900.Value', '1000.Normal')],
    [expression_graph(1100, fingerprints, 1101),
     expression_graph(1200, roughness, 1201)], domain='material'))

entities = [{'name': 'View', 'prototype': 'ClayView'},
            {'name': 'Stage', 'prototype': 'ClayStage'},
            {'name': 'Key', 'prototype': 'ClayLamp'}]
for preset, y in [('Neutral', -30), ('Terracotta', 34)]:
    for kind, x in [('sphere', -108), ('capsule', -36), ('block', 36), ('sculpt', 108)]:
        components = {'Transform': {'x': r(x), 'y': r(y)}}
        if kind != 'sculpt':
            components['Shader'] = {'shape': 'Clay' + kind.capitalize()}
            components['ClaySettings'] = {'has_form': False}
        entities.append({'name': preset + ' ' + kind, 'prototype': 'Clay' + preset,
                         'components': components})
write('content/scenes/MaterialLab.chibi', {
    'version': u(1), 'name': 'MaterialLab', 'entities': entities})

write('content/prototypes/ClayLamp.chibi', {
    'name': 'ClayLamp', 'components': {
        'Transform': {'x': r(0), 'y': r(-76)},
        'Appearance': {'width': r(220), 'height': r(6), 'color': u(4294967295), 'relief': r(72)},
        'Material': {'scene': u(4294967295), 'albedo': u(4294967295),
                     'emission_color': u(4294967295), 'emission_intensity': r(1),
                     'emission_multiplier': r(1)}}})
write('content/prototypes/ClayNeutral.chibi', {
    'name': 'ClayNeutral', 'components': {
        'Transform': {'x': r(0), 'y': r(0)},
        'Input': {'move': {'x': r(0), 'y': r(0)}},
        'Geometry3D': {'width': r(36), 'height': r(36), 'depth': r(36), 'elevation': r(24)},
        'VisualTransform': {'scale': r(1)},
        'SdfSurface3D': {'enabled': True},
        'Material': {'scene': u(4294967295), 'albedo': u(4294967295), 'edge_light': r(1)},
        'Shader': {'material': 'ClaySurface', 'shape': 'ClaySculpt'},
        'ClaySettings': {'form': r(1), 'fingerprints': r(1), 'roughness': r('0.68'),
                         'variation': r('0.06'), 'spacing': r('2.4'),
                         'has_form': True, 'dirty': True}},
    'behaviors': ['ClayControls']})
write('content/prototypes/ClayStage.chibi', {
    'name': 'ClayStage', 'components': {
        'EngineSystem': {'asset': 'ApplyClay'},
        'HRC2D': {'resolution': r('0.5'), 'adaptive': False, 'target_fps': r(60),
                  'ambient': r('0.3'), 'intensity': r(1), 'falloff': r(1),
                  'shadow_floor': r('0.03'), 'radiance_hysteresis': r(1),
                  'bounce_hysteresis': r(1), 'feedback': False, 'surface_radius': r(24),
                  'surface_mode': 'Depth thickness', 'surface_depth_radius': r(320),
                  'surface_depth_gain': r(1), 'surface_depth_quality': 'Balanced',
                  'surface_depth_visibility': 'Bounded segments', 'surface_thickness': r(24),
                  'surface_relief': r('1.5'), 'surface_bevel': r(1), 'specular': r(1)}}})
write('content/prototypes/ClayTerracotta.chibi', {
    'name': 'ClayTerracotta', 'includes': ['ClayNeutral'],
    'components': {'Shader': {'parameters': source({
        'material.BaseColor': [r('0.52'), r('0.16'), r('0.08')]})}}})
write('content/prototypes/ClayView.chibi', {
    'name': 'ClayView', 'components': {
        'Transform': {'x': r(0), 'y': r(0)},
        'Camera': {'width': r(320), 'height': r(180), 'pitch': r('0.35'), 'target_elevation': r(12)},
        'SurfaceQuality': {'resolution': 'Full', 'sdf_normal_mode': 'Tetrahedral',
                           'sdf_pixel_error': r('0.25')}}})

# Executable declarations stay executable. Changes are current event/cast
# spelling and explicit real literals, not new event semantics or setters.
component = DRAFT / 'content/components/ClaySettings.chibi'
component.parent.mkdir(parents=True, exist_ok=True)
component.write_bytes((ORIGINAL / 'content/components/ClaySettings.chibi').read_bytes())

behavior = (ORIGINAL / 'content/behaviors/ClayControls.chibi').read_text()
for action in ['Form', 'Fingerprints', 'Variation', 'Oiliness', 'Reset']:
    behavior = behavior.replace('on ' + action + ' {', 'on ' + action + ' :: () {')
behavior = behavior.replace('.form > 0 {', '.form > 0.0 {')
behavior = behavior.replace('.fingerprints > 0 {', '.fingerprints > 0.0 {')
behavior = behavior.replace('.variation > 0 {', '.variation > 0.0 {')
behavior = behavior.replace('.form = 0\n', '.form = 0.0\n').replace('.form = 1\n', '.form = 1.0\n')
behavior = behavior.replace('.fingerprints = 0\n', '.fingerprints = 0.0\n').replace('.fingerprints = 1\n', '.fingerprints = 1.0\n')
behavior = behavior.replace('.variation = 0\n', '.variation = 0.0\n')
target = DRAFT / 'content/behaviors/ClayControls.chibi'
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(behavior)

system = (ORIGINAL / 'content/systems/ApplyClay.chibi').read_text()
system = system.replace('system(settings: ClaySettings) {',
                        'system(settings: ClaySettings) on Tick :: () {')
system = system.replace('cast<float>(', 'float(')
target = DRAFT / 'content/systems/ApplyClay.chibi'
target.parent.mkdir(parents=True, exist_ok=True)
target.write_text(system)
(DRAFT / 'preview.json').write_bytes((ORIGINAL / 'preview.json').read_bytes())
