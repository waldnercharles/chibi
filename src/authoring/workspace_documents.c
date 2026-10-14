#include "authoring/workspace_documents.h"

#include "support/rooted_files.h"
#include "support/utf8.h"
#include "support/xalloc.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    char *text;
    size_t bytes;
    uint64_t age, group;
} document_text;
typedef struct
{
    int handle, revision;
    char *path;
    document_text current, saved, beginning;
    bool transaction, unsaved;
    document_text undo[WORKSPACE_DOCUMENT_HISTORY_MAX],
        redo[WORKSPACE_DOCUMENT_HISTORY_MAX];
    int undo_count, redo_count;
} document;
struct workspace_documents
{
    document documents[WORKSPACE_DOCUMENT_COUNT_MAX];
    int next_handle;
    rooted_files *root;
    uint64_t history_clock, next_group;
    bool destroying;
    size_t current_bytes, saved_bytes, history_bytes;
};

const char *workspace_document_status_text(workspace_document_status status)
{
    switch (status) {
        case WORKSPACE_DOCUMENT_OK: return "";
        case WORKSPACE_DOCUMENT_INVALID:
            return "Invalid document, path or edit range";
        case WORKSPACE_DOCUMENT_STALE: return "Document revision has changed";
        case WORKSPACE_DOCUMENT_LIMIT:
            return "Workspace document limit exceeded";
        case WORKSPACE_DOCUMENT_UTF8:
            return "Document text must be valid UTF-8 without NUL bytes";
        case WORKSPACE_DOCUMENT_IO:
            return "Workspace document file operation failed";
        case WORKSPACE_DOCUMENT_CONFLICT:
            return "Disk content differs from the opened or saved baseline";
        case WORKSPACE_DOCUMENT_TRANSACTION:
            return "Document transaction state does not permit this operation";
        case WORKSPACE_DOCUMENT_EMPTY_HISTORY:
            return "No document history in this direction";
        case WORKSPACE_DOCUMENT_NOT_FILE:
            return "Document has no workspace file";
        case WORKSPACE_DOCUMENT_EXISTS:
            return "Workspace destination already exists";
        case WORKSPACE_DOCUMENT_DIRTY:
            return "Save or discard document edits before renaming or removing "
                   "its file";
        case WORKSPACE_DOCUMENT_UNAVAILABLE:
            return "Workspace document service or disk backend unavailable";
    }
    return "Unknown workspace document status";
}
static workspace_document_status text_size(const char *text, size_t *bytes)
{
    if (!text) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    size_t n = strnlen(text, WORKSPACE_DOCUMENT_BYTES_MAX + 1u);
    if (n > WORKSPACE_DOCUMENT_BYTES_MAX) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    if (!utf8_text_valid(text, n)) {
        return WORKSPACE_DOCUMENT_UTF8;
    }
    *bytes = n;
    return WORKSPACE_DOCUMENT_OK;
}
static document_text copy_text(const char *text, size_t bytes)
{
    char *copy = xmalloc(bytes + 1);
    memcpy(copy, text, bytes);
    copy[bytes] = 0;
    return (document_text){ .text = copy, .bytes = bytes };
}
static bool equal_text(document_text a, document_text b)
{
    return a.bytes == b.bytes && !memcmp(a.text, b.text, a.bytes);
}
static document *find(const workspace_documents *service, int handle)
{
    if (!service || handle <= 0) {
        return NULL;
    }
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        if (service->documents[i].handle == handle) {
            return (document *)&service->documents[i];
        }
    }
    return NULL;
}
static workspace_document_status checked(
    workspace_documents *service,
    int handle,
    int revision,
    document **out
)
{
    if (!service) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    document *d = find(service, handle);
    if (!d) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    if (d->revision != revision) {
        return WORKSPACE_DOCUMENT_STALE;
    }
    *out = d;
    return WORKSPACE_DOCUMENT_OK;
}
static size_t current_charge(const document *d)
{
    return d->transaction && d->beginning.bytes > d->current.bytes
             ? d->beginning.bytes
             : d->current.bytes;
}
enum
{
    HISTORY_GROUP_MAX = WORKSPACE_DOCUMENT_COUNT_MAX * WORKSPACE_DOCUMENT_HISTORY_MAX * 2
};
static bool group_in(const uint64_t *groups, int count, uint64_t group)
{
    for (int i = 0; i < count; i++) {
        if (groups[i] == group) {
            return true;
        }
    }
    return false;
}
/* Redo snapshots newer than a removed group depend on its resulting state.
 * Invalidating them also invalidates their shared groups, transitively. */
