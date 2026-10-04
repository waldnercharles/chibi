// Each canvas pixel's one-pixel near-field step for gather: RGB its light,
// reflected light included, and A its optical depth. Gather reads a pixel's
// step for itself and for its neighbors on the way to each quadrant's probe,
// so computing it once here saves repeating it.

layout(set = 2, binding = 0) uniform sampler2D u_source;
layout(set = 2, binding = 1) uniform sampler2D u_bounce;

#ifdef HRC_PACK_RELIEF
#include "hrc/relief_sample.shd"
layout(location = 0) out vec4 out_relief;
layout(location = 1) out vec4 out_value;
#else
layout(location = 0) out vec4 out_value;
#endif

layout(set = 3, binding = 0) uniform uniform_block
{
	int u_canvas_w;
	int u_canvas_h;
	int u_out_w;
	int u_out_h;
	float u_bounce_intensity;
	vec2 u_history_offset;
	int u_history_valid;
	vec4 u_crop_uv;
};

#include "hrc/near_step.shd"

void main()
{
	ivec2 p = ivec2(gl_FragCoord.xy);
	#ifdef HRC_PACK_RELIEF
	out_relief = packed_relief(p);
	#endif
	vec3 bounce;
	vec4 step = near_step(p, 1.0, bounce);
	out_value = vec4(step.rgb + (1.0 - step.a) * bounce, max(texelFetch(u_source, p, 0).a, 0.0));
}
