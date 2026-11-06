// Averages square blocks of u_block texels of the surface seed's images; see
// surface_seed.frag. Each tap filters 2x2 texels, so a block has
// (u_block / 2)^2 taps.

layout(set = 2, binding = 0) uniform sampler2D u_q0;
layout(set = 2, binding = 1) uniform sampler2D u_q1;
layout(set = 2, binding = 2) uniform sampler2D u_q2;
layout(set = 2, binding = 3) uniform sampler2D u_q3;

layout(location = 0) out vec4 out_q0;
layout(location = 1) out vec4 out_q1;
layout(location = 2) out vec4 out_q2;
layout(location = 3) out vec4 out_q3;

layout(set = 3, binding = 0) uniform uniform_block
{
	int u_block;
};

void main()
{
	ivec2 p = ivec2(gl_FragCoord.xy);
	int taps = u_block / 2;
	vec2 size = vec2(textureSize(u_q0, 0));
	vec4 q0 = vec4(0), q1 = vec4(0), q2 = vec4(0), q3 = vec4(0);
	for (int y = 0; y < taps; y++) {
		for (int x = 0; x < taps; x++) {
			vec2 uv = (vec2(p * u_block) + vec2(2 * x + 1, 2 * y + 1)) / size;
			q0 += texture(u_q0, uv);
			q1 += texture(u_q1, uv);
			q2 += texture(u_q2, uv);
			q3 += texture(u_q3, uv);
		}
	}
	float area = float(taps * taps);
	out_q0 = q0 / area;
	out_q1 = q1 / area;
	out_q2 = q2 / area;
	out_q3 = q3 / area;
}
