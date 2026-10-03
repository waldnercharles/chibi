/* What each kind of asset looks like in its thumbnail:
 * - a shape, filled, as previewShader draws it;
 * - a field, its values and iso-contour, as previewField bakes them;
 * - a ui document, itself on Storybook's backdrop, as the Canvas shows it;
 * - a stylesheet, the colors it declares;
 * - a graph asset, a mini-map of its nodes and wires.
 * Other kinds keep their icon; shapes draw as the shader catalog holds
 * them. */
#include "editor/studio/studio_internal.h"
#include "editor/studio/studio_thumbnails.h"
#include "graph/document/serialize.h"
#include "render/field_image.h"
#include "render/shader_preview.h"
#include "render/ui.h"
#include "runtime/material_field.h"
#include "support/array.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The stage pictures stand on, as Unreal's thumbnails' dark backdrop. */
#define STAGE 0x202126ffu
/* Storybook's light and dark backgrounds, as the Canvas uses. */
#define BACKDROP_LIGHT 0xf4f4f5ffu
#define BACKDROP_DARK 0x1c1c1fffu
/* A field's world extent across its thumbnail. */
#define FIELD_EXTENT 768.0f
#define SWATCH_MAX 12
/* A widget's thumbnail frames its content to fill this much of it. */
#define DOCUMENT_FILL 0.86f
/* The dp a ui document's square screen spans, and the least a widget's
   does. */
#define DOCUMENT_SCREEN 720.0f
#define DOCUMENT_MIN_SCREEN 48.0f

struct studio_thumb_renderers
{
    render_ui *ui;
    ecs_world *ui_world;
    studio_sources sources; /* Studio's own ui assets bind its data, as in the Canvas */
};

int studio_thumb_side(reference_asset_kind kind, int side)
{
    return kind == REFERENCE_ASSET_FIELD ? side / 2 : side;
}

int studio_thumb_cost(reference_asset_kind kind)
{
    switch (kind) {
        case REFERENCE_ASSET_BEHAVIOR:
        case REFERENCE_ASSET_EVENT:
        case REFERENCE_ASSET_LIBRARY:
        case REFERENCE_ASSET_SYSTEM: return 0;
        case REFERENCE_ASSET_UI: return 1;
        case REFERENCE_ASSET_FIELD: return 2;
        case REFERENCE_ASSET_MATERIAL:
        case REFERENCE_ASSET_SHAPE: return 3;
        case REFERENCE_ASSET_PROTOTYPE: return 4;
        default: return 5;
    }
}

static CF_Color color_of(uint32_t rgba)
{
    return cf_make_color_rgba(rgba >> 24, rgba >> 16 & 255, rgba >> 8 & 255, rgba & 255);
}

static bool has_suffix(const char *text, const char *suffix)
{
    size_t length = strlen(text), size = strlen(suffix);
    return length >= size && !strcmp(text + length - size, suffix);
}

/* ---------- Graph assets ---------- */

static graph *largest_graph(const behavior_asset *asset)
{
    graph *largest = asset->graph;
    for (int i = 0; i < acount(asset->graphs); i++) {
        if (acount(asset->graphs[i]->nodes) > acount(largest->nodes)) {
            largest = asset->graphs[i];
        }
    }
    return largest;
}

static bool render_graph(const studio_thumb_job *job, CF_Canvas canvas)
{
    char *text = text_file_read(job->path);
    behavior_asset *asset = text ? behavior_asset_load(text) : NULL;
    free(text);
    if (!asset) {
        return false;
    }
    studio_minimap map;
    float side = (float)job->side;
    studio_minimap_build(largest_graph(asset), side, &map);
    behavior_asset_free(asset);
    bool drawn = acount(map.boxes) > 0;
    if (drawn) {
        cf_draw_push();
        cf_draw_projection(cf_ortho_2d(side / 2, side / 2, side, side));
        studio_minimap_draw(&map, side);
        cf_render_to(canvas, true);
        cf_draw_pop();
    }
    studio_minimap_free(&map);
    return drawn;
}

/* ---------- Stylesheets ---------- */

static int hex_digit(char c)
{
    return isdigit((unsigned char)c)  ? c - '0'
         : isxdigit((unsigned char)c) ? tolower((unsigned char)c) - 'a' + 10
                                      : -1;
}

/* A #rgb, #rgba, #rrggbb or #rrggbbaa color at `at`, as 0xRRGGBBAA; false
 * for anything else, as an id selector. */
