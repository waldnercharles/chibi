// Adapted from Neon-Swarm shaders/hrc/bounce.shd; original source comments retained.
// Reflected light for the next frame's transport: albedo times this frame's
// received far light. Alpha blending with u_hysteresis as alpha mixes it into
// the previous result in place.

layout(set = 2, binding = 0) uniform sampler2D u_radiance;
layout(set = 2, binding = 1) uniform sampler2D u_albedo;

layout(location = 0) out vec4 out_value;

layout(set = 3, binding = 0) uniform uniform_block
{
	float u_hysteresis;
};

void main()
{
	ivec2 p = ivec2(gl_FragCoord.xy);
	vec2 size = vec2(textureSize(u_radiance, 0));
	ivec2 albedo_p = ivec2((vec2(p) + 0.5) * vec2(textureSize(u_albedo, 0)) / size);
	vec3 bounce = texelFetch(u_albedo, albedo_p, 0).rgb * texelFetch(u_radiance, p, 0).rgb;
	out_value = vec4(bounce, u_hysteresis);
}
