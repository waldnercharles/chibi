#include "authoring/project_migration.h"

#include "assets/project.h"
#include "support/array.h"
#include "support/asset_json.h"
#include "support/asset_text.h"
#include "support/rooted_files.h"
#include "support/sha256.h"
#include "support/xalloc.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool fail(char *error, size_t size, const char *message)
{
    if (size) {
        snprintf(error, size, "%s", message);
    }
    return false;
}
static av_doc *manifest(rooted_files *files, char *error, size_t size)
{
    unsigned char *text = NULL;
    size_t bytes = 0;
    if (!rooted_files_read(files, "project.kdl", 65536, &text, &bytes) ||
        !bytes || memchr(text, 0, bytes)) {
        free(text);
        fail(error, size, "Expected a bounded regular UTF-8 project.kdl");
        return NULL;
    }
    av_doc *doc = av_doc_read((const char *)text, error, size);
    free(text);
    av_value *version = doc ? av_obj_get(av_doc_root(doc), "version") : NULL;
    if (!version || !av_is_int(version) || av_get_int(version) < 1 ||
        av_get_int(version) > 2147483647) {
        av_doc_free(doc);
        fail(error, size, "Project version must be a positive integer");
        return NULL;
    }
    return doc;
}
bool project_migration_version(const char *root, int *version, char *error, size_t size)
{
    *version = 0;
    if (size) {
        *error = 0;
    }
    if (!root || !*root) {
        return fail(error, size, "Select a project directory before checking its version");
    }
    rooted_files *files = rooted_files_open(root);
    av_doc *doc = files ? manifest(files, error, size) : NULL;
    if (!files) {
        fail(error, size, "Cannot open selected project directory");
    }
    if (doc) {
        *version = (int)av_get_int(av_obj_get(av_doc_root(doc), "version"));
    }
    av_doc_free(doc);
    if (files) {
        rooted_files_close(files);
    }
    return *version > 0;
}
static bool contained(av_value *root, const char *key, bool required)
{
    av_value *value = av_obj_get(root, key);
    if (!value) {
        return !required;
    }
    const char *path = av_get_str(value);
    return av_is_str(value) && path && strlen(path) <= 1024 &&
           !strchr(path, ':') && rooted_files_path_valid(path);
}
static bool remove_tree(rooted_files *files, int depth)
{
    if (depth > 17) {
        return false;
    }
    rooted_file_entry *entries = NULL;
    int count = 0;
    if (!rooted_files_list(files, "", 4096, &entries, &count)) {
        return false;
    }
    bool ok = true;
    for (int i = 0; i < count; i++) {
        if (entries[i].info.directory && !entries[i].info.link) {
            rooted_files *child = rooted_files_directory(files, entries[i].name, false);
            if (child) {
                ok = remove_tree(child, depth + 1) && ok;
                rooted_files_close(child);
            }
            ok = rooted_files_remove_directory(files, entries[i].name) && ok;
        } else if (entries[i].info.regular && !entries[i].info.link) {
            ok = rooted_files_remove(files, entries[i].name) && ok;
        }
    }
    rooted_file_entries_free(entries, count);
    return ok;
}
typedef struct
{
    size_t bytes;
    int entries;
} copy_budget;
static bool copy_tree(
    rooted_files *source,
    rooted_files *dest,
    int depth,
    copy_budget *budget,
    char *error,
    size_t size
)
{
    if (depth > 16) {
        return fail(error, size, "Migration copy exceeds directory depth 16");
    }
    rooted_file_entry *entries = NULL;
    int count = 0;
    if (!rooted_files_list(source, "", 4096, &entries, &count)) {
        return fail(error, size, "Cannot enumerate migration inputs within 4096 entries");
    }
    bool ok = true;
    for (int i = 0; ok && i < count; i++) {
        rooted_file_entry *entry = &entries[i];
        if (!depth &&
            (!strcmp(entry->name, ".git") || !strcmp(entry->name, ".chibi"))) {
            continue;
        }
        if (++budget->entries > 4096 || entry->info.link) {
            ok = fail(error, size, "Migration copy exceeds 4096 entries or contains links");
        } else if (entry->info.directory) {
            rooted_files *input = rooted_files_directory(source, entry->name, false);
            rooted_files *output = rooted_files_directory_new(dest, entry->name);
            ok = input && output &&
                 copy_tree(input, output, depth + 1, budget, error, size);
            if (input) {
                rooted_files_close(input);
            }
            if (output) {
                rooted_files_close(output);
            }
        } else if (entry->info.regular) {
            unsigned char *bytes = NULL;
            size_t length = 0;
            ok = rooted_files_read(source, entry->name, 16 * 1024 * 1024, &bytes, &length) &&
                 length <= 64 * 1024 * 1024 - budget->bytes &&
                 rooted_files_write(dest, entry->name, bytes, length, 16 * 1024 * 1024, false, NULL, 0);
            free(bytes);
            if (ok) {
                budget->bytes += length;
            }
        } else {
            ok = fail(error, size, "Migration copy refuses special files");
        }
    }
    rooted_file_entries_free(entries, count);
    if (!ok && size && !*error) {
        fail(error, size, "Migration input changed or copy IO/budget failed");
    }
    return ok;
}
typedef struct
{
    char *path;
    size_t bytes;
    char digest[65];
} file_digest;
static int digest_order(const void *left, const void *right)
{
    return strcmp(
        ((const file_digest *)left)->path,
        ((const file_digest *)right)->path
    );
}
static bool collect_digest(
    rooted_files *root,
    const char *directory,
    int depth,
    file_digest **rows,
    size_t *names,
    size_t *total
)
{
    if (depth > 16) {
        return false;
    }
    rooted_file_entry *entries = NULL;
    int count = 0;
    if (!rooted_files_list(root, directory, 4096, &entries, &count)) {
        return false;
    }
    bool ok = true;
    for (int i = 0; ok && i < count; i++) {
        const rooted_file_entry *entry = &entries[i];
        if (!strcmp(entry->name, ".chibi") ||
            (!depth && !strcmp(entry->name, ".git"))) {
            continue;
        }
        char path[1025];
        int length = snprintf(
            path,
            sizeof path,
            "%s%s%s",
            directory,
            *directory ? "/" : "",
            entry->name
        );
        ok = length > 0 && (size_t)length < sizeof path && !entry->info.link;
        if (!ok) {
            break;
        }
        if (entry->info.directory) {
            ok = collect_digest(root, path, depth + 1, rows, names, total);
            continue;
        }
        unsigned char *data = NULL;
        size_t bytes = 0;
        ok = entry->info.regular && acount(*rows) < 4096 &&
             (size_t)length <= 1048576 - *names &&
             rooted_files_read(root, path, 16777216, &data, &bytes) &&
             bytes <= 67108864 - *total;
        if (ok) {
            file_digest row = { .path = xstrdup(path), .bytes = bytes };
            sha256_hex(data, bytes, row.digest);
            apush(*rows, row);
            *names += (size_t)length;
            *total += bytes;
        }
        free(data);
    }
    rooted_file_entries_free(entries, count);
    return ok;
}
static bool project_digest(rooted_files *root, char hex[65])
{
    file_digest *rows = NULL;
    size_t names = 0, total = 0;
    bool ok = collect_digest(root, "", 0, &rows, &names, &total);
    if (ok) {
        qsort(rows, (size_t)acount(rows), sizeof *rows, digest_order);
        av_doc *doc = av_doc_new();
        av_value *list = av_arr(doc);
        av_doc_set_root(doc, list);
        for (int i = 0; i < acount(rows); i++) {
            av_value *row = av_obj(doc);
            av_obj_add_str(doc, row, "path", rows[i].path);
            av_obj_add_int(doc, row, "bytes", (int64_t)rows[i].bytes);
            av_obj_add_str(doc, row, "sha256", rows[i].digest);
            av_arr_append(list, row);
        }
        char *json = av_json_write(list);
        ok = json != NULL;
        if (ok) {
            sha256_hex(json, strlen(json), hex);
        }
        free(json);
        av_doc_free(doc);
    }
    for (int i = 0; i < acount(rows); i++) {
        free(rows[i].path);
    }
    afree(rows);
    return ok;
}
/* Compare directory identity while walking actual parent handles. Spelling
 * comparisons alone miss case-insensitive aliases and selected parent links. */
