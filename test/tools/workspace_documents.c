#include "authoring/workspace_documents.h"

#include "authoring/workspace_documents_definition.h"
#include "execution/function.h"
#include "host/native_catalog.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

static workspace_documents *documents;
static char root[1024];
static char *large;
static arena storage;
static void setup()
{
    workspace_document_status status;
    documents = workspace_documents_new(NULL, &status);
    assert(documents && status == WORKSPACE_DOCUMENT_OK);
}
static void cleanup()
{
    workspace_documents_free(documents);
    documents = NULL;
    free(large);
    large = NULL;
    arena_free(&storage);
    if (root[0]) {
        tool_remove_tree(root);
        root[0] = 0;
    }
}
static workspace_document_view view(int handle)
{
    workspace_document_view result;
    assert(workspace_document_read(documents, handle, &result) == WORKSPACE_DOCUMENT_OK);
    return result;
}
TEST_CASE(workspace_documents_group_cancel_and_reject_stale_utf8_edits)
{
    int handle;
    REQUIRE(workspace_document_memory(documents, "a\xc3\xa9z", &handle) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!view(handle).dirty);
    REQUIRE(workspace_document_begin(documents, handle, 1) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_edit(documents, handle, 1, 2, 3, "x") == WORKSPACE_DOCUMENT_INVALID);
    REQUIRE(workspace_document_edit(documents, handle, 1, 1, 3, "\xc0\xaf") == WORKSPACE_DOCUMENT_UTF8);
    REQUIRE(view(handle).revision == 1 && !strcmp(view(handle).text, "a\xc3\xa9z"));
    REQUIRE(workspace_document_edit(documents, handle, 1, 1, 3, "x") == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_edit(documents, handle, 1, 0, 1, "b") == WORKSPACE_DOCUMENT_STALE);
    REQUIRE(workspace_document_undo(documents, handle, 2) == WORKSPACE_DOCUMENT_TRANSACTION);
    REQUIRE(workspace_document_save(documents, handle, 2) == WORKSPACE_DOCUMENT_TRANSACTION);
    REQUIRE(workspace_document_edit(documents, handle, 2, 0, 1, "b") == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_commit(documents, handle, 3) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strcmp(view(handle).text, "bxz") && view(handle).can_undo);
    REQUIRE(workspace_document_undo(documents, handle, 3) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!view(handle).dirty && !strcmp(view(handle).text, "a\xc3\xa9z"));
    REQUIRE(workspace_document_redo(documents, handle, 4) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_begin(documents, handle, 5) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_edit(documents, handle, 5, 0, 3, "draft") == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_cancel(documents, handle, 6) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strcmp(view(handle).text, "bxz") && view(handle).revision == 7);
    REQUIRE(workspace_document_undo(documents, handle, 7) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!view(handle).dirty && view(handle).can_redo);
    REQUIRE(workspace_document_begin(documents, handle, 8) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_commit(documents, handle, 8) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view(handle).can_redo && !view(handle).can_undo);
    REQUIRE(workspace_document_save(documents, handle, 8) == WORKSPACE_DOCUMENT_NOT_FILE);
    return true;
}
TEST_CASE(workspace_documents_limits_preserve_history_and_handle_identity)
{
    int handle, later;
    REQUIRE(workspace_document_memory(documents, "", &handle) == WORKSPACE_DOCUMENT_OK);
    for (int i = 0; i < WORKSPACE_DOCUMENT_HISTORY_MAX + 8; i++) {
        REQUIRE(workspace_document_edit(documents, handle, i + 1, i, i, "x") == WORKSPACE_DOCUMENT_OK);
    }
    int revision = view(handle).revision;
    REQUIRE(view(handle).bytes == WORKSPACE_DOCUMENT_HISTORY_MAX + 8);
    for (int i = 0; i < WORKSPACE_DOCUMENT_HISTORY_MAX; i++) {
        REQUIRE(workspace_document_undo(documents, handle, revision + i) == WORKSPACE_DOCUMENT_OK);
    }
    REQUIRE(view(handle).bytes == 8 && view(handle).dirty);
    REQUIRE(
        workspace_document_undo(documents, handle, view(handle).revision) == WORKSPACE_DOCUMENT_EMPTY_HISTORY
    );
    REQUIRE(
        workspace_document_edit(documents, handle, view(handle).revision, 0, 0, "new") == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(!view(handle).can_redo);
    REQUIRE(workspace_document_close(documents, handle) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_memory(documents, "later", &later) == WORKSPACE_DOCUMENT_OK &&
        later > handle
    );
    workspace_document_view untouched = { .revision = 123 };
    REQUIRE(workspace_document_read(documents, handle, &untouched) == WORKSPACE_DOCUMENT_INVALID);
    REQUIRE(untouched.revision == 123);
    int newest;
    REQUIRE(workspace_document_memory(documents, "last", &newest) == WORKSPACE_DOCUMENT_OK);
    int handles[2];
    REQUIRE(
        workspace_document_handles(documents, handles, 2) == 2 &&
        handles[0] == later && handles[1] == newest
    );
    large = malloc(WORKSPACE_DOCUMENT_BYTES_MAX + 2u);
    REQUIRE(large);
    memset(large, 'x', WORKSPACE_DOCUMENT_BYTES_MAX + 1u);
    large[WORKSPACE_DOCUMENT_BYTES_MAX + 1] = 0;
    int refused = -1;
    REQUIRE(
        workspace_document_memory(documents, large, &refused) == WORKSPACE_DOCUMENT_LIMIT &&
        refused == -1
    );
    return true;
}
TEST_CASE(workspace_documents_reserve_cancel_and_release_committed_budget)
{
    large = malloc(WORKSPACE_DOCUMENT_BYTES_MAX + 1u);
    REQUIRE(large);
    memset(large, 'x', WORKSPACE_DOCUMENT_BYTES_MAX);
    large[WORKSPACE_DOCUMENT_BYTES_MAX] = 0;
    int handle;
    for (int i = 0; i < 15; i++) {
        REQUIRE(workspace_document_memory(documents, large, &handle) == WORKSPACE_DOCUMENT_OK);
    }
    large[WORKSPACE_DOCUMENT_BYTES_MAX / 2] = 0;
    int first, second;
    REQUIRE(workspace_document_memory(documents, large, &first) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_memory(documents, large, &second) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_begin(documents, first, 1) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_edit(documents, first, 1, 0, WORKSPACE_DOCUMENT_BYTES_MAX / 2, "") ==
        WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(workspace_document_edit(documents, second, 1, 0, 0, large) == WORKSPACE_DOCUMENT_LIMIT);
    REQUIRE(workspace_document_cancel(documents, first, 2) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view(first).bytes == WORKSPACE_DOCUMENT_BYTES_MAX / 2);
    REQUIRE(workspace_document_begin(documents, first, 3) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_edit(documents, first, 3, 0, WORKSPACE_DOCUMENT_BYTES_MAX / 2, "") ==
        WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(workspace_document_commit(documents, first, 4) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_edit(documents, second, 1, 0, 0, large) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_undo(documents, first, 4) == WORKSPACE_DOCUMENT_LIMIT);
    REQUIRE(view(first).bytes == 0 && view(first).can_undo);
    return true;
}
TEST_CASE(workspace_documents_byte_budget_evicts_old_snapshots)
{
    large = malloc(WORKSPACE_DOCUMENT_BYTES_MAX + 1u);
    REQUIRE(large);
    memset(large, 'x', WORKSPACE_DOCUMENT_BYTES_MAX);
    large[WORKSPACE_DOCUMENT_BYTES_MAX] = 0;
    int handle;
    REQUIRE(workspace_document_memory(documents, large, &handle) == WORKSPACE_DOCUMENT_OK);
    for (int i = 1; i <= 40; i++) {
        char number[4];
        snprintf(number, sizeof number, "%03d", i);
        REQUIRE(workspace_document_edit(documents, handle, i, 0, 3, number) == WORKSPACE_DOCUMENT_OK);
    }
    for (int i = 0; i < 32; i++) {
        REQUIRE(workspace_document_undo(documents, handle, 41 + i) == WORKSPACE_DOCUMENT_OK);
    }
    REQUIRE(!strncmp(view(handle).text, "008", 3) && !view(handle).can_undo);
    return true;
}
static const char *owned_string(void *arena, const char *text)
{
    size_t size = strlen(text) + 1;
    char *result = arena_alloc(arena, size);
    memcpy(result, text, size);
    return result;
}
TEST_CASE(workspace_document_absent_service_refuses_every_native_operation)
{
    behavior_function_context context = { .value_storage = &storage,
                                          .copy_string = owned_string };
#define NULL_CALL(ID, NAME, SOURCE, INPUTS) &WORKSPACE_DOCUMENT_##ID##_CALL,
    const node_host_call *calls[] = {
        WORKSPACE_DOCUMENT_OPERATIONS(NULL_CALL) & WORKSPACE_DOCUMENT_HANDLES_CALL,
        &WORKSPACE_DOCUMENT_PATH_CALL,
        &WORKSPACE_DOCUMENT_REPLACE_MANY_CALL,
        &WORKSPACE_DOCUMENT_VALIDATE_EDIT_CALL,
        &WORKSPACE_DOCUMENT_VALIDATE_CANDIDATES_CALL,
    };
#undef NULL_CALL
    int handle = 0;
    REQUIRE(!workspace_document_memory(documents, "retained", &handle));
    pin_value input[5] = { { 0 } }, output[13] = { { 0 } };
    for (size_t i = 0; i < sizeof calls / sizeof *calls; i++) {
        memset(output, 0, sizeof output);
        REQUIRE(calls[i]->optional_service);
        REQUIRE(!calls[i]->context(&context, NULL, input, output));
        REQUIRE(
            !output[0].b && output[1].i == WORKSPACE_DOCUMENT_UNAVAILABLE &&
            output[2].str && *output[2].str
        );
    }
    REQUIRE(!WORKSPACE_DOCUMENT_REPLACE_MANY_CALL.context(&context, NULL, NULL, output));
    REQUIRE(!output[0].b && output[1].i == WORKSPACE_DOCUMENT_UNAVAILABLE);
    REQUIRE(!WORKSPACE_DOCUMENT_VALIDATE_CANDIDATES_CALL.context(&context, NULL, NULL, output));
    REQUIRE(!output[0].b && output[1].i == WORKSPACE_DOCUMENT_UNAVAILABLE);
    REQUIRE(!WORKSPACE_DOCUMENT_HANDLES_CALL.context(&context, NULL, NULL, output));
    REQUIRE(
        output[3].ptr && ((pin_array *)output[3].ptr)->count == 0 && !output[0].b
    );
    REQUIRE(!WORKSPACE_DOCUMENT_PATH_CALL.context(&context, NULL, input, output));
    REQUIRE(!output[3].b && output[4].i == 0 && output[5].i == 0);
    REQUIRE(
        !strcmp(view(handle).text, "retained") &&
        workspace_document_handles(documents, NULL, 0) == 1
    );
    return true;
}
TEST_CASE(workspace_document_bindings_copy_results_and_report_refusals)
{
    char error[256];
    native_capabilities *catalog = host_native_catalog_new(
        NULL,
        0,
        &WORKSPACE_DOCUMENTS_DEFINITION_PACK,
        1,
        error,
        sizeof error
    );
    REQUIRE(catalog);
    native_capabilities_free(catalog);
    behavior_function_context context = { .value_storage = &storage,
                                          .copy_string = owned_string };
    pin_value input[] = { { .type = PIN_STR, .str = "original" } },
              output[13] = { { 0 } };
    REQUIRE(!WORKSPACE_DOCUMENT_MEMORY_CALL.context(&context, documents, input, output));
    REQUIRE(
        output[0].b && output[1].i == WORKSPACE_DOCUMENT_OK &&
        !strcmp(output[5].str, "original")
    );
    REQUIRE(output[11].i == 8 && !output[12].b);
    int handle = output[3].i;
    const char *retained = output[5].str;
    pin_value info_input = { .i = handle }, metadata[13] = { { 0 } };
    REQUIRE(!WORKSPACE_DOCUMENT_INFO_CALL.context(&context, documents, &info_input, metadata));
    REQUIRE(metadata[0].b && metadata[11].i == 8 && !strcmp(metadata[5].str, ""));
    REQUIRE(workspace_document_edit(documents, handle, 1, 0, 8, "changed") == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strcmp(retained, "original"));
    pin_value edit[] = { { .i = handle }, { .i = 1 }, { .i = 0 }, { .i = 7 }, { .str = "bad" } };
    REQUIRE(!WORKSPACE_DOCUMENT_EDIT_CALL.context(&context, documents, edit, output));
    REQUIRE(
        !output[0].b && output[1].i == WORKSPACE_DOCUMENT_STALE &&
        output[4].i == 2 && !strcmp(output[5].str, "changed")
    );
    REQUIRE(!WORKSPACE_DOCUMENT_READ_CALL.context(&context, NULL, edit, output));
    REQUIRE(
        !output[0].b && output[1].i == WORKSPACE_DOCUMENT_UNAVAILABLE &&
        output[4].i == 0
    );
    pin_value handles[4] = { { 0 } };
    REQUIRE(!WORKSPACE_DOCUMENT_HANDLES_CALL.context(&context, documents, NULL, handles));
    REQUIRE(
        handles[0].b && ((pin_array *)handles[3].ptr)->count == 1 &&
        ((pin_array *)handles[3].ptr)->items[0].i == handle
    );
    return true;
}
#if !defined(_WIN32) && defined(O_NOFOLLOW)
TEST_CASE(workspace_documents_save_detects_conflicts_and_refuses_unsafe_files)
{
    snprintf(root, sizeof root, "%s/workspace-documents-XXXXXX", tool_root());
    REQUIRE(mkdtemp(root));
    char path[1200];
    snprintf(path, sizeof path, "%s/source.chibi", root);
    tool_write(path, "original");
    REQUIRE(!chmod(path, 0640));
    workspace_documents_free(documents);
    workspace_document_status status;
    documents = workspace_documents_new(root, &status);
    REQUIRE(documents && !status);
    int handle, again;
    REQUIRE(workspace_document_open(documents, "source.chibi", &handle) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_open(documents, "source.chibi", &again) == WORKSPACE_DOCUMENT_OK &&
        again == handle
    );
    REQUIRE(workspace_document_edit(documents, handle, 1, 0, 8, "changed") == WORKSPACE_DOCUMENT_OK);
    tool_write(path, "external");
    REQUIRE(workspace_document_save(documents, handle, 2) == WORKSPACE_DOCUMENT_CONFLICT);
    char *disk = tool_read(path);
    REQUIRE(!strcmp(disk, "external"));
    free(disk);
    REQUIRE(view(handle).dirty && view(handle).revision == 2);
    tool_write(path, "original");
    REQUIRE(workspace_document_save(documents, handle, 2) == WORKSPACE_DOCUMENT_OK);
    disk = tool_read(path);
    REQUIRE(!strcmp(disk, "changed"));
    free(disk);
    struct stat info;
    REQUIRE(!stat(path, &info) && (info.st_mode & 0777) == 0640);
    REQUIRE(!view(handle).dirty && view(handle).can_undo);
    REQUIRE(
        workspace_document_undo(documents, handle, 2) == WORKSPACE_DOCUMENT_OK &&
        view(handle).dirty
    );
    REQUIRE(workspace_document_open(documents, "../escape", &again) == WORKSPACE_DOCUMENT_INVALID);
    REQUIRE(workspace_document_open(documents, "/absolute", &again) == WORKSPACE_DOCUMENT_INVALID);
    REQUIRE(workspace_document_open(documents, "./source.chibi", &again) == WORKSPACE_DOCUMENT_INVALID);
    snprintf(path, sizeof path, "%s/link", root);
    REQUIRE(!symlink("source.chibi", path));
    REQUIRE(workspace_document_open(documents, "link", &again) == WORKSPACE_DOCUMENT_IO);
    snprintf(path, sizeof path, "%s/fifo", root);
    REQUIRE(!mkfifo(path, 0600));
    REQUIRE(workspace_document_open(documents, "fifo", &again) == WORKSPACE_DOCUMENT_IO);
    snprintf(path, sizeof path, "%s/oversize", root);
    int fd = open(path, O_CREAT | O_EXCL | O_WRONLY, 0600);
    REQUIRE(fd >= 0);
    REQUIRE(!ftruncate(fd, WORKSPACE_DOCUMENT_BYTES_MAX + 1) && !close(fd));
    REQUIRE(workspace_document_open(documents, "oversize", &again) == WORKSPACE_DOCUMENT_LIMIT);
    snprintf(path, sizeof path, "%s/nul", root);
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    REQUIRE(fwrite("a\0b", 1, 3, file) == 3 && !fclose(file));
    REQUIRE(workspace_document_open(documents, "nul", &again) == WORKSPACE_DOCUMENT_UTF8);
    snprintf(path, sizeof path, "%s/dirlink", root);
    REQUIRE(!symlink(".", path));
    REQUIRE(workspace_document_open(documents, "dirlink/source.chibi", &again) == WORKSPACE_DOCUMENT_IO);
    snprintf(path, sizeof path, "%s/source.chibi", root);
    tool_write(path, "fresh");
    REQUIRE(workspace_document_reload(documents, handle, 3) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        view(handle).revision == 4 && !view(handle).dirty &&
        !strcmp(view(handle).text, "fresh")
    );
    REQUIRE(workspace_document_reload(documents, handle, 3) == WORKSPACE_DOCUMENT_STALE);
    REQUIRE(workspace_document_undo(documents, handle, 4) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view(handle).dirty && !strcmp(view(handle).text, "original"));
    disk = tool_read(path);
    REQUIRE(!strcmp(disk, "fresh"));
    free(disk);
    return true;
}
TEST_CASE(workspace_document_path_operations_are_exclusive_and_revisioned)
{
    snprintf(root, sizeof root, "%s/workspace-document-paths-XXXXXX", tool_root());
    REQUIRE(mkdtemp(root));
    workspace_documents_free(documents);
    workspace_document_status status;
    documents = workspace_documents_new(root, &status);
    REQUIRE(documents && !status);
    int handle, refused = -1;
    REQUIRE(workspace_document_create(documents, "one.chibi", "one", &handle) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_create(documents, "one.chibi", "overwrite", &refused) ==
            WORKSPACE_DOCUMENT_EXISTS &&
        refused == -1
    );
    REQUIRE(workspace_document_edit(documents, handle, 1, 0, 3, "two") == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_rename(documents, handle, 2, "renamed.chibi") == WORKSPACE_DOCUMENT_DIRTY);
    REQUIRE(workspace_document_remove(documents, handle, 2) == WORKSPACE_DOCUMENT_DIRTY);
    REQUIRE(workspace_document_save_as(documents, handle, 1, "two.chibi") == WORKSPACE_DOCUMENT_STALE);
    REQUIRE(workspace_document_save_as(documents, handle, 2, "two.chibi") == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        !view(handle).dirty && view(handle).revision == 3 &&
        !strcmp(view(handle).path, "two.chibi")
    );
    REQUIRE(workspace_document_save_as(documents, handle, 3, "one.chibi") == WORKSPACE_DOCUMENT_EXISTS);
    char path[1200];
    snprintf(path, sizeof path, "%s/one.chibi", root);
    char *text = tool_read(path);
    REQUIRE(!strcmp(text, "one"));
    free(text);
    REQUIRE(workspace_document_rename(documents, handle, 3, "one.chibi") == WORKSPACE_DOCUMENT_EXISTS);
#if defined(__APPLE__) || (defined(__linux__) && !defined(__EMSCRIPTEN__))
    REQUIRE(workspace_document_rename(documents, handle, 3, "renamed.chibi") == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view(handle).revision == 4 && !strcmp(view(handle).path, "renamed.chibi"));
    snprintf(path, sizeof path, "%s/two.chibi", root);
    REQUIRE(access(path, F_OK));
    snprintf(path, sizeof path, "%s/renamed.chibi", root);
    tool_write(path, "external");
    REQUIRE(workspace_document_remove(documents, handle, 4) == WORKSPACE_DOCUMENT_CONFLICT);
    tool_write(path, "two");
    REQUIRE(workspace_document_remove(documents, handle, 4) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(access(path, F_OK));
    workspace_document_view closed;
    REQUIRE(workspace_document_read(documents, handle, &closed) == WORKSPACE_DOCUMENT_INVALID);
#else
    REQUIRE(
        workspace_document_rename(documents, handle, 3, "renamed.chibi") == WORKSPACE_DOCUMENT_UNAVAILABLE
    );
#endif
    int memory;
    REQUIRE(workspace_document_memory(documents, "draft", &memory) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_save_as(documents, memory, 1, "draft.chibi") == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view(memory).revision == 2 && !strcmp(view(memory).path, "draft.chibi"));
    REQUIRE(workspace_document_remove(documents, memory, 2) == WORKSPACE_DOCUMENT_OK);
    return true;
}

TEST_CASE(workspace_document_drafts_do_not_write_and_first_save_is_exclusive)
{
    snprintf(root, sizeof root, "%s/workspace-document-drafts-XXXXXX", tool_root());
    REQUIRE(mkdtemp(root));
    workspace_documents_free(documents);
    workspace_document_status status;
    documents = workspace_documents_new(root, &status);
    REQUIRE(documents && !status);
    int source, draft;
    REQUIRE(!workspace_document_create(documents, "Sample.chibi", "before", &source));
    REQUIRE(!workspace_document_draft(documents, "Sample.chibi.meta", "", &draft));
    char path[1200];
    snprintf(path, sizeof path, "%s/Sample.chibi.meta", root);
    REQUIRE(access(path, F_OK) != 0);
    REQUIRE(view(draft).dirty && !view(draft).can_undo);
    REQUIRE(!strcmp(view(draft).path, "Sample.chibi.meta"));
    bool exists;
    int found, revision;
    REQUIRE(
        !workspace_document_path(documents, "missing.meta", &exists, &found, &revision) &&
        !exists && !found
    );
    REQUIRE(
        !workspace_document_path(documents, "Sample.chibi.meta", &exists, &found, &revision) &&
        exists && found == draft && revision == 1
    );
    REQUIRE(
        workspace_document_path(documents, "../escape", &exists, &found, &revision) ==
        WORKSPACE_DOCUMENT_INVALID
    );
    int ignored;
    REQUIRE(
        workspace_document_draft(documents, "Sample.chibi.meta", "x", &ignored) == WORKSPACE_DOCUMENT_EXISTS
    );
    workspace_document_replacement changes[] = { { source, 1, "after" },
                                                 { draft, 1, "雪 layout" } };
    REQUIRE(!workspace_document_replace_many(documents, changes, 2));
    REQUIRE(access(path, F_OK) != 0);
    REQUIRE(!workspace_document_undo(documents, draft, view(draft).revision));
    REQUIRE(!strcmp(view(source).text, "before") && !strcmp(view(draft).text, ""));
    REQUIRE(view(draft).dirty && access(path, F_OK) != 0);
    REQUIRE(!workspace_document_redo(documents, source, view(source).revision));
    tool_write(path, "foreign");
    REQUIRE(workspace_document_save(documents, draft, view(draft).revision) == WORKSPACE_DOCUMENT_EXISTS);
    char *disk = tool_read(path);
    REQUIRE(!strcmp(disk, "foreign"));
    free(disk);
    REQUIRE(view(draft).dirty && !strcmp(view(draft).text, "雪 layout"));
    REQUIRE(!unlink(path));
    REQUIRE(!workspace_document_save(documents, draft, view(draft).revision));
    REQUIRE(!view(draft).dirty);
    disk = tool_read(path);
    REQUIRE(!strcmp(disk, "雪 layout"));
    free(disk);
    REQUIRE(!workspace_document_close(documents, draft));
    REQUIRE(!workspace_document_open(documents, "Sample.chibi.meta", &draft));
    REQUIRE(!view(draft).dirty && !strcmp(view(draft).text, "雪 layout"));
    REQUIRE(workspace_document_draft(documents, "../escape", "", &ignored) == WORKSPACE_DOCUMENT_INVALID);
    REQUIRE(!workspace_document_draft(documents, "discard.meta", "unsaved", &ignored));
    REQUIRE(!workspace_document_close(documents, ignored));
    snprintf(path, sizeof path, "%s/discard.meta", root);
    REQUIRE(access(path, F_OK) != 0);
    return true;
}

#endif
TEST_CASE(workspace_document_anchored_layout_history_is_accessible_from_unchanged_source)
{
    int source, meta;
    REQUIRE(!workspace_document_memory(documents, "source", &source));
    REQUIRE(!workspace_document_memory(documents, "layout", &meta));
    workspace_document_replacement changes[] = { { source, 1, "source" },
                                                 { meta, 1, "layout" } };
    REQUIRE(!workspace_document_replace_anchored(documents, changes, 2, source));
    REQUIRE(view(source).revision == 1 && !view(source).can_undo);
    changes[1].text = "moved";
    REQUIRE(workspace_document_replace_anchored(documents, changes, 2, 999) == WORKSPACE_DOCUMENT_INVALID);
    REQUIRE(view(meta).revision == 1);
    REQUIRE(!workspace_document_replace_anchored(documents, changes, 2, source));
    REQUIRE(view(source).revision == 2 && view(source).can_undo && !view(source).dirty);
    REQUIRE(!workspace_document_undo(documents, source, 2));
    REQUIRE(!strcmp(view(meta).text, "layout") && !strcmp(view(source).text, "source"));
    REQUIRE(!workspace_document_redo(documents, source, view(source).revision));
    REQUIRE(!strcmp(view(meta).text, "moved"));
    return true;
}
TEST_CASE(workspace_document_batch_is_atomic_and_undo_redo_restore_all_members)
{
    int a, b;
    REQUIRE(workspace_document_memory(documents, "first", &a) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_memory(documents, "second", &b) == WORKSPACE_DOCUMENT_OK);
    workspace_document_replacement changes[] = { { a, 1, "renamed first" },
                                                 { b, 2, "renamed second" } };
    REQUIRE(workspace_document_replace_many(documents, changes, 2) == WORKSPACE_DOCUMENT_STALE);
    REQUIRE(view(a).revision == 1 && !view(a).can_undo && !strcmp(view(a).text, "first"));
    changes[1].revision = 1;
    REQUIRE(workspace_document_replace_many(documents, changes, 2) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view(a).revision == 2 && view(b).revision == 2);
    REQUIRE(workspace_document_edit(documents, b, 2, 0, 0, "local ") == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_undo(documents, a, 2) == WORKSPACE_DOCUMENT_CONFLICT);
    REQUIRE(!strcmp(view(a).text, "renamed first") && view(b).revision == 3);
    REQUIRE(workspace_document_undo(documents, b, 3) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_undo(documents, a, 2) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strcmp(view(a).text, "first") && !strcmp(view(b).text, "second"));
    REQUIRE(workspace_document_redo(documents, b, view(b).revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strcmp(view(a).text, "renamed first") && !strcmp(view(b).text, "renamed second"));
    REQUIRE(workspace_document_redo(documents, b, view(b).revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strcmp(view(b).text, "local renamed second"));
    return true;
}
TEST_CASE(workspace_document_batch_branch_close_and_gestures_keep_groups_atomic)
{
    int a, b;
    REQUIRE(workspace_document_memory(documents, "a", &a) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_memory(documents, "b", &b) == WORKSPACE_DOCUMENT_OK);
    workspace_document_replacement changes[] = { { a, 1, "A" }, { b, 1, "B" } };
    REQUIRE(workspace_document_replace_many(documents, changes, 2) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_undo(documents, a, 2) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_begin(documents, b, 3) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_edit(documents, a, 3, 0, 1, "branch") == WORKSPACE_DOCUMENT_TRANSACTION);
    REQUIRE(workspace_document_close(documents, a) == WORKSPACE_DOCUMENT_TRANSACTION);
    REQUIRE(workspace_document_edit(documents, b, 3, 0, 1, "draft") == WORKSPACE_DOCUMENT_OK);
    changes[0].revision = 3;
    changes[1].revision = 4;
    REQUIRE(workspace_document_replace_many(documents, changes, 2) == WORKSPACE_DOCUMENT_TRANSACTION);
    REQUIRE(workspace_document_cancel(documents, b, 4) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_redo(documents, a, 3) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strcmp(view(a).text, "A") && !strcmp(view(b).text, "B"));
    REQUIRE(workspace_document_undo(documents, b, view(b).revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_edit(documents, a, view(a).revision, 0, 1, "branch") == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(!view(b).can_redo);
    REQUIRE(
        workspace_document_replace_many(
            documents,
            (workspace_document_replacement[]){ { a, view(a).revision, "A" },
                                                { b, view(b).revision, "B" } },
            2
        ) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(workspace_document_close(documents, a) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!view(b).can_undo && !view(b).can_redo && !strcmp(view(b).text, "B"));
    return true;
}
TEST_CASE(workspace_document_batch_rejects_duplicate_invalid_utf8_and_preserves_noops)
{
    int a, b;
    REQUIRE(workspace_document_memory(documents, "a", &a) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_memory(documents, "b", &b) == WORKSPACE_DOCUMENT_OK);
    workspace_document_replacement duplicate[] = { { a, 1, "A" }, { a, 1, "B" } };
    REQUIRE(workspace_document_replace_many(documents, duplicate, 2) == WORKSPACE_DOCUMENT_INVALID);
    workspace_document_replacement invalid[] = { { a, 1, "A" }, { b, 1, "\xc0\xaf" } };
    REQUIRE(workspace_document_replace_many(documents, invalid, 2) == WORKSPACE_DOCUMENT_UTF8);
    REQUIRE(view(a).revision == 1 && view(b).revision == 1 && !view(a).can_undo);
    REQUIRE(workspace_document_edit(documents, b, 1, 0, 1, "B") == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_undo(documents, b, 2) == WORKSPACE_DOCUMENT_OK);
    workspace_document_replacement changes[] = { { a, 1, "A" }, { b, 3, "b" } };
    REQUIRE(workspace_document_replace_many(documents, changes, 2) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view(b).revision == 3 && view(b).can_redo);
    REQUIRE(workspace_document_redo(documents, b, 3) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strcmp(view(a).text, "A") && !strcmp(view(b).text, "B"));
    return true;
}
TEST_CASE(workspace_document_group_pruning_discards_dependent_redo_snapshots)
{
    int a, b;
    REQUIRE(workspace_document_memory(documents, "a", &a) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_memory(documents, "b", &b) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_replace_many(
            documents,
            (workspace_document_replacement[]){ { a, 1, "A" }, { b, 1, "B" } },
            2
        ) == WORKSPACE_DOCUMENT_OK
    );
    REQUIRE(workspace_document_edit(documents, b, 2, 0, 0, "local ") == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_undo(documents, b, 3) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_undo(documents, a, 2) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_edit(documents, a, 3, 0, 1, "branch") == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!view(b).can_redo && !strcmp(view(b).text, "b"));
    REQUIRE(
        workspace_document_replace_many(
            documents,
            (workspace_document_replacement[]){ { a, 4, "A" },
                                                { b, view(b).revision, "B" } },
            2
        ) == WORKSPACE_DOCUMENT_OK
    );
    for (int i = 0; i < WORKSPACE_DOCUMENT_HISTORY_MAX; i++) {
        workspace_document_view current = view(a);
        REQUIRE(
            workspace_document_edit(
                documents,
                a,
                current.revision,
                0,
                (int)current.bytes,
                i % 2 ? "even" : "odd"
            ) == WORKSPACE_DOCUMENT_OK
        );
    }
    REQUIRE(!view(b).can_undo && !strcmp(view(b).text, "B"));
    return true;
}
TEST_CASE(workspace_document_preflight_preserves_redo_and_rechecks_revisions)
{
    int source, metadata;
    REQUIRE(!workspace_document_memory(documents, "before", &source));
    REQUIRE(!workspace_document_memory(documents, "layout", &metadata));
    workspace_document_replacement accepted[] = { { source, 1, "accepted" },
                                                  { metadata, 1, "moved" } };
    REQUIRE(!workspace_document_replace_anchored(documents, accepted, 2, source));
    REQUIRE(!workspace_document_undo(documents, source, 2));
    workspace_document_view a = view(source), b = view(metadata);
    workspace_document_replacement proposed[] = { { source, a.revision, "proposal" },
                                                  { metadata, b.revision, "candidate" } };
    REQUIRE(!workspace_document_validate_replace_anchored(documents, proposed, 2, source));
    REQUIRE(!workspace_document_validate_edit(documents, source, a.revision, 0, 6, "proposal"));
    REQUIRE(
        workspace_document_validate_edit(documents, source, a.revision, 0, 99, "proposal") ==
        WORKSPACE_DOCUMENT_INVALID
    );
    REQUIRE(
        workspace_document_validate_edit(documents, source, a.revision, 0, 6, "\xc0\xaf") ==
        WORKSPACE_DOCUMENT_UTF8
    );
    REQUIRE(view(source).revision == a.revision && view(metadata).revision == b.revision);
    REQUIRE(view(source).can_redo && view(metadata).can_redo);
    REQUIRE(!strcmp(view(source).text, "before") && !strcmp(view(metadata).text, "layout"));
    REQUIRE(!workspace_document_redo(documents, source, a.revision));
    REQUIRE(!strcmp(view(source).text, "accepted") && !strcmp(view(metadata).text, "moved"));
    REQUIRE(
        workspace_document_replace_anchored(documents, proposed, 2, source) == WORKSPACE_DOCUMENT_STALE
    );
    REQUIRE(
        workspace_document_edit(documents, source, a.revision, 0, 6, "proposal") == WORKSPACE_DOCUMENT_STALE
    );
    REQUIRE(!strcmp(view(source).text, "accepted") && !strcmp(view(metadata).text, "moved"));
    behavior_function_context context = { .value_storage = &storage,
                                          .copy_string = owned_string };
    pin_value input[] = { { .i = source },
                          { .i = view(source).revision },
                          { .i = 0 },
                          { .i = 8 },
                          { .str = "next" } },
              output[3] = { { 0 } };
    REQUIRE(!WORKSPACE_DOCUMENT_VALIDATE_EDIT_CALL.context(&context, documents, input, output));
    REQUIRE(output[0].b && !output[1].i && !strcmp(output[2].str, ""));
    REQUIRE(!strcmp(view(source).text, "accepted"));
    return true;
}
#if !defined(_WIN32) && defined(O_NOFOLLOW)
TEST_CASE(workspace_document_missing_candidate_preflight_reserves_nothing)
{
    snprintf(root, sizeof root, "%s/workspace-document-preflight-XXXXXX", tool_root());
    REQUIRE(mkdtemp(root));
    workspace_documents_free(documents);
    workspace_document_status status;
    documents = workspace_documents_new(root, &status);
    REQUIRE(documents && !status);
    int source;
    REQUIRE(!workspace_document_create(documents, "Sample.chibi", "before", &source));
    REQUIRE(!workspace_document_edit(documents, source, 1, 0, 6, "accepted"));
    REQUIRE(!workspace_document_undo(documents, source, 2));
    workspace_document_candidate candidates[] = {
        { source, 3, "Sample.chibi", "before" },
        { 0, -1, "Sample.chibi.meta", "layout" }
    };
    REQUIRE(!workspace_document_validate_candidates(documents, candidates, 2, source));
    REQUIRE(workspace_document_handles(documents, NULL, 0) == 1);
    REQUIRE(
        view(source).revision == 3 && view(source).can_redo &&
        !strcmp(view(source).text, "before")
    );
    bool exists;
    int handle, revision;
    REQUIRE(!workspace_document_path(documents, "Sample.chibi.meta", &exists, &handle, &revision));
    REQUIRE(!exists && !handle);
    candidates[1].path = "Sample.chibi";
    REQUIRE(
        workspace_document_validate_candidates(documents, candidates, 2, source) == WORKSPACE_DOCUMENT_INVALID
    );
    candidates[1].path = "../outside";
    REQUIRE(
        workspace_document_validate_candidates(documents, candidates, 2, source) == WORKSPACE_DOCUMENT_INVALID
    );
    candidates[1].path = "Sample.chibi.meta";
    candidates[1].revision = 0;
    REQUIRE(
        workspace_document_validate_candidates(documents, candidates, 2, source) == WORKSPACE_DOCUMENT_INVALID
    );
    candidates[1].revision = -1;
    REQUIRE(!workspace_document_redo(documents, source, 3));
    REQUIRE(
        workspace_document_validate_candidates(documents, candidates, 2, source) == WORKSPACE_DOCUMENT_STALE
    );
    candidates[0].revision = 4;
    candidates[0].text = "accepted";
    REQUIRE(!workspace_document_validate_candidates(documents, candidates, 2, source));
    REQUIRE(!workspace_document_draft(documents, "Sample.chibi.meta", "", &handle));
    REQUIRE(
        workspace_document_validate_candidates(documents, candidates, 2, source) == WORKSPACE_DOCUMENT_EXISTS
    );
    workspace_document_replacement replacements[] = { { source, 4, "accepted" },
                                                      { handle, 1, "layout" } };
    REQUIRE(!workspace_document_replace_anchored(documents, replacements, 2, source));
    REQUIRE(!workspace_document_undo(documents, source, 5));
    REQUIRE(!strcmp(view(source).text, "accepted") && !strcmp(view(handle).text, ""));
    REQUIRE(!workspace_document_redo(documents, source, 6));
    REQUIRE(!strcmp(view(handle).text, "layout"));
    return true;
}
#endif
TEST_CASE(workspace_document_snapshot_roundtrip_preserves_grouped_redo_and_identity)
{
    int source, metadata, retired;
    REQUIRE(!workspace_document_memory(documents, "before", &source));
    REQUIRE(!workspace_document_memory(documents, "layout", &metadata));
    REQUIRE(!workspace_document_memory(documents, "retired", &retired));
    REQUIRE(!workspace_document_close(documents, retired));
    workspace_document_replacement replacement[] = { { source, 1, "after" },
                                                     { metadata, 1, "moved" } };
    REQUIRE(!workspace_document_replace_anchored(documents, replacement, 2, source));
    REQUIRE(!workspace_document_undo(documents, source, 2));
    unsigned char *bytes = NULL;
    size_t size = 0;
    REQUIRE(!workspace_documents_snapshot(documents, &bytes, &size));
    REQUIRE(bytes && size && view(source).can_redo && view(metadata).can_redo);
    /* A restored group cannot wrap its eviction clock across multiple members. */
    unsigned char *near_limit = malloc(size);
    memcpy(near_limit, bytes, size);
    memset(near_limit + 20, 0xff, 8);
    near_limit[20] = 0xfe;
    workspace_document_status counter_status;
    workspace_documents *counter = workspace_documents_new(NULL, &counter_status);
    REQUIRE(
        counter && !counter_status &&
        !workspace_documents_restore(counter, near_limit, size)
    );
    REQUIRE(workspace_document_redo(counter, source, 3) == WORKSPACE_DOCUMENT_LIMIT);
    workspace_document_view unchanged;
    REQUIRE(!workspace_document_read(counter, source, &unchanged));
    REQUIRE(unchanged.revision == 3 && unchanged.can_redo && !strcmp(unchanged.text, "before"));
    workspace_documents_free(counter);
    free(near_limit);
    workspace_document_status status;
    workspace_documents *restored = workspace_documents_new(NULL, &status);
    REQUIRE(restored && !status);
    REQUIRE(!workspace_documents_restore(restored, bytes, size));
    REQUIRE(workspace_documents_restore(restored, bytes, size) == WORKSPACE_DOCUMENT_INVALID);
    workspace_documents_free(documents);
    documents = restored;
    REQUIRE(view(source).revision == 3 && view(metadata).revision == 3);
    REQUIRE(!strcmp(view(source).text, "before") && !strcmp(view(metadata).text, "layout"));
    REQUIRE(view(source).can_redo && view(metadata).can_redo);
    REQUIRE(!workspace_document_redo(documents, metadata, 3));
    REQUIRE(!strcmp(view(source).text, "after") && !strcmp(view(metadata).text, "moved"));
    REQUIRE(!workspace_document_undo(documents, source, 4));
    REQUIRE(!strcmp(view(source).text, "before") && !strcmp(view(metadata).text, "layout"));
    int next;
    REQUIRE(!workspace_document_memory(documents, "new", &next));
    REQUIRE(next > retired);
    free(bytes);
    return true;
}
TEST_CASE(workspace_document_snapshot_refuses_transactions_and_invalid_candidates_atomically)
{
    int source;
    REQUIRE(!workspace_document_memory(documents, "before", &source));
    REQUIRE(!workspace_document_edit(documents, source, 1, 0, 6, "after"));
    unsigned char *bytes = NULL;
    size_t size = 0;
    REQUIRE(!workspace_documents_snapshot(documents, &bytes, &size));
    REQUIRE(!workspace_document_begin(documents, source, 2));
    unsigned char *refused = (void *)1;
    size_t refused_size = 1;
    REQUIRE(
        workspace_documents_snapshot(documents, &refused, &refused_size) == WORKSPACE_DOCUMENT_TRANSACTION
    );
    REQUIRE(!refused && !refused_size);
    REQUIRE(!workspace_document_cancel(documents, source, 2));
    workspace_document_status status;
    workspace_documents *fresh = workspace_documents_new(NULL, &status);
    REQUIRE(fresh && !status);
    unsigned char *broken = malloc(size + 1);
    memcpy(broken, bytes, size);
    broken[size] = 0;
    REQUIRE(workspace_documents_restore(fresh, broken, size + 1) == WORKSPACE_DOCUMENT_INVALID);
    REQUIRE(workspace_documents_restore(fresh, bytes, size - 1) == WORKSPACE_DOCUMENT_INVALID);
    broken[8] = 2;
    REQUIRE(workspace_documents_restore(fresh, broken, size) == WORKSPACE_DOCUMENT_INVALID);
    memcpy(broken, bytes, size);
    broken[40] = 0; // Invalid document revision.
    REQUIRE(workspace_documents_restore(fresh, broken, size) == WORKSPACE_DOCUMENT_INVALID);
    memcpy(broken, bytes, size);
    broken[64] = 1; // Current text cannot own a history group.
    REQUIRE(workspace_documents_restore(fresh, broken, size) == WORKSPACE_DOCUMENT_INVALID);
    memcpy(broken, bytes, size);
    broken[72] = 0xff; // Invalid UTF-8 text.
    REQUIRE(workspace_documents_restore(fresh, broken, size) == WORKSPACE_DOCUMENT_UTF8);
    REQUIRE(workspace_document_handles(fresh, NULL, 0) == 0);
    REQUIRE(!workspace_documents_restore(fresh, bytes, size));
    workspace_document_view current;
    REQUIRE(!workspace_document_read(fresh, source, &current));
    REQUIRE(current.revision == 2 && current.can_undo && !strcmp(current.text, "after"));
    workspace_documents_free(fresh);
    free(broken);
    free(bytes);
    return true;
}
#if !defined(_WIN32) && defined(O_NOFOLLOW)
TEST_CASE(workspace_document_snapshot_keeps_dirty_baselines_and_unsaved_drafts)
{
    snprintf(root, sizeof root, "%s/workspace-history-XXXXXX", tool_root());
    REQUIRE(mkdtemp(root));
    workspace_documents_free(documents);
    workspace_document_status status;
    documents = workspace_documents_new(root, &status);
    REQUIRE(documents && !status);
    int source, draft;
    REQUIRE(!workspace_document_create(documents, "Sample.chibi", "disk", &source));
    REQUIRE(!workspace_document_edit(documents, source, 1, 0, 4, "dirty"));
    REQUIRE(!workspace_document_draft(documents, "Sample.chibi.meta", "layout", &draft));
    unsigned char *bytes = NULL;
    size_t size = 0;
    REQUIRE(!workspace_documents_snapshot(documents, &bytes, &size));
    workspace_documents *fresh = workspace_documents_new(root, &status);
    REQUIRE(fresh && !status && !workspace_documents_restore(fresh, bytes, size));
    workspace_documents_free(documents);
    documents = fresh;
    REQUIRE(view(source).dirty && !view(source).new_file && view(source).revision == 2);
    REQUIRE(view(draft).dirty && view(draft).new_file && !strcmp(view(draft).text, "layout"));
    bool exists;
    int handle, revision;
    /* Owned draft exists logically; the disk file remains absent. */
    char path[1200];
    snprintf(path, sizeof path, "%s/Sample.chibi.meta", root);
    REQUIRE(access(path, F_OK) != 0);
    REQUIRE(!workspace_document_save(documents, source, 2));
    REQUIRE(!view(source).dirty && !workspace_document_undo(documents, source, 2));
    REQUIRE(view(source).dirty && !strcmp(view(source).text, "disk"));
    REQUIRE(!workspace_document_save(documents, draft, 1));
    REQUIRE(!view(draft).new_file && !view(draft).dirty && access(path, F_OK) == 0);
    REQUIRE(!workspace_document_path(documents, "Sample.chibi.meta", &exists, &handle, &revision));
    REQUIRE(exists && handle == draft);
    free(bytes);
    return true;
}
#endif

TEST_SUITE(test_workspace_documents)
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(workspace_document_snapshot_roundtrip_preserves_grouped_redo_and_identity);
    RUN_TEST_CASE(workspace_document_snapshot_refuses_transactions_and_invalid_candidates_atomically);
    RUN_TEST_CASE(workspace_document_preflight_preserves_redo_and_rechecks_revisions);
    RUN_TEST_CASE(workspace_document_anchored_layout_history_is_accessible_from_unchanged_source);
    RUN_TEST_CASE(workspace_document_group_pruning_discards_dependent_redo_snapshots);
    RUN_TEST_CASE(workspace_document_batch_is_atomic_and_undo_redo_restore_all_members);
    RUN_TEST_CASE(workspace_document_batch_branch_close_and_gestures_keep_groups_atomic);
    RUN_TEST_CASE(workspace_document_batch_rejects_duplicate_invalid_utf8_and_preserves_noops);
    RUN_TEST_CASE(workspace_documents_group_cancel_and_reject_stale_utf8_edits);
    RUN_TEST_CASE(workspace_documents_limits_preserve_history_and_handle_identity);
    RUN_TEST_CASE(workspace_documents_reserve_cancel_and_release_committed_budget);
    RUN_TEST_CASE(workspace_documents_byte_budget_evicts_old_snapshots);
    RUN_TEST_CASE(workspace_document_bindings_copy_results_and_report_refusals);
    RUN_TEST_CASE(workspace_document_absent_service_refuses_every_native_operation);
#if !defined(_WIN32) && defined(O_NOFOLLOW)
    RUN_TEST_CASE(workspace_document_snapshot_keeps_dirty_baselines_and_unsaved_drafts);
    RUN_TEST_CASE(workspace_document_missing_candidate_preflight_reserves_nothing);
    RUN_TEST_CASE(workspace_documents_save_detects_conflicts_and_refuses_unsafe_files);
    RUN_TEST_CASE(workspace_document_path_operations_are_exclusive_and_revisioned);
    RUN_TEST_CASE(workspace_document_drafts_do_not_write_and_first_save_is_exclusive);
#endif
    pu_clear_setup();
}
