// Surface seeds average each RGB quadrant over open space in square blocks.
// Every target stores light times openness in RGB and the open fraction in A.
//
// Each tap filters a 2x2 pixel group, so a block has (block / 2)^2 taps; the
// blur that follows is several blocks wide. Blurring these images gives, for
// a solid pixel, the light in the open space around it, how far that space
// is, and the directions the light arrives from.

layout(set = 2, binding = 0) uniform sampler2D u_q0;
layout(set = 2, binding = 1) uniform sampler2D u_q1;
layout(set = 2, binding = 2) uniform sampler2D u_q2;
layout(set = 2, binding = 3) uniform sampler2D u_q3;
layout(set = 2, binding = 4) uniform sampler2D u_scene;

layout(location = 0) out vec4 out_q0;
layout(location = 1) out vec4 out_q1;
layout(location = 2) out vec4 out_q2;
layout(location = 3) out vec4 out_q3;

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
	vec4 q0 = vec4(0), q1 = vec4(0), q2 = vec4(0), q3 = vec4(0);
	for (int y = 0; y < taps; y++) {
		for (int x = 0; x < taps; x++) {
			vec2 uv = (vec2(p * u_block) + (vec2(x, y) + 0.5) * spacing) /
			          vec2(u_canvas_w, u_canvas_h);
			float open = 1.0 - clamp(texture(u_scene, uv).a, 0.0, 1.0);
			q0 += vec4(max(texture(u_q0, uv).rgb, vec3(0)) * open, open);
			q1 += vec4(max(texture(u_q1, uv).rgb, vec3(0)) * open, open);
			q2 += vec4(max(texture(u_q2, uv).rgb, vec3(0)) * open, open);
			q3 += vec4(max(texture(u_q3, uv).rgb, vec3(0)) * open, open);
		}
	}
	float area = float(taps * taps);
	out_q0 = q0 / area;
	out_q1 = q1 / area;
	out_q2 = q2 / area;
	out_q3 = q3 / area;
}
