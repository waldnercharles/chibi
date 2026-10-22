#include "authoring/language_catalog.h"
#include "authoring/scene_snapshot.h"
#include "authoring/source_prepare.h"
#include "execution/module.h"
#include "execution/status.h"
#include "runtime/native_definitions.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Disposable existing-type specimen, not a production scene/UI model. Three
 * cases, six source preparations and eighteen dispatches. No ECS world, files,
 * processes, suspension, threads or GPU. */
#define READ_SERVICE "test.scene_snapshot"
#define READ_NAME "Snapshot Read Specimen"
#define READ_ERROR "Scene snapshot is not current"
#define DISPATCH_BUDGET 1000
static const char SOURCE[] =
    "SceneReadSpecimen :: behavior {"
    "ids:entity[]=[]; parents:int[]=[]; names:string[]=[];"
    "observed:entity=0; parent:int=-1; text:string=\"\"; "
    "completed:bool=false;"
    "on Read :: () { result:=snapshot_read_specimen(); ids=result.Entities;"
    "parents=result.Parents; names=result.Names; completed=true; }"
    "on Inspect :: () { observed=ids[1]; parent=parents[1]; text=names[1]; } }";
static const behavior_backend BACKENDS[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
static const uint64_t IDENTITIES[] = {
    UINT64_C(0x8000000100000007),
    UINT64_C(0x8000000200000007),
    UINT64_C(0xfedcba9800000009),
};
typedef struct
{
    authoring_scene_owner *current;
    authoring_scene_snapshot *snapshot;
    int calls, publications, ownership_checks;
    bool owned;
} read_fixture;
static read_fixture fixture;
static authoring_scene_owner *owner, *other;
static authoring_scene_snapshot *snapshots[2];
static authoring_scene_node *nodes;
static char *names[3];
static authoring_language_catalog *catalog;
static authoring_source *prepared;
static execution_program *program;
static execution_instance *instance;
static bool backend_observed;

static bool owned_name(const arena *storage, const char *name)
{
    return !name || arena_contains(storage, name);
}
static const char *read_snapshot(
    const behavior_function_context *context,
    void *service,
    const pin_value *,
    pin_value *out
)
{
    read_fixture *f = service;
    assert(f && context->value_storage);
    f->calls++;
    if (!f->snapshot || !authoring_scene_snapshot_current(f->snapshot, f->current)) {
        return READ_ERROR;
    }
    int count;
    const authoring_scene_node *raw = authoring_scene_snapshot_nodes(f->snapshot, &count);
    pin_array *arrays[3];
    for (int i = 0; i < 3; i++) {
        assert(out[i].container == PIN_CONTAINER_ARRAY);
        arrays[i] = pin_array_snapshot_allocate(
            context->value_storage,
            out[i].type,
            out[i].type_name,
            count
        );
    }
    for (int i = 0; i < count; i++) {
        const pin_value values[] = {
            { .type = PIN_HANDLE,
              .type_name = "ecs.entity",
              .u64 = ecs_entity_to_u64(raw[i].entity) },
            { .type = PIN_I32, .i = raw[i].parent },
            { .type = PIN_STR,
              .text = &(const pin_string){ .bytes = raw[i].name,
                                           .length = (uint32_t)strlen(raw[i].name) } },
        };
        for (int j = 0; j < 3; j++) {
            if (pin_value_snapshot_to_arena(context->value_storage, &arrays[j]->items[i], &values[j])) {
                return "Scene snapshot value copy failed";
            }
        }
    }
    /* State assignment deep-copies too: independently observe callback ownership
     * before that later copy can conceal a borrowed name. Retain no pointers. */
    bool owned = true;
    for (int i = 0; i < 3; i++) {
        const pin_array *a = arrays[i];
        owned &= !out[i].record && !a->record &&
                 arena_contains(context->value_storage, a) &&
                 owned_name(context->value_storage, a->type_name);
        if (count) {
            owned &= arena_contains(context->value_storage, a->items);
        }
        for (int j = 0; j < count; j++) {
            owned &= !a->items[j].record &&
                     owned_name(context->value_storage, a->items[j].type_name);
        }
    }
    for (int i = 0; i < count; i++) {
        const char *copy = pin_text(arrays[2]->items[i]).bytes;
        owned &= copy && copy != raw[i].name &&
                 arena_contains(context->value_storage, copy);
    }
    f->owned = owned;
    f->ownership_checks++;
    for (int i = 0; i < 3; i++) {
        out[i].ptr = arrays[i];
    }
    f->publications++;
    return NULL;
}
static const node_host_call READ_CALL = {
    .service = READ_SERVICE,
    .missing_service = "Scene snapshot fixture is required",
    .context = read_snapshot,
    .reusable = false,
};
static const node_access READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
static const native_parameter_definition RESULTS[] = {
    { .name = "Entities",
      .storage = PIN_HANDLE,
      .container = PIN_CONTAINER_ARRAY,
      .type_name = "ecs.entity" },
    { .name = "Parents", .storage = PIN_I32, .container = PIN_CONTAINER_ARRAY },
    { .name = "Names", .storage = PIN_STR, .container = PIN_CONTAINER_ARRAY },
};
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = READ_NAME,
      .source_name = "snapshot_read_specimen",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .outputs = RESULTS,
      .output_count = NATIVE_COUNT(RESULTS),
      .completion_output = -1,
      .pure = true },
    { .canonical_name = "Read", .kind = SCRIPT_CAPABILITY_EVENT, .completion_output = -1 },
    { .canonical_name = "Inspect", .kind = SCRIPT_CAPABILITY_EVENT, .completion_output = -1 },
};
static const native_definition_binding BINDING = {
    .canonical_name = READ_NAME,
    .implementation = { .call = &READ_CALL, .access = &READ_ACCESS },
};
static bool read_contract(const native_capabilities *native)
{
    const script_catalog *declarations = native_capabilities_catalog(native);
    uint64_t id = native_capability_definition_id(&DEFINITIONS[0]);
    const script_catalog_capability *read = script_catalog_capability_by_id(declarations, id);
    REQUIRE(read && read->kind == SCRIPT_CAPABILITY_FUNCTION && read->pure);
    REQUIRE(read->effects == SCRIPT_EFFECT_READ);
    const native_capability_binding *binding = native_capabilities_find(
        native,
        id,
        script_catalog_capability_abi(declarations, read)
    );
    REQUIRE(binding && binding->call == &READ_CALL && binding->access == &READ_ACCESS);
    REQUIRE(!binding->command && !binding->query);
    const node_host_call *call = binding->call;
    REQUIRE(call->context == read_snapshot && !call->invoke && !call->value && !call->action);
    REQUIRE(call->service && !strcmp(call->service, READ_SERVICE));
    REQUIRE(!call->reusable && !call->optional_service && !call->variadic);
    const node_access *access = binding->access;
    REQUIRE(access->flags == NODE_EFFECT_WORLD_READ);
    REQUIRE(!access->read_count && !access->write_count && !access->read_refs);
    return true;
}
static void free_producer()
{
    for (int i = 0; i < 3; i++) {
        free(names[i]);
        names[i] = NULL;
    }
    free(nodes);
    nodes = NULL;
}
static void cleanup()
{
    execution_program_free(program);
    program = NULL;
    instance = NULL;
    authoring_source_free(prepared);
    prepared = NULL;
    authoring_language_catalog_free(catalog);
    catalog = NULL;
    for (int i = 0; i < 2; i++) {
        if (snapshots[i]) {
            authoring_scene_snapshot_free(snapshots[i]);
        }
        snapshots[i] = NULL;
    }
    if (owner) {
        authoring_scene_owner_free(owner);
    }
    if (other) {
        authoring_scene_owner_free(other);
    }
    owner = other = NULL;
    free_producer();
    fixture = (read_fixture){ 0 };
}
static uint64_t state_id(const char *name)
{
    for (int i = 0; i < prepared->analysis->symbol_count; i++) {
        const script_symbol *s = &prepared->analysis->symbols[i];
        if (s->kind == SCRIPT_STATE && !strcmp(s->name, name)) {
            return s->id;
        }
    }
    return 0;
}
static const pin_value *state(const char *name)
{
    return execution_instance_state(instance, state_id(name));
}
static bool prepare(behavior_backend backend, int count)
{
    REQUIRE(sizeof SOURCE <= 1024 && count >= 0 && count <= 3);
    REQUIRE(behavior_backend_available(BEHAVIOR_SLJIT));
    const native_definition_pack pack = {
        .base = { .entity_type = script_catalog_id("type", "ecs.entity") },
        .definitions = DEFINITIONS,
        .definition_count = NATIVE_COUNT(DEFINITIONS),
        .bindings = &BINDING,
        .binding_count = 1,
    };
    char error[256];
    native_capabilities *
        native = native_capabilities_from_definition_packs(NULL, 0, &pack, 1, error, sizeof error);
    if (!native) {
        fprintf(stderr, "snapshot native declarations: %s\n", error);
    }
    REQUIRE(native);
    /* The helper can fail a REQUIRE, so release the local table before
     * returning from preparation even when a metadata assertion fails. */
    bool contract = read_contract(native);
    if (contract) {
        const authoring_language_catalog_input input = { .native = native };
        catalog = authoring_language_catalog_new(&input, error, sizeof error);
    }
    native_capabilities_free(native);
    REQUIRE(contract && catalog);
    script_lower_bindings bindings = authoring_language_bindings(catalog);
    prepared = authoring_source_prepare_document(
        "scene_snapshot_read.chibi",
        "test/scene-snapshot-read",
        SOURCE,
        authoring_language_catalog_view(catalog),
        &bindings,
        backend
    );
    REQUIRE(prepared);
    for (int i = 0; i < prepared->diagnostic_count; i++) {
        fprintf(stderr, "snapshot source: %s\n", prepared->diagnostics[i].message);
    }
    REQUIRE(prepared->valid);
    REQUIRE(execution_module_backend(prepared->module) == backend);
    /* module_effects conservatively classifies authored locals/arrays/state
     * stores as unknown. Read purity belongs to the native binding above, not
     * this whole stateful module; the module must still remain serial. */
    REQUIRE(!execution_module_effects(prepared->module).parallel_safe);
    service_binding *services = NULL;
    apush(services, ((service_binding){ .type_name = READ_SERVICE, .value = &fixture }));
    program = execution_program_new(prepared->module, NULL, services);
    afree(services);
    REQUIRE(program);
    instance = execution_program_attach(program, 1);
    REQUIRE(instance);
    owner = authoring_scene_owner_new();
    if (count) {
        nodes = xcalloc((size_t)count, sizeof *nodes);
    }
    const char *const initial[] = { "Root", "Child", "Other" };
    for (int i = 0; i < count; i++) {
        names[i] = xstrdup(initial[i]);
        nodes[i] = (authoring_scene_node){
            .entity = ecs_entity_from_u64(IDENTITIES[i]),
            .name = names[i],
            .parent = i == 1 ? 0 : -1,
        };
    }
    snapshots[0] = authoring_scene_snapshot_new(owner, nodes, count, error, sizeof error);
    REQUIRE(snapshots[0]);
    fixture.current = owner;
    fixture.snapshot = snapshots[0];
    return true;
}
static int dispatch(const char *event)
{
    uint64_t id = execution_module_event(prepared->module, event);
    assert(id);
    behavior_run_scope runs = { 0 };
    behavior_run_scope_begin(&runs);
    int status = execution_instance_dispatch(instance, id, NULL, 0, 0, 0, DISPATCH_BUDGET);
    behavior_run_scope_end(&runs);
    behavior_backend backend = execution_module_backend(prepared->module);
    backend_observed = !runs.counts.overflow && !runs.counts.wasm_entries &&
                       !runs.counts.inspected_sljit_entries &&
                       !runs.counts.inspected_wasm_entries &&
                       !runs.counts.inspection_bytecode_fallbacks &&
                       (backend == BEHAVIOR_BYTECODE
                            ? runs.counts.bytecode_entries > 0 && !runs.counts.sljit_entries
                            : runs.counts.sljit_entries > 0 &&
                                  !runs.counts.bytecode_entries);
    fprintf(
        stderr,
        "scene read entries: event=%s bytecode=%llu sljit=%llu fallback=%llu\n",
        event,
        (unsigned long long)runs.counts.bytecode_entries,
        (unsigned long long)runs.counts.sljit_entries,
        (unsigned long long)runs.counts.inspection_bytecode_fallbacks
    );
    return status;
}
static bool result(int count, const char *child)
{
    const char *const fields[] = { "ids", "parents", "names" };
    const pin_type types[] = { PIN_HANDLE, PIN_I32, PIN_STR };
    const pin_array *arrays[3];
    for (int i = 0; i < 3; i++) {
        const pin_value *value = state(fields[i]);
        REQUIRE(
            value && value->type == types[i] &&
            value->container == PIN_CONTAINER_ARRAY && !value->record
        );
        arrays[i] = value->ptr;
        REQUIRE(
            arrays[i] && arrays[i]->element_type == types[i] &&
            arrays[i]->count == count && !arrays[i]->record
        );
    }
    REQUIRE(state("ids")->type_name && !strcmp(state("ids")->type_name, "ecs.entity"));
    REQUIRE(arrays[0]->type_name && !strcmp(arrays[0]->type_name, "ecs.entity"));
    for (int i = 0; i < count; i++) {
        pin_value items[] = { pin_array_get(arrays[0], i),
                              pin_array_get(arrays[1], i),
                              pin_array_get(arrays[2], i) };
        REQUIRE(!items[0].record && !items[1].record && !items[2].record);
        REQUIRE(items[0].type == PIN_HANDLE && items[0].u64 == IDENTITIES[i]);
        REQUIRE(items[0].type_name && !strcmp(items[0].type_name, "ecs.entity"));
        REQUIRE(items[1].type == PIN_I32 && items[1].i == (i == 1 ? 0 : -1));
        const char *name = i == 0 ? "Root" : i == 1 ? child : "Other";
        REQUIRE(items[2].type == PIN_STR && !strcmp(pin_text(items[2]).bytes, name));
    }
    return true;
}
static bool rejected(const char *child)
{
    int calls = fixture.calls, publications = fixture.publications;
    int ownership_checks = fixture.ownership_checks;
    REQUIRE(
        execution_instance_set_state(instance, state_id("completed"), (pin_value){ .type = PIN_BOOL }) ==
        FLOW_OK
    );
    execution_program_clear_error(program);
    REQUIRE(!execution_program_error(program));
    int status = dispatch("Read");
    REQUIRE(backend_observed);
    const execution_program_failure *error = execution_program_error(program);
    if (status != FLOW_ERR_INVALID_GRAPH || !error ||
        error->status != FLOW_ERR_INVALID_GRAPH ||
        error->kind != BEHAVIOR_FUNCTION_HOST_DATA ||
        strcmp(error->message, READ_ERROR)) {
        fprintf(
            stderr,
            "Scene read rejection: status=%d error_status=%d kind=%d "
            "message=%s\n",
            status,
            error ? error->status : 0,
            error ? (int)error->kind : -1,
            error ? error->message : "(none)"
        );
    }
    REQUIRE(status == FLOW_ERR_INVALID_GRAPH);
    REQUIRE(error && error->status == FLOW_ERR_INVALID_GRAPH);
    REQUIRE(error->kind == BEHAVIOR_FUNCTION_HOST_DATA);
    REQUIRE(!strcmp(error->message, READ_ERROR));
    REQUIRE(fixture.calls == calls + 1 && fixture.publications == publications);
    REQUIRE(fixture.ownership_checks == ownership_checks);
    REQUIRE(state("completed") && !state("completed")->b);
    REQUIRE(result(2, child));
    return true;
}
TEST_CASE(scene_read_copies_identity_order_and_names_into_authored_state)
{
    for (int b = 0; b < NATIVE_COUNT(BACKENDS); b++) {
        cleanup();
        REQUIRE(prepare(BACKENDS[b], 3));
        REQUIRE(dispatch("Read") == FLOW_OK);
        REQUIRE(backend_observed);
        REQUIRE(result(3, "Child"));
        REQUIRE(state("completed") && state("completed")->b);
        REQUIRE(fixture.owned && fixture.ownership_checks == 1 && fixture.publications == 1);
        authoring_scene_owner_changed(owner);
        for (int i = 0; i < 3; i++) {
            names[i][0] = 'X';
            nodes[i].entity = ecs_entity_from_u64(99);
        }
        authoring_scene_owner_free(owner);
        owner = NULL;
        free_producer();
        authoring_scene_snapshot_free(snapshots[0]);
        snapshots[0] = NULL;
        fixture.current = NULL;
        fixture.snapshot = NULL;
        REQUIRE(dispatch("Inspect") == FLOW_OK);
        REQUIRE(backend_observed);
        REQUIRE(
            state("observed") && state("observed")->type == PIN_HANDLE &&
            !state("observed")->record && state("observed")->u64 == IDENTITIES[1]
        );
        REQUIRE(
            state("parent") && state("parent")->type == PIN_I32 &&
            !state("parent")->record && state("parent")->i == 0
        );
        REQUIRE(
            state("text") && state("text")->type == PIN_STR && !state("text")->record &&
            !strcmp(pin_text(*(state("text"))).bytes, "Child")
        );
        REQUIRE(result(3, "Child") && fixture.calls == 1);
    }
    return true;
}
TEST_CASE(scene_read_rejects_wrong_stale_closed_and_absent_current_owners)
{
    for (int b = 0; b < NATIVE_COUNT(BACKENDS); b++) {
        cleanup();
        REQUIRE(prepare(BACKENDS[b], 2));
        REQUIRE(dispatch("Read") == FLOW_OK && result(2, "Child"));
        REQUIRE(backend_observed);
        other = authoring_scene_owner_new();
        fixture.current = other;
        REQUIRE(rejected("Child"));
        fixture.current = owner;
        authoring_scene_owner_changed(owner);
        memcpy(names[1], "Fresh", sizeof "Fresh");
        REQUIRE(rejected("Child"));
        char error[256];
        snapshots[1] = authoring_scene_snapshot_new(owner, nodes, 2, error, sizeof error);
        REQUIRE(snapshots[1]);
        fixture.snapshot = snapshots[1];
        authoring_scene_snapshot_free(snapshots[0]);
        snapshots[0] = NULL;
        execution_program_clear_error(program);
        REQUIRE(dispatch("Read") == FLOW_OK && !execution_program_error(program));
        REQUIRE(backend_observed);
        REQUIRE(result(2, "Fresh") && state("completed")->b);
        REQUIRE(fixture.owned && fixture.ownership_checks == 2 && fixture.publications == 2);
        authoring_scene_owner_free(owner);
        owner = NULL;
        /* The closed origin remains allocated because snapshots[1] retains it. */
        REQUIRE(rejected("Fresh"));
        fixture.current = NULL;
        REQUIRE(rejected("Fresh"));
        REQUIRE(fixture.calls == 6 && fixture.publications == 2);
    }
    return true;
}
TEST_CASE(scene_read_preserves_typed_empty_arrays_after_snapshot_destruction)
{
    for (int b = 0; b < NATIVE_COUNT(BACKENDS); b++) {
        cleanup();
        REQUIRE(prepare(BACKENDS[b], 0));
        REQUIRE(dispatch("Read") == FLOW_OK && result(0, NULL));
        REQUIRE(backend_observed);
        REQUIRE(state("completed") && state("completed")->b);
        REQUIRE(fixture.owned && fixture.ownership_checks == 1 && fixture.publications == 1);
        authoring_scene_owner_free(owner);
        owner = NULL;
        authoring_scene_snapshot_free(snapshots[0]);
        snapshots[0] = NULL;
        fixture.current = NULL;
        fixture.snapshot = NULL;
        REQUIRE(result(0, NULL) && fixture.calls == 1);
    }
    return true;
}
void test_scene_snapshot_read()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(scene_read_copies_identity_order_and_names_into_authored_state);
    RUN_TEST_CASE(scene_read_rejects_wrong_stale_closed_and_absent_current_owners);
    RUN_TEST_CASE(scene_read_preserves_typed_empty_arrays_after_snapshot_destruction);
    pu_clear_setup();
}
