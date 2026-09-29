// Surface light seed: averages received light over open space in square
// canvas blocks. Target 0 holds light times openness in RGB and the open
// fraction in A; target 1 holds the per-quadrant arriving luminance times
// openness.
//
// Each tap filters a 2x2 pixel group, so a block has (block / 2)^2 taps; the
// blur that follows is several blocks wide. Blurring these images gives, for
// a solid pixel, the light in the open space around it, how far that space
// is, and the directions the light arrives from.

layout(set = 2, binding = 0) uniform sampler2D u_fluence;
layout(set = 2, binding = 1) uniform sampler2D u_scene;
layout(set = 2, binding = 2) uniform sampler2D u_dirs;

layout(location = 0) out vec4 out_light;
layout(location = 1) out vec4 out_dirs;

layout(set = 3, binding = 0) uniform uniform_block
{
	int u_canvas_w;
	int u_canvas_h;
	int u_block;
};

void main()
{
	ivec2 p = ivec2(gl_FragCoord.xy);
	int taps = max(u_block / 2, 1);
	float spacing = float(u_block) / float(taps);
	vec4 sum = vec4(0), dirs = vec4(0);
	for (int y = 0; y < taps; y++) {
		for (int x = 0; x < taps; x++) {
			vec2 uv = (vec2(p * u_block) + (vec2(x, y) + 0.5) * spacing) /
			          vec2(u_canvas_w, u_canvas_h);
			float open = 1.0 - clamp(texture(u_scene, uv).a, 0.0, 1.0);
			vec3 light = max(texture(u_fluence, uv).rgb, vec3(0));
			sum += vec4(light * open, open);
			dirs += max(texture(u_dirs, uv), vec4(0)) * open;
		}
	}
	float area = float(taps * taps);
	out_light = sum / area;
	out_dirs = dirs / area;
}
