#include "hrc/relief_sample.shd"
layout(location = 0) out vec4 out_relief;
void main()
{
    out_relief = packed_relief(ivec2(gl_FragCoord.xy));
}
