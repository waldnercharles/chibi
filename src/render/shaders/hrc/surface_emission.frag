// Area-average display emission, retaining an actual source texel and a
// conservative emitting-depth spread. Non-emitting geometry never sets depth.
layout(set = 2, binding = 0) uniform sampler2D u_source;
layout(set = 2, binding = 1) uniform sampler2D u_geometry;
layout(set = 2, binding = 2) uniform sampler2D u_cover;
layout(set = 3, binding = 0) uniform uniform_block
{
    int u_first_level;
    int u_base_block;
};
layout(location = 0) out vec4 out_light;
layout(location = 1) out vec4 out_geometry;
const vec3 LUMA = vec3(0.2126, 0.7152, 0.0722);
void main()
{
    int block = u_first_level != 0 ? u_base_block : 2;
    ivec2 origin = ivec2(gl_FragCoord.xy) * block;
    ivec2 size = textureSize(u_source, 0);
    vec3 sum = vec3(0);
    float coverage = 0.0;
    vec4 representative = vec4(0);
    float strongest = 0.0, lower = 65504.0, upper = -65504.0;
    for (int y = 0; y < block; y++) {
        for (int x = 0; x < block; x++) {
            ivec2 p = origin + ivec2(x, y);
            if (any(greaterThanEqual(p, size))) continue;
            vec4 light = texelFetch(u_source, p, 0);
            vec4 geometry = texelFetch(u_geometry, p, 0);
            if (u_first_level != 0) {
                vec2 cover = texelFetch(u_cover, p, 0).rg;
                light.rgb = max(light.rgb, vec3(0)) * max(cover.r, 0.0);
                light.a = dot(light.rgb, LUMA) > 0.0 ? 1.0 : 0.0;
                geometry = vec4((vec2(p) + 0.5) / vec2(size),
                                cover.g > 1e-6 ? geometry.a / cover.g : 0.0, 0.0);
            }
            sum += light.rgb / float(block * block);
            coverage += light.a / float(block * block);
            if (light.a <= 0.0) continue;
            lower = min(lower, geometry.z - geometry.w);
            upper = max(upper, geometry.z + geometry.w);
            float power = dot(light.rgb, LUMA);
            if (power > strongest) {
                strongest = power;
                representative = geometry;
            }
        }
    }
    representative.w = strongest > 0.0
        ? max(representative.z - lower, upper - representative.z) : 0.0;
    out_light = vec4(min(sum, vec3(65504)), coverage);
    out_geometry = representative;
}
