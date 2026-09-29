// Averages square blocks of u_block texels of the surface seed's images; see
// surface_seed.frag. Each tap filters 2x2 texels, so a block has
// (u_block / 2)^2 taps.

layout(set = 2, binding = 0) uniform sampler2D u_light;
layout(set = 2, binding = 1) uniform sampler2D u_dirs;

layout(location = 0) out vec4 out_light;
layout(location = 1) out vec4 out_dirs;

layout(set = 3, binding = 0) uniform uniform_block
{
	int u_block;
};

void main()
{
	ivec2 p = ivec2(gl_FragCoord.xy);
	int taps = u_block / 2;
	vec2 size = vec2(textureSize(u_light, 0));
	vec4 light = vec4(0), dirs = vec4(0);
	for (int y = 0; y < taps; y++) {
		for (int x = 0; x < taps; x++) {
			vec2 uv = (vec2(p * u_block) + vec2(2 * x + 1, 2 * y + 1)) / size;
			light += texture(u_light, uv);
			dirs += texture(u_dirs, uv);
		}
	}
	float area = float(taps * taps);
	out_light = light / area;
	out_dirs = dirs / area;
}
