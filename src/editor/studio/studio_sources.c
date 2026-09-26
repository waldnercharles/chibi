/* The data Studio's authored panels bind beyond their own world's `save`
 * (render/ui.h): `world`, the saved keys of the world the Scene shows, and
 * `theme`, the custom properties of the stylesheets the open scene's root
 * document imports. A panel's edits apply back: a saved key to the save, and
 * a theme value to its file, which restyles everything the page styles. */
#include "editor/studio/studio_internal.h"
#include "runtime/behaviors/live.h"
#include "runtime/scenes/prototype_preview.h"
#include "support/array.h"
#include "support/rcss.h"
#include "support/text_file.h"
#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void studio_sources_add(yyjson_mut_doc *doc)
{
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_val *world = yyjson_mut_obj_add_obj(doc, root, "world");
    yyjson_mut_obj_add_str(doc, world, "title", "");
    yyjson_mut_obj_add_arr(doc, world, "saved");
    yyjson_mut_obj_add_arr(doc, yyjson_mut_obj_add_obj(doc, root, "theme"), "sections");
}

static yyjson_mut_val *variable(ui_data *data, const char *name)
{
    return yyjson_mut_obj_get(yyjson_mut_doc_get_root(data->doc), name);
}

/* ---------- World ---------- */

/* The save of the world the Scene shows, and its title. */
static game_save *scene_save(studio *s, char *title, size_t size)
{
    app_scene_authoring *scene = s->scene;
    scene_prototype_asset *previewed = previewed_prototype(scene);
    if (scene->active_runtime_target == APP_RUNTIME_PREVIEW && previewed) {
        snprintf(title, size, "Preview \xc2\xb7 %s", previewed->prototype->name);
        return game_behavior_save(game_prototype_preview_behaviors(scene->prototype_preview));
    }
    char name[128];
    snprintf(title, size, "Scene \xc2\xb7 %s", studio_scene_title(scene, name, sizeof name));
    return game_behavior_save(scene->live->behaviors);
}

/* Saves the values rows hold that differ from `save`. */
static void save_rows(yyjson_mut_val *rows, game_save *save)
{
    for (size_t i = 0; i < yyjson_mut_arr_size(rows); i++) {
        yyjson_mut_val *row = yyjson_mut_arr_get(rows, i);
        yyjson_mut_val *value = yyjson_mut_obj_get(row, "value");
        if (yyjson_mut_is_str(value)) {
            game_save_set(save, yyjson_mut_get_str(yyjson_mut_obj_get(row, "key")), yyjson_mut_get_str(value));
        }
    }
}

/* Whether the rows name `save`'s keys, in order. */
static bool rows_match(yyjson_mut_val *rows, const game_save *save)
{
    if ((int)yyjson_mut_arr_size(rows) != game_save_count(save)) {
        return false;
    }
    for (int i = 0; i < game_save_count(save); i++) {
        yyjson_mut_val *key = yyjson_mut_obj_get(yyjson_mut_arr_get(rows, (size_t)i), "key");
        if (strcmp(yyjson_mut_get_str(key), game_save_key(save, i)) != 0) {
            return false;
        }
    }
    return true;
}

static void show_rows(yyjson_mut_doc *doc, yyjson_mut_val *rows, const game_save *save)
{
    if (!rows_match(rows, save)) {
        yyjson_mut_arr_clear(rows);
        for (int i = 0; i < game_save_count(save); i++) {
            yyjson_mut_val *row = yyjson_mut_arr_add_obj(doc, rows);
            yyjson_mut_obj_add_strcpy(doc, row, "key", game_save_key(save, i));
            yyjson_mut_obj_add_null(doc, row, "value");
        }
    }
    for (int i = 0; i < game_save_count(save); i++) {
        yyjson_mut_val *row = yyjson_mut_arr_get(rows, (size_t)i);
        ui_data_set_text(
            doc,
            yyjson_mut_obj_get(row, "value"),
            game_save_get(save, game_save_key(save, i))
        );
    }
}

