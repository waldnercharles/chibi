// Temporal stabilization of four RGB surface quadrants. Geometry openness is
// current-frame data; only RGB uses reprojected, neighborhood-clamped history.
layout(set = 2, binding = 0) uniform sampler2D u_q0;
layout(set = 2, binding = 1) uniform sampler2D u_q1;
layout(set = 2, binding = 2) uniform sampler2D u_q2;
layout(set = 2, binding = 3) uniform sampler2D u_q3;
layout(set = 2, binding = 4) uniform sampler2D u_prev0;
layout(set = 2, binding = 5) uniform sampler2D u_prev1;
layout(set = 2, binding = 6) uniform sampler2D u_prev2;
layout(set = 2, binding = 7) uniform sampler2D u_prev3;

layout(location = 0) out vec4 out_q0;
layout(location = 1) out vec4 out_q1;
layout(location = 2) out vec4 out_q2;
layout(location = 3) out vec4 out_q3;

layout(set = 3, binding = 0) uniform uniform_block
{
	vec2 u_texel_size;
	vec2 u_history_offset;
	float u_hysteresis;
	int u_history_valid;
};

vec4 current_at(int i, vec2 uv)
{
	if (i == 0) return texture(u_q0, uv);
	if (i == 1) return texture(u_q1, uv);
	if (i == 2) return texture(u_q2, uv);
	return texture(u_q3, uv);
}

vec4 previous_at(int i, vec2 uv)
{
	if (i == 0) return texture(u_prev0, uv);
	if (i == 1) return texture(u_prev1, uv);
	if (i == 2) return texture(u_prev2, uv);
	return texture(u_prev3, uv);
}

void main()
{
	vec2 uv = gl_FragCoord.xy * u_texel_size;
	vec2 history_uv = uv + u_history_offset;
	vec2 inset = u_texel_size * 0.5;
	bool use_history = u_hysteresis < 1.0 && u_history_valid != 0 &&
		all(greaterThanEqual(history_uv, inset)) &&
		all(lessThanEqual(history_uv, vec2(1.0) - inset));
	for (int i = 0; i < 4; i++) {
		vec4 value = current_at(i, uv);
		if (use_history) {
			vec3 lower = value.rgb, upper = value.rgb;
			for (int y = -1; y <= 1; y++) {
				for (int x = -1; x <= 1; x++) {
					vec3 tap = current_at(i, uv + vec2(x, y) * u_texel_size).rgb;
					lower = min(lower, tap);
					upper = max(upper, tap);
				}
			}
			vec3 previous = previous_at(i, history_uv).rgb;
			value.rgb = mix(clamp(previous, lower, upper), value.rgb, u_hysteresis);
		}
		if (i == 0) out_q0 = value;
		else if (i == 1) out_q1 = value;
		else if (i == 2) out_q2 = value;
		else out_q3 = value;
	}
}
