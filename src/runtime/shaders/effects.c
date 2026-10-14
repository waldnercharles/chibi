#include "runtime/shaders/effects.h"

#include "support/array.h"

#include <assert.h>
#include <math.h>
#include <string.h>

static float noise_hash(unsigned x, unsigned y, unsigned z)
{
    uint32_t h = x * 374761393u + y * 668265263u + z * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h >> 8) / 16777215.0f;
}
/* Quintic value noise. Integer hashing agrees on CPU/GPU; a 4096-cell period
 * bounds conversions even for large finite coordinates and negative cells. */
float shader_noise(const float *position, int width, float scale)
{
    unsigned cell[3] = { 0 };
    float t[3] = { 0 };
    for (int j = 0; j < width; j++) {
        float p = position[j] * scale;
        if (!isfinite(p)) {
            return 0;
        }
        float grid = floorf(p), f = p - grid;
        cell[j] = (unsigned)(grid - floorf(grid / 4096.0f) * 4096.0f);
        t[j] = f * f * f * (f * (f * 6 - 15) + 10);
    }
    float corners[8];
    for (int i = 0; i < 8; i++) {
        corners[i] = noise_hash(
            (cell[0] + (unsigned)(i & 1)) & 4095u,
            (cell[1] + (unsigned)((i >> 1) & 1)) & 4095u,
            (cell[2] + (unsigned)((i >> 2) & 1)) & 4095u
        );
    }
    for (int axis = 0; axis < 3; axis++) {
        for (int i = 0; i < (4 >> axis); i++) {
            float a = corners[i * 2], b = corners[i * 2 + 1];
            corners[i] = a + (b - a) * t[axis];
        }
    }
    return corners[0];
}
static float clamp01(float value)
{
    return fmaxf(0, fminf(1, value));
}

static void normalize3(const float *v, float out[3])
{
    float scale = fmaxf(fabsf(v[0]), fmaxf(fabsf(v[1]), fabsf(v[2])));
    float length = 0;
    for (int i = 0; i < 3; i++) {
        out[i] = scale > 0 ? v[i] / scale : 0;
        length += out[i] * out[i];
    }
    if (length > 0) {
        for (int i = 0; i < 3; i++) {
            out[i] /= sqrtf(length);
        }
    }
}

static void mapping(
    const shader_node *n,
    const float *p,
    const float *offset,
    const float *rotation,
    const float *scale,
    float out[4]
)
{
    const float radians = 0.017453292519943295f;
    float v[3] = { p[0] * scale[0], p[1] * scale[1], n->width == 3 ? p[2] * scale[2] : 0 };
    for (int axis = n->width == 2 ? 2 : 0; axis < 3; axis++) {
        float angle = (n->width == 2 ? rotation[0] : rotation[axis]) * radians;
        float c = cosf(angle), s = sinf(angle);
        int a = (axis + 1) % 3, b = (axis + 2) % 3;
        float x = v[a], y = v[b];
        v[a] = c * x - s * y;
        v[b] = s * x + c * y;
    }
    for (int i = 0; i < n->width; i++) {
        out[i] = v[i] + offset[i];
    }
}

static float fractal_noise(const float *p, int width, float scale, float detail, float roughness)
{
    detail = fmaxf(1, fminf(8, detail));
    roughness = clamp01(roughness);
    float sum = 0, weight = 0, amplitude = 1;
    for (int i = 0; i < 8 && i < detail; i++) {
        float amount = amplitude * clamp01(detail - i);
        sum += shader_noise(p, width, scale) * amount;
        weight += amount;
        scale *= 2;
        amplitude *= roughness;
    }
    return sum / weight;
}

