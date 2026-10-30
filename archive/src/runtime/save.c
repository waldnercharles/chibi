#include "runtime/save.h"

#include "support/array.h"
#include "support/file_io.h"
#include "support/xalloc.h"
#include "yyjson.h"

#include <assert.h>
#include <errno.h>
#include <math.h>
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
struct save
{
    save_entry *entries;
    char *committed;
    char *path;
    char error[256];
    uint64_t revision;
};

static bool fail(save *save, const char *message)
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

save *save_new()
{
    save *save = xcalloc(1, sizeof *save);
    save->committed = xstrdup("{}");
    return save;
}

void save_free(save *save)
{
    clear_entries(&save->entries);
    free(save->committed);
    free(save->path);
    free(save);
}

static int find_entry(const save *save, const char *key)
{
    for (int i = 0; key && i < acount(save->entries); i++) {
        if (!strcmp(save->entries[i].key, key)) {
            return i;
        }
    }
    return -1;
}

const char *save_get(const save *save, const char *key)
{
    int index = find_entry(save, key);
    return index < 0 ? NULL : save->entries[index].text;
}

bool save_get_number(const save *save, const char *key, double *number)
{
    assert(number);
    const char *text = save_get(save, key);
    if (!text || !text[0]) {
        return false;
    }
    yyjson_val value;
    const char *end = yyjson_read_number(text, &value, 0, NULL, NULL);
    if (!end || *end || !isfinite(yyjson_get_num(&value))) {
        return false;
    }
    *number = yyjson_get_num(&value);
    return true;
}

bool save_set_number(save *save, const char *key, double number)
{
    if (!isfinite(number)) {
        return fail(save, "Saved numbers must be finite");
    }
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_doc_set_root(doc, yyjson_mut_real(doc, number));
    char *text = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    assert(text);
    bool valid = save_set(save, key, text);
    free(text);
    return valid;
}

bool save_set(save *save, const char *key, const char *text)
{
    if (!key || !key[0] || strlen(key) > 256 || !text || strlen(text) > SAVE_TEXT_MAX) {
        return fail(save, "Save keys must contain 1-256 bytes and values at most 65536 bytes");
    }
    /* This store persists JSON text, so reject malformed UTF-8 before any
     * entry, revision or dirty state changes. The JSON writer supplies the
     * same validation used when committing the complete document. */
    yyjson_mut_doc *validation = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *probe = yyjson_mut_obj(validation);
    yyjson_mut_doc_set_root(validation, probe);
    yyjson_mut_obj_add_strcpy(validation, probe, key, text);
    char *encoded = yyjson_mut_write(validation, 0, NULL);
    yyjson_mut_doc_free(validation);
    if (!encoded) {
        return fail(save, "Save keys and values require valid UTF-8");
    }
    free(encoded);
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
    save->error[0] = 0;
    if (index >= 0 && !strcmp(save->entries[index].text, text)) {
        return true;
    }
    char *copy = xstrdup(text);
    if (index >= 0) {
        free(save->entries[index].text);
        save->entries[index].text = copy;
    } else {
        apush(save->entries, ((save_entry){ xstrdup(key), copy }));
    }
    save->revision++;
    return true;
}

bool save_remove(save *save, const char *key)
{
    int index = find_entry(save, key);
    if (index >= 0) {
        free(save->entries[index].key);
        free(save->entries[index].text);
        adel(save->entries, index);
        save->revision++;
    }
    save->error[0] = 0;
    return true;
}

int save_count(const save *save)
{
    return acount(save->entries);
}

const char *save_key(const save *save, int index)
{
    assert(index >= 0 && index < acount(save->entries));
    return save->entries[index].key;
}

uint64_t save_revision(const save *save)
{
    return save->revision;
}

static int compare_entries(const void *a, const void *b)
{
    return strcmp(
        (*(const save_entry *const *)a)->key,
        (*(const save_entry *const *)b)->key
    );
}

