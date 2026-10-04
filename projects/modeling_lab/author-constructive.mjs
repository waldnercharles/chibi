#!/usr/bin/env node
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { dirname, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { call } from './author.mjs';

const project = dirname(fileURLToPath(import.meta.url));
const evidence = resolve(project, '../../build/model-evidence/constructive');
mkdirSync(evidence, { recursive: true });
const status = call('getProjectStatus', {}).structuredContent;
if (resolve(status?.result?.project_root || '') !== project) throw Error('Unexpected public MCP project root');
writeFileSync(resolve(evidence, 'project-status.json'), JSON.stringify(status, null, 2));
const v = (x, y, z) => ({ x, y, z }), v2 = (x, y) => ({ x, y });
const wire = (from, from_pin, to, to_pin) => ({ op: 'connect', from, from_pin, to, to_pin, kind: 'data' });
const geo = (from, to, pin = 'Geometry') => wire(from, 'Geometry', to, pin);
const node = (ref, type, inputs = {}, metadata = {}) => [
  { op: 'add_node', ref, type: `Model ${type}`, ...metadata },
  ...Object.entries(inputs).map(([pin, value]) => ['Geometry', 'A', 'B'].includes(pin)
    ? geo(value, ref, pin) : { op: 'set_input', node: ref, pin, value }),
];
const param = (ref, label, value, low, high) => [
  ...node(ref, 'Parameter', {}, { reference_name: label }),
  ...Object.entries({ Default: value, Minimum: low, Maximum: high }).map(([pin, value]) =>
    ({ op: 'set_input', node: ref, pin, type: typeof value === 'number' ? 'float' : 'ShaderVec3', value })),
];
function model(name, description, operations, output) {
  const ops = [...operations, geo(output, 'output')];
  if (ops.length + 1 > 128) throw Error(`${name} exceeds public graph operation budget`);
  if (!existsSync(resolve(project, `content/models/${name}.kdl`)))
    call('buildGraph', { kind: 'model', name, create: true, description, operations: ops });
  call('validateAsset', { kind: 'model', name });
  const inspect = call('inspectGraph', { kind: 'model', name });
  writeFileSync(resolve(evidence, `${name}.json`), JSON.stringify(inspect.structuredContent, null, 2));
}
for (const [name, rim, amount] of [
  ['ConstructRoundedPlate', 'Rounded Extrude', 'Radius'],
  ['ConstructBeveledPlate', 'Beveled Extrude', 'Width'],
]) model(name, 'Matched outer footprint and depth with a profile-level circular through-hole and independent rounded or beveled rim.', [
  ...node('outline', 'Profile Rectangle', { 'Half Size': v2(35, 26) }),
  ...node('hole', 'Profile Circle', { Radius: 12, Center: v2(-8, 0) }),
  ...node('perforated', 'Profile Difference'), wire('outline', 'Profile', 'perforated', 'A'), wire('hole', 'Profile', 'perforated', 'B'),
  ...node('rim', rim, { 'Half Depth': 8, [amount]: 4 }), wire('perforated', 'Profile', 'rim', 'Profile'),
  ...node('plate', 'Part', { Geometry: 'rim' }, { reference_name: 'Perforated plate' }),
], 'plate');
model('ConstructShellWindow', 'An outward shell opens through a real solid cut. The shell thickness is full width, not half width.', [
  ...node('sphere', 'Sphere', { Radius: 28 }),
  ...node('shell', 'Shell', { Geometry: 'sphere', Thickness: 4, Side: 'Outward' }),
  ...node('window', 'Box', { 'Half Size': v(40, 12, 40) }),
  ...node('open', 'Difference', { A: 'shell', B: 'window' }),
  ...node('part', 'Part', { Geometry: 'open' }, { reference_name: 'Split spherical shell' }),
], 'part');
// A concave bracket uses six straight cubic spans; the sampled contour is editable.
const bracket = [[-32,-26],[32,-26],[32,-5],[-8,-5],[-8,26],[-32,26]];
const cubic = bracket.flatMap((p, i) => {
  const q = bracket[(i + 1) % bracket.length];
  return [v2(...p), v2(p[0] + (q[0]-p[0])/3, p[1] + (q[1]-p[1])/3),
    v2(p[0] + 2*(q[0]-p[0])/3, p[1] + 2*(q[1]-p[1])/3)];
});
model('ConstructConcaveBezier', 'Six cubic spans with collinear handles form a concave bracket. Positive profile offset gives a rounded outer silhouette before extrusion.', [
  ...node('outline', 'Profile Bezier', { 'Control Points': cubic, Steps: 3 }),
  ...node('expand', 'Profile Offset', { Distance: 3 }), wire('outline', 'Profile', 'expand', 'Profile'),
  ...node('extrude', 'Extrude', { 'Half Depth': 7 }), wire('expand', 'Profile', 'extrude', 'Profile'),
  ...node('part', 'Part', { Geometry: 'extrude' }, { reference_name: 'Concave expanded bracket' }),
], 'part');
model('ConstructBezierCable', 'Two cubic spans sampled to sixteen capsule segments form a spatial cable with spherical ends and bends.', [
  ...node('cable', 'Bezier Tube', { 'Control Points': [v(-40,-20,0),v(-20,-45,8),v(10,-45,18),v(5,0,18),v(0,40,18),v(30,45,-8),v(42,14,-8)], Radius: 4, Steps: 8, Closed: false }),
  ...node('part', 'Part', { Geometry: 'cable' }, { reference_name: 'Sampled cubic cable' }),
], 'part');
model('ConstructPivotArm', 'An asymmetric arm rotates around an explicit remote pivot and then scales around the same point. Public angles retain the frame relationship.', [
  ...param('angle', 'Angles', v(0,0,.7), v(-Math.PI,-Math.PI,-Math.PI), v(Math.PI,Math.PI,Math.PI)),
  ...node('box', 'Rounded Box', { 'Half Size': v(28,6,7), Radius: 3 }),
  ...node('offset', 'Translate', { Geometry: 'box', Offset: v(25,0,0) }),
  ...node('rotation', 'Rotate Around', { Geometry: 'offset', Pivot: v(-3,0,0) }), wire('angle','Value','rotation','Angles'),
  ...node('scale', 'Scale Around', { Geometry: 'rotation', Pivot: v(-3,0,0), Factors: v(1,1.2,1) }),
  ...node('cap', 'Sphere', { Radius: 9 }),
  ...node('pivot', 'Translate', { Geometry: 'cap', Offset: v(-3,0,0) }),
  ...node('assembly', 'Union', { A: 'scale', B: 'pivot' }),
], 'assembly');
model('ConstructAnchorProbe', 'Reusable probe carries a public socket position. The same declaration moves its geometry and exported attachment frame.', [
  ...param('position','Socket Position',v(0,-20,0),v(-10,-30,-10),v(10,-10,10)),
  ...node('socket','Anchor', {}, { reference_name: 'Socket' }), wire('position','Value','socket','Position'),
  ...node('body','Capsule',{Radius:7,'Half Segment':14}),
  ...node('tip','Sphere',{Radius:10}), ...node('tip_move','Translate',{Geometry:'tip',Offset:v(0,19,0)}),
  ...node('shape','Union',{A:'body',B:'tip_move'}),
  ...node('placed','Translate',{Geometry:'shape'}),wire('position','Value','placed','Offset'),
], 'placed');
model('ConstructAnchorFixture', 'Two instances attach by exported socket identity to independent rotated frames. One instance has a numeric socket override; source geometry and anchor must remain coherent.', [
  ...node('left','Instance',{}, {reference_asset:'ConstructAnchorProbe'}),
  {op:'add_pin',node:'left',side:'output',name:'Socket',kind:'data',type:'handle',type_name:'Anchor3D'},
  ...node('right','Instance',{'Socket Position':v(7,-25,0)}, {reference_asset:'ConstructAnchorProbe'}),
  {op:'add_pin',node:'right',side:'output',name:'Socket',kind:'data',type:'handle',type_name:'Anchor3D'},
  ...node('left_frame','Anchor',{Position:v(-30,0,0),Angles:v(0,0,.65)}, {reference_name:'Left mount'}),
  ...node('right_frame','Anchor',{Position:v(30,0,0),Angles:v(0,0,-.65)}, {reference_name:'Right mount'}),
  ...node('left_attach','Attach',{Geometry:'left'}),wire('left','Socket','left_attach','Source Anchor'),wire('left_frame','Anchor','left_attach','Target Anchor'),
  ...node('right_attach','Attach',{Geometry:'right'}),wire('right','Socket','right_attach','Source Anchor'),wire('right_frame','Anchor','right_attach','Target Anchor'),
  ...node('pair','Union',{A:'left_attach',B:'right_attach'}),
  ...node('mount','Rounded Box',{'Half Size':v(40,5,9),Radius:3}),
  ...node('assembly','Union',{A:'pair',B:'mount'}),
], 'assembly');
const names = ['ConstructRoundedPlate','ConstructBeveledPlate','ConstructShellWindow','ConstructConcaveBezier','ConstructBezierCable','ConstructPivotArm','ConstructAnchorFixture'];
for (const name of names) {
  if (!existsSync(resolve(project, `content/prototypes/${name}.kdl`)))
    call('buildPrototype', {kind:'prototype',name,create:true,operations:Object.entries({
      GameTransform:{},GameOrientation:{}, GameVisualTransform:{scale:1,pitch:.1,yaw:.15,roll:-1.1},
      GameGeometry3D:{width:1,height:1,depth:1},GameSdfSurface3D:{enabled:true},
      GameShader:{shape:name,material:'StudyIvory'},GameMaterial:{absorption:1,edge_light:1,emission_multiplier:1},
    }).map(([component,fields])=>({op:'add_component',component,fields}))});
}
const scenes = {
  ModelConstructiveRims:[['ConstructRoundedPlate',-95,-40],['ConstructBeveledPlate',95,-40],['ConstructShellWindow',-90,65],['ConstructConcaveBezier',90,65]],
  ModelConstructivePaths:[['ConstructBezierCable',-75,0],['ConstructPivotArm',75,0]],
  ModelAnchorStudy:[['ConstructAnchorFixture',0,0]],
};
for (const [name,entries] of Object.entries(scenes)) {
  if (!existsSync(resolve(project, `content/scenes/${name}.kdl`)))
    call('buildScene',{kind:'scene',name,create:true,operations:[
      {op:'add_entity',name:'Camera',prototype:'StudyCamera',components:{GameCamera:{width:340,height:240},GameHRC2D:{surface_relief:0,ambient:.3}}},
      {op:'add_entity',name:'Key',prototype:'StudyLight',components:{GameTransform:{x:-145,y:-95}}},
      {op:'add_entity',name:'Fill',prototype:'StudyLight',components:{GameTransform:{x:145,y:80},GameMaterial:{emission_intensity:.15}}},
      ...entries.map(([prototype,x,y])=>({op:'add_entity',name:prototype,prototype,components:{GameTransform:{x,y}}})),
    ]});
  call('validateAsset',{kind:'scene',name});
  if (process.argv.includes('--preview')) {
    const result=call('previewScene',{kind:'scene',name,width:1020,height:720,frames:0,render_frames:2,freeze_simulation:true});
    for (const item of result.content||[]) if(item.type==='image')writeFileSync(resolve(evidence,`${name}.png`),Buffer.from(item.data,'base64'));
    writeFileSync(resolve(evidence,`${name}-preview.json`),JSON.stringify(result.structuredContent,null,2));
  }
}
