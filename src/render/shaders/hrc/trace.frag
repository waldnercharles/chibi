// Adapted from Neon-Swarm shaders/hrc/trace.shd; original source comments retained.
// DDA path-traces the lowest level the hierarchy stores; see hrc_trace.shd.

#include "hrc/hrc_pass.shd"

layout(set = 3, binding = 0) uniform uniform_block
{
	int u_cascade;
	int u_rotate;
	int u_transposed;
	int u_work_w;
	int u_work_h;
	float u_step_scale;
};

#include "hrc/hrc_trace.shd"

void main()
{
	int directions = (1 << u_cascade) + 1;
	ivec2 er = hrc_element_row(u_transposed);
	int probe_x = er.x / directions;
	int k = hrc_parity_item(er.x - probe_x * directions, directions);
	out_value = interval_texel(trace_interval(u_cascade, probe_x, k, er.y));
}
