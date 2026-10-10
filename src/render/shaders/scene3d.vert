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

void main()
{
    vec4 p = vec4(in_pos, 1.0);
    vec3 world = vec3(dot(in_model0, p), dot(in_model1, p), dot(in_model2, p));
    // Cofactors preserve perpendicular normals under nonuniform scale. A
    // mirroring model's negative determinant would turn them inward.
    vec3 a = vec3(in_model0.x, in_model1.x, in_model2.x);
    vec3 b = vec3(in_model0.y, in_model1.y, in_model2.y);
    vec3 c = vec3(in_model0.z, in_model1.z, in_model2.z);
    float facing = dot(a, cross(b, c)) < 0.0 ? -1.0 : 1.0;
    v_normal = facing * normalize(cross(b, c) * in_normal.x +
                                  cross(c, a) * in_normal.y + cross(a, b) * in_normal.z);
    v_world = world;
    v_material = in_uv_rect;
    gl_Position = u_view_projection * vec4(world, 1.0);
}
