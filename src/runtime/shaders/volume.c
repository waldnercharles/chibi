#include "runtime/shaders/volume.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(const shader_node *n, shader_error *error, const char *message)
{
    if (error) {
        error->node = n->source_id;
        snprintf(error->message, sizeof error->message, "%s", message);
    }
    return false;
}

bool shader_volume_contract(const shader_node *n)
{
    if (n->parameter < 2 || n->parameter > 144 || !isfinite(n->volume_range) ||
        n->volume_range <= 0 || n->volume_range > 1e6f ||
        !isfinite(n->volume_lipschitz) || n->volume_lipschitz < 1 ||
        n->volume_lipschitz > 16) {
        return false;
    }
    for (int i = 0; i < 3; i++) {
        if (!isfinite(n->value[i]) || n->value[i] <= 1e-5f || n->value[i] > 1e6f) {
            return false;
        }
    }
    return true;
}
static int tiles(int n)
{
    return (int)ceil(sqrt(n));
}
static float voxel(const shader_node *n, const shader_image_view *image, int x, int y, int z)
{
    int size = n->parameter, columns = tiles(size);
    int u = (z % columns) * size + x, v = (z / columns) * size + y;
    const unsigned char *p = image->pixels + 4 * (v * image->width + u);
    return ((float)(p[0] * 256 + p[1]) * (2.0f / 65535.0f) - 1.0f) * n->volume_range;
}
bool shader_volume_validate(const shader_node *n, const shader_image_view *image, shader_error *error)
{
    const char *message = NULL;
    int size = n->parameter, columns = shader_volume_contract(n) ? tiles(size) : 1;
    if (!shader_volume_contract(n) || !image->pixels || image->width != columns * size ||
        image->height != ((size + columns - 1) / columns) * size) {
        message = "Sampled SDF atlas dimensions or metadata are invalid";
    }
    float gradient[3] = { 0 };
    for (int z = 0; !message && z < size; z++) {
        for (int y = 0; !message && y < size; y++) {
            for (int x = 0; !message && x < size; x++) {
                float d = voxel(n, image, x, y, z);
                int u = (z % columns) * size + x, v = (z / columns) * size + y;
                const unsigned char *p = image->pixels + 4 * (v * image->width + u);
                if (p[2] != 0 || p[3] != 255) {
                    message = "Sampled SDF must use RG16 data with B=0 and "
                              "A=255";
                }
                if ((!x || !y || !z || x == size - 1 || y == size - 1 || z == size - 1) &&
                    d <= 0) {
                    message = "Sampled SDF needs a strictly positive outer "
                              "border";
                }
                int at[3] = { x, y, z };
                for (int axis = 0; axis < 3; axis++) {
                    if (at[axis] + 1 < size) {
                        at[axis]++;
                        float delta = fabsf(voxel(n, image, at[0], at[1], at[2]) - d) *
                                      (size - 1) / (2 * n->value[axis]);
                        gradient[axis] = fmaxf(gradient[axis], delta);
                        at[axis]--;
                    }
                }
            }
        }
    }
    if (!message && sqrtf(
                        gradient[0] * gradient[0] + gradient[1] * gradient[1] +
                        gradient[2] * gradient[2]
                    ) > n->volume_lipschitz) {
        message = "Sampled SDF gradient exceeds its declared Lipschitz bound";
    }
    return message ? fail(n, error, message) : true;
}
bool shader_volume_eval(
    const shader_node *n,
    const shader_image_view *image,
    const float p[3],
    float *result,
    shader_error *error
)
{
    if (!shader_volume_contract(n)) {
        return fail(n, error, "Sampled SDF metadata is invalid");
    }
    int size = n->parameter, columns = tiles(size);
    if (!image->pixels || image->width != columns * size ||
        image->height != ((size + columns - 1) / columns) * size) {
        return fail(n, error, "Sampled SDF atlas dimensions or pixels are invalid");
    }
    int cell[3];
    float f[3], outside = 0;
    for (int axis = 0; axis < 3; axis++) {
        if (!isfinite(p[axis])) {
            return fail(n, error, "Sampled SDF coordinates are nonfinite");
        }
        float q = fmaxf(-n->value[axis], fminf(n->value[axis], p[axis]));
        float delta = p[axis] - q;
        outside += delta * delta;
        float g = (q / n->value[axis] * .5f + .5f) * (size - 1);
        cell[axis] = (int)fminf(floorf(g), size - 2);
        f[axis] = g - cell[axis];
    }
    float d = 0;
    for (int corner = 0; corner < 8; corner++) {
        float weight = 1;
        int at[3];
        for (int axis = 0; axis < 3; axis++) {
            int bit = (corner >> axis) & 1;
            at[axis] = cell[axis] + bit;
            weight *= bit ? f[axis] : 1 - f[axis];
        }
        d += weight * voxel(n, image, at[0], at[1], at[2]);
    }
    *result = d + sqrtf(outside);
    return isfinite(*result) ||
           fail(n, error, "Sampled SDF evaluated distance is nonfinite");
}
char *shader_volume_glsl(const shader_node *n, const char *prefix, int index)
{
    char *text = xmalloc(4096);
    int size = n->parameter, columns = tiles(size),
        rows = (size + columns - 1) / columns;
    snprintf(
        text,
        4096,
        "float %s_volume_%d(vec3 p) {\n"
        "vec3 half_size=vec3(%.9g,%.9g,%.9g); vec3 "
        "q=clamp(p,-half_size,half_size);\n"
        "vec3 g=(q/half_size*0.5+0.5)*%d.0; ivec3 "
        "cell=ivec3(min(floor(g),vec3(%d.0))); vec3 f=g-vec3(cell); float "
        "d=0.0;\n"
        "for(int k=0;k<8;k++){ ivec3 bit=ivec3(k&1,(k>>1)&1,(k>>2)&1); ivec3 "
        "a=cell+bit;\n"
        "vec2 "
        "uv=(vec2((a.z%%%d)*%d+a.x,(a.z/%d)*%d+a.y)+0.5)/vec2(%d.0,%d.0);\n"
        "vec2 bytes=floor(%s_image_%d(uv).rg*255.0+0.5); float "
        "value=((bytes.x*256.0+bytes.y)*(2.0/65535.0)-1.0)*%.9g;\n"
        "vec3 w=mix(vec3(1.0)-f,f,vec3(bit)); d+=w.x*w.y*w.z*value; } return "
        "d+length(p-q); }\n",
        prefix,
        index,
        n->value[0],
        n->value[1],
        n->value[2],
        size - 1,
        size - 2,
        columns,
        size,
        columns,
        size,
        columns * size,
        rows * size,
        prefix,
        n->image,
        n->volume_range
    );
    return text;
}
float shader_graph_lipschitz(const shader_graph *g)
{
    float bound = 1;
    for (int i = 0; i < acount(g->nodes); i++) {
        if (g->nodes[i].op == SHADER_SAMPLED_SDF) {
            bound = fmaxf(bound, g->nodes[i].volume_lipschitz + 1);
        }
    }
    return bound;
}
void shader_graph_copy_images(shader_graph *to, const shader_graph *from)
{
    for (int i = 0; i < acount(from->images); i++) {
        shader_image_source image = from->images[i];
        image.key = xstrdup(image.key);
        apush(to->images, image);
    }
}
