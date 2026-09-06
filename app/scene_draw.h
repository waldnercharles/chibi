#ifndef CBLUEPRINT_APP_SCENE_DRAW_H
#define CBLUEPRINT_APP_SCENE_DRAW_H

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    float x;
    float y;
} app_cf_v2;

typedef struct
{
    app_cf_v2 min;
    app_cf_v2 max;
} app_cf_aabb;

typedef struct
{
    float r;
    float g;
    float b;
    float a;
} app_cf_color;

static inline app_cf_v2 app_cf_make_v2(float x, float y)
{
    return (app_cf_v2){ x, y };
}

static inline app_cf_aabb app_cf_make_aabb_pos_w_h(app_cf_v2 position, float width, float height)
{
    app_cf_v2 half = { width * 0.5f, height * 0.5f };
    return (app_cf_aabb){
        { position.x - half.x, position.y - half.y },
        { position.x + half.x, position.y + half.y },
    };
}

static inline app_cf_color app_cf_make_color_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    return (app_cf_color){
        (float)r / 255.0f,
        (float)g / 255.0f,
        (float)b / 255.0f,
        1.0f,
    };
}

static inline app_cf_color app_cf_make_color_rgba(unsigned rgba)
{
    app_cf_color color = app_cf_make_color_rgb(rgba >> 24, rgba >> 16, rgba >> 8);
    color.a = (rgba & 255u) / 255.0f;
    return color;
}

void cf_draw_circle_fill2(app_cf_v2 position, float radius);
void cf_draw_capsule_fill2(app_cf_v2 p0, app_cf_v2 p1, float radius);
void cf_draw_polyline(const app_cf_v2 *points, int count, float thickness, bool loop);
void cf_draw_polygon_fill(const app_cf_v2 *points, int count, float chubbiness);
void cf_draw_push_outline(app_cf_color color, float width);
void cf_draw_push_interior_border(app_cf_color color, float width);
void cf_draw_pop_outline(void);
void cf_draw_push_glow(app_cf_color color, float radius);
void cf_draw_pop_glow(void);

void cf_draw_push_color(app_cf_color color);
app_cf_color cf_draw_pop_color(void);
void cf_draw_quad_fill(app_cf_aabb bounds, float chubbiness);
void cf_draw_quad(app_cf_aabb bounds, float thickness, float chubbiness);

#endif
