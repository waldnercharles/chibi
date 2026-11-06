// Separable Gaussian blur of the four RGB surface seeds along one axis.
// Edges repeat their nearest texel.

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
	vec4 q0 = vec4(0), q1 = vec4(0), q2 = vec4(0), q3 = vec4(0);
	float total = 0.0;
	for (int i = -radius; i <= radius; i++) {
		float weight = exp(float(i * i) * falloff);
		ivec2 q = clamp(p + axis * i, ivec2(0), last);
		q0 += texelFetch(u_q0, q, 0) * weight;
		q1 += texelFetch(u_q1, q, 0) * weight;
		q2 += texelFetch(u_q2, q, 0) * weight;
		q3 += texelFetch(u_q3, q, 0) * weight;
		total += weight;
	}
	out_q0 = q0 / total;
	out_q1 = q1 / total;
	out_q2 = q2 / total;
	out_q3 = q3 / total;
}
