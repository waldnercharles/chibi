#include "render/optics.h"

#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    float bounds[4], geometry[4], points[16], optical[4], surface[4];
} optical_shape;
typedef struct
{
    float pose[4], power[4], aperture[4];
} optical_light;
_Static_assert(sizeof(optical_shape) == 128, "optical shader layout");
_Static_assert(sizeof(optical_light) == 48, "light shader layout");

typedef struct
{
    float bounds[4];
    int skip, shape, padding[2];
} optical_node;
_Static_assert(sizeof(optical_node) == 32, "optical BVH layout");

struct render_optics
{
    CF_ComputeShader clear, trace, resolve;
    CF_Material material;
    CF_StorageBuffer shapes, lights, nodes, accumulator;
    CF_Texture direct, source, surface, black;
    int width, height, shape_capacity, light_capacity, node_capacity;
    optical_shape *geometry, *previous;
    optical_node *tree;
    int *indices;
    optical_light *emitters;
    char error[256];
};

render_optics *render_optics_new()
{
    render_optics *r = xcalloc(1, sizeof *r);
    r->clear = cf_make_compute_shader("optics/clear.shd");
    r->trace = cf_make_compute_shader("optics/trace.shd");
    r->resolve = cf_make_compute_shader("optics/resolve.shd");
    r->material = cf_make_material();
    CF_TextureParams p = cf_texture_defaults(1, 1);
    p.pixel_format = CF_PIXEL_FORMAT_R32_FLOAT;
    p.stream = true;
    r->black = cf_make_texture(p);
    float zero = 0;
    if (r->black.id) {
        cf_texture_update(r->black, &zero, sizeof zero);
    }
    return r;
}

void render_optics_free(render_optics *r)
{
    if (!r) {
        return;
    }
    CF_ComputeShader shaders[] = { r->clear, r->trace, r->resolve };
    CF_Texture textures[] = { r->direct, r->source, r->surface, r->black };
    CF_StorageBuffer buffers[] = { r->shapes, r->lights, r->nodes, r->accumulator };
    for (int i = 0; i < 3; i++) {
        if (shaders[i].id) {
            cf_destroy_compute_shader(shaders[i]);
        }
    }
    for (int i = 0; i < 4; i++) {
        if (textures[i].id) {
            cf_destroy_texture(textures[i]);
        }
    }
    for (int i = 0; i < 4; i++) {
        if (buffers[i].id) {
            cf_destroy_storage_buffer(buffers[i]);
        }
    }
    if (r->material.id) {
        cf_destroy_material(r->material);
    }
    afree(r->geometry);
    afree(r->previous);
    afree(r->tree);
    afree(r->indices);
    afree(r->emitters);
    free(r);
}

const char *render_optics_error(const render_optics *r)
{
    return r->error;
}
render_optical_field render_optics_field(const render_optics *r)
{
    return (render_optical_field){ r->direct, r->source, r->surface };
}

static bool fail(render_optics *r, const char *error)
{
    snprintf(r->error, sizeof r->error, "%s", error);
    return false;
}

static CF_StorageBuffer buffer(int bytes, bool writable)
{
    CF_StorageBufferParams p = cf_storage_buffer_defaults(bytes);
    p.compute_readable = true;
    p.compute_writable = writable;
    return cf_make_storage_buffer(p);
}

static bool upload(CF_StorageBuffer *b, int *capacity, const void *data, int bytes)
{
    if (bytes > *capacity || !b->id) {
        if (b->id) {
            cf_destroy_storage_buffer(*b);
        }
        *capacity = bytes > 128 ? bytes : 128;
        *b = buffer(*capacity, false);
    }
    if (!b->id) {
        return false;
    }
    if (bytes) {
        cf_update_storage_buffer(*b, data, bytes);
    }
    return true;
}

static float center(render_optics *r, int index, int axis)
{
    const float *b = r->geometry[r->indices[index]].bounds;
    return b[axis] + b[axis + 2];
}

/* Median partition bounds tree depth even for coincident or long thin shapes. */
static void partition(render_optics *r, int begin, int end, int middle, int axis)
{
    int lo = begin, hi = end - 1;
    while (lo < hi) {
        float pivot = center(r, (lo + hi) / 2, axis);
        int a = lo, b = hi;
        while (a <= b) {
            while (center(r, a, axis) < pivot) {
                a++;
            }
            while (center(r, b, axis) > pivot) {
                b--;
            }
            if (a <= b) {
                int value = r->indices[a];
                r->indices[a++] = r->indices[b];
                r->indices[b--] = value;
            }
        }
        if (middle <= b) {
            hi = b;
            continue;
        }
        if (middle >= a) {
            lo = a;
            continue;
        }
        break;
    }
}

