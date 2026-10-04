"""Mesh control of author-polaroid.mjs. Authored coordinates: X right, Y up, Z front.

Helpers accept authored dimensions/positions and Blender Material objects.
They create meshes only; they never reset a scene or change render settings.
"""
import json
import math
from pathlib import Path

import bpy
import bmesh
from mathutils import Vector

PROJECT = Path(__file__).resolve().parent.parent
COLORS = {
    "Ivory": (.80, .77, .66), "Black": (.025, .026, .022),
    "Lens": (.012, .015, .02), "Glass": (.009, .018, .026),
    "Red": (.92, .055, .008), "Cream": (.78, .72, .58),
    "Metal": (.34, .33, .27), "Label": (.015, .014, .011),
    "White": (.75, .72, .61),
}
RAINBOW = [(.85, .055, .24), (.92, .12, .012), (1, .49, .008),
           (.86, .75, .014), (.26, .46, .075), (.008, .24, .36)]


def mapped(point):
    return (point[0], -point[2], point[1])


def authored(point):
    return (point[0], point[2], -point[1])


def _attribute(obj):
    attr = obj.data.attributes.get("control_authored_position")
    if attr:
        obj.data.attributes.remove(attr)
    attr = obj.data.attributes.new("control_authored_position", "FLOAT_VECTOR", "POINT")
    for vertex, item in zip(obj.data.vertices, attr.data):
        item.vector = authored(obj.matrix_world @ vertex.co)


def _mesh(name, vertices, faces, material, offset=(0, 0, 0)):
    mesh = bpy.data.meshes.new(name + " Mesh")
    mesh.from_pydata([mapped(p) for p in vertices], [], faces)
    mesh.update()
    bm = bmesh.new()
    bm.from_mesh(mesh)
    bmesh.ops.recalc_face_normals(bm, faces=bm.faces)
    bm.to_mesh(mesh)
    bm.free()
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    obj.location = mapped(offset)
    obj["part"] = name
    obj["material_slot"] = material.get("slot", material.name)
    mesh.materials.append(material)
    bpy.context.view_layer.update()
    _attribute(obj)
    return obj


def _apply(obj, modifier):
    with bpy.context.temp_override(object=obj, active_object=obj,
                                   selected_objects=[obj], selected_editable_objects=[obj]):
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    _attribute(obj)


def rounded_box(name, half, offset, material, radius=.08):
    x, y, z = half
    vertices = [(a*x, b*y, c*z) for a in (-1, 1) for b in (-1, 1) for c in (-1, 1)]
    faces = [(0, 1, 3, 2), (4, 6, 7, 5), (0, 4, 5, 1),
             (2, 3, 7, 6), (0, 2, 6, 4), (1, 5, 7, 3)]
    obj = _mesh(name, vertices, faces, material, offset)
    if radius:
        bevel = obj.modifiers.new("Authored rounded-box radius", "BEVEL")
        bevel.width = radius
        bevel.segments = 8
        bevel.affect = "EDGES"
        bevel.profile = .5
        bevel.use_clamp_overlap = False
        _apply(obj, bevel)
    return obj


def side_prism(name, points_yz, half_width, material, offset=(0, 0, 0)):
    count = len(points_yz)
    vertices = [(x, y, z) for x in (-half_width, half_width) for y, z in points_yz]
    faces = [tuple(reversed(range(count))), tuple(range(count, count*2))]
    faces.extend((i, (i+1) % count, (i+1) % count + count, i + count) for i in range(count))
    return _mesh(name, vertices, faces, material, offset)


def lathe(name, profile_radius_axis, offset, material, segments=192):
    # Revolve around original Y, then Rx(pi/2): original Z becomes the axis.
    vertices, rings, faces = [], [], []
    for radius, axis in profile_radius_axis:
        if radius == 0:
            rings.append([len(vertices)])
            vertices.append((0, 0, axis))
        else:
            rings.append(list(range(len(vertices), len(vertices)+segments)))
            vertices.extend((radius*math.cos(i*math.tau/segments),
                             radius*math.sin(i*math.tau/segments), axis)
                            for i in range(segments))
    for j, a in enumerate(rings):
        b = rings[(j+1) % len(rings)]
        if len(a) == len(b) == 1:
            continue
        for i in range(segments):
            k = (i+1) % segments
            if len(a) == 1:
                faces.append((a[0], b[i], b[k]))
            elif len(b) == 1:
                faces.append((a[i], b[0], a[k]))
            else:
                faces.append((a[i], b[i], b[k], a[k]))
    obj = _mesh(name, vertices, faces, material, offset)
    # Flat caps remain flat; circumferential walls receive interpolated normals.
    for polygon in obj.data.polygons:
        polygon.use_smooth = abs(polygon.normal.y) < .999999
    return obj


