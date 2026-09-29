layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_world;
layout(location = 2) flat in vec4 v_material;
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform sampler2D arena_parameters;
const float PI = 3.141592653589793;

vec4 material_value(int slot)
{
    int texel = int(v_material.x) * 38 + slot;
    return texelFetch(arena_parameters, ivec2(texel % 2048, texel / 2048), 0);
}

vec3 safe_normal(vec3 value, vec3 fallback)
{
    float magnitude = dot(value, value);
    return magnitude > 0.00000001 ? value * inversesqrt(magnitude) : fallback;
}

vec2 primitive_uv(vec3 local, vec3 dimensions, vec3 object_normal, int primitive)
{
    vec3 p = local / dimensions;
    if (primitive == 9 || primitive == 10) p.x -= 1.0 / 6.0;
    if (primitive == 0 || primitive >= 4) {
        vec3 side = abs(object_normal);
        if (side.y >= side.x && side.y >= side.z) return p.xz + 0.5;
        if (side.x >= side.z) return vec2(p.z + 0.5, 0.5 - p.y);
        return vec2(p.x + 0.5, 0.5 - p.y);
    }
    float u = atan(p.z, p.x) / (2.0 * PI) + 0.5;
    if (primitive == 1) {
        vec3 direction = safe_normal(p, vec3(0, 1, 0));
        return vec2(u, 0.5 - asin(clamp(direction.y, -1.0, 1.0)) / PI);
    }
    return vec2(u, 0.5 - p.y);
}

// GGX distribution, height-correlated Smith visibility, Schlick Fresnel.
// Reference: https://google.github.io/filament/main/filament.html#materialsystem/standardmodelsummary
vec3 surface_light(vec3 base, float roughness, float metallic, vec3 normal, vec3 view, vec3 light)
{
    vec3 halfway = safe_normal(light + view, normal);
    float nv = max(dot(normal, view), 0.0001);
    float nl = max(dot(normal, light), 0.0);
    float nh = max(dot(normal, halfway), 0.0);
    float vh = max(dot(view, halfway), 0.0);
    float alpha = max(roughness * roughness, 0.0025);
    float a2 = alpha * alpha;
    float denominator = nh * nh * (a2 - 1.0) + 1.0;
    float distribution = a2 / max(PI * denominator * denominator, 0.00000001);
    float visibility = 0.5 / max(
        nl * sqrt(nv * nv * (1.0 - a2) + a2) +
        nv * sqrt(nl * nl * (1.0 - a2) + a2), 0.0001);
    vec3 f0 = mix(vec3(0.04), base, metallic);
    vec3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
    vec3 diffuse = base * (1.0 - metallic) * (1.0 - fresnel) / PI;
    vec3 specular = fresnel * distribution * visibility;
    return (diffuse + specular) * nl;
}

vec3 direct_light(vec3 base, float roughness, float metallic, vec3 normal, vec3 view)
{
    vec3 key = normalize(vec3(-0.45, 0.85, 0.4));
    float sky = 0.5 + 0.5 * normal.y;
    vec3 ambient = mix(vec3(0.06, 0.07, 0.10), vec3(0.17, 0.20, 0.27), sky);
    vec3 result = surface_light(base, roughness, metallic, normal, view, key) * 2.8 +
                  base * (1.0 - metallic) * ambient;
    for (int i = 0; i < int(arena_light_count.x); i++) {
        vec3 delta = arena_light_positions[i].xyz - v_world;
        float radius = arena_light_positions[i].w;
        float distance_squared = dot(delta, delta);
        if (distance_squared >= radius * radius || dot(normal, delta) < 0.0) continue;
        float fade = 1.0 - distance_squared / (radius * radius);
        vec3 light = safe_normal(delta, normal);
        vec3 radiance = arena_light_colors[i].rgb * arena_light_colors[i].w * fade * fade;
        result += surface_light(base, roughness, metallic, normal, view, light) * radiance;
    }
    return result;
}

