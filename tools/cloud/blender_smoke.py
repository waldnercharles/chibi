"""CPU-only OBJ contract: Blender Z-up -> Chibi Y-up, -Z forward; unit scale 1."""
from pathlib import Path
from tempfile import TemporaryDirectory

import bpy
from mathutils import Vector

assert bpy.app.version == (4, 3, 2), bpy.app.version_string
assert bpy.app.background, "Run with --background --factory-startup"
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
scene = bpy.context.scene
scene.unit_settings.system = "METRIC"
scene.unit_settings.scale_length = 1.0

# Unequal lengths, translation, rotation and nonuniform scale catch swapped axes,
# implicit scaling and lost transforms that a centered cube would miss.
mesh = bpy.data.meshes.new("asymmetric")
mesh.from_pydata([(0, 0, 0), (2, 0, 0), (0, 3, 0), (0, 0, 5)], [],
                 [(0, 2, 1), (0, 1, 3), (0, 3, 2), (1, 2, 3)])
mesh.update()
obj = bpy.data.objects.new("contract", mesh)
scene.collection.objects.link(obj)
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
obj.location = (1.25, -2.5, 3.75)
obj.rotation_euler = (0.2, -0.3, 0.4)
obj.scale = (1.5, 0.75, 2.0)
bpy.context.view_layer.update()
world = [obj.matrix_world @ v.co for v in mesh.vertices]


def same_points(actual, expected):
    assert len(actual) == len(expected), (len(actual), len(expected))
    unmatched = list(actual)
    for point in expected:
        closest = min(range(len(unmatched)), key=lambda i: (unmatched[i] - point).length)
        assert (unmatched.pop(closest) - point).length < 1e-5, point


with TemporaryDirectory(prefix="chibi-blender-smoke-") as temporary:
    path = Path(temporary) / "contract.obj"
    bpy.ops.wm.obj_export(filepath=str(path), export_selected_objects=True,
                          forward_axis="NEGATIVE_Z", up_axis="Y", global_scale=1.0,
                          apply_modifiers=True, export_triangulated_mesh=True,
                          export_normals=True, export_uv=False, export_materials=False)
    lines = path.read_text().splitlines()
    exported = [Vector(tuple(map(float, line.split()[1:]))) for line in lines if line.startswith("v ")]
    # Explicit independent expectation prevents a matching import/export mistake
    # from silently cancelling: Blender (x,y,z) becomes OBJ (x,z,-y).
    same_points(exported, [Vector((v.x, v.z, -v.y)) for v in world])
    assert len([line for line in lines if line.startswith("f ")]) == 4
    assert any(line.startswith("vn ") for line in lines)
    bpy.ops.object.delete(use_global=False)
    bpy.ops.wm.obj_import(filepath=str(path), forward_axis="NEGATIVE_Z", up_axis="Y",
                          global_scale=1.0, use_split_objects=False, use_split_groups=False)
    imported = [o for o in scene.objects if o.type == "MESH"]
    assert len(imported) == 1
    result = imported[0]
    same_points([result.matrix_world @ v.co for v in result.data.vertices], world)
    assert len(result.data.polygons) == 4
    assert all(len(p.vertices) == 3 for p in result.data.polygons)
    assert all(abs(s - 1.0) < 1e-6 for s in result.scale)
print("PASS Blender 4.3.2 bpy + OBJ: 4 vertices, 4 triangles; Y-up/-Z-forward; unit scale 1; baked transforms; CPU only")
