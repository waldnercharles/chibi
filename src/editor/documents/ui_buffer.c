#include "editor/documents/ui_buffer.h"

#include "assets/ui_document.h"
#include "support/array.h"
#include "support/asset_text.h"
#include "support/rcss.h"
#include "support/text_file.h"
#include "support/text_overlay.h"
#include "support/xalloc.h"

#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Typing within this many seconds of the last typed change joins its step. */
#define UI_BUFFER_TYPING 1.0
/* Seconds between looks at the file on disk. */
#define UI_BUFFER_POLL 1.0
#define UI_NAME_MAX 64

static const char *const PART_KEYS[UI_PART_COUNT] = { "rml", "rcss" };

static char *copy(const char *text)
{
    return text ? xstrdup(text) : NULL;
}

static bool same(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}

/* FNV-1a; 0 for a file that cannot be read. */
static uint64_t hash_text(const char *text)
{
    if (!text) {
        return 0;
    }
    uint64_t hash = 14695981039346656037ull;
    for (const char *c = text; *c; c++) {
        hash = (hash ^ (unsigned char)*c) * 1099511628211ull;
    }
    return hash;
}

static void free_parts(char **parts)
{
    for (int p = 0; p < UI_PART_COUNT; p++) {
        free(parts[p]);
        parts[p] = NULL;
    }
}

static void clear_steps(ui_buffer_step **steps)
{
    for (int i = 0; i < acount(*steps); i++) {
        free_parts((*steps)[i].parts);
    }
    afree(*steps);
    *steps = NULL;
}

/* A ui asset's text as its parts: a mapping of rml and rcss text. */
static bool split(const char *text, char **parts, char *error, size_t size)
{
    yyjson_doc *doc = asset_text_read(text, error, size);
    if (!doc) {
        return false;
    }
    yyjson_val *root = yyjson_doc_get_root(doc);
    size_t found = 0;
    bool text_values = true;
    for (int p = 0; p < UI_PART_COUNT; p++) {
        yyjson_val *value = yyjson_obj_get(root, PART_KEYS[p]);
        found += value != NULL;
        text_values = text_values && (!value || yyjson_is_str(value));
        parts[p] = xstrdup(yyjson_is_str(value) ? yyjson_get_str(value) : "");
    }
    bool ok = yyjson_is_obj(root) && yyjson_obj_size(root) == found && text_values;
    yyjson_doc_free(doc);
    if (!ok) {
        free_parts(parts);
        snprintf(error, size, "a ui asset is a mapping of rml and rcss text");
    }
    return ok;
}

/* The file's text as parts, and its hash. */
static bool read_parts(const char *path, bool stylesheet, char **parts, uint64_t *hash, char *error, size_t size)
{
    char *text = text_file_read_disk(path);
    if (!text) {
        snprintf(error, size, "cannot read %s", path);
        return false;
    }
    *hash = hash_text(text);
    if (stylesheet) {
        parts[UI_PART_RML] = NULL;
        parts[UI_PART_RCSS] = text;
        return true;
    }
    bool ok = split(text, parts, error, size);
    free(text);
    return ok;
}

static bool ends_with(const char *text, const char *suffix)
{
    size_t length = strlen(text), suffix_length = strlen(suffix);
    return length >= suffix_length && !strcmp(text + length - suffix_length, suffix);
}

ui_buffer *ui_buffer_open(const char *path, char *error, size_t size)
{
    assert(path[0] == '/');
    bool stylesheet = ends_with(path, ".rcss");
    char *parts[UI_PART_COUNT] = { 0 };
    uint64_t hash = 0;
    if (!read_parts(path, stylesheet, parts, &hash, error, size)) {
        return NULL;
    }
    ui_buffer *b = xcalloc(1, sizeof *b);
    b->path = xstrdup(path);
    b->stylesheet = stylesheet;
    for (int p = 0; p < UI_PART_COUNT; p++) {
        b->parts[p] = parts[p];
        b->saved[p] = copy(parts[p]);
    }
    b->disk = b->disk_seen = hash;
    b->part = stylesheet ? UI_PART_RCSS : UI_PART_RML;
    b->typed = -1;
    b->revision = 1;
    ui_buffer_check(b);
    return b;
}

static void publish(ui_buffer *b)
{
    char *text = ui_buffer_compose(b);
    text_overlay_set(b->path, text);
    free(text);
    b->published = true;
}

/* The file stands again; true when the text stood in for it. */
static bool unpublish(ui_buffer *b)
{
    bool was = b->published;
    if (was) {
        text_overlay_set(b->path, NULL);
    }
    b->published = false;
    return was;
}