vec4 graphic_particle(vec2 p, int shape, float age, vec4 tint, vec3 emission)
{
    float distance;
    if (shape == 1 || shape == 2) {
        distance = max(-p.x - 0.45, abs(p.y) - (0.9 - p.x) * 0.578);
        if (shape == 2) distance = max(distance, -distance - 0.14);
    } else if (shape == 3) {
        distance = abs(length(p) - 0.72) - mix(0.12, 0.045, age);
    } else if (shape == 4) {
        distance = abs(p.x) + abs(p.y) * 2.6 - 0.88;
    } else {
        // Three crisp lobes make a flat plasma mark, with no spherical shading.
        distance = length((p - vec2(0.05, 0)) * vec2(0.95, 1.12)) - 0.62;
        distance = min(distance, length(p - vec2(-0.42, 0.18)) - 0.33);
        distance = min(distance, length(p - vec2(-0.36, -0.25)) - 0.27);
    }
    float edge = max(fwidth(distance), 0.003);
    float coverage = 1.0 - smoothstep(-edge, edge, distance);
    float core = 1.0 - smoothstep(-edge, edge, distance + 0.19);
    if (shape == 2 || shape == 3) {
        core = 1.0 - smoothstep(-edge, edge, abs(distance + 0.06) - 0.025);
    }
    float ink = 1.0 - smoothstep(-edge, edge, distance + 0.055);
    vec3 body = tint.rgb * mix(0.48, 1.0, ink);
    body = mix(body, vec3(1.0, 0.98, 0.88), core * (0.86 - age * 0.3));
    return vec4(body + emission * (0.45 + core * 0.65), tint.a * coverage);
}

void main()
{
    vec4 inverse_x = material_value(34), inverse_y = material_value(35), inverse_z = material_value(36);
    vec4 world = vec4(v_world, 1.0);
    vec3 local = vec3(dot(inverse_x, world), dot(inverse_y, world), dot(inverse_z, world));
    if (v_material.w > 0.0) {
        result = graphic_particle(local.xy * 2.0 / material_value(37).xy,
            int(v_material.w) - 1, material_value(2).x, material_value(0), material_value(1).rgb);
        if (result.a <= 0.0) discard;
        return;
    }
    vec3 normal = safe_normal(v_normal, vec3(0, 1, 0));
    vec3 object_normal = vec3(dot(inverse_x.xyz, normal), dot(inverse_y.xyz, normal), dot(inverse_z.xyz, normal));
    vec3 view = safe_normal(arena_camera_time.xyz - v_world, normal);
    SGSample sg_context;
    sg_context.position = local.xz;
    sg_context.uv = primitive_uv(local, material_value(37).xyz, object_normal, int(v_material.z));
    sg_context.object_position = local;
    sg_context.world_position = v_world;
    sg_context.normal = normal;
    sg_context.view_direction = view;
    sg_context.time = arena_camera_time.w;
    vec4 fallback = material_value(0);
    vec3 base = fallback.rgb;
    vec3 emission = material_value(1).rgb;
    float roughness = 0.5, metallic = 0.0, opacity = fallback.a;
    bool unlit = false;
    int material = int(v_material.y);
    if (material > 0) {
        vec4 parameters[32], surface[8];
        for (int i = 0; i < sg_parameter_count(material); i++) parameters[i] = material_value(i + 2);
        sg_dispatch(material, sg_context, parameters, surface);
        base = max(surface[0].rgb, vec3(0));
        emission = max(surface[1].rgb, vec3(0));
        roughness = clamp(surface[2].x, 0.0, 1.0);
        metallic = clamp(surface[3].x, 0.0, 1.0);
        normal = safe_normal(surface[4].xyz, normal);
        opacity = clamp(surface[5].x, 0.0, 1.0);
        unlit = surface[6].x > 0.5;
    }
    if (isnan(opacity) || opacity <= 0.0) discard;
    vec3 color = (unlit ? base : direct_light(base, roughness, metallic, normal, view)) + emission;
    // Keep the scene-linear result within RGBA16F storage. A graph can produce
    // a larger value without turning subsequent bloom taps into infinity.
    if (isnan(color.r)) color.r = 0.0;
    if (isnan(color.g)) color.g = 0.0;
    if (isnan(color.b)) color.b = 0.0;
    result = vec4(clamp(color, vec3(0), vec3(65504)), opacity);
}