static void sync_world(studio *s, studio_sources *src, ui_data *data, bool wrote)
{
    yyjson_mut_val *world = variable(data, "world");
    yyjson_mut_val *rows = yyjson_mut_obj_get(world, "saved");
    yyjson_mut_val *shown_title = yyjson_mut_obj_get(world, "title");
    char title[192];
    game_save *save = scene_save(s, title, sizeof title);
    if (wrote && save == src->save) {
        save_rows(rows, save);
    }
    bool retitled = strcmp(yyjson_mut_get_str(shown_title), title) != 0;
    if (save == src->save && game_save_revision(save) == src->save_seen && !retitled) {
        return;
    }
    src->save = save;
    src->save_seen = game_save_revision(save);
    ui_data_set_text(data->doc, shown_title, title);
    show_rows(data->doc, rows, save);
    rml_data_model_dirty(data->model, "world");
}

/* ---------- Theme ---------- */

static void free_tokens(studio_sources *src)
{
    for (int i = 0; i < acount(src->tokens); i++) {
        free(src->tokens[i].value);
    }
    afree(src->tokens);
    src->tokens = NULL;
}

static bool starts(const char *text, const char *prefix)
{
    return !strncmp(text, prefix, strlen(prefix));
}

/* Whether a value names a color: a literal, a color function, or a token
 * holding one. */
static bool color_value(const studio_sources *src, const char *value)
{
    static const char *const COLORS[] = { "#", "rgb", "hsl", "color-mix(", "transparent" };
    for (size_t i = 0; i < sizeof COLORS / sizeof *COLORS; i++) {
        if (starts(value, COLORS[i])) {
            return true;
        }
    }
    if (!starts(value, "var(--")) {
        return false;
    }
    size_t length = strcspn(value + 4, ",)");
    for (int i = acount(src->tokens) - 1; i >= 0; i--) {
        const studio_token *t = &src->tokens[i];
        if (strlen(t->name) == length && !strncmp(t->name, value + 4, length)) {
            return t->color;
        }
    }
    return false;
}

/* The file's name without its folder and extension. */
static void file_title(char *out, size_t size, const char *path)
{
    const char *name = strrchr(path, '/');
    name = name ? name + 1 : path;
    snprintf(out, size, "%.*s", (int)strcspn(name, "."), name);
}

/* Lists the theme's files' custom properties, a section per file and rule. */
static void show_theme(studio_sources *src, ui_data *data)
{
    yyjson_mut_doc *doc = data->doc;
    yyjson_mut_val *sections = yyjson_mut_obj_get(variable(data, "theme"), "sections");
    yyjson_mut_arr_clear(sections);
    free_tokens(src);
    yyjson_mut_val *rows = NULL;
    for (int f = 0; f < acount(src->page.imports); f++) {
        const char *path = src->page.imports[f].path;
        char *text = file_watch_exists(&src->page.imports[f]) ? text_file_read(path)
                                                              : NULL;
        rcss_variable *vars = NULL;
        if (text) {
            rcss_variables(text, &vars);
        }
        for (int i = 0; i < acount(vars); i++) {
            const rcss_variable *v = &vars[i];
            if (!i || strcmp(v->selector, vars[i - 1].selector) != 0) {
                char title[64];
                file_title(title, sizeof title, path);
                yyjson_mut_val *section = yyjson_mut_arr_add_obj(doc, sections);
                yyjson_mut_obj_add_strcpy(doc, section, "file", title);
                yyjson_mut_obj_add_strcpy(doc, section, "selector", v->selector);
                rows = yyjson_mut_obj_add_arr(doc, section, "rows");
            }
            studio_token token = { .file = f, .start = v->start, .end = v->end };
            token.value = xmalloc((size_t)(v->end - v->start) + 1);
            snprintf(token.value, (size_t)(v->end - v->start) + 1, "%s", text + v->start);
            snprintf(token.name, sizeof token.name, "%s", v->name);
            token.color = color_value(src, token.value);
            apush(src->tokens, token);
            yyjson_mut_val *row = yyjson_mut_arr_add_obj(doc, rows);
            yyjson_mut_obj_add_strcpy(doc, row, "name", token.name);
            yyjson_mut_obj_add_strcpy(doc, row, "value", token.value);
            yyjson_mut_obj_add_bool(doc, row, "color", token.color);
        }
        afree(vars);
        free(text);
    }
    rml_data_model_dirty(data->model, "theme");
}

