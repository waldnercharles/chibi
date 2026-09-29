#ifndef CHIBI_RENDER_FULLSCREEN_H
#define CHIBI_RENDER_FULLSCREEN_H

#include "platform/cute.h"

/* One triangle covering the viewport, for fragment passes drawn with
 * cf_draw_elements; hrc/pass.vert reads its in_pos. */
static inline CF_Mesh render_make_pass_mesh()
{
    const float vertices[6] = { -1, -1, 3, -1, -1, 3 };
    CF_VertexAttribute attribute = { .name = "in_pos",
                                     .format = CF_VERTEX_FORMAT_FLOAT2 };
    CF_Mesh mesh = cf_make_mesh(sizeof vertices, &attribute, 1, 2 * (int)sizeof(float));
    cf_mesh_update_vertex_data(mesh, (void *)vertices, 3);
    return mesh;
}

/* Queues a quad covering every pixel of the canvas rendered next, shaded by
 * the pushed shader, with screen_uv spanning that canvas. Cute bins shapes
 * such as cf_draw_quad_fill into screen tiles; one screen-sized shape costs
 * over a millisecond of GPU time at 1080p on Metal. A canvas quad is a plain
 * triangle pair. The shader need not sample image, which binds as the quad's
 * texture and so must not be the destination canvas. */
static inline void render_draw_fullscreen(CF_Canvas image)
{
    cf_draw_push();
    cf_draw_TSR_absolute((CF_V2){ 0 }, (CF_V2){ 1, 1 }, 0);
    cf_draw_projection(cf_ortho_2d(0, 0, 2, 2));
    cf_draw_push_viewport((CF_Rect){ 0, 0, -1, -1 });
    cf_draw_push_scissor((CF_Rect){ 0, 0, -1, -1 });
    cf_draw_canvas(image, (CF_V2){ 0 }, (CF_V2){ 2, 2 });
    cf_draw_pop_scissor();
    cf_draw_pop_viewport();
    cf_draw_pop();
}

#endif
