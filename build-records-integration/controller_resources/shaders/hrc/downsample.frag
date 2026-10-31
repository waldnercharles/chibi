// Averages prefiltered transport inputs over square blocks of u_factor fine
// cells for color transport under a finer luminance transport. The view snaps
// to whole coarse cells, so blocks stay world-aligned; blocks past a
// dimension's end cover what remains.
// Light and transmittance average over a step of the coarse cell's length, in
// canvas pixels, as hrc/source_average.shd describes.

#include "hrc/source_average.shd"

layout(set = 2, binding = 0) uniform sampler2D u_source;
layout(set = 2, binding = 1) uniform sampler2D u_bounce;

layout(location = 0) out vec4 out_source;
layout(location = 1) out vec4 out_bounce;

layout(set = 3, binding = 0) uniform uniform_block
{
	int u_fine_w;
	int u_fine_h;
	int u_factor;
	float u_coarse_step;
};

void main()
{
	ivec2 gid = ivec2(gl_FragCoord.xy);

	ivec2 lo = gid * u_factor;
	ivec2 hi = min(lo + u_factor, ivec2(u_fine_w, u_fine_h));

	vec4 part = vec4(0.0), bounce = vec4(0.0);
	for (int y = lo.y; y < hi.y; y++) {
		for (int x = lo.x; x < hi.x; x++) {
			part += source_step(texelFetch(u_source, ivec2(x, y), 0), u_coarse_step);
			bounce += texelFetch(u_bounce, ivec2(x, y), 0);
		}
	}
	float inv_count = 1.0 / float((hi.x - lo.x) * (hi.y - lo.y));
	out_source = source_resolve(part * inv_count, u_coarse_step);
	out_bounce = bounce * inv_count;
}
