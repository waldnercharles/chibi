// Conservative 2x height bounds, not filtered optical depth. First level reads
// relief(height, coverage, tiltXY); later levels read(maxHeight,minCover,maxCover,0).
// Allocate each output with ceil(sourceSize/2); absent odd-edge children are empty.
layout(set = 2, binding = 0) uniform sampler2D u_source;
layout(set = 3, binding = 0) uniform uniform_block
{
    vec2 u_world_per_px;
    int u_first_level;
};
layout(location = 0) out vec4 out_bounds;
void main()
{
    ivec2 origin = ivec2(gl_FragCoord.xy) * 2;
    ivec2 size = textureSize(u_source, 0);
    float height = -65504.0, minimum = 1.0, maximum = 0.0;
    for (int y = 0; y < 2; y++) {
        for (int x = 0; x < 2; x++) {
            ivec2 p = origin + ivec2(x, y);
            if (any(greaterThanEqual(p, size))) {
                minimum = 0.0;
                continue;
            }
            vec4 cell = texelFetch(u_source, p, 0);
            float lo = cell.y, hi = u_first_level != 0 ? cell.y : cell.z;
            minimum = min(minimum, lo);
            maximum = max(maximum, hi);
            if (hi < 0.5) continue;
            float bound = cell.x;
            if (u_first_level != 0) {
                // Bound the same local face plane used by the reference horizon
                // over the entire base texel, including negative world heights.
                float nz = sqrt(max(1.0 - dot(cell.zw, cell.zw), 0.0));
                if (nz > 0.05) bound += dot(abs(cell.zw), u_world_per_px) * 0.5 / nz;
            }
            height = max(height, bound);
        }
    }
    out_bounds = vec4(clamp(height, -65504.0, 65504.0), minimum, maximum, 0.0);
}
