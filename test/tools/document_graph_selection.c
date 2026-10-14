#define _POSIX_C_SOURCE 200809L
#include "authoring/document_graph_selection_definition.h"
#include "execution/function.h"
#include "graph/authoring/graph_edit.h"
#include "graph/document/serialize.h"
#include "graph/modeling/model_compile.h"
#include "graph/nodes/core_nodes.h"
#include "graph/shader/shader_graph.h"
#include "host/native_catalog.h"
#include "support/array.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const registry_pack **previous;
static type_catalog_pack old_types;
static bool had_types;
static workspace_documents *documents;
static document_semantics *snapshot;
static behavior_asset *parsed;
static document_graph_selection_result result;
static char error[512];
static int handle;
static uint64_t selected[3], graph_id;
static script_catalog catalog;
static arena native_storage;
static char root[1024];
static void setup()
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous, registry_pack_at(i));
    }
    const type_catalog_pack *old = type_catalog_project();
    had_types = old != NULL;
    if (old) {
        old_types = *old;
    }
    type_catalog_pack types = { .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
    type_catalog_set_project(&types);
    const registry_pack *packs[] = { &MODEL_PACK, &SHADER_PACK, &CORE_FLOW_PACK };
    registry_set_packs(packs, 3);
    snprintf(root, sizeof root, "%s/graph-selection-XXXXXX", tool_root());
    if (!mkdtemp(root)) {
        abort();
    }
    documents = workspace_documents_new(root, NULL);
}
static void cleanup()
{
    behavior_asset_free(parsed);
    parsed = NULL;
    document_graph_selection_result_free(&result);
    arena_free(&native_storage);
    document_semantics_free(snapshot);
    snapshot = NULL;
    workspace_documents_free(documents);
    tool_remove_tree(root);
    type_catalog_set_project(had_types ? &old_types : NULL);
    registry_set_packs(previous, acount(previous));
    afree(previous);
}
static bool inspect(const char *text, const char *path)
{
    workspace_document_status
        status = path ? workspace_document_draft(documents, path, text, &handle)
                      : workspace_document_memory(documents, text, &handle);
    if (status) {
        return false;
    }
    snapshot = document_semantics_new(documents, handle, 1, &catalog, error, sizeof error);
    if (!snapshot || !document_semantics_graph(snapshot)) {
        fprintf(stderr, "Graph selection fixture: %s\n", error);
        if (snapshot && document_semantics_diagnostic_count(snapshot)) {
            fprintf(stderr, "%s\n", document_semantics_diagnostic(snapshot, 0)->message);
        }
        return false;
    }
    return true;
}
static bool model()
{
    behavior_asset *asset = behavior_asset_new("SelectionModel");
    asset->domain = BEHAVIOR_DOMAIN_MODEL;
    graph *g = asset->graph;
    node *a = graph_add_node(g, &NODE_ModelSphere),
         *b = graph_add_node(g, &NODE_ModelSphere),
         *pair = graph_add_node(g, &NODE_ModelUnion),
         *output = model_graph_add_output(g);
    selected[0] = a->id;
    selected[1] = b->id;
    selected[2] = pair->id;
    graph_id = g->graph_id;
    a->x = 10;
    b->x = 20;
    a->placed = b->placed = true;
    bool valid = !graph_connect_data(g, a, 0, pair, 0) &&
                 !graph_connect_data(g, b, 0, pair, 1) &&
                 !graph_connect_data(g, pair, 0, output, 0);
    char *text = valid ? behavior_asset_save(asset) : NULL;
    valid = text && inspect(text, "SelectionModel.kdl");
    free(text);
    behavior_asset_free(asset);
    return valid;
}
TEST_CASE(graph_selection_clone_and_delete_are_pure_candidates_with_shared_history)
{
    REQUIRE(model());
    workspace_document_view before;
    REQUIRE(!workspace_document_read(documents, handle, &before));
    char *original = strdup(before.text);
    document_graph_selection_request request = { .operation = DOCUMENT_SELECTION_CLONE,
                                                 .graph = graph_id,
                                                 .nodes = selected,
                                                 .count = 3,
                                                 .dx = 32,
                                                 .dy = 48 };
    REQUIRE(document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    REQUIRE(acount(result.created) == 3 && !result.metadata);
    parsed = behavior_asset_load(result.text);
    REQUIRE(parsed && acount(parsed->graph->nodes) == 7 && acount(parsed->graph->edges) == 5);
    for (int i = 0; i < 3; i++) {
        REQUIRE(result.created[i] != selected[i]);
    }
    workspace_document_view current;
    REQUIRE(!workspace_document_read(documents, handle, &current));
    REQUIRE(current.revision == 1 && !current.can_undo && !strcmp(current.text, original));
    REQUIRE(!workspace_document_edit(
        documents,
        handle,
        1,
        0,
        strlen(current.text),
        result.text
    ));
    REQUIRE(!workspace_document_read(documents, handle, &current));
    REQUIRE(!workspace_document_undo(documents, handle, current.revision));
    REQUIRE(
        !workspace_document_read(documents, handle, &current) &&
        !strcmp(current.text, original)
    );
    free(original);
    document_graph_selection_result_free(&result);
    request.operation = DOCUMENT_SELECTION_DELETE;
    REQUIRE(document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    behavior_asset_free(parsed);
    parsed = behavior_asset_load(result.text);
    REQUIRE(parsed && acount(parsed->graph->nodes) == 1 && !acount(parsed->graph->edges));
    return true;
}
TEST_CASE(graph_selection_preflights_all_positions_and_retains_comments_boxes_and_reroutes)
{
    REQUIRE(model());
    float x[] = { 140, NAN }, y[] = { 200, 250 };
    document_graph_selection_request request = { .operation = DOCUMENT_SELECTION_PLACE,
                                                 .graph = graph_id,
                                                 .nodes = selected,
                                                 .count = 2,
                                                 .x = x,
                                                 .y = y,
                                                 .points = 2 };
    REQUIRE(!document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    REQUIRE(!result.text && !result.metadata);
    REQUIRE(document_semantics_graph(snapshot)->graph->nodes[0]->x == 10);
    x[1] = 240;
    REQUIRE(document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    parsed = behavior_asset_load(result.text);
    REQUIRE(
        parsed && graph_node_by_id(parsed->graph, selected[0])->x == 140 &&
        graph_node_by_id(parsed->graph, selected[1])->x == 240
    );
    document_graph_selection_result_free(&result);
    request = (document_graph_selection_request){ .operation = DOCUMENT_SELECTION_COMMENT,
                                                  .graph = graph_id,
                                                  .node = selected[0],
                                                  .text = "Retained comment",
                                                  .hidden = true };
    REQUIRE(document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    behavior_asset_free(parsed);
    parsed = behavior_asset_load(result.text);
    REQUIRE(
        parsed && !strcmp(graph_node_by_id(parsed->graph, selected[0])->comment, "Retained comment")
    );
    REQUIRE(graph_node_by_id(parsed->graph, selected[0])->comment_hidden);
    document_graph_selection_result_free(&result);
    request = (document_graph_selection_request){ .operation = DOCUMENT_SELECTION_BOX,
                                                  .graph = graph_id,
                                                  .text = "Group",
                                                  .dx = 12,
                                                  .dy = 34,
                                                  .width = 320,
                                                  .height = 180,
                                                  .color = 0x334455ff };
    REQUIRE(document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    behavior_asset_free(parsed);
    parsed = behavior_asset_load(result.text);
    REQUIRE(
        parsed && result.box && acount(parsed->graph->comment_boxes) == 1 &&
        parsed->graph->comment_boxes[0].color == 0x334455ff
    );
    document_graph_selection_result_free(&result);
    const graph *g = document_semantics_graph(snapshot)->graph;
    edge e = g->edges[0];
    request = (document_graph_selection_request){ .operation = DOCUMENT_SELECTION_REROUTE,
                                                  .graph = graph_id,
                                                  .from_node = e.from_id,
                                                  .from_pin = e.from_pin_id,
                                                  .to_node = e.to_id,
                                                  .to_pin = e.to_pin_id,
                                                  .x = x,
                                                  .y = y,
                                                  .points = 2 };
    REQUIRE(document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    behavior_asset_free(parsed);
    parsed = behavior_asset_load(result.text);
    REQUIRE(
        parsed && parsed->graph->edges[0].reroute_count == 2 &&
        parsed->graph->edges[0].reroutes[1].x == 240
    );
    return true;
}
TEST_CASE(source_selection_metadata_and_clipboard_never_change_execution_source)
{
    const char
        *text = "library Source { Identity :: pure fn(value:int)->(result:int) "
                "{ named := value; return named; } }";
    REQUIRE(inspect(text, NULL));
    const behavior_asset *asset = document_semantics_graph(snapshot);
    const graph *g = NULL;
    for (int i = 0; i < acount(asset->graphs); i++) {
        if (asset->graphs[i]->kind == GRAPH_FUNCTION &&
            acount(asset->graphs[i]->nodes)) {
            g = asset->graphs[i];
            break;
        }
    }
    REQUIRE(g);
    uint64_t id = g->nodes[0]->id;
    float x = 314, y = 159;
    document_graph_selection_request request = { .operation = DOCUMENT_SELECTION_PLACE,
                                                 .graph = g->graph_id,
                                                 .nodes = &id,
                                                 .count = 1,
                                                 .x = &x,
                                                 .y = &y,
                                                 .points = 1 };
    REQUIRE(document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    REQUIRE(result.metadata && !strcmp(result.text, text) && g->nodes[0]->x != 314);
    authoring_source_presentation *copy = authoring_source_presentation_clone(
        document_semantics_presentation(snapshot)
    );
    REQUIRE(authoring_source_presentation_restore(copy, result.metadata));
    REQUIRE(
        graph_node_by_id(behavior_asset_find_graph(authoring_source_presentation_view(copy), g->graph_id), id)
            ->x == 314
    );
    authoring_source_presentation_free(copy);
    document_graph_selection_result_free(&result);
    const authoring_source_presentation *presentation = document_semantics_presentation(snapshot);
    uint64_t origin = 0;
    for (int i = 0; i < authoring_source_presentation_origin_count(presentation); i++) {
        const source_presentation_origin
            *o = authoring_source_presentation_origin_at(presentation, i);
        if (o->kind == SOURCE_PRESENTATION_STATEMENT && o->range.end > o->range.start &&
            strstr(text + o->range.start, "named :=") == text + o->range.start) {
            origin = o->identity;
        }
    }
    REQUIRE(origin);
    request = (document_graph_selection_request){ .operation = DOCUMENT_SELECTION_SOURCE_COPY,
                                                  .nodes = &origin,
                                                  .count = 1 };
    REQUIRE(document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    REQUIRE(result.text && strstr(result.text, "named := value") && !result.metadata);
    REQUIRE(!workspace_document_close(documents, handle));
    REQUIRE(strstr(result.text, "named := value"));
    return true;
}
TEST_CASE(graph_clipboard_candidates_refuse_duplicates_and_unknown_domain_nodes)
{
    REQUIRE(model());
    uint64_t duplicate[] = { selected[0], selected[0] };
    document_graph_selection_request request = { .operation = DOCUMENT_SELECTION_CLONE,
                                                 .graph = graph_id,
                                                 .nodes = duplicate,
                                                 .count = 2 };
    REQUIRE(!document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    request.operation = DOCUMENT_SELECTION_COPY;
    request.nodes = selected;
    request.count = 3;
    REQUIRE(document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    char *clipboard = strdup(result.text);
    document_graph_selection_result_free(&result);
    request = (document_graph_selection_request){ .operation = DOCUMENT_SELECTION_PASTE,
                                                  .graph = graph_id,
                                                  .text = clipboard,
                                                  .dx = 16,
                                                  .dy = 24 };
    REQUIRE(document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    REQUIRE(acount(result.created) == 3);
    free(clipboard);
    document_graph_selection_result_free(&result);
    request.text = "version 3\nid_counter 3\n(array)nodes { - id=1 "
                   "type=\"Unknown Invalid Node\"; }\n";
    REQUIRE(!document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    REQUIRE(!result.text && !result.metadata && !acount(result.created));
    workspace_document_view current;
    REQUIRE(
        !workspace_document_read(documents, handle, &current) &&
        current.revision == 1 && !current.can_undo
    );
    return true;
}
TEST_CASE(graph_presentation_native_types_and_rows_survive_document_retirement)
{
    native_type_catalog *
        types = native_type_catalog_new(DOCUMENT_GRAPH_SELECTION_TYPES, 2, error, sizeof error);
    REQUIRE(types);
    native_capability_pack pack = native_type_catalog_pack(types);
    native_capabilities *bindings = host_native_catalog_new(
        &pack,
        1,
        &DOCUMENT_GRAPH_SELECTION_DEFINITION_PACK,
        1,
        error,
        sizeof error
    );
    REQUIRE(bindings);
    native_capabilities_free(bindings);
    native_type_catalog_free(types);
    REQUIRE(model());
    document_graph_selection_request request = { .operation = DOCUMENT_SELECTION_COMMENT,
                                                 .graph = graph_id,
                                                 .node = selected[0],
                                                 .text = "Owned visible note" };
    REQUIRE(document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    workspace_document_view current;
    REQUIRE(!workspace_document_read(documents, handle, &current));
    REQUIRE(!workspace_document_edit(
        documents,
        handle,
        1,
        0,
        strlen(current.text),
        result.text
    ));
    REQUIRE(!workspace_document_read(documents, handle, &current));
    char graph_text[32];
    snprintf(graph_text, sizeof graph_text, "%llu", (unsigned long long)graph_id);
    pin_value input[] = { { .i = handle }, { .i = current.revision }, { .str = graph_text } },
              output[4] = { 0 };
    behavior_function_context context = { .value_storage = &native_storage };
    document_semantics_service service = { .documents = documents, .catalog = &catalog };
    const node_host_call *call = &DOCUMENT_GRAPH_SELECTION_CALLS[7];
    REQUIRE(!call->context(&context, &service, input, output));
    REQUIRE(output[0].b && !*output[1].str);
    pin_array *notes = output[2].ptr;
    REQUIRE(notes->count == 1);
    document_graph_note *note = notes->items[0].ptr;
    REQUIRE(!strcmp(note->title, "Owned visible note") && !note->box);
    REQUIRE(!workspace_document_close(documents, handle));
    REQUIRE(!strcmp(note->title, "Owned visible note"));
    pin_value refused[4] = { 0 };
    REQUIRE(!call->context(&context, &service, input, refused));
    REQUIRE(!refused[0].b && *refused[1].str && !((pin_array *)refused[2].ptr)->count);
    REQUIRE(!strcmp(note->title, "Owned visible note"));
    return true;
}
TEST_CASE(graph_clone_uses_visible_unplaced_positions_without_moving_originals)
{
    REQUIRE(model());
    workspace_document_view original;
    REQUIRE(!workspace_document_read(documents, handle, &original));
    parsed = behavior_asset_load(original.text);
    REQUIRE(parsed);
    for (int i = 0; i < 3; i++) {
        node *n = graph_node_by_id(parsed->graph, selected[i]);
        n->x = n->y = 0;
        n->placed = false;
    }
    char *unplaced = behavior_asset_save(parsed);
    REQUIRE(unplaced);
    REQUIRE(!workspace_document_edit(documents, handle, 1, 0, original.bytes, unplaced));
    free(unplaced);
    behavior_asset_free(parsed);
    parsed = NULL;
    document_semantics_free(snapshot);
    snapshot = document_semantics_new(documents, handle, 2, &catalog, error, sizeof error);
    REQUIRE(snapshot && document_semantics_graph(snapshot));
    float x[] = { 120, 400, 700 }, y[] = { 100, 200, 300 };
    document_graph_selection_request request = { .operation = DOCUMENT_SELECTION_CLONE,
                                                 .graph = graph_id,
                                                 .nodes = selected,
                                                 .count = 3,
                                                 .x = x,
                                                 .y = y,
                                                 .points = 3,
                                                 .dx = 32,
                                                 .dy = 48 };
    REQUIRE(document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    REQUIRE(acount(result.created) == 3);
    parsed = behavior_asset_load(result.text);
    REQUIRE(parsed);
    for (int i = 0; i < 3; i++) {
        const node *created = graph_node_by_id(parsed->graph, result.created[i]);
        const node *original_node = graph_node_by_id(parsed->graph, selected[i]);
        REQUIRE(
            created && created->placed && created->x == x[i] + 32 &&
            created->y == y[i] + 48
        );
        REQUIRE(
            original_node && !original_node->placed && original_node->x == 0 &&
            original_node->y == 0
        );
    }
    workspace_document_view current;
    REQUIRE(!workspace_document_read(documents, handle, &current));
    REQUIRE(current.revision == 2);
    document_graph_selection_result_free(&result);
    x[1] = NAN;
    REQUIRE(!document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    REQUIRE(strstr(error, "Clone positions"));
    return true;
}
TEST_CASE(graph_selection_missing_service_returns_typed_empty_refusals)
{
    arena storage;
    arena_init(&storage, 4096);
    behavior_function_context context = { .value_storage = &storage };
    for (int i = 0; i < DOCUMENT_GRAPH_SELECTION_CAPABILITY_COUNT; i++) {
        const node_host_call *call = &DOCUMENT_GRAPH_SELECTION_CALLS[i];
        pin_value output[6] = { 0 };
        REQUIRE(call->optional_service);
        REQUIRE(!call->context(&context, NULL, NULL, output));
        REQUIRE(!output[0].b && strstr(output[1].str, "unavailable"));
        if (i == 7) {
            REQUIRE(((pin_array *)output[2].ptr)->count == 0);
            REQUIRE(((pin_array *)output[3].ptr)->count == 0);
        } else {
            REQUIRE(!*output[2].str && !*output[3].str);
            REQUIRE(((pin_array *)output[4].ptr)->count == 0);
            REQUIRE(!strcmp(output[5].str, "0"));
        }
    }
    arena_free(&storage);
    return true;
}
TEST_CASE(source_node_flags_are_owned_revision_bound_metadata_candidates)
{
    const char *
        source = "library Flags { Identity :: pure fn(value:int)->(result:int) "
                 "{ named := value; return named; } }";
    REQUIRE(inspect(source, NULL));
    const behavior_asset *asset = document_semantics_graph(snapshot);
    const graph *g = NULL;
    for (int i = 0; i < acount(asset->graphs); i++) {
        if (asset->graphs[i]->kind == GRAPH_FUNCTION &&
            acount(asset->graphs[i]->nodes)) {
            g = asset->graphs[i];
            break;
        }
    }
    REQUIRE(g);
    uint64_t id = g->nodes[0]->id;
    for (int kind = 1; kind <= 3; kind++) {
        document_graph_selection_request request = { .operation = DOCUMENT_SELECTION_NODE_FLAG,
                                                     .graph = g->graph_id,
                                                     .nodes = &id,
                                                     .count = 1,
                                                     .flag_kind = kind,
                                                     .hidden = true };
        REQUIRE(document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
        REQUIRE(result.metadata && !strcmp(result.text, source));
        authoring_source_presentation *copy = authoring_source_presentation_clone(
            document_semantics_presentation(snapshot)
        );
        REQUIRE(authoring_source_presentation_restore(copy, result.metadata));
        const node *changed = graph_node_by_id(
            behavior_asset_find_graph(authoring_source_presentation_view(copy), g->graph_id),
            id
        );
        REQUIRE(changed);
        REQUIRE(kind != 1 || changed->breakpoint);
        REQUIRE(kind != 2 || changed->presentation == NODE_PRESENTATION_INLINE);
        REQUIRE(kind != 3 || changed->show_advanced);
        authoring_source_presentation_free(copy);
        document_graph_selection_result_free(&result);
    }
    uint64_t invalid[] = { id, UINT64_MAX };
    document_graph_selection_request request = { .operation = DOCUMENT_SELECTION_NODE_FLAG,
                                                 .graph = g->graph_id,
                                                 .nodes = invalid,
                                                 .count = 2,
                                                 .flag_kind = 1,
                                                 .hidden = true };
    REQUIRE(!document_graph_selection_prepare(snapshot, &request, &result, error, sizeof error));
    REQUIRE(!result.text && !result.metadata && !g->nodes[0]->breakpoint);
    workspace_document_view view;
    REQUIRE(!workspace_document_read(documents, handle, &view));
    REQUIRE(view.revision == 1 && !view.can_undo && !strcmp(view.text, source));
    return true;
}
void test_tool_document_graph_selection()
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(graph_selection_clone_and_delete_are_pure_candidates_with_shared_history);
    RUN_TEST_CASE(graph_selection_preflights_all_positions_and_retains_comments_boxes_and_reroutes);
    RUN_TEST_CASE(source_selection_metadata_and_clipboard_never_change_execution_source);
    RUN_TEST_CASE(graph_clipboard_candidates_refuse_duplicates_and_unknown_domain_nodes);
    RUN_TEST_CASE(graph_presentation_native_types_and_rows_survive_document_retirement);
    RUN_TEST_CASE(graph_clone_uses_visible_unplaced_positions_without_moving_originals);
    RUN_TEST_CASE(graph_selection_missing_service_returns_typed_empty_refusals);
    RUN_TEST_CASE(source_node_flags_are_owned_revision_bound_metadata_candidates);
    pu_clear_setup();
}
