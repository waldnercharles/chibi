#include "authoring/document_graph.h"

#include "authoring/document_graph_definition.h"
#include "authoring/document_layout.h"
#include "authoring/document_layout_definition.h"
#include "authoring/document_observation.h"
#include "authoring/document_semantics_definition.h"
#include "execution/definition.h"
#include "execution/function.h"
#include "graph/document/serialize.h"
#include "graph/authoring/graph_edit.h"
#include "graph/modeling/model_compile.h"
#include "graph/nodes/core_nodes.h"
#include "graph/shader/shader_graph.h"
#include "language/lower.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const registry_pack **previous;
static type_catalog_pack previous_types;
static bool had_previous_types;
static arena inspection_storage;
static workspace_documents *documents;
static behavior_asset *parsed;
static document_semantics *observation_snapshot;
static execution_module_plan *observation_module;
static int handle;
static char error[256];
static char layout_root[128];
static void setup(void)
{
    for (int i = 0; i < registry_pack_count(); i++) {
        apush(previous, registry_pack_at(i));
    }
    const type_catalog_pack *old = type_catalog_project();
    had_previous_types = old != NULL;
    if (old) {
        previous_types = *old;
        type_catalog_set_project(NULL);
    }
    type_catalog_pack types = { .types = SHADER_PACK.types,
                                .type_count = SHADER_PACK.type_count,
                                .enums = SHADER_PACK.enums,
                                .enum_count = SHADER_PACK.enum_count };
    type_catalog_set_project(&types);
    const registry_pack *packs[] = { &SHADER_PACK, &MODEL_PACK, &CORE_FLOW_PACK };
    registry_set_packs(packs, 3);
}
static void cleanup(void)
{
    arena_free(&inspection_storage);
    document_semantics_free(observation_snapshot);
    execution_module_plan_free(observation_module);
    observation_snapshot = NULL;
    observation_module = NULL;
    behavior_asset_free(parsed);
    parsed = NULL;
    workspace_documents_free(documents);
    documents = NULL;
    if (*layout_root) {
        char path[256];
        snprintf(path, sizeof path, "%s/Sample.chibi.meta", layout_root);
        unlink(path);
        snprintf(path, sizeof path, "%s/Sample.chibi", layout_root);
        unlink(path);
        rmdir(layout_root);
        *layout_root = 0;
    }
    registry_set_packs(previous, acount(previous));
    afree(previous);
    type_catalog_set_project(NULL);
    if (had_previous_types) {
        type_catalog_set_project(&previous_types);
    }
}
static bool open_graph(behavior_domain domain)
{
    behavior_asset *asset = behavior_asset_new("Example");
    asset->domain = domain;
    char *text = behavior_asset_save(asset);
    behavior_asset_free(asset);
    workspace_document_status status;
    documents = workspace_documents_new(NULL, &status);
    REQUIRE(status == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_memory(documents, text, &handle) == WORKSPACE_DOCUMENT_OK);
    free(text);
    return true;
}
static bool apply(document_graph_request request, uint64_t *created)
{
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    bool ok = document_graph_edit(documents, handle, view.revision, &request, created, error, sizeof error);
    if (!ok) {
        fprintf(stderr, "document_graph operation %d refused: %.255s\n", request.operation, error);
    }
    REQUIRE(ok);
    return true;
}
static bool read_graph(void)
{
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    behavior_asset_free(parsed);
    parsed = behavior_asset_load(view.text);
    REQUIRE(parsed);
    return true;
}
TEST_CASE(declarative_graph_typed_connections_defaults_and_history)
{
    REQUIRE(open_graph(BEHAVIOR_DOMAIN_MATERIAL));
    uint64_t output, time, ignored;
    REQUIRE(apply(
        (document_graph_request){ .operation = DOCUMENT_GRAPH_ADD,
                                  .type = "Shader Surface Output" },
        &output
    ));
    REQUIRE(
        apply((document_graph_request){ .operation = DOCUMENT_GRAPH_ADD, .type = "Shader Time" }, &time)
    );
    REQUIRE(read_graph());
    node *out = graph_node_by_id(parsed->graph, output),
         *producer = graph_node_by_id(parsed->graph, time);
    REQUIRE(out && producer);
    uint64_t roughness = out->input_pins[2].id, value = producer->output_pins[0].id;
    REQUIRE(apply(
        (document_graph_request){ .operation = DOCUMENT_GRAPH_DEFAULT,
                                  .node = output,
                                  .pin = roughness,
                                  .value = "0.25" },
        &ignored
    ));
    REQUIRE(read_graph());
    REQUIRE(graph_node_by_id(parsed->graph, output)->input_pins[2].default_value.f == .25f);
    REQUIRE(apply(
        (document_graph_request){ .operation = DOCUMENT_GRAPH_CONNECT,
                                  .node = output,
                                  .pin = roughness,
                                  .from_node = time,
                                  .from_pin = value },
        &ignored
    ));
    REQUIRE(read_graph());
    REQUIRE(acount(parsed->graph->edges) == 1);
    REQUIRE(apply(
        (document_graph_request){ .operation = DOCUMENT_GRAPH_DISCONNECT,
                                  .node = output,
                                  .pin = roughness },
        &ignored
    ));
    REQUIRE(read_graph());
    REQUIRE(acount(parsed->graph->edges) == 0);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_undo(documents, handle, view.revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(read_graph());
    REQUIRE(acount(parsed->graph->edges) == 1);
    return true;
}
TEST_CASE(declarative_literal_outputs_validate_owned_values_and_history)
{
    REQUIRE(open_graph(BEHAVIOR_DOMAIN_MATERIAL));
    uint64_t constant, time, output, ignored;
    REQUIRE(apply((document_graph_request){ .operation = DOCUMENT_GRAPH_ADD, .type = "Shader Constant" }, &constant));
    REQUIRE(apply((document_graph_request){ .operation = DOCUMENT_GRAPH_ADD, .type = "Shader Time" }, &time));
    REQUIRE(apply((document_graph_request){ .operation = DOCUMENT_GRAPH_ADD, .type = "Shader Surface Output" }, &output));
    REQUIRE(read_graph());
    node *literal = graph_node_by_id(parsed->graph, constant);
    node *computed = graph_node_by_id(parsed->graph, time);
    REQUIRE(literal && computed && literal->output_count == 1 && computed->output_count == 1);
    REQUIRE(graph_output_default_editable(literal, 0));
    REQUIRE(!graph_output_default_editable(computed, 0));
    REQUIRE(!graph_set_pin_type(parsed->graph, literal, 1, 0, PIN_STRUCT, "ShaderVec3"));
    float zero[] = { 0, 0, 0 };
    pin_value initial = { .type = PIN_STRUCT, .type_name = "ShaderVec3", .ptr = zero };
    REQUIRE(!graph_set_pin_default(parsed->graph, literal, 1, 0, &initial));
    uint64_t literal_pin = literal->output_pins[0].id, computed_pin = computed->output_pins[0].id;
    char *source = behavior_asset_save(parsed);
    REQUIRE(source);
    workspace_documents_free(documents);
    snprintf(layout_root, sizeof layout_root, "/tmp/chibi-literal-output-XXXXXX");
    REQUIRE(mkdtemp(layout_root));
    documents = workspace_documents_new(layout_root, NULL);
    REQUIRE(workspace_document_draft(documents, "Sample.kdl", source, &handle) == WORKSPACE_DOCUMENT_OK);
    free(source);
    REQUIRE(read_graph());
    literal = graph_node_by_id(parsed->graph, constant);
    REQUIRE(literal && literal->output_pins[0].id == literal_pin);
    REQUIRE(literal->output_pins[0].type == PIN_STRUCT);
    REQUIRE(literal->output_pins[0].type_name && !strcmp(literal->output_pins[0].type_name, "ShaderVec3"));
    REQUIRE(graph_output_default_editable(literal, 0));
    node *surface = graph_node_by_id(parsed->graph, output);
    REQUIRE(surface && surface->input_count > 0 && !strcmp(surface->input_pins[0].name, "Base Color"));
    REQUIRE(apply((document_graph_request){ .operation = DOCUMENT_GRAPH_CONNECT,
                                          .node = output,
                                          .pin = surface->input_pins[0].id,
                                          .from_node = constant,
                                          .from_pin = literal_pin }, &ignored));
    workspace_document_view before, after;
    REQUIRE(workspace_document_read(documents, handle, &before) == WORKSPACE_DOCUMENT_OK);
    char *original = strdup(before.text);
    document_graph_request request = { .operation = DOCUMENT_GRAPH_DEFAULT, .node = constant,
                                       .pin = literal_pin, .value = "x 0.25\ny 0.5\nz 0.75\n" };
    REQUIRE(apply(request, &ignored));
    REQUIRE(read_graph());
    literal = graph_node_by_id(parsed->graph, constant);
    const float *value = literal->output_pins[0].default_value.ptr;
    REQUIRE(value && value[0] == .25f && value[1] == .5f && value[2] == .75f);
    REQUIRE(workspace_document_read(documents, handle, &after) == WORKSPACE_DOCUMENT_OK);
    char *edited = strdup(after.text);
    static const script_catalog catalog = { 0 };
    document_semantics_service service = { .documents = documents, .catalog = &catalog };
    behavior_function_context context = { .value_storage = &inspection_storage };
    pin_value in[] = { { .i = handle }, { .i = after.revision } }, out[8] = { 0 };
    REQUIRE(!DOCUMENT_SEMANTICS_CALLS[0].context(&context, &service, in, out));
    if (!out[0].b) {
        fprintf(stderr, "literal output inspection refused: %.255s\n", out[6].str);
        const pin_array *diagnostics = out[1].ptr;
        for (int i = 0; diagnostics && i < diagnostics->count && i < 4; i++) {
            const document_diagnostic_row *row = diagnostics->items[i].ptr;
            fprintf(stderr, "literal output diagnostic: %.255s\n", row->message);
        }
    }
    REQUIRE(!strcmp(out[7].str, "kdl"));
    REQUIRE(out[0].b);
    pin_array *ports = out[5].ptr;
    bool found_literal = false;
    for (int i = 0; ports && i < ports->count; i++) {
        const document_port_row *row = ports->items[i].ptr;
        if (strtoull(row->node, NULL, 10) == constant && row->output) {
            REQUIRE(row->editable && row->vector_width == 3);
            REQUIRE(!strcmp(row->value_json, "{\"x\":0.25,\"y\":0.5,\"z\":0.75}"));
            found_literal = true;
        }
    }
    REQUIRE(found_literal);
    request.value = "\"invalid\"";
    REQUIRE(!document_graph_edit(documents, handle, after.revision, &request, &ignored, error, sizeof error));
    request.value = "x 1\ny 2\nz 3\n";
    REQUIRE(!document_graph_edit(documents, handle, after.revision - 1, &request, &ignored, error, sizeof error));
    request.node = time;
    request.pin = computed_pin;
    request.value = "1.0";
    REQUIRE(!document_graph_edit(documents, handle, after.revision, &request, &ignored, error, sizeof error));
    workspace_document_view refused;
    REQUIRE(workspace_document_read(documents, handle, &refused) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(refused.revision == after.revision && !strcmp(refused.text, edited) && refused.can_undo && !refused.can_redo);
    REQUIRE(workspace_document_undo(documents, handle, refused.revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, handle, &refused) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strcmp(refused.text, original) && refused.can_redo);
    REQUIRE(workspace_document_redo(documents, handle, refused.revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, handle, &refused) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strcmp(refused.text, edited));
    free(edited);
    free(original);
    return true;
}
TEST_CASE(declarative_graph_refusals_preserve_revision_bytes_and_history)
{
    REQUIRE(open_graph(BEHAVIOR_DOMAIN_MODEL));
    uint64_t sphere, ignored;
    REQUIRE(apply(
        (
            document_graph_request
        ){ .operation = DOCUMENT_GRAPH_ADD, .type = "Model Sphere", .x = 12, .y = 34 },
        &sphere
    ));
    workspace_document_view before, after;
    REQUIRE(workspace_document_read(documents, handle, &before) == WORKSPACE_DOCUMENT_OK);
    char *text = strdup(before.text);
    document_graph_request request = { .operation = DOCUMENT_GRAPH_ADD,
                                       .type = "Shader Time" };
    REQUIRE(
        !document_graph_edit(documents, handle, before.revision, &request, &ignored, error, sizeof error)
    );
    request = (document_graph_request){ .operation = DOCUMENT_GRAPH_PLACE,
                                        .node = sphere,
                                        .x = NAN };
    REQUIRE(
        !document_graph_edit(documents, handle, before.revision, &request, &ignored, error, sizeof error)
    );
    request.x = 0;
    REQUIRE(
        !document_graph_edit(documents, handle, before.revision - 1, &request, &ignored, error, sizeof error)
    );
    REQUIRE(workspace_document_read(documents, handle, &after) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(after.revision == before.revision && !strcmp(text, after.text));
    free(text);
    REQUIRE(apply(
        (
            document_graph_request
        ){ .operation = DOCUMENT_GRAPH_PLACE, .node = sphere, .x = 51, .y = 72, .placed = true },
        &ignored
    ));
    REQUIRE(read_graph());
    node *n = graph_node_by_id(parsed->graph, sphere);
    REQUIRE(n && n->placed && n->x == 51 && n->y == 72);
    REQUIRE(
        apply((document_graph_request){ .operation = DOCUMENT_GRAPH_REMOVE, .node = sphere }, &ignored)
    );
    REQUIRE(read_graph());
    REQUIRE(!graph_node_by_id(parsed->graph, sphere));
    return true;
}
TEST_CASE(declarative_graph_string_defaults_own_quotes_and_utf8)
{
    REQUIRE(open_graph(BEHAVIOR_DOMAIN_MATERIAL));
    uint64_t image, ignored;
    REQUIRE(
        apply((document_graph_request){ .operation = DOCUMENT_GRAPH_ADD, .type = "Shader Image Texture" }, &image)
    );
    REQUIRE(read_graph());
    node *n = graph_node_by_id(parsed->graph, image);
    uint64_t pin = 0;
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].type == PIN_STR) {
            pin = n->input_pins[i].id;
        }
    }
    REQUIRE(pin);
    char text[] = "images/雪\"tile";
    REQUIRE(apply(
        (
            document_graph_request
        ){ .operation = DOCUMENT_GRAPH_TEXT, .node = image, .pin = pin, .value = text },
        &ignored
    ));
    memset(text, 'x', sizeof text - 1);
    REQUIRE(read_graph());
    n = graph_node_by_id(parsed->graph, image);
    for (int i = 0; i < n->input_count; i++) {
        if (n->input_pins[i].id == pin) {
            REQUIRE(!strcmp(n->input_pins[i].default_value.str, "images/雪\"tile"));
        }
    }
    return true;
}
TEST_CASE(source_graph_typed_request_appends_declaration_and_refuses_stale_revision)
{
    workspace_document_status status;
    documents = workspace_documents_new(NULL, &status);
    REQUIRE(status == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        workspace_document_memory(documents, "library Sample { Identity :: pure fn(value:int)->(result:int) { return value; } }", &handle) ==
        WORKSPACE_DOCUMENT_OK
    );
    workspace_document_view before, after;
    REQUIRE(workspace_document_read(documents, handle, &before) == WORKSPACE_DOCUMENT_OK);
    script_catalog catalog = { 0 };
    document_semantics_service service = { .documents = documents, .catalog = &catalog };
    document_source_edit request = { .kind = AUTHORING_EDIT_APPEND_DECLARATIONS, .fragment = "Twice :: pure fn(value:int)->(result:int) { return value * 2; }" };
    pin_value in[] = { { .i = handle },
                       { .i = before.revision },
                       { .ptr = &request },
                       { 0 } },
              out[3] = { 0 };
    arena storage = { 0 };
    behavior_function_context context = { .value_storage = &storage };
    REQUIRE(!DOCUMENT_GRAPH_CALLS[10].context(&context, &service, in, out));
    REQUIRE(out[0].b);
    REQUIRE(workspace_document_read(documents, handle, &after) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(strstr(after.text, "Twice") && after.can_undo);
    REQUIRE(!DOCUMENT_GRAPH_CALLS[10].context(&context, &service, in, out));
    REQUIRE(!out[0].b && *out[1].str);
    REQUIRE(workspace_document_undo(documents, handle, after.revision) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_read(documents, handle, &after) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(!strstr(after.text, "Twice"));
    arena_free(&storage);
    return true;
}
static const graph *layout_graph(const behavior_asset *asset)
{
    for (int i = 0; asset && i < acount(asset->graphs); i++) {
        if (acount(asset->graphs[i]->nodes)) {
            return asset->graphs[i];
        }
    }
    return NULL;
}
TEST_CASE(source_layout_sidecar_restores_owned_drafts_history_and_saved_reopen)
{
    strcpy(layout_root, "/tmp/chibi-layout-XXXXXX");
    REQUIRE(mkdtemp(layout_root));
    workspace_document_status status;
    documents = workspace_documents_new(layout_root, &status);
    REQUIRE(documents && !status);
    REQUIRE(
        !workspace_document_create(documents, "Sample.chibi", "library Sample { Identity :: pure fn(value:int)->(result:int) { return value; } }", &handle)
    );
    workspace_document_view source;
    REQUIRE(!workspace_document_read(documents, handle, &source));
    script_catalog catalog = { 0 };
    document_semantics *snapshot = document_semantics_new(
        documents,
        handle,
        source.revision,
        &catalog,
        error,
        sizeof error
    );
    REQUIRE(snapshot && document_semantics_presentation(snapshot));
    const behavior_asset *asset = document_semantics_graph(snapshot);
    const graph *function = layout_graph(asset);
    REQUIRE(function);
    uint64_t gid = function->graph_id, nid = function->nodes[0]->id;
    char *initial = authoring_source_presentation_metadata(
             document_semantics_presentation(snapshot)
         ),
         *moved = NULL;
    REQUIRE(initial);
    REQUIRE(
        document_layout_prepare(snapshot, gid, nid, 412, 237, true, false, &moved, error, sizeof error)
    );
    REQUIRE(moved && strcmp(initial, moved));
    char *path = document_layout_path(documents, handle, source.revision, error, sizeof error);
    REQUIRE(path && !strcmp(path, "Sample.chibi.meta"));
    int sidecar;
    REQUIRE(!workspace_document_draft(documents, path, "", &sidecar));
    workspace_document_view meta;
    REQUIRE(!workspace_document_read(documents, sidecar, &meta));
    REQUIRE(
        !workspace_document_edit(documents, sidecar, meta.revision, 0, (int)meta.bytes, moved)
    );
    document_semantics_free(snapshot);
    snapshot = document_semantics_new(documents, handle, source.revision, &catalog, error, sizeof error);
    REQUIRE(snapshot);
    const node *n = graph_node_by_id(
        behavior_asset_find_graph(document_semantics_graph(snapshot), gid),
        nid
    );
    REQUIRE(n && n->placed && n->x == 412 && n->y == 237);
    document_semantics_free(snapshot);
    char *read = NULL;
    int handles[64], before = workspace_document_handles(documents, handles, 64);
    REQUIRE(!workspace_document_read_path(documents, path, &read) && !strcmp(read, moved));
    free(read);
    REQUIRE(workspace_document_handles(documents, handles, 64) == before);
    REQUIRE(!workspace_document_read(documents, sidecar, &meta));
    REQUIRE(!workspace_document_undo(documents, sidecar, meta.revision));
    REQUIRE(!workspace_document_read(documents, sidecar, &meta) && !strcmp(meta.text, ""));
    snapshot = document_semantics_new(documents, handle, source.revision, &catalog, error, sizeof error);
    REQUIRE(snapshot);
    n = graph_node_by_id(behavior_asset_find_graph(document_semantics_graph(snapshot), gid), nid);
    REQUIRE(n && !n->placed);
    REQUIRE(document_semantics_valid(snapshot));
    document_semantics_free(snapshot);
    REQUIRE(!workspace_document_read(documents, sidecar, &meta));
    REQUIRE(!workspace_document_redo(documents, sidecar, meta.revision));
    REQUIRE(!workspace_document_read(documents, sidecar, &meta));
    REQUIRE(!workspace_document_save(documents, sidecar, meta.revision));
    REQUIRE(!workspace_document_close(documents, sidecar));
    snapshot = document_semantics_new(documents, handle, source.revision, &catalog, error, sizeof error);
    REQUIRE(snapshot);
    n = graph_node_by_id(behavior_asset_find_graph(document_semantics_graph(snapshot), gid), nid);
    REQUIRE(n && n->placed && n->x == 412 && n->y == 237);
    document_semantics_free(snapshot);
    REQUIRE(
        !workspace_document_read(documents, handle, &source) &&
        !source.can_undo && !source.dirty
    );
    REQUIRE(!workspace_document_edit(
        documents,
        handle,
        source.revision,
        (int)source.bytes,
        (int)source.bytes,
        "\n"
    ));
    REQUIRE(!workspace_document_read(documents, handle, &source));
    snapshot = document_semantics_new(documents, handle, source.revision, &catalog, error, sizeof error);
    REQUIRE(snapshot);
    asset = document_semantics_graph(snapshot);
    for (int i = 0; i < acount(asset->graphs); i++) {
        for (int j = 0; j < acount(asset->graphs[i]->nodes); j++) {
            REQUIRE(!asset->graphs[i]->nodes[j]->placed);
        }
    }
    document_semantics_free(snapshot);
    free(initial);
    free(moved);
    free(path);
    return true;
}
TEST_CASE(source_layout_refusals_preserve_canonical_source_and_revision)
{
    workspace_document_status status;
    documents = workspace_documents_new(NULL, &status);
    REQUIRE(
        !workspace_document_memory(documents, "library Sample { Identity :: pure fn(value:int)->(result:int) { return value; } }", &handle)
    );
    workspace_document_view before, after;
    REQUIRE(!workspace_document_read(documents, handle, &before));
    script_catalog catalog = { 0 };
    document_semantics *snapshot = document_semantics_new(
        documents,
        handle,
        before.revision,
        &catalog,
        error,
        sizeof error
    );
    REQUIRE(snapshot);
    const behavior_asset *asset = document_semantics_graph(snapshot);
    const graph *function = layout_graph(asset);
    REQUIRE(function);
    char *metadata = NULL;
    REQUIRE(!document_layout_prepare(
        snapshot,
        function->graph_id,
        function->nodes[0]->id,
        NAN,
        1,
        true,
        false,
        &metadata,
        error,
        sizeof error
    ));
    REQUIRE(!metadata && *error);
    REQUIRE(
        !document_layout_prepare(snapshot, UINT64_MAX, 0, 0, 0, false, true, &metadata, error, sizeof error)
    );
    REQUIRE(!metadata);
    REQUIRE(!document_layout_path(documents, handle, before.revision + 1, error, sizeof error));
    REQUIRE(workspace_document_read_path(documents, "../escape", &metadata) == WORKSPACE_DOCUMENT_INVALID);
    document_semantics_free(snapshot);
    REQUIRE(!workspace_document_read(documents, handle, &after));
    REQUIRE(after.revision == before.revision && !after.dirty && !after.can_undo);
    return true;
}
TEST_CASE(declarative_function_signature_changes_refresh_owned_calls)
{
    REQUIRE(open_graph(BEHAVIOR_DOMAIN_SHADER_LIBRARY));
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    uint64_t function;
    document_scope_edit request = { .operation = DOCUMENT_SCOPE_ADD, .name = "Tint" };
    REQUIRE(
        document_graph_scope_edit(documents, handle, view.revision, &request, &function, error, sizeof error)
    );
    char graph_id[32];
    snprintf(graph_id, sizeof graph_id, "%llu", (unsigned long long)function);
    request = (document_scope_edit){ .operation = DOCUMENT_SCOPE_ADD_PIN,
                                     .graph = graph_id,
                                     .name = "Amount",
                                     .storage = PIN_F32 };
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    uint64_t ignored;
    REQUIRE(
        document_graph_scope_edit(documents, handle, view.revision, &request, &ignored, error, sizeof error)
    );
    request.result = true;
    request.name = "Value";
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        document_graph_scope_edit(documents, handle, view.revision, &request, &ignored, error, sizeof error)
    );
    request = (document_scope_edit){ .operation = DOCUMENT_SCOPE_CALL,
                                     .graph = graph_id,
                                     .caller = "0" };
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    uint64_t call;
    REQUIRE(
        document_graph_scope_edit(documents, handle, view.revision, &request, &call, error, sizeof error)
    );
    request = (document_scope_edit){ .operation = DOCUMENT_SCOPE_RENAME_PIN,
                                     .graph = graph_id,
                                     .name = "Strength",
                                     .index = 0 };
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        document_graph_scope_edit(documents, handle, view.revision, &request, &ignored, error, sizeof error)
    );
    REQUIRE(read_graph());
    node *n = graph_node_by_id(parsed->graph, call);
    REQUIRE(n && n->input_count == 1 && !strcmp(n->input_pins[0].name, "Strength"));
    request = (document_scope_edit){ .operation = DOCUMENT_SCOPE_REMOVE_PIN,
                                     .graph = graph_id,
                                     .index = 0 };
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        document_graph_scope_edit(documents, handle, view.revision, &request, &ignored, error, sizeof error)
    );
    REQUIRE(read_graph());
    n = graph_node_by_id(parsed->graph, call);
    REQUIRE(n && n->input_count == 0 && n->output_count == 1);
    return true;
}
TEST_CASE(graph_prepare_returns_owned_candidates_without_document_or_history_mutation)
{
    REQUIRE(open_graph(BEHAVIOR_DOMAIN_MATERIAL));
    workspace_document_view before, after;
    REQUIRE(workspace_document_read(documents, handle, &before) == WORKSPACE_DOCUMENT_OK);
    char *original = strdup(before.text), *candidate = NULL;
    uint64_t created;
    document_graph_request request = { .operation = DOCUMENT_GRAPH_ADD,
                                       .type = "Shader Time" };
    REQUIRE(
        document_graph_prepare(documents, handle, before.revision, &request, &created, &candidate, error, sizeof error)
    );
    REQUIRE(created && candidate);
    behavior_asset *planned = behavior_asset_load(candidate);
    REQUIRE(planned && graph_node_by_id(planned->graph, created));
    behavior_asset_free(planned);
    REQUIRE(workspace_document_read(documents, handle, &after) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(
        after.revision == before.revision && !after.can_undo &&
        !strcmp(original, after.text)
    );
    REQUIRE(
        workspace_document_edit(documents, handle, after.revision, 0, (int)after.bytes, candidate) ==
        WORKSPACE_DOCUMENT_OK
    );
    free(candidate);
    free(original);
    REQUIRE(read_graph());
    REQUIRE(graph_node_by_id(parsed->graph, created));
    REQUIRE(workspace_document_read(documents, handle, &after) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(workspace_document_undo(documents, handle, after.revision) == WORKSPACE_DOCUMENT_OK);
    return true;
}
TEST_CASE(source_graph_prepare_returns_validated_candidate_without_adoption)
{
    workspace_document_status status;
    documents = workspace_documents_new(NULL, &status);
    REQUIRE(status == WORKSPACE_DOCUMENT_OK);
    const char *text = "library Example { First :: pure fn()->(result:int) { "
                       "return 1; } }";
    REQUIRE(workspace_document_memory(documents, text, &handle) == WORKSPACE_DOCUMENT_OK);
    script_catalog catalog = { 0 };
    document_semantics_service service = { .documents = documents, .catalog = &catalog };
    document_source_edit request = {
        .kind = AUTHORING_EDIT_APPEND_DECLARATIONS,
        .fragment = "Second :: pure fn()->(result:int) { return 2; }"
    };
    pin_value in[] = { { .i = handle }, { .i = 1 }, { .ptr = &request }, { 0 } },
              out[4] = { 0 };
    arena storage = { 0 };
    behavior_function_context context = { .value_storage = &storage };
    REQUIRE(!DOCUMENT_GRAPH_CALLS[13].context(&context, &service, in, out));
    REQUIRE(out[0].b && strstr(out[3].str, "Second"));
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == 1 && !view.can_undo && !strcmp(view.text, text));
    arena_free(&storage);
    return true;
}
TEST_CASE(source_port_observation_keys_join_actual_lowered_captures_without_evaluating)
{
    const char *source = "library Capture { Twice :: pure "
                         "fn(value:int)->(result:int) { return value * 2; } "
                         "Use :: pure fn(value:int)->(result:int) { return "
                         "Twice(value); } }";
    documents = workspace_documents_new(NULL, NULL);
    REQUIRE(workspace_document_memory(documents, source, &handle) == WORKSPACE_DOCUMENT_OK);
    script_catalog catalog = { 0 };
    observation_snapshot = document_semantics_new(documents, handle, 1, &catalog, error, sizeof error);
    REQUIRE(observation_snapshot && document_semantics_valid(observation_snapshot));
    const script_analysis *analysis = document_semantics_analysis(observation_snapshot);
    observation_module = script_lower(analysis, &catalog, NULL);
    REQUIRE(observation_module && observation_module->valid);
    const behavior_asset *asset = document_semantics_graph(observation_snapshot);
    int inputs = 0, outputs = 0, fallbacks = 0;
    for (int g = 0; g < acount(asset->graphs); g++) {
        const graph *graph = asset->graphs[g];
        for (int n = 0; n < acount(graph->nodes); n++) {
            const node *node = graph->nodes[n];
            for (int side = 0; side < 2; side++) {
                int count = side ? node->output_count : node->input_count;
                const pin_spec *pins = side ? node->output_pins : node->input_pins;
                for (int pin = 0; pin < count; pin++) {
                    document_observation keys;
                    bool found = document_observation_query(
                        observation_snapshot,
                        graph->graph_id,
                        node->id,
                        pins[pin].id,
                        side != 0,
                        &keys,
                        error,
                        sizeof error
                    );
                    if (pins[pin].kind == PIN_EXEC) {
                        REQUIRE(!found);
                        continue;
                    }
                    if (!found) {
                        continue;
                    }
                    REQUIRE(keys.module == observation_module->id);
                    document_observation wrong;
                    REQUIRE(!document_observation_query(
                        observation_snapshot,
                        graph->graph_id,
                        node->id,
                        pins[pin].id,
                        side == 0,
                        &wrong,
                        error,
                        sizeof error
                    ));
                    for (int f = 0; f < observation_module->function_count; f++) {
                        const execution_module_function
                            *function = &observation_module->functions[f];
                        if (function->id != keys.function) {
                            continue;
                        }
                        for (int w = 0; w < acount(function->body->source_watches);
                             w++) {
                            const behavior_source_watch *watch = &function->body->source_watches[w];
                            if (watch->origin.occurrence == keys.occurrence &&
                                watch->ordinal == keys.index &&
                                watch->output == keys.output) {
                                if (side) {
                                    outputs++;
                                } else {
                                    inputs++;
                                }
                            }
                            if (keys.has_fallback &&
                                watch->origin.occurrence == keys.fallback_occurrence &&
                                watch->ordinal == keys.fallback_index &&
                                watch->output == keys.fallback_output) {
                                fallbacks++;
                            }
                        }
                    }
                }
            }
        }
    }
    REQUIRE(inputs > 0 && outputs > 0 && fallbacks > 0);
    workspace_document_view view;
    REQUIRE(workspace_document_read(documents, handle, &view) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(view.revision == 1 && !view.can_undo && !strcmp(view.text, source));
    return true;
}
TEST_CASE(source_node_execution_keys_match_lowered_points_and_refuse_unknown_identity)
{
    const char
        *source = "library Flow { Twice :: pure fn(value:int)->(result:int) { "
                  "return value * 2; } Use :: pure fn(value:int)->(result:int) "
                  "{ return Twice(value); } }";
    documents = workspace_documents_new(NULL, NULL);
    REQUIRE(workspace_document_memory(documents, source, &handle) == WORKSPACE_DOCUMENT_OK);
    static const script_catalog catalog = { 0 };
    observation_snapshot = document_semantics_new(documents, handle, 1, &catalog, error, sizeof error);
    REQUIRE(observation_snapshot && document_semantics_valid(observation_snapshot));
    observation_module = script_lower(document_semantics_analysis(observation_snapshot), &catalog, NULL);
    REQUIRE(observation_module && observation_module->valid);
    const behavior_asset *asset = document_semantics_graph(observation_snapshot);
    document_semantics_service service = { .documents = documents, .catalog = &catalog };
    behavior_function_context context = { .value_storage = &inspection_storage };
    pin_value in[] = { { .i = handle }, { .i = 1 } }, out[8] = { 0 };
    REQUIRE(!DOCUMENT_SEMANTICS_CALLS[0].context(&context, &service, in, out));
    REQUIRE(out[0].b && !*out[6].str);
    pin_array *inspected = out[2].ptr;
    int operators = 0, calls = 0, connections = 0;
    for (int i = 0; i < inspected->count; i++) {
        const document_node_row *row = inspected->items[i].ptr;
        REQUIRE(row->symbol && row->source && row->category);
        if (!strcmp(row->symbol, "×")) {
            REQUIRE(!strcmp(row->source, "value * 2"));
            REQUIRE(!strcmp(row->category, "expression") && !row->call);
            REQUIRE(row->source_kind == SOURCE_PRESENTATION_EXPRESSION);
            REQUIRE(row->operation == SCRIPT_EXPR_BINARY);
            operators++;
        }
        if (row->call) {
            REQUIRE(!strcmp(row->source, "Twice(value)"));
            REQUIRE(row->operation == SCRIPT_EXPR_CALL);
            calls++;
        }
    }
    pin_array *inspected_ports = out[5].ptr;
    REQUIRE(inspected_ports);
    for (int i = 0; i < inspected_ports->count; i++) {
        const document_port_row *row = inspected_ports->items[i].ptr;
        REQUIRE(row->visible && row->source && row->vector_width >= 0);
        connections += row->connected;
    }
    REQUIRE(operators > 0 && calls > 0 && connections > 0);
    int bulk_matched = 0;
    int matched = 0;
    for (int g = 0; g < acount(asset->graphs); g++) {
        const graph *graph = asset->graphs[g];
        for (int n = 0; n < acount(graph->nodes); n++) {
            document_execution key;
            if (!document_execution_query(
                    observation_snapshot,
                    graph->graph_id,
                    graph->nodes[n]->id,
                    &key,
                    error,
                    sizeof error
                )) {
                continue;
            }
            REQUIRE(key.module == observation_module->id && key.function && key.occurrence);
            for (int row = 0; row < inspected->count; row++) {
                const document_node_row *candidate = inspected->items[row].ptr;
                if (strtoull(candidate->graph, NULL, 10) == graph->graph_id &&
                    strtoull(candidate->node, NULL, 10) == graph->nodes[n]->id) {
                    REQUIRE(
                        candidate->execution_module == key.module &&
                        candidate->execution_function == key.function &&
                        candidate->execution_occurrence == key.occurrence
                    );
                    bulk_matched++;
                }
            }
            for (int f = 0; f < observation_module->function_count; f++) {
                const execution_module_function *function = &observation_module->functions[f];
                if (function->id != key.function) {
                    continue;
                }
                for (int point = 0; point < acount(function->body->source_points);
                     point++) {
                    matched += function->body->source_points[point].occurrence ==
                               key.occurrence;
                }
            }
        }
    }
    REQUIRE(matched > 0 && bulk_matched > 0);
    document_execution wrong = { 1, 2, 3 };
    REQUIRE(
        !document_execution_query(observation_snapshot, UINT64_MAX, UINT64_MAX, &wrong, error, sizeof error)
    );
    REQUIRE(!wrong.module && !wrong.function && !wrong.occurrence && *error);
    workspace_document_view current;
    REQUIRE(workspace_document_read(documents, handle, &current) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(current.revision == 1 && !current.can_undo && !strcmp(current.text, source));
    return true;
}
TEST_CASE(declarative_node_inspection_has_no_invented_source_execution_keys)
{
    REQUIRE(open_graph(BEHAVIOR_DOMAIN_MATERIAL));
    uint64_t output;
    REQUIRE(apply(
        (document_graph_request){ .operation = DOCUMENT_GRAPH_ADD,
                                  .type = "Shader Surface Output" },
        &output
    ));
    REQUIRE(read_graph());
    char *source = behavior_asset_save(parsed);
    workspace_documents_free(documents);
    documents = NULL;
    snprintf(layout_root, sizeof layout_root, "/tmp/chibi-bulk-kdl-XXXXXX");
    REQUIRE(mkdtemp(layout_root));
    documents = workspace_documents_new(layout_root, NULL);
    workspace_document_status
        status = workspace_document_draft(documents, "Sample.kdl", source, &handle);
    free(source);
    REQUIRE(status == WORKSPACE_DOCUMENT_OK);
    workspace_document_view before;
    REQUIRE(workspace_document_read(documents, handle, &before) == WORKSPACE_DOCUMENT_OK);
    static const script_catalog catalog = { 0 };
    document_semantics_service service = { .documents = documents, .catalog = &catalog };
    behavior_function_context context = { .value_storage = &inspection_storage };
    pin_value in[] = { { .i = handle }, { .i = before.revision } }, out[8] = { 0 };
    REQUIRE(!DOCUMENT_SEMANTICS_CALLS[0].context(&context, &service, in, out));
    REQUIRE(!strcmp(out[7].str, "kdl"));
    pin_array *nodes = out[2].ptr;
    REQUIRE(nodes && nodes->count == 1);
    const document_node_row *row = nodes->items[0].ptr;
    REQUIRE(strtoull(row->node, NULL, 10) == output);
    REQUIRE(!row->execution_module && !row->execution_function && !row->execution_occurrence);
    REQUIRE(workspace_document_close(documents, handle) == WORKSPACE_DOCUMENT_OK);
    REQUIRE(strtoull(row->node, NULL, 10) == output);
    return true;
}
void test_document_graph(void)
{
    pu_setup(setup, cleanup);
    RUN_TEST_CASE(source_port_observation_keys_join_actual_lowered_captures_without_evaluating);
    RUN_TEST_CASE(source_layout_sidecar_restores_owned_drafts_history_and_saved_reopen);
    RUN_TEST_CASE(source_layout_refusals_preserve_canonical_source_and_revision);
    RUN_TEST_CASE(graph_prepare_returns_owned_candidates_without_document_or_history_mutation);
    RUN_TEST_CASE(source_graph_prepare_returns_validated_candidate_without_adoption);
    RUN_TEST_CASE(declarative_graph_typed_connections_defaults_and_history);
    RUN_TEST_CASE(declarative_literal_outputs_validate_owned_values_and_history);
    RUN_TEST_CASE(declarative_graph_refusals_preserve_revision_bytes_and_history);
    RUN_TEST_CASE(declarative_graph_string_defaults_own_quotes_and_utf8);
    RUN_TEST_CASE(source_graph_typed_request_appends_declaration_and_refuses_stale_revision);
    RUN_TEST_CASE(declarative_function_signature_changes_refresh_owned_calls);
    RUN_TEST_CASE(source_node_execution_keys_match_lowered_points_and_refuse_unknown_identity);
    RUN_TEST_CASE(declarative_node_inspection_has_no_invented_source_execution_keys);
    pu_setup(NULL, NULL);
}
