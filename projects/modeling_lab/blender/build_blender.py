"""Recreate the reference camera and render controlled comparisons in Blender.
Run: blender -b --factory-startup --python build_blender.py -- --variant refined
All geometry uses source coordinates X right, Y up, Z toward the lens.
"""
import argparse
import json
import math
import sys
import time
from pathlib import Path
import bpy
from mathutils import Vector, Matrix
from bpy_extras.object_utils import world_to_camera_view

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE))
import sdf_control as control


def args():
    p = argparse.ArgumentParser()
    p.add_argument('--variant', choices=['control', 'refined', 'diffuse'], default='refined')
    p.add_argument('--samples', type=int, default=96)
    p.add_argument('--width', type=int, default=1040)
    p.add_argument('--view', choices=['reference', 'side', 'back'], default='reference')
    p.add_argument('--output', type=Path, default=ROOT / 'build/model-evidence/blender-comparison')
    return p.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])


def material(name, color, roughness=.3, metallic=0, transmission=0, coat=0):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    bs = m.node_tree.nodes.get('Principled BSDF')
    for key, value in {'Base Color': (*color, 1), 'Roughness': roughness,
                       'Metallic': metallic, 'Transmission Weight': transmission,
                       'Coat Weight': coat, 'Coat Roughness': .13, 'IOR': 1.49}.items():
        bs.inputs[key].default_value = value
    return m


def bevel(obj, radius=.08):
    b = obj.modifiers.new('Manufactured edge fillets', 'BEVEL')
    b.width = radius
    b.segments = 5
    b.limit_method = 'ANGLE'
    n = obj.modifiers.new('Weighted corner normals', 'WEIGHTED_NORMAL')
    n.keep_sharp = True


def remove(obj):
    bpy.data.objects.remove(obj, do_unlink=True)


