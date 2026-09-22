#include "multigrid.h"

#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

enum
{
    MAX_LEVELS = 7
};
enum
{
    CLEAR,
    SOURCE,
    RELAX,
    RESIDUAL,
    RESTRICT,
    CORRECT,
    EVALUATE
};

typedef struct
{
    int width, height, ping;
    CF_Canvas field[2], rhs, residual;
} Level;

struct gi_multigrid
{
    Level levels[MAX_LEVELS];
    int count, directions, angular_transfer;
    size_t bytes;
    CF_Material material;
    CF_ComputeShader shader;
    int grid[4];
};

static CF_Canvas canvas(int width, int height)
{
    CF_CanvasParams p = cf_canvas_defaults(width, height);
    p.target.pixel_format = CF_PIXEL_FORMAT_R32G32B32A32_FLOAT;
    p.target.filter = CF_FILTER_LINEAR;
    p.target.usage |= CF_TEXTURE_USAGE_COMPUTE_STORAGE_WRITE_BIT;
    CF_Canvas c = cf_make_canvas(p);
    assert(c.id);
    return c;
}

gi_multigrid *gi_multigrid_create(int width, int height, int directions, int angular_transfer, bool hierarchy)
{
    gi_multigrid *s = xmalloc(sizeof *s);
    memset(s, 0, sizeof *s);
    s->directions = directions;
    s->angular_transfer = angular_transfer;
    s->material = cf_make_material();
    s->shader = cf_make_compute_shader("multigrid.shd");
    assert(s->shader.id);
    for (;;) {
        Level *l = &s->levels[s->count++];
        l->width = width;
        l->height = height;
        int w = width * 8, h = height * (directions / 8);
        l->field[0] = canvas(w, h);
        l->field[1] = canvas(w, h);
        l->rhs = canvas(w, h);
        if (hierarchy) {
            l->residual = canvas(w, h);
        }
        s->bytes += (size_t)w * h * 16 * (hierarchy ? 4 : 3);
        if (!hierarchy || width <= 16 || s->count == MAX_LEVELS) {
            break;
        }
        width = (width + 1) / 2;
        height = (height + 1) / 2;
    }
    return s;
}

static void dispatch(gi_multigrid *s, int level, int operation, CF_Canvas previous, CF_Canvas rhs, CF_Canvas target)
{
    Level *l = &s->levels[level];
    s->grid[0] = l->width;
    s->grid[1] = l->height;
    cf_material_set_uniform_cs(s->material, "u_grid", s->grid, CF_UNIFORM_TYPE_INT, 4);
    int controls[] = { operation, level == 0, s->angular_transfer, 0 };
    cf_material_set_uniform_cs(s->material, "u_multigrid", controls, CF_UNIFORM_TYPE_INT, 4);
    cf_material_set_texture_cs(s->material, "u_previous", cf_canvas_get_target(previous));
    cf_material_set_texture_cs(s->material, "u_rhs", cf_canvas_get_target(rhs));
    CF_Texture output = cf_canvas_get_target(target);
    CF_ComputeDispatch
        d = cf_compute_dispatch_defaults((l->width + 7) / 8, (l->height + 7) / 8, s->directions);
    d.rw_textures = &output;
    d.rw_texture_count = 1;
    cf_dispatch_compute(s->shader, s->material, d);
}

static void relax(gi_multigrid *s, int level, int steps)
{
    Level *l = &s->levels[level];
    for (int i = 0; i < steps; i++) {
        dispatch(s, level, RELAX, l->field[l->ping], l->rhs, l->field[1 - l->ping]);
        l->ping = 1 - l->ping;
    }
}

static void cycle(gi_multigrid *s, int level, int smooth)
{
    Level *l = &s->levels[level];
    if (level == s->count - 1) {
        relax(s, level, l->width * 4);
        return;
    }
    relax(s, level, smooth);
    dispatch(s, level, RESIDUAL, l->field[l->ping], l->rhs, l->residual);
    Level *coarse = &s->levels[level + 1];
    dispatch(s, level + 1, RESTRICT, l->residual, l->rhs, coarse->rhs);
    coarse->ping = 0;
    dispatch(s, level + 1, CLEAR, coarse->field[1], coarse->rhs, coarse->field[0]);
    cycle(s, level + 1, smooth);
    dispatch(s, level, CORRECT, coarse->field[coarse->ping], l->field[l->ping], l->field[1 - l->ping]);
    l->ping = 1 - l->ping;
    relax(s, level, smooth);
}

CF_Texture gi_multigrid_solve(
    gi_multigrid *s,
    const int *grid,
    const int *budget,
    const float *scene,
    int cycles,
    int smooth
)
{
    memcpy(s->grid, grid, sizeof s->grid);
    int controls[4];
    float fixture[4];
    memcpy(controls, budget, sizeof controls);
    memcpy(fixture, scene, sizeof fixture);
    cf_material_set_uniform_cs(s->material, "u_budget", controls, CF_UNIFORM_TYPE_INT, 4);
    cf_material_set_uniform_cs(s->material, "u_scene", fixture, CF_UNIFORM_TYPE_FLOAT, 4);
    Level *l = &s->levels[0];
    l->ping = 0;
    dispatch(s, 0, CLEAR, l->field[1], l->rhs, l->field[0]);
    dispatch(s, 0, SOURCE, l->field[0], l->field[0], l->rhs);
    // Zero cycles isolates convergence of the same 32-bit fine-grid operator.
    if (!cycles) {
        bool band = fixture[3] < 0;
        relax(s, 0, smooth - (band ? 1 : 0));
        if (band) {
            dispatch(s, 0, EVALUATE, l->field[l->ping], l->rhs, l->field[1 - l->ping]);
            l->ping = 1 - l->ping;
        }
    }
    for (int i = 0; i < cycles; i++) {
        cycle(s, 0, smooth);
    }
    return cf_canvas_get_target(l->field[l->ping]);
}

size_t gi_multigrid_bytes(const gi_multigrid *s)
{
    return s->bytes;
}
int gi_multigrid_levels(const gi_multigrid *s)
{
    return s->count;
}

void gi_multigrid_destroy(gi_multigrid *s)
{
    for (int i = 0; i < s->count; i++) {
        Level *l = &s->levels[i];
        cf_destroy_canvas(l->field[0]);
        cf_destroy_canvas(l->field[1]);
        cf_destroy_canvas(l->rhs);
        if (l->residual.id) {
            cf_destroy_canvas(l->residual);
        }
    }
    cf_destroy_compute_shader(s->shader);
    cf_destroy_material(s->material);
    free(s);
}