def cylinder(name, radius, half_depth, offset, material, segments=192):
    return lathe(name, [(0, -half_depth), (radius, -half_depth),
                        (radius, half_depth), (0, half_depth)], offset, material, segments)


def _cut(obj, tool):
    boolean = obj.modifiers.new("Authored Difference", "BOOLEAN")
    boolean.operation = "DIFFERENCE"
    boolean.solver = "EXACT"
    boolean.object = tool
    _apply(obj, boolean)
    bpy.data.objects.remove(tool, do_unlink=True)
    return obj


def _material(name, color, roughness=.38, metallic=0):
    material = bpy.data.materials.new("SDF Control " + name)
    material["slot"] = name
    material.use_nodes = True
    shader = material.node_tree.nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1)
    shader.inputs["Roughness"].default_value = roughness
    shader.inputs["Metallic"].default_value = metallic
    return material


def _math(tree, op, a, b=None):
    node = tree.nodes.new("ShaderNodeMath")
    node.operation = op
    for index, value in enumerate((a, b)):
        if value is None:
            continue
        if isinstance(value, (int, float)):
            node.inputs[index].default_value = value
        else:
            tree.links.new(value, node.inputs[index])
    return node.outputs[0]


def _position(tree):
    attr = tree.nodes.new("ShaderNodeAttribute")
    attr.attribute_name = "control_authored_position"
    axes = tree.nodes.new("ShaderNodeSeparateXYZ")
    tree.links.new(attr.outputs["Vector"], axes.inputs[0])
    return axes.outputs


def _homography(corners):
    rows = []
    for (u, v), (x, y) in zip(((0, 0), (1, 0), (1, 1), (0, 1)), corners):
        rows.extend(([u, v, 1, 0, 0, 0, -x*u, -x*v, x],
                     [0, 0, 0, u, v, 1, -y*u, -y*v, y]))
    for col in range(8):
        pivot = max(range(col, 8), key=lambda row: abs(rows[row][col]))
        rows[col], rows[pivot] = rows[pivot], rows[col]
        factor = rows[col][col]
        rows[col] = [value/factor for value in rows[col]]
        for row in range(8):
            if row != col:
                factor = rows[row][col]
                rows[row] = [a-factor*b for a, b in zip(rows[row], rows[col])]
    return [row[8] for row in rows]


def _label_material():
    material = _material("Label", COLORS["Label"], .45)
    tree = material.node_tree
    p = _position(tree)
    select = _math(tree, "GREATER_THAN", p[1], 3.2)
    corners = [((.31579, .38889), (.40241, .40058), (.39748, .45249), (.31689, .44079)),
               ((.23575, .71637), (.62774, .80556), (.62500, .86330), (.24178, .77632))]
    coordinates = []
    for points, xmin, width, ymax, height in ((corners[0], -3.95, 2, 6.72, .84),
                                             (corners[1], -4.45, 8.9, 1.96, .92)):
        u = _math(tree, "DIVIDE", _math(tree, "SUBTRACT", p[0], xmin), width)
        v = _math(tree, "DIVIDE", _math(tree, "SUBTRACT", ymax, p[1]), height)
        u = _math(tree, "MINIMUM", _math(tree, "MAXIMUM", u, 0), 1)
        v = _math(tree, "MINIMUM", _math(tree, "MAXIMUM", v, 0), 1)
        h = _homography(points)
        def affine(a, b, c):
            return _math(tree, "ADD", _math(tree, "ADD", _math(tree, "MULTIPLY", u, a),
                                            _math(tree, "MULTIPLY", v, b)), c)
        denominator = affine(h[6], h[7], 1)
        coordinates.append([_math(tree, "DIVIDE", affine(*h[i:i+3]), denominator)
                            for i in (0, 3)])
    uv = tree.nodes.new("ShaderNodeCombineXYZ")
    for i in range(2):
        component = _math(tree, "ADD", coordinates[1][i],
                          _math(tree, "MULTIPLY", select,
                                _math(tree, "SUBTRACT", coordinates[0][i], coordinates[1][i])))
        if i == 1:
            component = _math(tree, "SUBTRACT", 1, component)
        tree.links.new(component, uv.inputs[i])
    image = tree.nodes.new("ShaderNodeTexImage")
    image.image = bpy.data.images.load(str(PROJECT / "reference/polaroid-onestep-1280.jpg"), check_existing=True)
    image.extension = "EXTEND"
    tree.links.new(uv.outputs[0], image.inputs["Vector"])
    tree.links.new(image.outputs["Color"], tree.nodes.get("Principled BSDF").inputs["Base Color"])
    return material


