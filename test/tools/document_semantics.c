#include "authoring/document_semantics.h"

#include "authoring/document_graph_definition.h"
#include "authoring/document_layout_definition.h"
#include "authoring/document_observation_definition.h"
#include "authoring/document_port_schema_definition.h"
#include "authoring/document_semantics_definition.h"
#include "authoring/project_semantics_definition.h"
#include "execution/function.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static workspace_documents *documents;
static document_semantics *snapshot;
static script_catalog catalog;
static char error[256];
static int handle;
static char dialect_root[128];
static bool reflection, had_previous;
static type_catalog_pack previous;

static void cleanup()
{
    if (reflection) {
        type_catalog_set_project(NULL);
        if (had_previous) {
            type_catalog_set_project(&previous);
        }
        reflection = false;
    }
    document_semantics_free(snapshot);
    workspace_documents_free(documents);
    snapshot = NULL;
    documents = NULL;
    catalog = (script_catalog){ 0 };
    if (dialect_root[0]) {
        const char *names[] = { "page.rml", "theme.rcss", "data.kdl", "notes.txt" };
        char path[256];
        for (int i = 0; i < 4; i++) {
            snprintf(path, sizeof path, "%s/%s", dialect_root, names[i]);
            unlink(path);
        }
        rmdir(dialect_root);
        dialect_root[0] = 0;
    }
}
static bool open_source(const char *source)
{
    workspace_document_status status;
    documents = workspace_documents_new(NULL, &status);
    REQUIRE(status == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_memory(documents, source, &handle) == WORKSPACE_DOCUMENT_OK);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    snapshot = document_semantics_new(documents, handle, view.revision, &catalog, error, sizeof error);
    REQUIRE(snapshot);
    return true;
}
TEST_CASE(semantic_snapshots_keep_navigation_after_document_edit)
{
    REQUIRE(
        open_source("library Sample { Twice :: pure fn(value:int)->(result:int) { return value * 2; } }")
    );
    REQUIRE(document_semantics_valid(snapshot));
    const authoring_navigation *navigation = document_semantics_navigation(snapshot);
    const char *source = authoring_navigation_document_text(navigation, 0);
    int offset = (int)(strstr(source, "value *") - source);
    const authoring_reference *reference = authoring_navigation_at(navigation, 0, offset);
    REQUIRE(reference);
    const authoring_reference
        *definition = authoring_navigation_definition(navigation, reference->target);
    REQUIRE(definition);
    REQUIRE(definition->location.range.start < offset);
    REQUIRE(
        document_semantics_replace(documents, snapshot, offset, offset + 5, "value + 1", true, error, sizeof error)
    );
    REQUIRE(strstr(authoring_navigation_document_text(navigation, 0), "value * 2"));
    REQUIRE(
        !document_semantics_replace(documents, snapshot, offset, offset + 5, "value", true, error, sizeof error)
    );
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(strstr(view.text, "value + 1 * 2"));
    REQUIRE(view.can_undo);
    REQUIRE(workspace_document_undo(documents, handle, view.revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(strstr(view.text, "value * 2"));
    return true;
}
TEST_CASE(semantic_validation_rejects_invalid_replacement_without_history)
{
    REQUIRE(
        open_source("library Sample { Identity :: pure fn(value:int)->(result:int) { return value; } }")
    );
    int offset = (int)(strstr(authoring_navigation_document_text(document_semantics_navigation(snapshot), 0), "return value") -
                       authoring_navigation_document_text(document_semantics_navigation(snapshot), 0)) +
                 7;
    REQUIRE(
        !document_semantics_replace(documents, snapshot, offset, offset + 5, "unknown", true, error, sizeof error)
    );
    REQUIRE(*error);
    REQUIRE(
        !document_semantics_replace(documents, snapshot, -1, offset, "unknown", true, error, sizeof error)
    );
    REQUIRE(
        !document_semantics_replace(documents, snapshot, offset, 1000000, "unknown", true, error, sizeof error)
    );
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == document_semantics_revision(snapshot));
    REQUIRE(!view.can_undo);
    REQUIRE(
        document_semantics_replace(documents, snapshot, offset, offset + 5, "unknown", false, error, sizeof error)
    );
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    document_semantics *invalid = document_semantics_new(
        documents,
        handle,
        view.revision,
        &catalog,
        error,
        sizeof error
    );
    REQUIRE(invalid);
    REQUIRE(!document_semantics_valid(invalid));
    REQUIRE(document_semantics_diagnostic_count(invalid) > 0);
    int count = 0;
    authoring_highlight *highlights = authoring_source_highlights(
        document_semantics_navigation(invalid),
        0,
        &count
    );
    REQUIRE(count > 0);
    free(highlights);
    document_semantics_free(invalid);
    return true;
}
TEST_CASE(semantic_graph_edit_is_one_transaction_and_revision_guarded)
{
    REQUIRE(
        open_source("library Sample { Twice :: pure fn(value:int)->(result:int) { return value * 2; } }")
    );
    const authoring_source_presentation *p = document_semantics_presentation(snapshot);
    REQUIRE(p);
    const source_presentation_origin *selected = NULL;
    for (int i = 0; i < authoring_source_presentation_origin_count(p); i++) {
        const source_presentation_origin *o = authoring_source_presentation_origin_at(p, i);
        if (o->kind == SOURCE_PRESENTATION_EXPRESSION && o->op == SCRIPT_OP_MUL) {
            selected = o;
            break;
        }
    }
    REQUIRE(selected);
    authoring_edit_request request = { .kind = AUTHORING_EDIT_EXPRESSION,
                                       .generation = authoring_source_edit_generation(p),
                                       .origin = selected->identity,
                                       .expression = "value * 3" };
    REQUIRE(
        workspace_document_begin(documents, handle, document_semantics_revision(snapshot)) ==
        WORKSPACE_DOCUMENT_OK
    );
    /* Beginning a gesture may change a service revision. Reacquire the exact
     * generation before performing a semantic operation inside it. */
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    document_semantics_free(snapshot);
    snapshot = document_semantics_new(documents, handle, view.revision, &catalog, error, sizeof error);
    REQUIRE(snapshot);
    request.generation = authoring_source_edit_generation(
        document_semantics_presentation(snapshot)
    );
    REQUIRE(document_semantics_edit(documents, snapshot, &request, error, sizeof error));
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(strstr(view.text, "value * 3"));
    REQUIRE(!document_semantics_edit(documents, snapshot, &request, error, sizeof error));
    REQUIRE(workspace_document_cancel(documents, handle, view.revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(strstr(view.text, "value * 2"));
    REQUIRE(!view.can_undo);
    return true;
}

TEST_CASE(document_find_keeps_utf8_byte_offsets_and_local_rename_validates)
{
    REQUIRE(
        open_source("library Sample { Twice :: pure fn(value:int)->(result:int) { local := value * 2; return local; } }")
    );
    int start, end;
    REQUIRE(document_semantics_find(
        documents,
        handle,
        document_semantics_revision(snapshot),
        "LOCAL",
        0,
        true,
        &start,
        &end,
        error,
        sizeof error
    ));
    REQUIRE(start >= 0 && end == start + 5);
    REQUIRE(document_semantics_rename(documents, snapshot, start, "renamed", error, sizeof error));
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(strstr(view.text, "renamed :=") || strstr(view.text, "renamed:="));
    REQUIRE(strstr(view.text, "return renamed"));
    REQUIRE(!document_semantics_find(
        documents,
        handle,
        document_semantics_revision(snapshot),
        "local",
        0,
        false,
        &start,
        &end,
        error,
        sizeof error
    ));
    REQUIRE(workspace_document_undo(documents, handle, view.revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(strstr(view.text, "return local"));
    int other;
    REQUIRE(workspace_document_memory(documents, "a☃café☃", &other) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, other, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        document_semantics_find(documents, other, view.revision, "☃", 2, false, &start, &end, error, sizeof error) ==
        false
    );
    REQUIRE(
        document_semantics_find(documents, other, view.revision, "☃", 4, false, &start, &end, error, sizeof error)
    );
    REQUIRE(start == 9 && end == 12);
    return true;
}

TEST_CASE(document_semantics_uses_each_source_language_without_false_chibi_errors)
{
    snprintf(dialect_root, sizeof dialect_root, "/tmp/chibi-document-dialects-XXXXXX");
    REQUIRE(mkdtemp(dialect_root));
    documents = workspace_documents_new(dialect_root, NULL);
    const char *names[] = { "page.rml", "theme.rcss", "data.kdl", "notes.txt" };
    const char *texts[] = { "<div class=\"example\">雪☃</div>",
                            ".example { color: #fff; }",
                            "message \"雪☃\"",
                            "raw notes ☃" };
    for (int i = 0; i < 4; i++) {
        REQUIRE(
            workspace_document_create(documents, names[i], texts[i], &handle) == WORKSPACE_DOCUMENT_OK
        );
        workspace_document_view view;
        REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
        snapshot = document_semantics_new(documents, handle, view.revision, &catalog, error, sizeof error);
        REQUIRE(snapshot && document_semantics_valid(snapshot));
        REQUIRE(document_semantics_diagnostic_count(snapshot) == 0);
        REQUIRE(
            !document_semantics_navigation(snapshot) &&
            !document_semantics_presentation(snapshot)
        );
        int count = 0;
        authoring_highlight *highlights = document_semantics_highlights(snapshot, &count);
        REQUIRE(i == 3 ? count == 0 : count > 0);
        free(highlights);
        if (i == 0) {
            authoring_completions completion = { 0 };
            document_semantics_complete(snapshot, 1, NULL, &completion);
            REQUIRE(completion.count > 0);
            authoring_completions_free(&completion);
        }
        if (i == 1) {
            REQUIRE(!document_semantics_replace(
                documents,
                snapshot,
                0,
                (int)strlen(texts[i]),
                ".example { color:",
                true,
                error,
                sizeof error
            ));
            REQUIRE(
                workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK &&
                view.revision == document_semantics_revision(snapshot)
            );
        }
        document_semantics_free(snapshot);
        snapshot = NULL;
    }
    return true;
}
TEST_CASE(component_source_uses_its_dialect_and_validates_without_executable_analysis)
{
    const script_catalog_type component_types[] = {
        { .id = script_catalog_id("type", "Charge"), .name = "Charge", .component = true },
        { .id = script_catalog_id("type", "Other"), .name = "Other", .component = true }
    };
    catalog = (script_catalog){ .types = component_types, .type_count = 2 };
    const char *source = "// Recharge data.\ncomponent Charge {\n level: "
                         "float\n limit: 0..10 charge\n rate: charge/s\n}\n";
    REQUIRE(open_source(source));
    REQUIRE(document_semantics_valid(snapshot));
    REQUIRE(!document_semantics_graph(snapshot) && !document_semantics_navigation(snapshot));
    int count = 0;
    authoring_highlight *tokens = document_semantics_highlights(snapshot, &count);
    REQUIRE(count > 0);
    free(tokens);
    int unit_at = (int)(strstr(source, "rate: charge") - source) + 6;
    REQUIRE(
        !document_semantics_replace(documents, snapshot, unit_at, unit_at + 6, "Other", true, error, sizeof error)
    );
    REQUIRE(strstr(error, "type, not a unit"));
    int at = (int)(strstr(source, "float") - source);
    REQUIRE(
        !document_semantics_replace(documents, snapshot, at, at + 5, "NoSuchType", true, error, sizeof error)
    );
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == 1 && !view.can_undo && !strcmp(view.text, source));
    REQUIRE(
        document_semantics_replace(documents, snapshot, at, at + 5, "int", true, error, sizeof error)
    );
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == 2 && view.can_undo && strstr(view.text, "level: int"));
    return true;
}
TEST_CASE(source_port_rows_keep_exact_authored_expression_and_completion_range)
{
    const type_catalog_pack *old = type_catalog_project();
    had_previous = old != NULL;
    if (old) {
        previous = *old;
        type_catalog_set_project(NULL);
    }
    type_catalog_set_project(&DOCUMENT_SEMANTICS_TYPE_PACK);
    reflection = true;
    const char *source = "library Sample { Twice :: pure "
                         "fn(value:int)->(result:int) { return value * 2; } "
                         "Use :: pure fn(value:int)->(result:int) { return "
                         "Twice(value); } }";
    REQUIRE(open_source(source));
    document_semantics_service service = { .documents = documents, .catalog = &catalog };
    arena storage = { 0 };
    behavior_function_context context = { .value_storage = &storage };
    pin_value in[] = { { .i = handle }, { .i = 1 } }, out[8] = { 0 };
    REQUIRE(!DOCUMENT_SEMANTICS_CALLS[0].context(&context, &service, in, out));
    REQUIRE(out[0].b);
    const pin_array *ports = out[5].ptr;
    int authored = 0;
    for (int i = 0; i < ports->count; i++) {
        const document_port_row *port = ports->items[i].ptr;
        if (!port->editable) {
            continue;
        }
        REQUIRE(!port->output && !port->flow && port->start >= 0 && port->end >= port->start);
        REQUIRE(strlen(port->text) == (size_t)(port->end - port->start));
        REQUIRE(!strncmp(source + port->start, port->text, (size_t)(port->end - port->start)));
        authored += !strcmp(port->text, "value");
    }
    REQUIRE(authored > 0);
    arena_free(&storage);
    return true;
}
TEST_CASE(semantic_native_capabilities_refuse_absent_workspace_with_typed_results)
{
    arena storage = { 0 };
    behavior_function_context context = { .value_storage = &storage };
    pin_value in[12] = { { 0 } }, out[12] = { { 0 } };
    for (int i = 0; i < 8; i++) {
        REQUIRE(DOCUMENT_SEMANTICS_CALLS[i].optional_service);
        REQUIRE(!DOCUMENT_SEMANTICS_CALLS[i].context(&context, NULL, in, out));
        int error_index = i == 0 ? 6 : (i == 1 || i == 6) ? 3 : 1;
        REQUIRE(out[error_index].str && strstr(out[error_index].str, "unavailable"));
        if (i == 0) {
            REQUIRE(
                !out[0].b && ((pin_array *)out[1].ptr)->count == 0 &&
                ((pin_array *)out[5].ptr)->count == 0
            );
        }
        if (i == 6) {
            REQUIRE(!out[0].b && out[1].i == -1 && out[2].i == -1);
        }
        memset(out, 0, sizeof out);
    }
    for (int i = 0; i < 15; i++) {
        REQUIRE(DOCUMENT_GRAPH_CALLS[i].optional_service);
        REQUIRE(!DOCUMENT_GRAPH_CALLS[i].context(&context, NULL, in, out));
        REQUIRE(out[1].str && strstr(out[1].str, "unavailable"));
        if (i == 8 || i == 9) {
            REQUIRE(out[0].ptr && ((pin_array *)out[0].ptr)->count == 0);
        } else {
            REQUIRE(!out[0].b);
        }
        memset(out, 0, sizeof out);
    }
    for (int i = 0; i < 4; i++) {
        REQUIRE(PROJECT_SEMANTICS_CALLS[i].optional_service);
        REQUIRE(!PROJECT_SEMANTICS_CALLS[i].context(&context, NULL, in, out));
        REQUIRE(out[2].str && strstr(out[2].str, "unavailable"));
        if (i == 2) {
            REQUIRE(!out[0].b && ((pin_array *)out[1].ptr)->count == 0);
        } else {
            REQUIRE(((pin_array *)out[0].ptr)->count == 0 && out[1].i == 0);
        }
        memset(out, 0, sizeof out);
    }
    REQUIRE(DOCUMENT_LAYOUT_CALL.optional_service && DOCUMENT_OBSERVATION_CALL.optional_service);
    REQUIRE(!DOCUMENT_LAYOUT_CALL.context(&context, NULL, in, out));
    REQUIRE(
        !out[0].b && strstr(out[1].str, "unavailable") && !*out[2].str &&
        !*out[3].str
    );
    memset(out, 0, sizeof out);
    REQUIRE(!DOCUMENT_OBSERVATION_CALL.context(&context, NULL, in, out));
    REQUIRE(
        !out[0].b && strstr(out[1].str, "unavailable") && out[2].u64 == 0 &&
        !out[10].b
    );
    for (int i = 0; i < 2; i++) {
        memset(out, 0, sizeof out);
        REQUIRE(DOCUMENT_PORT_SCHEMA_CALLS[i].optional_service);
        REQUIRE(!DOCUMENT_PORT_SCHEMA_CALLS[i].context(&context, NULL, in, out));
        REQUIRE(strstr(out[i == 0 ? 3 : 1].str, "unavailable"));
        if (i == 0) {
            REQUIRE(
                ((pin_array *)out[0].ptr)->count == 0 &&
                ((pin_array *)out[1].ptr)->count == 0 && out[2].i == 0
            );
        } else {
            REQUIRE(!out[0].b && !*out[2].str);
        }
    }
    arena_free(&storage);
    return true;
}
void test_document_semantics()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(semantic_native_capabilities_refuse_absent_workspace_with_typed_results);
    RUN_TEST_CASE(source_port_rows_keep_exact_authored_expression_and_completion_range);
    RUN_TEST_CASE(component_source_uses_its_dialect_and_validates_without_executable_analysis);
    RUN_TEST_CASE(semantic_snapshots_keep_navigation_after_document_edit);
    RUN_TEST_CASE(semantic_validation_rejects_invalid_replacement_without_history);
    RUN_TEST_CASE(semantic_graph_edit_is_one_transaction_and_revision_guarded);
    RUN_TEST_CASE(document_find_keeps_utf8_byte_offsets_and_local_rename_validates);
    RUN_TEST_CASE(document_semantics_uses_each_source_language_without_false_chibi_errors);
    pu_setup(NULL, NULL);
}
