// Adapted from Neon-Swarm shaders/hrc/extend.shd; original source comments retained.
// Builds the ray interval hierarchy T_0 .. T_N (Sec 4.1).
//
// Each level doubles ray length by chaining two T_{n-1} intervals end-to-end.
// Stores optical depth. Serial segments add depths; alternative paths average
// transmission (Sec 4.1, Eq 20), then convert back to optical depth.
//
// Level layout: element = probe_x * directions + slot of k, row = y; see
// hrc_slots.shd.

#include "hrc/hrc_pass.shd"

layout(set = 2, binding = 0) uniform sampler2D u_prev;

layout(set = 3, binding = 0) uniform uniform_block
{
	int u_cascade;
	int u_transposed;
	int u_world_w;
	int u_world_h;
};

Interval merge(Interval a, Interval b)
{
	return Interval(
		a.rad + exp(-a.tau) * b.rad,
		a.tau + b.tau
	);
}

float blend_depth(float a, float b)
{
	// -log((exp(-a) + exp(-b)) / 2), without underflow for opaque paths.
	// Equal depths also cover two half-float infinities without inf - inf.
	if (a == b) return a;
	return min(a, b) - log(0.5 + 0.5 * exp(-abs(a - b)));
}

vec2 blend_depth(vec2 a, vec2 b)
{
	return vec2(blend_depth(a.x, b.x), blend_depth(a.y, b.y));
}

Interval T_load_prev(int probe_x, int k, int y)
{
	int prev_n = u_cascade - 1;
	int directions = (1 << prev_n) + 1;
	int num_probes = probe_count(u_world_w, prev_n);

	if (probe_x < 0 || probe_x >= num_probes || k < 0 || k >= directions || y < 0 || y >= u_world_h) {
		return Interval(HRC_LIGHT(0.0), HRC_DEPTH(0.0));
	}

	int element = probe_x * directions + hrc_parity_slot(k, directions);
	return interval_fetch(u_prev, hrc_texel(element, y, u_transposed));
}

int far_y_offset(int k)
{
	int prev_pow2 = 1 << (u_cascade - 1);
	return 2 * k - prev_pow2;
}

Interval extend(int probe_x, int k, int y)
{
	int k_lo = k / 2;
	int k_hi = (k + 1) / 2;
	int prev_probe_x = probe_x * 2;
	int mid_probe_x = prev_probe_x + 1;

	// Candidate A: go k_lo then k_hi
	Interval near_a = T_load_prev(prev_probe_x, k_lo, y);
	int far_y_a = y + far_y_offset(k_lo);
	Interval far_a = T_load_prev(mid_probe_x, k_hi, far_y_a);
	Interval A = merge(near_a, far_a);
	// Even directions use the same two intervals for both candidates.
	if (k_lo == k_hi) return A;

	// Candidate B: go k_hi then k_lo
	Interval near_b = T_load_prev(prev_probe_x, k_hi, y);
	int far_y_b = y + far_y_offset(k_hi);
	Interval far_b = T_load_prev(mid_probe_x, k_lo, far_y_b);
	Interval B = merge(near_b, far_b);

	return Interval(
		(A.rad + B.rad) * 0.5,
		blend_depth(A.tau, B.tau)
	);
}

void main()
{
	int directions = (1 << u_cascade) + 1;
	ivec2 er = hrc_element_row(u_transposed);
	int probe_x = er.x / directions;
	int k = hrc_parity_item(er.x - probe_x * directions, directions);
	out_value = interval_texel(extend(probe_x, k, er.y));
}
