// Separable Gaussian blur of both surface light seed images along one axis.
// Edges repeat their nearest texel.

layout(set = 2, binding = 0) uniform sampler2D u_light;
layout(set = 2, binding = 1) uniform sampler2D u_dirs;

layout(location = 0) out vec4 out_light;
layout(location = 1) out vec4 out_dirs;

layout(set = 3, binding = 0) uniform uniform_block
{
	int u_width;
	int u_height;
	int u_vertical;
	float u_sigma;
};

void main()
{
	ivec2 p = ivec2(gl_FragCoord.xy);
	ivec2 axis = u_vertical != 0 ? ivec2(0, 1) : ivec2(1, 0);
	ivec2 last = ivec2(u_width - 1, u_height - 1);
	int radius = int(ceil(u_sigma * 3.0));
	float falloff = -0.5 / (u_sigma * u_sigma);
	vec4 light = vec4(0), dirs = vec4(0);
	float total = 0.0;
	for (int i = -radius; i <= radius; i++) {
		float weight = exp(float(i * i) * falloff);
		ivec2 q = clamp(p + axis * i, ivec2(0), last);
		light += texelFetch(u_light, q, 0) * weight;
		dirs += texelFetch(u_dirs, q, 0) * weight;
		total += weight;
	}
	out_light = light / total;
	out_dirs = dirs / total;
}