def refine(objects):
    by_name = {o.name: o for o in objects}
    mats = control.get_materials()
    # Preserve control geometry/materials in a separate .blend, not hidden in this file.
    ivory = material('Refined ivory glossy ABS', (.76, .745, .63), .22, coat=.32)
    black = material('Refined black molded ABS', (.013, .012, .010), .3, coat=.12)
    cream = material('Refined cream lens trim', (.72, .66, .47), .22)
    red = material('Refined red shutter', (.8, .026, .003), .2, coat=.4)
    glass = material('Optical glass IOR 1.49', (.93, .96, .99), .055, transmission=1)
    dark = material('Inside optical chamber', (.0015, .0018, .0024), 1)
    dark.node_tree.nodes.get('Principled BSDF').inputs['Specular IOR Level'].default_value=0
    metal = material('Viewfinder internal reflector', (.19, .21, .24), .12, metallic=.82)
    # Subtle molded plastic microtexture is geometric normal variation, not a photo.
    nt = black.node_tree
    noise = nt.nodes.new('ShaderNodeTexNoise')
    noise.inputs['Scale'].default_value = 105
    noise.inputs['Detail'].default_value = 2
    bump = nt.nodes.new('ShaderNodeBump')
    bump.inputs['Strength'].default_value = .13
    bump.inputs['Distance'].default_value = .018
    nt.links.new(noise.outputs['Fac'], bump.inputs['Height'])
    nt.links.new(bump.outputs['Normal'], nt.nodes.get('Principled BSDF').inputs['Normal'])
    for o in objects:
        slot = o.get('material_slot')
        replacement = {'Ivory': ivory, 'Black': black, 'Cream': cream, 'Red': red}.get(slot)
        if replacement:
            o.data.materials.clear()
            o.data.materials.append(replacement)
    def pick(fragment):
        return next(o for o in objects if fragment.lower() in o.name.lower())
    def replace(fragment, new):
        old = pick(fragment)
        objects.remove(old)
        remove(old)
        objects.append(new)
        return new
    # Raise the top, lens and controls from measured photo landmarks. Base stays fixed.
    replace('upright face', control.rounded_box('Refined ivory upright face', (4.8, 2.52, .18), (0, 6.12, 1.35), ivory, .16))
    replace('rear main shell', control.rounded_box('Refined rear shell', (4.75, 3.16, 1.94), (0, 5.46, -.75), black, .2))
    for word in ['Right shell reveal', 'Left shell reveal']:
        o = pick(word)
        o.scale.z *= 1.17
        o.location.z += .36
    # Apron still meets the face at its original lower edge; lifted front lip
    # matches the photographed short film compartment.
    apron = replace('sloped ivory apron', control.side_prism('Refined sloped ivory apron', [(2.9,4.8),(3.7,1.5),(4.0,1.5),(3.2,4.8)],4.8,mats['Rainbow']))
    bevel(apron, .075)
    pick('ivory front lip').location.z += .55
    for o in [pick('rear viewfinder stalk'), pick('raised rear eyecup'), pick('recessed eyecup')]:
        o.location.z += .35
    for o in [pick('top accessory'), pick('accessory shoe central')]:
        o.location.z += .72
    for o in [pick('cream shutter collar'), pick('red shutter button')]:
        o.location.x -= .22
        o.location.z += .37
        bevel(o, .035)
    pick('Raised OneStep').location.z += .54
    pick('Raised OneStep').location.x -= .25
    # Keep same photo sampled nameplate UV contract, preserving the two permitted labels.
    # Moving local mesh keeps authored shader coordinates unchanged.
    pick('film door nameplate').location.z += .16
    base = replace('Film housing', control.side_prism('Refined film housing', [(.15,-3.8),(3.3,-3.8),(2.85,4.9),(.15,4.9)], 5, black))
    for vertex in base.data.vertices:
        t=max(0,min(1,(vertex.co.z-.15)/2.7))
        vertex.co.x *= .92+.08*t
    slot = control.rounded_box('Film slot cutting tool',(4.35,.28,.8),(0,.66,4.8),black,.08)
    control._cut(base,slot)
    bevel(base, .12)
    # Remove opaque placeholder optics and replace with real chambers and curved glass.
    for fragment in ['Recessed lens barrel','Rear lens flare','Cream annular lens rim','Dark optical glass',
                     'Front viewfinder bevel','Dark front viewfinder window','Viewfinder internal',
                     'Exposure adjustment knob','Exposure index dash']:
        o=pick(fragment); objects.remove(o); remove(o)
    center=(.20,6.67,0)
    def lens(name, profile, mat):
        o=control.lathe(name, profile, center, mat, segments=256)
        o.scale.x = 1.035
        o.scale.z = 1.035
        objects.append(o)
        return o
    lens('Lens rear tapered mount', [(1.4,1.45),(1.56,1.45),(1.9,1.83),(1.88,2.17),(1.4,2.17)],black)
    # Closed annular barrel with physically modeled concentric grooves on its inner cone.
    profile=[(1.78,1.78),(1.88,2.38),(1.84,2.68),(1.70,2.68)]
    for i in range(36):
        t=i/35
        r=1.69-.35*t
        z=2.67-.70*t
        profile.extend([(r,z),(r-.007,z-.02),(r-.014,z-.018)])
    profile += [(1.33,1.83)]
    lens('Grooved lens barrel',profile,black)
    lens('Cream annular lens trim',[(1.76,2.67),(1.84,2.67),(1.84,2.71),(1.76,2.71)],cream)
    lens('Lens aperture and dark chamber',[(0,1.70),(1.30,1.70),(1.30,1.83),(0,1.83)],dark)
    # Closed biconvex disk; the contour is sampled analytically, not a flat decal.
    cap=[]
    for i in range(49):
        r=1.325*i/48
        cap.append((r,1.87+.4*(1-(r/1.325)**2)))
    for i in range(48,-1,-1):
        r=1.325*i/48
        cap.append((r,1.845-.035*(1-(r/1.325)**2)))
    lens('Curved transmitting front lens',cap,glass)
    # Inner reflection and aperture retain true depth when seen from another angle.
    lens('Inner retaining ring',[(1.19,1.79),(1.32,1.79),(1.32,1.84),(1.19,1.84)],black)
    finder=(3.38,7.32,1.63)
    outer=control.rounded_box('Front viewfinder surround',(1.03,.99,.10),finder,cream,.07)
    objects.append(outer)
    objects.append(control.rounded_box('Viewfinder dark backing',(.94,.89,.025),(3.38,7.32,1.755),dark,.04))
    # Faceted reflective walls and a curved central optic produce real reflected light.
    for x in [-.88,.88]:
        objects.append(control.rounded_box('Viewfinder silver side',(.045,.82,.095),(3.38+x,7.32,1.83),metal,.03))
    for y in [-.82,.82]:
        objects.append(control.rounded_box('Viewfinder silver edge',(.89,.04,.095),(3.38,7.32+y,1.83),metal,.02))
    optic=control.cylinder('Viewfinder inner dark optic',.60,.025,(3.38,7.30,1.80),dark)
    objects.append(optic)
    bpy.ops.mesh.primitive_uv_sphere_add(segments=64,ring_count=32,location=(3.38,-1.86,7.30))
    o=bpy.context.object; o.name='Convex viewfinder optical element'; o.scale=(.65,.17,.65)
    o.data.materials.append(glass)
    for p in o.data.polygons: p.use_smooth=True
    objects.append(o)
    objects.append(control.rounded_box('Transmitting viewfinder cover',(.92,.87,.025),(3.38,7.32,2.035),glass,.02))
    knob=control.lathe('Exposure dial',[(0,-.16),(.54,-.16),(.63,.10),(.55,.34),(0,.34)],(2.86,5.23,1.78),black)
    objects.append(knob)
    for i in range(32):
        a=i*2*math.pi/32
        p=(2.86+.596*math.cos(a),5.23+.596*math.sin(a),1.88)
        rib=control.rounded_box('Exposure dial grip %02d'%i,(.024,.036,.13),p,black,.014)
        rib.rotation_euler.y=-a
        objects.append(rib)
    objects.append(control.rounded_box('Exposure index',(.037,.16,.018),(2.86,6.00,1.56),cream,.015))
    return objects


