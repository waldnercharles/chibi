vec3 rain_drift(float index, float seed, float time, vec3 wind)
{
    vec3 drift = wind * time;
    float phase = index * 0.73 + seed * 0.17;
    for (int octave = 0; octave < 3; octave++) {
        float frequency = float(octave) + 1.0;
        float wave = sin(phase * frequency + time * (0.4 + frequency * 0.2));
        drift += vec3(wave, 0.0, cos(phase + time * frequency)) / (frequency * 3.0);
    }
    return drift;
}
