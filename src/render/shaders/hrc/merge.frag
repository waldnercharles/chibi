// Adapted from Neon-Swarm shaders/hrc/merge.shd; original source comments retained.
// Builds angular fluence R_n from R_{n+1} (Sec 4, Eq 14-17).
//
// Odd probe x (Eq 14): trace one T_n step outward to reach R_{n+1} probes.
// Even probe x (Eq 15-17): average direct R_{n+1} lookup with traced T_{n+1} path.
//
// Nothing lights a ray from beyond the hierarchy, past R_N or the domain. In
// RGB, R's alpha holds the share of each direction's cone whose rays leave it
// unblocked heading up the world, toward the sky. Canvas rows run down the
// world, so quadrant 3 marches up, quadrant 1 down, and quadrants 0 and 2
// head up in their lower and upper halves of directions.
//
// Level layout:
//   T: element = probe_x * (2^n + 1) + slot of k, row = y
//   R: element = slot * 2^n + i,          row = y; see hrc_parity_slot

#include "hrc/hrc_pass.shd"

layout(set = 3, binding = 0) uniform uniform_block
{
	int u_cascade;
	int u_transposed;
	int u_world_w;
	int u_world_h;
	int u_cascade_count;
	float u_falloff;
	// Levels below this one are traced here rather than stored.
	int u_stored_from;
	int u_rotate;
	int u_work_w;
	int u_work_h;
	float u_step_scale;
};

#include "hrc/hrc_trace.shd"

layout(set = 2, binding = 2) uniform sampler2D u_t_curr;
layout(set = 2, binding = 3) uniform sampler2D u_t_next;
layout(set = 2, binding = 4) uniform sampler2D u_r_prev;
// Row n holds each level-n direction's shares of its sector in its upper (x)
// and lower (y) sub-cones.
layout(set = 2, binding = 5) uniform sampler2D u_weights;

HRC_FLUENCE merge_r(Interval near, HRC_FLUENCE far_rad)
{
	// Distance falloff: attenuate far light by this cascade's interval length.
	// Exponential compounding across levels gives total atten = exp(-falloff * dist).
	float atten = u_falloff > 0.0 ? exp(-u_falloff * float(1 << u_cascade)) : 1.0;
#ifdef HRC_SCALAR
	return near.rad + exp(-near.tau) * far_rad * atten;
#else
	// The sky lies beyond any distance, so falloff leaves its share alone.
	return vec4(near.rad, 0.0) + exp(-near.tau) * far_rad * vec4(vec3(atten), 1.0);
#endif
}

Interval T_load(sampler2D level_tex, int n, int probe_x, int k, int y)
{
	if (n >= u_cascade_count) return Interval(HRC_LIGHT(0.0), HRC_DEPTH(0.0));
	int directions = (1 << n) + 1;
	int num_probes = probe_count(u_world_w, n);
	if (probe_x < 0 || probe_x >= num_probes || k < 0 || k >= directions || y < 0 || y >= u_world_h) {
		return Interval(HRC_LIGHT(0.0), HRC_DEPTH(0.0));
	}
	// WebGL forbids ?: on structs.
	if (n < u_stored_from) {
		if (n == 0) return trace_cell(probe_x, y);
		return trace_interval(n, probe_x, k, y);
	}
	int element = probe_x * directions + hrc_parity_slot(k, directions);
	return interval_fetch(level_tex, hrc_texel(element, y, u_transposed));
}

// What reaches level-n direction i from beyond the hierarchy.
HRC_FLUENCE R_beyond(int n, int i)
{
#ifdef HRC_SCALAR
	return vec2(0.0);
#else
	int mid = 1 << (n - 1);
	bool up = u_rotate == 3 || (u_rotate == 0 && i < mid) || (u_rotate == 2 && i >= mid);
	return vec4(vec3(0.0), up ? 1.0 : 0.0);
#endif
}

HRC_FLUENCE R_load(int probe_x, int i, int y)
{
	int n = u_cascade + 1;
	if (n >= u_cascade_count) return R_beyond(n, i);
	int directions = 1 << n;
	int num_probes = probe_count(u_world_w, n);
	if (probe_x < 0 || probe_x >= num_probes || i < 0 || i >= directions || y < 0 || y >= u_world_h) {
		return R_beyond(n, i);
	}
	int element = hrc_parity_slot(probe_x, num_probes) * directions + i;
	return fluence_fetch(u_r_prev, hrc_texel(element, y, u_transposed));
}

HRC_FLUENCE merge_odd(int probe_x, int i, int y, vec2 w)
{
	int next_probe_x = (probe_x + 1) / 2;
	int pow2n = 1 << u_cascade;
	Interval upper = T_load(u_t_curr, u_cascade, probe_x, i + 1, y);
	// Both level-0 intervals of a probe cross only its cell.
	Interval lower = upper;
	if (u_cascade != 0) lower = T_load(u_t_curr, u_cascade, probe_x, i, y);
	return w.x * merge_r(upper, R_load(next_probe_x, 2 * i + 1, y + 2 * (i + 1) - pow2n)) +
	       w.y * merge_r(lower, R_load(next_probe_x, 2 * i, y + 2 * i - pow2n));
}

HRC_FLUENCE merge_even(int probe_x, int i, int y, vec2 w)
{
	int next_probe_x = probe_x / 2;
	int far_probe_x = next_probe_x + 1;
	int pow2n1 = 1 << (u_cascade + 1);
	HRC_FLUENCE far = w.x * merge_r(T_load(u_t_next, u_cascade + 1, next_probe_x, 2 * (i + 1), y),
	                                R_load(far_probe_x, 2 * i + 1, y + 2 * 2 * (i + 1) - pow2n1)) +
	                  w.y * merge_r(T_load(u_t_next, u_cascade + 1, next_probe_x, 2 * i, y),
	                                R_load(far_probe_x, 2 * i, y + 2 * 2 * i - pow2n1));
	HRC_FLUENCE near = w.x * R_load(next_probe_x, 2 * i + 1, y) + w.y * R_load(next_probe_x, 2 * i, y);
	return (near + far) * 0.5;
}

void main()
{
	int directions = 1 << u_cascade;
	int num_probes = probe_count(u_world_w, u_cascade);
	ivec2 er = hrc_element_row(u_transposed);
	int slot = er.x / directions;
	int i = er.x - slot * directions;
	int probe_x = hrc_parity_item(slot, num_probes);
	vec2 w = texelFetch(u_weights, ivec2(i, u_cascade), 0).xy;
	HRC_FLUENCE light = (probe_x % 2 == 0) ? merge_even(probe_x, i, er.y, w)
	                                       : merge_odd(probe_x, i, er.y, w);
	out_value = fluence_texel(light);
}
