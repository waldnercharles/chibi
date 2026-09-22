// Live regression for command identity, revision checks and Undo across graph ticks.
// Requires the LayaLab fixture editor; changes are never saved.
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {setTimeout as delay} from 'node:timers/promises';
import {editorCommand} from '../mcp/editor-command.mjs';

const transport=JSON.parse(readFileSync(process.argv[2] || 0,'utf8'));
const call=packet=>editorCommand({...transport,...packet});
const inspect=()=>call({operation:'inspect'});
let catalog=await inspect();
const asset=catalog.objects.find(o=>o.kind==='behavior' && o.name==='CommandMath');
assert.ok(asset,'Open the LayaLab fixture project');
assert.equal((await call({operation:'submit',revision:catalog.revision,operations:[{op:'open',target:asset.id}]})).status,'applied');
await delay(100);
catalog=await inspect();
const real=value=>catalog.objects.find(o=>o.kind==='node' && o.name==='Real' && o.fields.some(f=>f.editable && f.value===value));
const first=real(2),second=real(3),multiply=catalog.objects.find(o=>o.kind==='node' && o.name==='Multiply');
assert.ok(first && second && multiply);
const value=first.fields.find(f=>f.editable);
const cases=[
 {op:'set',target:first.id,field:value.id,value:5},
 {op:'connect',target:second.id,field:second.fields.find(f=>f.output).id,destination:multiply.id,pin:multiply.fields.find(f=>f.name==='A' && !f.output).id},
];
for(const operation of cases){
 const before=await inspect();
 const result=await call({operation:'submit',revision:before.revision,operations:[operation]});
 assert.equal(result.status,'applied',JSON.stringify(result));
 try{
  await delay(100);
  const after=await inspect();
  assert.equal(after.revision,result.after_revision,'A frame without edits must preserve the command revision');
  assert.notEqual(after.graph_hash,before.graph_hash);
  assert.deepEqual(after.objects.filter(o=>o.kind==='node').map(o=>o.id),before.objects.filter(o=>o.kind==='node').map(o=>o.id));
  if(operation.op==='set')assert.equal(after.objects.find(o=>o.id===first.id).fields.find(f=>f.id===value.id).value,5);
  const stale=await call({operation:'submit',revision:before.revision,operations:[operation]});
  assert.equal(stale.status,'failed','Old observations must remain invalid');
 }finally{
  const undone=await call({operation:'respond',task_id:result.task_id,undo:true});
  assert.equal(undone.status,'undone',JSON.stringify(undone));
 }
 await delay(100);
 assert.equal((await inspect()).graph_hash,before.graph_hash,'Undo restores graph values, connections and layout');
}
console.log('PASS: graph values, connections, stable identities, stale revisions and guarded Undo');
