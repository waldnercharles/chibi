#include "render/trail_2d.h"

#include "support/array.h"

#include <math.h>
#include <string.h>

/* A capsule over segments [first, last) of one trail, with their mean width
 * and fade. Trail history is in 3D arena axes: x, elevation, gameplay y. */
static void piece(
    const arena_trail_segment *first,
    const arena_trail_segment *last,
    float aa,
    float time,
    render_material_entity **entities
)
{
    CF_V2 from = { first->from.x, -first->from.z },
          to = { last[-1].to.x, -last[-1].to.z };
    float width = 0, fade = 0;
    for (const arena_trail_segment *s = first; s < last; s++) {
        width += s->width / (float)(last - first);
        fade += s->color.w / (float)(last - first);
    }
    float radius = .5f * width;
    if (radius <= 0 || fade <= 0) {
        return;
    }
    CF_V2 mid = cf_mul_v2_f(cf_add_v2(from, to), .5f);
    CF_V2 half = cf_mul_v2_f(cf_sub_v2(to, from), .5f);
    CF_V4 color = first->color;
    float glow = first->emission;
    render_material_entity entity = {
        .inv_rot = { 1, 0, 0, 1 },
        .pos = { mid.x, mid.y },
        .extent = { fabsf(half.x) + radius + aa, fabsf(half.y) + radius + aa },
        .shape_type = RENDER_SHAPE_CAPSULE,
        .radius = radius,
        .aa = aa,
        .points = { -half.x, -half.y, half.x, half.y },
        .scene = { color.x, color.y, color.z, fade },
        .emission = { color.x * glow, color.y * glow, color.z * glow, fade },
        .time = { time },
    };
    apush(*entities, entity);
}

void render_trail_2d_append(
    const arena_trail_segment *segments,
    int count,
    float aa,
    float time,
    render_material_entity **entities
)
{
    for (int start = 0; start < count;) {
        int end = start + 1;
        while (
            end < count &&
            !memcmp(&segments[end].from, &segments[end - 1].to, sizeof segments->to)) {
            end++;
        }
        int pieces = end - start < RENDER_TRAIL_2D_PIECES ? end - start
                                                          : RENDER_TRAIL_2D_PIECES;
        for (int i = 0; i < pieces; i++) {
            piece(
                &segments[start + (end - start) * i / pieces],
                &segments[start + (end - start) * (i + 1) / pieces],
                aa,
                time,
                entities
            );
        }
        start = end;
    }
}
