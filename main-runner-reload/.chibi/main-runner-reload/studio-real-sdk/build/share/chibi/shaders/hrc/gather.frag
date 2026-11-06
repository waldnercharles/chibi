// Adapted from Neon-Swarm shaders/hrc/gather.shd; original source comments retained.
// C-1 gather: per-quadrant raymarching at canvas resolution.
//
// Samples each quadrant's resolved cascade-0 fluence (see resolve.frag) and
// traces c-1 intervals at full canvas resolution for per-pixel detail even
// when work_res < canvas_res.
//
// With split lighting, the work grid is a finer luminance transport's: its
// four directions arrive in one image (resolve_luminance.frag), and color
// comes from a coarser RGB transport on the color grid. Each quadrant keeps
// its color's proportions at the luminance's brightness.
//
// A fragment pass stores total light, far light for bounce, and the four RGB
// quadrant contributions. Each quadrant's fluence carries in alpha its share
// that sees the sky (see merge.frag): quadrants add the sky's light, and total
// light keeps it apart in alpha. When the work grid is coarser than the canvas,
// near-field steps come from u_near, which holds each canvas pixel's step once;
// see near.frag.

// RGB emission, A optical depth per canvas pixel, mipmapped; see
// render_material_textures.
layout(set = 2, binding = 0) uniform sampler2D u_source;
// The same, prefiltered onto the work grid.
layout(set = 2, binding = 1) uniform sampler2D u_source_filtered;
layout(set = 2, binding = 2) uniform sampler2D u_bounce;

// Color transport quadrants on the color grid.
layout(set = 2, binding = 3) uniform sampler2D u_f0;
layout(set = 2, binding = 4) uniform sampler2D u_f1;
layout(set = 2, binding = 5) uniform sampler2D u_f2;
layout(set = 2, binding = 6) uniform sampler2D u_f3;
// Split lighting: each quadrant's luminance on the work grid.
layout(set = 2, binding = 7) uniform sampler2D u_luminance;
layout(set = 2, binding = 8) uniform sampler2D u_near;

// A holds the share of the sky the pixel sees: one on open ground.
layout(location = 0) out vec4 out_light;
layout(location = 1) out vec4 out_far;
// RGB contributions from +x, +y (down the texture), -x and -y. Each already
// includes its quarter weight; surface shading preserves their colors.
layout(location = 2) out vec4 out_q0;
layout(location = 3) out vec4 out_q1;
layout(location = 4) out vec4 out_q2;
layout(location = 5) out vec4 out_q3;

layout(set = 3, binding = 0) uniform uniform_block
{
	int u_canvas_w;
	int u_canvas_h;
	int u_work_w;
	int u_work_h;
	int u_color_w;
	int u_color_h;
	int u_out_w;
	int u_out_h;
	int u_split;
	float u_bounce_intensity;
	vec2 u_history_offset;
	int u_history_valid;
	vec4 u_crop_uv;
	vec3 u_sky;
};

#include "hrc_common.shd"
#include "hrc/near_step.shd"

// Absorption difference that reconstruction treats as an edge.
const float UPSCALE_THRESHOLD = 0.1;

// The sky lights half the circle around open ground, so twice each
// quadrant's share gives it the whole sky color.
void output_quadrant(int rot, vec3 light, float sky)
{
	vec4 value = vec4((light + u_sky * (2.0 * sky)) * 0.25, 1.0);
	if (rot == 0) out_q0 = value;
	else if (rot == 1) out_q1 = value;
	else if (rot == 2) out_q2 = value;
	else out_q3 = value;
}

// Quadrant fluence at a color pixel; nothing arrives from outside the domain.
vec4 F_load(int rot, ivec2 p)
{
	if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, ivec2(u_color_w, u_color_h)))) return vec4(0.0);
	if (rot == 0) return texelFetch(u_f0, p, 0);
	if (rot == 1) return texelFetch(u_f1, p, 0);
	if (rot == 2) return texelFetch(u_f2, p, 0);
	return texelFetch(u_f3, p, 0);
}

// Hardware-filtered quadrant fluence at a continuous color-pixel position.
// Textures may keep a larger capacity; filtering stays inside the domain.
vec4 F_filtered(int rot, vec2 grid)
{
	grid = clamp(grid, vec2(0.0), vec2(u_color_w - 1, u_color_h - 1));
	vec2 uv = (grid + 0.5) / vec2(textureSize(u_f0, 0));
	if (rot == 0) return textureLod(u_f0, uv, 0.0);
	if (rot == 1) return textureLod(u_f1, uv, 0.0);
	if (rot == 2) return textureLod(u_f2, uv, 0.0);
	return textureLod(u_f3, uv, 0.0);
}

