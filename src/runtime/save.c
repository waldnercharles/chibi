#include "runtime/save.h"

#include "graph/runtime/eval.h"
#include "graph/runtime/flow.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/text_file.h"
#include "support/xalloc.h"
#include "third_party/yyjson/yyjson.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(char *, save_read_browser, (const char *path), {
    try {
        return stringToNewUTF8(localStorage.getItem(UTF8ToString(path)) || "{}");
}
catch(_)
{
    return 0;
}
})
EM_JS(bool, save_write_browser, (const char *path, const char *json), {
    try {
        localStorage.setItem(UTF8ToString(path), UTF8ToString(json));
        return true;
}
catch(_)
{
    return false;
}
})
#endif

#define SAVE_ENTRIES_MAX 1024
#define SAVE_TEXT_MAX 65536
#define SAVE_BYTES_MAX (1024 * 1024)

typedef struct
{
    char *key, *text;
} save_entry;
struct game_save
{
    save_entry *entries;
    char *committed;
    char *path;
    char error[256];
};

static bool fail(game_save *save, const char *message)
{
    snprintf(save->error, sizeof save->error, "%s", message);
    return false;
}

static void clear_entries(save_entry **entries)
{
    for (int i = 0; i < acount(*entries); i++) {
        free((*entries)[i].key);
        free((*entries)[i].text);
    }
    afree(*entries);
}

game_save *game_save_new()
{
    game_save *save = xcalloc(1, sizeof *save);
    save->committed = xstrdup("{}");
    return save;
}

void game_save_free(game_save *save)
{
    clear_entries(&save->entries);
    free(save->committed);
    free(save->path);
    free(save);
}

static int find_entry(const game_save *save, const char *key)
{
    for (int i = 0; key && i < acount(save->entries); i++) {
        if (!strcmp(save->entries[i].key, key)) {
            return i;
        }
    }
    return -1;
}

const char *game_save_get(const game_save *save, const char *key)
{
    int index = find_entry(save, key);
    return index < 0 ? NULL : save->entries[index].text;
}

bool game_save_set(game_save *save, const char *key, const char *text)
{
    if (!key || !key[0] || strlen(key) > 256 || !text || strlen(text) > SAVE_TEXT_MAX) {
        return fail(save, "Save keys must contain 1-256 bytes and values at most 65536 bytes");
    }
    int index = find_entry(save, key);
    if (index < 0 && acount(save->entries) >= SAVE_ENTRIES_MAX) {
        return fail(save, "Save entry limit exceeded");
    }
    size_t bytes = strlen(key) + strlen(text);
    for (int i = 0; i < acount(save->entries); i++) {
        if (i != index) {
            bytes += strlen(save->entries[i].key) + strlen(save->entries[i].text);
        }
    }
    if (bytes > SAVE_BYTES_MAX) {
        return fail(save, "Save data exceeds one megabyte");
    }
    char *copy = xstrdup(text);
    if (index >= 0) {
        free(save->entries[index].text);
        save->entries[index].text = copy;
    } else {
        apush(save->entries, ((save_entry){ xstrdup(key), copy }));
    }
    save->error[0] = 0;
    return true;
}

bool game_save_remove(game_save *save, const char *key)
{
    int index = find_entry(save, key);
    if (index >= 0) {
        free(save->entries[index].key);
        free(save->entries[index].text);
        adel(save->entries, index);
    }
    save->error[0] = 0;
    return true;
}

static int compare_entries(const void *a, const void *b)
{
    return strcmp(
        (*(const save_entry *const *)a)->key,
        (*(const save_entry *const *)b)->key
    );
}

static char *serialize(const game_save *save)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    const save_entry **ordered = NULL;
    for (int i = 0; i < acount(save->entries); i++) {
        apush(ordered, &save->entries[i]);
    }
    if (acount(ordered) > 1) {
        qsort(ordered, (size_t)acount(ordered), sizeof *ordered, compare_entries);
    }
    for (int i = 0; i < acount(ordered); i++) {
        yyjson_mut_obj_add_strcpy(doc, root, ordered[i]->key, ordered[i]->text);
    }
    afree(ordered);
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

