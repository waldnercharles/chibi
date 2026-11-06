// The nearest of each 2x2 block of shadow depths, which the lit pass's blocker
// search reads raw.
layout(set = 2, binding = 0) uniform sampler2D map;
layout(location = 0) out vec4 result;

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy) * 2;
    float near = min(
        min(texelFetch(map, p, 0).r, texelFetch(map, p + ivec2(1, 0), 0).r),
        min(texelFetch(map, p + ivec2(0, 1), 0).r, texelFetch(map, p + ivec2(1, 1), 0).r));
    result = vec4(near, 0.0, 0.0, 1.0);
}
