#include "authoring/workspace_snapshot.h"

#include "assets/project.h"
#include "support/rooted_files.h"
#include "support/utf8.h"
#include "support/xalloc.h"

#include <errno.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _WIN32
#include <process.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

struct workspace_snapshot
{
    char *root;
    rooted_files *directory, *parent;
    char name[96];
    size_t bytes;
    int entries;
};
static bool fail(char *out, size_t size, const char *text)
{
    if (size) {
        snprintf(out, size, "%s", text);
    }
    return false;
}
const char *workspace_snapshot_root(const workspace_snapshot *s)
{
    return s->root;
}
static void remove_tree(rooted_files *root, int depth)
{
    if (depth > 34) {
        return;
    }
    rooted_file_entry *entries = NULL;
    int count = 0;
    if (!rooted_files_list(root, "", 8192, &entries, &count)) {
        return;
    }
    for (int i = 0; i < count; i++) {
        if (entries[i].info.directory && !entries[i].info.link) {
            rooted_files *child = rooted_files_directory(root, entries[i].name, false);
            if (child) {
                remove_tree(child, depth + 1);
                rooted_files_close(child);
                rooted_files_remove_directory(root, entries[i].name);
            }
        } else if (entries[i].info.regular && !entries[i].info.link) {
            rooted_files_remove(root, entries[i].name);
        }
    }
    rooted_file_entries_free(entries, count);
}
void workspace_snapshot_free(workspace_snapshot *s)
{
    if (!s) {
        return;
    }
    if (s->directory) {
        rooted_file_info owned, named;
        bool same = rooted_stream_info(s->directory, &owned) &&
                    rooted_files_stat(s->parent, s->name, &named) &&
                    owned.device == named.device &&
                    !memcmp(owned.identity, named.identity, sizeof owned.identity);
        remove_tree(s->directory, 0);
        rooted_files_close(s->directory);
        if (same) {
            rooted_files_remove_directory(s->parent, s->name);
        }
    }
    rooted_files_close(s->parent);
    free(s->root);
    free(s);
}
static rooted_files *temporary_parent()
{
#ifdef _WIN32
    wchar_t wide[32768];
    DWORD length = GetTempPathW(32768, wide);
    if (!length || length >= 32768) {
        return NULL;
    }
    int bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, NULL, 0, NULL, NULL);
    if (bytes <= 0 || bytes > 4096) {
        return NULL;
    }
    char *path = xmalloc((size_t)bytes);
    bool valid = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, path, bytes, NULL, NULL) ==
                 bytes;
    rooted_files *parent = valid ? rooted_files_open(path) : NULL;
    free(path);
    return parent;
#else
    const char *path = getenv("TMPDIR");
    return rooted_files_open(path && *path ? path : "/tmp");
