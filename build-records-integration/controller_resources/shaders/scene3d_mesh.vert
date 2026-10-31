layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 2) in vec2 in_uv;
layout(location = 3) in vec3 in_part_position;
layout(location = 4) in vec3 in_part_frame0;
layout(location = 5) in vec3 in_part_frame1;
layout(location = 6) in vec3 in_part_frame2;
layout(location = 7) in vec4 in_tangent;
layout(location = 8) in vec4 in_model0;
layout(location = 9) in vec4 in_model1;
layout(location = 10) in vec4 in_model2;
layout(location = 11) in vec4 in_uv_rect;
layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_world;
layout(location = 2) flat out vec4 v_material;
layout(location = 3) out vec2 v_uv;
layout(location = 4) out vec3 v_object_normal;
layout(location = 5) out vec3 v_part_position;
layout(location = 6) out vec3 v_part_frame0;
layout(location = 7) out vec3 v_part_frame1;
layout(location = 8) out vec3 v_part_frame2;
layout(location = 9) out vec4 v_tangent;
layout(set = 1, binding = 0) uniform uniform_block {
    mat4 u_view_projection;
};

// The largest part of `v`, by size.
float largest_part(vec3 v)
{
    return max(abs(v.x), max(abs(v.y), abs(v.z)));
}

// `v` at unit length, divided by its largest part first so that a tiny or
// huge one neither underflows nor overflows on the way.
vec3 direction(vec3 v)
{
    return normalize(v / largest_part(v));
}

// An imported mesh in the lit pass: scene3d.vert's placement, plus the
// vertex's texture coordinate, tangent and model part frame for its material.
void main()
{
    vec4 p = vec4(in_pos, 1.0);
    vec3 world = vec3(dot(in_model0, p), dot(in_model1, p), dot(in_model2, p));
    vec3 a = vec3(in_model0.x, in_model1.x, in_model2.x);
    vec3 b = vec3(in_model0.y, in_model1.y, in_model2.y);
    vec3 c = vec3(in_model0.z, in_model1.z, in_model2.z);
    // As in scene3d.vert, the axes' directions say whether the model
    // mirrors, one common scale keeps their cofactors in range, and the
    // normal is their direction.
    float facing = dot(direction(a), cross(direction(b), direction(c))) < 0.0 ? -1.0 : 1.0;
    float scale = max(largest_part(a), max(largest_part(b), largest_part(c)));
    a /= scale;
    b /= scale;
    c /= scale;
    v_normal = facing * direction(cross(b, c) * in_normal.x + cross(c, a) * in_normal.y +
                                  cross(a, b) * in_normal.z);
    v_world = world;
    v_material = in_uv_rect;
    v_uv = in_uv;
    v_object_normal = in_normal;
    v_part_position = in_part_position;
    v_part_frame0 = in_part_frame0;
    v_part_frame1 = in_part_frame1;
    v_part_frame2 = in_part_frame2;
    // Tangents run along the surface, so they turn with the model, and a
    // mirror turns the bitangent the normal and tangent imply. Taking their
    // direction keeps one along an axis far shorter than the longest.
    v_tangent = vec4(direction(a * in_tangent.x + b * in_tangent.y + c * in_tangent.z), in_tangent.w * facing);
    gl_Position = u_view_projection * vec4(world, 1.0);
}
