// Compare geometry only. Changing illumination never invalidates a horizon.
layout(set = 2, binding = 0) uniform sampler2D u_relief;
layout(set = 2, binding = 1) uniform sampler2D u_previous;
layout(set = 3, binding = 0) uniform uniform_block
{
    ivec2 u_offset;
    int u_valid;
};
layout(location = 0) out vec4 out_dirty;
void main()
{
    ivec2 tile = ivec2(gl_FragCoord.xy);
    ivec2 size = textureSize(u_relief, 0);
    ivec2 tiles = (size + 15) / 16;
    out_dirty = vec4(1);
    if (u_valid == 0) return;
    // Reprojection changes both exposed and clipped edges of the domain.
    if (any(notEqual(u_offset, ivec2(0))) &&
        (any(equal(tile, ivec2(0))) || any(equal(tile, tiles - 1)))) return;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            ivec2 p = tile * 16 + ivec2(x, y);
            if (any(greaterThanEqual(p, size))) continue;
            ivec2 previous = p + u_offset;
            if (any(lessThan(previous, ivec2(0))) || any(greaterThanEqual(previous, size))) return;
            if (any(notEqual(texelFetch(u_relief, p, 0), texelFetch(u_previous, previous, 0)))) return;
        }
    }
    out_dirty = vec4(0);
}
