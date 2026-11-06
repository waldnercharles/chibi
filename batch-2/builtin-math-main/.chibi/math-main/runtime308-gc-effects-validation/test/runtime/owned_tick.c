#include "authoring/source_prepare.h"
#include "execution/inspection.h"
#include "runtime/behaviors/source.h"
#include "support/array.h"
#include "test/support/reflected_types.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/text.h"

#include <stdio.h>
#include <string.h>

/* Managed snapshots survive producer retirement, sibling execution and publication. */
static ecs_world *world;
static source_registry *registry;
static authoring_source *source;
static execution_inspector *inspector;
static uint64_t state_ids[4], captured_id;
static const char *const state_names[] = { "source", "saved", "final", "runs" };
static const script_catalog_capability capabilities[] = {
    { .id = 1, .name = "Tick", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
    { .id = 2,
      .name = "Fail",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .effects = SCRIPT_EFFECT_HOST,
      .completion_outcome = -1 },
};
static const char *fail(void *, const pin_value *, pin_value *)
{
    return "retained owned Tick failure";
}
static bool host(void *, uint64_t id, script_lower_host *out)
{
    if (id != 2) {
        return false;
    }
    static const node_host_call call = { .service = "owned.failure", .invoke = fail };
    static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
    *out = (script_lower_host){ .call = &call, .access = &access };
    return true;
}
static void cleanup()
{
    execution_inspector_free(inspector);
    inspector = NULL;
    authoring_source_free(source);
    source = NULL;
    if (registry) {
        source_registry_free(registry);
    }
    registry = NULL;
    if (world) {
        ecs_world_free(world);
    }
    world = NULL;
}
static bool prepare(int kind, behavior_backend backend, bool fails, bool replace)
{
    const char *const names[] = { "int", "string", "name", "ManagedRecord" };
    script_catalog_type types[2] = { 0 };
    if (kind == 3) {
        const char *const reflected[] = { "ManagedInner", "ManagedRecord" };
        for (int i = 0; i < 2; i++) {
            const type_desc *type = type_catalog_find(reflected[i]);
            REQUIRE(type);
            types[i] = (script_catalog_type){ .id = 701 + i,
                                              .name = reflected[i],
                                              .type = { .storage = PIN_STRUCT,
                                                        .nominal = 701 + i },
                                              .size = type->size,
                                              .layout = type_layout_hash(type) };
        }
    }
    script_catalog catalog = { .capabilities = capabilities,
                               .capability_count = 2,
                               .types = types,
                               .type_count = kind == 3 ? 2 : 0 };
    script_lower_bindings bindings = { .host = host };
    char text[512];
    snprintf(
        text,
        sizeof text,
        "Owned :: behavior { source:%s[]=[]; saved:%s[]=[]; final:%s[]=[]; "
        "runs:int=0; "
        "on Tick :: () { captured:=source; saved=captured; final=saved; "
        "runs+=1; %s "
        "} }",
        names[kind],
        names[kind],
        names[kind],
        fails ? "Fail();" : ""
    );
    source = authoring_source_prepare_document("Owned.chibi", "Owned.chibi", text, &catalog, &bindings, backend);
    if (source && !source->valid) {
        for (int i = 0; i < source->diagnostic_count; i++) {
            fprintf(stderr, "owned Tick: %s\n", source->diagnostics[i].message);
        }
    }
    REQUIRE(source && source->valid);
    memset(state_ids, 0, sizeof state_ids);
    captured_id = 0;
    for (int i = 0; i < source->analysis->symbol_count; i++) {
        const script_symbol *s = &source->analysis->symbols[i];
        if (s->kind == SCRIPT_LOCAL && !strcmp(s->name, "captured")) {
            captured_id = s->id;
        }
        for (int j = 0; j < 4; j++) {
            if (s->kind == SCRIPT_STATE && !strcmp(s->name, state_names[j])) {
                state_ids[j] = s->id;
            }
        }
    }
    for (int i = 0; i < 4; i++) {
        REQUIRE(state_ids[i]);
    }
    REQUIRE(captured_id);
    service_binding *services = NULL;
    apush(services, ((service_binding){ "owned.failure", &world }));
    int status = replace
                   ? source_replace(registry, "Owned", source->module, false)
                   : source_register(registry, "Owned", source->module, services);
    afree(services);
    REQUIRE(!status);
    authoring_source_free(source);
    source = NULL;
    return true;
}
static execution_instance *instance(ecs_entity owner)
{
    return execution_program_instance(source_program(registry, "Owned"), ecs_entity_to_u64(owner));
}
static bool seed(ecs_entity owner, int kind, int id)
{
    const pin_value *declared = execution_instance_state(instance(owner), state_ids[0]);
    REQUIRE(declared && declared->container == PIN_CONTAINER_ARRAY);
    char labels[3][64];
    whole_managed records[3];
    pin_string descriptors[3];
    pin_value items[3];
    for (int i = 0; i < 3; i++) {
        snprintf(labels[i], sizeof labels[i], "owner %d item %d", id, i);
        records[i] = (whole_managed){ .label = labels[i],
                                      .nested = { .text = labels[i], .weight = .5f },
                                      .count = id + i };
        items[i] = (pin_value){ .type = kind == 0 ? PIN_I32
                                      : kind == 1 ? PIN_STR
                                      : kind == 2 ? PIN_NAME
                                                  : PIN_STRUCT,
                                .type_name = declared->type_name };
        if (!kind) {
            items[i].i = id + i;
        } else if (kind == 3) {
            items[i].ptr = &records[i];
        } else if (kind == 1) {
            descriptors[i] = (pin_string){ labels[i], (uint32_t)strlen(labels[i]) };
            items[i].text = &descriptors[i];
        } else {
            items[i].str = labels[i];
        }
    }
    pin_array array = { .element_type = items[0].type,
                        .type_name = items[0].type_name,
                        .items = items,
                        .count = 3,
                        .capacity = 3 };
    pin_value value = { .type = array.element_type,
                        .type_name = array.type_name,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = &array };
    REQUIRE(!execution_instance_set_state(instance(owner), state_ids[0], value));
    memset(labels, 'x', sizeof labels);
    memset(records, 0, sizeof records);
    return true;
}
static bool owned_values(ecs_entity owner, int kind, int id, int runs)
{
    execution_instance *item = instance(owner);
    REQUIRE(item);
    const pin_value *count = execution_instance_state(item, state_ids[3]);
    REQUIRE(count && count->type == PIN_I32 && count->i == runs);
    const pin_value *saved = execution_instance_state(item, state_ids[1]);
    const pin_value *final = execution_instance_state(item, state_ids[2]);
    REQUIRE(
        saved && final && saved->container == PIN_CONTAINER_ARRAY &&
        final->container == PIN_CONTAINER_ARRAY
    );
    const pin_array *array = final->ptr;
    REQUIRE(array && array->count == 3 && pin_array_equal(saved->ptr, array));
    for (int i = 0; i < 3; i++) {
        char text[64];
        snprintf(text, sizeof text, "owner %d item %d", id, i);
        if (!kind) {
            REQUIRE(pin_array_get(array, i).i == id + i);
        } else if (kind == 3) {
            const whole_managed *record = pin_array_get(array, i).ptr;
            REQUIRE(record && record->count == id + i && !strcmp(record->label, text));
            REQUIRE(!strcmp(record->nested.text, text) && record->nested.weight == .5f);
        } else {
            REQUIRE(!strcmp(pin_text(pin_array_get(array, i)).bytes, text));
        }
    }
    return true;
}
static bool scenario(int kind, behavior_backend backend, bool fails)
{
    world = ecs_world_new();
    registry = source_registry_new(world);
    REQUIRE(prepare(kind, backend, fails, false));
    ecs_entity owners[3];
    for (int i = 0; i < 3; i++) {
        owners[i] = ecs_world_create_entity(world);
    }
    REQUIRE(!ecs_world_apply_commands(world));
    for (int i = 0; i < 3; i++) {
        REQUIRE(!source_attach(registry, owners[i], "Owned", false, 0, 0));
        REQUIRE(seed(owners[i], kind, 100 + i));
    }
    execution_program *program = source_program(registry, "Owned");
    inspector = execution_program_inspector(program);
    REQUIRE(
        inspector &&
        execution_inspector_select_owner(inspector, ecs_entity_to_u64(owners[0]))
    );
    int watch_index = -1;
    execution_program_failure failure_snapshot = { 0 };
    for (int frame = 0; frame < 3; frame++) {
        for (int i = 0; i < 3; i++) {
            int status = frame == 1
                           ? execution_instance_dispatch(
                                 instance(owners[i]),
                                 execution_module_event(
                                     source_attachment_at(registry, 0).module,
                                     "Tick"
                                 ),
                                 NULL,
                                 0,
                                 frame,
                                 .25,
                                 1000
                             )
                           : source_dispatch(registry, owners[i], "Tick", NULL, 0, frame, .25);
            REQUIRE(fails ? status < 0 : status >= 0);
            if (fails) {
                const execution_program_failure *failure = execution_program_error(program);
                REQUIRE(failure && strstr(failure->message, "retained owned Tick failure"));
                REQUIRE(failure->start >= 0 && failure->end >= failure->start);
                REQUIRE(!strcmp(failure->asset_name, "Owned"));
                if (!frame && !i) {
                    failure_snapshot = *failure;
                }
            }
            REQUIRE(owned_values(owners[i], kind, 100 + i, frame + 1));
            if (!frame && !i) {
                for (int n = 0; n < execution_inspector_watch_count(inspector); n++) {
                    const execution_debug_watch *watch = execution_inspector_watch_at(inspector, n);
                    if (watch->owner == ecs_entity_to_u64(owners[0]) &&
                        watch->source.origin.meaning == captured_id &&
                        watch->value.container == PIN_CONTAINER_ARRAY &&
                        watch->value.ptr) {
                        watch_index = n;
                    }
                }
                REQUIRE(watch_index >= 0);
            }
        }
        REQUIRE(!source_pump(registry, frame + .1, .25));
        REQUIRE(!execution_program_error(program));
    }
    if (fails) {
        REQUIRE(
            failure_snapshot.status < 0 &&
            strstr(failure_snapshot.message, "retained owned Tick failure")
        );
        REQUIRE(
            execution_inspector_owner_frame_count(inspector, ecs_entity_to_u64(owners[0])) > 0
        );
    }
    REQUIRE(prepare(kind, backend, fails, true));
    for (int i = 0; i < 3; i++) {
        REQUIRE(owned_values(owners[i], kind, 100 + i, 3));
    }
    REQUIRE(seed(owners[0], kind, 999));
    REQUIRE(owned_values(owners[0], kind, 100, 3));
    const execution_debug_watch *retained = execution_inspector_watch_at(inspector, watch_index);
    REQUIRE(retained && retained->availability == EXECUTION_WATCH_AVAILABLE);
    const pin_array *snapshot = retained->value.ptr;
    REQUIRE(snapshot && snapshot->count == 3);
    if (!kind) {
        REQUIRE(pin_array_get(snapshot, 0).i == 100);
    } else if (kind == 3) {
        const whole_managed *record = pin_array_get(snapshot, 0).ptr;
        REQUIRE(record && record->count == 100 && !strcmp(record->nested.text, "owner 100 item 0"));
    } else {
        REQUIRE(!strcmp(pin_text(pin_array_get(snapshot, 0)).bytes, "owner 100 item 0"));
    }
    cleanup();
    return true;
}
TEST_CASE(source_owned_tick_values_survive_dispatch_and_publication)
{
    for (int kind = 0; kind < 4; kind++) {
        REQUIRE(scenario(kind, BEHAVIOR_BYTECODE, false));
        REQUIRE(scenario(kind, BEHAVIOR_SLJIT, false));
    }
    return true;
}
TEST_CASE(source_owned_tick_failures_survive_sibling_bank_reuse)
{
    for (int kind = 0; kind < 4; kind++) {
        REQUIRE(scenario(kind, BEHAVIOR_BYTECODE, true));
        REQUIRE(scenario(kind, BEHAVIOR_SLJIT, true));
    }
    return true;
}
void test_owned_ticks()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(source_owned_tick_values_survive_dispatch_and_publication);
    RUN_TEST_CASE(source_owned_tick_failures_survive_sibling_bank_reuse);
    pu_clear_setup();
}