static char *serialize(const save *save)
{
    yyjson_mut_doc *doc = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root = yyjson_mut_obj(doc);
    yyjson_mut_doc_set_root(doc, root);
    const save_entry **ordered = NULL;
    for (int i = 0; i < acount(save->entries); i++) {
        apush(ordered, &save->entries[i]);
    }
    if (acount(ordered) > 1) {
        asort(ordered, compare_entries);
    }
    for (int i = 0; i < acount(ordered); i++) {
        yyjson_mut_obj_add_strcpy(doc, root, ordered[i]->key, ordered[i]->text);
    }
    afree(ordered);
    char *json = yyjson_mut_write(doc, 0, NULL);
    yyjson_mut_doc_free(doc);
    return json;
}

bool save_import(save *store, const char *json)
{
    if (!json || strlen(json) > SAVE_BYTES_MAX * 6) {
        return fail(store, "Invalid save document size");
    }
    yyjson_doc *doc = yyjson_read(json, strlen(json), 0);
    yyjson_val *root = yyjson_doc_get_root(doc);
    save *candidate = save_new();
    bool valid = yyjson_is_obj(root);
    size_t i, max;
    yyjson_val *key, *value;
    yyjson_obj_foreach(root, i, max, key, value)
    {
        const char *name = yyjson_get_str(key);
        if (!yyjson_is_str(value) || strlen(name) != yyjson_get_len(key) ||
            strlen(yyjson_get_str(value)) != yyjson_get_len(value) ||
            save_get(candidate, name) ||
            !save_set(candidate, name, yyjson_get_str(value))) {
            valid = false;
            break;
        }
    }
    yyjson_doc_free(doc);
    if (valid) {
        clear_entries(&store->entries);
        store->entries = candidate->entries;
        candidate->entries = NULL;
        free(store->committed);
        store->committed = serialize(store);
        store->error[0] = 0;
        store->revision++;
    }
    save_free(candidate);
    return valid ||
           fail(store, "Save document must contain unique text keys and bounded text values");
}

bool save_open(save *save, const char *path)
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
    char *json = file_read_text(path);
    if (!json && errno != ENOENT) {
        return fail(save, "Could not read save storage");
    }
#endif
    bool loaded = save_import(save, json ? json : "{}");
    free(json);
    if (loaded) {
        free(save->path);
        save->path = xstrdup(path);
    }
    return loaded;
}

bool save_open_project(save *save, const char *project_name, const char *directory, const char *override)
{
    if (override) {
        return save_open(save, override);
    }
    if (!project_name || !project_name[0]) {
        return fail(save, "Save project name is empty");
    }
    /* FNV-1a keeps names, including path separators and Unicode, in one bounded
     * filesystem component. The same manifest name keeps its key on export. */
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)project_name; *p; p++) {
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    }
    char key[48];
    snprintf(key, sizeof key, "chibi-save-%016llx.json", (unsigned long long)hash);
#ifdef __EMSCRIPTEN__
    (void)directory;
    return save_open(save, key);
#else
    if (!directory || !directory[0]) {
        return fail(save, "Save storage directory is unavailable");
    }
    size_t size = strlen(directory) + sizeof key + 1;
    char *path = xmalloc(size);
    snprintf(path, size, "%s/%s", directory, key);
    bool opened = save_open(save, path);
    free(path);
    return opened;
#endif
}

#ifndef __EMSCRIPTEN__
static bool write_file(const char *path, const void *bytes, size_t length)
{
    size_t size = strlen(path) + 16;
    char *temporary = xmalloc(size);
    snprintf(temporary, size, "%s.XXXXXX", path);
    int fd = mkstemp(temporary);
    FILE *file = fd < 0 ? NULL : fdopen(fd, "wb");
    if (!file && fd >= 0) {
        close(fd);
    }
    bool ok = file && fwrite(bytes, 1, length, file) == length;
    if (ok && (fflush(file) || fsync(fd))) {
        ok = false;
    }
    if (file && fclose(file)) {
        ok = false;
    }
    if (ok) {
        ok = rename(temporary, path) == 0;
    }
    if (!ok && fd >= 0) {
        remove(temporary);
    }
    free(temporary);
    return ok;
}
#endif

