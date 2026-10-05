"""Add an isolated Suzanne variant of sdf_lab's existing orbiting-light fixture."""
import copy,json,os,shutil,sys
from pathlib import Path
from author import call,ENV,ROOT,EVIDENCE
PROJECT=ROOT/'projects/sdf_lab'
ENV['CHIBI_ROOT']=str(PROJECT)
NAME='SdfSuzanneOrbitingEmitters';MODEL='SdfSampledSuzanne';OPPOSITE='SdfSuzanneOppositeCircle'
def result(tool,**args):return call(tool,**args)['structuredContent']['result']
source=result('readAsset',kind='scene',name='SdfDepthCoverageCircle')
result('searchNodes',query='Model Sampled SDF',domain='model')
result('inspectGraph',kind='behavior',name='SdfCoverageLightCircle')
provenance=json.loads((ROOT/'projects/suzanne_sampled/reference/provenance.json').read_text());field=next(x for x in provenance['grids'] if x['resolution']==144)
image=PROJECT/field['image'];image.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(ROOT/'projects/suzanne_sampled'/field['image'],image)
if not (PROJECT/f'content/models/{MODEL}.kdl').exists():
 ops=[{'op':'remove_node','node':3},{'op':'add_node','ref':'volume','type':'Model Sampled SDF'}]
 for pin,value in {'Image':field['image'],'Half Size':dict(zip(('x','y','z'),provenance['half_size'])),'Resolution':144,'Range':field['range'],'Lipschitz':field['lipschitz']}.items():ops.append({'op':'set_input','node':'volume','pin':pin,'value':value})
 ops += [{'op':'add_node','ref':'part','type':'Model Part','reference_name':'Subdivided closed Suzanne'},{'op':'connect','from':'volume','from_pin':'Geometry','to':'part','to_pin':'Geometry','kind':'data'},{'op':'connect','from':'part','from_pin':'Geometry','to':'output','to_pin':'Geometry','kind':'data'}]
 result('buildGraph',kind='model',name=MODEL,create=True,operations=ops)
if not (PROJECT/f'content/behaviors/{OPPOSITE}.kdl').exists():
 ops=[{'op':'add_variable','name':'elapsed','type':'float','default':0},{'op':'add_node','ref':'tick','type':'Event','reference_name':'Tick'},{'op':'add_node','ref':'advance','type':'Behavior Expression','source':'elapsed += dt'},{'op':'bind_input','node':'advance','pin':'elapsed','kind':'variable','variable':'elapsed'},{'op':'bind_input','node':'advance','pin':'dt','kind':'delta_time'},{'op':'connect','from':'tick','from_pin':'Then','to':'advance','to_pin':'Exec','kind':'exec'}]
 for axis,fn,previous in [('x','cos','advance'),('y','sin','x')]:
  ops += [{'op':'add_node','ref':axis,'type':'Behavior Expression','source':f'{axis} = -60 * {fn}(elapsed * (2 * pi / 8))'},{'op':'bind_input','node':axis,'pin':'elapsed','kind':'variable','variable':'elapsed'},{'op':'bind_input','node':axis,'pin':axis,'kind':'component','component':'GameTransform','field':axis},{'op':'connect','from':previous,'from_pin':'Then','to':axis,'to_pin':'Exec','kind':'exec'}]
 result('buildGraph',kind='behavior',name=OPPOSITE,create=True,owner_type='ecs.entity',description='Opposite-phase copy of SdfCoverageLightCircle for the isolated sampled Suzanne lighting demo.',operations=ops)
if not (PROJECT/f'content/scenes/{NAME}.kdl').exists():
 entities=copy.deepcopy(source['document']['entities']);camera,receiver,light=entities
 camera['components']['GameCamera'].update(width=180,height=160)
 camera['components']['GameHRC2D'].update(ambient=.002,exposure=3.0)
 receiver['name']='Subdivided Blender Suzanne';receiver['components'].update(GameGeometry3D={'width':1,'height':1,'depth':1,'elevation':0},GameVisualTransform={'scale':1.1,'pitch':.55,'yaw':0,'roll':-1.4},GameShader={'shape':MODEL,'material':'SdfIvory'})
 light['name']='Cyan orbiting emitter';light['components']['GameMaterial']['emission_intensity']=3;light['components']['GameVisualTransform']['scale']=.2
 opposite=copy.deepcopy(light);opposite['name']='Amber orbiting emitter';opposite['components']['GameTransform']['x']=-60;opposite['components']['GameMaterial']['emission_color']=0xffac54ff;opposite['behaviors']=[OPPOSITE];entities.append(opposite)
 result('buildScene',kind='scene',name=NAME,create=True,operations=[dict(op='add_entity',**e) for e in entities])
for kind,name in [('model',MODEL),('behavior',OPPOSITE),('scene',NAME)]:assert result('validateAsset',kind=kind,name=name)['valid']
assert result('readAsset',kind='scene',name='SdfDepthCoverageCircle')['hash']==source['hash']
poses=[]
for frames in (0,120,240,360,480):
 poses.append(result('captureRuntimeSnapshot',kind='scene',name=NAME,frames=frames,fields=['GameTransform.x','GameTransform.y','GameVisualTransform.pitch','GameVisualTransform.roll','GameShader.shape']))
for snapshot,expected in zip(poses,[(60,0),(0,60),(-60,0),(0,-60),(60,0)]):
 for name,sign in [('Cyan orbiting emitter',1),('Amber orbiting emitter',-1)]:
  entity=next(e for e in snapshot['entities'] if e['name']==name)
  transform=next(c for c in entity['components'] if c['name']=='GameTransform')
  values={f['name']:f['value'] for f in transform['fields']}
  assert abs(values['x']-sign*expected[0])<.01 and abs(values['y']-sign*expected[1])<.01,(name,values,expected)
(EVIDENCE/'orbit-emitter-poses.json').write_text(json.dumps(poses,indent=2))
(EVIDENCE/'orbit-source-preservation.json').write_text(json.dumps({'scene':'SdfDepthCoverageCircle','before_hash':source['hash'],'after_hash':source['hash'],'demo':NAME},indent=2))
print('Created and validated',NAME,flush=True)