bool game_save_import(game_save *save, const char *json)
{
    if (!json || strlen(json) > SAVE_BYTES_MAX * 6) {
        return fail(save, "Invalid save document size");
    }
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    game_save *candidate = game_save_new();
    bool valid = yyjson_is_obj(root);
    size_t i, max;
    yyjson_val *key, *value;
    yyjson_obj_foreach(root, i, max, key, value)
    {
        const char *name = yyjson_get_str(key);
        if (!yyjson_is_str(value) || strlen(name) != yyjson_get_len(key) ||
            strlen(yyjson_get_str(value)) != yyjson_get_len(value) ||
            game_save_get(candidate, name) ||
            !game_save_set(candidate, name, yyjson_get_str(value))) {
            valid = false;
            break;
        }
    }
    yyjson_doc_free(doc);
    if (valid) {
        clear_entries(&save->entries);
        save->entries = candidate->entries;
        candidate->entries = NULL;
        free(save->committed);
        save->committed = serialize(save);
        save->error[0] = 0;
    }
    game_save_free(candidate);
    return valid ||
           fail(save, "Save document must contain unique text keys and bounded text values");
}

bool game_save_open(game_save *save, const char *path)
{
    if (!path || !path[0]) {
        return fail(save, "Save storage path is empty");
    }
#ifdef __EMSCRIPTEN__
    char *json = save_read_browser(path);
    if (!json) {
        return fail(save, "Browser save storage is unavailable");
    }
#else
    errno = 0;
    char *json = text_file_read(path);
    if (!json && errno != ENOENT) {
        return fail(save, "Could not read save storage");
    }
#endif
    bool loaded = game_save_import(save, json ? json : "{}");
    free(json);
    if (loaded) {
        free(save->path);
        save->path = xstrdup(path);
    }
    return loaded;
}

static bool write_storage(game_save *save, const char *json)
{
    if (!save->path) {
        return true;
    }
#ifdef __EMSCRIPTEN__
    return save_write_browser(save->path, json);
#else
    size_t size = strlen(save->path) + 16;
    char *temporary = xmalloc(size);
    snprintf(temporary, size, "%s.XXXXXX", save->path);
    int fd = mkstemp(temporary);
    FILE *file = fd < 0 ? NULL : fdopen(fd, "wb");
    if (!file && fd >= 0) {
        close(fd);
    }
    bool ok = file && fputs(json, file) >= 0;
    if (ok && (fflush(file) || fsync(fd))) {
        ok = false;
    }
    if (file && fclose(file)) {
        ok = false;
    }
    if (ok) {
        ok = rename(temporary, save->path) == 0;
    }
    if (!ok && fd >= 0) {
        remove(temporary);
    }
    free(temporary);
    return ok;
#endif
}

bool game_save_commit(game_save *save)
{
    char *json = serialize(save);
    if (!write_storage(save, json)) {
        free(json);
        return fail(save, "Could not commit save data; the previous save is intact");
    }
    free(save->committed);
    save->committed = json;
    save->error[0] = 0;
    return true;
}

void game_save_discard(game_save *save)
{
    char *json = game_save_export(save);
    game_save_import(save, json);
    free(json);
}

char *game_save_export(const game_save *save)
{
    return xstrdup(save->committed);
}

const char *game_save_error(const game_save *save)
{
    return save->error;
}

static const char *read_text(const eval_context *ctx, const pin_value *in, arena *storage, pin_value *out)
{
    game_save *save = eval_context_service(ctx, "game.save");
    if (!save) {
        return "Read Saved Text requires a game world";
    }
    const char *text = game_save_get(save, in[0].str);
    out[0] = (pin_value){ .type = PIN_BOOL, .type_name = "bool", .b = text != NULL };
    if (!text) {
        text = in[1].str ? in[1].str : "";
    }
    char *copy = arena_alloc(storage, strlen(text) + 1);
    strcpy(copy, text);
    out[1] = (pin_value){ .type = PIN_STR, .str = copy };
    return NULL;
}