def get_materials():
    materials = {name: _material(name, color, .09 if name == "Glass" else .26 if name == "Red" else .38,
                                  .7 if name == "Metal" else 0)
                 for name, color in COLORS.items() if name not in ("Label", "Lens")}
    materials["Label"] = _label_material()
    lens = _material("Lens", COLORS["Lens"])
    tree = lens.node_tree
    p = _position(tree)
    x = _math(tree, "ADD", p[0], .05)
    y = _math(tree, "SUBTRACT", p[1], 5.7)
    radius = _math(tree, "SQRT", _math(tree, "ADD", _math(tree, "MULTIPLY", x, x), _math(tree, "MULTIPLY", y, y)))
    wave = _math(tree, "ADD", .5, _math(tree, "MULTIPLY", .5, _math(tree, "SINE", _math(tree, "MULTIPLY", radius, 150))))
    g = _math(tree, "ADD", .65, _math(tree, "MULTIPLY", .35, _math(tree, "POWER", wave, 4)))
    color = tree.nodes.new("ShaderNodeCombineXYZ")
    for i, tint in enumerate((.03, .029, .026)):
        tree.links.new(_math(tree, "MULTIPLY", tint, g), color.inputs[i])
    shader = tree.nodes.get("Principled BSDF")
    tree.links.new(color.outputs[0], shader.inputs["Base Color"])
    tree.links.new(_math(tree, "ADD", .2, _math(tree, "MULTIPLY", .25, g)), shader.inputs["Roughness"])
    materials["Lens"] = lens
    rainbow = _material("Rainbow", COLORS["Ivory"], .35)
    tree = rainbow.node_tree
    x = _position(tree)[0]
    combine = tree.nodes.new("ShaderNodeCombineXYZ")
    inside = _math(tree, "LESS_THAN", _math(tree, "ABSOLUTE", x), .45)
    for channel in range(3):
        value = RAINBOW[0][channel]
        for i in range(1, 6):
            step = _math(tree, "GREATER_THAN", x, -.45+i*.15)
            value = _math(tree, "ADD", value, _math(tree, "MULTIPLY", step, _math(tree, "SUBTRACT", RAINBOW[i][channel], value)))
        value = _math(tree, "ADD", COLORS["Ivory"][channel],
                      _math(tree, "MULTIPLY", inside, _math(tree, "SUBTRACT", value, COLORS["Ivory"][channel])))
        tree.links.new(value, combine.inputs[channel])
    tree.links.new(combine.outputs[0], tree.nodes.get("Principled BSDF").inputs["Base Color"])
    materials["Rainbow"] = rainbow
    return materials


