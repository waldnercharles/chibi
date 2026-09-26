/* The data Studio's authored panels bind beyond their own world's `save`
 * (render/ui.h): `world`, the saved keys of the world the Scene shows, and
 * `theme`, the custom properties of the stylesheets the open scene's root
 * document imports. A panel's edits apply back: a saved key to the save, and
 * a theme value to its file, which restyles everything the page styles.
 * `inspector` is the UI Inspector's (see its section below). */
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

static void add_inspector(yyjson_mut_doc *doc, yyjson_mut_val *root);
static void sync_inspector(studio *s, ui_data *data);

void studio_sources_add(yyjson_mut_doc *doc)
{
    yyjson_mut_val *root = yyjson_mut_doc_get_root(doc);
    yyjson_mut_val *world = yyjson_mut_obj_add_obj(doc, root, "world");
    yyjson_mut_obj_add_str(doc, world, "title", "");
    yyjson_mut_obj_add_arr(doc, world, "saved");
    yyjson_mut_obj_add_arr(doc, yyjson_mut_obj_add_obj(doc, root, "theme"), "sections");
    add_inspector(doc, root);
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
    sync_inspector(s, data);
}

void studio_sources_free(studio_sources *src)
{
    free_tokens(src);
    ui_page_free(&src->page);
    *src = (studio_sources){ 0 };
}

/* ---------- Inspector ---------- */

/* `inspector` is what the Inspector panel shows (studio_inspect.c): its
 * target and pick state, the Elements rows, and the selection's box,
 * styles, computed values and attributes. Its strings point at the
 * Inspector's own, which it keeps until its next read, so showing a model
 * allocates nothing once each list has its items; the items a list sheds
 * wait in `spare` until it grows again. The panel writes what it asks for:
 * select, toggle, hover and rule (rows, -1 for none), action ("source" for
 * Edit Source), target, and force (the pseudo classes forced). */

static const char *const TARGET_NAMES[STUDIO_INSPECT_TARGET_COUNT] = { "studio",
                                                                       "graph",
                                                                       "game" };
static const char *const PSEUDO_KEYS[] = { "hover", "active", "focus", "checked", NULL };
static const char *const EDGE_KEYS[] = { "top", "right", "bottom", "left", NULL };
static const char *const BOX_KEYS[] = { "margin", "border", "padding" };

/* A list item's members: strings, then booleans, each list NULL-terminated. */
typedef struct item_shape
{
    const char *const *texts;
    const char *const *flags;
} item_shape;

static const char *const ROW_TEXTS[] = { "indent", "tag",  "id", "classes",
                                         "widget", "text", NULL };
static const char *const ROW_FLAGS[] = { "node",     "parent",  "open",
                                         "selected", "hovered", "hidden",
                                         NULL };
static const char *const STYLE_TEXTS[] = { "selector", "origin", "name", "value", NULL };
static const char *const STYLE_FLAGS[] = { "heading", "ancestor", "inherited", "linked", NULL };
static const char *const PAIR_TEXTS[] = { "name", "value", NULL };
static const char *const NO_FLAGS[] = { NULL };
static const item_shape ROW = { ROW_TEXTS, ROW_FLAGS };
static const item_shape STYLE = { STYLE_TEXTS, STYLE_FLAGS };
static const item_shape PAIR = { PAIR_TEXTS, NO_FLAGS };

static void add_texts(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *const *keys)
{
    for (; *keys; keys++) {
        yyjson_mut_obj_add_str(doc, obj, *keys, "");
    }
}

static void add_flags(yyjson_mut_doc *doc, yyjson_mut_val *obj, const char *const *keys)
{
    for (; *keys; keys++) {
        yyjson_mut_obj_add_bool(doc, obj, *keys, false);
    }
}

