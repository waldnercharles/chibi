#include "authoring/document_source_inventory.h"

#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static workspace_documents *docs;
static document_semantics *semantic;
static arena storage;
static script_catalog catalog;
static int handle;
static char error[256];
static void cleanup(void)
{
    document_semantics_free(semantic);
    workspace_documents_free(docs);
    arena_free(&storage);
    semantic = NULL;
    docs = NULL;
    catalog = (script_catalog){ 0 };
}
static bool open_source(const char *text)
{
    docs = workspace_documents_new(NULL, NULL);
    REQUIRE(docs);
    REQUIRE(workspace_document_memory(docs, text, &handle) == WORKSPACE_DOCUMENT_OK);
    semantic = document_semantics_new(docs, handle, 1, &catalog, error, sizeof error);
    if (semantic && !document_semantics_valid(semantic)) {
        for (int i = 0; i < document_semantics_diagnostic_count(semantic); i++) {
            const script_diagnostic *d = document_semantics_diagnostic(semantic, i);
            fprintf(
                stderr,
                "Inventory fixture diagnostic at %d:%d: %s\n",
                d->range.line,
                d->range.column,
                d->message
            );
        }
    }
    REQUIRE(semantic && document_semantics_valid(semantic));
    return true;
}
TEST_CASE(source_members_keep_exact_callable_signature_and_scope)
{
    const char *source = "library Inventory { Base :: 7; Twice :: pure "
                         "fn(value:int)->(result:int) { local := value * 2; "
                         "return local; } Other :: pure fn()->(result:int) { "
                         "hidden := 9; return hidden; } }";
    REQUIRE(open_source(source));
    document_source_member *rows;
    int count, total;
    REQUIRE(
        document_source_members(semantic, 0, 0, 256, "", &storage, &rows, &count, &total, error, sizeof error)
    );
    REQUIRE(count == total && count >= 3);
    uint64_t graph = 0;
    bool constant = false;
    for (int i = 0; i < count; i++) {
        REQUIRE(strcmp(rows[i].name, "local") && strcmp(rows[i].name, "hidden"));
        if (!strcmp(rows[i].name, "Base")) {
            constant = !strcmp(rows[i].kind, "constant") && !strcmp(rows[i].text, "7");
        }
        if (!strcmp(rows[i].name, "Twice")) {
            REQUIRE(!strcmp(rows[i].kind, "function") && rows[i].pure);
            REQUIRE(!strcmp(rows[i].signature, "(value:int)->(result:int)"));
            REQUIRE(strcmp(rows[i].origin, "0") && strcmp(rows[i].body, "0"));
            graph = strtoull(rows[i].graph, NULL, 10);
            REQUIRE(graph);
        }
    }
    REQUIRE(constant && graph);
    REQUIRE(
        document_source_members(semantic, graph, 0, 256, "", &storage, &rows, &count, &total, error, sizeof error)
    );
    bool local = false, parameter = false, result = false;
    for (int i = 0; i < count; i++) {
        REQUIRE(strcmp(rows[i].name, "hidden"));
        local |= !strcmp(rows[i].name, "local") && !strcmp(rows[i].kind, "local");
        parameter |= !strcmp(rows[i].name, "value") &&
                     !strcmp(rows[i].kind, "parameter");
        result |= !strcmp(rows[i].name, "result") && !strcmp(rows[i].kind, "result");
    }
    REQUIRE(local && parameter && result);
    REQUIRE(
        document_source_members(semantic, graph, 0, 1, "local", &storage, &rows, &count, &total, error, sizeof error)
    );
    REQUIRE(count == 1 && total == 1 && !strcmp(rows[0].name, "local"));
    REQUIRE(
        document_source_members(semantic, graph, 1, 1, "local", &storage, &rows, &count, &total, error, sizeof error)
    );
    REQUIRE(count == 0 && total == 1);
    REQUIRE(
        !document_source_members(semantic, UINT64_MAX, 0, 1, "", &storage, &rows, &count, &total, error, sizeof error)
    );
    workspace_document_view view;
    REQUIRE(workspace_document_read(docs, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == 1 && !view.can_undo && !strcmp(view.text, source));
    return true;
}
TEST_CASE(source_bindings_keep_real_field_contracts_and_lexical_visibility)
{
    const script_catalog_field fields[] = {
        { .id = 101,
          .name = "level",
          .type = { .storage = PIN_I32 },
          .writable = true,
          .unit = "charge",
          .range_contract = true,
          .min = 0,
          .max = 100 },
        { .id = 102, .name = "label", .type = { .storage = PIN_STR } }
    };
    const script_catalog_type record = { .id = 100,
                                         .name = "Charge",
                                         .type = { .storage = PIN_STRUCT, .nominal = 100 },
                                         .fields = fields,
                                         .field_count = 2 };
    catalog = (script_catalog){ .types = &record, .type_count = 1 };
    const char
        *source = "library Inventory { Twice :: pure "
                  "fn(value:int)->(result:int) { return value * 2; } Use :: "
                  "pure fn(charge:Charge)->(result:int) { before := 3; out := "
                  "Twice(before); later := 4; return out; } }";
    REQUIRE(open_source(source));
    const authoring_source_presentation *presentation = document_semantics_presentation(semantic);
    uint64_t origin = 0;
    int start = (int)(strstr(source, "Twice(before)") - source) + 6;
    for (int i = 0; i < authoring_source_presentation_origin_count(presentation); i++) {
        const source_presentation_origin
            *o = authoring_source_presentation_origin_at(presentation, i);
        if (o->kind == SOURCE_PRESENTATION_EXPRESSION && !o->flow &&
            o->range.start == start && o->range.end == start + 6) {
            origin = o->identity;
        }
    }
    REQUIRE(origin);
    document_source_binding *rows;
    int count, total;
    REQUIRE(
        document_source_bindings(semantic, origin, 0, 256, "", &storage, &rows, &count, &total, error, sizeof error)
    );
    bool level = false, label = false, before = false;
    for (int i = 0; i < count; i++) {
        REQUIRE(strcmp(rows[i].label, "later"));
        if (!strcmp(rows[i].label, "before")) {
            before = rows[i].compatible && rows[i].binding == AUTHORING_BIND_SYMBOL;
        }
        if (!strcmp(rows[i].label, "charge.level")) {
            REQUIRE(rows[i].compatible && rows[i].binding == AUTHORING_BIND_SYMBOL);
            REQUIRE(!strcmp(rows[i].member, "101") && !strcmp(rows[i].component, "0"));
            REQUIRE(
                !strcmp(rows[i].unit, "charge") && rows[i].range &&
                rows[i].min == 0 && rows[i].max == 100
            );
            level = true;
        }
        if (!strcmp(rows[i].label, "charge.label")) {
            label = !rows[i].compatible && !rows[i].writable;
        }
    }
    REQUIRE(level && label && before);
    REQUIRE(
        document_source_bindings(semantic, origin, 0, 1, "charge.level", &storage, &rows, &count, &total, error, sizeof error)
    );
    REQUIRE(count == 1 && total == 1);
    REQUIRE(
        !document_source_bindings(semantic, 0, 0, 1, "", &storage, &rows, &count, &total, error, sizeof error)
    );
    workspace_document_view view;
    REQUIRE(workspace_document_read(docs, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == 1 && !view.can_undo && !strcmp(view.text, source));
    return true;
}
void test_document_source_inventory(void)
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_members_keep_exact_callable_signature_and_scope);
    RUN_TEST_CASE(source_bindings_keep_real_field_contracts_and_lexical_visibility);
    pu_setup(NULL, NULL);
}
