#include "authoring/ui_authoring.h"

#include "assets/ui_document.h"
#include "authoring/ui_authoring_definition.h"
#include "execution/function.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static workspace_documents *documents;
static ui_authoring *authoring;
static void setup()
{
    workspace_document_status status;
    documents = workspace_documents_new(NULL, &status);
    assert(documents && !status);
    authoring = ui_authoring_new(documents);
}
static void teardown()
{
    ui_authoring_free(authoring);
    workspace_documents_free(documents);
}
TEST_CASE(ui_source_pages_are_owned_and_reject_stale_indexes)
{
    int document;
    REQUIRE(
        !workspace_document_memory(documents, "<div id=\"root\"><button class=\"btn\">One</button></div>", &document)
    );
    workspace_document_view view;
    REQUIRE(!workspace_document_read(documents, document, &view));
    arena storage = { 0 };
    pin_array *rows;
    ui_authoring_status status = ui_authoring_query(
        authoring,
        document,
        view.revision,
        "rml",
        "element",
        -1,
        0,
        1,
        &storage,
        &rows
    );
    REQUIRE(status.ok && status.valid && status.total == 2 && rows->count == 1);
    ui_authoring_row *row = rows->items[0].ptr;
    REQUIRE(!strcmp(row->name, "div") && !strcmp(row->value, "root"));
    int before = view.revision;
    status = ui_authoring_edit(authoring, document, before, "attribute", 0, -1, "inside", "id", "changed", false);
    REQUIRE(status.ok && status.revision > before);
    REQUIRE(!strcmp(row->value, "root"));
    status = ui_authoring_edit(authoring, document, before, "delete", 0, -1, "inside", "", "", false);
    REQUIRE(!status.ok && status.code == WORKSPACE_DOCUMENT_STALE);
    REQUIRE(!workspace_document_read(documents, document, &view));
    REQUIRE(strstr(view.text, "changed"));
    status = ui_authoring_query(authoring, document, view.revision, "rml", "element", -1, 0, 257, &storage, &rows);
    REQUIRE(!status.ok && rows->count == 0);
    arena_free(&storage);
    return true;
}
TEST_CASE(ui_source_operations_share_atomic_document_history)
{
    int document;
    REQUIRE(!workspace_document_memory(documents, "<div><button>One</button></div>", &document));
    workspace_document_view view;
    REQUIRE(!workspace_document_read(documents, document, &view));
    REQUIRE(!workspace_document_begin(documents, document, view.revision));
    ui_authoring_status result = ui_authoring_edit(
        authoring,
        document,
        view.revision,
        "style",
        1,
        -1,
        "inside",
        "width",
        "40dp",
        false
    );
    REQUIRE(result.ok);
    result = ui_authoring_edit(authoring, document, result.revision, "text", 1, -1, "inside", "", "A < B & C", false);
    REQUIRE(result.ok);
    REQUIRE(!workspace_document_commit(documents, document, result.revision));
    REQUIRE(!workspace_document_read(documents, document, &view));
    REQUIRE(strstr(view.text, "&lt;") && strstr(view.text, "40dp"));
    int edited = view.revision;
    REQUIRE(!workspace_document_undo(documents, document, edited));
    REQUIRE(!workspace_document_read(documents, document, &view));
    REQUIRE(!strcmp(view.text, "<div><button>One</button></div>"));
    result = ui_authoring_edit(authoring, document, view.revision, "move", 0, 1, "inside", "", "", false);
    REQUIRE(!result.ok);
    REQUIRE(!workspace_document_read(documents, document, &view));
    REQUIRE(!strcmp(view.text, "<div><button>One</button></div>"));
    return true;
}
TEST_CASE(ui_style_query_and_structural_budget_are_bounded)
{
    int document;
    REQUIRE(
        !workspace_document_memory(documents, ".panel { color: red; width: 40dp; }", &document)
    );
    workspace_document_view view;
    REQUIRE(!workspace_document_read(documents, document, &view));
    arena storage = { 0 };
    pin_array *rows;
    ui_authoring_status status = ui_authoring_query(
        authoring,
        document,
        view.revision,
        "rcss",
        "rule",
        -1,
        0,
        64,
        &storage,
        &rows
    );
    REQUIRE(
        status.ok && rows->count == 1 &&
        !strcmp(((ui_authoring_row *)rows->items[0].ptr)->name, ".panel")
    );
    status = ui_authoring_query(
        authoring,
        document,
        view.revision,
        "rcss",
        "declaration",
        0,
        1,
        1,
        &storage,
        &rows
    );
    REQUIRE(
        status.ok && status.total == 2 && rows->count == 1 &&
        !strcmp(((ui_authoring_row *)rows->items[0].ptr)->value, "40dp")
    );
    status = ui_authoring_edit(authoring, document, view.revision, "declaration", 0, 1, "inside", "width", "80dp", false);
    REQUIRE(status.ok);
    REQUIRE(!workspace_document_read(documents, document, &view));
    REQUIRE(strstr(view.text, "80dp"));
    REQUIRE(!workspace_document_undo(documents, document, view.revision));
    REQUIRE(!workspace_document_read(documents, document, &view));
    REQUIRE(strstr(view.text, "40dp"));
    char *large = malloc(4097 * 12 + 1);
    REQUIRE(large);
    large[0] = 0;
    for (int i = 0; i < 4097; i++) {
        strcat(large, "<div></div>");
    }
    int bounded;
    REQUIRE(!workspace_document_memory(documents, large, &bounded));
    free(large);
    REQUIRE(!workspace_document_read(documents, bounded, &view));
    status = ui_authoring_query(authoring, bounded, view.revision, "rml", "element", -1, 0, 64, &storage, &rows);
    REQUIRE(!status.ok && status.code == WORKSPACE_DOCUMENT_LIMIT && rows->count == 0);
    arena_free(&storage);
    return true;
}
static char *memory_ui_read(void *context, const char *path)
{
    int *calls = context;
    (*calls)++;
    if (!strcmp(path, "memory/ui/Main.rml")) {
        return strdup("\n<body class='page'><div><use ui='Badge' title='instance'/></div></body>");
    }
    if (!strcmp(path, "memory/ui/Main.rcss")) {
        return strdup("@import 'colors.rcss'; div { width:40px; }");
    }
    if (!strcmp(path, "memory/ui/Badge.rml")) {
        return strdup("<span attrs id='from-overlay'>Unsaved widget</span>");
    }
    if (!strcmp(path, "memory/ui/colors.rcss")) {
        return strdup("span { color:red; }");
    }
    return NULL;
}
TEST_CASE(ui_source_reader_resolves_overlay_widgets_and_imports_without_disk)
{
    int calls = 0;
    text_reader reader = { .context = &calls, .read = memory_ui_read };
    ui_document document = { 0 };
    char error[256];
    REQUIRE(
        ui_document_load_reader("memory/ui", "Main", NULL, &document, error, sizeof error, &reader)
    );
    REQUIRE(document.rml_start == 20 && document.body_start == 1 && document.body_line == 2);
    char *exact = ui_document_sources_at(
        document.rml,
        "Main",
        document.rml_line,
        document.rml_start
    );
    REQUIRE(strstr(exact, "<div data-src=\"Main:2\" data-source-start=\"20\""));
    REQUIRE(strstr(exact, "<use data-src=\"Main:2\" data-source-start=\"25\""));
    free(exact);
    exact = ui_document_sources_at("<div/>", "Quoted\"&Name", 1, 0);
    REQUIRE(strstr(exact, "data-src=\"Quoted&quot;&amp;Name:1\" data-source-start=\"0\""));
    free(exact);
    char **uses = NULL;
    REQUIRE(
        ui_document_expand_reader(&document, "Main", "memory/ui", true, &uses, error, sizeof error, &reader)
    );
    REQUIRE(acount(uses) == 1 && !strcmp(uses[0], "Badge"));
    REQUIRE(strstr(document.rml, "Unsaved widget") && strstr(document.rml, "data-src="));
    REQUIRE(strstr(document.rml, "<span data-src=\"Badge:1\" data-source-start=\"0\""));
    REQUIRE(strstr(document.rml, "title='instance'"));
    REQUIRE(
        !strstr(document.rml, "<span data-src=\"Badge:1\" data-source-start=\"0\" data-source-start=")
    );
    REQUIRE(
        strstr(document.rcss, "color") && strstr(document.rcss, "red") &&
        calls >= 5 && calls < 16
    );
    /* A missing authoritative source refuses instead of reading another source. */
    REQUIRE(
        !ui_document_load_reader("memory/ui", "Missing", NULL, &document, error, sizeof error, &reader)
    );
    REQUIRE(strstr(document.rml, "Unsaved widget"));
    for (int i = 0; i < acount(uses); i++) {
        free(uses[i]);
    }
    afree(uses);
    ui_document_free(&document);
    return true;
}
static const char *snapshot_string(void *storage, const char *text)
{
    size_t size = strlen(text) + 1;
    char *result = arena_alloc(storage, size);
    memcpy(result, text, size);
    return result;
}
TEST_CASE(ui_source_missing_workspace_is_typed_and_empty)
{
    arena storage = { 0 };
    behavior_function_context context = { .value_storage = &storage,
                                          .copy_string = snapshot_string };
    ui_authoring_status result;
    pin_value out[2] = { { .type = PIN_STRUCT, .ptr = &result }, { 0 } };
    pin_value in[9] = { 0 };
    REQUIRE(UI_AUTHORING_QUERY_CALL.optional_service && UI_AUTHORING_EDIT_CALL.optional_service);
    REQUIRE(!UI_AUTHORING_QUERY_CALL.context(&context, NULL, in, out));
    REQUIRE(!result.ok && !result.valid && result.code == WORKSPACE_DOCUMENT_UNAVAILABLE);
    REQUIRE(
        !result.document && !result.revision && !result.total &&
        strstr(result.error, "active workspace")
    );
    REQUIRE(out[1].ptr && ((pin_array *)out[1].ptr)->count == 0);
    memset(&result, 0x7f, sizeof result);
    REQUIRE(!UI_AUTHORING_EDIT_CALL.context(&context, NULL, in, out));
    REQUIRE(
        !result.ok && result.code == WORKSPACE_DOCUMENT_UNAVAILABLE &&
        !result.document && !result.revision
    );
    arena_free(&storage);
    return true;
}
void test_tool_ui_authoring()
{
    pu_setup(setup, teardown);
    RUN_TEST_CASE(ui_source_pages_are_owned_and_reject_stale_indexes);
    RUN_TEST_CASE(ui_source_operations_share_atomic_document_history);
    RUN_TEST_CASE(ui_style_query_and_structural_budget_are_bounded);
    RUN_TEST_CASE(ui_source_reader_resolves_overlay_widgets_and_imports_without_disk);
    RUN_TEST_CASE(ui_source_missing_workspace_is_typed_and_empty);
    pu_clear_setup();
}
