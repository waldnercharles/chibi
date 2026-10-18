#include "compiler_fixture.h"

#include "backend.h"
#include "execution/module.h"
#include "execution/status.h"
#include "runtime/arrays_definition.h"
#include "runtime/text_definition.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"

#include <stdio.h>
#include <string.h>

static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
const authoring_package *compiler_fixture_package(
    const compiler_fixture *fixture,
    int backend,
    const char *identity
)
{
    for (int i = 0; i < authoring_packages_count(fixture->owners[backend]); i++) {
        const authoring_package *package = authoring_packages_at(fixture->owners[backend], i);
        if (!strcmp(package->identity, identity)) {
            return package;
        }
    }
    return NULL;
}
uint64_t compiler_fixture_export(
    const compiler_fixture *fixture,
    int backend,
    const char *identity,
    const char *name
)
{
    const authoring_package *package = compiler_fixture_package(fixture, backend, identity);
    for (int i = 0; package && i < package->exports->member_count; i++) {
        if (!strcmp(package->exports->members[i].name, name)) {
            return package->exports->members[i].id;
        }
    }
    return 0;
}
void compiler_fixture_free(compiler_fixture *fixture)
{
    for (int b = 0; b < 2; b++) {
        authoring_packages_free(fixture->owners[b]);
    }
    native_capabilities_free(fixture->native);
    *fixture = (compiler_fixture){ 0 };
}
bool compiler_fixture_prepare(compiler_fixture *fixture, const source_package_input *files, int count)
{
    native_definition_pack packs[] = { NATIVE_TEXT_DEFINITION_PACK,
                                       NATIVE_ARRAY_DEFINITION_PACK };
    char message[192];
    fixture->native = native_capabilities_from_definition_packs(NULL, 0, packs, 2, message, sizeof message);
    REQUIRE(fixture->native);
    source_package_collection collections[] = { { "core", "/sdk/core" } };
    source_package_snapshot_options options = { .project_root = "/compiler",
                                                .files = files,
                                                .file_count = count,
                                                .collections = collections,
                                                .collection_count = 1 };
    for (int b = 0; b < 2; b++) {
        source_package_snapshot *snapshot = source_package_snapshot_new(&options);
        REQUIRE(snapshot);
        const source_package_snapshot_error *error = source_package_snapshot_error_at(snapshot);
        if (error) {
            fprintf(
                stderr,
                "compiler fixture:%d:%d: %s\n",
                error->diagnostic.range.line,
                error->diagnostic.range.column,
                error->diagnostic.message
            );
            source_package_snapshot_free(snapshot);
            REQUIRE(false);
        }
        script_lower_bindings bindings = authoring_native_bindings(fixture->native);
        fixture->owners[b] = authoring_packages_prepare(
            snapshot,
            native_capabilities_catalog(fixture->native),
            &bindings,
            backends[b],
            NULL
        );
        REQUIRE(fixture->owners[b]);
        for (int p = 0; p < authoring_packages_count(fixture->owners[b]); p++) {
            const authoring_package *package = authoring_packages_at(fixture->owners[b], p);
            for (int d = 0; d < package->diagnostic_count; d++) {
                const script_diagnostic *diagnostic = &package->diagnostics[d];
                fprintf(
                    stderr,
                    "%s:%d:%d: %s\n",
                    package->identity,
                    diagnostic->range.line,
                    diagnostic->range.column,
                    diagnostic->message
                );
            }
        }
        REQUIRE(authoring_packages_valid(fixture->owners[b]));
    }
    return true;
}
const pin_record *compiler_fixture_record(
    const compiler_fixture *fixture,
    int backend,
    const char *identity,
    const char *name,
    arena *storage
)
{
    const authoring_package *package = compiler_fixture_package(fixture, backend, identity);
    if (!package) {
        return NULL;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        package->exports->metadata,
        compiler_fixture_export(fixture, backend, identity, name)
    );
    if (!type || !type->authored_record || type->field_count > 32) {
        return NULL;
    }
    type_field fields[32] = { 0 };
    for (int i = 0; i < type->field_count; i++) {
        fields[i] = (type_field){ .name = type->fields[i].name,
                                  .type = type->fields[i].type.storage };
    }
    return pin_record_create(storage, type->name, type->id, fields, type->field_count);
}
typedef struct
{
    behavior_function_context context;
    behavior_function_activation activation;
    arena producer;
    pin_value result;
} call_state;
static bool run(
    const compiler_fixture *fixture,
    int backend,
    const char *identity,
    const char *name,
    const pin_value *arguments,
    int count,
    int budget,
    arena *detached,
    pin_value *out,
    call_state *state
)
{
    REQUIRE(backend >= 0 && backend < 2 && budget > 0);
    const authoring_package *package = compiler_fixture_package(fixture, backend, identity);
    REQUIRE(package && package->valid && package->module);
    uint64_t id = compiler_fixture_export(fixture, backend, identity, name);
    const script_catalog_capability
        *signature = script_catalog_capability_by_id(package->exports->metadata, id);
    REQUIRE(signature && signature->result_count == 1);
    const behavior_function *function = NULL;
    for (int i = 0; i < execution_module_function_count(package->module); i++) {
        execution_module_function_info
            info = execution_module_function_at(package->module, i);
        if (info.id == id) {
            function = info.body;
        }
    }
    REQUIRE(function);
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &state->activation);
    }
    state->result = (pin_value){ .type = signature->results[0].type.storage,
                                 .container = signature->results[0].type.container };
    state->context = (behavior_function_context){
        .activation = behavior_function_resumable(function) ? &state->activation : NULL,
        .arguments = arguments,
        .argument_count = count,
        .results = &state->result,
        .budget = (uintptr_t)budget
    };
    behavior_function_bind_storage(&state->context, &state->producer);
    REQUIRE(behavior_function_bind(function, &state->context, NULL, 0));
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = behavior_function_run(function, &state->context, backend != 0);
    behavior_run_scope_end(&runs);
    REQUIRE(status == FLOW_OK && state->context.returned);
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    REQUIRE(backend == 0 ? runs.counts.bytecode_entries > 0 : !runs.counts.bytecode_entries);
    REQUIRE(backends[backend] != BEHAVIOR_SLJIT || runs.counts.sljit_entries > 0);
    REQUIRE(backends[backend] != BEHAVIOR_WASM || runs.counts.wasm_entries > 0);
    REQUIRE(!pin_value_snapshot_to_arena(detached, out, &state->result));
    return true;
}
bool compiler_fixture_call(
    const compiler_fixture *fixture,
    int backend,
    const char *identity,
    const char *name,
    const pin_value *arguments,
    int count,
    int budget,
    arena *detached,
    pin_value *out
)
{
    call_state state = { 0 };
    bool valid = run(fixture, backend, identity, name, arguments, count, budget, detached, out, &state);
    behavior_function_release_host(&state.context);
    behavior_function_activation_free(&state.activation);
    arena_free(&state.producer);
    return valid;
}
bool compiler_fixture_nodes(TSNode root, compiler_fixture_node *nodes, int capacity, int depth_cap, int *count)
{
    REQUIRE(capacity > 0 && depth_cap >= 0 && nodes && count && !ts_node_is_null(root));
    REQUIRE(ts_node_descendant_count(root) <= (uint32_t)capacity);
    *count = 0;
    TSTreeCursor cursor = ts_tree_cursor_new(root);
    int parent = -1, previous = -1, depth = 0;
    for (;;) {
        if (depth > depth_cap || *count >= capacity) {
            ts_tree_cursor_delete(&cursor);
            REQUIRE(false);
        }
        int current = (*count)++;
        nodes[current] = (compiler_fixture_node){
            ts_tree_cursor_current_node(&cursor),
            ts_tree_cursor_current_field_name(&cursor),
            parent,
            -1,
            -1
        };
        if (parent >= 0) {
            if (previous < 0) {
                nodes[parent].first_child = current;
            } else {
                nodes[previous].next_sibling = current;
            }
        }
        if (ts_tree_cursor_goto_first_child(&cursor)) {
            parent = current;
            previous = -1;
            depth++;
            continue;
        }
        for (;;) {
            if (ts_tree_cursor_goto_next_sibling(&cursor)) {
                previous = current;
                break;
            }
            if (!ts_tree_cursor_goto_parent(&cursor)) {
                ts_tree_cursor_delete(&cursor);
                return true;
            }
            current = parent;
            parent = nodes[current].parent;
            depth--;
        }
    }
}
static pin_value integer(int value)
{
    return (pin_value){ .type = PIN_I32, .i = value };
}
static pin_value boolean(bool value)
{
    return (pin_value){ .type = PIN_BOOL, .b = value };
}
static pin_value text(const char *value)
{
    return (pin_value){ .type = PIN_STR, .str = value };
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    pin_write_field(row->ptr, field, value);
    return true;
}
bool compiler_fixture_pack_nodes(
    const compiler_fixture *fixture,
    int backend,
    const compiler_fixture_node *nodes,
    int count,
    arena *input,
    pin_value *out,
    pin_value *fields
)
{
    const pin_record *record = compiler_fixture_record(fixture, backend, "project:syntax_diagnostics", "Node", input);
    REQUIRE(record && record->type.field_count == 11);
    pin_array
        *array = pin_array_snapshot_allocate(input, PIN_STRUCT, record->type.name, count);
    pin_array *names = fields ? pin_array_snapshot_allocate(input, PIN_STR, NULL, count)
                              : NULL;
    REQUIRE(array && (!fields || names));
    array->record = record;
    for (int i = 0; i < count; i++) {
        TSNode node = nodes[i].node;
        TSPoint point = ts_node_start_point(node);
        pin_value row = { .type = PIN_STRUCT,
                          .ptr = arena_alloc(input, record->type.size),
                          .record = record,
                          .type_name = record->type.name };
        memset(row.ptr, 0, record->type.size);
        REQUIRE(put(&row, "kind", text(ts_node_type(node))));
        REQUIRE(
            put(&row, "start", integer((int)ts_node_start_byte(node))) &&
            put(&row, "end", integer((int)ts_node_end_byte(node)))
        );
        REQUIRE(
            put(&row, "row", integer((int)point.row)) &&
            put(&row, "column", integer((int)point.column))
        );
        REQUIRE(
            put(&row, "parent", integer(nodes[i].parent)) &&
            put(&row, "first_child", integer(nodes[i].first_child)) &&
            put(&row, "next_sibling", integer(nodes[i].next_sibling))
        );
        REQUIRE(
            put(&row, "error", boolean(ts_node_is_error(node))) &&
            put(&row, "missing", boolean(ts_node_is_missing(node))) &&
            put(&row, "has_error", boolean(ts_node_has_error(node)))
        );
        array->items[i] = row;
        if (names) {
            names->items[i] = text(nodes[i].field ? nodes[i].field : "");
        }
    }
    *out = (pin_value){ .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = array,
                        .record = record,
                        .type_name = record->type.name };
    if (fields) {
        *fields = (pin_value){ .type = PIN_STR,
                               .container = PIN_CONTAINER_ARRAY,
                               .ptr = names };
    }
    return true;
}
