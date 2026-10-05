"""Run with Blender 4.3.2 --background --factory-startup --python convert.py -- 96 144.
Blender's bundled monkey data is GPL-2.0-or-later; see reference/NOTICE.txt.
Two Catmull-Clark levels, THEN cap every boundary loop, THEN fixed triangulation.
The caps are part of the explicitly closed reference, not an assumed stock solid.
"""
import bpy,bmesh,json,hashlib,math,sys,time,zlib,struct
import numpy as np
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parent
EVIDENCE=ROOT.parents[1]/'.chibi/sampled-suzanne'
EVIDENCE.mkdir(parents=True,exist_ok=True)
assert bpy.app.version[:3]==(4,3,2),f'Pinned recipe requires Blender 4.3.2, got {bpy.app.version_string}'
bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
bpy.ops.mesh.primitive_monkey_add();obj=bpy.context.object
base={'vertices':[list(v.co) for v in obj.data.vertices],'faces':[list(p.vertices) for p in obj.data.polygons]}
base_bytes=(json.dumps(base,separators=(',',':'))+'\n').encode()
assert hashlib.sha256(base_bytes).hexdigest()=='17d946b91414f02b926bdacdef03c190923876261288f6bb57479067819ca25f', 'Bundled Suzanne geometry differs from pinned source'
(ROOT/'reference/stock-suzanne.json').write_bytes(base_bytes)
bm=bmesh.new();bm.from_mesh(obj.data)
stock_boundary=sum(e.is_boundary for e in bm.edges);bm.free()
mod=obj.modifiers.new('Pinned Catmull-Clark 2','SUBSURF');mod.subdivision_type='CATMULL_CLARK';mod.levels=2;mod.render_levels=2;mod.show_only_control_edges=False
bpy.ops.object.modifier_apply(modifier=mod.name)
bm=bmesh.new();bm.from_mesh(obj.data);bm.edges.ensure_lookup_table()
boundary=[e for e in bm.edges if e.is_boundary]
# Identify and report each boundary component before closing it.
remaining=set(boundary);loops=[]
while remaining:
 edge=min(remaining,key=lambda e:e.index);stack=[edge];loop=[]
 while stack:
  e=stack.pop()
  if e not in remaining: continue
  remaining.remove(e);loop.append(e)
  for v in e.verts: stack.extend(x for x in v.link_edges if x in remaining)
 loops.append(sorted(loop,key=lambda e:e.index))
for loop in loops: bmesh.ops.holes_fill(bm,edges=loop,sides=0)
bmesh.ops.triangulate(bm,faces=list(bm.faces),quad_method='FIXED',ngon_method='EAR_CLIP')
bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
assert all(e.is_manifold for e in bm.edges)
bm.to_mesh(obj.data);bm.free();obj.data.update()
vertices=np.array([(v.co.x,v.co.z,-v.co.y) for v in obj.data.vertices],dtype=np.float64)*40
faces=np.array([list(p.vertices) for p in obj.data.polygons],dtype=np.int32)
# Face-connected components; orient each closed component to positive volume.
parent=np.arange(len(vertices))
def find(a):
 while parent[a]!=a: parent[a]=parent[parent[a]];a=parent[a]
 return a
for f in faces:
 for v in f[1:]: parent[find(int(v))]=find(int(f[0]))
groups={}
for i,f in enumerate(faces): groups.setdefault(find(int(f[0])),[]).append(i)
components=[]
for ids in sorted(groups.values(),key=lambda x:min(x)):
 triangles=faces[ids].copy();a,b,c=(vertices[triangles[:,i]] for i in range(3));volume=np.einsum('ij,ij->i',a,np.cross(b,c)).sum()/6
 if volume<0: triangles=triangles[:,::-1];faces[ids]=triangles
 assert abs(volume)>1e-5
 points=vertices[np.unique(triangles)]
 tree=BVHTree.FromPolygons([Vector(x) for x in vertices],triangles.tolist(),all_triangles=True,epsilon=0)
 components.append((tree,points.min(axis=0),points.max(axis=0),len(triangles),abs(float(volume))))
half=np.max(np.abs(vertices),axis=0)+4.8
np.savez(EVIDENCE/'closed-reference.npz',vertices=vertices,faces=faces)
mesh_hash=hashlib.sha256(vertices.astype('<f8').tobytes()+faces.astype('<i4').tobytes()).hexdigest()
meta={'blender':bpy.app.version_string,'blender_build_hash':bpy.app.build_hash.decode(),'source':'https://github.com/blender/blender/blob/v4.3.2/source/blender/bmesh/operators/bmo_primitive.cc','license':'GPL-2.0-or-later','stock_sha256':hashlib.sha256(base_bytes).hexdigest(),'stock_vertices':len(base['vertices']),'stock_faces':len(base['faces']),'stock_boundary_edges':stock_boundary,'subdivision':'Catmull-Clark, levels=2, applied before closure','closure':'Cap every subdivided boundary loop with bmesh holes_fill; fixed quad split and EAR_CLIP ngons; consistent outward component orientation; union of three closed components','closed_boundary_edges':0,'closed_triangles':len(faces),'closure_loop_lengths':[len(loop) for loop in loops],'components':[{'triangles':x[3],'volume':x[4]} for x in components],'transform':'engine (x,y,z) = 40 * Blender (x,z,-y); no recentering','closed_geometry_sha256':mesh_hash,'half_size':half.tolist(),'sign':'negative inside union of closed components; parity scanlines per component','grids':[]}
def png(path,rgba):
 def chunk(kind,data): return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
 h,w,_=rgba.shape;raw=b''.join(b'\0'+row.tobytes() for row in rgba)
 path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,6,0,0,0))+chunk(b'IDAT',zlib.compress(raw,9))+chunk(b'IEND',b''))
