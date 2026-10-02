// Adds fluid to the material targets in the tiles it covers: reads a copy of
// the material raster's and writes the originals, including the camera-only
// visibility; see render_material_textures.

layout(set = 2, binding = 0) uniform sampler2D u_scene;
layout(set = 2, binding = 1) uniform sampler2D u_source;
layout(set = 2, binding = 2) uniform sampler2D u_albedo;
layout(set = 2, binding = 3) uniform sampler2D u_normal;
layout(set = 2, binding = 4) uniform sampler2D u_visibility;
layout(set = 2, binding = 5) uniform sampler2D u_cover;

#define FLUID_PARTICLES_BINDING 6
#define FLUID_OFFSETS_BINDING 7
#define FLUID_INDICES_BINDING 8
#include "hrc/fluid_common.shd"

layout(location = 0) out vec4 out_scene;
layout(location = 1) out vec4 out_source;
layout(location = 2) out vec4 out_albedo;
layout(location = 3) out vec4 out_normal;
layout(location = 4) out vec4 out_visibility;
layout(location = 5) out vec4 out_cover;

void main()
{
    ivec2 pixel = ivec2(gl_FragCoord.xy);
    vec4 old_scene = texelFetch(u_scene, pixel, 0);
    vec4 old_source = texelFetch(u_source, pixel, 0);
    vec4 old_albedo = texelFetch(u_albedo, pixel, 0);
    vec4 old_normal = texelFetch(u_normal, pixel, 0);
    out_scene = old_scene;
    out_source = old_source;
    out_albedo = old_albedo;
    out_normal = old_normal;
    out_visibility = texelFetch(u_visibility, pixel, 0);
    out_cover = texelFetch(u_cover, pixel, 0);
    FluidSample f;
    if (!fluid_sample(pixel, f)) return;
    vec4 surface = vec4(f.liquid_color.rgb * f.liquid_alpha, f.liquid_alpha);
    surface += vec4(f.gas_color.rgb * f.gas_alpha, f.gas_alpha) * (1.0 - surface.a);
    surface += vec4(f.flame_color.rgb * f.flame_alpha, f.flame_alpha) * (1.0 - surface.a);
    surface = f.grain_color + surface * (1.0 - f.grain_color.a);

    // Scalar diagnostics use camera visibility, independent of incident GI.
    // Existing solid coverage still occludes them and no light is emitted.
    if (u_debug != 0) {
        float alpha = surface.a * (1.0 - old_scene.a);
        vec3 color = surface.rgb / max(surface.a, .00001);
        out_scene = vec4(old_scene.rgb * (1.0 - alpha), old_scene.a + alpha);
        out_source = old_source * (1.0 - alpha);
        out_albedo = old_albedo * (1.0 - alpha);
        out_normal = old_normal * (1.0 - alpha);
        out_visibility = vec4(out_visibility.rgb * (1.0 - alpha) + color * alpha,
                              out_visibility.a * (1.0 - alpha));
        return;
    }

    // Gas overlaps the visible surface as a shallow volume. Preserve some
    // emissive detail for ship/projectile readability; AI never samples this.
    float visible = 1.0 - old_scene.a;
    vec4 liquid_surface = vec4(f.liquid_color.rgb * f.liquid_alpha, f.liquid_alpha);
    vec4 flame_surface = vec4(f.flame_color.rgb * f.flame_alpha, f.flame_alpha);
    vec4 under = old_scene + (f.grain_color + (flame_surface + liquid_surface * (1.0 - f.flame_alpha)) * (1.0 - f.grain_color.a)) * visible;
    float cover = f.gas_alpha * .8;
    out_scene = vec4(under.rgb * (1.0 - cover) + f.gas_color.rgb * f.gas_alpha,
                     under.a + f.gas_alpha * (1.0 - under.a));
    // Fluid absorbs evenly, and emits like absorbing solids: its radiance
    // times its optical depth, as density (see hrc/source_average.shd).
    float grain_depth = f.grain_absorption * visible;
    float depth = f.absorption * max(visible, cover);
    vec3 density = depth > 0.0 ? f.emission * depth : f.emission;
    vec3 grain_density = f.grain_source * visible;
    out_source = vec4(old_source.rgb * (1.0 - cover * .6) + density + grain_density, old_source.a + depth + grain_depth);
    // Display shows emission rather than density; see render_material_textures.
    vec3 luma = vec3(.2126, .7152, .0722);
    float shown = dot(old_source.rgb * texelFetch(u_cover, pixel, 0).r * (1.0 - cover * .6) + f.emission + f.grain_emission * visible, luma);
    float dense = dot(out_source.rgb, luma);
    out_cover = vec4(dense > 1e-8 ? min(shown / dense, 60000.0) : 1.0, out_cover.gba);
    // Albedo alpha is material relief, which fluid leaves.
    out_albedo = vec4(old_albedo.rgb * (1.0 - cover) + surface.rgb * max(visible, cover), old_albedo.a);
    vec3 readability = f.liquid_alpha * f.liquid_visibility / max(f.liquid, .00001);
    readability += f.gas_alpha * f.gas_visibility / max(f.gas, .00001) * (1.0 - f.liquid_alpha);
    readability = f.grain_visibility + readability * (1.0 - f.grain_color.a);
    // Alpha marks participating fluid for camera exposure, not radiance feedback.
    float volume = f.grain_color.a + (f.liquid_alpha + f.gas_alpha * (1.0 - f.liquid_alpha)) * (1.0 - f.grain_color.a);
    out_visibility = vec4(readability * visible, volume * visible);
    float gradient_length = length(f.gradient);
    if (f.liquid_alpha > 0.0 && visible > 0.0) {
        float inside_distance = max(0.0, f.liquid - .6) / max(gradient_length, .0001);
        float surface_band = 1.0 - smoothstep(f.aa, f.aa * 12.0, inside_distance);
        float confidence = f.liquid_alpha * surface_band * visible;
        vec4 surface_normal = old_normal + vec4(-f.gradient / max(gradient_length, .0001), 0, 1) * confidence * (1.0 - old_normal.a);
        // Planar normals need only XY. Positive Z is solid edge-light gain;
        // negative Z marks liquid and stores its coverage-weighted distortion.
        surface_normal.z = mix(old_normal.z, -max(.001, f.liquid_refraction / f.liquid) * f.liquid_alpha, visible);
        out_normal = surface_normal;
    }
    if (f.grain_liquid > 0.0 && visible > 0.0) {
        float confidence = f.grain_liquid * visible;
        vec2 normal = f.grain_normal / max(f.grain_liquid, .00001);
        out_normal.xy = mix(out_normal.xy, normal, confidence);
        out_normal.z = mix(out_normal.z, -max(.001, f.grain_refraction / f.grain_liquid), confidence);
        out_normal.w = max(out_normal.w, confidence);
    }
}
