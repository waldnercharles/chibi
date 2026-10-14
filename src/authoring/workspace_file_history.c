#include "authoring/workspace_file_history.h"

#include "support/asset_text.h"
#include "support/rooted_files.h"
#include "support/xalloc.h"

#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct
{
    char *path, *recovery, *snapshot;
    unsigned char *bytes;
    size_t count;
    bool tombstoned, discarded, snapshot_discarded;
} entry;
typedef struct
{
    int token, count;
    entry files[WORKSPACE_FILE_HISTORY_FILES_MAX];
    const char *paths[WORKSPACE_FILE_HISTORY_FILES_MAX],
        *recovery[WORKSPACE_FILE_HISTORY_FILES_MAX];
    size_t bytes;
    char *manifest, *manifest_text;
    bool partial, forgetting;
    char error[256];
} batch;
struct workspace_file_history
{
    rooted_files *root;
    workspace_documents *documents;
    int next;
    size_t bytes;
    batch batches[WORKSPACE_FILE_HISTORY_TOKENS_MAX];
    char error[256];
};
static const char *state(batch *b)
{
    if (b->forgetting) {
        return "discarding";
    }
    if (b->partial) {
        return "partial";
    }
    return b->count && b->files[0].tombstoned ? "deleted" : "restored";
}
static workspace_file_history_result result(
    workspace_file_history *s,
    batch *b,
    bool ok,
    int changed,
    const char *error
)
{
    char *storage = b ? b->error : s->error;
    if (error != storage) {
        snprintf(storage, 256, "%s", error ? error : "");
    }
    return (
        workspace_file_history_result
    ){ ok, b ? b->token : 0, changed, b ? state(b) : "unavailable", storage };
}
static batch *find(workspace_file_history *s, int token)
{
    if (!s || token <= 0) {
        return NULL;
    }
    for (int i = 0; i < WORKSPACE_FILE_HISTORY_TOKENS_MAX; i++) {
        if (s->batches[i].token == token) {
            return &s->batches[i];
        }
    }
    return NULL;
}
static void release(batch *b)
{
    for (int i = 0; i < b->count; i++) {
        free(b->files[i].path);
        free(b->files[i].recovery);
        free(b->files[i].snapshot);
        free(b->files[i].bytes);
    }
    free(b->manifest);
    free(b->manifest_text);
    memset(b, 0, sizeof *b);
}
workspace_file_history *workspace_file_history_new(const char *root, workspace_documents *docs)
{
    rooted_files *files = root ? rooted_files_open(root) : NULL;
    if (!files) {
        return NULL;
    }
    workspace_file_history *s = xcalloc(1, sizeof *s);
    s->root = files;
    s->documents = docs;
    s->next = 1;
    return s;
}
void workspace_file_history_free(workspace_file_history *s)
{
    if (!s) {
        return;
    }
    for (int i = 0; i < WORKSPACE_FILE_HISTORY_TOKENS_MAX; i++) {
        release(&s->batches[i]);
    }
    rooted_files_close(s->root);
    free(s);
}
static bool matches(workspace_file_history *s, const char *path, const unsigned char *expected, size_t count)
{
    unsigned char *bytes = NULL;
    size_t n = 0;
    bool ok = rooted_files_read(s->root, path, WORKSPACE_FILE_HISTORY_FILE_BYTES_MAX, &bytes, &n) &&
              n == count && (!n || !memcmp(bytes, expected, n));
    free(bytes);
    return ok;
}
static bool absent(workspace_file_history *s, const char *path)
{
    rooted_file_info info;
    if (rooted_files_stat(s->root, path, &info)) {
        errno = EEXIST;
        return false;
    }
    return errno == ENOENT;
}
static bool clean_documents(workspace_file_history *s, const char *path, const unsigned char *bytes, size_t count)
{
    if (!s->documents) {
        return true;
    }
    int handles[WORKSPACE_DOCUMENT_COUNT_MAX];
    int total = workspace_document_handles(s->documents, handles, WORKSPACE_DOCUMENT_COUNT_MAX);
    for (int i = 0; i < total; i++) {
        workspace_document_view v;
        if (workspace_document_read(s->documents, handles[i], &v) != WORKSPACE_DOCUMENT_OK ||
            strcmp(v.path, path)) {
            continue;
        }
        if (v.dirty || v.transaction || strlen(v.text) != count ||
            (count && memcmp(v.text, bytes, count))) {
            return false;
        }
    }
    return true;
}
/* Move only into an absent destination. If the final source changed between
 * preflight and move, its bytes remain in the tombstone and rollback attempts
 * to return them exclusively. Neither rollback nor restoration overwrites. */
