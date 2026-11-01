#include "execution/module_builder.h"
#include "execution/status.h"
#include "support/array.h"
#include "test/support/reflected_types.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static execution_module_plan *plan;
static execution_module *module;
static execution_program *program;
static arena snapshots;
static void cleanup()
{
    execution_program_free(program);
    execution_module_free(module);
    execution_module_plan_free(plan);
    arena_free(&snapshots);
    program = NULL;
    module = NULL;
    plan = NULL;
}
static bool generation(behavior_backend backend, bool reverse)
{
    plan = execution_module_plan_new();
    plan->id = 9001;
    plan->valid = true;
    const execution_module_state states[] = {
        { .id = 11, .initial = { .type = PIN_STRUCT, .type_name = "WholeRecord" } },
        { .id = 12, .initial = { .type = PIN_STRUCT, .type_name = "ManagedRecord" } }
    };
    for (int i = 0; i < 2; i++) {
        apush(plan->states, states[reverse ? 1 - i : i]);
    }
    plan->state_count = 2;
    const type_desc *types[] = { &WHOLE_RECORD_TYPE, &WHOLE_MANAGED_TYPE };
    for (int i = 0; i < 2; i++) {
        apush(
            plan->types,
            ((execution_type_requirement){ .id = (uint64_t)(100 + i),
                                           .layout = type_layout_hash(types[i]),
                                           .storage = PIN_STRUCT,
                                           .name = types[i]->name,
                                           .size = types[i]->size })
        );
    }
    plan->type_count = 2;
    char error[192];
    module = execution_module_finish(&plan, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "persistent store publication: %s\n", error);
    }
    return module != NULL;
}
static bool record_equal(const pin_value *value, const whole_record *expected)
{
    if (!value || value->type != PIN_STRUCT || !value->ptr ||
        !value->type_name || strcmp(value->type_name, "WholeRecord")) {
        return false;
    }
    const whole_record *v = value->ptr;
    return v->tag == expected->tag && v->stamp == expected->stamp &&
           v->active == expected->active && v->position.x == expected->position.x &&
           signbit(v->position.x) == signbit(expected->position.x) &&
           v->position.y == expected->position.y && v->weight == expected->weight &&
           signbit(v->weight) == signbit(expected->weight);
}
static bool managed_equal(const pin_value *value)
{
    if (!value || value->type != PIN_STRUCT || !value->ptr ||
        !value->type_name || strcmp(value->type_name, "ManagedRecord")) {
        return false;
    }
    const whole_managed *v = value->ptr;
    return !strcmp(v->label, "owned label") &&
           !strcmp(v->nested.text, "owned nested") &&
           v->nested.weight == 2.75f && v->count == 7;
}
TEST_CASE(persistent_states_own_payloads_and_survive_reordered_generations)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, BEHAVIOR_SLJIT };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(generation(backends[b], false));
        program = execution_program_new(module, NULL);
        REQUIRE(program);
        execution_module_free(module);
        module = NULL;
        whole_record record = { .tag = 251,
                                .position = { -0.f, 4 },
                                .weight = -0.,
                                .active = true,
                                .stamp = INT64_MIN + 7 };
        whole_record expected = record;
        char nominal[] = "WholeRecord", label[] = "owned label",
             nested[] = "owned nested";
        whole_managed managed = { .label = label, .nested = { nested, 2.75f }, .count = 7 };
        REQUIRE(
            execution_program_set_state(
                program,
                11,
                (pin_value){ .type = PIN_STRUCT, .type_name = nominal, .ptr = &record }
            ) == FLOW_OK
        );
        REQUIRE(
            execution_program_set_state(
                program,
                12,
                (pin_value){ .type = PIN_STRUCT, .type_name = "ManagedRecord", .ptr = &managed }
            ) == FLOW_OK
        );
        record.position.x = 11;
        nominal[0] = 'X';
        label[0] = 'X';
        nested[0] = 'X';
        REQUIRE(record_equal(execution_program_state(program, 11), &expected));
        REQUIRE(managed_equal(execution_program_state(program, 12)));
        pin_value retained_record, retained_managed;
        REQUIRE(
            !pin_value_snapshot_to_arena(&snapshots, &retained_record, execution_program_state(program, 11))
        );
        REQUIRE(!pin_value_snapshot_to_arena(
            &snapshots,
            &retained_managed,
            execution_program_state(program, 12)
        ));
        REQUIRE(
            execution_program_set_state(
                program,
                11,
                (pin_value){ .type = PIN_STRUCT, .type_name = "WholeRecord" }
            ) == FLOW_OK
        );
        REQUIRE(!execution_program_state(program, 11)->ptr);
        REQUIRE(record_equal(&retained_record, &expected));
        REQUIRE(execution_program_set_state(program, 11, retained_record) == FLOW_OK);
        REQUIRE(generation(backends[b], true));
        REQUIRE(execution_program_replace(program, module, false) == FLOW_OK);
        execution_module_free(module);
        module = NULL;
        REQUIRE(record_equal(execution_program_state(program, 11), &expected));
        REQUIRE(managed_equal(execution_program_state(program, 12)));
        REQUIRE(
            execution_program_set_state(program, 11, (pin_value){ .type = PIN_I32, .i = 99 }) ==
            FLOW_ERR_ARGUMENT
        );
        REQUIRE(record_equal(execution_program_state(program, 11), &expected));
        REQUIRE(generation(backends[b], false));
        REQUIRE(execution_program_replace(program, module, true) == FLOW_OK);
        execution_module_free(module);
        module = NULL;
        REQUIRE(
            !execution_program_state(program, 11)->ptr &&
            !execution_program_state(program, 12)->ptr
        );
        execution_program_free(program);
        program = NULL;
        REQUIRE(record_equal(&retained_record, &expected));
        REQUIRE(managed_equal(&retained_managed));
    }
    return true;
}
void test_persistent_stores()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(persistent_states_own_payloads_and_survive_reordered_generations);
}
