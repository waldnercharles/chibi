// Unrotates one quadrant's cascade-0 fluence into a filterable world-space
// image for gather. World pixel p holds the fluence arriving at the next probe
// along the quadrant's direction, where gather's near interval ends, and in
// alpha the share of the quadrant that sees the sky; see merge.frag.

#include "hrc/hrc_pass.shd"

layout(set = 2, binding = 0) uniform sampler2D u_frustum;

layout(set = 3, binding = 0) uniform uniform_block
{
	int u_rotate;
	int u_work_w;
	int u_work_h;
};

// Fluence arriving at rotated probe rp. Probes past the domain receive no
// light, and the sky across the quadrant's upward directions: all of them
// up the world, half of them sideways and none down.
vec4 probe_fluence(ivec2 rp, int rot_w, int rot_h)
{
	if (rp.x >= rot_w) return vec4(vec3(0.0), u_rotate == 3 ? 1.0 : u_rotate == 1 ? 0.0 : 0.5);
	ivec2 texel = ivec2(hrc_parity_slot(rp.x, rot_w), clamp(rp.y, 0, rot_h - 1));
	return texelFetch(u_frustum, (u_rotate & 1) != 0 ? texel.yx : texel, 0);
}

void main()
{
	ivec2 p = ivec2(gl_FragCoord.xy);
	int rot_w = (u_rotate & 1) != 0 ? u_work_h : u_work_w;
	int rot_h = (u_rotate & 1) != 0 ? u_work_w : u_work_h;
	ivec2 rp;
	if (u_rotate == 1)      rp = ivec2(p.y, u_work_w - 1 - p.x);
	else if (u_rotate == 2) rp = ivec2(u_work_w - 1 - p.x, u_work_h - 1 - p.y);
	else if (u_rotate == 3) rp = ivec2(u_work_h - 1 - p.y, p.x);
	else                    rp = p;
	rp.x += 1;
	// Even and odd probes merge differently, which leaves a checkerboard at
	// the probe grid's highest frequency. A 1-2-1 tent removes exactly it.
	vec4 fluence = vec4(0.0);
	for (int dy = -1; dy <= 1; dy++) {
		for (int dx = -1; dx <= 1; dx++) {
			float weight = float((2 - abs(dx)) * (2 - abs(dy)));
			fluence += weight * probe_fluence(rp + ivec2(dx, dy), rot_w, rot_h);
		}
	}
	out_value = fluence / 16.0;
}