def bake(n):
 start=time.time();axes=[np.linspace(-h,h,n,dtype=np.float64) for h in half]
 z,y,x=np.meshgrid(axes[2],axes[1],axes[0],indexing='ij');points=np.stack((x,y,z),axis=-1).reshape(-1,3)
 field=np.full(len(points),np.inf)
 for comp,(tree,lo,hi,_,_) in enumerate(components):
  inside=np.zeros((n,n,n),dtype=bool)
  # Grid rows are fixed; offset rays by a tiny deterministic amount only for
  # ambiguous edge coincidences. Sampling itself stays on the exact lattice.
  for iz,zv in enumerate(axes[2]):
   for iy,yv in enumerate(axes[1]):
    if not(lo[1]-1e-8<=yv<=hi[1]+1e-8 and lo[2]-1e-8<=zv<=hi[2]+1e-8): continue
    origin=Vector((float(-half[0]-1),float(yv+1e-7),float(zv+1.61803398875e-7)));hits=[]
    for _ in range(128):
     hit=tree.ray_cast(origin,Vector((1,0,0)),float(half[0]*2+2))
     if hit[0] is None: break
     hits.append(hit[0].x);origin=hit[0]+Vector((1e-3,0,0))
    assert len(hits)%2==0,(comp,iz,iy,hits)
    inside[iz,iy]=(np.searchsorted(hits,axes[0],side='right')%2)==1
  inside=inside.ravel();boxdist=np.linalg.norm(np.maximum(np.maximum(lo-points,points-hi),0),axis=1)
  candidates=np.flatnonzero(inside | ((field>=0)&(boxdist<=field)))
  for index in candidates:
   nearest=tree.find_nearest(Vector(points[index]))
   assert nearest[0] is not None
   distance=float(nearest[3])*(-1 if inside[index] else 1)
   field[index]=min(field[index],distance)
  print('grid',n,'component',comp,'queries',len(candidates),'seconds',round(time.time()-start,2),flush=True)
 field=field.reshape(n,n,n);distance_range=float(np.max(np.abs(field))*1.01)
 encoded=np.rint(np.clip((field/distance_range+1)*.5,0,1)*65535).astype(np.uint16)
 decoded=(encoded.astype(np.float32)*(np.float32(2)/np.float32(65535))-np.float32(1))*np.float32(distance_range)
 gradient=[float(np.max(np.abs(np.diff(decoded,axis=2-axis))))/(float(half[axis])*2/(n-1)) for axis in range(3)]
 lipschitz=float(math.sqrt(sum(g*g for g in gradient))*1.0001+1e-5)
 border=np.concatenate([decoded[0].ravel(),decoded[-1].ravel(),decoded[:,0].ravel(),decoded[:,-1].ravel(),decoded[:,:,0].ravel(),decoded[:,:,-1].ravel()]);assert border.min()>0
 cols=math.ceil(math.sqrt(n));rows=math.ceil(n/cols);atlas=np.zeros((rows*n,cols*n,4),dtype=np.uint8);atlas[:,:,3]=255
 for iz in range(n):
  tile=atlas[(iz//cols)*n:(iz//cols+1)*n,(iz%cols)*n:(iz%cols+1)*n];tile[:,:,0]=(encoded[iz]>>8).astype(np.uint8);tile[:,:,1]=(encoded[iz]&255).astype(np.uint8)
 digest=14695981039346656037
 for b in atlas.tobytes(): digest=((digest^b)*1099511628211)&((1<<64)-1)
 name=f'{digest:016x}.sdf.png';path=ROOT/'content/sdf'/name;png(path,atlas)
 np.save(EVIDENCE/f'field-{n}.npy',decoded)
 result={'resolution':n,'image':'content/sdf/'+name,'range':distance_range,'lipschitz':lipschitz,'spacing':(half*2/(n-1)).tolist(),'quantization_error_max':float(np.max(np.abs(decoded-field))),'border_min':float(border.min()),'pixels_fnv1a64':f'{digest:016x}','png_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'sample_bytes_sha256':hashlib.sha256(encoded.astype('<u2').tobytes()).hexdigest(),'atlas_size':[cols*n,rows*n]}
 return result
sizes=[int(x) for x in sys.argv[sys.argv.index('--')+1:]] if '--' in sys.argv else [96,144]
for n in sizes: meta['grids'].append(bake(n));(ROOT/'reference/provenance.json').write_text(json.dumps(meta,indent=2)+'\n')
print('DONE',json.dumps(meta),flush=True)