static int write_text(node *n, flow_context *ctx)
{
    game_save *save = flow_context_service(ctx, "game.save");
    if (!save) {
        return flow_context_fail(ctx, FLOW_ERR_NODE, "Saving requires a game world");
    }
    pin_value in[2];
    for (int i = 1; i < n->input_count; i++) {
        int result = flow_context_eval_data(ctx, i, &in[i - 1]);
        if (result < 0) {
            return result;
        }
    }
    bool ok = true;
    if (!strcmp(n->desc->name, "Set Saved Text")) {
        ok = game_save_set(save, in[0].str, in[1].str ? in[1].str : "");
    } else if (!strcmp(n->desc->name, "Remove Saved Text")) {
        ok = game_save_remove(save, in[0].str);
    } else if (!strcmp(n->desc->name, "Commit Save")) {
        return flow_context_continue(ctx, game_save_commit(save) ? 0 : 1);
    } else {
        game_save_discard(save);
    }
    return ok ? flow_context_continue(ctx, 0)
              : flow_context_fail(ctx, FLOW_ERR_NODE, game_save_error(save));
}

static const node_access READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
static const node_access WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
static pin_spec READ_IN[] = { { .name = "Key", .type = PIN_NAME },
                              { .name = "Default", .type = PIN_STR } };
static pin_spec READ_OUT[] = { { .name = "Found", .type = PIN_BOOL, .type_name = "bool" },
                               { .name = "Text", .type = PIN_STR } };
static pin_spec WRITE_IN[] = { { .name = "Exec", .type = PIN_FLOW, .kind = PIN_EXEC },
                               { .name = "Key", .type = PIN_NAME },
                               { .name = "Text", .type = PIN_STR } };
static pin_spec THEN[] = { { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC } };
static node_desc READ = {
    .name = "Read Saved Text",
    .category = "Storage",
    .keywords = "save profile unlock persistent load",
    .description = "Reads Key from save data, including uncommitted changes; "
                   "when missing, Found is false and Text is Default.",
    .input_count = 2,
    .inputs = READ_IN,
    .output_count = 2,
    .outputs = READ_OUT,
    .pure_kind = NODE_PURE_READ,
    .read_inputs = read_text,
    .access = &READ_ACCESS
};
#define SAVE_NODE(label, count, text)                                          \
    { .name = label,                                                           \
      .category = "Storage",                                                   \
      .description = text,                                                     \
      .input_count = count,                                                    \
      .inputs = WRITE_IN,                                                      \
      .output_count = 1,                                                       \
      .outputs = THEN,                                                         \
      .flow_kind = NODE_FLOW_CUSTOM,                                           \
      .flow = write_text,                                                      \
      .display_class = NODE_DISPLAY_FLOW,                                      \
      .access = &WRITE_ACCESS }
static node_desc SET = SAVE_NODE("Set Saved Text", 3, "Sets Key to Text in save data; the change persists only after Commit Save.");
static node_desc REMOVE = SAVE_NODE("Remove Saved Text", 2, "Removes Key from save data; the removal persists only after Commit Save.");
static pin_spec COMMIT_OUT[] = {
    { .name = "Then", .type = PIN_FLOW, .kind = PIN_EXEC },
    { .name = "Failed", .type = PIN_FLOW, .kind = PIN_EXEC }
};
static node_desc COMMIT = {
    .name = "Commit Save",
    .category = "Storage",
    .description = "Writes save data to storage; if writing fails, runs Failed "
                   "and keeps the previous save.",
    .input_count = 1,
    .inputs = WRITE_IN,
    .output_count = 2,
    .outputs = COMMIT_OUT,
    .flow_kind = NODE_FLOW_CUSTOM,
    .flow = write_text,
    .display_class = NODE_DISPLAY_FLOW,
    .access = &WRITE_ACCESS
};
static node_desc DISCARD = SAVE_NODE("Discard Save Changes", 1, "Reverts save data to the last commit, dropping uncommitted changes.");
static node_desc *const NODES[] = { &READ, &SET, &REMOVE, &COMMIT, &DISCARD };
const registry_pack GAME_SAVE_PACK = { .name = "game.save",
                                       .nodes = NODES,
                                       .node_count = 5 };
