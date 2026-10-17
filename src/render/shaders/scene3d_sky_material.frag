// A surface material drawn as the sky: its Emission, plus Base Color when
// Unlit, along each direction. The view pass reads v_ray; the light pass
// renders one cube face per draw from its texel positions instead.
layout(location = 0) in vec3 v_ray;
layout(location = 0) out vec4 result;
layout(set = 3, binding = 0) uniform uniform_block {
    // The radiance scale.
    vec4 sky_scale;
    // The material, then the cube face and its edge in texels, or -1 for the view.
    vec4 sky_material;
    vec4 sky_parameters[32];
    // Cute's draw3d clock; the light pass sets zero.
    vec4 scene_time;
};

// Direction through face texel position st in [-1, 1], in the layout
// scene3d_sky_filter.frag reads; row 0 holds t = -1.
vec3 face_direction(int face, vec2 st)
{
    float s = st.x, t = st.y;
    if (face == 0) return vec3(1.0, -t, -s);
    if (face == 1) return vec3(-1.0, -t, s);
    if (face == 2) return vec3(s, 1.0, t);
    if (face == 3) return vec3(s, -1.0, -t);
    if (face == 4) return vec3(s, -t, 1.0);
    return vec3(-s, -t, -1.0);
}

void main()
{
    vec3 ray = sky_material.y < 0.0
        ? v_ray
        : face_direction(int(sky_material.y), gl_FragCoord.xy * 2.0 / sky_material.z - 1.0);
    vec3 d = normalize(ray);
    SGSample sg_context;
    sg_context.position = d.xz;
    sg_context.uv = vec2(0.5 + atan(d.x, -d.z) / 6.283185307179586,
                         0.5 - asin(clamp(d.y, -1.0, 1.0)) / 3.141592653589793);
    sg_context.object_position = d;
    sg_context.part_position = d;
    sg_context.world_position = d;
    sg_context.normal = -d;
    sg_context.view_direction = -d;
    for (int i = 0; i < 3; i++) {
        sg_context.object_from_world[i] = vec3(0);
        sg_context.object_from_world[i][i] = 1.0;
        sg_context.part_from_world[i] = sg_context.object_from_world[i];
    }
    sg_context.uv_from_world[0] = vec3(0);
    sg_context.uv_from_world[1] = vec3(0);
    sg_context.time = scene_time.x;
    vec4 parameters[32], surface[SG_OUTPUTS];
    for (int i = 0; i < 32; i++) parameters[i] = sky_parameters[i];
    sg_dispatch(int(sky_material.x), sg_context, parameters, surface);
    vec3 sky = max(surface[1].rgb, vec3(0));
    if (surface[6].x > 0.5) sky += max(surface[0].rgb, vec3(0));
    sky *= sky_scale.x;
    if (any(isnan(sky))) sky = vec3(0);
    result = vec4(min(sky, vec3(65504.0)), 1.0);
}