#endif
}
static workspace_snapshot *temporary(char *out, size_t size)
{
    static atomic_uint sequence;
    workspace_snapshot *s = xcalloc(1, sizeof *s);
    s->parent = temporary_parent();
    struct timespec now = { 0 };
    timespec_get(&now, TIME_UTC);
#ifdef _WIN32
    unsigned process = (unsigned)_getpid();
#else
    unsigned process = (unsigned)getpid();
#endif
    for (int i = 0; s->parent && i < 64; i++) {
        unsigned next = atomic_fetch_add(&sequence, 1);
        snprintf(
            s->name,
            sizeof s->name,
            "chibi-runtime-%u-%llx-%u",
            process,
            (unsigned long long)now.tv_sec * 1000000000ull +
                (unsigned long long)now.tv_nsec,
            next
        );
        s->directory = rooted_files_directory_new(s->parent, s->name);
        if (s->directory || errno != EEXIST) {
            break;
        }
    }
    if (s->directory) {
        s->root = rooted_files_host_path(s->directory);
    }
    if (!s->root) {
        workspace_snapshot_free(s);
        fail(out, size, "Cannot create an exclusive private preview snapshot");
        return NULL;
    }
    return s;
}
static bool copy_file(
    workspace_snapshot *s,
    rooted_files *source,
    rooted_files *dest,
    const char *name,
    char *out,
    size_t size
)
{
    unsigned char *bytes = NULL;
    size_t length = 0;
    bool ok = rooted_files_read(source, name, 16 * 1024 * 1024, &bytes, &length);
    if (!ok || length > 64 * 1024 * 1024 - s->bytes) {
        free(bytes);
        return fail(out, size, "Preview snapshot refuses unreadable, linked, changing, special or oversized inputs");
    }
    ok = rooted_files_write(dest, name, bytes, length, 16 * 1024 * 1024, false, NULL, 0);
    free(bytes);
    if (!ok) {
        return fail(out, size, "Preview snapshot file could not be published");
    }
    s->bytes += length;
    return true;
}
static bool copy_directory(
    workspace_snapshot *s,
    rooted_files *source,
    rooted_files *dest,
    int depth,
    char *out,
    size_t size
)
{
    if (depth > 16) {
        return fail(out, size, "Preview snapshot directory depth exceeds 16");
    }
    rooted_file_entry *entries = NULL;
    int count = 0;
    if (!rooted_files_list(source, "", 4096, &entries, &count)) {
        return fail(out, size, "Cannot enumerate bounded preview inputs");
    }
    bool ok = true;
    for (int i = 0; ok && i < count; i++) {
        const rooted_file_entry *entry = &entries[i];
        if (++s->entries > 4096 || entry->info.link) {
            ok = fail(out, size, "Preview snapshot exceeds 4096 entries or contains a link");
        } else if (entry->info.directory) {
            rooted_files *input = rooted_files_directory(source, entry->name, false);
            rooted_files *output = rooted_files_directory_new(dest, entry->name);
            ok = input && output &&
                 copy_directory(s, input, output, depth + 1, out, size);
            rooted_files_close(input);
            rooted_files_close(output);
        } else if (entry->info.regular) {
            ok = copy_file(s, source, dest, entry->name, out, size);
        } else {
            ok = fail(out, size, "Preview snapshot refuses nonregular inputs");
        }
    }
    rooted_file_entries_free(entries, count);
    if (!ok && size && !*out) {
        fail(out, size, "Preview directory changed or could not be copied");
    }
    return ok;
}
static bool overlay(workspace_snapshot *s, const workspace_document_view *view, char *out, size_t size)
{
    int depth = 1;
    for (const char *p = view->path; *p; p++) {
        depth += *p == '/';
    }
    if (depth > 16 || !rooted_files_path_valid(view->path) ||
        strlen(view->path) > WORKSPACE_DOCUMENT_PATH_MAX ||
        view->bytes > 64 * 1024 * 1024 - s->bytes) {
        return fail(out, size, "Preview document overlay exceeds path/storage limits");
    }
    char path[WORKSPACE_DOCUMENT_PATH_MAX + 1];
    strcpy(path, view->path);
    char *slash = strrchr(path, '/');
    if (slash) {
        *slash = 0;
        rooted_files *parent = rooted_files_directory(s->directory, path, true);
        if (!parent) {
            return fail(out, size, "Cannot create preview overlay parent");
        }
        rooted_files_close(parent);
    }
    rooted_file_info info;
    bool replace = rooted_files_stat(s->directory, view->path, &info);
    unsigned char *before = NULL;
    size_t bytes = 0;
    if ((!replace && errno != ENOENT) ||
        (replace &&
         !rooted_files_read(s->directory, view->path, 16 * 1024 * 1024, &before, &bytes))) {
        return fail(out, size, "Cannot inspect private preview overlay baseline");
    }
    bool ok = rooted_files_write(
        s->directory,
        view->path,
        view->text,
        view->bytes,
        WORKSPACE_DOCUMENT_BYTES_MAX,
        replace,
        before,
        bytes
    );
    free(before);
    if (!ok) {
        return fail(out, size, "Could not publish exact preview document overlay");
    }
    s->bytes += view->bytes;
    return true;
}
workspace_snapshot *workspace_snapshot_new(
    const char *root,
    workspace_documents *documents,
    char *out,
    size_t size
)
{
    if (size) {
        *out = 0;
    }
    rooted_files *source = root ? rooted_files_open(root) : NULL;
    if (!source) {
        fail(out, size, "Open a valid workspace before freezing preview inputs");
        return NULL;
    }
    workspace_snapshot *s = temporary(out, size);
    bool ok = s && copy_file(s, source, s->directory, "project.kdl", out, size);
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    int count = workspace_document_handles(documents, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    for (int i = 0; ok && i < count; i++) {
        workspace_document_view view;
        if (workspace_document_read(documents, handles[i], &view) || view.transaction) {
            ok = fail(out, size, "Finish document gestures before freezing preview inputs");
        } else if (!strcmp(view.path, "project.kdl")) {
            ok = overlay(s, &view, out, size);
        }
    }
    char *content = ok ? chibi_project_content(s->root, out, size) : NULL;
    const char *relative = content && !strncmp(content, s->root, strlen(s->root)) &&
                                   content[strlen(s->root)] == '/'
                             ? content + strlen(s->root) + 1
                             : NULL;
    int content_depth = 1;
    for (const char *p = relative; p && *p; p++) {
        content_depth += *p == '/';
    }
    if (content_depth > 16) {
        relative = NULL;
        fail(out, size, "Preview content path depth exceeds 16");
    }
    rooted_files *input = relative ? rooted_files_directory(source, relative, false)
                                   : NULL;
    rooted_files *output = relative
                             ? rooted_files_directory(s->directory, relative, true)
                             : NULL;
    ok = ok && input && output && copy_directory(s, input, output, 0, out, size);
    rooted_files_close(input);
    rooted_files_close(output);
    free(content);
    for (int i = 0; ok && i < count; i++) {
        workspace_document_view view;
        if (workspace_document_read(documents, handles[i], &view)) {
            ok = false;
        } else if (!*view.path) {
            if (view.dirty || view.bytes) {
                ok = fail(out, size, "Save As is required for unnamed preview inputs");
            }
        } else if (strcmp(view.path, "project.kdl")) {
            ok = overlay(s, &view, out, size);
        }
    }
    rooted_files_close(source);
    if (!ok) {
        if (size && !*out) {
            fail(out, size, "Preview snapshot could not be prepared");
        }
        workspace_snapshot_free(s);
        return NULL;
    }
    return s;
}
workspace_snapshot *workspace_snapshot_new_sources(
    const char *root,
    workspace_documents *documents,
    const char *const *paths,
    const char *const *texts,
    int count,
    char *out,
    size_t size
)
{
    if (count < 0 || count > 16 || (count && (!paths || !texts))) {
        fail(out, size, "Private preview sources require at most 16 path/source pairs");
        return NULL;
    }
    size_t total = 0;
    for (int i = 0; i < count; i++) {
        size_t bytes = texts[i] ? strlen(texts[i]) : 0;
        if (!paths[i] || strlen(paths[i]) > WORKSPACE_DOCUMENT_PATH_MAX ||
            !rooted_files_path_valid(paths[i]) || !texts[i] ||
            bytes > 1024u * 1024u || !utf8_text_valid(texts[i], bytes) ||
            total + bytes > 4u * 1024u * 1024u) {
            fail(out, size, "Private preview sources exceed relative path or UTF-8 storage limits");
            return NULL;
        }
        int depth = 1;
        for (const char *p = paths[i]; *p; p++) {
            depth += *p == '/';
        }
        if (depth > 16) {
            fail(out, size, "Private preview source path depth exceeds 16");
            return NULL;
        }
        total += bytes;
        for (int j = 0; j < i; j++) {
            if (!strcmp(paths[i], paths[j])) {
                fail(out, size, "Private preview source paths must be distinct");
                return NULL;
            }
        }
    }
    workspace_snapshot *s = workspace_snapshot_new(root, documents, out, size);
    if (!s) {
        return NULL;
    }
    if (total > 64u * 1024u * 1024u - s->bytes || s->entries > 8192 - count) {
        fail(out, size, "Private preview sources exceed snapshot storage limits");
        workspace_snapshot_free(s);
        return NULL;
    }
    for (int i = 0; i < count; i++) {
        char parent_path[WORKSPACE_DOCUMENT_PATH_MAX + 1];
        strcpy(parent_path, paths[i]);
        char *slash = strrchr(parent_path, '/');
        if (slash) {
            *slash = 0;
            rooted_files *parent = rooted_files_directory(s->directory, parent_path, true);
            if (!parent) {
                fail(out, size, "Cannot create private preview source parent");
                workspace_snapshot_free(s);
                return NULL;
            }
            rooted_files_close(parent);
        }
        size_t bytes = strlen(texts[i]);
        if (!rooted_files_write(s->directory, paths[i], texts[i], bytes,
                                1024u * 1024u, false, NULL, 0)) {
            fail(out, size, "Private preview source conflicts with frozen inputs");
            workspace_snapshot_free(s);
            return NULL;
        }
        s->bytes += bytes;
        s->entries++;
    }
    return s;
}
