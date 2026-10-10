layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec3 in_normal;
layout(location = 8) in vec4 in_model0;
layout(location = 9) in vec4 in_model1;
layout(location = 10) in vec4 in_model2;
layout(location = 11) in vec4 in_uv_rect;
layout(location = 15) in vec4 in_mesh_attributes;
layout(location = 0) out vec3 v_normal;
layout(location = 1) out vec3 v_world;
layout(location = 2) flat out vec4 v_material;
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

void main()
{
    vec4 p = vec4(in_pos, 1.0);
    vec3 world = vec3(dot(in_model0, p), dot(in_model1, p), dot(in_model2, p));
    // Cofactors preserve perpendicular normals under nonuniform scale. A
    // mirroring model's negative determinant would turn them inward. Its
    // sign comes from the axes' directions alone, as the CPU's culling
    // reads it, since the determinant itself underflows float at tiny
    // scales. Dividing every axis by one common scale keeps the cofactors in
    // range and their directions as they were, and taking the normal as the
    // direction of their sum keeps a short sum from underflowing.
    vec3 a = vec3(in_model0.x, in_model1.x, in_model2.x);
    vec3 b = vec3(in_model0.y, in_model1.y, in_model2.y);
    vec3 c = vec3(in_model0.z, in_model1.z, in_model2.z);
    float facing = dot(direction(a), cross(direction(b), direction(c))) < 0.0 ? -1.0 : 1.0;
    float scale = max(largest_part(a), max(largest_part(b), largest_part(c)));
    a /= scale;
    b /= scale;
    c /= scale;
    v_normal = facing * direction(cross(b, c) * in_normal.x + cross(c, a) * in_normal.y +
                                  cross(a, b) * in_normal.z);
    v_world = world;
    v_material = in_uv_rect;
    gl_Position = u_view_projection * vec4(world, 1.0);
}
