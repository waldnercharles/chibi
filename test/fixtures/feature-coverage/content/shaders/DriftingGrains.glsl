// Retained grains accelerate in a crosswind and respawn at their source.
ChibiParticleState advect(ChibiParticleInput p, ChibiParameters u, ChibiParticleState state)
{
    ChibiParticleState s;
    if (p.reset || state.age + p.delta >= u.Lifetime) {
        float phase = p.index * 2.399963 + p.seed;
        s.position = vec3(cos(phase) * 10.0, -20.0, sin(phase) * 10.0);
        s.velocity = vec3(0.0, 18.0 + mod(p.index, 7.0), 0.0);
        s.age = 0.0;
        return s;
    }
    vec3 wind = vec3(3.0 * sin(p.time + state.position.y * 0.03), -6.0, 1.0);
    s.velocity = state.velocity + wind * p.delta;
    s.position = state.position + s.velocity * p.delta;
    s.age = state.age + p.delta;
    return s;
}

ChibiParticle grain(ChibiParticleInput p, ChibiParameters u)
{
    ChibiParticle s = chibi_particle();
    s.position = p.position;
    s.size = 0.8;
    s.color = vec4(0.8, 0.6, 0.3, 1.0);
    s.scale = vec3(1.0, 1.8, 1.0);
    s.rotation = vec3(0.0, p.age, 0.0);
    s.lighting = 1.0;
    return s;
}
