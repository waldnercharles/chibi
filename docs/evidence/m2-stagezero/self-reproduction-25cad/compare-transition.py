from pathlib import Path
import struct,hashlib,json,re
w=Path.cwd();p=w/'.chibi/stagezero-validation/self-reproduction-25cad';c=p/'captures'
sha=lambda b:hashlib.sha256(b).hexdigest()
def modules(b):
 magic,version,root,count=struct.unpack('<8sQQQ',b[:32]);assert magic==b'CHIBICLO' and version==1;off=32;rows={}
 for _ in range(count):
  ident,nlen,length=struct.unpack_from('<QQQ',b,off);off+=24;name=b[off:off+nlen].decode();off+=nlen;rows[name]={'id':ident,'offset':off,'data':b[off:off+length]};off+=length
 assert off==len(b);return root,rows
def word(b,at):
 result=0;shift=0
 while True:
  v=b[at];at+=1;result|=(v&127)<<shift
  if v<128:return result,at
  shift+=7;assert shift<70
def origin(b,at):
 out={};fields={}
 for n in ['start','end','line','column']:
  begin=at;v,at=word(b,at);out[n]=(v>>1)^-(v&1);fields[n]=[begin,at]
 return out,fields,at
first=(c/'transition-first.chibip').read_bytes();second=(c/'transition-second.chibip').read_bytes();two=(c/'stage-two.chibip').read_bytes();three=(c/'stage-three.chibip').read_bytes();assert second==two==three
ar,a=modules(first);br,b=modules(second);assert ar==br and a.keys()==b.keys();report={'source':'25cad452ae87beb899d1f2b43bb30a10c09af498','current_source_self_reproduction':True,'transition_equal':first==second,'artifacts':{n:{'bytes':len(x),'sha256':sha(x)} for n,x in [('transition-first',first),('transition-second',second),('stage-two',two),('stage-three',three)]},'modules':[],'origins':[]}
asset=b'project:package_compiler/Number.chibi';source=(w/'tools/compiler/package_compiler/Number.chibi').read_bytes()
for name,aa in a.items():
 bb=b[name];x=aa['data'];y=bb['data'];assert aa['id']==bb['id'] and len(x)==len(y) and aa['offset']==bb['offset'];diff=[i for i,(v,z) in enumerate(zip(x,y)) if v!=z];report['modules'].append({'name':name,'bytes':len(x),'first_sha256':sha(x),'second_sha256':sha(y),'changed_bytes':len(diff)})
 while diff:
  d=diff[0];start=x.rfind(asset,0,d);assert start>=0 and y[start:start+len(asset)]==asset;at=start+len(asset);old,fields,end=origin(x,at);new,nfields,nend=origin(y,at);assert fields==nfields and end==nend
  # Confirm the exact prepared_source prefix: graph=0, call=0,
  # parent=-1 (zigzag1), and a present asset string of len+1 bytes.
  assert x[start-4:start]==bytes([0,0,1,len(asset)+1])
  changed=[i for i in diff if at<=i<end];assert changed and all(i<end for i in changed)
  assert new==dict(old,start=old['start']+1,end=old['end']-1,column=old['column']+1)
  expr=source[old['start']:old['end']].decode();inner=source[new['start']:new['end']].decode();assert expr=='('+inner+')';functions=list(re.finditer(rb'^(?:pub )?(\w+)\s*::',source[:old['start']],re.M));function=functions[-1][1].decode()
  report['origins'].append({'module':name,'asset':asset.decode(),'function':function,'old':old,'current':new,'old_expression':expr,'current_expression':inner,'module_field_offsets':fields,'changed_module_byte_offsets':changed,'changed_closure_byte_offsets':[aa['offset']+i for i in changed],'old_encoded_fields':x[at:end].hex(),'current_encoded_fields':y[at:end].hex()});diff=[i for i in diff if i not in changed]
assert len(report['origins'])==5 and sum(r['changed_bytes'] for r in report['modules'])==15
(p/'divergence.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report['origins'],indent=2))