static bool hex_color(const char *at, uint32_t *out, int *length)
{
    int n = 0;
    while (hex_digit(at[n]) >= 0) {
        n++;
    }
    if ((n != 3 && n != 4 && n != 6 && n != 8) ||
        isalnum((unsigned char)at[n]) || at[n] == '-' || at[n] == '_') {
        return false;
    }
    uint32_t value = 0;
    for (int i = 0; i < n; i++) {
        int digit = hex_digit(at[i]);
        value = n <= 4 ? value << 8 | (uint32_t)(digit * 17)
                       : value << 4 | (uint32_t)digit;
    }
    bool alpha = n == 4 || n == 8;
    *out = alpha ? value : value << 8 | 0xffu;
    *length = n;
    return true;
}

/* The distinct colors a stylesheet's declarations name, in order. */
static int sheet_colors(const char *text, uint32_t *colors, int max)
{
    int count = 0;
    bool in_value = false;
    for (const char *p = text; *p && count < max; p++) {
        if (*p == ':' || *p == ';' || *p == '{' || *p == '}') {
            in_value = *p == ':';
            continue;
        }
        uint32_t color;
        int length;
        if (*p != '#' || !in_value || !hex_color(p + 1, &color, &length)) {
            continue;
        }
        p += length;
        bool seen = false;
        for (int i = 0; i < count; i++) {
            seen |= colors[i] == color;
        }
        if (!seen) {
            colors[count++] = color;
        }
    }
    return count;
}

/* Up to four swatches a row, centered on the stage. */
static bool render_stylesheet(const studio_thumb_job *job, CF_Canvas canvas)
{
    char *text = text_file_read(job->path);
    uint32_t colors[SWATCH_MAX];
    int count = text ? sheet_colors(text, colors, SWATCH_MAX) : 0;
    free(text);
    if (!count) {
        return false;
    }
    float side = (float)job->side;
    int columns = count < 4 ? count : 4, rows = (count + columns - 1) / columns;
    float cell = side * 0.8f / 4, gap = cell * 0.12f;
    float left = (side - columns * cell) / 2, top = (side + rows * cell) / 2;
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(side / 2, side / 2, side, side));
    for (int i = 0; i < count; i++) {
        float x = left + (float)(i % columns) * cell,
              y = top - (float)(i / columns + 1) * cell;
        CF_Aabb box = { { x + gap, y + gap }, { x + cell - gap, y + cell - gap } };
        cf_draw_push_color(color_of(0x3a3c44ffu));
        cf_draw_box_rounded_fill(
            (CF_Aabb){ { box.min.x - 1, box.min.y - 1 },
                       { box.max.x + 1, box.max.y + 1 } },
            gap
        );
        cf_draw_pop_color();
        cf_draw_push_color(color_of(colors[i]));
        cf_draw_box_rounded_fill(box, gap);
        cf_draw_pop_color();
    }
    cf_render_to(canvas, true);
    cf_draw_pop();
    return true;
}

/* ---------- UI documents ---------- */

/* A square centered on the content, which fills DOCUMENT_FILL of it, and
   never smaller than the canvas. */
static void frame_content(const float bounds[4], float region[4])
{
    float span = fmaxf(bounds[2] - bounds[0], bounds[3] - bounds[1]);
    float half = fmaxf(span / DOCUMENT_FILL, 1) / 2;
    float x = (bounds[0] + bounds[2]) / 2, y = (bounds[1] + bounds[3]) / 2;
    float frame[4] = { x - half, y - half, x + half, y + half };
    memcpy(region, frame, sizeof frame);
}

/* The document on the Canvas's backdrop, as laid out on a 720 dp square
   screen; a smaller one lays out again on a screen of its size, so a widget
   shows large and sharp, and the square centers on its content. */