static bool parent_inside(rooted_files *source, rooted_files *parent)
{
    rooted_file_info original;
    if (!rooted_stream_info(source, &original)) {
        return true;
    }
    char *path = rooted_files_host_path(parent);
    for (int i = 0; path && i < 1024; i++) {
        rooted_files *current = rooted_files_open(path);
        rooted_file_info info;
        bool valid = current && rooted_stream_info(current, &info);
        if (current) {
            rooted_files_close(current);
        }
        if (!valid ||
            (original.device == info.device &&
             !memcmp(original.identity, info.identity, sizeof info.identity))) {
            free(path);
            return true;
        }
        char *leaf = NULL;
        rooted_files *above = rooted_files_open_parent(path, &leaf);
        char *next = above ? rooted_files_host_path(above) : NULL;
        if (above) {
            rooted_files_close(above);
        }
        free(leaf);
        if (!next || !strcmp(path, next)) {
            free(next);
            free(path);
            return false;
        }
        free(path);
        path = next;
    }
    free(path);
    return true;
}
bool project_migration_copy(
    const char *source,
    const char *parent,
    const char *name,
    char **result,
    char *error,
    size_t size
)
{
    *result = NULL;
    if (size) {
        *error = 0;
    }
    if (!source || !*source || !parent || !*parent) {
        return fail(error, size, "Select source and destination parent directories");
    }
    if (!name || !rooted_files_path_valid(name) || strchr(name, '/') ||
        strlen(name) > 128) {
        return fail(error, size, "Choose one valid destination folder name");
    }
    rooted_files *input = rooted_files_open(source),
                 *destination = rooted_files_open(parent);
    av_doc *doc = input ? manifest(input, error, size) : NULL;
    int from = doc ? (int)av_get_int(av_obj_get(av_doc_root(doc), "version")) : 0;
    bool ok = destination && doc && from >= CHIBI_PROJECT_OLDEST_VERSION &&
              from < CHIBI_PROJECT_VERSION &&
              contained(av_doc_root(doc), "content", false) &&
              contained(av_doc_root(doc), "start_scene", true);
    if (!ok) {
        fail(error, size, "Choose a supported older project with contained content and scene paths");
    }
    av_doc_free(doc);
    if (ok && parent_inside(input, destination)) {
        ok = fail(error, size, "Destination parent must be outside the original project");
    }
    rooted_files *copy = ok ? rooted_files_directory_new(destination, name) : NULL;
    if (ok && !copy) {
        ok = fail(error, size, "Destination already exists or cannot be created exclusively");
    }
    char *path = copy ? rooted_files_host_path(copy) : NULL;
    copy_budget budget = { 0 };
    ok = ok && path && copy_tree(input, copy, 0, &budget, error, size);
    char before_hash[65], after_hash[65];
    if (ok) {
        ok = project_digest(copy, before_hash);
    }
    if (ok) {
        ok = chibi_project_migrate(path, error, size) >= 0;
    }
    if (ok) {
        ok = project_digest(copy, after_hash);
    }
    if (!ok && size && !*error) {
        fail(error, size, "Could not produce bounded migration file receipts");
    }
    if (ok) {
        /* Original project version is recorded without putting host paths into
         * authored source. Detailed canonical diagnostics are job output. */
        char report[512];
        int n = snprintf(
            report,
            sizeof report,
            "Migration copy completed: version %d to %d; %d entries, %zu input "
            "bytes. Original project unchanged.\nSource files SHA256: "
            "%s\nResult files SHA256: %s\n",
            from,
            CHIBI_PROJECT_VERSION,
            budget.entries,
            budget.bytes,
            before_hash,
            after_hash
        );
        rooted_files *reports = rooted_files_directory(copy, ".chibi", true);
        ok = n > 0 && (size_t)n < sizeof report && reports &&
             rooted_files_write(reports, "migration-report.txt", report, (size_t)n, sizeof report, false, NULL, 0);
        if (reports) {
            rooted_files_close(reports);
        }
        if (!ok) {
            fail(error, size, "Could not retain migration report; copy was not accepted");
        }
    }
    rooted_file_info owned, named;
    bool same = copy && rooted_stream_info(copy, &owned) &&
                rooted_files_stat(destination, name, &named) &&
                owned.device == named.device &&
                !memcmp(owned.identity, named.identity, sizeof owned.identity);
    bool cleaned = !copy || ok || remove_tree(copy, 0);
    if (copy) {
        rooted_files_close(copy);
    }
    if (!ok && destination && path) {
        cleaned = same && rooted_files_remove_directory(destination, name) && cleaned;
    }
    if (input) {
        rooted_files_close(input);
    }
    if (destination) {
        rooted_files_close(destination);
    }
    if (!ok) {
        if (!cleaned && size) {
            snprintf(error, size, "Migration refused; incomplete private copy retained at %s", path);
        }
        free(path);
        return false;
    }
    *result = path;
    return true;
}