static void build_tree(render_optics *r, int begin, int end)
{
    optical_node n = { .shape = -1 };
    memcpy(n.bounds, r->geometry[r->indices[begin]].bounds, sizeof n.bounds);
    for (int i = begin + 1; i < end; i++) {
        const float *b = r->geometry[r->indices[i]].bounds;
        for (int axis = 0; axis < 2; axis++) {
            n.bounds[axis] = fminf(n.bounds[axis], b[axis]);
            n.bounds[axis + 2] = fmaxf(n.bounds[axis + 2], b[axis + 2]);
        }
    }
    int node = acount(r->tree);
    apush(r->tree, n);
    if (end - begin == 1) {
        r->tree[node].shape = r->indices[begin];
        r->tree[node].skip = acount(r->tree);
        return;
    }
    int axis = (n.bounds[3] - n.bounds[1]) > (n.bounds[2] - n.bounds[0]);
    int middle = (begin + end) / 2;
    partition(r, begin, end, middle, axis);
    build_tree(r, begin, middle);
    build_tree(r, middle, end);
    r->tree[node].skip = acount(r->tree);
}

static bool upload_geometry(render_optics *r)
{
    int count = acount(r->geometry), bytes = count * (int)sizeof(optical_shape);
    if (r->nodes.id && acount(r->previous) == count &&
        (!count || !memcmp(r->previous, r->geometry, bytes))) {
        return true;
    }
    asetlen(r->indices, count);
    for (int i = 0; i < count; i++) {
        r->indices[i] = i;
    }
    aclear(r->tree);
    if (count) {
        build_tree(r, 0, count);
    }
    if (!upload(&r->shapes, &r->shape_capacity, r->geometry, bytes) ||
        !upload(&r->nodes, &r->node_capacity, r->tree, acount(r->tree) * (int)sizeof(optical_node))) {
        return false;
    }
    asetlen(r->previous, count);
    if (count) {
        memcpy(r->previous, r->geometry, bytes);
    }
    return true;
}

static bool resize(render_optics *r, int width, int height)
{
    if (r->width == width && r->height == height) {
        return true;
    }
    if (r->accumulator.id) {
        cf_destroy_storage_buffer(r->accumulator);
    }
    r->accumulator = buffer(width * height * 9 * (int)sizeof(unsigned), true);
    CF_Texture *textures[] = { &r->direct, &r->source, &r->surface };
    for (int i = 0; i < 3; i++) {
        if (textures[i]->id) {
            cf_destroy_texture(*textures[i]);
        }
        CF_TextureParams p = cf_texture_defaults(width, height);
        p.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
        p.filter = CF_FILTER_LINEAR;
        p.usage = CF_TEXTURE_USAGE_SAMPLER_BIT |
                  CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
        *textures[i] = cf_make_texture(p);
    }
    if (!r->accumulator.id || !r->direct.id || !r->source.id || !r->surface.id) {
        return false;
    }
    r->width = width;
    r->height = height;
    return true;
}

static void world_point(const render_material_entity *e, float x, float y, float *out)
{
    out[0] = e->pos[0] + e->inv_rot[0] * x + e->inv_rot[2] * y;
    out[1] = -(e->pos[1] + e->inv_rot[1] * x + e->inv_rot[3] * y);
}

static bool append_shape(render_optics *r, const render_material_entity *e)
{
    bool optical = e->optical[0] != 0;
    if (!optical &&
        fmaxf(e->absorption[0], fmaxf(e->absorption[1], e->absorption[2])) <= 0) {
        return true;
    }
    if (e->reserved[1] || e->metaball_group || e->wobble[0] || e->pluck[0]) {
        return fail(r, "Optical transport needs an undeformed analytic footprint; custom SDF and metaball optics are not supported yet");
    }
    optical_shape s = {
        .bounds = { e->pos[0] - e->extent[0],
                    -e->pos[1] - e->extent[1],
                    e->pos[0] + e->extent[0],
                    -e->pos[1] + e->extent[1] },
        .geometry = { e->pos[0], -e->pos[1], e->radius, 0 },
        .optical = { e->optical[0], e->optical[1], e->optical[2], e->optical[3] },
        .surface = { e->optical[4], e->albedo[0], e->albedo[1], e->albedo[2] }
    };
    switch (e->shape_type) {
        case RENDER_SHAPE_CIRCLE: break;
        case RENDER_SHAPE_BOX: {
            float x = e->points[0], y = e->points[1];
            world_point(e, -x, -y, s.points);
            world_point(e, x, -y, s.points + 2);
            world_point(e, x, y, s.points + 4);
            world_point(e, -x, y, s.points + 6);
            s.geometry[3] = 4;
            break;
        }
        case RENDER_SHAPE_LINE:
        case RENDER_SHAPE_CAPSULE:
            s.geometry[3] = -1;
            if (e->shape_type == RENDER_SHAPE_LINE) {
                s.geometry[2] = e->thickness * .5f;
            }
            world_point(e, e->points[0], e->points[1], s.points);
            world_point(e, e->points[2], e->points[3], s.points + 2);
            break;
        case RENDER_SHAPE_POLY_FILL:
            assert(e->point_count >= 3 && e->point_count <= 8);
            s.geometry[3] = e->point_count;
            for (int i = 0; i < (int)e->point_count; i++) {
                world_point(e, e->points[i * 2], e->points[i * 2 + 1], s.points + i * 2);
            }
            break;
        default:
            return fail(r, "Optical transport supports circles, boxes, capsules, lines and filled polygons");
    }
    apush(r->geometry, s);
    return true;
}

