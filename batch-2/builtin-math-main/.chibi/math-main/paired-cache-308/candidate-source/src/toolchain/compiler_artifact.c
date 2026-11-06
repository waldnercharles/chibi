#include "toolchain/compiler_artifact.h"

#include "support/xalloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARTIFACT_HEADER 32
#define ARTIFACT_ROW 24
static const unsigned char magic[8] = { 'C', 'H', 'I', 'B', 'I', 'C', 'L', 'O' };
static void fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
}
static void put(unsigned char *p, uint64_t n)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (unsigned char)(n >> (8 * i));
    }
}
static uint64_t get(const unsigned char *p)
{
    uint64_t n = 0;
    for (int i = 0; i < 8; i++) {
        n |= (uint64_t)p[i] << (8 * i);
    }
    return n;
}
static int order(const void *left, const void *right)
{
    const package_module *const *a = left, *const *b = right;
    return strcmp((*a)->name, (*b)->name);
}
bool compiler_artifact_write(
    const package_module *rows,
    int count,
    uint64_t root,
    unsigned char **out,
    size_t *length,
    char *error,
    size_t error_size
)
{
    if (out) {
        *out = NULL;
    }
    if (length) {
        *length = 0;
    }
    if (!out || !length || !rows || count <= 0 || count > PACKAGE_MAX_MODULES || !root) {
        fail(error, error_size, "invalid compiler artifact records");
        return false;
    }
    const package_module **sorted = xmalloc((size_t)count * sizeof *sorted);
    size_t bytes = ARTIFACT_HEADER;
    bool found = false;
    for (int i = 0; i < count; i++) {
        const package_module *r = &rows[i];
        size_t name = r->name ? strnlen(r->name, PACKAGE_MAX_BYTES) : 0;
        if (!name || name >= PACKAGE_MAX_BYTES || !r->id || !r->data ||
            !r->length || name > PACKAGE_MAX_BYTES - ARTIFACT_ROW ||
            bytes > PACKAGE_MAX_BYTES - ARTIFACT_ROW - name ||
            r->length > PACKAGE_MAX_BYTES - bytes - ARTIFACT_ROW - name) {
            fail(error, error_size, "compiler artifact size or record is invalid");
            free(sorted);
            return false;
        }
        bytes += ARTIFACT_ROW + name + r->length;
        sorted[i] = r;
        found |= r->id == root;
        for (int j = 0; j < i; j++) {
            if (rows[j].id == r->id || !strcmp(rows[j].name, r->name)) {
                fail(error, error_size, "duplicate compiler artifact module");
                free(sorted);
                return false;
            }
        }
    }
    if (!found) {
        fail(error, error_size, "compiler artifact root is missing");
        free(sorted);
        return false;
    }
    qsort(sorted, (size_t)count, sizeof *sorted, order);
    unsigned char *data = xmalloc(bytes);
    memcpy(data, magic, 8);
    put(data + 8, 1);
    put(data + 16, root);
    put(data + 24, (uint64_t)count);
    size_t offset = ARTIFACT_HEADER;
    for (int i = 0; i < count; i++) {
        const package_module *r = sorted[i];
        size_t name = strlen(r->name);
        put(data + offset, r->id);
        put(data + offset + 8, name);
        put(data + offset + 16, r->length);
        offset += ARTIFACT_ROW;
        memcpy(data + offset, r->name, name);
        offset += name;
        memcpy(data + offset, r->data, r->length);
        offset += r->length;
    }
    free(sorted);
    *out = data;
    *length = bytes;
    return true;
}
package_module *compiler_artifact_rows(
    const void *input,
    size_t length,
    int *count,
    uint64_t *root,
    char *error,
    size_t error_size
)
{
    *count = 0;
    *root = 0;
    const unsigned char *data = input;
    if (!data || length < ARTIFACT_HEADER || length > PACKAGE_MAX_BYTES ||
        memcmp(data, magic, 8) || get(data + 8) != 1 || !get(data + 16) ||
        !get(data + 24) || get(data + 24) > PACKAGE_MAX_MODULES ||
        get(data + 24) > (length - ARTIFACT_HEADER) / (ARTIFACT_ROW + 2)) {
        fail(error, error_size, "invalid compiler artifact header");
        return NULL;
    }
    int rows_count = (int)get(data + 24);
    package_module *rows = xcalloc((size_t)rows_count, sizeof *rows);
    size_t offset = ARTIFACT_HEADER;
    bool found = false, valid = true;
    int at = 0;
    for (; at < rows_count && valid; at++) {
        if (length - offset < ARTIFACT_ROW) {
            valid = false;
            break;
        }
        uint64_t id = get(data + offset), name = get(data + offset + 8),
                 blob = get(data + offset + 16);
        offset += ARTIFACT_ROW;
        if (!id || !name || name > length - offset || !blob ||
            blob > length - offset - (size_t)name ||
            memchr(data + offset, 0, (size_t)name)) {
            valid = false;
            break;
        }
        char *text = xmalloc((size_t)name + 1);
        memcpy(text, data + offset, (size_t)name);
        text[name] = 0;
        offset += (size_t)name;
        rows[at] = (package_module){ text, id, data + offset, (size_t)blob };
        offset += (size_t)blob;
        if (at && strcmp(rows[at - 1].name, text) >= 0) {
            valid = false;
        }
        for (int j = 0; j < at; j++) {
            if (rows[j].id == id) {
                valid = false;
            }
        }
        found |= id == get(data + 16);
    }
    if (!valid || !found || offset != length) {
        fail(error, error_size, "invalid compiler artifact module records");
        compiler_artifact_rows_free(rows, at);
        return NULL;
    }
    *count = rows_count;
    *root = get(data + 16);
    return rows;
}
void compiler_artifact_rows_free(package_module *rows, int count)
{
    for (int i = 0; i < count; i++) {
        free((void *)rows[i].name);
    }
    free(rows);
}
package_collection *compiler_artifact_read(
    const void *input,
    size_t length,
    const execution_prepared_linker *linker,
    behavior_backend backend,
    size_t *remaining,
    uint64_t *root,
    char *error,
    size_t error_size
)
{
    if (root) {
        *root = 0;
    }
    if (!root || !remaining) {
        fail(error, error_size, "invalid compiler artifact header");
        return NULL;
    }
    int count = 0;
    uint64_t closure_root = 0;
    package_module *rows = compiler_artifact_rows(input, length, &count, &closure_root, error, error_size);
    if (!rows) {
        return NULL;
    }
    execution_prepared_linker closed = linker ? *linker
                                              : (execution_prepared_linker){ 0 };
    closed.import = NULL;
    package_collection *
        collection = package_read_budget(rows, count, &closed, NULL, backend, remaining, error, error_size);
    if (collection) {
        *root = closure_root;
    }
    compiler_artifact_rows_free(rows, count);
    return collection;
}