static void add_inspector(yyjson_mut_doc *doc, yyjson_mut_val *root)
{
    static const char *const TEXTS[] = { "target", "shortcut", "title",
                                         "path",   "source",   "pseudo",
                                         "size",   "action",   NULL };
    static const char *const FLAGS[] = { "picking",  "wide",   "graph",
                                         "game",     "more",   "selection",
                                         "detached", "native", "editable",
                                         NULL };
    static const char *const REQUESTS[] = { "select", "toggle", "hover", "rule" };
    static const char *const LISTS[] = { "rows", "styles", "computed", "attributes" };
    static const char *const SPARES[] = { "rows", "styles", "pairs" };
    yyjson_mut_val *in = yyjson_mut_obj_add_obj(doc, root, "inspector");
    add_texts(doc, in, TEXTS);
    add_flags(doc, in, FLAGS);
    yyjson_mut_obj_add_str(doc, in, "tab", "styles"); /* the panel's own */
    for (size_t i = 0; i < sizeof REQUESTS / sizeof *REQUESTS; i++) {
        yyjson_mut_obj_add_int(doc, in, REQUESTS[i], -1);
    }
    add_flags(doc, yyjson_mut_obj_add_obj(doc, in, "force"), PSEUDO_KEYS);
    yyjson_mut_val *box = yyjson_mut_obj_add_obj(doc, in, "box");
    for (size_t b = 0; b < sizeof BOX_KEYS / sizeof *BOX_KEYS; b++) {
        add_texts(doc, yyjson_mut_obj_add_obj(doc, box, BOX_KEYS[b]), EDGE_KEYS);
    }
    for (size_t i = 0; i < sizeof LISTS / sizeof *LISTS; i++) {
        yyjson_mut_obj_add_arr(doc, in, LISTS[i]);
    }
    yyjson_mut_val *spare = yyjson_mut_obj_add_obj(doc, in, "spare");
    for (size_t i = 0; i < sizeof SPARES / sizeof *SPARES; i++) {
        yyjson_mut_obj_add_arr(doc, spare, SPARES[i]);
    }
}

static yyjson_mut_val *member(yyjson_mut_val *obj, const char *key)
{
    return yyjson_mut_obj_get(obj, key);
}

static const char *text_at(yyjson_mut_val *obj, const char *key)
{
    const char *text = yyjson_mut_get_str(member(obj, key));
    return text ? text : "";
}

static int number_at(yyjson_mut_val *obj, const char *key)
{
    yyjson_mut_val *v = member(obj, key);
    return yyjson_mut_is_num(v) ? (int)yyjson_mut_get_num(v) : -1;
}

/* A request the panel writes once, taken and cleared. */
static int take(yyjson_mut_val *in, const char *key)
{
    int value = number_at(in, key);
    yyjson_mut_set_int(member(in, key), -1);
    return value;
}

static int target_named(const char *name)
{
    for (int t = 0; t < STUDIO_INSPECT_TARGET_COUNT; t++) {
        if (!strcmp(TARGET_NAMES[t], name)) {
            return t;
        }
    }
    return -1;
}

static studio_inspect_input inspector_input(yyjson_mut_val *in)
{
    studio_inspect_input input = {
        .select = take(in, "select"),
        .toggle = take(in, "toggle"),
        .rule = take(in, "rule"),
        .hover = number_at(in, "hover"),
        .target = target_named(text_at(in, "target")),
        .source = !strcmp(text_at(in, "action"), "source"),
    };
    yyjson_mut_set_str(member(in, "action"), "");
    for (int p = 0; p < STUDIO_PSEUDO_COUNT; p++) {
        input.force[p] = yyjson_mut_get_bool(member(member(in, "force"), PSEUDO_KEYS[p]));
    }
    return input;
}

/* Points string member `key` at `text`, without a copy. */
static void point(yyjson_mut_val *obj, const char *key, const char *text)
{
    yyjson_mut_set_strn(member(obj, key), text, strlen(text));
}

static void flag(yyjson_mut_val *obj, const char *key, bool on)
{
    yyjson_mut_set_bool(member(obj, key), on);
}

static yyjson_mut_val *make_item(yyjson_mut_doc *doc, const item_shape *shape)
{
    yyjson_mut_val *item = yyjson_mut_obj(doc);
    add_texts(doc, item, shape->texts);
    add_flags(doc, item, shape->flags);
    return item;
}

/* Gives `list` `count` items, shedding extra ones to `spare` and taking
 * new ones from it before making any. */
static void resize(
    yyjson_mut_doc *doc,
    yyjson_mut_val *list,
    yyjson_mut_val *spare,
    int count,
    const item_shape *shape
)
{
    while ((int)yyjson_mut_arr_size(list) > count) {
        yyjson_mut_arr_append(spare, yyjson_mut_arr_remove_last(list));
    }
    while ((int)yyjson_mut_arr_size(list) < count) {
        yyjson_mut_val *item = yyjson_mut_arr_size(spare)
                                 ? yyjson_mut_arr_remove_last(spare)
                                 : make_item(doc, shape);
        yyjson_mut_arr_append(list, item);
    }
}

static void show_item(
    yyjson_mut_val *item,
    const item_shape *shape,
    const char *const *texts,
    const bool *flags
)
{
    for (int t = 0; shape->texts[t]; t++) {
        point(item, shape->texts[t], texts[t]);
    }
    for (int f = 0; shape->flags[f]; f++) {
        flag(item, shape->flags[f], flags[f]);
    }
}