bool render_optics_draw(
    render_optics *r,
    const render_material_entity *entities,
    int count,
    const render_optical_light *lights,
    int light_count,
    render_optical_budget budget,
    CF_Texture medium,
    CF_Color scattering,
    render_view_2d view,
    int width,
    int height
)
{
    r->error[0] = 0;
    if (budget.max_size < 16 || budget.max_size > 1024 || budget.rays < 128 ||
        budget.rays > 65536 || budget.events < 0 || budget.events > 4 ||
        light_count < 1 || light_count > 64 || view.width <= 0 || view.height <= 0) {
        return fail(r, "Optics budget requires size 16..1024, rays 128..65536, events 0..4 and 1..64 lights");
    }
    if (!r->trace.id || !r->clear.id || !r->resolve.id || !r->material.id ||
        !r->black.id) {
        return fail(r, "Optical shader or material creation failed");
    }
    float scale = fminf(1, (float)budget.max_size / fmaxf(width, height));
    int w = (int)fmaxf(2, floorf(width * scale)),
        h = (int)fmaxf(2, floorf(height * scale));
    if (!resize(r, w, h)) {
        return fail(r, "Optical field allocation failed");
    }
    aclear(r->geometry);
    aclear(r->emitters);
    for (int i = 0; i < count; i++) {
        if (!append_shape(r, entities + i)) {
            return false;
        }
    }
    float total_power = 0;
    for (int i = 0; i < light_count; i++) {
        const render_optical_light *l = lights + i;
        int begin = i * budget.rays / light_count,
            end = (i + 1) * budget.rays / light_count;
        optical_light source = {
            .pose = { l->position[0], l->position[1], l->direction[0], l->direction[1] },
            .power = { l->color[0] * l->power,
                       l->color[1] * l->power,
                       l->color[2] * l->power,
                       0 },
            .aperture = { l->width, l->spread, begin, end - begin }
        };
        total_power += l->power;
        apush(r->emitters, source);
    }
    if (!upload_geometry(r) ||
        !upload(&r->lights, &r->light_capacity, r->emitters, light_count * (int)sizeof(optical_light))) {
        return fail(r, "Optical scene upload failed");
    }
    float cx = view.width / w, cy = view.height / h, area = cx * cy;
    /* Conservative bound: all source power crosses one cell at every allowed event.
     * Fresnel children sum to parent power, so splitting does not increase this bound. */
    float bound = total_power * (budget.events + 1) *
                  fmaxf(
                      hypotf(cx, cy) / (area * 6.2831853f),
                      fmaxf(1 / (area * 3.14159265f), 1 / (2 * fminf(cx, cy)))
                  );
    float control[] = { 1e9f / fmaxf(bound, 1), view.width / width, fminf(cx, cy) * 1e-4f, 0 };
    float camera[] = { view.min_x, -view.max_y, view.width, view.height };
    int grid[] = { w, h, acount(r->geometry), light_count };
    int limits[] = { budget.rays, budget.events, acount(r->tree), 0 };
    cf_material_set_uniform_cs(r->material, "u_view", camera, CF_UNIFORM_TYPE_FLOAT, 4);
    cf_material_set_uniform_cs(r->material, "u_grid", grid, CF_UNIFORM_TYPE_INT, 4);
    cf_material_set_uniform_cs(r->material, "u_budget", limits, CF_UNIFORM_TYPE_INT, 4);
    cf_material_set_uniform_cs(r->material, "u_control", control, CF_UNIFORM_TYPE_FLOAT, 4);
    cf_material_set_uniform_cs(r->material, "u_scattering", &scattering, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_material_set_texture_cs(r->material, "u_medium", medium.id ? medium : r->black);
    cf_push_gpu_label("Optical paths");
    CF_ComputeDispatch d = cf_compute_dispatch_defaults((w * h * 9 + 255) / 256, 1, 1);
    d.rw_buffers = &r->accumulator;
    d.rw_buffer_count = 1;
    cf_dispatch_compute(r->clear, r->material, d);
    CF_StorageBuffer scene[] = { r->shapes, r->lights, r->nodes };
    d.group_count_x = (budget.rays + 63) / 64;
    d.ro_buffers = scene;
    d.ro_buffer_count = 3;
    cf_dispatch_compute(r->trace, r->material, d);
    d = cf_compute_dispatch_defaults((w + 15) / 16, (h + 15) / 16, 1);
    d.ro_buffers = &r->accumulator;
    d.ro_buffer_count = 1;
    CF_Texture output[] = { r->direct, r->source, r->surface };
    d.rw_textures = output;
    d.rw_texture_count = 3;
    cf_dispatch_compute(r->resolve, r->material, d);
    cf_pop_gpu_label();
    return true;
}
