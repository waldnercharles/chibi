#include "backend.h"
#include "execution/delay.h"
#include "execution/function_builder.h"
#include "execution/status.h"
#include "language/lower.h"
#include "runtime/behaviors/source.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const script_catalog_parameter duration = { .name = "Duration",
                                                   .type = { .storage = PIN_F64 } };
static const char *const completed[] = { "Completed" };
static const script_catalog_parameter shared_result = { .name = "amount",
                                                        .type = { .storage = PIN_F64 } };
static const script_catalog_capability capabilities[] = {
    { .id = 10, .name = "Begin", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
    { .id = 11, .name = "Pulse", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
    { .id = 12, .name = "Wait", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
    { .id = 13,
      .name = "Touch",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .effects = SCRIPT_EFFECT_HOST,
      .completion_outcome = -1 },
    { .id = 14,
      .name = "timer",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .parameters = &duration,
      .parameter_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_SUSPEND },
    { .id = 15,
      .name = "shared_read",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .results = &shared_result,
      .result_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_READ | SCRIPT_EFFECT_WRITE },
};
static const script_catalog_field ghost_field = { .id = 1152,
                                                  .name = "value",
                                                  .type = { .storage = PIN_F64 },
                                                  .writable = true };
static const type_field reflected_field = { .name = "value",
                                            .type = PIN_F64,
                                            .type_name = "f64",
                                            .size = sizeof(double) };
static const type_desc ghost_desc = { .name = "PublicationGhost",
                                      .size = sizeof(double),
                                      .fields = &reflected_field,
                                      .field_count = 1 };
static script_catalog_type ghost_type;
static const script_catalog_global self = { .id = 30,
                                            .name = "self",
                                            .type = { .storage = PIN_HANDLE,
                                                      .nominal = 700 } };
static script_catalog catalog;
static ecs_world *world;
static source_registry *registry;
static execution_module *modules[6];
enum
{
    IMPORT_DIAMOND_MODULES = 32
};
static execution_module *diamond[IMPORT_DIAMOND_MODULES];
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static uint64_t count_ids[2], shared_ids[6], observed_ids[6];
static execution_module *imported;
static int host_calls;

static void cleanup()
{
    if (registry) {
        source_registry_free(registry);
    }
    registry = NULL;
    for (int i = IMPORT_DIAMOND_MODULES - 1; i >= 0; i--) {
        execution_module_free(diamond[i]);
        diamond[i] = NULL;
    }
    for (int i = 0; i < 6; i++) {
        execution_module_free(modules[i]);
        modules[i] = NULL;
    }
    execution_module_plan_free(plan);
    plan = NULL;
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    ecs_world_free(world);
    world = NULL;
    host_calls = 0;
    imported = NULL;
}
static const char *touch(void *, const pin_value *, pin_value *)
{
    host_calls++;
    return NULL;
}
static bool host(void *, uint64_t id, script_lower_host *out)
{
    if (id == 13) {
        static const node_host_call call = { .service = "publication.service",
                                             .invoke = touch };
        static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
        *out = (script_lower_host){ .call = &call, .access = &access };
        return true;
    }
    if (id == 15 && imported) {
        for (int i = 0; i < execution_module_function_count(imported); i++) {
            execution_module_function_info f = execution_module_function_at(imported, i);
            if (f.kind == EXECUTION_FUNCTION_AUTHORED) {
                *out = (script_lower_host){ .module = imported, .function = f.body };
                return true;
            }
        }
    }
    if (id == 14) {
        *out = (script_lower_host){ .command = &EXECUTION_Delay };
        return true;
    }
    return false;
}
static bool global(void *, uint64_t id, behavior_function_instruction *out)
{
    if (id != 30) {
        return false;
    }
    *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
    return true;
}
static bool compile(int index, const char *text, behavior_backend backend)
{
    syntax = script_parse(text);
    analysis = script_analyze(syntax, &catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "publication semantic: %s\n", analysis->diagnostics[i].message);
    }
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_SHARED_STATE &&
            !strcmp(analysis->symbols[i].name, "total")) {
            shared_ids[index] = analysis->symbols[i].id;
        }
        if (analysis->symbols[i].kind == SCRIPT_SHARED_STATE &&
            !strcmp(analysis->symbols[i].name, "observed")) {
            observed_ids[index] = analysis->symbols[i].id;
        }
    }
    if (index < 2) {
        for (int i = 0; i < analysis->symbol_count; i++) {
            if (analysis->symbols[i].kind == SCRIPT_STATE &&
                !strcmp(analysis->symbols[i].name, "count")) {
                count_ids[index] = analysis->symbols[i].id;
            }
        }
    }
    const script_lower_bindings bindings = { .host = host, .global = global };
    plan = script_lower(analysis, &catalog, &bindings);
    for (int i = 0; i < plan->diagnostic_count; i++) {
        fprintf(stderr, "publication lower: %s\n", plan->diagnostics[i].message);
    }
    char error[192];
    modules[index] = execution_module_finish(&plan, backend, error, sizeof error);
    if (!modules[index]) {
        fprintf(stderr, "publication finish: %s\n", error);
        return false;
    }
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    return true;
}
static bool count_is(execution_program *program, ecs_entity entity, uint64_t id, double expected)
{
    execution_instance *instance = execution_program_instance(program, ecs_entity_to_u64(entity));
    const pin_value *v = instance ? execution_instance_state(instance, id) : NULL;
    return v && v->type == PIN_F64 && v->d == expected;
}
static bool setup(behavior_backend backend)
{
    ghost_type = (script_catalog_type){ .id = 1150,
                                        .name = ghost_desc.name,
                                        .type = { .storage = PIN_STRUCT, .nominal = 1150 },
                                        .fields = &ghost_field,
                                        .field_count = 1,
                                        .size = ghost_desc.size,
                                        .layout = type_layout_hash(&ghost_desc),
                                        .component_id = 1151,
                                        .component = true };
    catalog = (script_catalog){ .capabilities = capabilities,
                                .capability_count = 6,
                                .globals = &self,
                                .global_count = 1,
                                .entity_type = 700,
                                .types = &ghost_type,
                                .type_count = 1 };
    REQUIRE(
        compile(0, "Alpha :: behavior { count:f64=0; on Begin :: () {count+=1;} on Pulse :: () {count+=1;} on Wait :: () {await timer(10);count+=100;} }", backend)
    );
    REQUIRE(
        compile(1, "Beta :: behavior { count:f64=5; on Begin :: () {count+=2;} on Pulse :: () {count+=2;} }", backend)
    );
    REQUIRE(
        compile(2, "Alpha :: behavior { count:f64=999; on Begin :: () {count+=100;} on Pulse :: () {count+=10;} on Wait :: () {await timer(10);count+=1000;} }", backend)
    );
    REQUIRE(
        compile(3, "Beta :: behavior { count:f64=555; on Begin :: () {count+=200;} on Pulse :: () {count+=20;} }", backend)
    );
    REQUIRE(compile(4, "Service :: behavior {on Begin :: () {Touch();}}", backend));
    REQUIRE(compile(5, "Schema :: behavior {bind self.PublicationGhost.value=1;}", backend));
    world = ecs_world_new();
    registry = source_registry_new(world);
    REQUIRE(source_register(registry, "Alpha", modules[0], NULL) == 0);
    REQUIRE(source_register(registry, "Beta", modules[1], NULL) == 0);
    execution_module_free(modules[0]);
    execution_module_free(modules[1]);
    modules[0] = modules[1] = NULL;
    return true;
}
TEST_CASE(publication_late_failure_preserves_running_generations)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(setup(backends[b]));
        ecs_entity entity = ecs_world_create_entity(world);
        REQUIRE(source_attach(registry, entity, "Alpha", false, 0, 0) == FLOW_OK);
        execution_program *original = source_program(registry, "Alpha");
        execution_instance *instance = execution_program_instance(original, ecs_entity_to_u64(entity));
        REQUIRE(instance && count_is(original, entity, count_ids[0], 1));
        REQUIRE(source_dispatch(registry, entity, "Wait", NULL, 0, 0, 0) == 1);
        REQUIRE(execution_instance_pending(instance) == 1);
        for (int failure = 4; failure < 6; failure++) {
            const source_publication entries[] = {
                { .name = "Alpha", .previous_name = "Alpha", .module = modules[2] },
                { .name = "Rejected", .module = modules[failure] },
            };
            char error[192];
            REQUIRE(
                source_publish(registry, entries, 2, NULL, error, sizeof error) != FLOW_OK &&
                error[0]
            );
            REQUIRE(
                source_program(registry, "Alpha") == original &&
                !source_registered(registry, "Rejected")
            );
            REQUIRE(execution_program_instance(original, ecs_entity_to_u64(entity)) == instance);
            REQUIRE(
                count_is(original, entity, count_ids[0], 1) &&
                execution_instance_pending(instance) == 1 && host_calls == 0
            );
        }
        REQUIRE(source_dispatch(registry, entity, "Pulse", NULL, 0, 1, 0) == 2);
        REQUIRE(count_is(original, entity, count_ids[0], 2));
        REQUIRE(source_step(registry, 11, 0) == FLOW_OK);
        REQUIRE(
            count_is(original, entity, count_ids[0], 102) &&
            execution_instance_pending(instance) == 0
        );
    }
    return true;
}
/* One entity on two backends. A publication that changes a kept state's type
 * is refused with a restart hint and keeps the running program and its value;
 * the same publication as a restart applies and resets it. */
