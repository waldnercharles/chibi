// Separable dilation at tile resolution includes the complete shadow reach.
layout(set = 2, binding = 0) uniform sampler2D u_dirty;
layout(set = 3, binding = 0) uniform uniform_block
{
    ivec2 u_axis;
    int u_radius;
};
layout(location = 0) out vec4 out_dirty;
void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(u_dirty, 0);
    out_dirty = vec4(0);
    for (int i = -u_radius; i <= u_radius; i++) {
        ivec2 q = p + u_axis * i;
        if (any(lessThan(q, ivec2(0))) || any(greaterThanEqual(q, size))) continue;
        if (texelFetch(u_dirty, q, 0).r > 0.5) { out_dirty = vec4(1); return; }
    }
}
