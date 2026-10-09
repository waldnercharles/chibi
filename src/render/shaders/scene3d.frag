layout(location = 0) in vec3 v_normal;
layout(location = 1) in vec3 v_world;
layout(location = 2) flat in vec4 v_material;
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform sampler2D scene_parameters;
layout(set = 2, binding = SG_SHADOW_BINDING) uniform sampler2DShadow scene_shadow_map;
layout(set = 2, binding = SG_SHADOW_BLOCKERS_BINDING) uniform sampler2D scene_shadow_blockers;
layout(set = 2, binding = SG_LIGHT_SHADOW_BINDING) uniform sampler2DShadow scene_light_shadow_map;
const float PI = 3.141592653589793;
// Taps per blocker search and per soft filter, and the widest radius of each,
// in texels of the cascade; scene3d_shadow.c widens cascades by that radius.
const int SEARCH_TAPS = 16;
const int FILTER_TAPS = 32;
const float SOFT_TEXELS = 128.0;
// The surface's world-space derivatives across the screen.
vec3 world_dx;
vec3 world_dy;

vec4 material_value(int slot)
{
    int texel = int(v_material.x) * 38 + slot;
    return texelFetch(scene_parameters, ivec2(texel % 2048, texel / 2048), 0);
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

// Nine hardware 2x2 comparisons around coord, a texel apart.
float hard_shadow(vec3 coord, vec2 texel)
{
    float lit = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            lit += textureLod(scene_shadow_map, vec3(coord.xy + vec2(x, y) * texel, coord.z), 0.0);
        }
    }
    return lit / 9.0;
}

// How a cascade's depth changes across its atlas UV along the surface's plane,
// so that wide filters compare each tap against the surface's own depth there
// rather than the center's (receiver plane depth bias).
vec2 depth_slope(mat4 matrix)
{
    vec2 scale = vec2(0.5, -0.5) * scene_shadow.zw;
    vec3 dx = (matrix * vec4(world_dx, 0.0)).xyz, dy = (matrix * vec4(world_dy, 0.0)).xyz;
    dx.xy *= scale;
    dy.xy *= scale;
    float determinant = dx.x * dy.y - dx.y * dy.x;
    if (determinant == 0.0) return vec2(0);
    return vec2(dy.y * dx.z - dx.y * dy.z, dx.x * dy.z - dy.x * dx.z) / determinant;
}

// The receiver's plane at uv. Taps past the far plane compare as the far
// plane, so texels with nothing in them stay lit.
float plane_depth(vec3 coord, vec2 slope, vec2 uv)
{
    return min(coord.z + dot(slope, uv - coord.xy), 1.0);
}

// Tap i of count on a unit Vogel disc turned by angle.
vec2 vogel_tap(int i, int count, float angle)
{
    float theta = float(i) * 2.39996323 + angle;
    return sqrt((float(i) + 0.5) / float(count)) * vec2(cos(theta), sin(theta));
}

