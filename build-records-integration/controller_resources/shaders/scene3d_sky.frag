layout(location = 0) in vec3 v_ray;
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform samplerCube sky_cube;
layout(set = 3, binding = 0) uniform uniform_block {
    vec4 sky_scale;
};

void main()
{
    result = vec4(min(texture(sky_cube, v_ray).rgb * sky_scale.x, vec3(65504.0)), 1.0);
}