def set_camera(scene, view):
    spec=json.loads((HERE/'sdf-camera.json').read_text())['canonical_model_camera']
    camera_data=bpy.data.cameras.new('Photograph comparison camera')
    camera=bpy.data.objects.new('Photograph comparison camera',camera_data)
    scene.collection.objects.link(camera)
    camera.matrix_world=Matrix(spec['matrix_world'])
    for key in ['lens','sensor_width','shift_x','shift_y']:
        setattr(camera_data,key,spec[key])
    camera_data.sensor_fit='HORIZONTAL'
    camera_data.clip_start=.05
    camera_data.clip_end=1000
    scene.camera=camera
    if view != 'reference':
        target=Vector((0,0,4.2))
        camera.location=(22,-15,13) if view=='side' else (17,22,12)
        camera.rotation_euler=(target-camera.location).to_track_quat('-Z','Y').to_euler()
        camera_data.lens=44
        camera_data.shift_x=0
        camera_data.shift_y=0
    return camera


def light(name,location,target,power,color,size,size_y=None):
    data=bpy.data.lights.new(name,'AREA')
    data.energy=power; data.color=color
    data.shape='DISK' if size_y is None else 'RECTANGLE'
    data.size=size
    if size_y is not None: data.size_y=size_y
    o=bpy.data.objects.new(name,data); bpy.context.scene.collection.objects.link(o)
    o.location=location
    o.rotation_euler=(Vector(target)-o.location).to_track_quat('-Z','Y').to_euler()
    return o


