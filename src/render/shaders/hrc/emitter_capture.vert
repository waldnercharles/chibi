layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec3 in_center;
layout(location = 3) in vec3 in_emission;
layout(location = 4) in float in_scale;
layout(location = 5) in float in_radius;
layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 center;
layout(location = 2) out vec3 emission;
layout(location = 3) out float density;
layout(location = 4) out float source_radius;
void main()
{
    gl_Position = vec4(in_pos.x, -in_pos.y, 0.0, 1.0);
    uv = in_uv;
    center = in_center;
    emission = in_emission;
    density = in_scale;
    source_radius = in_radius;
}
