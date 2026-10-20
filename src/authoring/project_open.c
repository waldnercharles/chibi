#include "authoring/project_open.h"

#include "authoring/component_declarations.h"
#include "authoring/language_catalog.h"
#include "authoring/source_catalog.h"
#include "authoring/source_minimize.h"
#include "support/array.h"
#include "support/file_io.h"
#include "support/xalloc.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct
{
    char *path, *before, *after, *staged;
} migration_file;

static bool stage(migration_file *file, char *error, size_t size)
{
    struct stat info;
    if (lstat(file->path, &info) || !S_ISREG(info.st_mode)) {
        goto fail;
    }
    size_t bytes = strlen(file->path) + sizeof ".migrating.XXXXXX";
    file->staged = xmalloc(bytes);
    snprintf(file->staged, bytes, "%s.migrating.XXXXXX", file->path);
    int fd = mkstemp(file->staged);
    if (fd < 0) {
        goto fail;
    }
    bool okay = !fchmod(fd, info.st_mode & 07777);
    const char *text = file->after;
    size_t left = strlen(text);
    while (okay && left) {
        ssize_t written = write(fd, text, left);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            okay = false;
            break;
        }
        text += written;
        left -= (size_t)written;
    }
    if (okay && fsync(fd)) {
        okay = false;
    }
    if (close(fd)) {
        okay = false;
    }
    if (okay) {
        return true;
    }
fail:
    if (size) {
        snprintf(error, size, "Cannot stage source migration: %s", file->path);
    }
    return false;
}

static bool same_diagnostic(const source_catalog_diagnostic *a, const source_catalog_diagnostic *b)
{
    return !strcmp(a->path, b->path) && !strcmp(a->message, b->message) &&
           a->range.start == b->range.start && a->range.end == b->range.end;
}

static bool catalog_valid(
    const source_catalog *catalog,
    const source_catalog *baseline,
    char *error,
    size_t size
)
{
    const source_catalog_diagnostic *d = NULL;
    if (catalog) {
        for (int i = 0; i < source_catalog_diagnostic_count(catalog); i++) {
            const source_catalog_diagnostic *candidate = source_catalog_diagnostic_at(catalog, i);
            bool existing = false;
            /* Missing declarations are pre-existing project incompleteness,
             * not permission to publish a new or moved diagnostic. Bounds,
             * malformed documents and registration conflicts still abort. */
            if (baseline && !strncmp(candidate->message, "Unknown ", 8)) {
                for (int j = 0; j < source_catalog_diagnostic_count(baseline); j++) {
                    existing |= same_diagnostic(candidate, source_catalog_diagnostic_at(baseline, j));
                }
            }
            if (!existing) {
                d = candidate;
                break;
            }
        }
        if (!d) {
            return true;
        }
    }
    if (size) {
        snprintf(
            error,
            size,
            "Cannot finalize source migration%s%s: %s",
            d ? " in " : "",
            d ? d->path : "",
            d ? d->message : "catalog unavailable"
        );
    }
    return false;
}

/* Even an unrelated invalid handler must not hide a retired native call.
 * This conservative CST check never rewrites or guesses lexical resolution. */
static bool retired_call(const script_syntax *syntax, TSNode node, const script_catalog *catalog)
{
    if (!strcmp(ts_node_type(node), "call_expression")) {
        TSNode target = ts_node_child_by_field_name(node, "function", 8);
        if (!ts_node_is_null(target)) {
            size_t start = ts_node_start_byte(target),
                   length = ts_node_end_byte(target) - start;
            char *name = xmalloc(length + 1);
            memcpy(name, syntax->text + start, length);
            name[length] = 0;
            for (int i = 0; i < catalog->capability_count; i++) {
                const script_catalog_capability *cap = &catalog->capabilities[i];
                bool retired = cap->id == script_catalog_id("capability", "Self") ||
                               (cap->component &&
                                (cap->component_access == SCRIPT_COMPONENT_ACCESS_READ ||
                                 cap->component_access == SCRIPT_COMPONENT_ACCESS_WRITE ||
                                 cap->component_access == SCRIPT_COMPONENT_ACCESS_HAS));
                if (!retired) {
                    continue;
                }
                const char *names[] = { cap->name, cap->qualified_name };
                for (int j = 0; j < 2; j++) {
                    if (names[j] && script_name_matches(name, names[j])) {
                        free(name);
                        return true;
                    }
                }
            }
            free(name);
        }
    }
    for (uint32_t i = 0; i < ts_node_child_count(node); i++) {
        if (retired_call(syntax, ts_node_child(node, i), catalog)) {
            return true;
        }
    }
    return false;
}