static bool render_document(studio *s, studio_thumb_renderers *r, const studio_thumb_job *job, CF_Canvas canvas)
{
    if (!r->ui) {
        r->ui = render_ui_new(s->scene->content_root, s->own_project ? studio_sources_new() : NULL);
        r->ui_world = ecs_world_new();
    }
    if (s->own_project) {
        studio_sources_show(s, &r->sources, render_ui_data(r->ui));
    }
    ui_world world = { .ecs = r->ui_world, .page = app_scene_authoring_page(s->scene) };
    render_ui_set_overlay(r->ui, job->name, job->light ? "light" : "dark");
    render_ui_set_reference_height(r->ui, 0);
    render_ui_update(r->ui, &world, job->side, job->side, 0);
    float bounds[4], region[4];
    bool shows = render_ui_overlay_bounds(r->ui, bounds) &&
                 !render_ui_error(r->ui)[0];
    float screen = shows ? fmaxf(bounds[2] - bounds[0], bounds[3] - bounds[1]) *
                               DOCUMENT_SCREEN / DOCUMENT_FILL
                         : DOCUMENT_SCREEN;
    if (shows && screen < DOCUMENT_SCREEN * 0.9f) {
        render_ui_set_reference_height(r->ui, fmaxf(screen, DOCUMENT_MIN_SCREEN));
        render_ui_update(r->ui, &world, job->side, job->side, 0);
        shows = render_ui_overlay_bounds(r->ui, bounds);
    }
    if (shows) {
        frame_content(bounds, region);
        cf_canvas_set_clear_color(canvas, color_of(job->light ? BACKDROP_LIGHT : BACKDROP_DARK));
        cf_render_to(canvas, true);
        render_ui_composite_region(r->ui, canvas, region);
    }
    render_ui_set_overlay(r->ui, job->name, NULL);
    return shows;
}

/* ---------- Fields ---------- */

static bool render_field(studio *s, const studio_thumb_job *job, CF_Canvas canvas)
{
    int side = job->side;
    const material_fields *fields = game_materials_fields(s->scene->live->materials);
    float *values = xmalloc((size_t)side * side * sizeof *values);
    char error[256];
    field_grid grid = { side, side, FIELD_EXTENT / (float)side, 0, 0 };
    bool baked = material_field_bake(fields, job->name, NULL, 0, grid, values, NULL, error, sizeof error);
    if (baked) {
        CF_Pixel *pixels = xmalloc((size_t)side * side * sizeof *pixels);
        render_field_image(values, side, side, grid.cell_size, pixels);
        cf_texture_update(cf_canvas_get_target(canvas), pixels, side * side * (int)sizeof *pixels);
        free(pixels);
    }
    free(values);
    return baked;
}

/* ---------- Shapes ---------- */

static bool render_shape(const studio_thumb_job *job, CF_Canvas canvas)
{
    shader_error error;
    render_shader_preview
        *preview = render_shader_preview_find(job->name, job->side, job->side, &error);
    if (!preview) {
        return false;
    }
    float side = (float)job->side;
    cf_draw_push();
    cf_draw_projection(cf_ortho_2d(0, 0, side, side));
    bool drawn = render_shader_preview_draw(preview, 0, NULL);
    cf_render_to(canvas, true);
    cf_draw_pop();
    render_shader_preview_free(preview);
    return drawn;
}

/* ---------- Dispatch ---------- */

static bool render_still(studio *s, studio_thumb_renderers *r, const studio_thumb_job *job, CF_Canvas canvas)
{
    switch (job->kind) {
        case REFERENCE_ASSET_SHAPE: return render_shape(job, canvas);
        case REFERENCE_ASSET_FIELD: return render_field(s, job, canvas);
        case REFERENCE_ASSET_UI:
            return has_suffix(job->path, UI_STYLE_EXTENSION)
                     ? render_stylesheet(job, canvas)
                     : render_document(s, r, job, canvas);
        case REFERENCE_ASSET_BEHAVIOR:
        case REFERENCE_ASSET_EVENT:
        case REFERENCE_ASSET_LIBRARY:
        case REFERENCE_ASSET_SYSTEM: return render_graph(job, canvas);
        default: return false;
    }
}

studio_thumb_step studio_thumb_render(
    studio *s,
    studio_thumb_renderers **renderers,
    const studio_thumb_job *job,
    CF_Canvas canvas
)
{
    if (!*renderers) {
        *renderers = xcalloc(1, sizeof **renderers);
    }
    cf_canvas_set_clear_color(canvas, color_of(STAGE));
    return render_still(s, *renderers, job, canvas) ? STUDIO_THUMB_DRAWN
                                                    : STUDIO_THUMB_BLANK;
}

void studio_thumb_cancel(studio_thumb_renderers *) {}

void studio_thumb_renderers_free(studio_thumb_renderers *r)
{
    if (!r) {
        return;
    }
    render_ui_free(r->ui);
    studio_sources_free(&r->sources);
    ecs_world_free(r->ui_world);
    free(r);
}