/* The value each token's row holds, in token order; NULL past the rows. */
static const char *row_value(ui_data *data, int token)
{
    yyjson_mut_val *sections = yyjson_mut_obj_get(variable(data, "theme"), "sections");
    for (size_t i = 0; i < yyjson_mut_arr_size(sections); i++) {
        yyjson_mut_val *rows = yyjson_mut_obj_get(yyjson_mut_arr_get(sections, i), "rows");
        size_t count = yyjson_mut_arr_size(rows);
        if ((size_t)token < count) {
            yyjson_mut_val *value = yyjson_mut_obj_get(yyjson_mut_arr_get(rows, (size_t)token), "value");
            return yyjson_mut_is_str(value) ? yyjson_mut_get_str(value) : NULL;
        }
        token -= (int)count;
    }
    return NULL;
}

static bool theme_edited(const studio_sources *src, ui_data *data)
{
    for (int i = 0; i < acount(src->tokens); i++) {
        const char *value = row_value(data, i);
        if (value && strcmp(value, src->tokens[i].value) != 0) {
            return true;
        }
    }
    return false;
}

/* Writes file `file` with each edited token's new value in place of its
 * old one; a value the file no longer holds is left alone. */
static void save_file(studio *s, studio_sources *src, ui_data *data, int file)
{
    const char *path = src->page.imports[file].path;
    char *text = text_file_read(path);
    if (!text) {
        return;
    }
    char *out = NULL;
    int at = 0;
    for (int i = 0; i < acount(src->tokens); i++) {
        const studio_token *t = &src->tokens[i];
        const char *value = row_value(data, i);
        size_t old = strlen(t->value);
        if (t->file != file || !value || !strcmp(value, t->value) ||
            (size_t)(t->end - t->start) != old ||
            strncmp(text + t->start, t->value, old) != 0) {
            continue;
        }
        int start = acount(out);
        asetlen(out, start + (t->start - at) + (int)strlen(value));
        memcpy(out + start, text + at, (size_t)(t->start - at));
        memcpy(out + start + (t->start - at), value, strlen(value));
        at = t->end;
    }
    int start = acount(out);
    asetlen(out, start + (int)strlen(text + at) + 1);
    memcpy(out + start, text + at, strlen(text + at) + 1);
    if (write_file_atomic(path, out) != 0) {
        char message[1100];
        snprintf(message, sizeof message, "Could not save %s", path);
        kit_toast(s->kit, 0, &(kit_toast_desc){ .type = KIT_TOAST_ERROR, .title = "Theme", .description = message });
        studio_record(s, KIT_TOAST_ERROR, message);
    }
    afree(out);
    free(text);
}

static void save_theme(studio *s, studio_sources *src, ui_data *data)
{
    for (int f = 0; f < acount(src->page.imports); f++) {
        bool edited = false;
        for (int i = 0; i < acount(src->tokens) && !edited; i++) {
            const char *value = row_value(data, i);
            edited = src->tokens[i].file == f && value &&
                     strcmp(value, src->tokens[i].value) != 0;
        }
        if (edited) {
            save_file(s, src, data, f);
        }
    }
    show_theme(src, data);
}

static void sync_theme(studio *s, studio_sources *src, ui_data *data, bool wrote)
{
    const char *content = s->scene->content_root;
    if (strcmp(src->content_root, content) != 0) {
        ui_page_free(&src->page);
        snprintf(src->content_root, sizeof src->content_root, "%s", content);
    }
    if (wrote && theme_edited(src, data)) {
        save_theme(s, src, data);
    }
    const char *page = app_scene_authoring_page(s->scene);
    if (ui_page_sync(&src->page, content, page ? page : "", s->time)) {
        show_theme(src, data);
    }
}

void studio_sources_sync(studio *s, studio_sources *src, ui_data *data)
{
    uint64_t writes = rml_data_model_writes(data->model);
    bool wrote = writes != src->writes_seen;
    src->writes_seen = writes;
    sync_world(s, src, data, wrote);
    sync_theme(s, src, data, wrote);
}

void studio_sources_free(studio_sources *src)
{
    free_tokens(src);
    ui_page_free(&src->page);
    *src = (studio_sources){ 0 };
}
