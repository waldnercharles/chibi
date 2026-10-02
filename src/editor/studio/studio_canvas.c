/* The Canvas preview: one ui asset alone over an empty world, as Storybook
 * shows a story and Unity's UI Builder its Canvas, on its own frame and game
 * UI host so nothing of the scene shows or hears it. Its events land in a
 * log (Storybook's Actions) instead of reaching behaviors or running
 * commands. With the Studio project open, its ui assets are Studio's own
 * chrome, so the preview binds the data Studio's does (studio_sources.c).
 * Also the game UI errors of whatever the Scene shows, recorded once per
 * change. */
#include "editor/studio/studio_internal.h"
#include "platform/cute.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/xalloc.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Screen sizes in logical pixels, as UMG's screen size and Unity UI Builder's
 * canvas size preview devices: game UI scales with the canvas height, so a
 * size sets the aspect the document lays out in. */
static const studio_canvas_size_info SIZES[STUDIO_CANVAS_SIZE_COUNT] = {
    [STUDIO_CANVAS_FIT] = { "Fit", 0, 0 },
    [STUDIO_CANVAS_PHONE] = { "Phone", 390, 844 },
    [STUDIO_CANVAS_TABLET] = { "Tablet", 820, 1180 },
    [STUDIO_CANVAS_DESKTOP] = { "Desktop", 1920, 1080 },
};

/* Storybook's light and dark backgrounds. */
#define BACKDROP_LIGHT 0xf4f4f5ffu
#define BACKDROP_DARK 0x1c1c1fffu

studio_canvas_size_info studio_canvas_size_of(studio_canvas_size size)
{
    return SIZES[size];
}

/* ---------- UI errors ---------- */

bool studio_ui_error_asset(const char *error, char *out, size_t size)
{
    static const char PREFIX[] = "ui/", SUFFIX[] = UI_MARKUP_EXTENSION;
    const char *end = strncmp(error, PREFIX, sizeof PREFIX - 1)
                        ? NULL
                        : strstr(error, SUFFIX);
    if (!end) {
        return false;
    }
    const char *name = error + sizeof PREFIX - 1;
    snprintf(out, size, "%.*s", (int)(end - name), name);
    return end > name;
}

void studio_ui_error(studio *s, const char *error)
{
    if (!strcmp(error, s->ui_error)) {
        return;
    }
    snprintf(s->ui_error, sizeof s->ui_error, "%s", error);
    if (error[0]) {
        studio_record(s, KIT_TOAST_ERROR, error);
    }
}

/* ---------- Preview ---------- */

static void draw_backdrop(void *user)
{
    const studio_canvas *c = user;
    engine_viewport_camera camera;
    engine_viewport_get_camera(c->viewport, &camera);
    uint32_t color = c->light ? BACKDROP_LIGHT : BACKDROP_DARK;
    cf_draw_push_color(cf_make_color_rgba(color >> 24, color >> 16, color >> 8, color));
    cf_draw_quad_fill(
        cf_make_aabb_pos_w_h(
            cf_v2(camera.center_x, camera.center_y),
            2 * camera.span_width,
            2 * camera.span_height
        ),
        0
    );
    cf_draw_pop_color();
}

/* Shows `asset`, or closes the preview for NULL, leaving the areas as they
   are. Closing keeps the preview until it would draw next, so the header and
   body emitting it this frame hold it. */
static void preview(studio *s, const char *asset)
{
    if (!asset || !asset[0]) {
        if (s->canvas) {
            s->canvas->asset[0] = '\0';
        }
        return;
    }
    if (!s->canvas) {
        s->canvas = xcalloc(1, sizeof *s->canvas);
        s->canvas->world = ecs_world_new();
        s->canvas->save = game_save_new();
    }
    studio_canvas *c = s->canvas;
    if (strcmp(c->asset, asset)) {
        snprintf(c->asset, sizeof c->asset, "%s", asset);
        asetlen(c->actions, 0);
    }
}

void studio_preview_ui(studio *s, const char *asset)
{
    preview(s, asset);
    if (asset && asset[0]) {
        studio_show_editor(s, STUDIO_EDITOR_SCENE);
    }
}

/* The ui asset a UI document is, in `name`: a .rml file directly in the open
   project's ui folder, as mounts find assets by name; false for a stylesheet
   or another project's asset. */