def build_control(lens_radius=1.8, scale=(1, 1, 1), centered=False):
    m = get_materials()
    objects = []
    def box(name, half, offset, material, radius=.08):
        obj = rounded_box(name, half, offset, m[material], radius)
        objects.append(obj)
        return obj
    def side(name, points, width, material, offset=(0, 0, 0)):
        obj = side_prism(name, points, width, m[material], offset)
        objects.append(obj)
        return obj
    def cyl(name, radius, depth, offset, material):
        obj = cylinder(name, radius, depth, offset, m[material])
        objects.append(obj)
        return obj
    def turned(name, points, offset, material):
        obj = lathe(name, points, offset, m[material])
        objects.append(obj)
        return obj
    base = side("Film housing with recessed ejection slot", [(.15, -3.8), (3.3, -3.8), (2.25, 4.9), (.15, 4.9)], 5, "Black")
    _cut(base, rounded_box("Control slot cutter", (4.35, .28, .8), (0, .66, 4.8), m["Black"], .08))
    box("Rear main shell", (4.75, 2.8, 1.94), (0, 5.1, -.75), "Black", .2)
    side("Sloped ivory apron", [(2.35, 4.8), (3.5, 1.5), (3.8, 1.5), (2.65, 4.8)], 4.8, "Rainbow")
    box("Ivory front lip", (4.8, .2, .13), (0, 2.44, 4.8), "Rainbow", .06)
    box("Ivory upright face", (4.8, 2.25, .18), (0, 5.65, 1.35), "Ivory", .16)
    box("Right shell reveal", (.065, 2.1, .23), (4.83, 5.65, 1.12), "Black", .05)
    box("Left shell reveal", (.065, 2.1, .23), (-4.83, 5.65, 1.12), "Black", .05)
    side("Rear viewfinder stalk", [(6.3, -1.7), (7.05, -4.7), (8.25, -4.7), (7.1, -1.7)], 1.2, "Black", (3.6, 0, 0))
    eye = box("Raised rear eyecup", (1.45, 1.03, .22), (4.2, 7.65, -4.8), "Black", .2)
    _cut(eye, rounded_box("Control eyecup cutter", (1.08, .7, .3), (4.2, 7.65, -5.03), m["Black"], .12))
    box("Recessed eyecup window", (1.04, .65, .02), (4.2, 7.65, -4.75), "Glass", .015)
    lens_start = len(objects)
    barrel = cyl("Recessed lens barrel", 1.8, .48, (-.05, 5.7, 2.04), "Lens")
    bore = lathe("Control conical lens bore", [(0, 0), (1.12, 0), (1.62, .62), (0, .62)], (-.05, 5.7, 2.09), m["Lens"])
    _cut(barrel, bore)
    turned("Rear lens flare", [(0, -.21), (1.5, -.21), (1.87, .16), (0, .16)], (-.05, 5.7, 1.76), "Black")
    turned("Cream annular lens rim", [(1.69, -.04), (1.8, -.04), (1.8, .04), (1.69, .04)], (-.05, 5.7, 2.52), "Cream")
    cyl("Dark optical glass", 1.11, .04, (-.05, 5.7, 2.13), "Glass")
    factor = lens_radius / 1.8
    for obj in objects[lens_start:]:
        obj.scale.x = obj.scale.z = factor
        obj.location.x = -.05 + (obj.location.x + .05)*factor
        obj.location.z = 5.7 + (obj.location.z - 5.7)*factor
    cyl("Cream shutter collar", .64, .08, (-2.9, 4.75, 1.61), "Cream")
    cyl("Red shutter button", .53, .12, (-2.9, 4.75, 1.75), "Red")
    box("Front viewfinder bevel", (1.02, .9, .09), (3.05, 6.55, 1.61), "Cream", .09)
    box("Dark front viewfinder window", (.93, .8, .035), (3.05, 6.55, 1.735), "Glass", .025)
    cyl("Viewfinder internal optical surface", .57, .017, (3.05, 6.55, 1.785), "Lens")
    turned("Exposure adjustment knob", [(0, -.16), (.52, -.16), (.63, .13), (.5, .36), (0, .36)], (3, 4.55, 1.78), "Black")
    box("Exposure index dash", (.045, .14, .018), (3, 5.3, 1.56), "White", .015)
    box("Raised OneStep badge", (1, .42, .026), (-2.95, 6.3, 1.56), "Label", .02)
    box("Polaroid film door nameplate", (4.45, .46, .035), (0, 1.5, 4.94), "Label", .025)
    box("Top accessory shoe", (1.8, .08, .48), (0, 7.98, .8), "Metal", .06)
    box("Accessory shoe central slot", (1.1, .013, .12), (0, 8.074, .8), "Black", .01)
    box("Visible film slot roller", (4.16, .035, .13), (0, .41, 4.55), "Cream", .025)
    for obj in objects:
        obj.location.z -= 4.2 if centered else 0
        obj.location = Vector((obj.location.x*scale[0], obj.location.y*scale[2], obj.location.z*scale[1]))
        obj.scale = Vector((obj.scale.x*scale[0], obj.scale.y*scale[2], obj.scale.z*scale[1]))
    bpy.context.view_layer.update()
    return objects


def control_report(objects):
    return {"source": "author-polaroid.mjs", "parts": len(objects),
            "coordinate_map": "(x,y,z) -> (x,-z,y)",
            "departures": ["192-sided cylinder/lathe mesh approximation of continuous revolution",
                           "8-segment Blender bevel approximates SDF circular edges and spherical corners",
                           "Separate overlapping meshes preserve named parts; union interior faces remain hidden",
                           "Principled BRDF and Blender filtering/lighting differ from the engine renderer"],
            "objects": [{"part": obj["part"], "material": obj["material_slot"],
                         "vertices": len(obj.data.vertices), "polygons": len(obj.data.polygons)}
                        for obj in objects]}