// Split lighting: color with this luminance, or grey where color has none.
vec3 with_luminance(vec3 color, float luminance)
{
	float l = dot(color, HRC_LUMA);
	return l > 1e-8 ? color * (luminance / l) : vec3(luminance);
}

// Color pixel position of a canvas pixel center.
vec2 color_grid(ivec2 canvas_pos)
{
	return (vec2(canvas_pos) + 0.5) * vec2(u_color_w, u_color_h) / vec2(u_canvas_w, u_canvas_h) - 0.5;
}

// Light, reflected light included, and transmittance of a step of length s
// at canvas pixel p, from its one-pixel step in u_near. A step's light
// follows its source length, as does its reflected light, which scales with
// its opacity; see hrc/source_average.shd.
vec4 stored_step(ivec2 p, float s)
{
	vec4 near = texelFetch(u_near, p, 0);
	float scale = s == 1.0 ? 1.0 : source_length(near.a, s) / source_length(near.a, 1.0);
	return vec4(near.rgb * scale, exp(-near.a * s));
}

// World-space march direction for each quadrant's rotated +x axis.
ivec2 quadrant_dir(int rot)
{
	if (rot == 1) return ivec2(0, 1);
	if (rot == 2) return ivec2(-1, 0);
	if (rot == 3) return ivec2(0, -1);
	return ivec2(1, 0);
}

// Raymarch c-1 interval from canvas pixel toward nearest c0 probe.
// Covers the gap between the pixel and the R_0 probe at +1 grid offset
// in the quadrant's +x direction, sampled at full canvas resolution.
// Starts from step 1 — step 0 (center pixel) is pre-accumulated by the caller.
Interval trace_cminus1(ivec2 canvas_pos, int rot, vec3 init_rad, float init_tau, float init_trn)
{
	ivec2 dir = quadrant_dir(rot);

	// Dimensions along march direction.
	int canvas_dim = (rot == 0 || rot == 2) ? u_canvas_w : u_canvas_h;
	int work_dim = (rot == 0 || rot == 2) ? u_work_w : u_work_h;

	// Position along march direction in canvas space.
	int pos_along;
	if      (rot == 0) pos_along = canvas_pos.x;
	else if (rot == 1) pos_along = canvas_pos.y;
	else if (rot == 2) pos_along = (u_canvas_w - 1) - canvas_pos.x;
	else               pos_along = (u_canvas_h - 1) - canvas_pos.y;

	// Grid cell this pixel maps to (must match R_0 bilinear grid in main()).
	float cell_f = (float(pos_along) + 0.5) * float(work_dim) / float(canvas_dim) - 0.5;
	int grid_cell = int(floor(cell_f));

	// First canvas pixel of the next cell (where grid_f = grid_cell + 1).
	int next_probe = int(ceil((float(grid_cell) + 1.5) * float(canvas_dim) / float(work_dim) - 0.5));
	int num_steps = max(next_probe - pos_along, 1);

	vec3 acc_rad = init_rad;
	float acc_trn = init_trn;
	float acc_tau = init_tau;

	// Bound full-resolution refinement work as adaptive resolution decreases.
	int samples = min(num_steps - 1, 2);
	float stride = float(num_steps - 1) / float(max(samples, 1));
	for (int i = 0; i < samples; i++) {
		int offset = int(ceil((float(i) + 0.5) * stride));
		ivec2 p = clamp(canvas_pos + dir * offset, ivec2(0), ivec2(u_canvas_w - 1, u_canvas_h - 1));

		vec4 near = stored_step(p, stride);
		float T = max(near.a, 1e-6);
		acc_rad += acc_trn * near.rgb;
		acc_trn *= T;
		acc_tau -= log(T);
	}

	return Interval(acc_rad, acc_tau);
}