void ui_buffer_free(ui_buffer *b)
{
    if (!b) {
        return;
    }
    unpublish(b);
    free(b->path);
    free_parts(b->parts);
    free_parts(b->saved);
    clear_steps(&b->undo);
    clear_steps(&b->redo);
    free(b);
}

const char *ui_buffer_text(const ui_buffer *b, ui_part part)
{
    return b->parts[part];
}

/* ---------- Edits and history ---------- */

static ui_buffer_step snapshot(const ui_buffer *b, ui_part part)
{
    ui_buffer_step step = { .part = part };
    for (int p = 0; p < UI_PART_COUNT; p++) {
        step.parts[p] = copy(b->parts[p]);
    }
    return step;
}

static void apply(ui_buffer *b, ui_part part, const text_edit *edits, int count, bool join)
{
    assert(b->parts[part]);
    if (!join) {
        apush(b->undo, snapshot(b, part));
        clear_steps(&b->redo);
    }
    char *text = text_edits_apply(b->parts[part], edits, count);
    free(b->parts[part]);
    b->parts[part] = text;
    b->part = part;
    b->revision++;
}

void ui_buffer_edit(ui_buffer *b, ui_part part, const text_edit *edits, int count)
{
    b->typed = -1;
    apply(b, part, edits, count, false);
}

void ui_buffer_type(ui_buffer *b, ui_part part, const char *text, double now)
{
    const char *old = b->parts[part];
    assert(old);
    int length = (int)strlen(old), typed = (int)strlen(text);
    int start = 0, end = 0;
    while (start < length && start < typed && old[start] == text[start]) {
        start++;
    }
    while (end < length - start && end < typed - start &&
           old[length - 1 - end] == text[typed - 1 - end]) {
        end++;
    }
    if (start == length && start == typed) {
        return;
    }
    char *inserted = xmalloc((size_t)(typed - start - end) + 1);
    snprintf(inserted, (size_t)(typed - start - end) + 1, "%s", text + start);
    text_edit edit = { start, length - end, inserted };
    bool join = b->typed >= 0 && now - b->typed < UI_BUFFER_TYPING &&
                b->part == part && acount(b->undo);
    apply(b, part, &edit, 1, join);
    b->typed = now;
    free(inserted);
}

bool ui_buffer_can_undo(const ui_buffer *b)
{
    return acount(b->undo) > 0;
}

bool ui_buffer_can_redo(const ui_buffer *b)
{
    return acount(b->redo) > 0;
}

/* Restores the last step of `from`, recording the text it replaces in `to`. */
static void replay(ui_buffer *b, ui_buffer_step **from, ui_buffer_step **to)
{
    ui_buffer_step step = apop(*from);
    apush(*to, snapshot(b, step.part));
    free_parts(b->parts);
    memcpy(b->parts, step.parts, sizeof b->parts);
    b->part = step.part;
    b->typed = -1;
    b->revision++;
}

void ui_buffer_undo(ui_buffer *b)
{
    assert(ui_buffer_can_undo(b));
    replay(b, &b->undo, &b->redo);
}

void ui_buffer_redo(ui_buffer *b)
{
    assert(ui_buffer_can_redo(b));
    replay(b, &b->redo, &b->undo);
}

bool ui_buffer_dirty(const ui_buffer *b)
{
    for (int p = 0; p < UI_PART_COUNT; p++) {
        if (!same(b->parts[p], b->saved[p])) {
            return true;
        }
    }
    return false;
}

/* ---------- The file's text ---------- */

/* Canonical ui asset YAML: the markup, then the RCSS when there is any. */
static char *compose_yaml(char *const *parts)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    for (int p = 0; p < UI_PART_COUNT; p++) {
        if (p == UI_PART_RML || parts[p][0]) {
            yyjson_mut_obj_add_strcpy(doc, root, PART_KEYS[p], parts[p]);
        }
    }
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    char error[128];
    char *text = asset_text_to_disk(json, error, sizeof error);
    assert(text);
    free(json);
    return text;
}

char *ui_buffer_compose(const ui_buffer *b)
{
    return b->stylesheet ? xstrdup(b->parts[UI_PART_RCSS]) : compose_yaml(b->parts);
}

bool ui_buffer_check(ui_buffer *b)
{
    char *text = ui_buffer_compose(b);
    char directory[1024];
    const char *slash = strrchr(b->path, '/');
    snprintf(directory, sizeof directory, "%.*s", (int)(slash - b->path), b->path);
    b->error[0] = '\0';
    bool ok;
    if (b->stylesheet) {
        char *flat = rcss_expand(text, directory, NULL, b->error, sizeof b->error);
        ok = flat != NULL;
        free(flat);
    } else {
        ui_document document = { 0 };
        ok = ui_document_read(
            text,
            directory,
            NULL,
            &document,
            b->error,
            sizeof b->error
        );
        ui_document_free(&document);
    }
    b->checked = b->revision;
    free(text);
    return ok;
}

