#include "authoring/workspace_file_history.h"

#include "authoring/workspace_file_history_definition.h"
#include "execution/function.h"
#include "support/rooted_files.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
static char root[128];
static rooted_files *files;
static workspace_documents *documents;
static workspace_file_history *history;
static void cleanup(void)
{
    workspace_file_history_free(history);
    history = NULL;
    workspace_documents_free(documents);
    documents = NULL;
    rooted_files_close(files);
    files = NULL;
    if (*root) {
        tool_remove_tree(root);
        *root = 0;
    }
}
static bool setup(void)
{
    strcpy(root, "/tmp/chibi-file-history-XXXXXX");
    REQUIRE(mkdtemp(root));
    files = rooted_files_open(root);
    documents = workspace_documents_new(root, NULL);
    history = workspace_file_history_new(root, documents);
    REQUIRE(files && documents && history);
    return true;
}
static bool says(const char *path, const void *expected, size_t count)
{
    unsigned char *bytes = NULL;
    size_t n = 0;
    bool ok = rooted_files_read(files, path, WORKSPACE_FILE_HISTORY_FILE_BYTES_MAX, &bytes, &n) &&
              n == count && !memcmp(bytes, expected, n);
    free(bytes);
    return ok;
}
static bool missing(const char *path)
{
    rooted_file_info info;
    return !rooted_files_stat(files, path, &info);
}
TEST_CASE(file_history_restores_binary_batches_and_redoes_exactly)
{
    REQUIRE(setup());
    const unsigned char binary[] = { 0, 255, 1, 0 };
    REQUIRE(rooted_files_write(files, "a.txt", "alpha", 5, 16, false, NULL, 0));
    REQUIRE(rooted_files_write(files, "b.bin", binary, sizeof binary, 16, false, NULL, 0));
    const char *paths[] = { "a.txt", "b.bin" };
    workspace_file_history_result r = workspace_files_delete(history, paths, 2);
    REQUIRE(r.ok && r.token > 0 && r.changed == 2 && !strcmp(r.state, "deleted"));
    int token = r.token;
    REQUIRE(missing("a.txt") && missing("b.bin"));
    workspace_file_history_view v;
    REQUIRE(workspace_file_history_read(history, token, &v));
    REQUIRE(
        v.can_undo && !v.can_redo && v.count == 2 && v.bytes == 9 &&
        says(v.recovery[1], binary, sizeof binary)
    );
    r = workspace_files_restore(history, token);
    REQUIRE(
        r.ok && r.changed == 2 && says("a.txt", "alpha", 5) &&
        says("b.bin", binary, sizeof binary)
    );
    r = workspace_files_redo(history, token);
    REQUIRE(r.ok && missing("a.txt") && missing("b.bin"));
    r = workspace_files_restore(history, token);
    REQUIRE(r.ok);
    r = workspace_files_forget(history, token);
    REQUIRE(r.ok && !workspace_file_history_read(history, token, &v));
    REQUIRE(says("a.txt", "alpha", 5) && says("b.bin", binary, sizeof binary));
    return true;
}
TEST_CASE(file_history_conflicts_preflight_entire_batch_without_overwrite)
{
    REQUIRE(setup());
    REQUIRE(rooted_files_write(files, "a", "a", 1, 8, false, NULL, 0));
    REQUIRE(rooted_files_write(files, "b", "b", 1, 8, false, NULL, 0));
    const char *paths[] = { "a", "b" };
    workspace_file_history_result r = workspace_files_delete(history, paths, 2);
    REQUIRE(r.ok);
    int token = r.token;
    REQUIRE(rooted_files_write(files, "a", "foreign", 7, 8, false, NULL, 0));
    r = workspace_files_restore(history, token);
    REQUIRE(!r.ok && r.changed == 0 && says("a", "foreign", 7) && missing("b"));
    REQUIRE(rooted_files_remove(files, "a"));
    REQUIRE(workspace_files_restore(history, token).ok);
    REQUIRE(rooted_files_write(files, "b", "edited", 6, 8, true, "b", 1));
    r = workspace_files_redo(history, token);
    REQUIRE(!r.ok && r.changed == 0 && says("a", "a", 1) && says("b", "edited", 6));
    REQUIRE(rooted_files_write(files, "b", "b", 1, 8, true, "edited", 6));
    workspace_file_history_view v;
    REQUIRE(workspace_file_history_read(history, token, &v));
    REQUIRE(rooted_files_write(files, v.recovery[0], "foreign", 7, 8, true, "a", 1));
    r = workspace_files_redo(history, token);
    REQUIRE(!r.ok && r.changed == 0 && strstr(r.error, "recovery baseline"));
    REQUIRE(says("a", "a", 1) && says("b", "b", 1) && says(v.recovery[0], "foreign", 7));
    REQUIRE(rooted_files_write(files, v.recovery[0], "a", 1, 8, true, "foreign", 7));
    REQUIRE(rooted_files_remove(files, v.recovery[1]));
    r = workspace_files_redo(history, token);
    REQUIRE(!r.ok && r.changed == 0 && says("a", "a", 1) && says("b", "b", 1));
    REQUIRE(workspace_files_forget(history, token).ok);
    return true;
}
TEST_CASE(file_history_refuses_dirty_documents_and_symlink_batches)
{
    REQUIRE(setup());
    REQUIRE(rooted_files_write(files, "source", "clean", 5, 16, false, NULL, 0));
    int document;
    REQUIRE(workspace_document_open(documents, "source", &document) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_edit(documents, document, 1, 0, 5, "dirty") == WORKSPACE_DOCUMENT_OK);
    const char *source[] = { "source" };
    workspace_file_history_result r = workspace_files_delete(history, source, 1);
    REQUIRE(!r.ok && r.token == 0 && says("source", "clean", 5));
    REQUIRE(workspace_document_close(documents, document) == WORKSPACE_DOCUMENT_OK);
    char link[256];
    snprintf(link, sizeof link, "%s/link", root);
    REQUIRE(!symlink("source", link));
    const char *linked[] = { "source", "link" };
    r = workspace_files_delete(history, linked, 2);
    REQUIRE(!r.ok && r.token == 0 && says("source", "clean", 5));
    const char *unsafe[] = { "../source" };
    REQUIRE(!workspace_files_delete(history, unsafe, 1).ok);
    return true;
}
TEST_CASE(file_history_byte_and_token_limits_refuse_before_source_mutation)
{
    REQUIRE(setup());
    const char *large[] = { "large0", "large1", "large2" };
    for (int i = 0; i < 3; i++) {
        char path[256];
        snprintf(path, sizeof path, "%s/%s", root, large[i]);
        int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
        REQUIRE(fd >= 0);
        REQUIRE(!ftruncate(fd, 12 * 1024 * 1024));
        REQUIRE(!close(fd));
    }
    workspace_file_history_result r = workspace_files_delete(history, large, 3);
    REQUIRE(!r.ok && r.token == 0 && strstr(r.error, "32MiB"));
    for (int i = 0; i < 3; i++) {
        REQUIRE(!missing(large[i]));
    }
    for (int i = 0; i < 8; i++) {
        char path[16];
        snprintf(path, sizeof path, "small%d", i);
        REQUIRE(rooted_files_write(files, path, "x", 1, 8, false, NULL, 0));
        const char *one[] = { path };
        REQUIRE(workspace_files_delete(history, one, 1).ok);
    }
    REQUIRE(rooted_files_write(files, "ninth", "x", 1, 8, false, NULL, 0));
    const char *last[] = { "ninth" };
    r = workspace_files_delete(history, last, 1);
    REQUIRE(!r.ok && r.token == 0 && says("ninth", "x", 1));
    return true;
}
#endif
static arena inactive_storage;
static void inactive_cleanup(void)
{
    arena_free(&inactive_storage);
}
TEST_CASE(file_history_bindings_report_unavailable_without_project)
{
    behavior_function_context context = { .value_storage = &inactive_storage };
    workspace_file_history_view absent = { .bytes = 123, .can_undo = true };
    REQUIRE(!workspace_file_history_read(NULL, 1, &absent));
    REQUIRE(
        !strcmp(absent.result.state, "unavailable") &&
        strstr(absent.result.error, "unavailable") && absent.bytes == 0 &&
        !absent.can_undo && !absent.paths
    );
    for (int i = 0; i < WORKSPACE_FILE_HISTORY_DEFINITION_PACK.binding_count; i++) {
        const native_definition_binding *binding = &WORKSPACE_FILE_HISTORY_DEFINITION_PACK
                                                        .bindings[i];
        const node_host_call *call = binding->implementation.call;
        REQUIRE(call && call->optional_service && call->context);
        pin_value input[1] = { 0 }, output[8] = { 0 };
        REQUIRE(!call->context(&context, NULL, input, output));
        REQUIRE(!output[0].b);
        if (!strcmp(binding->canonical_name, "Read Workspace File History")) {
            REQUIRE(!strcmp(output[1].str, "unavailable") && strstr(output[2].str, "unavailable"));
            REQUIRE(output[3].ptr && ((pin_array *)output[3].ptr)->count == 0);
            REQUIRE(output[4].ptr && ((pin_array *)output[4].ptr)->count == 0);
            REQUIRE(output[5].i == 0 && !output[6].b && !output[7].b);
        } else {
            REQUIRE(
                output[1].i == 0 && !strcmp(output[2].str, "unavailable") &&
                strstr(output[3].str, "unavailable") && output[4].i == 0
            );
        }
    }
    return true;
}
void test_workspace_file_history(void)
{
    pu_setup(NULL, inactive_cleanup);
    RUN_TEST_CASE(file_history_bindings_report_unavailable_without_project);
    pu_clear_setup();
#if !defined(_WIN32)
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(file_history_restores_binary_batches_and_redoes_exactly);
    RUN_TEST_CASE(file_history_conflicts_preflight_entire_batch_without_overwrite);
    RUN_TEST_CASE(file_history_refuses_dirty_documents_and_symlink_batches);
    RUN_TEST_CASE(file_history_byte_and_token_limits_refuse_before_source_mutation);
    pu_clear_setup();
#endif
}
