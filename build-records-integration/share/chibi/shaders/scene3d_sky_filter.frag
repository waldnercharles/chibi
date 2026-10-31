// Prefilters the sky for one roughness: each texel of a cube face averages the
// sky over the GGX lobe of a surface facing its direction and seen head on.
// Each sample reads the sky's mip that matches its share of the lobe, so few
// samples suffice.
// Reference: https://developer.nvidia.com/gpugems/gpugems3/part-iii-rendering/chapter-20-gpu-based-importance-sampling
layout(location = 0) out vec4 result;
layout(set = 2, binding = 0) uniform samplerCube sky_cube;
layout(set = 3, binding = 0) uniform uniform_block {
    // Roughness, the face's edge and the sky's in texels, and the face.
    vec4 filter_settings;
};

const float PI = 3.141592653589793;
const uint SAMPLES = 64u;

// Direction through face texel position st in [-1, 1], in the GPU cube-map
// layout that runtime/images/sky.c writes; row 0 holds t = -1.
vec3 face_direction(int face, vec2 st)
{
    float s = st.x, t = st.y;
    if (face == 0) return vec3(1.0, -t, -s);
    if (face == 1) return vec3(-1.0, -t, s);
    if (face == 2) return vec3(s, 1.0, t);
    if (face == 3) return vec3(s, -1.0, -t);
    if (face == 4) return vec3(s, -t, 1.0);
    return vec3(-s, -t, -1.0);
}

// The base-2 radical inverse of bits: Hammersley's second coordinate.
float radical_inverse(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

void main()
{
    float roughness = filter_settings.x, size = filter_settings.y, source = filter_settings.z;
    vec3 n = normalize(face_direction(int(filter_settings.w), gl_FragCoord.xy * 2.0 / size - 1.0));
    if (roughness == 0.0) {
        result = vec4(textureLod(sky_cube, n, max(log2(source / size), 0.0)).rgb, 1.0);
        return;
    }
    vec3 up = abs(n.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, n));
    vec3 bitangent = cross(n, tangent);
    // The direct lights' alpha, and the solid angle of one sky texel.
    float alpha = max(roughness * roughness, 0.0025);
    float a2 = alpha * alpha;
    float texel = 4.0 * PI / (6.0 * source * source);
    vec3 sum = vec3(0.0);
    float weight = 0.0;
    for (uint i = 0u; i < SAMPLES; i++) {
        float phi = 2.0 * PI * float(i) / float(SAMPLES), u = radical_inverse(i);
        float nh = sqrt((1.0 - u) / (1.0 + (a2 - 1.0) * u));
        float sh = sqrt(1.0 - nh * nh);
        vec3 h = tangent * (sh * cos(phi)) + bitangent * (sh * sin(phi)) + n * nh;
        vec3 l = 2.0 * nh * h - n;
        float nl = dot(n, l);
        if (nl <= 0.0) continue;
        // With the view along the normal, the light's density is D / 4.
        float d = nh * nh * (a2 - 1.0) + 1.0;
        float density = a2 / (4.0 * PI * d * d);
        float lod = 0.5 * log2(1.0 / (float(SAMPLES) * density * texel)) + 1.0;
        sum += textureLod(sky_cube, l, max(lod, 0.0)).rgb * nl;
        weight += nl;
    }
    result = vec4(sum / weight, 1.0);
}
