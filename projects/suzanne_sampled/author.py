"""Create missing sampled-Suzanne assets through public MCP; preserve existing edits."""
import base64,json,os,subprocess,sys
from pathlib import Path
PROJECT=Path(__file__).resolve().parent
ROOT=PROJECT.parents[1]
EVIDENCE=ROOT/'.chibi/sampled-suzanne';EVIDENCE.mkdir(parents=True,exist_ok=True)
ENV={**os.environ,'CHIBI_ROOT':str(PROJECT),'CHIBI_CTL':str(ROOT/'build-release/chibi_ctl')}
def call(tool,**args):
 req={'jsonrpc':'2.0','id':1,'method':'tools/call','params':{'name':tool,'arguments':args}}
 run=subprocess.run(['sh',str(ROOT/'tools/mcp/launch.sh')],input=json.dumps(req)+'\n',text=True,capture_output=True,env=ENV,timeout=300)
 if run.returncode: raise RuntimeError(run.stderr)
 r=json.loads(run.stdout)
 if r.get('error') or r.get('result',{}).get('isError'): raise RuntimeError(json.dumps(r))
 return r['result']
def v(x,y,z):return dict(x=x,y=y,z=z)
def graph(kind,name,ops):
 if not (PROJECT/f'content/{kind}s/{name}.{"code" if kind in ("behavior", "library", "system") else "kdl"}').exists(): call('buildGraph',kind=kind,name=name,create=True,operations=([dict(op='remove_node',node=3)] if kind=='model' else [])+ops)
def wire(a,b,pin='Geometry'):return dict(op='connect',**{'from':a},from_pin='Geometry',to=b,to_pin=pin,kind='data')
def proto(name,components):
 if not (PROJECT/f'content/prototypes/{name}.kdl').exists():call('buildPrototype',kind='prototype',name=name,create=True,operations=[dict(op='add_component',component=k,fields=v) for k,v in components.items()])
def main():
 if not (PROJECT/'project.kdl').exists(): (PROJECT/'project.kdl').write_text('version 7\nname "Sampled Blender Suzanne"\ncontent "content"\nstart_scene "scenes/SuzanneFront.kdl"\n')
 provenance=json.loads((PROJECT/'reference/provenance.json').read_text())
 for field in provenance['grids']:
  name='BlenderSuzanne'+str(field['resolution'])
  ops=[dict(op='add_node',ref='volume',type='Model Sampled SDF')]
  for pin,value in {'Image':field['image'],'Half Size':v(*provenance['half_size']),'Resolution':field['resolution'],'Range':field['range'],'Lipschitz':field['lipschitz']}.items():ops.append(dict(op='set_input',node='volume',pin=pin,value=value))
  ops+=[dict(op='add_node',ref='part',type='Model Part',reference_name='Subdivided closed Suzanne'),wire('volume','part'),wire('part','output')]
  graph('model',name,ops)
  r=call('validateAsset',kind='model',name=name);assert r['structuredContent']['result']['valid'];print(name,'valid',flush=True)
  (EVIDENCE/(name+'-inspection.json')).write_text(json.dumps(call('inspectGraph',kind='model',name=name),indent=2))
  if '--export' in sys.argv:
   result=call('bakeModel',model=name,destination=str(EVIDENCE/('mesh'+str(field['resolution']))),options={'cell_size':1.2,'max_axis_cells':256,'max_triangles':349525,'max_samples':100000000,'max_work':100000000000})
   (EVIDENCE/('export'+str(field['resolution'])+'.json')).write_text(json.dumps(result,indent=2))
 resolution=max(g['resolution'] for g in provenance['grids'])
 graph('model','BlenderSuzanne',[dict(op='add_node',ref='asset',type='Model Instance',reference_asset='BlenderSuzanne'+str(resolution)),dict(op='add_node',ref='scale',type='Model Scale'),wire('asset','scale'),wire('scale','output')])
 graph('material','Clay',[dict(op='set_input',node='output',pin='Base Color',value=v(.64,.67,.7)),dict(op='set_input',node='output',pin='Roughness',value=.65)])
 proto('Camera',{'GameTransform':{},'GameCamera':{'width':150,'height':115,'focal_distance':0},'GameHRC2D':{'resolution':.5,'min_resolution':.5,'adaptive':False,'ambient':.12,'intensity':3,'exposure':.7,'surface_relief':1.5,'bloom':.01}})
 proto('Light',{'GameTransform':{},'GameAppearance':{'shape':'Circle','width':4,'height':4},'GameMaterial':{'scene':0xffffffff,'albedo':0xffffffff,'emission_color':0xffffffff,'emission_intensity':1.2,'emission_multiplier':1}})
 proto('Suzanne',{'GameTransform':{},'GameOrientation':{},'GameVisualTransform':{'scale':1,'roll':-1.57079632679},'GameGeometry3D':{'width':1,'height':1,'depth':1},'GameSdfSurface3D':{'enabled':True},'GameShader':{'shape':'BlenderSuzanne','material':'Clay'},'GameMaterial':{'absorption':1,'edge_light':1,'emission_multiplier':1}})
 for name,pitch,roll in [('SuzanneFront',0,-1.57079632679),('SuzanneThreeQuarter',.55,-1.4),('SuzanneSide',1.57079632679,-1.57079632679)]:
  if not (PROJECT/f'content/scenes/{name}.kdl').exists():call('buildScene',kind='scene',name=name,create=True,operations=[dict(op='add_entity',name='Camera',prototype='Camera'),dict(op='add_entity',name='Key',prototype='Light',components={'GameTransform':{'x':-68,'y':-48}}),dict(op='add_entity',name='Fill',prototype='Light',components={'GameTransform':{'x':68,'y':42},'GameMaterial':{'emission_intensity':.8}}),dict(op='add_entity',name='Suzanne',prototype='Suzanne',components={'GameVisualTransform':{'pitch':pitch,'roll':roll}})])
  assert call('validateAsset',kind='scene',name=name)['structuredContent']['result']['valid']
  if '--preview' in sys.argv:
   r=call('previewScene',kind='scene',name=name,width=900,height=690,frames=0,render_frames=2,freeze_simulation=True)
   for item in r['content']:
    if item['type']=='image': (EVIDENCE/(name+'.png')).write_bytes(base64.b64decode(item['data']))
 subprocess.run([ENV['CHIBI_CTL'],'--project-root',str(PROJECT),'--format'],check=True)
if __name__=='__main__':main()