static void voronoi(const float *position, int width, float scale, float randomness, float out[4])
{
    float f[3] = { 0 };
    unsigned cell[3] = { 0 };
    for (int i = 0; i < width; i++) {
        float p = position[i] * scale;
        if (!isfinite(p)) {
            return;
        }
        float grid = floorf(p);
        f[i] = p - grid;
        cell[i] = (unsigned)(grid - floorf(grid / 4096.0f) * 4096.0f);
    }
    randomness = clamp01(randomness);
    float nearest = 4;
    for (int z = width == 3 ? -2 : 0; z <= (width == 3 ? 2 : 0); z++) {
        for (int y = -2; y <= 2; y++) {
            for (int x = -2; x <= 2; x++) {
                int offset[3] = { x, y, z };
                float lower = 0;
                for (int i = 0; i < width; i++) {
                    float distance = fmaxf(0, fmaxf(offset[i] - f[i], f[i] - offset[i] - 1));
                    lower += distance * distance;
                }
                if (lower >= nearest) {
                    continue;
                }
                unsigned cx = (cell[0] + (unsigned)x) & 4095u;
                unsigned cy = (cell[1] + (unsigned)y) & 4095u;
                unsigned cz = (cell[2] + (unsigned)z) & 4095u;
                float color[3] = { noise_hash(cx, cy, cz),
                                   noise_hash((cx + 149u) & 4095u, cy, cz),
                                   noise_hash(cx, (cy + 311u) & 4095u, cz) };
                float distance = 0;
                for (int i = 0; i < width; i++) {
                    float delta = offset[i] + .5f +
                                  (color[i] - .5f) * randomness - f[i];
                    distance += delta * delta;
                }
                if (distance >= nearest) {
                    continue;
                }
                nearest = distance;
                for (int i = 0; i < 3; i++) {
                    out[i + 1] = color[i];
                }
            }
        }
    }
    out[0] = sqrtf(nearest);
}

bool shader_ramp_valid(const shader_ramp_stop *stops, int count)
{
    if (!stops || count < 2 || count > SHADER_RAMP_MAX) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (!isfinite(stops[i].position) || stops[i].position < 0 ||
            stops[i].position > 1 ||
            (i && stops[i].position <= stops[i - 1].position)) {
            return false;
        }
        for (int j = 0; j < 4; j++) {
            if (!isfinite(stops[i].color[j])) {
                return false;
            }
        }
    }
    return true;
}

void shader_effect_eval(
    const shader_graph *g,
    const shader_node *n,
    const float (*values)[4],
    float out[4]
)
{
    const float *a = values[n->input[0]], *b = values[n->input[1]];
    const float *c = shader_op_inputs(n->op) > 2 ? values[n->input[2]] : NULL;
    const float *d = shader_op_inputs(n->op) > 3 ? values[n->input[3]] : NULL;
    switch (n->op) {
        case SHADER_MAPPING:
        case SHADER_MAPPING3D: mapping(n, a, b, c, d, out); break;
        case SHADER_FRESNEL: {
            float normal[3], view[3];
            normalize3(a, normal);
            normalize3(b, view);
            float dot = 0;
            for (int i = 0; i < 3; i++) {
                dot += normal[i] * view[i];
            }
            float power = fmaxf(0, fminf(64, c[0])), base = clamp01(d[0]);
            out[0] = base + (1 - base) *
                                (power == 0 ? 1 : powf(1 - clamp01(dot), power));
            break;
        }
        case SHADER_REMAP:
            for (int i = 0; i < n->width; i++) {
                float t = b[0] == b[1] ? 0 : (a[i] - b[0]) / (b[1] - b[0]);
                if (d[0] != 0) {
                    t = clamp01(t);
                }
                out[i] = c[0] + (c[1] - c[0]) * t;
            }
            break;
        case SHADER_GRADIENT: {
            int mode = (int)fmaxf(0, fminf(3, b[0]));
            float t = mode == 0 ? a[0]
                    : mode == 1 ? a[1]
                    : mode == 2
                        ? hypotf((a[0] - .5f) * 2, (a[1] - .5f) * 2)
                        : (a[0] == .5f && a[1] == .5f
                               ? 0
                               : atan2f(a[1] - .5f, a[0] - .5f) / 6.283185307179586f + .5f);
            out[0] = t * c[0] + d[0];
            break;
        }
        case SHADER_FRACTAL_NOISE:
            out[0] = fractal_noise(a, g->nodes[n->input[0]].width, b[0], c[0], d[0]);
            break;
        case SHADER_VORONOI:
        case SHADER_VORONOI_COLOR: {
            float result[4] = { 0 };
            voronoi(a, g->nodes[n->input[0]].width, b[0], c[0], result);
            if (n->op == SHADER_VORONOI) {
                out[0] = result[0];
            } else {
                memcpy(out, result + 1, 3 * sizeof(float));
            }
            break;
        }
        case SHADER_COLOR_RAMP:
        case SHADER_COLOR_RAMP_ALPHA: {
            int count = acount(n->ramp), left = 0;
            while (left + 1 < count && a[0] >= n->ramp[left + 1].position) {
                left++;
            }
            int right = left + 1 < count ? left + 1 : left;
            float t = right == left
                        ? 0
                        : clamp01(
                              (a[0] - n->ramp[left].position) /
                              (n->ramp[right].position - n->ramp[left].position)
                          );
            int mode = (int)fmaxf(0, fminf(2, b[0]));
            if (mode == 1) {
                t = 0;
            }
            if (mode == 2) {
                t = t * t * (3 - 2 * t);
            }
            for (int i = 0; i < n->width; i++) {
                int channel = n->op == SHADER_COLOR_RAMP_ALPHA ? 3 : i;
                float low = n->ramp[left].color[channel],
                      high = n->ramp[right].color[channel];
                out[i] = low + (high - low) * t;
            }
            break;
        }
        default: assert(false);
    }
}

