layout(location = 0) in vec3 in_pos;
layout(location = 8) in vec4 in_model0;
layout(location = 9) in vec4 in_model1;
layout(location = 10) in vec4 in_model2;
layout(set = 1, binding = 0) uniform uniform_block {
    mat4 u_view_projection;
};

void main()
{
    vec4 p = vec4(in_pos, 1.0);
    vec3 world = vec3(dot(in_model0, p), dot(in_model1, p), dot(in_model2, p));
    gl_Position = u_view_projection * vec4(world, 1.0);
}