static bool move_entry(workspace_file_history *s, entry *e, bool deleted)
{
    const char *from = deleted ? e->path : e->recovery,
               *to = deleted ? e->recovery : e->path;
    if (!deleted && absent(s, from)) {
        if (!matches(s, e->snapshot, e->bytes, e->count) || !absent(s, to)) {
            return false;
        }
        bool written = rooted_files_write(
            s->root,
            to,
            e->bytes,
            e->count,
            WORKSPACE_FILE_HISTORY_FILE_BYTES_MAX,
            false,
            NULL,
            0
        );
        if (written || matches(s, to, e->bytes, e->count)) {
            e->tombstoned = false;
        }
        return written;
    }
    if (!matches(s, from, e->bytes, e->count) || !absent(s, to)) {
        return false;
    }
    if (deleted && !clean_documents(s, e->path, e->bytes, e->count)) {
        return false;
    }
    if (!rooted_files_move(s->root, from, to, false)) {
        return false;
    }
    e->tombstoned = deleted;
    return matches(s, to, e->bytes, e->count);
}
static workspace_file_history_result transition(workspace_file_history *s, batch *b, bool deleted)
{
    if (b->forgetting) {
        return result(s, b, false, 0, "This batch is being discarded; retry Forget to finish cleanup");
    }
    if (b->partial) {
        return result(s, b, false, 0, "This partial batch requires recovery; retained files are listed by Read File History");
    }
    if (b->files[0].tombstoned == deleted) {
        return result(s, b, false, 0, "File history is already in that state");
    }
    for (int i = 0; i < b->count; i++) {
        entry *e = &b->files[i];
        const char *from = deleted ? e->path : e->recovery,
                   *to = deleted ? e->recovery : e->path;
        if (deleted && !matches(s, e->snapshot, e->bytes, e->count)) {
            return result(s, b, false, 0, "A recovery baseline is missing or changed; no files changed");
        }
        if (!matches(s, from, e->bytes, e->count) || !absent(s, to) ||
            (deleted && !clean_documents(s, e->path, e->bytes, e->count))) {
            return result(s, b, false, 0, "A file changed, a destination exists, or an open source has unsaved edits; no files changed");
        }
    }
    int changed = 0;
    bool ok = true;
    for (int i = 0; i < b->count; i++) {
        bool before = b->files[i].tombstoned;
        if (!move_entry(s, &b->files[i], deleted)) {
            if (before != b->files[i].tombstoned) {
                changed++;
            }
            ok = false;
            break;
        }
        changed++;
    }
    if (ok) {
        return result(s, b, true, changed, "");
    }
    bool rolled_back = true;
    for (int i = changed - 1; i >= 0; i--) {
        entry *e = &b->files[i];
        const char *from = e->tombstoned ? e->recovery : e->path,
                   *to = e->tombstoned ? e->path : e->recovery;
        /* Even a raced-in source is retained intact; rollback does not assume
         * it still equals the preflight snapshot. */
        if (!absent(s, to) || !rooted_files_move(s->root, from, to, false)) {
            rolled_back = false;
            continue;
        }
        e->tombstoned = !e->tombstoned;
        if (!matches(s, to, e->bytes, e->count)) {
            rolled_back = false;
        }
    }
    b->partial = !rolled_back;
    int remaining = 0;
    for (int i = 0; i < b->count; i++) {
        if (b->files[i].tombstoned == deleted) {
            remaining++;
        }
    }
    return result(
        s,
        b,
        false,
        remaining,
        rolled_back ? "File operation failed; all moves were rolled back" : "Partial file operation: recovery files and manifest are retained; no existing destination was overwritten"
    );
}
static bool manifest(workspace_file_history *s, batch *b)
{
    rooted_files *directory = rooted_files_directory(s->root, ".chibi/file-history", true);
    if (!directory) {
        return false;
    }
    rooted_files_close(directory);
    for (int attempt = 0; attempt < 64 && s->next < INT_MAX; attempt++) {
        int token = s->next++;
        char path[128];
        snprintf(path, sizeof path, ".chibi/file-history/delete-%08d.kdl", token);
        if (!absent(s, path)) {
            continue;
        }
        bool clear = true;
        for (int i = 0; i < b->count; i++) {
            char trash[128];
            snprintf(trash, sizeof trash, ".chibi/file-history/delete-%08d-%02d", token, i);
            char snapshot[160];
            snprintf(snapshot, sizeof snapshot, "%s.snapshot", trash);
            if (!absent(s, trash) || !absent(s, snapshot)) {
                clear = false;
                break;
            }
            free(b->files[i].recovery);
            free(b->files[i].snapshot);
            b->files[i].recovery = xstrdup(trash);
            b->files[i].snapshot = xstrdup(snapshot);
            b->recovery[i] = b->files[i].snapshot;
        }
        if (!clear) {
            continue;
        }
        av_doc *doc = av_doc_new();
        av_value *root = av_obj(doc);
        av_doc_set_root(doc, root);
        av_obj_add_int(doc, root, "version", 1);
        av_obj_add_int(doc, root, "token", token);
        av_value *files = av_obj_add_arr(doc, root, "files");
        for (int i = 0; i < b->count; i++) {
            av_value *row = av_arr_add_obj(doc, files);
            av_obj_add_strcpy(doc, row, "path", b->files[i].path);
            av_obj_add_strcpy(doc, row, "recovery", b->files[i].recovery);
            av_obj_add_strcpy(doc, row, "snapshot", b->files[i].snapshot);
        }
        char *text = av_doc_write(doc);
        av_doc_free(doc);
        bool written = text && rooted_files_write(
                                   s->root,
                                   path,
                                   text,
                                   strlen(text),
                                   WORKSPACE_DOCUMENT_BYTES_MAX,
                                   false,
                                   NULL,
                                   0
                               );
        if (!written) {
            free(text);
            return false;
        }
        b->token = token;
        b->manifest = xstrdup(path);
        b->manifest_text = text;
        for (int i = 0; i < b->count; i++) {
            entry *e = &b->files[i];
            if (!rooted_files_write(
                    s->root,
                    e->snapshot,
                    e->bytes,
                    e->count,
                    WORKSPACE_FILE_HISTORY_FILE_BYTES_MAX,
                    false,
                    NULL,
                    0
                )) {
                b->partial = true;
                return false;
            }
        }
        return true;
    }
    return false;
}
workspace_file_history_result workspace_files_delete(
    workspace_file_history *s,
    const char *const *paths,
    int count
)
{
    if (!s) {
        return (
            workspace_file_history_result
        ){ false, 0, 0, "unavailable", "File history unavailable" };
    }
    if (!paths || count < 1 || count > WORKSPACE_FILE_HISTORY_FILES_MAX) {
        return result(s, NULL, false, 0, "File deletion requires 1 to 64 distinct paths");
    }
    batch *b = NULL;
    for (int i = 0; i < WORKSPACE_FILE_HISTORY_TOKENS_MAX; i++) {
        if (!s->batches[i].token) {
            b = &s->batches[i];
            break;
        }
    }
    if (!b) {
        return result(s, NULL, false, 0, "Eight recoverable deletions are retained; restore or explicitly forget one first");
    }
    size_t bytes = 0;
    for (int i = 0; i < count; i++) {
        if (!rooted_files_path_valid(paths[i]) || !strncmp(paths[i], ".chibi/", 7)) {
            return result(s, NULL, false, 0, "Invalid source path or reserved recovery path");
        }
        for (int j = 0; j < i; j++) {
            if (!strcmp(paths[i], paths[j])) {
                return result(s, NULL, false, 0, "Deletion paths must be distinct");
            }
        }
        rooted_file_info info;
        if (!rooted_files_stat(s->root, paths[i], &info) || !info.regular ||
            info.bytes > WORKSPACE_FILE_HISTORY_FILE_BYTES_MAX) {
            return result(s, NULL, false, 0, "Source is unavailable, linked, or exceeds the 16MiB file limit");
        }
        if (info.bytes > WORKSPACE_FILE_HISTORY_BYTES_MAX - s->bytes - bytes) {
            return result(s, NULL, false, 0, "Recoverable deletions exceed the 32MiB byte budget");
        }
        bytes += (size_t)info.bytes;
    }
    b->count = count;
    for (int i = 0; i < count; i++) {
        entry *e = &b->files[i];
        e->path = xstrdup(paths[i]);
        b->paths[i] = e->path;
        if (!rooted_files_read(s->root, paths[i], WORKSPACE_FILE_HISTORY_FILE_BYTES_MAX, &e->bytes, &e->count) ||
            !clean_documents(s, e->path, e->bytes, e->count)) {
            release(b);
            return result(s, NULL, false, 0, "Source changed or contains unsaved document edits; no files changed");
        }
        if (e->count > WORKSPACE_FILE_HISTORY_BYTES_MAX - s->bytes - b->bytes) {
            release(b);
            return result(s, NULL, false, 0, "Source grew beyond the retained byte budget; no files changed");
        }
        b->bytes += e->count;
    }
    if (!manifest(s, b)) {
        if (b->token) {
            s->bytes += b->bytes;
            return result(s, b, false, 0, "Recovery preparation failed; original files are unchanged and recovery metadata is retained");
        }
        release(b);
        return result(s, NULL, false, 0, "Cannot reserve a recovery manifest; no source files changed");
    }
    s->bytes += b->bytes;
    workspace_file_history_result r = transition(s, b, true);
    /* A preflight or fully rolled-back refusal retains a usable restored token.
     * The caller may explicitly close it without losing source bytes. */
    return r;
}
workspace_file_history_result workspace_files_restore(workspace_file_history *s, int token)
{
    batch *b = find(s, token);
    if (!b) {
        return s ? result(s, NULL, false, 0, "File history token is closed")
                 : (
                       workspace_file_history_result
                   ){ false, 0, 0, "unavailable", "File history unavailable" };
    }
    if (b->forgetting) {
        return result(s, b, false, 0, "This batch is being discarded; retry Forget to finish cleanup");
    }
    if (!b->partial) {
        return transition(s, b, false);
    }
    /* Recover only owned tombstones into absent original names. Already restored
     * originals must still match; conflicts leave all recovery bytes retained. */
    int changed = 0;
    bool ok = true;
    for (int i = 0; i < b->count; i++) {
        entry *e = &b->files[i];
        if (e->tombstoned) {
            if (!move_entry(s, e, false)) {
                ok = false;
            }
            if (!e->tombstoned) {
                changed++;
            }
        } else if (!matches(s, e->path, e->bytes, e->count)) {
            ok = false;
        }
    }
    b->partial = !ok;
    return result(s, b, ok, changed, ok ? "" : "Some recovery destinations conflict; retained tombstones were not overwritten");
}
workspace_file_history_result workspace_files_redo(workspace_file_history *s, int token)
{
    batch *b = find(s, token);
    return b ? transition(s, b, true)
         : s ? result(s, NULL, false, 0, "File history token is closed")
             : (
                   workspace_file_history_result
               ){ false, 0, 0, "unavailable", "File history unavailable" };
}
bool workspace_file_history_read(workspace_file_history *s, int token, workspace_file_history_view *out)
{
    if (!out) {
        return false;
    }
    *out = (workspace_file_history_view){
        .result = { false, 0, 0, s ? "closed" : "unavailable", s ? "File history token is closed" : "File history unavailable" }
    };
    batch *b = find(s, token);
    if (!b) {
        return false;
    }
    *out = (workspace_file_history_view){
        .result = result(s, b, true, 0, b->error),
        .paths = b->paths,
        .recovery = b->recovery,
        .count = b->count,
        .bytes = b->bytes,
        .can_undo = !b->forgetting && (b->partial || b->files[0].tombstoned),
        .can_redo = !b->forgetting && !b->partial && !b->files[0].tombstoned
    };
    return true;
}
workspace_file_history_result workspace_files_forget(workspace_file_history *s, int token)
{
    batch *b = find(s, token);
    if (!b) {
        return s ? result(s, NULL, false, 0, "File history token is closed")
                 : (
                       workspace_file_history_result
                   ){ false, 0, 0, "unavailable", "File history unavailable" };
    }
    if (b->partial && !b->forgetting) {
        return result(s, b, false, 0, "Recover partial deletions before forgetting their recovery bytes");
    }
    for (int i = 0; i < b->count; i++) {
        entry *e = &b->files[i];
        if (e->tombstoned && !e->discarded && !absent(s, e->recovery) &&
            !matches(s, e->recovery, e->bytes, e->count)) {
            return result(s, b, false, 0, "A recovery file changed; it was preserved");
        }
        if (!e->snapshot_discarded && !absent(s, e->snapshot) &&
            !matches(s, e->snapshot, e->bytes, e->count)) {
            return result(s, b, false, 0, "A baseline snapshot changed; it was preserved");
        }
    }
    if (!matches(s, b->manifest, (unsigned char *)b->manifest_text, strlen(b->manifest_text))) {
        return result(s, b, false, 0, "The recovery manifest changed; it was preserved");
    }
    b->forgetting = true;
    for (int i = 0; i < b->count; i++) {
        entry *e = &b->files[i];
        if (e->tombstoned && !e->discarded) {
            if (!absent(s, e->recovery) && !rooted_files_remove(s->root, e->recovery)) {
                return result(s, b, false, i, "Recovery cleanup failed; remaining files are retained. Retry Forget to finish cleanup");
            }
            e->discarded = true;
        }
    }
    for (int i = 0; i < b->count; i++) {
        entry *e = &b->files[i];
        if (!e->snapshot_discarded) {
            if (!absent(s, e->snapshot) && !rooted_files_remove(s->root, e->snapshot)) {
                return result(s, b, false, i, "Baseline cleanup failed; remaining files are retained. Retry Forget to finish cleanup");
            }
            e->snapshot_discarded = true;
        }
    }
    if (!rooted_files_remove(s->root, b->manifest)) {
        return result(s, b, false, 0, "Could not discard recovery manifest; retry Forget");
    }
    s->bytes -= b->bytes;
    release(b);
    return result(s, NULL, true, 0, "");
}
