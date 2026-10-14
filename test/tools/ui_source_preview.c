#include "authoring/ui_source_preview.h"

#include "authoring/resource_previews_definition.h"
#include "authoring/ui_source_preview_definition.h"
#include "execution/function.h"
#include "third_party/pico_headers/pico_unit.h"

#include <string.h>
static workspace_documents *documents;
static ui_source_preview *preview;
static int document;
static void setup()
{
    workspace_document_status status;
    documents = workspace_documents_new("test/fixtures/ui-source-preview", &status);
    workspace_document_open(documents, "content/ui/Main.rml", &document);
    ui_source_preview_config config = {
        .root = "test/fixtures/ui-source-preview",
        .content = "content",
        .documents = documents
    };
    char error[256];
    preview = ui_source_preview_new(&config, error, sizeof error);
}
static void teardown()
{
    ui_source_preview_free(preview);
    workspace_documents_free(documents);
}
TEST_CASE(ui_preview_metadata_refuses_stale_and_unused_source_without_graphics)
{
    REQUIRE(preview && document > 0);
    workspace_document_view view;
    REQUIRE(!workspace_document_read(documents, document, &view));
    ui_source_preview_result
        result = ui_source_preview_open(preview, document, view.revision, "Main", 0, 0);
    REQUIRE(
        result.ok && result.preview > 0 && !result.texture &&
        strstr(result.context, "source-preview/")
    );
    int handle = result.preview;
    result = ui_source_preview_open(preview, document, view.revision + 1, "Main", 0, 0);
    REQUIRE(!result.ok && result.preview == handle && !result.texture);
    int draft;
    REQUIRE(!workspace_document_memory(documents, "<span>Unrelated draft</span>", &draft));
    REQUIRE(!workspace_document_read(documents, draft, &view));
    result = ui_source_preview_open(preview, draft, view.revision, "Main", 0, 0);
    REQUIRE(!result.ok && !result.texture && strstr(result.error, "not used"));
    REQUIRE(ui_source_preview_close(preview, handle).ok);
    REQUIRE(!ui_source_preview_close(preview, handle).ok);
    return true;
}
TEST_CASE(ui_preview_invalid_draws_and_missing_target_never_create_renderer)
{
    workspace_document_view view;
    REQUIRE(!workspace_document_read(documents, document, &view));
    ui_source_preview_result
        opened = ui_source_preview_open(preview, document, view.revision, "Main", 0, 0);
    REQUIRE(opened.ok);
    REQUIRE(!ui_source_preview_draw(preview, opened.preview, view.revision, 2049, 1, 0)
                 .ok);
    REQUIRE(!ui_source_preview_draw(preview, opened.preview, view.revision, 10, 10, 1)
                 .ok);
    REQUIRE(!ui_source_preview_draw(preview, opened.preview + 1, view.revision, 10, 10, 0)
                 .ok);
    REQUIRE(
        !ui_source_preview_open(preview, document, view.revision, "Main", 0, 1).ok
    );
    opened = ui_source_preview_open(preview, document, view.revision, "Main", 7, 0);
    REQUIRE(opened.ok);
    ui_source_preview_result
        result = ui_source_preview_draw(preview, opened.preview, view.revision, 10, 10, 0);
    REQUIRE(!result.ok && !result.texture && strstr(result.error, "no target runtime"));
    ui_source_preview_retire_target(preview);
    REQUIRE(!ui_source_preview_current(preview).preview);
    return true;
}
static const char *snapshot_string(void *storage, const char *text)
{
    size_t size = strlen(text) + 1;
    char *result = arena_alloc(storage, size);
    memcpy(result, text, size);
    return result;
}
TEST_CASE(ui_preview_missing_workspace_bindings_never_create_handles)
{
    arena storage = { 0 };
    behavior_function_context context = { .value_storage = &storage,
                                          .copy_string = snapshot_string };
    pin_value in[5] = { 0 };
    for (int i = 0; i < 3; ++i) {
        ui_source_preview_result result;
        memset(&result, 0x7f, sizeof result);
        pin_value out = { .type = PIN_STRUCT, .ptr = &result };
        REQUIRE(UI_SOURCE_PREVIEW_CALLS[i].optional_service);
        REQUIRE(!UI_SOURCE_PREVIEW_CALLS[i].context(&context, NULL, in, &out));
        REQUIRE(
            !result.ok && !result.preview && !result.texture &&
            !result.document && !result.revision
        );
        REQUIRE(!strcmp(result.context, "") && strstr(result.error, "active workspace"));
    }
    arena_free(&storage);
    return true;
}
TEST_CASE(resource_preview_missing_workspace_returns_unavailable_without_handles)
{
    arena storage = { 0 };
    behavior_function_context context = { .value_storage = &storage,
                                          .copy_string = snapshot_string };
    pin_value in[3] = { 0 }, out[4] = { 0 };
    const native_definition_pack *pack = &RESOURCE_PREVIEWS_DEFINITION_PACK;
    REQUIRE(pack->binding_count == 3);
    for (int i = 0; i < 3; ++i) {
        REQUIRE(pack->bindings[i].implementation.call->optional_service);
    }
    REQUIRE(!pack->bindings[0].implementation.call->context(&context, NULL, in, out));
    REQUIRE(!out[0].i && strstr(out[1].str, "active workspace"));
    REQUIRE(!pack->bindings[1].implementation.call->context(&context, NULL, in, out));
    REQUIRE(!out[0].b && !strcmp(out[1].str, "unavailable") && !strcmp(out[2].str, ""));
    REQUIRE(strstr(out[3].str, "active workspace"));
    out[0].b = true;
    REQUIRE(!pack->bindings[2].implementation.call->context(&context, NULL, in, out));
    REQUIRE(!out[0].b);
    arena_free(&storage);
    return true;
}
void test_tool_ui_source_preview()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(ui_preview_metadata_refuses_stale_and_unused_source_without_graphics);
    RUN_TEST_CASE(ui_preview_invalid_draws_and_missing_target_never_create_renderer);
    RUN_TEST_CASE(ui_preview_missing_workspace_bindings_never_create_handles);
    RUN_TEST_CASE(resource_preview_missing_workspace_returns_unavailable_without_handles);
    pu_clear_setup();
}