static int finalize(const char *content, const char *start_scene, int from, void *user, char *error, size_t size)
{
    (void)start_scene;
    (void)from;
    native_capabilities *native = user;
    declared_components *declared = NULL;
    authoring_language_catalog *language = NULL;
    source_catalog *catalog = NULL, *validated = NULL;
    migration_file *files = NULL;
    source_catalog_edit *edits = NULL;
    int result = -1;
    if (!authoring_component_declarations_load(
            content,
            native_capabilities_catalog(native),
            &declared,
            error,
            size
        )) {
        goto done;
    }
    language = authoring_language_catalog_new(
        &(authoring_language_catalog_input){ .native = native },
        error,
        size
    );
    if (!language) {
        goto done;
    }
    catalog = source_catalog_new_for_migration(content, authoring_language_catalog_view(language));
    if (!catalog_valid(catalog, catalog, error, size)) {
        goto done;
    }
    for (int i = 0; i < source_catalog_source_count(catalog); i++) {
        const source_catalog_source *source = source_catalog_source_at(catalog, i);
        /* A project upgrade operates on disk, never on an editor overlay. */
        char *disk = file_read_text(source->path);
        bool matches_disk = disk && !strcmp(disk, source->text);
        free(disk);
        if (!matches_disk) {
            if (size) {
                snprintf(
                    error,
                    size,
                    "Source migration snapshot differs from disk: %s; save or "
                    "close pending edits first",
                    source->path
                );
            }
            goto done;
        }
        script_analysis *ordinary = script_analyze_document(
            source->syntax,
            source_catalog_view(catalog),
            source->identity
        );
        bool unchanged_source = !source->syntax->diagnostic_count &&
                                !retired_call(
                                    source->syntax,
                                    ts_tree_root_node(source->syntax->tree),
                                    source_catalog_view(catalog)
                                );
        for (int d = 0; unchanged_source && d < ordinary->diagnostic_count; d++) {
            const char *message = ordinary->diagnostics[d].message;
            if (strstr(message, "Generated component accessors") ||
                strstr(message, "Use self instead")) {
                unchanged_source = false;
            }
        }
        script_analysis_free(ordinary);
        char normalization_error[512] = { 0 };
        char *after = unchanged_source ? xstrdup(source->text)
                                       : authoring_source_migrate_generated(
                                             source->text,
                                             source_catalog_view(catalog),
                                             source->identity,
                                             normalization_error,
                                             sizeof normalization_error
                                         );
        if (!after) {
            if (size) {
                snprintf(
                    error,
                    size,
                    "Cannot migrate %s: %s",
                    source->path,
                    normalization_error[0] ? normalization_error : "generated-call normalization could not establish equivalence"
                );
            }
            goto done;
        }
        apush(edits, ((source_catalog_edit){ source->path, after, NULL }));
        if (strcmp(source->text, after)) {
            apush(
                files,
                ((
                    migration_file
                ){ xstrdup(source->path), xstrdup(source->text), xstrdup(after), NULL })
            );
        }
    }
    validated = source_catalog_replaced(
        content,
        authoring_language_catalog_view(language),
        edits,
        acount(edits)
    );
    if (!catalog_valid(validated, catalog, error, size)) {
        goto done;
    }
    for (int i = 0; i < acount(files); i++) {
        if (!stage(&files[i], error, size)) {
            goto done;
        }
    }
    /* Detect concurrent edits before replacing any member of the staged set. */
    for (int i = 0; i < acount(files); i++) {
        char *current = file_read_text(files[i].path);
        bool same = current && !strcmp(current, files[i].before);
        free(current);
        if (!same) {
            if (size) {
                snprintf(
                    error,
                    size,
                    "Source changed during migration: %s",
                    files[i].path
                );
            }
            goto done;
        }
    }
    for (int i = 0; i < acount(files); i++) {
        if (rename(files[i].staged, files[i].path)) {
            if (size) {
                snprintf(
                    error,
                    size,
                    "Cannot publish source migration: %s; retry migration",
                    files[i].path
                );
            }
            goto done;
        }
    }
    result = 0;
done:
    for (int i = 0; i < acount(files); i++) {
        if (files[i].staged) {
            unlink(files[i].staged);
        }
        free(files[i].path);
        free(files[i].before);
        free(files[i].after);
        free(files[i].staged);
    }
    for (int i = 0; i < acount(edits); i++) {
        free((char *)edits[i].text);
    }
    afree(files);
    afree(edits);
    source_catalog_free(validated);
    source_catalog_free(catalog);
    authoring_language_catalog_free(language);
    declared_components_free(declared);
    return result;
}

int authoring_project_open(
    chibi_project *project,
    const char *path,
    native_capabilities *native,
    char *error,
    size_t size
)
{
    if (!native) {
        if (size) {
            snprintf(error, size, "Project migration requires an explicit native capability table");
        }
        return -1;
    }
    return chibi_project_open_migrating(project, path, finalize, native, error, size);
}

int authoring_project_migrate(const char *path, native_capabilities *native, char *error, size_t size)
{
    if (!native) {
        if (size) {
            snprintf(error, size, "Project migration requires an explicit native capability table");
        }
        return -1;
    }
    return chibi_project_migrate_finalized(path, finalize, native, error, size);
}
int authoring_project_open_content(
    chibi_project *project,
    const char *path,
    native_capabilities *native,
    char *error,
    size_t size
)
{
    if (!native) {
        if (size) {
            snprintf(error, size, "Project migration requires an explicit native capability table");
        }
        return -1;
    }
    return chibi_project_open_content_migrating(project, path, finalize, native, error, size);
}
