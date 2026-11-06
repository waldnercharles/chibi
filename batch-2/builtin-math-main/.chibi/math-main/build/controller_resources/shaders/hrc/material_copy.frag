// Copies the material targets; see render_material_textures.

layout(set = 2, binding = 0) uniform sampler2D u_scene;
layout(set = 2, binding = 1) uniform sampler2D u_source;
layout(set = 2, binding = 2) uniform sampler2D u_albedo;
layout(set = 2, binding = 3) uniform sampler2D u_normal;
layout(set = 2, binding = 4) uniform sampler2D u_visibility;
layout(set = 2, binding = 5) uniform sampler2D u_cover;

layout(location = 0) out vec4 out_scene;
layout(location = 1) out vec4 out_source;
layout(location = 2) out vec4 out_albedo;
layout(location = 3) out vec4 out_normal;
layout(location = 4) out vec4 out_visibility;
layout(location = 5) out vec4 out_cover;

void main()
{
	ivec2 pixel = ivec2(gl_FragCoord.xy);
	out_scene = texelFetch(u_scene, pixel, 0);
	out_source = texelFetch(u_source, pixel, 0);
	out_albedo = texelFetch(u_albedo, pixel, 0);
	out_normal = texelFetch(u_normal, pixel, 0);
	out_visibility = texelFetch(u_visibility, pixel, 0);
	out_cover = texelFetch(u_cover, pixel, 0);
}
