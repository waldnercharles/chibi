#include "GlslRainCommon.glsl"

ChibiParticle rain(ChibiParticleInput p, ChibiParameters u)
{
    float column = mod(p.index, 16.0);
    float row = floor(p.index / 16.0);
    float fall = fract(p.index / max(p.count, 1.0) + p.seed * 0.173 - p.time * 0.2);
    float envelope = smoothstep(0.0, 0.08, fall) * (1.0 - smoothstep(0.92, 1.0, fall));

    ChibiParticle drop;
    drop.position = vec3((column - 7.5) * u.Spacing,
                         (fall * 2.0 - 1.0) * 25.0,
                         (row - 3.5) * u.Spacing);
    drop.position += rain_drift(p.index, p.seed, p.time, u.Wind);
    drop.size = u.DropSize * envelope;
    drop.color = vec4(0.35, 0.7, 1.3, 0.85 * envelope);
    drop.scale = vec3(0.8, 1.5, 1.0);
    drop.rotation = vec3(u.Wind.z * 0.1, 0.0, -u.Wind.x * 0.1);
    drop.lighting = 1.0;
    return drop;
}
