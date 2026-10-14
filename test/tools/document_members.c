#include "authoring/document_members_definition.h"
#include "execution/function.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static workspace_documents *documents;
static document_semantics *snapshot;
static arena storage;
static script_catalog catalog;
static int handle;
static char error[256];
static void cleanup(void)
{
    document_semantics_free(snapshot);
    workspace_documents_free(documents);
    arena_free(&storage);
    snapshot = NULL;
    documents = NULL;
    catalog = (script_catalog){ 0 };
}
static bool open_source(const char *source)
{
    documents = workspace_documents_new(NULL, NULL);
    REQUIRE(documents);
    REQUIRE(workspace_document_memory(documents, source, &handle) == WORKSPACE_DOCUMENT_OK);
    snapshot = document_semantics_new(documents, handle, 1, &catalog, error, sizeof error);
    REQUIRE(snapshot && document_semantics_valid(snapshot));
    return true;
}
static bool inspect(document_member **members, int *mc, document_member **pins, int *pc, uint64_t *graph)
{
    document_member *types;
    int tc;
    REQUIRE(
        document_members_inspect(snapshot, 0, &storage, members, mc, pins, pc, &types, &tc, error, sizeof error)
    );
    *graph = 0;
    for (int i = 0; i < *mc; i++) {
        if (!strcmp((*members)[i].name, "Calculate")) {
            *graph = strtoull((*members)[i].graph, NULL, 10);
        }
    }
    REQUIRE(*graph);
    REQUIRE(
        document_members_inspect(snapshot, *graph, &storage, members, mc, pins, pc, &types, &tc, error, sizeof error)
    );
    REQUIRE(tc >= 7);
    return true;
}
TEST_CASE(structured_members_preserve_exact_signature_and_rewrite_renamed_parameter_uses)
{
    const char
        *source = "library Members { Calculate :: pure fn(value:int, extra:int "
                  "= 3)->(result:int) { return value + extra; } }";
    REQUIRE(open_source(source));
    document_member *members, *pins;
    int mc, pc;
    uint64_t graph;
    REQUIRE(inspect(&members, &mc, &pins, &pc, &graph));
    REQUIRE(
        pc == 3 && !strcmp(pins[0].type, "int") &&
        !strcmp(pins[1].value, "3") && pins[1].has_default
    );
    char graph_id[32];
    snprintf(graph_id, sizeof graph_id, "%llu", (unsigned long long)graph);
    char *text = NULL, *metadata = NULL;
    document_member_edit request = { .operation = DOCUMENT_MEMBER_RENAME_PIN,
                                     .graph = graph_id,
                                     .index = 0,
                                     .name = "amount" };
    bool ok = document_member_prepare(snapshot, &request, &text, &metadata, error, sizeof error);
    if (!ok) {
        fprintf(stderr, "structured rename: %s\n", error);
    }
    REQUIRE(ok);
    REQUIRE(strstr(text, "amount:int") && strstr(text, "return amount + extra"));
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == 1 && !view.can_undo && !strcmp(view.text, source));
    document_semantics_free(snapshot);
    snapshot = NULL;
    workspace_documents_free(documents);
    documents = NULL;
    REQUIRE(strstr(text, "return amount + extra"));
    free(text);
    free(metadata);
    return true;
}
TEST_CASE(structured_members_reorder_preserves_defaults_and_rejects_referenced_pin_removal)
{
    REQUIRE(
        open_source("library Members { Calculate :: pure fn(value:int, extra:int = 3)->(result:int) { return value + extra; } }")
    );
    document_member *members, *pins;
    int mc, pc;
    uint64_t graph;
    REQUIRE(inspect(&members, &mc, &pins, &pc, &graph));
    char graph_id[32];
    snprintf(graph_id, sizeof graph_id, "%llu", (unsigned long long)graph);
    char *text = NULL, *metadata = NULL;
    document_member_edit request = { .operation = DOCUMENT_MEMBER_MOVE_PIN,
                                     .graph = graph_id,
                                     .index = 1,
                                     .target = 0 };
    bool ok = document_member_prepare(snapshot, &request, &text, &metadata, error, sizeof error);
    if (!ok) {
        fprintf(stderr, "structured reorder: %s\n", error);
    }
    REQUIRE(ok);
    REQUIRE(strstr(text, "(extra:int = 3, value:int)"));
    free(text);
    free(metadata);
    request.operation = DOCUMENT_MEMBER_REMOVE_PIN;
    request.index = 0;
    REQUIRE(!document_member_prepare(snapshot, &request, &text, &metadata, error, sizeof error));
    REQUIRE(!text && !metadata && *error);
    return true;
}
TEST_CASE(structured_members_validate_whole_source_and_refuse_foreign_graph)
{
    REQUIRE(
        open_source("library Members { Calculate :: pure fn(value:int)->(result:int) { return value; } }")
    );
    document_member *members, *pins;
    int mc, pc;
    uint64_t graph;
    REQUIRE(inspect(&members, &mc, &pins, &pc, &graph));
    char graph_id[32];
    snprintf(graph_id, sizeof graph_id, "%llu", (unsigned long long)graph);
    char *text = NULL, *metadata = NULL;
    document_member_edit request = { .operation = DOCUMENT_MEMBER_RETYPE_PIN,
                                     .graph = graph_id,
                                     .index = 0,
                                     .type = "string" };
    REQUIRE(!document_member_prepare(snapshot, &request, &text, &metadata, error, sizeof error));
    REQUIRE(!text && !metadata);
    request.graph = "18446744073709551616";
    REQUIRE(!document_member_prepare(snapshot, &request, &text, &metadata, error, sizeof error));
    REQUIRE(*error);
    return true;
}
static const char *native_copy(void *storage, const char *text)
{
    char *out = arena_alloc(storage, strlen(text) + 1);
    strcpy(out, text);
    return out;
}
TEST_CASE(structured_member_bindings_refuse_absent_service_before_decoding_inputs)
{
    behavior_function_context context = { .value_storage = &storage,
                                          .copy_string = native_copy };
    pin_value outputs[4] = { 0 };
    REQUIRE(
        DOCUMENT_MEMBERS_CALLS[0].optional_service && DOCUMENT_MEMBERS_CALLS[1].optional_service
    );
    REQUIRE(!DOCUMENT_MEMBERS_CALLS[0].context(&context, NULL, NULL, outputs));
    for (int i = 0; i < 3; i++) {
        const pin_array *rows = outputs[i].ptr;
        REQUIRE(
            rows && rows->count == 0 && rows->element_type == PIN_STRUCT &&
            !strcmp(rows->type_name, "DocumentMember")
        );
    }
    REQUIRE(strstr(outputs[3].str, "unavailable"));
    memset(outputs, 0, sizeof outputs);
    REQUIRE(!DOCUMENT_MEMBERS_CALLS[1].context(&context, NULL, NULL, outputs));
    REQUIRE(
        !outputs[0].b && strstr(outputs[1].str, "unavailable") &&
        !outputs[2].str[0] && !outputs[3].str[0]
    );
    return true;
}
TEST_CASE(structured_parameter_rename_preserves_resolved_named_calls_and_unrelated_labels)
{
    const char *source = "library Members { Calculate :: pure "
                         "fn(value:int)->(result:int) { return value; } Other "
                         ":: pure fn(value:int)->(result:int) { return value; "
                         "} Use :: pure fn()->(result:int) { return "
                         "Calculate(value: 7) + Other(value: 9); } }";
    REQUIRE(open_source(source));
    document_member *members, *pins;
    int mc, pc;
    uint64_t graph;
    REQUIRE(inspect(&members, &mc, &pins, &pc, &graph));
    char graph_id[32];
    snprintf(graph_id, sizeof graph_id, "%llu", (unsigned long long)graph);
    document_member_edit request = { .operation = DOCUMENT_MEMBER_RENAME_PIN,
                                     .graph = graph_id,
                                     .index = 0,
                                     .name = "amount" };
    char *text = NULL, *metadata = NULL;
    bool ok = document_member_prepare(snapshot, &request, &text, &metadata, error, sizeof error);
    if (!ok) {
        fprintf(stderr, "resolved named-call rename: %s\n", error);
    }
    REQUIRE(ok);
    REQUIRE(
        strstr(text, "Calculate(amount: 7)") &&
        strstr(text, "Other(value: 9)") && strstr(text, "return amount;")
    );
    workspace_document_view view;
    REQUIRE(!workspace_document_read(documents, handle, &view));
    REQUIRE(view.revision == 1 && !view.can_undo && !strcmp(view.text, source));
    free(text);
    free(metadata);
    return true;
}
void test_document_members(void)
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(structured_members_preserve_exact_signature_and_rewrite_renamed_parameter_uses);
    RUN_TEST_CASE(structured_members_reorder_preserves_defaults_and_rejects_referenced_pin_removal);
    RUN_TEST_CASE(structured_members_validate_whole_source_and_refuse_foreign_graph);
    RUN_TEST_CASE(structured_member_bindings_refuse_absent_service_before_decoding_inputs);
    RUN_TEST_CASE(structured_parameter_rename_preserves_resolved_named_calls_and_unrelated_labels);
    pu_setup(NULL, NULL);
}