static int group_closure(const workspace_documents *service, uint64_t group, uint64_t *groups)
{
    int count = 1;
    groups[0] = group;
    for (int at = 0; at < count; at++) {
        for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
            const document *d = &service->documents[i];
            for (int j = 0; j < d->redo_count; j++) {
                if (d->redo[j].group == groups[at]) {
                    for (int k = 0; k < j; k++) {
                        uint64_t dependent = d->redo[k].group;
                        if (dependent && !group_in(groups, count, dependent)) {
                            assert(count < HISTORY_GROUP_MAX);
                            groups[count++] = dependent;
                        }
                    }
                }
            }
        }
    }
    return count;
}
static bool group_conflict(const workspace_documents *service, const document *owner, uint64_t group)
{
    if (!group || service->destroying) {
        return false;
    }
    bool active = false;
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        active |= service->documents[i].transaction && &service->documents[i] != owner;
    }
    if (!active) {
        return false;
    }
    uint64_t groups[HISTORY_GROUP_MAX];
    int count = group_closure(service, group, groups);
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        const document *d = &service->documents[i];
        if (!d->transaction || d == owner) {
            continue;
        }
        for (int j = 0; j < d->redo_count; j++) {
            if (d->redo[j].group && group_in(groups, count, d->redo[j].group)) {
                return true;
            }
        }
    }
    return false;
}
static bool group_reserved(const workspace_documents *service, uint64_t group)
{
    return group_conflict(service, NULL, group);
}
static bool history_conflict(
    const workspace_documents *service,
    const document *owner,
    const document_text *items,
    int count
)
{
    for (int i = 0; i < count; i++) {
        if (group_conflict(service, owner, items[i].group)) {
            return true;
        }
    }
    return false;
}
static void remove_history_at(workspace_documents *service, document_text *items, int *count, int index)
{
    service->history_bytes -= items[index].bytes;
    free(items[index].text);
    (*count)--;
    memmove(items + index, items + index + 1, (size_t)(*count - index) * sizeof *items);
}
static void remove_group(workspace_documents *service, uint64_t group)
{
    uint64_t groups[HISTORY_GROUP_MAX];
    int count = group_closure(service, group, groups);
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        document *d = &service->documents[i];
        for (int j = d->undo_count - 1; j >= 0; j--) {
            if (d->undo[j].group && group_in(groups, count, d->undo[j].group)) {
                remove_history_at(service, d->undo, &d->undo_count, j);
            }
        }
        int prefix = -1;
        for (int j = 0; j < d->redo_count; j++) {
            if (d->redo[j].group && group_in(groups, count, d->redo[j].group)) {
                prefix = j;
            }
        }
        for (int j = prefix; j >= 0; j--) {
            remove_history_at(service, d->redo, &d->redo_count, j);
        }
    }
}
static void clear_history(workspace_documents *service, document_text *history, int *count)
{
    while (*count) {
        uint64_t group = history[*count - 1].group;
        if (group) {
            remove_group(service, group);
        } else {
            remove_history_at(service, history, count, *count - 1);
        }
    }
}
static size_t redo_bytes(const document *d)
{
    size_t bytes = 0;
    for (int i = 0; i < d->redo_count; i++) {
        bytes += d->redo[i].bytes;
    }
    return bytes;
}
static bool history_possible(const workspace_documents *service, size_t added)
{
    size_t reserved = 0;
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        const document *d = &service->documents[i];
        if (d->transaction) {
            reserved += d->beginning.bytes + redo_bytes(d);
        }
        for (int j = 0; j < d->undo_count; j++) {
            if (group_reserved(service, d->undo[j].group)) {
                reserved += d->undo[j].bytes;
            }
        }
        if (!d->transaction) {
            for (int j = 0; j < d->redo_count; j++) {
                if (group_reserved(service, d->redo[j].group)) {
                    reserved += d->redo[j].bytes;
                }
            }
        }
    }
    return reserved + added <= WORKSPACE_DOCUMENT_HISTORY_BYTES_MAX;
}
static void evict(workspace_documents *service, document_text *items, int *count)
{
    if (items[0].group) {
        remove_group(service, items[0].group);
    } else {
        remove_history_at(service, items, count, 0);
    }
}
static void history_room(workspace_documents *service, size_t added, document *target)
{
    if (target && target->undo_count == WORKSPACE_DOCUMENT_HISTORY_MAX) {
        evict(service, target->undo, &target->undo_count);
    }
    while (service->history_bytes + added > WORKSPACE_DOCUMENT_HISTORY_BYTES_MAX) {
        document *oldest = NULL;
        bool redo = false;
        int index = -1;
        uint64_t age = UINT64_MAX;
        for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
            document *candidate = &service->documents[i];
            for (int j = 0; j < candidate->undo_count; j++) {
                if (!group_reserved(service, candidate->undo[j].group) &&
                    candidate->undo[j].age < age) {
                    oldest = candidate;
                    redo = false;
                    index = j;
                    age = candidate->undo[j].age;
                }
            }
            if (!candidate->transaction) {
                for (int j = 0; j < candidate->redo_count; j++) {
                    if (!group_reserved(service, candidate->redo[j].group) &&
                        candidate->redo[j].age < age) {
                        oldest = candidate;
                        redo = true;
                        index = j;
                        age = candidate->redo[j].age;
                    }
                }
            }
        }
        assert(oldest && index >= 0);
        document_text *items = redo ? oldest->redo : oldest->undo;
        int *count = redo ? &oldest->redo_count : &oldest->undo_count;
        if (items[index].group) {
            remove_group(service, items[index].group);
        } else {
            remove_history_at(service, items, count, index);
        }
    }
}
workspace_documents *workspace_documents_new(const char *path, workspace_document_status *status)
{
    if (status) {
        *status = WORKSPACE_DOCUMENT_OK;
    }
    rooted_files *root = path ? rooted_files_open(path) : NULL;
    if (path && !root) {
        if (status) {
            *status = WORKSPACE_DOCUMENT_IO;
        }
        return NULL;
    }
    workspace_documents *service = xcalloc(1, sizeof *service);
    service->root = root;
    service->next_handle = 1;
    return service;
}
void workspace_documents_free(workspace_documents *service)
{
    if (!service) {
        return;
    }
    service->destroying = true;
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        if (service->documents[i].handle) {
            workspace_document_close(service, service->documents[i].handle);
        }
    }
    if (service->root) {
        rooted_files_close(service->root);
    }
    free(service);
}
static workspace_document_status add(
    workspace_documents *service,
    const char *path,
    const char *text,
    size_t bytes,
    int *handle
)
{
    if (service->next_handle == INT_MAX ||
        service->current_bytes + bytes > WORKSPACE_DOCUMENT_TEXT_BYTES_MAX ||
        service->saved_bytes + bytes > WORKSPACE_DOCUMENT_TEXT_BYTES_MAX) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        document *d = &service->documents[i];
        if (d->handle) {
            continue;
        }
        *d = (document){ .handle = service->next_handle++,
                         .revision = 1,
                         .path = path ? xstrdup(path) : NULL,
                         .current = copy_text(text, bytes),
                         .saved = copy_text(text, bytes) };
        service->current_bytes += bytes;
        service->saved_bytes += bytes;
        *handle = d->handle;
        return WORKSPACE_DOCUMENT_OK;
    }
    return WORKSPACE_DOCUMENT_LIMIT;
}
workspace_document_status workspace_document_memory(
    workspace_documents *service,
    const char *text,
    int *handle
)
{
    if (!service) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    if (!handle) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    size_t bytes;
    workspace_document_status status = text_size(text, &bytes);
    return status ? status : add(service, NULL, text, bytes, handle);
}
static bool valid_path(const char *path)
{
    if (!path) {
        return false;
    }
    size_t size = strnlen(path, WORKSPACE_DOCUMENT_PATH_MAX + 1u);
    if (!size || size > WORKSPACE_DOCUMENT_PATH_MAX || path[0] == '/' ||
        !utf8_text_valid(path, size)) {
        return false;
    }
    const char *start = path;
    for (const char *p = path;; p++) {
        if (*p == '\\' || *p == ':' || ((unsigned char)*p < 32 && *p)) {
            return false;
        }
        if (*p && *p != '/') {
            continue;
        }
        size_t n = (size_t)(p - start);
        if (!n || (n == 1 && start[0] == '.') ||
            (n == 2 && start[0] == '.' && start[1] == '.')) {
            return false;
        }
        if (!*p) {
            return true;
        }
        start = p + 1;
    }
}
static bool same_path(const workspace_documents *service, const char *left, const char *right)
{
    return rooted_files_path_equal(service->root, left, right);
}
static workspace_document_status file_error()
{
    if (errno == EFBIG) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    if (errno == EEXIST) {
        return WORKSPACE_DOCUMENT_EXISTS;
    }
    if (errno == EBUSY) {
        return WORKSPACE_DOCUMENT_CONFLICT;
    }
    return WORKSPACE_DOCUMENT_IO;
}
static workspace_document_status disk_read(rooted_files *root, const char *path, document_text *out)
{
    unsigned char *bytes = NULL;
    size_t size = 0;
    if (!rooted_files_read(root, path, WORKSPACE_DOCUMENT_BYTES_MAX, &bytes, &size)) {
        return errno == EFBIG ? WORKSPACE_DOCUMENT_LIMIT : WORKSPACE_DOCUMENT_IO;
    }
    if (!utf8_text_valid((const char *)bytes, size)) {
        free(bytes);
        return WORKSPACE_DOCUMENT_UTF8;
    }
    *out = (document_text){ .text = (char *)bytes, .bytes = size };
    return WORKSPACE_DOCUMENT_OK;
}
workspace_document_status workspace_document_read_path(
    const workspace_documents *service,
    const char *path,
    char **text
)
{
    if (!text) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    *text = NULL;
    if (!service) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    if (!valid_path(path)) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        const document *d = &service->documents[i];
        if (d->handle && d->path && same_path(service, d->path, path)) {
            *text = xstrdup(d->current.text);
            return WORKSPACE_DOCUMENT_OK;
        }
    }
    if (!service->root) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    document_text snapshot = { 0 };
    workspace_document_status status = disk_read(service->root, path, &snapshot);
    if (!status) {
        *text = snapshot.text;
    }
    return status;
}

