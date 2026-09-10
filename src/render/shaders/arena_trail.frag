layout(location = 0) in vec4 v_color;
layout(location = 1) in vec4 v_edge;
layout(location = 0) out vec4 result;

void main()
{
    float side = 1.0 - abs(v_edge.x);
    vec3 distance = vec3(side, v_edge.y, v_edge.z);
    vec3 coverage = smoothstep(vec3(0), max(fwidth(distance), vec3(0.00001)), distance);
    float alpha = v_color.a * min(coverage.x, min(coverage.y, coverage.z));
    if (alpha <= 0.0) discard;
    result = vec4(clamp(v_color.rgb * (1.0 + v_edge.w), vec3(0), vec3(65504)), alpha);
}
