// Adapted from Neon-Swarm shaders/hrc/blur.shd; original source comments retained.
// 3x3 edge-aware Gaussian blur for gather output, blended with the previous
// frame's result. Weights: [1 2 1]^2 / 16. Skips taps where transmittance
// differs from center by more than u_threshold (hard edge detection). The sky
// share in alpha filters with the light.

layout(set = 2, binding = 0) uniform sampler2D u_in_radiance;
// A holds optical depth; see render_material_textures.
layout(set = 2, binding = 1) uniform sampler2D u_source;
layout(set = 2, binding = 2) uniform sampler2D u_prev_radiance;

layout(location = 0) out vec4 out_value;

layout(set = 3, binding = 0) uniform uniform_block
{
	vec2 u_texel_size;
	float u_threshold;
	float u_hysteresis;
	vec2 u_history_offset;
	int u_history_valid;
};

vec4 input_radiance(vec2 uv)
{
	return texture(u_in_radiance, uv);
}

void main()
{
	vec2 uv = gl_FragCoord.xy * u_texel_size;

	float center_s = texture(u_source, uv).a;

	bool history_active = false;
	vec2 history_uv = uv;
	if (u_hysteresis < 1.0 && u_history_valid != 0) {
		history_uv += u_history_offset;
		vec2 inset = u_texel_size * 0.5;
		history_active = all(greaterThanEqual(history_uv, inset)) &&
		                 all(lessThanEqual(history_uv, vec2(1.0) - inset));
	}

	// 1D weights: [1 2 1] / 4
	const float w1d[3] = float[3](1.0/4.0, 2.0/4.0, 1.0/4.0);

	vec4 sum = vec4(0.0);
	float wsum = 0.0;
	vec4 lower = vec4(0.0), upper = vec4(0.0);
	if (history_active) {
		lower = input_radiance(uv);
		upper = lower;
	}

	for (int dy = -1; dy <= 1; ++dy) {
		for (int dx = -1; dx <= 1; ++dx) {
			vec2 uv_q = uv + vec2(float(dx), float(dy)) * u_texel_size;

			float s = texture(u_source, uv_q).a;
			float edge_w = 1.0 - smoothstep(0.0, u_threshold, abs(s - center_s));

			float w = w1d[dx + 1] * w1d[dy + 1] * edge_w;
			vec4 current = input_radiance(uv_q);
			sum += current * w;
			wsum += w;
			if (history_active && edge_w > 0.0) {
				lower = min(lower, current);
				upper = max(upper, current);
			}
		}
	}

	vec4 out_r = wsum > 0.0 ? sum / wsum : input_radiance(uv);
	if (history_active) {
		vec4 prev = texture(u_prev_radiance, history_uv);
		out_r = mix(clamp(prev, lower, upper), out_r, u_hysteresis);
	}
	out_value = out_r;
}