bool ui_buffer_update(ui_buffer *b, double now, bool live)
{
    if (b->seen != b->revision) {
        b->seen = b->revision;
        b->seen_time = now;
    }
    bool settled = now - b->seen_time >= UI_BUFFER_SETTLE;
    bool fresh = settled && b->checked != b->revision;
    if (fresh) {
        ui_buffer_check(b);
    }
    if (!live) {
        return unpublish(b);
    }
    if (!settled || b->error[0] || (!fresh && b->published)) {
        return false;
    }
    if (!ui_buffer_dirty(b)) {
        return unpublish(b);
    }
    publish(b);
    return true;
}

/* ---------- Disk ---------- */

static uint64_t disk_hash(const ui_buffer *b)
{
    char *text = text_file_read_disk(b->path);
    uint64_t hash = hash_text(text);
    free(text);
    return hash;
}

bool ui_buffer_on_disk(const ui_buffer *b)
{
    return disk_hash(b) == b->disk;
}

bool ui_buffer_poll_disk(ui_buffer *b, double now)
{
    if (now < b->disk_due) {
        return false;
    }
    b->disk_due = now + UI_BUFFER_POLL;
    uint64_t hash = disk_hash(b);
    bool changed = hash != b->disk_seen && hash != b->disk;
    b->disk_seen = hash;
    return changed;
}

void ui_buffer_saved(ui_buffer *b, const char *text)
{
    for (int p = 0; p < UI_PART_COUNT; p++) {
        free(b->saved[p]);
        b->saved[p] = copy(b->parts[p]);
    }
    b->disk = b->disk_seen = hash_text(text);
    b->typed = -1;
    unpublish(b);
}

bool ui_buffer_reload(ui_buffer *b, char *error, size_t size)
{
    char *parts[UI_PART_COUNT] = { 0 };
    uint64_t hash = 0;
    if (!read_parts(b->path, b->stylesheet, parts, &hash, error, size)) {
        return false;
    }
    free_parts(b->parts);
    free_parts(b->saved);
    for (int p = 0; p < UI_PART_COUNT; p++) {
        b->parts[p] = parts[p];
        b->saved[p] = copy(parts[p]);
    }
    b->disk = b->disk_seen = hash;
    clear_steps(&b->undo);
    clear_steps(&b->redo);
    b->typed = -1;
    b->revision++;
    unpublish(b);
    ui_buffer_check(b);
    return true;
}

/* ---------- Checkpoints ---------- */

static yyjson_mut_val *parts_value(yyjson_mut_doc *doc, char *const *parts)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    for (int p = 0; p < UI_PART_COUNT; p++) {
        yyjson_mut_arr_append(array, parts[p] ? yyjson_mut_strcpy(doc, parts[p]) : yyjson_mut_null(doc));
    }
    return array;
}

static yyjson_mut_val *steps_value(yyjson_mut_doc *doc, const ui_buffer_step *steps)
{
    yyjson_mut_val *array = yyjson_mut_arr(doc);
    for (int i = 0; i < acount(steps); i++) {
        yyjson_mut_val *step = yyjson_mut_arr_add_obj(doc, array);
        yyjson_mut_obj_add_val(doc, step, "parts", parts_value(doc, steps[i].parts));
        yyjson_mut_obj_add_int(doc, step, "part", steps[i].part);
    }
    return array;
}

yyjson_mut_val *ui_buffer_checkpoint(const ui_buffer *b, yyjson_mut_doc *doc)
{
    yyjson_mut_val *value = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_strcpy(doc, value, "path", b->path);
    yyjson_mut_obj_add_val(doc, value, "parts", parts_value(doc, b->parts));
    yyjson_mut_obj_add_val(doc, value, "saved", parts_value(doc, b->saved));
    yyjson_mut_obj_add_uint(doc, value, "disk", b->disk);
    yyjson_mut_obj_add_int(doc, value, "part", b->part);
    yyjson_mut_obj_add_val(doc, value, "undo", steps_value(doc, b->undo));
    yyjson_mut_obj_add_val(doc, value, "redo", steps_value(doc, b->redo));
    return value;
}

static bool read_parts_value(yyjson_val *array, bool stylesheet, char **parts)
{
    if (yyjson_arr_size(array) != UI_PART_COUNT) {
        return false;
    }
    for (int p = 0; p < UI_PART_COUNT; p++) {
        yyjson_val *text = yyjson_arr_get(array, p);
        bool absent = stylesheet && p == UI_PART_RML;
        if (absent ? !yyjson_is_null(text) : !yyjson_is_str(text)) {
            return false;
        }
        parts[p] = absent ? NULL : xstrdup(yyjson_get_str(text));
    }
    return true;
}