static void show_elements(yyjson_mut_doc *doc, yyjson_mut_val *in, const studio_inspect_row *rows)
{
    yyjson_mut_val *list = member(in, "rows");
    resize(doc, list, member(member(in, "spare"), "rows"), acount(rows), &ROW);
    yyjson_mut_arr_iter it = yyjson_mut_arr_iter_with(list);
    for (int i = 0; i < acount(rows); i++) {
        const studio_inspect_row *r = &rows[i];
        const char *texts[] = { r->indent,  r->tag,    r->id,
                                r->classes, r->widget, r->text };
        bool flags[] = { r->node,     r->parent,  r->open,
                         r->selected, r->hovered, r->hidden };
        show_item(yyjson_mut_arr_iter_next(&it), &ROW, texts, flags);
    }
}

static void show_styles(yyjson_mut_doc *doc, yyjson_mut_val *in, const studio_inspect_style *styles)
{
    yyjson_mut_val *list = member(in, "styles");
    resize(doc, list, member(member(in, "spare"), "styles"), acount(styles), &STYLE);
    yyjson_mut_arr_iter it = yyjson_mut_arr_iter_with(list);
    for (int i = 0; i < acount(styles); i++) {
        const studio_inspect_style *st = &styles[i];
        const char *texts[] = { st->selector, st->origin, st->name, st->value };
        bool flags[] = { st->heading, st->ancestor, st->inherited, st->linked };
        show_item(yyjson_mut_arr_iter_next(&it), &STYLE, texts, flags);
    }
}

static void show_pairs(yyjson_mut_doc *doc, yyjson_mut_val *in, const char *key, const studio_inspect_pair *pairs)
{
    yyjson_mut_val *list = member(in, key);
    resize(doc, list, member(member(in, "spare"), "pairs"), acount(pairs), &PAIR);
    yyjson_mut_arr_iter it = yyjson_mut_arr_iter_with(list);
    for (int i = 0; i < acount(pairs); i++) {
        const char *texts[] = { pairs[i].name, pairs[i].value };
        show_item(yyjson_mut_arr_iter_next(&it), &PAIR, texts, NULL);
    }
}

static void show_inspector(yyjson_mut_doc *doc, yyjson_mut_val *in, const studio_inspect_model *m)
{
    point(in, "target", TARGET_NAMES[m->target]);
    const char *texts[] = { m->shortcut, m->title,  m->path,
                            m->source,   m->pseudo, m->size };
    static const char *const TEXTS[] = { "shortcut", "title",  "path",
                                         "source",   "pseudo", "size" };
    for (size_t t = 0; t < sizeof TEXTS / sizeof *TEXTS; t++) {
        point(in, TEXTS[t], texts[t]);
    }
    bool flags[] = { m->picking,
                     m->wide,
                     m->targets[STUDIO_INSPECT_GRAPH],
                     m->targets[STUDIO_INSPECT_GAME],
                     m->more,
                     m->selection,
                     m->detached,
                     m->native,
                     m->editable };
    static const char *const FLAGS[] = { "picking",  "wide",   "graph",
                                         "game",     "more",   "selection",
                                         "detached", "native", "editable" };
    for (size_t f = 0; f < sizeof FLAGS / sizeof *FLAGS; f++) {
        flag(in, FLAGS[f], flags[f]);
    }
    for (int p = 0; p < STUDIO_PSEUDO_COUNT; p++) {
        flag(member(in, "force"), PSEUDO_KEYS[p], m->force[p]);
    }
    for (size_t b = 0; b < sizeof BOX_KEYS / sizeof *BOX_KEYS; b++) {
        for (int e = 0; EDGE_KEYS[e]; e++) {
            point(member(member(in, "box"), BOX_KEYS[b]), EDGE_KEYS[e], m->box[b][e]);
        }
    }
    show_elements(doc, in, m->rows);
    show_styles(doc, in, m->styles);
    show_pairs(doc, in, "computed", m->computed);
    show_pairs(doc, in, "attributes", m->attributes);
}

static void sync_inspector(studio *s, ui_data *data)
{
    yyjson_mut_val *in = variable(data, "inspector");
    studio_inspect_input input = inspector_input(in);
    const studio_inspect_model *m = studio_inspect_sync(s, &input);
    if (m && m->changed) {
        show_inspector(data->doc, in, m);
        rml_data_model_dirty(data->model, "inspector");
    }
}
