"""Validate the saved Blender comparison artifact without changing it."""
import json
import math
import sys
from pathlib import Path
import bpy

output=Path(sys.argv[sys.argv.index('--')+1])
scene=bpy.context.scene
meshes=[o for o in scene.objects if o.type=='MESH']
images=[i for i in bpy.data.images if i.source=='FILE']
assert len(meshes)==67, len(meshes)
assert images and all(i.packed_file for i in images)
assert scene.render.resolution_x==1040 and scene.render.resolution_y==780
assert scene.render.engine=='CYCLES' and scene.cycles.samples==128
assert abs(scene.camera.data.lens-45)<.00001
assert all(math.isfinite(x) for o in meshes for v in o.data.vertices for x in v.co)
assert bpy.data.materials['Optical glass IOR 1.49'].node_tree.nodes['Principled BSDF'].inputs['Transmission Weight'].default_value==1
assert bpy.data.objects.get('Curved transmitting front lens')
assert bpy.data.objects.get('Grooved lens barrel')
report={'passed':True,'blender':bpy.app.version_string,'file':bpy.data.filepath,
        'mesh_objects_including_backdrop':len(meshes),'packed_images':[i.name for i in images],
        'resolution':[1040,780],'samples':scene.cycles.samples,'lens_mm':scene.camera.data.lens,
        'finite_base_mesh_coordinates':True,'transmission_enabled':True}
output.write_text(json.dumps(report,indent=2)+'\n')
print('BLEND_VALIDATION',json.dumps(report))
