"""Blender background: sampled bidirectional distances, not Hausdorff bounds."""
import bpy,json,math
import numpy as np
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'.chibi/sampled-suzanne'
ref=np.load(OUT/'closed-reference.npz');vertices=ref['vertices'];faces=ref['faces']
def tree(v,f):return BVHTree.FromPolygons([Vector(p) for p in v],f.tolist(),all_triangles=True)
def sample(v,f):
 rng=np.random.default_rng(20261004);tri=v[f];area=np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)
 t=tri[rng.choice(len(f),size=50000,p=area/area.sum())];uv=rng.random((len(t),2));uv[uv.sum(axis=1)>1]=1-uv[uv.sum(axis=1)>1]
 return t[:,0]+uv[:,0,None]*(t[:,1]-t[:,0])+uv[:,1,None]*(t[:,2]-t[:,0])
def distances(points,target):
 d=np.array([target.find_nearest(Vector(p))[3] for p in points]);return {'count':len(d),'mean':float(d.mean()),'rms':float(np.sqrt((d*d).mean())),'p95':float(np.quantile(d,.95)),'sampled_max':float(d.max())}
reference=tree(vertices,faces);source_points=sample(vertices,faces)
# Stock eyes overlap the head; their buried surfaces are not union boundary.
parent=np.arange(len(vertices))
def find(a):
 while parent[a]!=a: parent[a]=parent[parent[a]];a=parent[a]
 return a
for face in faces:
 for index in face[1:]:parent[find(int(index))]=find(int(face[0]))
groups={}
for face in faces:groups.setdefault(find(int(face[0])),[]).append(face)
components=[tree(vertices,np.array(f)) for f in groups.values()]
def buried(point):
 for component in components:
  if component.find_nearest(Vector(point))[3]<1e-4:continue
  origin=Vector(point);hits=0
  for _ in range(128):
   hit=component.ray_cast(origin,Vector((1,.000137,.000193)),1000)
   if hit[0] is None:break
   hits+=1;origin=hit[0]+Vector((.001,.000000137,.000000193))
  if hits%2:return True
 return False
source_points=source_points[[not buried(p) for p in source_points]]
report={'method':'50000 deterministic area-weighted samples each direction; exact nearest triangle via Blender BVH; sampled maximum is not Hausdorff distance','source_samples_retained':len(source_points),'excluded':'Buried component surfaces inside another component are not part of the solid union boundary','seed':20261004,'units':'engine units; source width approximately 108','results':[]}
for n in (96,144):
 v=[];f=[]
 for line in (OUT/f'mesh{n}/model.obj').read_text().splitlines():
  parts=line.split()
  if parts and parts[0]=='v':v.append(list(map(float,parts[1:4])))
  if parts and parts[0]=='f':f.append([int(x.split('/')[0])-1 for x in parts[1:4]])
 v=np.array(v);f=np.array(f);surface=tree(v,f)
 result={'resolution':n,'vertices':len(v),'triangles':len(f),'export_cell_size':1.2,'source_to_extracted':distances(source_points,surface),'extracted_to_source':distances(sample(v,f),reference)}
 report['results'].append(result);print(json.dumps(result),flush=True)
(OUT/'geometry-comparison.json').write_text(json.dumps(report,indent=2)+'\n')
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
mesh=bpy.data.meshes.new('Closed Suzanne reference');mesh.from_pydata(vertices.tolist(),[],faces.tolist());mesh.update();obj=bpy.data.objects.new('Closed Suzanne reference',mesh);bpy.context.collection.objects.link(obj)
for polygon in mesh.polygons:polygon.use_smooth=True
scene=bpy.context.scene;scene.render.engine='BLENDER_WORKBENCH';scene.render.resolution_x=700;scene.render.resolution_y=700;scene.render.resolution_percentage=100
scene.display.shading.light='STUDIO';scene.display.shading.color_type='SINGLE';scene.display.shading.single_color=(.64,.67,.7);scene.display.shading.show_shadows=True;scene.display.shading.show_cavity=True;scene.display.shading.background_type='WORLD';scene.world.color=(.06,.06,.06)
bpy.ops.object.camera_add();camera=bpy.context.object;camera.data.type='ORTHO';camera.data.ortho_scale=130;scene.camera=camera
for name,yaw in [('front',0),('three-quarter',.55),('side',math.pi/2)]:
 camera.location=(250*math.sin(yaw),0,250*math.cos(yaw));camera.rotation_euler=(0,yaw,0);scene.render.filepath=str(OUT/f'reference-{name}.png');bpy.ops.render.render(write_still=True)
