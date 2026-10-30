// Unrotates the luminance hierarchy's cascade-0 fluence into one filterable
// world-space image: channel r holds the luminance arriving from quadrant r
// (+x, +y, -x, -y). As in resolve.shd, world pixel p holds the fluence
// arriving at the next probe along each direction. u_pair0 holds quadrants 0
// and 2, u_pair1 quadrants 1 and 3, transposed; see hrc_common.shd.

#include "hrc/hrc_pass.shd"

layout(set = 2, binding = 0) uniform sampler2D u_pair0;
layout(set = 2, binding = 1) uniform sampler2D u_pair1;

layout(set = 3, binding = 0) uniform uniform_block
{
	int u_work_w;
	int u_work_h;
};

ivec2 world_to_rotated(ivec2 p, int rot)
{
	if (rot == 1) return ivec2(p.y, u_work_w - 1 - p.x);
	if (rot == 2) return ivec2(u_work_w - 1 - p.x, u_work_h - 1 - p.y);
	if (rot == 3) return ivec2(u_work_h - 1 - p.y, p.x);
	return p;
}

// Luminance arriving at a rotated probe; probes past the domain receive none.
float probe_luminance(int rot, ivec2 rp, int rot_w, int rot_h)
{
	if (rp.x >= rot_w) return 0.0;
	ivec2 texel = ivec2(hrc_parity_slot(rp.x, rot_w), clamp(rp.y, 0, rot_h - 1));
	vec2 pair = (rot & 1) != 0 ? texelFetch(u_pair1, texel.yx, 0).xy : texelFetch(u_pair0, texel, 0).xy;
	return pair[rot >> 1];
}

void main()
{
	ivec2 p = ivec2(gl_FragCoord.xy);
	vec4 luminance = vec4(0.0);
	for (int rot = 0; rot < 4; rot++) {
		int rot_w = (rot & 1) != 0 ? u_work_h : u_work_w;
		int rot_h = (rot & 1) != 0 ? u_work_w : u_work_h;
		ivec2 rp = world_to_rotated(p, rot) + ivec2(1, 0);
		// The 1-2-1 tent of resolve.frag, against the probe checkerboard.
		for (int dy = -1; dy <= 1; dy++) {
			for (int dx = -1; dx <= 1; dx++) {
				float weight = float((2 - abs(dx)) * (2 - abs(dy)));
				luminance[rot] += weight * probe_luminance(rot, rp + ivec2(dx, dy), rot_w, rot_h);
			}
		}
		luminance[rot] /= 16.0;
	}
	out_value = luminance;
}