workspace_document_status workspace_document_path(
    const workspace_documents *service,
    const char *path,
    bool *exists,
    int *handle,
    int *revision
)
{
    if (!exists || !handle || !revision) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    *exists = false;
    *handle = 0;
    *revision = 0;
    if (!service) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    if (!valid_path(path)) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        const document *d = &service->documents[i];
        if (d->handle && d->path && same_path(service, d->path, path)) {
            *exists = true;
            *handle = d->handle;
            *revision = d->revision;
            return WORKSPACE_DOCUMENT_OK;
        }
    }
    if (!service->root) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    rooted_file_info info;
    if (rooted_files_stat(service->root, path, &info)) {
        if (!info.regular || info.link) {
            return WORKSPACE_DOCUMENT_NOT_FILE;
        }
        *exists = true;
        return WORKSPACE_DOCUMENT_OK;
    }
    return errno == ENOENT ? WORKSPACE_DOCUMENT_OK : file_error();
}

workspace_document_status workspace_document_open(
    workspace_documents *service,
    const char *path,
    int *handle
)
{
    if (!service) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    if (!handle || !valid_path(path)) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        document *d = &service->documents[i];
        if (d->handle && d->path && same_path(service, path, d->path)) {
            *handle = d->handle;
            return WORKSPACE_DOCUMENT_OK;
        }
    }
    if (!service->root) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    char *canonical = rooted_files_canonical_path(service->root, path);
    if (!canonical) {
        return file_error();
    }
    if (!valid_path(canonical)) {
        free(canonical);
        return WORKSPACE_DOCUMENT_INVALID;
    }
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        document *d = &service->documents[i];
        if (d->handle && d->path && same_path(service, d->path, canonical)) {
            *handle = d->handle;
            free(canonical);
            return WORKSPACE_DOCUMENT_OK;
        }
    }
    document_text text = { 0 };
    workspace_document_status status = disk_read(service->root, canonical, &text);
    if (!status) {
        status = add(service, canonical, text.text, text.bytes, handle);
    }
    free(text.text);
    free(canonical);
    return status;
}