bool studio_ui_document_asset(const studio *s, const char *path, char *name, size_t size)
{
    char ui[PATH_MAX], folder[PATH_MAX];
    snprintf(ui, sizeof ui, "%s/ui", s->scene->content_root);
    const char *base = content_basename(path);
    size_t length = strlen(base), extension = strlen(UI_MARKUP_EXTENSION);
    if (!realpath(ui, folder) || length <= extension ||
        strcmp(base + length - extension, UI_MARKUP_EXTENSION) ||
        strncmp(path, folder, strlen(folder)) || base != path + strlen(folder) + 1) {
        return false;
    }
    snprintf(name, size, "%.*s", (int)(length - extension), base);
    return true;
}

void studio_canvas_follow(studio *s)
{
    ui_buffer *b = studio_ui_buffer(s);
    char name[sizeof s->followed] = "";
    if ((b && !studio_ui_document_asset(s, b->path, name, sizeof name)) ||
        !strcmp(name, s->followed)) {
        return;
    }
    char before[sizeof s->followed];
    snprintf(before, sizeof before, "%s", s->followed);
    snprintf(s->followed, sizeof s->followed, "%s", name);
    if (name[0]) {
        preview(s, name);
        return;
    }
    /* Another kind of document is active: the Scene shows the scene again,
       unless something else was previewed since. */
    const char *shown = studio_previewed_ui(s);
    if (shown && !strcmp(shown, before)) {
        preview(s, NULL);
    }
}

const char *studio_previewed_ui(const studio *s)
{
    return s->canvas && s->canvas->asset[0] ? s->canvas->asset : NULL;
}

static bool same_action(const studio_canvas_action *a, const studio_canvas_action *b)
{
    return !strcmp(a->event, b->event) && !strcmp(a->element, b->element) &&
           !strcmp(a->value, b->value);
}

/* Logs an activation; a repeat of the last one counts on it. */
static void log_action(studio_canvas *c, const ui_activation *a, double time)
{
    studio_canvas_action action = { .time = time, .count = 1 };
    snprintf(action.event, sizeof action.event, "%s", a->event);
    if (a->id[0]) {
        snprintf(action.element, sizeof action.element, "#%s", a->id);
    }
    if (a->has_value) {
        snprintf(action.value, sizeof action.value, "%g", a->value);
    }
    int count = acount(c->actions);
    if (count && same_action(&c->actions[count - 1], &action)) {
        c->actions[count - 1].count++;
        c->actions[count - 1].time = time;
        return;
    }
    if (count == STUDIO_CANVAS_ACTIONS) {
        memmove(c->actions, c->actions + 1, (size_t)(count - 1) * sizeof *c->actions);
        asetlen(c->actions, count - 1);
    }
    apush(c->actions, action);
}

static engine_viewport *canvas_viewport(studio *s, studio_canvas *c)
{
    engine_viewport *viewport = engine_viewport_new(1, 1);
    assert(viewport);
    engine_viewport_set_render_callback(viewport, draw_backdrop, c);
    yyjson_mut_doc *data = s->own_project ? studio_sources_new() : NULL;
    engine_viewport_adopt_ui(viewport, render_ui_new(s->scene->content_root, data));
    return viewport;
}

bool studio_canvas_draw(studio *s, engine_viewport_host_input *input)
{
    if (!studio_previewed_ui(s)) {
        studio_canvas_free(s);
        return false;
    }
    studio_canvas *c = s->canvas;
    if (!c->viewport) {
        c->viewport = canvas_viewport(s, c);
    }
    ui_world world = { .ecs = c->world,
                       .page = app_scene_authoring_page(s->scene),
                       .save = c->save };
    engine_viewport_set_ui(c->viewport, &world, s->scene->content_root);
    render_ui *ui = engine_viewport_ui(c->viewport);
    if (s->own_project) {
        studio_sources_show(s, &c->sources, render_ui_data(ui));
    }
    /* The theme is the root's class, as a page's .dark tokens expect. */
    render_ui_set_overlay(ui, c->asset, c->light ? "light" : "dark");
    engine_viewport_set_host_input(c->viewport, input);
    engine_viewport_draw(c->viewport, &(engine_viewport_controls){ .playing = true });
    ui_activation a;
    while (render_ui_take_overlay_activation(ui, &a)) {
        log_action(c, &a, s->time);
    }
    studio_ui_error(s, render_ui_error(ui));
    rml_backend_set_texture(s->backend, "canvas", engine_viewport_texture(c->viewport));
    return true;
}

void studio_canvas_free(studio *s)
{
    studio_canvas *c = s->canvas;
    if (!c) {
        return;
    }
    rml_backend_set_texture(s->backend, "canvas", 0);
    engine_viewport_free(c->viewport);
    studio_sources_free(&c->sources);
    ecs_world_free(c->world);
    game_save_free(c->save);
    afree(c->actions);
    free(c);
    s->canvas = NULL;
}
