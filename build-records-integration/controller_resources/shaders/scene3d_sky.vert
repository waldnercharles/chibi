layout(location = 0) in vec2 in_pos;
layout(location = 0) out vec3 v_ray;
layout(set = 1, binding = 0) uniform uniform_block {
    mat4 u_view_projection;
    mat4 sky_rays;
};

void main()
{
    // Two depths along each corner's view ray give its direction. Clip w
    // depends on depth alone, so the direction is linear across the screen
    // and interpolates exactly.
    vec4 near = sky_rays * vec4(in_pos, 0.0, 1.0);
    vec4 far = sky_rays * vec4(in_pos, 0.5, 1.0);
    v_ray = far.xyz / far.w - near.xyz / near.w;
    gl_Position = vec4(in_pos, 1.0, 1.0);
}