void main()
{
	ivec2 gid = ivec2(gl_FragCoord.xy);
	ivec2 out_size = ivec2(u_out_w, u_out_h);
	ivec2 canvas_pos = gid;
	if (out_size.x != u_canvas_w || out_size.y != u_canvas_h) {
		canvas_pos = clamp(
			ivec2((vec2(gid) + 0.5) * vec2(u_canvas_w, u_canvas_h) / vec2(out_size)),
			ivec2(0),
			ivec2(u_canvas_w - 1, u_canvas_h - 1)
		);
	}

	// Matching grids need one probe per quadrant and only the center interval.
	// Keep the same accumulation order as the reconstruction path below.
	if (u_canvas_w == u_work_w && u_canvas_h == u_work_h) {
		vec3 bounce;
		vec4 near = near_step(canvas_pos, 1.0, bounce);
		float trn = near.a;
		vec3 near_rad = near.rgb + (1.0 - trn) * bounce;
		vec3 total = vec3(0.0), raw = vec3(0.0);
		float sky = 0.0;
		vec4 luminance = u_split != 0 ? texelFetch(u_luminance, canvas_pos, 0) : vec4(0.0);
		for (int rot = 0; rot < 4; rot++) {
			vec4 far = u_split != 0
			             ? F_filtered(rot, color_grid(canvas_pos))
			             : F_load(rot, canvas_pos);
			vec3 far_rad = far.rgb;
			vec3 quadrant = near_rad + trn * far_rad;
			if (u_split != 0) {
				quadrant = with_luminance(quadrant, dot(near_rad, HRC_LUMA) + trn * luminance[rot]);
				far_rad = with_luminance(far_rad, luminance[rot]);
			}
			total += quadrant;
			sky += trn * far.a;
			output_quadrant(rot, quadrant, trn * far.a);
			raw += far_rad;
		}
		out_light = vec4(total * 0.25, sky * 0.5);
		out_far = vec4(raw * 0.25, 1.0);
		return;
	}

	// Floating-point grid position for bilinear R_0 interpolation.
	// At scale=1.0 this degenerates to nearest-neighbor (frac = 0).
	vec2 grid_f = (vec2(canvas_pos) + 0.5) * vec2(u_work_w, u_work_h) / vec2(u_canvas_w, u_canvas_h) - 0.5;
	ivec2 g0 = ivec2(floor(grid_f));
	vec2 frac = grid_f - vec2(g0);

	// Absorption edges turn bilinear toward the nearest probe.
	ivec2 c00 = clamp(g0, ivec2(0), ivec2(u_work_w - 1, u_work_h - 1));
	ivec2 c11 = clamp(g0 + ivec2(1, 1), ivec2(0), ivec2(u_work_w - 1, u_work_h - 1));
	float t00 = texelFetch(u_source_filtered, c00, 0).a;
	float t10 = texelFetch(u_source_filtered, ivec2(c11.x, c00.y), 0).a;
	float t01 = texelFetch(u_source_filtered, ivec2(c00.x, c11.y), 0).a;
	float t11 = texelFetch(u_source_filtered, c11, 0).a;
	float mn = min(min(t00, t10), min(t01, t11));
	float mx = max(max(t00, t10), max(t01, t11));
	float edge = smoothstep(0.0, UPSCALE_THRESHOLD, mx - mn);

	// Sample center pixel once for all quadrants (step 0 of trace_cminus1).
	vec4 center = stored_step(canvas_pos, 1.0);
	float center_T = max(center.a, 1e-6);
	float center_tau = -log(center_T);
	vec3 center_rad = center.rgb;

	// Split lighting: every quadrant's luminance from one image, blended toward
	// the nearest work probe at absorption edges like color.
	vec4 luminance = vec4(0.0);
	vec2 grid_c = grid_f;
	if (u_split != 0) {
		ivec2 last = ivec2(u_work_w - 1, u_work_h - 1);
		vec2 uv = (clamp(grid_f, vec2(0.0), vec2(last)) + 0.5) / vec2(textureSize(u_luminance, 0));
		luminance = textureLod(u_luminance, uv, 0.0);
		if (edge > 0.0) {
			ivec2 nearest = clamp(g0 + ivec2(step(vec2(0.5), frac)), ivec2(0), last);
			luminance = mix(luminance, texelFetch(u_luminance, nearest, 0), edge);
		}
		grid_c = color_grid(canvas_pos);
	}

	vec3 total = vec3(0.0);
	vec3 total_raw = vec3(0.0);
	float sky = 0.0;
	for (int rot = 0; rot < 4; rot++) {
		ivec2 s0 = g0;
		vec4 far;
		if (u_split != 0) {
			// Color supplies only proportions; bilinear on its coarser grid.
			far = F_filtered(rot, grid_c);
		} else {
			// Edge: blend bilinear -> nearest at absorption edges.
			far = F_filtered(rot, grid_f);
			if (edge > 0.0) {
				far = mix(far, F_load(rot, s0 + ivec2(step(vec2(0.5), frac))), edge);
			}
		}

		Interval cminus1 = trace_cminus1(canvas_pos, rot, center_rad, center_tau, center_T);
		float trn = exp(-cminus1.tau);
		vec3 r0 = far.rgb;
		vec3 quadrant = cminus1.rad + trn * r0;
		if (u_split != 0) {
			quadrant = with_luminance(quadrant, dot(cminus1.rad, HRC_LUMA) + trn * luminance[rot]);
			r0 = with_luminance(r0, luminance[rot]);
		}
		total += quadrant;
		sky += trn * far.a;
		output_quadrant(rot, quadrant, trn * far.a);
		total_raw += r0;
	}

	out_light = vec4(total * 0.25, sky * 0.5);
	out_far = vec4(total_raw * 0.25, 1.0);
}
