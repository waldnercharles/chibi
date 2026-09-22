// Run against a fresh LayaLab editor. Changes remain unsaved and are undone.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {setTimeout as delay} from 'node:timers/promises';
import {editorCommand} from '../mcp/editor-command.mjs';

const transport = JSON.parse(readFileSync(process.argv[2] || 0, 'utf8'));
const call = packet => editorCommand({...transport, ...packet});
const inspect = () => call({operation:'inspect'});
const results = [];

async function settled(result) {
  for (let i=0; result.status==='resolving' && i<100; i++) {
    await delay(100);
    result=await call({operation:'status', task_id:result.task_id});
  }
  assert.notEqual(result.status, 'resolving', 'Command did not finish');
  return result;
}

async function run(command, check, extra={}) {
  let result=await settled(await call({operation:'submit', command, ...extra}));
  assert.ok(['applied','needs_review','ready'].includes(result.status), JSON.stringify(result));
  check?.(result.operations);
  const reviewed=result.status==='needs_review';
  if (reviewed) result=await call({operation:'respond', task_id:result.task_id, approve:true});
  assert.equal(result.status, extra.dry_run ? 'ready' : 'applied', JSON.stringify(result));
  results.push({command, reviewed, elapsed_ms:result.elapsed_ms});
  return result;
}

async function undo(result) {
  const reply=await call({operation:'respond', task_id:result.task_id, undo:true});
  assert.equal(reply.status, 'undone', JSON.stringify(reply));
}

function object(catalog, name, kind) {
  const match=catalog.objects.filter(o=>o.name===name && o.kind===kind);
  assert.equal(match.length,1,`Expected one ${kind} ${name}`);
  return match[0];
}
const value=(obj, field)=>obj.fields.find(f=>f.id===field).value;

let catalog=await inspect();
assert.ok(catalog.objects, 'Select one live editor session');
const boost=object(catalog,'BoostEmitter','prototype');
const color='GAME.GameParticleEmitter.color', end='GAME.GameParticleEmitter.end_color', rate='GAME.GameParticleEmitter.rate';
const batch=await run('Change boost emitter particle color to green and end color to blue', ops=>{
  assert.deepEqual(ops,[{op:'set',target:boost.id,field:color,value:(0x00ff0000+(value(boost,color)&255))>>>0},
                       {op:'set',target:boost.id,field:end,value:(0x0000ff00+(value(boost,end)&255))>>>0}]);
});
let after=object(await inspect(),'BoostEmitter','prototype');
assert.equal(value(after,color)>>>8,0x00ff00);
assert.equal(value(after,end)>>>8,0x0000ff);
const followup=await run('Set its rate to 31', ops=>assert.deepEqual(ops,[{op:'set',target:boost.id,field:rate,value:31}]));
assert.equal(value(object(await inspect(),'BoostEmitter','prototype'),rate),31);
await undo(followup);
await run('Undo', ops=>assert.deepEqual(ops,[{op:'undo'}]));
after=object(await inspect(),'BoostEmitter','prototype');
assert.equal(value(after,color),value(boost,color));
assert.equal(value(after,end),value(boost,end));

await run('Open Boost Emittr prototype', ops=>assert.deepEqual(ops,[{op:'open',target:boost.id}]));
await run('Select all LayaBlueMarker instances', ops=>assert.equal(ops.length,2));
catalog=await inspect();
assert.equal(catalog.objects.filter(o=>o.kind==='entity' && o.selected).length,2);
const marker=object(catalog,'Marker','entity');
const width='GAME.GameAppearance.width';
const x=marker.fields.find(f=>f.name==='x' && f.component==='GameTransform').id;
const grouped=await run('Set selected width to 72', ops=>assert.equal(ops.length,2));
assert.equal(value(object(await inspect(),'Marker','entity'),width),72);
assert.equal(value(object(await inspect(),'Marker East','entity'),width),72);
await undo(grouped);

catalog=await inspect();
const entities=catalog.objects.filter(o=>o.kind==='entity');
const duplicate=await run('Duplicate Marker then move the copy 30 units right', ops=>{
  assert.equal(ops[0].op,'duplicate');
  assert.equal(ops[0].target,marker.id);
  assert.equal(ops[1].result_target,ops[0].result);
});
const copies=(await inspect()).objects.filter(o=>o.kind==='entity' && !entities.some(e=>e.id===o.id));
assert.equal(copies.length,1);
assert.equal(value(copies[0],x),value(marker,x)+30);
await undo(duplicate);
assert.equal((await inspect()).objects.filter(o=>o.kind==='entity').length,entities.length);

catalog=await inspect();
let rejected=await settled(await call({operation:'submit',command:'Set Marker width to 40 and invent a new shader'}));
assert.equal(rejected.status,'unsupported');
assert.equal((await inspect()).revision,catalog.revision);
let pending=await call({operation:'submit',command:'Open'});
assert.equal((await call({operation:'cancel',task_id:pending.task_id})).status,'cancelled');
assert.equal((await inspect()).revision,catalog.revision);

await run('Open LayoutLab scene');
catalog=await inspect();
const tile=object(catalog,'Left Tile','entity');
const layout=await run('Set Left Tile width to 80 and height to 50', ops=>{
  assert.deepEqual(ops,[{op:'set',target:tile.id,field:width,value:80},
                       {op:'set',target:tile.id,field:'GAME.GameAppearance.height',value:50}]);
});
after=object(await inspect(),'Left Tile','entity');
assert.equal(value(after,width),80);
assert.equal(value(after,'GAME.GameAppearance.height'),50);
await undo(layout);
assert.equal(value(object(await inspect(),'Left Tile','entity'),width),value(tile,width));
await run('Play the scene');
assert.equal((await inspect()).playing,true);
await run('Stop the scene');
assert.equal((await inspect()).playing,false);
const laya=(await inspect()).objects.find(o=>o.kind==='scene' && o.path.endsWith('/LayaLab.json'));
assert.ok(laya);
await run('Open LayaLab scene', ops=>assert.deepEqual(ops,[{op:'open',target:laya.id}]));
assert.ok((await inspect()).objects.some(o=>o.kind==='entity' && o.name==='Boost'));
console.log(JSON.stringify({passed:true,results},null,2));
