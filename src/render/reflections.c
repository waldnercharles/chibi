#include "render/reflections.h"

#include "render/draw_textures.h"
#include "render/fullscreen.h"
#include "support/xalloc.h"

#include <stdlib.h>

struct render_reflections
{
    CF_Shader planar, liquid;
    CF_Canvas canvas;
    int width, height;
};

render_reflections *render_reflections_new()
{
    render_reflections *r = xcalloc(1, sizeof *r);
    r->planar = cf_make_draw_shader("hrc/reflection.shd");
    r->liquid = cf_make_draw_shader("liquid_reflection.shd");
    return r;
}

void render_reflections_free(render_reflections *r)
{
    if (!r) {
        return;
    }
    if (r->planar.id) {
        cf_destroy_shader(r->planar);
    }
    if (r->liquid.id) {
        cf_destroy_shader(r->liquid);
    }
    if (r->canvas.id) {
        cf_destroy_canvas(r->canvas);
    }
    free(r);
}

CF_Canvas render_reflections_canvas(render_reflections *r, int width, int height)
{
    if (r->width != width || r->height != height) {
        if (r->canvas.id) {
            cf_destroy_canvas(r->canvas);
        }
        r->canvas = cf_make_canvas(cf_canvas_defaults(width, height));
        r->width = width;
        r->height = height;
    }
    return r->canvas;
}

/* The frame, with liquid reflecting what lies above it. */
static void draw_liquid(render_reflections *r, const render_reflection_frame *f)
{
    render_view_2d v = f->view;
    float view[] = { v.min_x, v.max_y, v.width, v.height };
    float crop[] = { v.crop_min_u, v.crop_min_v, v.crop_max_u, v.crop_max_v };
    int width, height;
    cf_canvas_get_size(f->destination, &width, &height);
    cf_draw_push_shader(r->liquid);
    cf_draw_set_texture("normal_tex", f->normal);
    cf_draw_set_uniform("view", view, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("crop_uv", crop, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform_float("strength", f->liquid);
    cf_draw_set_uniform_float("time", f->time);
    cf_draw_set_uniform_float("world_per_px", v.width / (float)width);
    render_draw_fullscreen(f->source);
    RENDER_DRAW_UNBIND_TEXTURES("normal_tex");
    cf_draw_pop_shader();
}

static void draw_planar(render_reflections *r, const render_reflection_frame *f)
{
    int width, height;
    cf_canvas_get_size(f->destination, &width, &height);
    render_view_2d v = f->view;
    cf_draw_push_shader(r->planar);
    cf_draw_set_texture("u_scene", cf_canvas_get_target(f->source));
    float rect[] = { v.min_x, v.max_y, v.width, v.height };
    float pixels[] = { 1.f / width, 1.f / height };
    cf_draw_set_uniform("u_view", rect, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("u_pixel", pixels, CF_UNIFORM_TYPE_FLOAT2, 1);
    for (int i = 0; i < f->count; i++) {
        const render_reflection *s = &f->surfaces[i];
        cf_draw_set_uniform("u_plane", (void *)s->plane, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_draw_set_uniform("u_tint", (void *)s->tint, CF_UNIFORM_TYPE_FLOAT4, 1);
        float optics[] = { s->strength, s->roughness };
        cf_draw_set_uniform("u_optics", optics, CF_UNIFORM_TYPE_FLOAT2, 1);
        cf_draw_quad_fill2(s->corners[0], s->corners[1], s->corners[2], s->corners[3], 0);
    }
    cf_draw_pop_shader();
}

bool render_reflections_draw(render_reflections *r, const render_reflection_frame *f)
{
    if (!r->planar.id || !r->liquid.id || !f->destination.id) {
        return false;
    }
    cf_push_gpu_label("Reflections");
    if (f->liquid > 0) {
        draw_liquid(r, f);
    } else {
        render_view_2d v = f->view;
        cf_draw_canvas(
            f->source,
            (CF_V2){ v.min_x + v.width * .5f, v.max_y - v.height * .5f },
            (CF_V2){ v.width, v.height }
        );
    }
    if (f->count) {
        draw_planar(r, f);
    }
    cf_canvas_set_clear_color(f->destination, cf_make_color_rgba_f(0, 0, 0, 1));
    cf_render_to(f->destination, true);
    RENDER_DRAW_UNBIND_TEXTURES("u_scene");
    cf_pop_gpu_label();
    return true;
}
