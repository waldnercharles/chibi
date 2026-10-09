layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec4 in_color;
layout(location = 2) in vec4 in_edge;
layout(location = 8) in vec4 in_model0;
layout(location = 9) in vec4 in_model1;
layout(location = 10) in vec4 in_model2;
layout(location = 0) out vec4 v_color;
layout(location = 1) out vec4 v_edge;
layout(set = 1, binding = 0) uniform uniform_block {
    mat4 u_view_projection;
    vec4 trail_origin;
};

void main()
{
    vec3 world = vec3(dot(in_model0.xyz, in_pos), dot(in_model1.xyz, in_pos),
                      dot(in_model2.xyz, in_pos)) + trail_origin.xyz;
    v_color = in_color;
    v_edge = in_edge;
    gl_Position = u_view_projection * vec4(world, 1.0);
}