static bool write_storage(save *save, const char *json)
{
    if (!save->path) {
        return true;
    }
#ifdef __EMSCRIPTEN__
    return save_write_browser(save->path, json);
#else
    return write_file(save->path, json, strlen(json));
#endif
}

static bool blob_available(save *save, const char *key)
{
    if (!key || !key[0] || strlen(key) > SAVE_BLOB_KEY_MAX) {
        return fail(save, "Binary save keys must contain 1-64 bytes");
    }
    if (!save->path) {
        return fail(save, "Binary saves require host-bound durable storage");
    }
#ifdef __EMSCRIPTEN__
    return fail(save, "Binary save storage is unavailable in this browser host");
#else
    return true;
#endif
}

#ifndef __EMSCRIPTEN__
static char *blob_path(const save *save, const char *key)
{
    static const char hex[] = "0123456789abcdef";
    char encoded[SAVE_BLOB_KEY_MAX * 2 + 1];
    size_t length = strlen(key);
    for (size_t i = 0; i < length; i++) {
        unsigned char byte = (unsigned char)key[i];
        encoded[2 * i] = hex[byte >> 4];
        encoded[2 * i + 1] = hex[byte & 15];
    }
    encoded[length * 2] = 0;
    size_t size = strlen(save->path) + strlen(encoded) + 16;
    char *path = xmalloc(size);
    snprintf(path, size, "%s.blob-%s.bin", save->path, encoded);
    return path;
}
#endif

bool save_blob_write(save *save, const char *key, const void *bytes, size_t length)
{
    if (!blob_available(save, key)) {
        return false;
    }
    if (!bytes || !length || length > SAVE_BLOB_BYTES_MAX) {
        return fail(save, "Binary save must contain 1 byte to 64 MiB");
    }
#ifdef __EMSCRIPTEN__
    return false;
#else
    char *path = blob_path(save, key);
    bool ok = write_file(path, bytes, length);
    free(path);
    if (!ok) {
        return fail(save, "Could not write binary save; the previous save is intact");
    }
    save->error[0] = 0;
    return true;
#endif
}

bool save_blob_read(save *save, const char *key, unsigned char **bytes, size_t *length)
{
    assert(bytes && length);
    *bytes = NULL;
    *length = 0;
    if (!blob_available(save, key)) {
        return false;
    }
#ifdef __EMSCRIPTEN__
    return false;
#else
    char *path = blob_path(save, key);
    FILE *file = fopen(path, "rb");
    int open_error = errno;
    free(path);
    if (!file) {
        return fail(save, open_error == ENOENT ? "Binary save was not found" : "Could not open binary save");
    }
    bool ok = fseek(file, 0, SEEK_END) == 0;
    long count = ok ? ftell(file) : -1;
    if (count <= 0 || (unsigned long)count > SAVE_BLOB_BYTES_MAX ||
        fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return fail(save, "Binary save has an invalid size or cannot be read");
    }
    unsigned char *data = xmalloc((size_t)count);
    ok = fread(data, 1, (size_t)count, file) == (size_t)count &&
         fgetc(file) == EOF && !ferror(file);
    if (fclose(file)) {
        ok = false;
    }
    if (!ok) {
        free(data);
        return fail(save, "Could not read complete binary save");
    }
    *bytes = data;
    *length = (size_t)count;
    save->error[0] = 0;
    return true;
#endif
}

bool save_commit(save *save)
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

void save_discard(save *save)
{
    char *json = save_export(save);
    save_import(save, json);
    free(json);
}

char *save_export(const save *save)
{
    return xstrdup(save->committed);
}

const char *save_error(const save *save)
{
    return save->error;
}