static bool read_part(yyjson_val *value, ui_part *part)
{
    int index = yyjson_get_int(value);
    *part = (ui_part)index;
    return yyjson_is_int(value) && index >= 0 && index < UI_PART_COUNT;
}

static bool read_steps(yyjson_val *array, bool stylesheet, ui_buffer_step **steps)
{
    size_t i, count;
    yyjson_val *item;
    yyjson_arr_foreach(array, i, count, item)
    {
        ui_buffer_step step = { 0 };
        bool ok = read_parts_value(yyjson_obj_get(item, "parts"), stylesheet, step.parts) &&
                  read_part(yyjson_obj_get(item, "part"), &step.part);
        apush(*steps, step);
        if (!ok) {
            return false;
        }
    }
    return yyjson_is_arr(array);
}

ui_buffer *ui_buffer_restore(yyjson_val *value)
{
    const char *path = yyjson_get_str(yyjson_obj_get(value, "path"));
    if (!path || path[0] != '/') {
        return NULL;
    }
    ui_buffer *b = xcalloc(1, sizeof *b);
    b->path = xstrdup(path);
    b->stylesheet = ends_with(path, ".rcss");
    b->disk = b->disk_seen = yyjson_get_uint(yyjson_obj_get(value, "disk"));
    b->typed = -1;
    b->revision = 1;
    if (!read_parts_value(yyjson_obj_get(value, "parts"), b->stylesheet, b->parts) ||
        !read_parts_value(yyjson_obj_get(value, "saved"), b->stylesheet, b->saved) ||
        !read_part(yyjson_obj_get(value, "part"), &b->part) ||
        !read_steps(yyjson_obj_get(value, "undo"), b->stylesheet, &b->undo) ||
        !read_steps(yyjson_obj_get(value, "redo"), b->stylesheet, &b->redo) ||
        !b->parts[b->part]) {
        ui_buffer_free(b);
        return NULL;
    }
    return b;
}

/* ---------- New assets ---------- */

static const char *const NEW_EXTENSIONS[UI_NEW_KIND_COUNT] = { ".yaml", ".yaml", ".rcss" };

/* Names become file names and <use ui="Name"> values. */
static bool valid_name(const char *name)
{
    if (!isalpha((unsigned char)name[0]) || strlen(name) > UI_NAME_MAX) {
        return false;
    }
    for (const char *c = name; *c; c++) {
        if (!isalnum((unsigned char)*c) && *c != '_' && *c != '-') {
            return false;
        }
    }
    return true;
}

char *ui_buffer_new_path(const char *folder, ui_new_kind kind, const char *name, char *error, size_t size)
{
    if (!valid_name(name)) {
        snprintf(error, size, name[0] ? "Start with a letter; use letters, digits, - and _" : "Enter a name");
        return NULL;
    }
    size_t length = strlen(folder) + strlen(name) + strlen(NEW_EXTENSIONS[kind]) + 2;
    char *path = xmalloc(length);
    snprintf(path, length, "%s/%s%s", folder, name, NEW_EXTENSIONS[kind]);
    struct stat info;
    if (stat(path, &info) == 0) {
        snprintf(error, size, "%s%s already exists", name, NEW_EXTENSIONS[kind]);
        free(path);
        return NULL;
    }
    return path;
}

char *ui_buffer_template(ui_new_kind kind)
{
    static const char *const RML[UI_NEW_KIND_COUNT] = {
        [UI_NEW_WIDGET] = "<div class=\"content\"><slot/></div>",
        [UI_NEW_DOCUMENT] = "<div class=\"panel\">\n  <div "
                            "class=\"title\">Title</div>\n</div>\n",
    };
    static const char *const RCSS[UI_NEW_KIND_COUNT] = {
        [UI_NEW_WIDGET] = "body { display: block; }\n"
                          ".content { display: flex; align-items: center; gap: "
                          "6dp; }\n",
        [UI_NEW_DOCUMENT] = ".panel { display: block; padding: 12dp; }\n"
                            ".title { font-size: 16dp; font-weight: 600; }\n",
        [UI_NEW_STYLESHEET] = "/* Rules ui assets share with @import. */\n",
    };
    if (kind == UI_NEW_STYLESHEET) {
        return xstrdup(RCSS[kind]);
    }
    char *const parts[UI_PART_COUNT] = { (char *)RML[kind], (char *)RCSS[kind] };
    return compose_yaml(parts);
}