workspace_document_status workspace_document_read(
    const workspace_documents *service,
    int handle,
    workspace_document_view *view
)
{
    if (!service) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    const document *d = find(service, handle);
    if (!d || !view) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    *view = (workspace_document_view){
        .document = handle,
        .revision = d->revision,
        .text = d->current.text,
        .path = d->path ? d->path : "",
        .bytes = d->current.bytes,
        .dirty = d->unsaved || !equal_text(d->current, d->saved),
        .transaction = d->transaction,
        .can_undo = !d->transaction && d->undo_count > 0,
        .can_redo = !d->transaction && d->redo_count > 0,
        .new_file = d->unsaved
    };
    return WORKSPACE_DOCUMENT_OK;
}
int workspace_document_handles(const workspace_documents *service, int *out, int capacity)
{
    if (!service || capacity < 0 || (capacity && !out)) {
        return 0;
    }
    int count = 0, handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        if (service->documents[i].handle) {
            handles[count++] = service->documents[i].handle;
        }
    }
    for (int i = 1; i < count; i++) {
        int value = handles[i], j = i;
        while (j && handles[j - 1] > value) {
            handles[j] = handles[j - 1];
            j--;
        }
        handles[j] = value;
    }
    int filled = count < capacity ? count : capacity;
    if (filled) {
        memcpy(out, handles, (size_t)filled * sizeof *out);
    }
    return count;
}
workspace_document_status workspace_document_close(workspace_documents *service, int handle)
{
    if (!service) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    document *d = find(service, handle);
    if (!d) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    if (history_conflict(service, d, d->undo, d->undo_count) ||
        history_conflict(service, d, d->redo, d->redo_count)) {
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    service->current_bytes -= current_charge(d);
    service->saved_bytes -= d->saved.bytes;
    free(d->current.text);
    free(d->saved.text);
    free(d->path);
    if (d->transaction) {
        service->history_bytes -= d->beginning.bytes;
        free(d->beginning.text);
    }
    clear_history(service, d->undo, &d->undo_count);
    clear_history(service, d->redo, &d->redo_count);
    *d = (document){ 0 };
    return WORKSPACE_DOCUMENT_OK;
}
static bool boundary(document_text text, int at)
{
    return at >= 0 && (size_t)at <= text.bytes &&
           ((size_t)at == text.bytes || ((unsigned char)text.text[at] & 0xc0) != 0x80);
}
static workspace_document_status validate_edit(
    workspace_documents *service,
    int handle,
    int revision,
    int start,
    int end,
    const char *text,
    document **out,
    size_t *bytes,
    size_t *result_bytes,
    bool *unchanged
)
{
    *unchanged = false;
    workspace_document_status status = checked(service, handle, revision, out);
    if (status) {
        return status;
    }
    document *d = *out;
    status = text_size(text, bytes);
    if (status) {
        return status;
    }
    if (end < start || !boundary(d->current, start) || !boundary(d->current, end)) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    *result_bytes = d->current.bytes - (size_t)(end - start) + *bytes;
    if (*result_bytes > WORKSPACE_DOCUMENT_BYTES_MAX ||
        service->current_bytes - current_charge(d) +
                (d->transaction && d->beginning.bytes > *result_bytes
                     ? d->beginning.bytes
                     : *result_bytes) >
            WORKSPACE_DOCUMENT_TEXT_BYTES_MAX) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    if (*bytes == (size_t)(end - start) &&
        !memcmp(d->current.text + start, text, *bytes)) {
        *unchanged = true;
        return WORKSPACE_DOCUMENT_OK;
    }
    if (d->revision >= INT_MAX - (d->transaction ? 1 : 0) ||
        (!d->transaction && (!history_possible(service, d->current.bytes) ||
                             service->history_clock == UINT64_MAX))) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    if (!d->transaction && history_conflict(service, d, d->redo, d->redo_count)) {
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    if (!d->transaction && d->undo_count == WORKSPACE_DOCUMENT_HISTORY_MAX &&
        group_reserved(service, d->undo[0].group)) {
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    return WORKSPACE_DOCUMENT_OK;
}
workspace_document_status workspace_document_validate_edit(
    workspace_documents *service,
    int handle,
    int revision,
    int start,
    int end,
    const char *text
)
{
    document *d;
    size_t bytes, result_bytes;
    bool unchanged;
    return validate_edit(service, handle, revision, start, end, text, &d, &bytes, &result_bytes, &unchanged);
}
workspace_document_status workspace_document_edit(
    workspace_documents *service,
    int handle,
    int revision,
    int start,
    int end,
    const char *text
)
{
    document *d;
    size_t bytes, result_bytes;
    bool unchanged;
    workspace_document_status
        status = validate_edit(service, handle, revision, start, end, text, &d, &bytes, &result_bytes, &unchanged);
    if (status || unchanged) {
        return status;
    }
    char *candidate = xmalloc(result_bytes + 1);
    memcpy(candidate, d->current.text, (size_t)start);
    memcpy(candidate + start, text, bytes);
    memcpy(candidate + start + bytes, d->current.text + end, d->current.bytes - (size_t)end + 1);
    service->current_bytes = service->current_bytes - current_charge(d) +
                             (d->transaction && d->beginning.bytes > result_bytes
                                  ? d->beginning.bytes
                                  : result_bytes);
    if (d->transaction) {
        free(d->current.text);
    } else {
        clear_history(service, d->redo, &d->redo_count);
        history_room(service, d->current.bytes, d);
        service->history_bytes += d->current.bytes;
        d->current.age = ++service->history_clock;
        d->undo[d->undo_count++] = d->current;
    }
    d->current = (document_text){ .text = candidate, .bytes = result_bytes };
    d->revision++;
    return WORKSPACE_DOCUMENT_OK;
}
workspace_document_status workspace_document_begin(workspace_documents *service, int handle, int revision)
{
    document *d;
    workspace_document_status status = checked(service, handle, revision, &d);
    if (status) {
        return status;
    }
    if (d->transaction) {
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    if (history_conflict(service, d, d->redo, d->redo_count) ||
        (d->undo_count == WORKSPACE_DOCUMENT_HISTORY_MAX &&
         group_reserved(service, d->undo[0].group))) {
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    if (!history_possible(service, d->current.bytes)) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    history_room(service, d->current.bytes, NULL);
    d->beginning = copy_text(d->current.text, d->current.bytes);
    service->history_bytes += d->current.bytes;
    d->transaction = true;
    return WORKSPACE_DOCUMENT_OK;
}
workspace_document_status workspace_document_commit(workspace_documents *service, int handle, int revision)
{
    document *d;
    workspace_document_status status = checked(service, handle, revision, &d);
    if (status) {
        return status;
    }
    if (!d->transaction) {
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    if (!equal_text(d->current, d->beginning) &&
        (history_conflict(service, d, d->redo, d->redo_count) ||
         (d->undo_count == WORKSPACE_DOCUMENT_HISTORY_MAX &&
          group_reserved(service, d->undo[0].group)))) {
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    if (!equal_text(d->current, d->beginning) && service->history_clock == UINT64_MAX) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    service->current_bytes -= current_charge(d) - d->current.bytes;
    if (equal_text(d->current, d->beginning)) {
        service->history_bytes -= d->beginning.bytes;
        free(d->beginning.text);
    } else {
        clear_history(service, d->redo, &d->redo_count);
        history_room(service, 0, d);
        d->beginning.age = ++service->history_clock;
        d->undo[d->undo_count++] = d->beginning;
    }
    d->beginning = (document_text){ 0 };
    d->transaction = false;
    return WORKSPACE_DOCUMENT_OK;
}
workspace_document_status workspace_document_cancel(workspace_documents *service, int handle, int revision)
{
    document *d;
    workspace_document_status status = checked(service, handle, revision, &d);
    if (status) {
        return status;
    }
    if (!d->transaction) {
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    bool changed = !equal_text(d->current, d->beginning);
    if (changed && d->revision == INT_MAX) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    service->current_bytes = service->current_bytes - current_charge(d) +
                             d->beginning.bytes;
    service->history_bytes -= d->beginning.bytes;
    free(d->current.text);
    d->current = d->beginning;
    d->beginning = (document_text){ 0 };
    d->transaction = false;
    if (changed) {
        d->revision++;
    }
    return WORKSPACE_DOCUMENT_OK;
}
static workspace_document_status history(workspace_documents *service, int handle, int revision, bool redo)
{
    document *selected;
    workspace_document_status status = checked(service, handle, revision, &selected);
    if (status) {
        return status;
    }
    if (selected->transaction) {
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    int count = redo ? selected->redo_count : selected->undo_count;
    document_text *items = redo ? selected->redo : selected->undo;
    if (!count) {
        return WORKSPACE_DOCUMENT_EMPTY_HISTORY;
    }
    uint64_t group = items[count - 1].group;
    document *members[WORKSPACE_DOCUMENT_COUNT_MAX];
    int member_count = 0;
    size_t current = service->current_bytes, historic = service->history_bytes;
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        document *d = &service->documents[i];
        bool member = d == selected;
        if (group) {
            for (int j = 0; j < d->undo_count; j++) {
                member |= d->undo[j].group == group;
            }
            for (int j = 0; j < d->redo_count; j++) {
                member |= d->redo[j].group == group;
            }
        }
        if (!member) {
            continue;
        }
        if (d->transaction) {
            return WORKSPACE_DOCUMENT_TRANSACTION;
        }
        document_text *from = redo ? d->redo : d->undo;
        int from_count = redo ? d->redo_count : d->undo_count;
        int to_count = redo ? d->undo_count : d->redo_count;
        if (!from_count || (group && from[from_count - 1].group != group)) {
            return WORKSPACE_DOCUMENT_CONFLICT;
        }
        if (d->revision == INT_MAX || to_count == WORKSPACE_DOCUMENT_HISTORY_MAX) {
            return WORKSPACE_DOCUMENT_LIMIT;
        }
        current = current - d->current.bytes + from[from_count - 1].bytes;
        historic = historic - from[from_count - 1].bytes + d->current.bytes;
        members[member_count++] = d;
    }
    if (current > WORKSPACE_DOCUMENT_TEXT_BYTES_MAX ||
        historic > WORKSPACE_DOCUMENT_HISTORY_BYTES_MAX ||
        (uint64_t)member_count > UINT64_MAX - service->history_clock) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    service->current_bytes = current;
    service->history_bytes = historic;
    for (int i = 0; i < member_count; i++) {
        document *d = members[i];
        document_text *from = redo ? d->redo : d->undo, *to = redo ? d->undo : d->redo;
        int *from_count = redo ? &d->redo_count : &d->undo_count,
            *to_count = redo ? &d->undo_count : &d->redo_count;
        document_text next = from[--(*from_count)];
        d->current.age = ++service->history_clock;
        d->current.group = next.group;
        to[(*to_count)++] = d->current;
        next.group = 0;
        d->current = next;
        d->revision++;
    }
    return WORKSPACE_DOCUMENT_OK;
}
workspace_document_status workspace_document_undo(workspace_documents *s, int h, int r)
{
    return history(s, h, r, false);
}
workspace_document_status workspace_document_redo(workspace_documents *s, int h, int r)
{
    return history(s, h, r, true);
}
workspace_document_status workspace_document_save(workspace_documents *service, int handle, int revision)
{
    document *d;
    workspace_document_status status = checked(service, handle, revision, &d);
    if (status) {
        return status;
    }
    if (d->transaction) {
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    if (!d->path) {
        return WORKSPACE_DOCUMENT_NOT_FILE;
    }
    if (service->saved_bytes - d->saved.bytes + d->current.bytes >
        WORKSPACE_DOCUMENT_TEXT_BYTES_MAX) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    if (!service->root) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    if (d->unsaved) {
        if (!rooted_files_write(
                service->root,
                d->path,
                d->current.text,
                d->current.bytes,
                WORKSPACE_DOCUMENT_BYTES_MAX,
                false,
                NULL,
                0
            )) {
            return file_error();
        }
        service->saved_bytes = service->saved_bytes - d->saved.bytes +
                               d->current.bytes;
        free(d->saved.text);
        d->saved = copy_text(d->current.text, d->current.bytes);
        d->unsaved = false;
        return WORKSPACE_DOCUMENT_OK;
    }
    document_text disk = { 0 };
    status = disk_read(service->root, d->path, &disk);
    if (!status && !equal_text(disk, d->saved)) {
        status = WORKSPACE_DOCUMENT_CONFLICT;
    }
    free(disk.text);
    if (status || equal_text(d->current, d->saved)) {
        return status;
    }
    if (!rooted_files_write(
            service->root,
            d->path,
            d->current.text,
            d->current.bytes,
            WORKSPACE_DOCUMENT_BYTES_MAX,
            true,
            d->saved.text,
            d->saved.bytes
        )) {
        return file_error();
    }
    service->saved_bytes = service->saved_bytes - d->saved.bytes + d->current.bytes;
    free(d->saved.text);
    d->saved = copy_text(d->current.text, d->current.bytes);
    return WORKSPACE_DOCUMENT_OK;
}

static bool reserved_path(const workspace_documents *service, const char *path)
{
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        if (service->documents[i].handle && service->documents[i].path &&
            same_path(service, service->documents[i].path, path)) {
            return true;
        }
    }
    return false;
}
static workspace_document_status can_add(workspace_documents *service, size_t bytes)
{
    if (service->next_handle == INT_MAX ||
        service->current_bytes + bytes > WORKSPACE_DOCUMENT_TEXT_BYTES_MAX ||
        service->saved_bytes + bytes > WORKSPACE_DOCUMENT_TEXT_BYTES_MAX) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        if (!service->documents[i].handle) {
            return WORKSPACE_DOCUMENT_OK;
        }
    }
    return WORKSPACE_DOCUMENT_LIMIT;
}
static workspace_document_status create_file(workspace_documents *service, const char *path, document_text text)
{
    return rooted_files_write(service->root, path, text.text, text.bytes, WORKSPACE_DOCUMENT_BYTES_MAX, false, NULL, 0)
             ? WORKSPACE_DOCUMENT_OK
             : file_error();
}
static workspace_document_status file_baseline(workspace_documents *service, const document *d)
{
    if (d->transaction) {
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    if (!d->path) {
        return WORKSPACE_DOCUMENT_NOT_FILE;
    }
    if (d->unsaved || !equal_text(d->current, d->saved)) {
        return WORKSPACE_DOCUMENT_DIRTY;
    }
    if (!service->root) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    document_text disk = { 0 };
    workspace_document_status status = disk_read(service->root, d->path, &disk);
    if (!status && !equal_text(disk, d->saved)) {
        status = WORKSPACE_DOCUMENT_CONFLICT;
    }
    free(disk.text);
    return status;
}
workspace_document_status workspace_document_draft(
    workspace_documents *service,
    const char *path,
    const char *text,
    int *handle
)
{
    if (!service) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    if (!handle || !valid_path(path)) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    if (reserved_path(service, path)) {
        return WORKSPACE_DOCUMENT_EXISTS;
    }
    size_t bytes;
    workspace_document_status status = text_size(text, &bytes);
    if (status) {
        return status;
    }
    status = can_add(service, bytes);
    if (status) {
        return status;
    }
    if (!service->root) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    rooted_file_info info;
    if (rooted_files_stat(service->root, path, &info)) {
        return WORKSPACE_DOCUMENT_EXISTS;
    }
    if (errno != ENOENT) {
        return file_error();
    }
    status = add(service, path, text, bytes, handle);
    if (!status) {
        document *d = find(service, *handle);
        d->unsaved = true;
        service->saved_bytes -= d->saved.bytes;
        free(d->saved.text);
        d->saved = copy_text("", 0);
    }
    return status;
}

workspace_document_status workspace_document_create(
    workspace_documents *service,
    const char *path,
    const char *text,
    int *handle
)
{
    if (!service) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    if (!handle || !valid_path(path)) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    if (reserved_path(service, path)) {
        return WORKSPACE_DOCUMENT_EXISTS;
    }
    size_t bytes;
    workspace_document_status status = text_size(text, &bytes);
    if (status) {
        return status;
    }
    status = can_add(service, bytes);
    if (status) {
        return status;
    }
    if (!service->root) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    /* Inputs may borrow this service; publication happens only after IO. */
    status = create_file(service, path, (document_text){ .text = (char *)text, .bytes = bytes });
    return status ? status : add(service, path, text, bytes, handle);
}
workspace_document_status workspace_document_save_as(
    workspace_documents *service,
    int handle,
    int revision,
    const char *path
)
{
    document *d;
    workspace_document_status status = checked(service, handle, revision, &d);
    if (status) {
        return status;
    }
    if (!valid_path(path)) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    if (d->transaction) {
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    if (service->saved_bytes - d->saved.bytes + d->current.bytes >
        WORKSPACE_DOCUMENT_TEXT_BYTES_MAX) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    if (reserved_path(service, path)) {
        return WORKSPACE_DOCUMENT_EXISTS;
    }
    if (d->revision == INT_MAX) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    if (!service->root) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    status = create_file(service, path, d->current);
    if (status) {
        return status;
    }
    char *new_path = xstrdup(path);
    free(d->path);
    d->path = new_path;
    service->saved_bytes = service->saved_bytes - d->saved.bytes + d->current.bytes;
    free(d->saved.text);
    d->saved = copy_text(d->current.text, d->current.bytes);
    d->unsaved = false;
    d->revision++;
    return WORKSPACE_DOCUMENT_OK;
}
workspace_document_status workspace_document_rename(
    workspace_documents *service,
    int handle,
    int revision,
    const char *path
)
{
    document *d;
    workspace_document_status status = checked(service, handle, revision, &d);
    if (status) {
        return status;
    }
    if (!valid_path(path)) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    if (reserved_path(service, path)) {
        return WORKSPACE_DOCUMENT_EXISTS;
    }
    if (d->revision == INT_MAX) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    status = file_baseline(service, d);
    if (status) {
        return status;
    }
    if (!rooted_files_move(service->root, d->path, path, false)) {
        return file_error();
    }
    char *new_path = xstrdup(path);
    free(d->path);
    d->path = new_path;
    d->revision++;
    return WORKSPACE_DOCUMENT_OK;
}
workspace_document_status workspace_document_remove(workspace_documents *service, int handle, int revision)
{
    document *d;
    workspace_document_status status = checked(service, handle, revision, &d);
    if (status) {
        return status;
    }
    status = file_baseline(service, d);
    if (status) {
        return status;
    }
    if (!rooted_files_remove(service->root, d->path)) {
        return file_error();
    }
    return workspace_document_close(service, handle);
}
workspace_document_status workspace_document_reload(workspace_documents *service, int handle, int revision)
{
    document *d;
    workspace_document_status status = checked(service, handle, revision, &d);
    if (status) {
        return status;
    }
    if (d->transaction) {
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    if (!d->path) {
        return WORKSPACE_DOCUMENT_NOT_FILE;
    }
    if (d->revision == INT_MAX) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    if (!service->root) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    document_text disk = { 0 };
    status = disk_read(service->root, d->path, &disk);
    if (status) {
        return status;
    }
    bool changed = !equal_text(d->current, disk);
    if (changed && (history_conflict(service, d, d->redo, d->redo_count) ||
                    (d->undo_count == WORKSPACE_DOCUMENT_HISTORY_MAX &&
                     group_reserved(service, d->undo[0].group)))) {
        free(disk.text);
        return WORKSPACE_DOCUMENT_TRANSACTION;
    }
    if (service->current_bytes - d->current.bytes + disk.bytes >
            WORKSPACE_DOCUMENT_TEXT_BYTES_MAX ||
        service->saved_bytes - d->saved.bytes + disk.bytes > WORKSPACE_DOCUMENT_TEXT_BYTES_MAX ||
        (changed && (!history_possible(service, d->current.bytes) ||
                     service->history_clock == UINT64_MAX))) {
        free(disk.text);
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    document_text saved = copy_text(disk.text, disk.bytes);
    if (changed) {
        clear_history(service, d->redo, &d->redo_count);
        history_room(service, d->current.bytes, d);
        service->history_bytes += d->current.bytes;
        d->current.age = ++service->history_clock;
        d->undo[d->undo_count++] = d->current;
    } else {
        free(d->current.text);
    }
    service->current_bytes = service->current_bytes - d->current.bytes + disk.bytes;
    service->saved_bytes = service->saved_bytes - d->saved.bytes + disk.bytes;
    free(d->saved.text);
    d->saved = saved;
    d->unsaved = false;
    d->current = disk;
    d->revision++;
    return WORKSPACE_DOCUMENT_OK;
}

typedef struct
{
    document *members[WORKSPACE_DOCUMENT_COUNT_MAX];
    document_text replacements[WORKSPACE_DOCUMENT_COUNT_MAX];
    int changed;
    size_t current, added;
} replacement_plan;
static workspace_document_status validate_replacements(
    workspace_documents *service,
    const workspace_document_replacement *requests,
    int count,
    int anchor,
    replacement_plan *plan
)
{
    *plan = (replacement_plan){ 0 };
    if (!service) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    if (count < 0 || count > WORKSPACE_DOCUMENT_COUNT_MAX || (count && !requests)) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    document **members = plan->members;
    document_text *replacements = plan->replacements;
    int changed = 0;
    document *anchor_document = NULL;
    bool anchor_changed = false;
    size_t current = service->current_bytes, added = 0;
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < i; j++) {
            if (requests[j].document == requests[i].document) {
                return WORKSPACE_DOCUMENT_INVALID;
            }
        }
        document *d;
        workspace_document_status
            status = checked(service, requests[i].document, requests[i].revision, &d);
        if (status) {
            return status;
        }
        if (d->handle == anchor) {
            anchor_document = d;
        }
        if (d->transaction || history_conflict(service, d, d->redo, d->redo_count)) {
            return WORKSPACE_DOCUMENT_TRANSACTION;
        }
        size_t bytes;
        status = text_size(requests[i].text, &bytes);
        if (status) {
            return status;
        }
        if (bytes == d->current.bytes &&
            !memcmp(requests[i].text, d->current.text, bytes)) {
            continue;
        }
        if (d->revision == INT_MAX || (d->undo_count == WORKSPACE_DOCUMENT_HISTORY_MAX &&
                                       group_reserved(service, d->undo[0].group))) {
            return WORKSPACE_DOCUMENT_LIMIT;
        }
        current = current - d->current.bytes + bytes;
        added += d->current.bytes;
        anchor_changed |= d->handle == anchor;
        members[changed] = d;
        replacements[changed++] = (document_text){ .text = (char *)requests[i].text,
                                                   .bytes = bytes };
    }
    if (anchor < 0 || (anchor && !anchor_document)) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    if (!changed) {
        plan->current = current;
        return WORKSPACE_DOCUMENT_OK;
    }
    if (anchor_document && !anchor_changed) {
        if (anchor_document->revision == INT_MAX ||
            (anchor_document->undo_count == WORKSPACE_DOCUMENT_HISTORY_MAX &&
             group_reserved(service, anchor_document->undo[0].group))) {
            return WORKSPACE_DOCUMENT_LIMIT;
        }
        members[changed] = anchor_document;
        replacements[changed++] = anchor_document->current;
        added += anchor_document->current.bytes;
    }
    if (current > WORKSPACE_DOCUMENT_TEXT_BYTES_MAX ||
        !history_possible(service, added) || service->next_group == UINT64_MAX ||
        (uint64_t)changed > UINT64_MAX - service->history_clock) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    plan->changed = changed;
    plan->current = current;
    plan->added = added;
    return WORKSPACE_DOCUMENT_OK;
}
workspace_document_status workspace_document_validate_replace_anchored(
    workspace_documents *service,
    const workspace_document_replacement *requests,
    int count,
    int anchor
)
{
    replacement_plan plan;
    return validate_replacements(service, requests, count, anchor, &plan);
}
workspace_document_status workspace_document_validate_candidates(
    workspace_documents *service,
    const workspace_document_candidate *requests,
    int count,
    int anchor
)
{
    if (!service) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    if (count < 0 || count > WORKSPACE_DOCUMENT_COUNT_MAX || (count && !requests)) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    workspace_document_replacement existing[WORKSPACE_DOCUMENT_COUNT_MAX];
    int existing_count = 0, missing = 0, free_slots = 0;
    size_t missing_bytes = 0;
    int missing_changes = 0;
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        free_slots += !service->documents[i].handle;
    }
    for (int i = 0; i < count; i++) {
        const workspace_document_candidate *r = &requests[i];
        if (!r->path || (r->path[0] && !valid_path(r->path))) {
            return WORKSPACE_DOCUMENT_INVALID;
        }
        for (int j = 0; j < i; j++) {
            if (r->path[0] && requests[j].path[0] &&
                same_path(service, r->path, requests[j].path)) {
                return WORKSPACE_DOCUMENT_INVALID;
            }
        }
        if (r->document) {
            document *d;
            workspace_document_status status = checked(service, r->document, r->revision, &d);
            if (status) {
                return status;
            }
            if (d->path ? !same_path(service, d->path, r->path) : r->path[0] != 0) {
                return WORKSPACE_DOCUMENT_INVALID;
            }
            existing[existing_count++] = (workspace_document_replacement){ r->document,
                                                                           r->revision,
                                                                           r->text };
            continue;
        }
        if (r->revision != -1 || !valid_path(r->path)) {
            return WORKSPACE_DOCUMENT_INVALID;
        }
        if (!service->root) {
            return WORKSPACE_DOCUMENT_UNAVAILABLE;
        }
        if (reserved_path(service, r->path)) {
            return WORKSPACE_DOCUMENT_EXISTS;
        }
        rooted_file_info info;
        if (rooted_files_stat(service->root, r->path, &info)) {
            return WORKSPACE_DOCUMENT_EXISTS;
        }
        if (errno != ENOENT) {
            return file_error();
        }
        size_t bytes;
        workspace_document_status status = text_size(r->text, &bytes);
        if (status) {
            return status;
        }
        missing++;
        missing_bytes += bytes;
        missing_changes += bytes != 0;
    }
    replacement_plan plan;
    workspace_document_status
        status = validate_replacements(service, existing, existing_count, anchor, &plan);
    if (status) {
        return status;
    }
    if (missing > free_slots || missing > INT_MAX - service->next_handle ||
        plan.current + missing_bytes > WORKSPACE_DOCUMENT_TEXT_BYTES_MAX) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    /* Drafts begin empty. A nonempty draft makes an otherwise unchanged anchor
     * join the eventual group; reserve its original bytes just as apply does. */
    if (missing_changes && !plan.changed) {
        document *d = anchor ? find(service, anchor) : NULL;
        if (d && (d->revision == INT_MAX ||
                  (d->undo_count == WORKSPACE_DOCUMENT_HISTORY_MAX &&
                   group_reserved(service, d->undo[0].group)))) {
            return WORKSPACE_DOCUMENT_LIMIT;
        }
        if (!history_possible(service, d ? d->current.bytes : 0) ||
            service->next_group == UINT64_MAX) {
            return WORKSPACE_DOCUMENT_LIMIT;
        }
    }
    int future_changes = plan.changed + missing_changes +
                         (missing_changes && !plan.changed && anchor ? 1 : 0);
    if ((uint64_t)future_changes > UINT64_MAX - service->history_clock) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    return WORKSPACE_DOCUMENT_OK;
}
workspace_document_status workspace_document_replace_anchored(
    workspace_documents *service,
    const workspace_document_replacement *requests,
    int count,
    int anchor
)
{
    replacement_plan plan;
    workspace_document_status status = validate_replacements(service, requests, count, anchor, &plan);
    if (status || !plan.changed) {
        return status;
    }
    document **members = plan.members;
    document_text *replacements = plan.replacements;
    int changed = plan.changed;
    size_t current = plan.current, added = plan.added;
    for (int i = 0; i < changed; i++) {
        replacements[i] = copy_text(replacements[i].text, replacements[i].bytes);
    }
    for (int i = 0; i < changed; i++) {
        clear_history(service, members[i]->redo, &members[i]->redo_count);
    }
    for (int i = 0; i < changed; i++) {
        if (members[i]->undo_count == WORKSPACE_DOCUMENT_HISTORY_MAX) {
            evict(service, members[i]->undo, &members[i]->undo_count);
        }
    }
    history_room(service, added, NULL);
    uint64_t group = ++service->next_group;
    for (int i = 0; i < changed; i++) {
        document *d = members[i];
        d->current.age = ++service->history_clock;
        d->current.group = group;
        d->undo[d->undo_count++] = d->current;
        service->history_bytes += d->current.bytes;
        d->current = replacements[i];
        d->revision++;
    }
    service->current_bytes = current;
    return WORKSPACE_DOCUMENT_OK;
}

workspace_document_status workspace_document_replace_many(
    workspace_documents *service,
    const workspace_document_replacement *requests,
    int count
)
{
    return workspace_document_replace_anchored(service, requests, count, 0);
}

/* Versioned document state is opaque to application policy and transport. */
typedef struct
{
    unsigned char *data;
    size_t size, capacity;
    bool failed;
} document_snapshot_writer;
static void snapshot_write(document_snapshot_writer *w, const void *bytes, size_t count)
{
    if (w->failed || count > WORKSPACE_DOCUMENT_SNAPSHOT_BYTES_MAX - w->size) {
        w->failed = true;
        return;
    }
    if (w->data && count) {
        assert(w->size + count <= w->capacity);
        memcpy(w->data + w->size, bytes, count);
    }
    w->size += count;
}
static void snapshot_write_u32(document_snapshot_writer *w, uint32_t value)
{
    unsigned char bytes[4];
    for (int i = 0; i < 4; i++) {
        bytes[i] = (unsigned char)(value >> (8 * i));
    }
    snapshot_write(w, bytes, sizeof bytes);
}
static void snapshot_write_u64(document_snapshot_writer *w, uint64_t value)
{
    unsigned char bytes[8];
    for (int i = 0; i < 8; i++) {
        bytes[i] = (unsigned char)(value >> (8 * i));
    }
    snapshot_write(w, bytes, sizeof bytes);
}
static void snapshot_write_text(document_snapshot_writer *w, document_text text)
{
    snapshot_write_u32(w, (uint32_t)text.bytes);
    snapshot_write_u64(w, text.age);
    snapshot_write_u64(w, text.group);
    snapshot_write(w, text.text, text.bytes);
}
static void snapshot_write_documents(const workspace_documents *service, document_snapshot_writer *w)
{
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    int count = workspace_document_handles(service, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    snapshot_write(w, "CHBIDOCS", 8);
    snapshot_write_u32(w, 1);
    snapshot_write_u32(w, (uint32_t)count);
    snapshot_write_u32(w, (uint32_t)service->next_handle);
    snapshot_write_u64(w, service->history_clock);
    snapshot_write_u64(w, service->next_group);
    for (int i = 0; i < count; i++) {
        const document *d = find(service, handles[i]);
        size_t path_bytes = d->path ? strlen(d->path) : 0;
        snapshot_write_u32(w, (uint32_t)d->handle);
        snapshot_write_u32(w, (uint32_t)d->revision);
        snapshot_write_u32(w, d->unsaved ? 1 : 0);
        snapshot_write_u32(w, (uint32_t)path_bytes);
        snapshot_write(w, d->path, path_bytes);
        snapshot_write_text(w, d->current);
        snapshot_write_text(w, d->saved);
        snapshot_write_u32(w, (uint32_t)d->undo_count);
        snapshot_write_u32(w, (uint32_t)d->redo_count);
        for (int j = 0; j < d->undo_count; j++) {
            snapshot_write_text(w, d->undo[j]);
        }
        for (int j = 0; j < d->redo_count; j++) {
            snapshot_write_text(w, d->redo[j]);
        }
    }
}
workspace_document_status workspace_documents_snapshot(
    const workspace_documents *service,
    unsigned char **owned,
    size_t *bytes
)
{
    if (owned) {
        *owned = NULL;
    }
    if (bytes) {
        *bytes = 0;
    }
    if (!service) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    if (!owned || !bytes) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        if (service->documents[i].transaction) {
            return WORKSPACE_DOCUMENT_TRANSACTION;
        }
    }
    document_snapshot_writer w = { 0 };
    snapshot_write_documents(service, &w);
    if (w.failed) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    w.data = xmalloc(w.size);
    w.capacity = w.size;
    w.size = 0;
    snapshot_write_documents(service, &w);
    assert(!w.failed && w.size == w.capacity);
    *owned = w.data;
    *bytes = w.size;
    return WORKSPACE_DOCUMENT_OK;
}
typedef struct
{
    const unsigned char *data;
    size_t size, at;
    workspace_document_status status;
} document_snapshot_reader;
static const unsigned char *snapshot_read(document_snapshot_reader *r, size_t count)
{
    if (r->status) {
        return NULL;
    }
    if (count > r->size - r->at) {
        r->status = WORKSPACE_DOCUMENT_INVALID;
        return NULL;
    }
    const unsigned char *result = r->data + r->at;
    r->at += count;
    return result;
}
static uint32_t snapshot_read_u32(document_snapshot_reader *r)
{
    const unsigned char *bytes = snapshot_read(r, 4);
    uint32_t value = 0;
    if (bytes) {
        for (int i = 0; i < 4; i++) {
            value |= (uint32_t)bytes[i] << (8 * i);
        }
    }
    return value;
}
static uint64_t snapshot_read_u64(document_snapshot_reader *r)
{
    const unsigned char *bytes = snapshot_read(r, 8);
    uint64_t value = 0;
    if (bytes) {
        for (int i = 0; i < 8; i++) {
            value |= (uint64_t)bytes[i] << (8 * i);
        }
    }
    return value;
}
static document_text snapshot_read_text(document_snapshot_reader *r, uint64_t clock, uint64_t group)
{
    document_text text = { 0 };
    uint32_t bytes = snapshot_read_u32(r);
    text.age = snapshot_read_u64(r);
    text.group = snapshot_read_u64(r);
    if (r->status) {
        return text;
    }
    if (bytes > WORKSPACE_DOCUMENT_BYTES_MAX) {
        r->status = WORKSPACE_DOCUMENT_LIMIT;
        return text;
    }
    if (text.age > clock || text.group > group) {
        r->status = WORKSPACE_DOCUMENT_INVALID;
        return text;
    }
    const unsigned char *source = snapshot_read(r, bytes);
    if (!source) {
        return text;
    }
    if (memchr(source, 0, bytes) || !utf8_text_valid((const char *)source, bytes)) {
        r->status = WORKSPACE_DOCUMENT_UTF8;
        return text;
    }
    text = (document_text){ .text = xmalloc((size_t)bytes + 1),
                            .bytes = bytes,
                            .age = text.age,
                            .group = text.group };
    memcpy(text.text, source, bytes);
    text.text[bytes] = 0;
    return text;
}
typedef struct
{
    uint64_t age, group;
    int handle, direction;
} document_snapshot_history;
static int snapshot_history_group_order(const void *left, const void *right)
{
    const document_snapshot_history *a = left, *b = right;
    if (a->group != b->group) {
        return a->group < b->group ? -1 : 1;
    }
    if (a->handle != b->handle) {
        return a->handle < b->handle ? -1 : 1;
    }
    return a->direction - b->direction;
}
static int snapshot_history_age_order(const void *left, const void *right)
{
    const document_snapshot_history *a = left, *b = right;
    return a->age < b->age ? -1 : a->age != b->age;
}
static workspace_document_status snapshot_validate_history(const workspace_documents *service)
{
    size_t capacity = (size_t)WORKSPACE_DOCUMENT_COUNT_MAX *
                      WORKSPACE_DOCUMENT_HISTORY_MAX * 2;
    document_snapshot_history *entries = xmalloc(capacity * sizeof *entries);
    int count = 0;
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        const document *d = &service->documents[i];
        for (int direction = 0; direction < 2; direction++) {
            const document_text *items = direction ? d->redo : d->undo;
            int n = direction ? d->redo_count : d->undo_count;
            for (int j = 0; j < n; j++) {
                if (!items[j].age) {
                    free(entries);
                    return WORKSPACE_DOCUMENT_INVALID;
                }
                entries[count++] = (document_snapshot_history){ items[j].age,
                                                                items[j].group,
                                                                d->handle,
                                                                direction };
            }
        }
    }
    qsort(entries, (size_t)count, sizeof *entries, snapshot_history_group_order);
    for (int i = 1; i < count; i++) {
        if (entries[i].group && entries[i].group == entries[i - 1].group &&
            (entries[i].handle == entries[i - 1].handle ||
             entries[i].direction != entries[i - 1].direction)) {
            free(entries);
            return WORKSPACE_DOCUMENT_INVALID;
        }
    }
    qsort(entries, (size_t)count, sizeof *entries, snapshot_history_age_order);
    for (int i = 1; i < count; i++) {
        if (entries[i].age == entries[i - 1].age) {
            free(entries);
            return WORKSPACE_DOCUMENT_INVALID;
        }
    }
    free(entries);
    return WORKSPACE_DOCUMENT_OK;
}
static void snapshot_free_candidate(workspace_documents *candidate)
{
    for (int i = 0; i < WORKSPACE_DOCUMENT_COUNT_MAX; i++) {
        document *d = &candidate->documents[i];
        free(d->path);
        free(d->current.text);
        free(d->saved.text);
        for (int j = 0; j < d->undo_count; j++) {
            free(d->undo[j].text);
        }
        for (int j = 0; j < d->redo_count; j++) {
            free(d->redo[j].text);
        }
    }
    free(candidate);
}
workspace_document_status workspace_documents_restore(
    workspace_documents *service,
    const void *bytes,
    size_t size
)
{
    if (!service) {
        return WORKSPACE_DOCUMENT_UNAVAILABLE;
    }
    if (!bytes || size < 36) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    if (size > WORKSPACE_DOCUMENT_SNAPSHOT_BYTES_MAX) {
        return WORKSPACE_DOCUMENT_LIMIT;
    }
    if (service->next_handle != 1 || workspace_document_handles(service, NULL, 0)) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    document_snapshot_reader r = { .data = bytes, .size = size };
    const unsigned char *magic = snapshot_read(&r, 8);
    if (!magic || memcmp(magic, "CHBIDOCS", 8) || snapshot_read_u32(&r) != 1) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    uint32_t count = snapshot_read_u32(&r), next_handle = snapshot_read_u32(&r);
    uint64_t clock = snapshot_read_u64(&r), group = snapshot_read_u64(&r);
    if (r.status || count > WORKSPACE_DOCUMENT_COUNT_MAX || !next_handle ||
        next_handle > INT_MAX) {
        return WORKSPACE_DOCUMENT_INVALID;
    }
    workspace_document_status status;
    workspace_documents *candidate = workspace_documents_new(NULL, &status);
    candidate->next_handle = (int)next_handle;
    candidate->history_clock = clock;
    candidate->next_group = group;
    int previous_handle = 0;
    for (uint32_t i = 0; i < count && !r.status; i++) {
        document *d = &candidate->documents[i];
        uint32_t handle = snapshot_read_u32(&r), revision = snapshot_read_u32(&r),
                 flags = snapshot_read_u32(&r), path_bytes = snapshot_read_u32(&r);
        if (r.status || handle <= (uint32_t)previous_handle ||
            handle >= next_handle || revision < 1 || revision > INT_MAX ||
            flags > 1 || path_bytes > WORKSPACE_DOCUMENT_PATH_MAX) {
            r.status = WORKSPACE_DOCUMENT_INVALID;
            break;
        }
        d->handle = (int)handle;
        d->revision = (int)revision;
        d->unsaved = flags != 0;
        previous_handle = d->handle;
        if (path_bytes) {
            const unsigned char *path = snapshot_read(&r, path_bytes);
            if (!path) {
                break;
            }
            if (memchr(path, 0, path_bytes)) {
                r.status = WORKSPACE_DOCUMENT_INVALID;
                break;
            }
            d->path = xmalloc((size_t)path_bytes + 1);
            memcpy(d->path, path, path_bytes);
            d->path[path_bytes] = 0;
            if (!valid_path(d->path)) {
                r.status = WORKSPACE_DOCUMENT_INVALID;
                break;
            }
            for (uint32_t j = 0; j < i; j++) {
                if (candidate->documents[j].path &&
                    same_path(service, d->path, candidate->documents[j].path)) {
                    r.status = WORKSPACE_DOCUMENT_INVALID;
                    break;
                }
            }
        }
        if (r.status) {
            break;
        }
        d->current = snapshot_read_text(&r, clock, group);
        d->saved = snapshot_read_text(&r, clock, group);
        if (r.status) {
            break;
        }
        if (d->current.group || d->saved.group || d->saved.age ||
            (d->unsaved && (!d->path || d->saved.bytes))) {
            r.status = WORKSPACE_DOCUMENT_INVALID;
            break;
        }
        candidate->current_bytes += d->current.bytes;
        candidate->saved_bytes += d->saved.bytes;
        uint32_t undo = snapshot_read_u32(&r), redo = snapshot_read_u32(&r);
        if (r.status || undo > WORKSPACE_DOCUMENT_HISTORY_MAX ||
            redo > WORKSPACE_DOCUMENT_HISTORY_MAX) {
            r.status = WORKSPACE_DOCUMENT_INVALID;
            break;
        }
        for (uint32_t j = 0; j < undo && !r.status; j++) {
            d->undo[d->undo_count++] = snapshot_read_text(&r, clock, group);
            candidate->history_bytes += d->undo[d->undo_count - 1].bytes;
        }
        for (uint32_t j = 0; j < redo && !r.status; j++) {
            d->redo[d->redo_count++] = snapshot_read_text(&r, clock, group);
            candidate->history_bytes += d->redo[d->redo_count - 1].bytes;
        }
        if (candidate->current_bytes > WORKSPACE_DOCUMENT_TEXT_BYTES_MAX ||
            candidate->saved_bytes > WORKSPACE_DOCUMENT_TEXT_BYTES_MAX ||
            candidate->history_bytes > WORKSPACE_DOCUMENT_HISTORY_BYTES_MAX) {
            r.status = WORKSPACE_DOCUMENT_LIMIT;
        }
    }
    status = r.status;
    if (!status && r.at != r.size) {
        status = WORKSPACE_DOCUMENT_INVALID;
    }
    if (!status) {
        status = snapshot_validate_history(candidate);
    }
    if (status) {
        snapshot_free_candidate(candidate);
        return status;
    }
    rooted_files *root = service->root;
    *service = *candidate;
    service->root = root;
    free(candidate);
    return WORKSPACE_DOCUMENT_OK;
}
