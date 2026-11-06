#include "render/reflections.h"

#include "render/draw_textures.h"
#include "support/xalloc.h"

#include <stdlib.h>

struct render_reflections
{
    CF_Shader shader;
    CF_Canvas output;
    int width, height;
};

render_reflections *render_reflections_new()
{
    render_reflections *r = xcalloc(1, sizeof *r);
    r->shader = cf_make_draw_shader("hrc/reflection.shd");
    return r;
}

void render_reflections_free(render_reflections *r)
{
    if (!r) {
        return;
    }
    if (r->shader.id) {
        cf_destroy_shader(r->shader);
    }
    if (r->output.id) {
        cf_destroy_canvas(r->output);
    }
    free(r);
}

CF_Canvas render_reflections_draw(
    render_reflections *r,
    CF_Canvas source,
    int width,
    int height,
    render_view_2d view,
    const render_reflection *surfaces,
    int count
)
{
    if (!count) {
        return source;
    }
    if (!r->shader.id) {
        return (CF_Canvas){ 0 };
    }
    if (r->width != width || r->height != height) {
        if (r->output.id) {
            cf_destroy_canvas(r->output);
        }
        r->output = cf_make_canvas(cf_canvas_defaults(width, height));
        r->width = width;
        r->height = height;
    }
    if (!r->output.id) {
        return r->output;
    }
    CF_V2 center = { view.min_x + view.width * .5f, view.max_y - view.height * .5f };
    cf_draw_canvas(source, center, (CF_V2){ view.width, view.height });
    cf_draw_push_shader(r->shader);
    cf_draw_set_texture("u_scene", cf_canvas_get_target(source));
    float rect[] = { view.min_x, view.max_y, view.width, view.height };
    float pixels[] = { 1.f / width, 1.f / height };
    cf_draw_set_uniform("u_view", rect, CF_UNIFORM_TYPE_FLOAT4, 1);
    cf_draw_set_uniform("u_pixel", pixels, CF_UNIFORM_TYPE_FLOAT2, 1);
    for (int i = 0; i < count; i++) {
        render_reflection surface = surfaces[i];
        render_reflection *s = &surface;
        cf_draw_set_uniform("u_plane", s->plane, CF_UNIFORM_TYPE_FLOAT4, 1);
        cf_draw_set_uniform("u_tint", s->tint, CF_UNIFORM_TYPE_FLOAT4, 1);
        float optics[] = { s->strength, s->roughness };
        cf_draw_set_uniform("u_optics", optics, CF_UNIFORM_TYPE_FLOAT2, 1);
        cf_draw_quad_fill2(s->corners[0], s->corners[1], s->corners[2], s->corners[3], 0);
    }
    cf_draw_pop_shader();
    cf_canvas_set_clear_color(r->output, cf_make_color_rgba_f(0, 0, 0, 1));
    cf_push_gpu_label("Planar reflections");
    cf_render_to(r->output, true);
    RENDER_DRAW_UNBIND_TEXTURES("u_scene");
    cf_pop_gpu_label();
    return r->output;
}
