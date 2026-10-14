#include "authoring/workspace_resources_definition.h"
#include "execution/function.h"
#include "support/asset_json.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static char root[128];
static workspace_resources *workspace;
static arena storage;
static void setup()
{
    strcpy(root, "/tmp/chibi-workspace-resources-XXXXXX");
    assert(mkdtemp(root));
    char path[256], error[256];
    snprintf(path, sizeof path, "%s/content", root);
    assert(!mkdir(path, 0700));
    workspace = workspace_resources_new(root, error, sizeof error);
    assert(workspace);
}
static void teardown()
{
    workspace_resources_free(workspace);
    workspace = NULL;
    arena_free(&storage);
    tool_remove_tree(root);
}
static void put(const char *relative, const char *text)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, relative);
    tool_write(path, text);
}
static pin_array *query(int first, int count, const char *kind, const char *search, workspace_resource_result *result)
{
    pin_array *rows = NULL;
    *result = workspace_resources_query(workspace, first, count, kind, search, &storage, &rows);
    assert(rows);
    return rows;
}
TEST_CASE(workspace_rows_filter_page_and_stable_revision)
{
    put("content/z.kdl", "name \"Last\"\n");
    put("content/a.kdl", "name \"First\"\n");
    workspace_resource_result result = workspace_resources_refresh(workspace);
    REQUIRE(result.ok && result.complete && result.total == 2 && result.revision > 0);
    int revision = result.revision;
    REQUIRE(workspace_resources_refresh(workspace).revision == revision);
    pin_array *rows = query(1, 1, "document", "", &result);
    REQUIRE(result.ok && result.total == 2 && rows->count == 1);
    workspace_resource_row *row = rows->items[0].ptr;
    REQUIRE(!strcmp(row->name, "Last"));
    REQUIRE(!strcmp(row->path, "content/z.kdl"));
    REQUIRE(!strcmp(row->key, "document/content/z.kdl"));
    rows = query(0, 64, "", "FIRST", &result);
    REQUIRE(result.total == 1 && rows->count == 1);
    REQUIRE(!strcmp(((workspace_resource_row *)rows->items[0].ptr)->status, "syntax-valid"));
    put("content/a.kdl", "name \"Changed\"\n");
    REQUIRE(workspace_resources_refresh(workspace).revision != revision);
    return true;
}
TEST_CASE(workspace_snapshot_outlives_refresh_and_host)
{
    put("content/item.rml", "<p>One</p>");
    REQUIRE(workspace_resources_refresh(workspace).ok);
    workspace_resource_result result;
    pin_array *rows = query(0, 64, "", "", &result);
    workspace_resource_row *row = rows->items[0].ptr;
    put("content/item.rml", "<p>Two</p>");
    REQUIRE(workspace_resources_refresh(workspace).ok);
    workspace_resources_free(workspace);
    workspace = NULL;
    REQUIRE(!strcmp(row->name, "item"));
    REQUIRE(!strcmp(row->kind, "ui"));
    REQUIRE(!strcmp(row->key, "ui/content/item.rml"));
    return true;
}
TEST_CASE(workspace_rejects_symlink_and_configured_traversal)
{
    char path[512];
    snprintf(path, sizeof path, "%s/content/outside.rml", root);
    REQUIRE(!symlink("/etc/passwd", path));
    workspace_resource_result result = workspace_resources_refresh(workspace);
    REQUIRE(!result.ok && !result.complete && result.code == WORKSPACE_RESOURCE_UNSAFE_PATH);
    REQUIRE(result.total == 0);
    REQUIRE(!unlink(path));
    put("project.kdl", "name \"Utility\"\ncontent \"../outside\"\n");
    result = workspace_resources_refresh(workspace);
    REQUIRE(!result.ok && result.code == WORKSPACE_RESOURCE_UNSAFE_PATH);
    REQUIRE(result.total == 0);
    put("project.kdl", "name \"Utility\"\ncontent \"content\"\n");
    REQUIRE(workspace_resources_refresh(workspace).ok);
    return true;
}
TEST_CASE(workspace_configured_nested_content_and_missing_lookup)
{
    char path[512];
    snprintf(path, sizeof path, "%s/content/assets", root);
    REQUIRE(!mkdir(path, 0700));
    put("project.kdl", "content \"content/assets\"\n");
    put("content/assets/page.rml", "<p>Example</p>");
    REQUIRE(workspace_resources_refresh(workspace).ok);
    workspace_resource_row row;
    workspace_resource_result result = workspace_resources_lookup(
        workspace,
        "ui/content/assets/page.rml",
        &storage,
        &row
    );
    REQUIRE(result.ok && result.total == 1 && !strcmp(row.path, "content/assets/page.rml"));
    REQUIRE(!strcmp(row.reference, "content/page.rml"));
    result = workspace_resources_lookup(workspace, "ui/content/assets/removed.rml", &storage, &row);
    REQUIRE(!result.ok && result.code == WORKSPACE_RESOURCE_NOT_FOUND && !row.path[0]);
    return true;
}
TEST_CASE(workspace_enforces_query_and_source_limits)
{
    workspace_resource_result result;
    REQUIRE(workspace_resources_refresh(workspace).ok);
    pin_array *rows = query(-1, 1, "", "", &result);
    REQUIRE(!result.ok && result.code == WORKSPACE_RESOURCE_INVALID_QUERY && rows->count == 0);
    rows = query(0, WORKSPACE_RESOURCE_PAGE_MAX + 1, "", "", &result);
    REQUIRE(!result.ok && rows->count == 0);
    char path[512];
    snprintf(path, sizeof path, "%s/content/huge.rml", root);
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    REQUIRE(fd >= 0 && !ftruncate(fd, 512 * 1024 + 1) && !close(fd));
    result = workspace_resources_refresh(workspace);
    REQUIRE(!result.ok && result.code == WORKSPACE_RESOURCE_LIMIT && result.total == 1);
    int revision = result.revision;
    REQUIRE(workspace_resources_refresh(workspace).revision == revision);
    rows = query(0, 1, "", "", &result);
    REQUIRE(
        rows->count == 1 &&
        !strcmp(((workspace_resource_row *)rows->items[0].ptr)->status, "limit_exceeded")
    );
    put("content/new.rml", "<p>Added even while another file is oversized</p>");
    REQUIRE(workspace_resources_refresh(workspace).revision != revision);
    return true;
}
TEST_CASE(workspace_native_query_preserves_typed_result_backing)
{
    put("content/page.rml", "<p>Utility</p>");
    REQUIRE(workspace_resources_refresh(workspace).ok);
    behavior_function_context context = { .value_storage = &storage };
    pin_value inputs[] = { { .type = PIN_I32, .i = 0 },
                           { .type = PIN_I32, .i = 64 },
                           { .type = PIN_STR, .str = "ui" },
                           { .type = PIN_STR, .str = "" } };
    workspace_resource_result status = { 0 };
    pin_value outputs[] = {
        { .type = PIN_STRUCT, .type_name = "WorkspaceResourceResult", .ptr = &status },
        { .type = PIN_STRUCT, .type_name = "WorkspaceResourceRow", .container = PIN_CONTAINER_ARRAY }
    };
    REQUIRE(!WORKSPACE_RESOURCE_QUERY_CALL.context(&context, workspace, inputs, outputs));
    REQUIRE(outputs[0].ptr == &status && status.ok && status.total == 1);
    pin_array *rows = outputs[1].ptr;
    REQUIRE(
        rows && rows->count == 1 &&
        !strcmp(((workspace_resource_row *)rows->items[0].ptr)->kind, "ui")
    );
    REQUIRE(!strcmp(WORKSPACE_RESOURCE_ROW_LAYOUT.fields[0].name, "Key"));
    return true;
}
TEST_CASE(workspace_copies_catalog_and_semantic_reference_annotations)
{
    put("content/image.png", "not decoded");
    put("content/part.kdl", "name \"Part\"\n");
    REQUIRE(workspace_resources_refresh(workspace).ok);
    av_doc *catalog = av_json_read("{\"resources\":[{\"key\":\"content/image.png\",\"status\":\"metadata\",\"metadata\":{\"width\":2,\"height\":3}}]}");
    REQUIRE(catalog);
    workspace_resources_catalog(workspace, catalog);
    av_doc_free(catalog);
    workspace_resource_row row;
    REQUIRE(workspace_resources_lookup(workspace, "image/content/image.png", &storage, &row)
                .ok);
    REQUIRE(row.width == 2 && row.height == 3 && !strcmp(row.status, "metadata"));
    reference_index *index = reference_index_new();
    reference_index_add_asset(index, REFERENCE_ASSET_PROTOTYPE, "Authored Part", "content/part.kdl", NULL);
    reference_index_named_asset(index, REFERENCE_ASSET_MATERIAL, "Missing Material");
    workspace_resources_references(workspace, index);
    reference_index_free(index);
    REQUIRE(workspace_resources_lookup(workspace, "document/content/part.kdl", &storage, &row)
                .ok);
    REQUIRE(!strcmp(row.name, "Authored Part"));
    REQUIRE(workspace_resources_lookup(workspace, "material/Missing Material", &storage, &row)
                .ok);
    REQUIRE(row.unresolved && !strcmp(row.status, "missing"));
    REQUIRE(!row.path[0] && !row.reference[0]);
    int annotated = workspace_resources_lookup(workspace, "document/content/part.kdl", &storage, &row)
                        .revision;
    REQUIRE(workspace_resources_refresh(workspace).revision != annotated);
    REQUIRE(
        workspace_resources_lookup(workspace, "material/Missing Material", &storage, &row)
            .code == WORKSPACE_RESOURCE_NOT_FOUND
    );
    return true;
}
static void inactive_cleanup(void)
{
    arena_free(&storage);
}
TEST_CASE(workspace_resource_bindings_report_unavailable_without_project)
{
    behavior_function_context context = { .value_storage = &storage };
    workspace_resource_result status = { 0 };
    pin_value inputs[4] = { { .type = PIN_I32 },
                            { .type = PIN_I32, .i = 64 },
                            { .type = PIN_STR, .str = "" },
                            { .type = PIN_STR, .str = "" } };
    pin_value outputs[2] = {
        { .type = PIN_STRUCT, .type_name = "WorkspaceResourceResult", .ptr = &status },
        { .type = PIN_STRUCT, .type_name = "WorkspaceResourceRow", .container = PIN_CONTAINER_ARRAY }
    };
    REQUIRE(
        WORKSPACE_RESOURCE_QUERY_CALL.optional_service &&
        WORKSPACE_RESOURCE_REFRESH_CALL.optional_service &&
        WORKSPACE_RESOURCE_LOOKUP_CALL.optional_service
    );
    REQUIRE(!WORKSPACE_RESOURCE_QUERY_CALL.context(&context, NULL, inputs, outputs));
    REQUIRE(
        !status.ok && status.code == WORKSPACE_RESOURCE_UNAVAILABLE &&
        !strcmp(status.status, "unavailable") && strstr(status.message, "unavailable") &&
        !status.complete && status.total == 0 && status.revision == 0
    );
    REQUIRE(
        outputs[0].ptr == &status && outputs[1].ptr &&
        ((pin_array *)outputs[1].ptr)->count == 0
    );
    REQUIRE(!WORKSPACE_RESOURCE_REFRESH_CALL.context(&context, NULL, NULL, outputs));
    REQUIRE(!status.ok && status.code == WORKSPACE_RESOURCE_UNAVAILABLE);
    workspace_resource_row row = { 0 };
    outputs[1] = (pin_value){ .type = PIN_STRUCT,
                              .type_name = "WorkspaceResourceRow",
                              .ptr = &row };
    inputs[0] = (pin_value){ .type = PIN_STR, .str = "image/content/absent.png" };
    REQUIRE(!WORKSPACE_RESOURCE_LOOKUP_CALL.context(&context, NULL, inputs, outputs));
    REQUIRE(
        !status.ok && status.code == WORKSPACE_RESOURCE_UNAVAILABLE &&
        !strcmp(row.status, "unavailable") && !*row.key && !*row.path && !*row.reference
    );
    return true;
}
void test_tool_workspace_resources()
{
    pu_setup(NULL, inactive_cleanup);
    RUN_TEST_CASE(workspace_resource_bindings_report_unavailable_without_project);
    pu_setup(setup, teardown);
    RUN_TEST_CASE(workspace_rows_filter_page_and_stable_revision);
    RUN_TEST_CASE(workspace_snapshot_outlives_refresh_and_host);
    RUN_TEST_CASE(workspace_rejects_symlink_and_configured_traversal);
    RUN_TEST_CASE(workspace_configured_nested_content_and_missing_lookup);
    RUN_TEST_CASE(workspace_enforces_query_and_source_limits);
    RUN_TEST_CASE(workspace_native_query_preserves_typed_result_backing);
    RUN_TEST_CASE(workspace_copies_catalog_and_semantic_reference_annotations);
    pu_clear_setup();
}