def main():
    a=args(); a.output.mkdir(parents=True,exist_ok=True)
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    scene=bpy.context.scene
    scene.render.engine='CYCLES'
    scene.cycles.samples=a.samples
    scene.cycles.use_denoising=True
    scene.cycles.seed=29
    scene.cycles.max_bounces=12
    scene.cycles.transmission_bounces=8
    scene.cycles.transparent_max_bounces=8
    preferences=bpy.context.preferences.addons['cycles'].preferences
    available=[t[0] for t in preferences.get_device_types(bpy.context)]
    for kind in ['METAL','OPTIX','CUDA','HIP','ONEAPI']:
        if kind in available:
            preferences.compute_device_type=kind; preferences.get_devices()
            chosen=[d for d in preferences.devices if d.type==kind]
            if chosen:
                for d in preferences.devices: d.use=d.type==kind
                scene.cycles.device='GPU'; break
    objects=control.build_control(centered=False)
    if a.variant in ['refined','diffuse']: objects=refine(objects)
    if a.variant=='diffuse':
        for m in {m for o in objects for m in o.data.materials}:
            if not m or not m.use_nodes: continue
            nt=m.node_tree; bs=nt.nodes.get('Principled BSDF')
            if not bs: continue
            diffuse=nt.nodes.new('ShaderNodeBsdfDiffuse')
            diffuse.inputs['Color'].default_value=bs.inputs['Base Color'].default_value
            if bs.inputs['Base Color'].links:
                nt.links.new(bs.inputs['Base Color'].links[0].from_socket,diffuse.inputs['Color'])
            if bs.inputs['Normal'].links:
                nt.links.new(bs.inputs['Normal'].links[0].from_socket,diffuse.inputs['Normal'])
            nt.links.new(diffuse.outputs['BSDF'],nt.nodes.get('Material Output').inputs['Surface'])
    camera=set_camera(scene,a.view)
    # A continuous physical backdrop and three area lights; no projected photo.
    ground=material('Warm paper backdrop',(.72,.64,.42),.64)
    curve=[(-200,.14),(12,.14)] + [(12+12*math.sin(i*math.pi/2/32), .14+12*(1-math.cos(i*math.pi/2/32))) for i in range(1,33)] + [(24,200)]
    verts=[(x,y,z) for x in [-200,200] for y,z in curve]
    n=len(curve); faces=[(i,i+1,i+1+n,i+n) for i in range(n-1)]
    mesh=bpy.data.meshes.new('Continuous paper sweep'); mesh.from_pydata(verts,[],faces); mesh.update()
    sweep=bpy.data.objects.new('Physical curved paper backdrop',mesh); scene.collection.objects.link(sweep); mesh.materials.append(ground)
    for poly in mesh.polygons: poly.use_smooth=True
    world=bpy.data.worlds.new('Studio ambient'); world.use_nodes=True
    world.node_tree.nodes['Background'].inputs['Color'].default_value=(.30,.34,.43,1)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value=.30
    scene.world=world
    light('Warm upper left softbox',(-12,-9,19),(0,0,3.7),3600,(1,.92,.73),5,3)
    light('Camera right cool fill',(10,-14,10),(0,-1,5),480,(.62,.72,1),5,7)
    light('Small optical reflection',(2,-16,8),(0,-2,6.7),85,(.82,.87,1),.35)
    light('Backdrop illumination',(-15,4,17),(-12,24,6),2800,(1,.94,.83),20,14)
    scene.render.resolution_x=a.width; scene.render.resolution_y=round(a.width*.75)
    scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG'; scene.render.image_settings.color_mode='RGBA'
    scene.view_settings.view_transform='Standard'
    scene.view_settings.look='None'
    scene.view_settings.exposure=0
    scene.render.film_transparent=False
    name=f'{a.variant}-{a.view}'
    scene.render.filepath=str(a.output/(name+'.png'))
    # Pack label textures so the .blend can be opened on Linux without this checkout.
    bpy.ops.file.pack_all()
    bpy.ops.wm.save_as_mainfile(filepath=str(a.output/(name+'.blend')))
    start=time.time(); bpy.ops.render.render(write_still=True); elapsed=time.time()-start
    points={'lens_center':(.20,6.67,2.71) if a.variant!='control' else (-.05,5.7,2.56),
            'shutter_center':(-3.12,5.12,1.87) if a.variant!='control' else (-2.9,4.75,1.87),
            'finder_center':(3.38,7.32,2.06) if a.variant!='control' else (3.05,6.55,1.77),
            'knob_center':(2.86,5.23,2.14) if a.variant!='control' else (3,4.55,2.14)}
    landmarks={}
    for name2,(x,y,z) in points.items():
        co=world_to_camera_view(scene,camera,Vector((x,-z,y)))
        landmarks[name2]=[co.x*a.width,(1-co.y)*round(a.width*.75)]
    report={'blender':bpy.app.version_string,'variant':a.variant,'view':a.view,'samples':a.samples,
            'resolution':[a.width,round(a.width*.75)],'render_seconds':elapsed,'device':scene.cycles.device,
            'objects':len(objects),'mesh_vertices':sum(len(o.data.vertices) for o in objects),
            'landmarks':landmarks,'reference_photo_on_surfaces':'two printed nameplates only',
            'engine':'Cycles','seed':29,'color_management':'Standard, no look, exposure 0',
            'camera_matrix':[list(r) for r in camera.matrix_world],
            'images':[{'name':i.name,'packed':bool(i.packed_file)} for i in bpy.data.images if i.source=='FILE']}
    (a.output/(name+'.json')).write_text(json.dumps(report,indent=2)+'\n')
    print('COMPARISON_REPORT',json.dumps(report))

if __name__=='__main__': main()
