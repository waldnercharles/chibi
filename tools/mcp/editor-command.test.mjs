import test from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import http from 'node:http';
import {mkdtemp,writeFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {spawn} from 'node:child_process';
import {editorCommand} from './editor-command.mjs';

test('MCP reports inspections and task results without hiding status or diagnostics', async t=>{
  const path=join(await directory(t),'editor.sock');
  const replies=[
    {revision:'observed-1',objects:[],capabilities:['open']},
    {ok:true,status:'applied',task_id:'saved',message:'Saved scene'},
    {ok:false,status:'failed',task_id:'bad-pin',message:'Operation 3: incompatible pin types'},
  ];
  const server=net.createServer(socket=>{
    socket.once('data',()=>socket.end(JSON.stringify(replies.shift())+'\n'));
  });
  await new Promise(resolve=>server.listen(path,resolve));
  t.after(()=>new Promise(resolve=>server.close(resolve)));
  const child=spawn(process.execPath,[new URL('./server.mjs',import.meta.url).pathname],{stdio:['pipe','pipe','pipe']});
  t.after(()=>child.kill());
  let pending='';
  const responses=[];
  child.stdout.setEncoding('utf8');
  child.stdout.on('data',chunk=>{
    pending+=chunk;
    let newline;
    while((newline=pending.indexOf('\n'))>=0){
      responses.shift()(JSON.parse(pending.slice(0,newline)));
      pending=pending.slice(newline+1);
    }
  });
  async function call(operation){
    const received=new Promise(resolve=>responses.push(resolve));
    child.stdin.write(JSON.stringify({jsonrpc:'2.0',id:operation,method:'tools/call',params:{name:'editorCommand',arguments:{socket:path,operation}}})+'\n');
    return (await received).result;
  }
  const inspected=await call('inspect');
  assert.equal(inspected.isError,false);
  assert.equal(JSON.parse(inspected.content[0].text).revision,'observed-1');
  const applied=await call('submit');
  assert.equal(applied.isError,false);
  assert.equal(JSON.parse(applied.content[0].text).status,'applied');
  assert.equal(JSON.parse(applied.content[0].text).task_id,'saved');
  const failed=await call('status');
  assert.equal(failed.isError,true);
  assert.equal(JSON.parse(failed.content[0].text).message,'Operation 3: incompatible pin types');
  assert.equal(failed.structuredContent.task_id,'bad-pin');
});

async function directory(t) {
  const path=await mkdtemp(join(tmpdir(),'chibi-editor-api-'));
  t.after(()=>rm(path,{recursive:true,force:true}));
  return path;
}

test('native requests have replayable IDs and decode a fragmented response', async t=>{
  const path=join(await directory(t),'editor.sock');
  let request;
  const server=net.createServer(socket=>{
    let input='';
    socket.on('data',chunk=>{
      input+=chunk;
      if(!input.includes('\n'))return;
      request=JSON.parse(input);
      socket.write('{"status":"pending",');
      socket.end(`"task_id":${JSON.stringify(request.request_id)}}\n`);
    });
  });
  await new Promise(resolve=>server.listen(path,resolve));
  t.after(()=>new Promise(resolve=>server.close(resolve)));
  const result=await editorCommand({socket:path,operation:'submit',command:'Set width to 30'});
  assert.equal(result.status,'pending');
  assert.equal(result.task_id,request.request_id);
  assert.match(request.request_id,/^[0-9a-f-]{36}$/);
  assert.equal(request.command,'Set width to 30');
});

test('browser calls require a session and forward to the live bridge', async t=>{
  const file=join(await directory(t),'connection.json');
  const requests=[];
  const server=http.createServer(async (request,response)=>{
    assert.equal(request.headers['x-chibi-token'],'local-test-token');
    response.setHeader('Content-Type','application/json');
    if(request.url==='/__chibi/sessions')return response.end('[{"session":"one","active":true}]');
    let text='';for await (const chunk of request)text+=chunk;
    const body=JSON.parse(text);requests.push(body);
    response.end(JSON.stringify({status:'applied',task_id:body.packet.request_id}));
  });
  await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
  t.after(()=>new Promise(resolve=>server.close(resolve)));
  await writeFile(file,JSON.stringify({url:`http://127.0.0.1:${server.address().port}`,token:'local-test-token'}));
  const lookup=await editorCommand({connection_file:file,operation:'inspect'});
  assert.equal(lookup.status,'needs_clarification');
  assert.equal(lookup.sessions[0].session,'one');
  const result=await editorCommand({connection_file:file,session:'one',operation:'submit',request_id:'stable',command:'Open Marker'});
  assert.equal(result.task_id,'stable');
  assert.equal(requests[0].session,'one');
  assert.equal(requests[0].packet.command,'Open Marker');
  await writeFile(file,JSON.stringify({url:'https://example.com',token:'test'}));
  await assert.rejects(editorCommand({connection_file:file,session:'one',operation:'inspect'}),/loopback/);
});