// Percentage-closer soft shadows: the farther the receiver lies past the
// blockers around it, the wider its filter, by the sun's angular size. The
// blocker search reads the nearest depth of each 2x2 block of texels, then
// the filter spreads hardware comparisons over a Vogel disc that each pixel
// turns differently. Taps stay inside the cascade's square, low to high.
// Reference: https://developer.download.nvidia.com/shaderlibrary/docs/shadow_PCSS.pdf
float soft_shadow(int cascade, vec3 coord, vec2 low, vec2 high)
{
    vec2 texel = scene_shadow.y * scene_shadow.zw;
    vec2 slope = depth_slope(scene_shadow_matrices[cascade]);
    float per_depth = scene_shadow_soft.x * scene_shadow_ranges[cascade] / scene_shadow_texels[cascade];
    float angle = 2.0 * PI * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));
    // A block's nearest texel can lie two texels off the tap, and half floats
    // hold depths near the far plane only to a 2048th.
    float tolerance = 2.0 * dot(abs(slope), texel) + coord.z / 1024.0;
    float search = clamp(per_depth * coord.z, 1.0, SOFT_TEXELS), blockers = 0.0, count = 0.0;
    for (int i = 0; i < SEARCH_TAPS; i++) {
        vec2 uv = clamp(coord.xy + vogel_tap(i, SEARCH_TAPS, angle) * search * texel, low, high);
        float depth = texelFetch(scene_shadow_blockers, ivec2(uv / texel) / 2, 0).r;
        if (depth >= plane_depth(coord, slope, uv) - tolerance) continue;
        blockers += depth;
        count += 1.0;
    }
    float radius = count > 0.0 ? min(per_depth * (coord.z - blockers / count), SOFT_TEXELS) : 0.0;
    if (radius < 1.5) return hard_shadow(coord, texel);
    float lit = 0.0;
    for (int i = 0; i < FILTER_TAPS; i++) {
        vec2 uv = clamp(coord.xy + vogel_tap(i, FILTER_TAPS, angle) * radius * texel, low, high);
        lit += textureLod(scene_shadow_map, vec3(uv, plane_depth(coord, slope, uv)), 0.0);
    }
    return lit / float(FILTER_TAPS);
}

// The sun's visibility from the first cascade, finest first, whose square
// holds the lookup, lifted along its geometric normal by up to 2.5 of that
// cascade's texels so that sloped surfaces stay in front of their own stored
// depth. A sun without angular size compares a 3x3 grid around the point.
float sun_shadow()
{
    vec3 normal = safe_normal(v_normal, vec3(0, 1, 0));
    float facing = clamp(dot(normal, scene_sun_direction.xyz), 0.0, 1.0);
    float lift = 0.5 + 2.0 * sqrt(1.0 - facing * facing);
    float edge = 1.5 * scene_shadow.y;
    vec2 texel = scene_shadow.y * scene_shadow.zw;
    for (int i = 0; i < int(scene_shadow.x); i++) {
        vec4 clip = scene_shadow_matrices[i] * vec4(v_world + normal * lift * scene_shadow_texels[i], 1.0);
        vec2 uv = vec2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
        if (any(lessThan(uv, vec2(edge))) || any(greaterThan(uv, vec2(1.0 - edge))) || clip.z >= 1.0) continue;
        vec2 square = vec2(i & 1, i >> 1);
        vec3 coord = vec3((uv + square) * scene_shadow.zw, clip.z);
        if (scene_shadow_soft.x <= 0.0) return hard_shadow(coord, texel);
        return soft_shadow(i, coord, square * scene_shadow.zw + texel, (square + 1.0) * scene_shadow.zw - texel);
    }
    return 1.0;
}

// Point light i's visibility from the one of its six faces whose 90 degrees
// hold the lookup, lifted along the geometric normal like the sun's by that
// face's texels at the point's distance, then compared on a 3x3 grid. Faces
// look along +x, -x, +y, -y, +z and -z, with up along +y, or +z for the faces
// along y, and each light takes one atlas row of them.
float point_shadow(int i)
{
    vec4 light = scene_light_positions[i];
    vec3 normal = safe_normal(v_normal, vec3(0, 1, 0));
    vec3 delta = v_world - light.xyz;
    float facing = clamp(-dot(normal, safe_normal(delta, -normal)), 0.0, 1.0);
    vec2 size = vec2(textureSize(scene_light_shadow_map, 0));
    float face_texels = size.x / 6.0, spread = scene_light_shadow.y;
    vec3 axis = abs(delta);
    float texel = 2.0 * spread * max(axis.x, max(axis.y, axis.z)) / face_texels;
    delta += normal * (0.5 + 2.0 * sqrt(1.0 - facing * facing)) * texel;
    axis = abs(delta);
    int along = axis.x >= axis.y && axis.x >= axis.z ? 0 : axis.y >= axis.z ? 1 : 2;
    vec3 forward = vec3(0);
    forward[along] = delta[along] >= 0.0 ? 1.0 : -1.0;
    int face = 2 * along + (delta[along] >= 0.0 ? 0 : 1);
    vec3 side = cross(forward, along == 1 ? vec3(0, 0, 1) : vec3(0, 1, 0));
    vec3 above = cross(side, forward);
    float ahead = dot(delta, forward);
    vec2 uv = vec2(dot(delta, side), -dot(delta, above)) / (ahead * spread) * 0.5 + 0.5;
    // Perspective depth with the near plane at a share of the radius.
    float near = scene_light_shadow.z;
    float depth = (1.0 - near * light.w / ahead) / (1.0 - near);
    vec2 coord = (vec2(face, i) + uv) * face_texels / size;
    float lit = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            lit += textureLod(scene_light_shadow_map, vec3(coord + vec2(x, y) / size, depth), 0.0);
        }
    }
    return lit / 9.0;
}