TEST_CASE(publication_changed_state_type_needs_restart)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(setup(backends[b]));
        execution_module_free(modules[4]);
        REQUIRE(
            compile(4, "Alpha :: behavior { count: int = 0; on Pulse :: () { count += 1; } }", backends[b])
        );
        ecs_entity entity = ecs_world_create_entity(world);
        REQUIRE(source_attach(registry, entity, "Alpha", false, 0, 0) == FLOW_OK);
        execution_program *alpha = source_program(registry, "Alpha");
        source_publication change = { .name = "Alpha",
                                      .previous_name = "Alpha",
                                      .module = modules[4] };
        char error[192];
        REQUIRE(source_publish(registry, &change, 1, NULL, error, sizeof error) != FLOW_OK);
        REQUIRE(strstr(error, "Source Alpha cannot keep its state; restart it"));
        REQUIRE(
            source_program(registry, "Alpha") == alpha &&
            count_is(alpha, entity, count_ids[0], 1)
        );
        change.restart = true;
        REQUIRE(source_publish(registry, &change, 1, NULL, error, sizeof error) == FLOW_OK);
        const pin_value *count = execution_instance_state(
            execution_program_instance(alpha, ecs_entity_to_u64(entity)),
            count_ids[0]
        );
        REQUIRE(count && count->type != PIN_F64);
    }
    return true;
}
TEST_CASE(publication_name_swap_preserves_state_and_removal_cancels)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(setup(backends[b]));
        ecs_entity entity = ecs_world_create_entity(world);
        REQUIRE(source_attach(registry, entity, "Alpha", false, 0, 0) == FLOW_OK);
        REQUIRE(source_attach(registry, entity, "Beta", false, 0, 0) == FLOW_OK);
        execution_program *alpha = source_program(registry, "Alpha"),
                          *beta = source_program(registry, "Beta");
        const source_publication swap[] = {
            { .name = "Beta", .previous_name = "Alpha", .module = modules[2] },
            { .name = "Alpha", .previous_name = "Beta", .module = modules[3] },
        };
        char error[192];
        REQUIRE(source_publish(registry, swap, 2, NULL, error, sizeof error) == FLOW_OK);
        execution_module_free(modules[2]);
        execution_module_free(modules[3]);
        modules[2] = modules[3] = NULL;
        REQUIRE(
            source_program(registry, "Beta") == alpha &&
            source_program(registry, "Alpha") == beta
        );
        REQUIRE(
            count_is(alpha, entity, count_ids[0], 1) &&
            count_is(beta, entity, count_ids[1], 7)
        );
        REQUIRE(source_dispatch(registry, entity, "Pulse", NULL, 0, 1, 0) == 2);
        REQUIRE(
            count_is(alpha, entity, count_ids[0], 11) &&
            count_is(beta, entity, count_ids[1], 27)
        );
        REQUIRE(source_dispatch(registry, entity, "Wait", NULL, 0, 1, 0) == 1);
        REQUIRE(
            execution_instance_pending(
                execution_program_instance(alpha, ecs_entity_to_u64(entity))
            ) == 1
        );
        const source_publication remove = { .previous_name = "Beta" };
        REQUIRE(source_publish(registry, &remove, 1, NULL, error, sizeof error) == FLOW_OK);
        REQUIRE(!source_registered(registry, "Beta") && source_program(registry, "Alpha") == beta);
        REQUIRE(
            source_step(registry, 20, 0) == FLOW_OK &&
            count_is(beta, entity, count_ids[1], 27)
        );
        REQUIRE(source_detach_entity(registry, entity) == 1);
        REQUIRE(execution_program_instance(beta, ecs_entity_to_u64(entity)) == NULL);
    }
    return true;
}
TEST_CASE(publication_preserves_detached_root_and_imported_shared_state)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        cleanup();
        catalog = (script_catalog){ .capabilities = capabilities,
                                    .capability_count = 6 };
        REQUIRE(compile(
            0,
            "SharedChild :: system() { total:retain f64=0; Next :: "
            "()->(amount:f64){total+=2;return(total);} }",
            backends[b]
        ));
        imported = modules[0];
        REQUIRE(compile(
            1,
            "SharedRoot :: system() { total:retain f64=0; observed: "
            "retain f64=0; on Pulse :: () {total+=1; v:=await "
            "shared_read(); "
            "observed=v.amount;} }",
            backends[b]
        ));
        REQUIRE(execution_module_import_count(modules[1]) == 1);
        REQUIRE(compile(
            2,
            "SharedChild :: system() { total:retain f64=500; Next "
            ":: ()->(amount:f64){total+=20;return(total);} }",
            backends[b]
        ));
        imported = modules[2];
        REQUIRE(compile(
            3,
            "SharedRoot :: system() { total:retain f64=999; "
            "observed: retain f64=999; on Pulse :: () {total+=10; v:=await "
            "shared_read(); observed=v.amount;} }",
            backends[b]
        ));
        imported = NULL;
        REQUIRE(shared_ids[0] && shared_ids[1] && shared_ids[0] != shared_ids[1]);
        REQUIRE(shared_ids[0] == shared_ids[2] && shared_ids[1] == shared_ids[3]);
        REQUIRE(observed_ids[1] && observed_ids[1] == observed_ids[3]);
        world = ecs_world_new();
        registry = source_registry_new(world);
        REQUIRE(source_register(registry, "Shared", modules[1], NULL) == 0);
        execution_module_free(modules[0]);
        execution_module_free(modules[1]);
        modules[0] = modules[1] = NULL;
        execution_program *program = source_program(registry, "Shared");
        ecs_entity owners[] = { ecs_world_create_entity(world),
                                ecs_world_create_entity(world) };
        for (int i = 0; i < 2; i++) {
            REQUIRE(source_attach(registry, owners[i], "Shared", true, 0, 0) == 0);
        }
        /* system() reconciles every live owner at dispatch/pump boundaries.
         * Establish both explicit attachments before that reconciliation. */
        for (int i = 0; i < 2; i++) {
            REQUIRE(source_dispatch(registry, owners[i], "Pulse", NULL, 0, i, 0) == 1);
            REQUIRE(source_pump(registry, i, 0) == 0);
        }
        REQUIRE(count_is(program, owners[0], shared_ids[1], 2));
        REQUIRE(count_is(program, owners[1], shared_ids[1], 2));
        REQUIRE(count_is(program, owners[0], observed_ids[1], 4));
        REQUIRE(count_is(program, owners[1], observed_ids[1], 4));
        for (int i = 0; i < 2; i++) {
            REQUIRE(source_detach_entity(registry, owners[i]) == 1);
            REQUIRE(execution_program_instance(program, ecs_entity_to_u64(owners[i])) == NULL);
        }
        const source_publication replace = { .name = "Shared",
                                             .previous_name = "Shared",
                                             .module = modules[3] };
        char error[192];
        REQUIRE(source_publish(registry, &replace, 1, NULL, error, sizeof error) == 0);
        REQUIRE(source_program(registry, "Shared") == program);
        execution_module_free(modules[2]);
        execution_module_free(modules[3]);
        modules[2] = modules[3] = NULL;
        REQUIRE(source_attach(registry, owners[0], "Shared", true, 2, 0) == 0);
        REQUIRE(count_is(program, owners[0], shared_ids[1], 2));
        REQUIRE(count_is(program, owners[0], observed_ids[1], 4));
        REQUIRE(source_dispatch(registry, owners[0], "Pulse", NULL, 0, 2, 0) == 1);
        REQUIRE(source_pump(registry, 2, 0) == 0);
        REQUIRE(count_is(program, owners[0], shared_ids[1], 12));
        REQUIRE(count_is(program, owners[0], observed_ids[1], 24));
    }
    return true;
}
TEST_CASE(publication_profile_counts_actual_execution_and_retains_origins)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        cleanup();
        catalog = (script_catalog){ .capabilities = capabilities,
                                    .capability_count = 6,
                                    .globals = &self,
                                    .global_count = 1,
                                    .entity_type = 700 };
        REQUIRE(compile(
            0,
            "Alpha :: behavior { count:f64=0; on Begin :: () {count+=1;} on "
            "Pulse :: () "
            "{count+=1;} on Wait :: () {await timer(10);count+=100;} }",
            backends[b]
        ));
        uint64_t module_id = execution_module_id(modules[0]);
        world = ecs_world_new();
        registry = source_registry_new(world);
        REQUIRE(source_register(registry, "Alpha", modules[0], NULL) == FLOW_OK);
        execution_module_free(modules[0]);
        modules[0] = NULL;
        REQUIRE(source_profile_count(registry) == 0);
        source_profile_enable(registry, true);
        ecs_entity entity = ecs_world_create_entity(world);
        REQUIRE(source_attach(registry, entity, "Alpha", false, 0, 0) == FLOW_OK);
        execution_program *program = source_program(registry, "Alpha");
        REQUIRE(count_is(program, entity, count_ids[0], 1));
        REQUIRE(source_dispatch(registry, entity, "Pulse", NULL, 0, 0, 0) == 1);
        REQUIRE(source_dispatch(registry, entity, "Wait", NULL, 0, 0, 0) == 1);
        REQUIRE(source_pump(registry, 1, 0) == FLOW_OK);
        REQUIRE(count_is(program, entity, count_ids[0], 2));
        uint64_t entries = 0, resumes = 0;
        int rows = source_profile_count(registry);
        REQUIRE(rows == 3 && source_profile_dropped(registry) == 0);
        for (int i = 0; i < rows; i++) {
            const source_profile_row *row = source_profile_at(registry, i);
            entries += row->entries;
            resumes += row->resumes;
            REQUIRE(!row->internal && row->function_id && row->module_id == module_id);
            REQUIRE(row->owner == ecs_entity_to_u64(entity) && !strcmp(row->name, "Alpha"));
            REQUIRE(row->source && row->start >= 0 && row->end >= row->start);
            REQUIRE(
                !row->cpu_available ||
                (isfinite(row->cpu_seconds) && row->cpu_seconds >= 0)
            );
        }
        REQUIRE(entries == 3 && resumes == 0);
        REQUIRE(source_pump(registry, 10, 0) == FLOW_OK);
        REQUIRE(count_is(program, entity, count_ids[0], 102));
        entries = resumes = 0;
        for (int i = 0; i < rows; i++) {
            const source_profile_row *row = source_profile_at(registry, i);
            entries += row->entries;
            resumes += row->resumes;
        }
        REQUIRE(entries == 3 && resumes == 1);
        source_profile_enable(registry, false);
        REQUIRE(source_dispatch(registry, entity, "Pulse", NULL, 0, 11, 0) == 1);
        REQUIRE(count_is(program, entity, count_ids[0], 103));
        const source_profile_row *retained = source_profile_at(registry, 0);
        REQUIRE(source_remove(registry, "Alpha") == 0);
        REQUIRE(source_profile_count(registry) == rows);
        REQUIRE(
            retained->module_id == module_id &&
            !strcmp(retained->name, "Alpha") && retained->source
        );
        entries = resumes = 0;
        for (int i = 0; i < rows; i++) {
            const source_profile_row *row = source_profile_at(registry, i);
            entries += row->entries;
            resumes += row->resumes;
        }
        REQUIRE(entries == 3 && resumes == 1);
        source_profile_reset(registry);
        REQUIRE(source_profile_count(registry) == 0 && source_profile_dropped(registry) == 0);
    }
    return true;
}
TEST_CASE(publication_shared_import_dag_preserves_transitive_task_rejection)
{
    /* Each generation imports the prior two. There are only 32 modules, not
     * one module for every path through the shared dependency graph. */
    for (int tasks = 0; tasks < 2; tasks++) {
        for (int i = 0; i < IMPORT_DIAMOND_MODULES; i++) {
            plan = execution_module_plan_new();
            plan->valid = true;
            plan->id = (uint64_t)i + 1;
            for (int j = i - 2; j < i; j++) {
                if (j >= 0) {
                    execution_module_retain(diamond[j]);
                    apush(plan->imports, diamond[j]);
                }
            }
            plan->import_count = acount(plan->imports);
            if (!i && tasks) {
                apush(
                    plan->states,
                    ((execution_module_state){ .id = 1,
                                               .initial = { .type = PIN_TASK },
                                               .contains_tasks = true })
                );
                plan->state_count = 1;
            }
            if (i == IMPORT_DIAMOND_MODULES - 1) {
                behavior_function_builder *builder = behavior_function_builder_new();
                behavior_function_builder_emit(
                    builder,
                    (behavior_function_instruction){ .op = BF_RETURN }
                );
                behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
                char error[192];
                behavior_function *body = behavior_function_builder_finish(builder, error, sizeof error);
                behavior_function_builder_free(builder);
                REQUIRE(body);
                apush(
                    plan->functions,
                    ((execution_module_function){ .id = 1,
                                                  .kind = EXECUTION_FUNCTION_SYSTEM,
                                                  .body = body })
                );
                plan->function_count = 1;
            }
            char error[192] = "";
            diamond[i] = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
            if (tasks && i == IMPORT_DIAMOND_MODULES - 1) {
                REQUIRE(!diamond[i] && strstr(error, "cannot contain tasks"));
            } else {
                REQUIRE(diamond[i]);
            }
        }
        cleanup();
    }
    return true;
}

void test_language_publication()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(publication_late_failure_preserves_running_generations);
    RUN_TEST_CASE(publication_changed_state_type_needs_restart);
    RUN_TEST_CASE(publication_name_swap_preserves_state_and_removal_cancels);
    RUN_TEST_CASE(publication_preserves_detached_root_and_imported_shared_state);
    RUN_TEST_CASE(publication_profile_counts_actual_execution_and_retains_origins);
    RUN_TEST_CASE(publication_shared_import_dag_preserves_transitive_task_rejection);
    pu_clear_setup();
}
