layout(location = 0) in vec4 v_color;
layout(location = 0) out vec4 color;

void main()
{
    if (isnan(v_color.a) || v_color.a <= 0.0) discard;
    color = vec4(clamp(v_color.rgb, vec3(0.0), vec3(65504.0)), clamp(v_color.a, 0.0, 1.0));
}