vec3 direct_light(vec3 base, float roughness, float metallic, vec3 normal, vec3 view)
{
    float sky = 0.5 + 0.5 * normal.y;
    vec3 ambient = mix(scene_ambient_ground.rgb, scene_ambient_sky.rgb, sky);
    vec3 result = base * (1.0 - metallic) * ambient;
    if (dot(scene_sun_color.rgb, scene_sun_color.rgb) > 0.0) {
        result += surface_light(base, roughness, metallic, normal, view, scene_sun_direction.xyz) *
                  scene_sun_color.rgb * sun_shadow();
    }
    for (int i = 0; i < int(scene_light_count.x); i++) {
        vec3 delta = scene_light_positions[i].xyz - v_world;
        float radius = scene_light_positions[i].w;
        float distance_squared = dot(delta, delta);
        if (distance_squared >= radius * radius || dot(normal, delta) < 0.0) continue;
        float fade = 1.0 - distance_squared / (radius * radius);
        vec3 light = safe_normal(delta, normal);
        vec3 radiance = scene_light_colors[i].rgb * scene_light_colors[i].w * fade * fade;
        if (i < int(scene_light_shadow.x)) radiance *= point_shadow(i);
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
    vec3 view = safe_normal(scene_camera_time.xyz - v_world, normal);
    SGSample sg_context;
    sg_context.position = local.xz;
    sg_context.uv = primitive_uv(local, material_value(37).xyz, object_normal, int(v_material.z));
    sg_context.object_position = local;
    sg_context.part_position = local;
    sg_context.object_from_world[0]=inverse_x.xyz;
    sg_context.object_from_world[1]=inverse_y.xyz;
    sg_context.object_from_world[2]=inverse_z.xyz;
    for(int i=0;i<3;i++) sg_context.part_from_world[i]=sg_context.object_from_world[i];
    vec3 dx=dFdx(v_world),dy=dFdy(v_world);
    world_dx = dx;
    world_dy = dy;
    vec2 du=dFdx(sg_context.uv),dv=dFdy(sg_context.uv);
    float xx=dot(dx,dx),yy=dot(dy,dy),xy=dot(dx,dy),determinant=xx*yy-xy*xy;
    for(int i=0;i<2;i++) sg_context.uv_from_world[i]=vec3(0);
    if(determinant>1e-20) {
        sg_context.uv_from_world[0]=(dx*(yy*du.x-xy*dv.x)+dy*(xx*dv.x-xy*du.x))/determinant;
        sg_context.uv_from_world[1]=(dx*(yy*du.y-xy*dv.y)+dy*(xx*dv.y-xy*du.y))/determinant;
    }
    sg_context.world_position = v_world;
    sg_context.normal = normal;
    sg_context.view_direction = view;
    sg_context.time = scene_camera_time.w;
    vec4 fallback = material_value(0);
    vec3 base = fallback.rgb;
    vec3 emission = material_value(1).rgb;
    float roughness = 0.5, metallic = 0.0, opacity = fallback.a;
    bool unlit = false;
    int material = int(v_material.y);
    if (material > 0) {
        vec4 parameters[32], surface[8];
        for (int i = 0; i < 32; i++) parameters[i] = material_value(i + 2);
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
