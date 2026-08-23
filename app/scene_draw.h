#ifndef CBLUEPRINT_APP_SCENE_DRAW_H
#define CBLUEPRINT_APP_SCENE_DRAW_H

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

void cf_draw_push_color(app_cf_color color);
app_cf_color cf_draw_pop_color(void);
void cf_draw_quad_fill(app_cf_aabb bounds, float chubbiness);

#endif