const char *shader_effects_glsl()
{
    return "#ifndef SG_EFFECTS_DEFINED\n"
           "#define SG_EFFECTS_DEFINED\n"
           "float sgx_hash(uvec3 cell) {\n"
           " uint h=cell.x*374761393u+cell.y*668265263u+cell.z*2246822519u;\n"
           " h=(h^(h>>13u))*1274126177u; h^=h>>16u;\n"
           " return float(h>>8u)/16777215.0;\n"
           "}\n"
           "float sgx_noise(vec3 p) {\n"
           " if(any(isnan(p))||any(isinf(p))) return 0.0;\n"
           " vec3 grid=floor(p), f=p-grid;\n"
           " uvec3 cell=uvec3(grid-floor(grid/4096.0)*4096.0);\n"
           " vec3 t=f*f*f*(f*(f*6.0-15.0)+10.0);\n"
           " float corners[8];\n"
           " for(int i=0;i<8;i++) {\n"
           "  uvec3 corner=uvec3(uint(i&1),uint((i>>1)&1),uint((i>>2)&1));\n"
           "  corners[i]=sgx_hash((cell+corner)&uvec3(4095u));\n"
           " }\n"
           " for(int axis=0;axis<3;axis++) {\n"
           /* Keep shifts out of comparisons: Cute's WGSL printer drops their
            * parentheses, making `i < 4 >> axis` look like a template. */
           "  int count=4>>axis;\n"
           "  for(int i=0;i<count;i++) {\n"
           "   float a=corners[i*2], b=corners[i*2+1]; "
           "corners[i]=a+(b-a)*t[axis];\n"
           "  }\n"
           " }\n"
           " return corners[0];\n"
           "}\n"
           "vec3 sgx_normalize(vec3 v) {\n"
           " float scale=max(abs(v.x),max(abs(v.y),abs(v.z)));\n"
           " if(scale==0.0) return vec3(0);\n"
           " v/=scale; return v/length(v);\n"
           "}\n"
           "vec2 sgx_mapping(vec2 p,vec2 offset,float angle,vec2 scale) {\n"
           " p*=scale; angle*=0.017453292519943295;\n"
           " float c=cos(angle),s=sin(angle);\n"
           " return vec2(c*p.x-s*p.y,s*p.x+c*p.y)+offset;\n"
           "}\n"
           "vec3 sgx_mapping3d(vec3 p,vec3 offset,vec3 rotation,vec3 scale) {\n"
           " p*=scale; rotation*=0.017453292519943295;\n"
           " vec3 c=cos(rotation),s=sin(rotation);\n"
           " p.yz=vec2(c.x*p.y-s.x*p.z,s.x*p.y+c.x*p.z);\n"
           " p.zx=vec2(c.y*p.z-s.y*p.x,s.y*p.z+c.y*p.x);\n"
           " p.xy=vec2(c.z*p.x-s.z*p.y,s.z*p.x+c.z*p.y);\n"
           " return p+offset;\n"
           "}\n"
           "float sgx_fresnel(vec3 normal,vec3 view,float exponent,float base) "
           "{\n"
           " float "
           "nv=clamp(dot(sgx_normalize(normal),sgx_normalize(view)),0.0,1.0);\n"
           " exponent=clamp(exponent,0.0,64.0); base=clamp(base,0.0,1.0);\n"
           " return base+(1.0-base)*(exponent==0.0?1.0:pow(1.0-nv,exponent));\n"
           "}\n"
           "vec4 sgx_remap(vec4 value,vec2 from_range,vec2 to_range,float "
           "bounded) {\n"
           " vec4 "
           "t=from_range.x==from_range.y?vec4(0):(value-from_range.x)/"
           "(from_range.y-from_range.x);\n"
           " if(bounded!=0.0) t=clamp(t,0.0,1.0);\n"
           " return to_range.x+(to_range.y-to_range.x)*t;\n"
           "}\n"
           "float sgx_gradient(vec2 p,float mode,float scale,float offset) {\n"
           " int kind=int(clamp(mode,0.0,3.0)); float t=p.x;\n"
           " if(kind==1) t=p.y;\n"
           " if(kind==2) t=length((p-0.5)*2.0);\n"
           " if(kind==3) "
           "t=all(equal(p,vec2(0.5)))?0.0:atan(p.y-0.5,p.x-0.5)/"
           "6.283185307179586+0.5;\n"
           " return t*scale+offset;\n"
           "}\n"
           "float sgx_fbm(vec3 p,float scale,float detail,float roughness) {\n"
           " detail=clamp(detail,1.0,8.0); "
           "roughness=clamp(roughness,0.0,1.0);\n"
           " float sum=0.0,weight=0.0,amplitude=1.0;\n"
           " for(int i=0;i<8;i++) {\n"
           "  if(float(i)>=detail) break;\n"
           "  float amount=amplitude*clamp(detail-float(i),0.0,1.0);\n"
           "  sum+=sgx_noise(p*scale)*amount; weight+=amount;\n"
           "  scale*=2.0; amplitude*=roughness;\n"
           " }\n"
           " return sum/weight;\n"
           "}\n"
           "vec4 sgx_voronoi(vec3 p,int dimensions,float randomness) {\n"
           " if(any(isnan(p))||any(isinf(p))) return vec4(0);\n"
           " vec3 grid=floor(p),f=p-grid;\n"
           " uvec3 cell=uvec3(grid-floor(grid/4096.0)*4096.0);\n"
           " randomness=clamp(randomness,0.0,1.0);\n"
           " float nearest=4.0; vec3 color=vec3(0);\n"
           " for(int z=-2;z<=2;z++) {\n"
           "  if(dimensions==2&&z!=0) continue;\n"
           "  for(int y=-2;y<=2;y++) for(int x=-2;x<=2;x++) {\n"
           "   vec3 offset=vec3(x,y,z);\n"
           "   vec3 lower=max(max(offset-f,f-offset-1.0),vec3(0));\n"
           "   if(dimensions==2) lower.z=0.0;\n"
           "   if(dot(lower,lower)>=nearest) continue;\n"
           "   uvec3 c=(cell+uvec3(ivec3(x,y,z)))&uvec3(4095u);\n"
           "   vec3 "
           "tint=vec3(sgx_hash(c),sgx_hash((c+uvec3(149,0,0))&uvec3(4095u)),"
           "sgx_hash((c+uvec3(0,311,0))&uvec3(4095u)));\n"
           "   vec3 delta=offset+0.5+(tint-0.5)*randomness-f;\n"
           "   if(dimensions==2) delta.z=0.0;\n"
           "   float distance=dot(delta,delta);\n"
           "   if(distance>=nearest) continue;\n"
           "   nearest=distance; color=tint;\n"
           "  }\n"
           " }\n"
           " return vec4(sqrt(nearest),color);\n"
           "}\n"
           "#endif\n";
}
