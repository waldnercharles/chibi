// Adapted from Neon-Swarm shaders/hrc/prefilter.shd; original source comments retained.
// Prefilter for trace shader inputs.
//
// Averages each work cell's canvas-resolution footprint directly, so inputs
// need no mipmaps. Pixels count by the area they share with the cell, so
// cells that are not a whole number of pixels neither lose nor gain pixels:
//   Light:         box average up to 2x2 pixels, Gaussian-weighted beyond
//   Transmittance: area mean over a step of the cell's length
//   Bounce:        weighted like light, reprojected into the current view
// Light and transmittance average as hrc/source_average.shd describes.
//
// Emission and optical depth share one texture, which the trace samples at
// every step. Reflected light stays separate: it scales with the opacity a
// step meets even where emission does not.
//
// Drawn at work_w x work_h. Each fragment reads an S x S block of
// canvas-resolution pixels (S = canvas_dim / work_dim).

// RGB emission, A optical depth; see render_material_textures.
layout(set = 2, binding = 0) uniform sampler2D u_source;
layout(set = 2, binding = 1) uniform sampler2D u_bounce;

// RGB emission, A optical depth per canvas pixel.
layout(location = 0) out vec4 out_source;
layout(location = 1) out vec4 out_bounce;

layout(set = 3, binding = 0) uniform uniform_block
{
	int u_canvas_w;
	int u_canvas_h;
	int u_work_w;
	int u_work_h;
	float u_bounce_intensity;
	vec2 u_history_offset;
	int u_history_valid;
	vec4 u_crop_uv;
};

#include "hrc/hrc_domain.shd"
#include "hrc/source_average.shd"

vec3 bounce_sample(vec2 uv)
{
	uv += u_history_offset;
	if (u_history_valid == 0 || any(lessThan(uv, vec2(0.0))) ||
	    any(greaterThanEqual(uv, vec2(1.0)))) return vec3(0.0);
	return textureLod(u_bounce, uv, 0.0).rgb;
}

void main()
{
	ivec2 gid = ivec2(gl_FragCoord.xy);

	// Floating-point cell size (handles non-integer canvas/work ratios).
	vec2 canvas = vec2(u_canvas_w, u_canvas_h);
	vec2 cell = canvas / vec2(u_work_w, u_work_h);
	vec2 origin = vec2(gid) * cell;
	ivec2 lo = ivec2(floor(origin));
	ivec2 hi = min(ivec2(ceil(origin + cell)), ivec2(u_canvas_w, u_canvas_h));

	// Cells up to 2x2 pixels use a uniform average; larger cells use a Gaussian
	// with the Nyquist-correct sigma of half a cell.
	bool uniform_weights = all(lessThanEqual(cell, vec2(2.0)));
	vec2 center = origin + cell * 0.5;
	vec2 inv2s2 = 2.0 / (cell * cell);

	vec3 light_sum = vec3(0.0);
	vec3 bounce_sum = vec3(0.0);
	float transmittance_sum = 0.0;
	float area_sum = 0.0;
	float weight_sum = 0.0;
	for (int y = lo.y; y < hi.y; y++) {
		for (int x = lo.x; x < hi.x; x++) {
			ivec2 p = ivec2(x, y);
			vec2 pixel = vec2(p) + 0.5;
			vec2 overlap = min(vec2(p) + 1.0, origin + cell) - max(vec2(p), origin);
			float area = overlap.x * overlap.y;
			vec2 d = pixel - center;
			float w = area * (uniform_weights ? 1.0 : exp(-dot(d * d, inv2s2)));
			vec4 part = source_step(texelFetch(u_source, p, 0), cell.x);
			light_sum += part.rgb * w;
			bounce_sum += bounce_sample(pixel / canvas) * w;
			transmittance_sum += part.a * area;
			area_sum += area;
			weight_sum += w;
		}
	}
	vec2 uv = (vec2(gid) + 0.5) / vec2(u_work_w, u_work_h);
	vec4 source = source_resolve(vec4(light_sum / weight_sum, transmittance_sum / area_sum), cell.x);
	vec3 bounce = bounce_sum / weight_sum;
	out_source = vec4(source.rgb * hrc_emission_weight(uv, u_crop_uv), source.a);
	out_bounce = vec4(bounce * u_bounce_intensity, 1.0);
}
