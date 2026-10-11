#include "execution/prepared.h"

#include "execution/log.h"
#include "language/lower.h"
#include "runtime/state_definition.h"
#include "support/array.h"
#include "test/prepared_fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module, *library, *dependency;
static unsigned char *bytes, *again;
static const char *directory;
static uint64_t state_id, target_id;
static native_capabilities *state_native;

static const script_catalog_field field = {
    .id = 811,
    .name = "value",
    .type = { .storage = PIN_I32 },
    .writable = true,
};
static const script_catalog_parameter amount = { .name = "amount",
                                                 .type = { .storage = PIN_I32 } };
static const script_catalog_parameter text = { .name = "InString",
                                               .type = { .storage = PIN_STR } };
static const char *const outcomes[] = { "Then" };
static const script_catalog_capability capabilities[] = {
    { .id = 10, .name = "Begin", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
    { .id = 11, .name = "Tick", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
    { .id = 12,
      .name = "Add",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = &amount,
      .parameter_count = 1,
      .completion_outcome = -1 },
    { .id = 13,
      .name = "PrintString",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &text,
      .parameter_count = 1,
      .effects = SCRIPT_EFFECT_HOST,
      .completion_outcome = -1 },
    { .id = 40,
      .name = "library_next",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .results = &amount,
      .result_count = 1,
      .outcomes = outcomes,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_READ | SCRIPT_EFFECT_WRITE },
};
static const script_catalog_global owner = {
    .id = 30,
    .name = "self",
    .type = { .storage = PIN_HANDLE, .nominal = 700 },
};
static script_catalog_type type;
static script_catalog catalog;
static void cleanup()
{
    free(bytes);
    free(again);
    execution_module_plan_free(plan);
    execution_module_free(module);
    execution_module_free(library);
    execution_module_free(dependency);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    native_capabilities_free(state_native);
    state_native = NULL;
    bytes = again = NULL;
    module = library = dependency = NULL;
    plan = NULL;
    syntax = NULL;
    analysis = NULL;
}
static bool resolve_global(void *, uint64_t id, behavior_function_instruction *out)
{
    if (id != 30) {
        return false;
    }
    *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
    return true;
}
static bool resolve_host(void *, uint64_t id, script_lower_host *out)
{
    if (id == 13) {
        static const node_access access = { .flags = NODE_EFFECT_HOST_SINK };
        *out = (script_lower_host){ .call = &EXECUTION_PrintString, .access = &access };
        return true;
    }
    if (id == 40 && library) {
        for (int i = 0; i < execution_module_function_count(library); i++) {
            execution_module_function_info f = execution_module_function_at(library, i);
            if (f.kind == EXECUTION_FUNCTION_AUTHORED) {
                *out = (script_lower_host){ .function = f.body, .module = library };
                return true;
            }
        }
    }
    return false;
}
static bool compile_source(const char *source)
{
    syntax = script_parse(source);
    analysis = script_analyze(syntax, &catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "prepared semantic: %s\n", analysis->diagnostics[i].message);
    }
    if (analysis->kind == SCRIPT_PROGRAM_BEHAVIOR) {
        for (int i = 0; i < analysis->symbol_count; i++) {
            if (analysis->symbols[i].kind == SCRIPT_STATE &&
                !strcmp(analysis->symbols[i].name, "count")) {
                state_id = analysis->symbols[i].id;
            }
            if (analysis->symbols[i].kind == SCRIPT_STATE &&
                !strcmp(analysis->symbols[i].name, "target")) {
                target_id = analysis->symbols[i].id;
            }
        }
    }
    const script_lower_bindings bindings = { .global = resolve_global,
                                             .host = resolve_host };
    plan = script_lower(analysis, &catalog, &bindings);
    for (int i = 0; i < plan->diagnostic_count; i++) {
        fprintf(stderr, "prepared lowering: %s\n", plan->diagnostics[i].message);
    }
    if (dependency) {
        REQUIRE(plan->import_count == 0);
        execution_module_retain(dependency);
        apush(plan->imports, dependency);
        plan->import_count = acount(plan->imports);
    }
    char error[192];
    module = execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!module) {
        fprintf(stderr, "prepared finish: %s\n", error);
        return false;
    }
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    analysis = NULL;
    syntax = NULL;
    return true;
}
static bool save(const char *name)
{
    char error[192];
    size_t length = 0, second = 0;
    REQUIRE(execution_prepared_write(module, &bytes, &length, error, sizeof error));
    REQUIRE(length > 32 && length <= PREPARED_TEST_MAX_BYTES);
    REQUIRE(execution_prepared_write(module, &again, &second, error, sizeof error));
    REQUIRE(length == second && !memcmp(bytes, again, length));
    char path[PREPARED_TEST_MAX_PATH];
    int n = snprintf(path, sizeof path, "%s/%s", directory, name);
    REQUIRE(n > 0 && n < (int)sizeof path);
    FILE *file = fopen(path, "wb");
    REQUIRE(file);
    bool written = fwrite(bytes, 1, length, file) == length;
    written = fclose(file) == 0 && written;
    REQUIRE(written);
    free(bytes);
    free(again);
    bytes = again = NULL;
    return true;
}
TEST_CASE(prepared_source_generations_write_after_analysis_retirement)
{
    type = (script_catalog_type){ .id = 101,
                                  .name = prepared_counter_type.name,
                                  .type = { .storage = PIN_STRUCT, .nominal = 101 },
                                  .fields = &field,
                                  .field_count = 1,
                                  .size = prepared_counter_type.size,
                                  .layout = type_layout_hash(&prepared_counter_type),
                                  .component_id = prepared_counter_component.id,
                                  .component = true };
    catalog = (script_catalog){ .types = &type,
                                .type_count = 1,
                                .capabilities = capabilities,
                                .capability_count = 5,
                                .globals = &owner,
                                .global_count = 1,
                                .entity_type = 700,
                                .tick_event = 11 };
    REQUIRE(
        compile_source("library Depot { count:int=10; function Next() -> (amount:int) { count+=1; return(count); } }")
    );
    REQUIRE(save("library.prepared"));
    library = module;
    module = NULL;
    REQUIRE(
        compile_source("behavior Persist { count:int=0; on Begin() {count+=1;} on Tick() {PrintString(\"persistent\"); count+=2;} on Add(amount:int) {v := await library_next(); count+=amount+v.amount;} bind self.PersistCounter.value=count; }")
    );
    REQUIRE(execution_module_import_count(module) == 1);
    REQUIRE(save("main.prepared"));
    execution_module_free(module);
    module = NULL;
    execution_module_free(library);
    library = NULL;
    REQUIRE(
        compile_source("library Depot { count:int=10; function Next() -> (amount:int) { count+=2; return(count); } }")
    );
    REQUIRE(save("wrong-library.prepared"));
    char path[PREPARED_TEST_MAX_PATH];
    int n = snprintf(path, sizeof path, "%s/host-abi.txt", directory);
    REQUIRE(n > 0 && n < (int)sizeof path);
    FILE *file = fopen(path, "w");
    REQUIRE(file);
    int written = fprintf(
        file,
        "%llu %llu\n",
        (unsigned long long)script_catalog_capability_abi(&catalog, &capabilities[3]),
        (unsigned long long)state_id
    );
    bool closed = fclose(file) == 0;
    REQUIRE(written > 0 && closed);
    return true;
}
/* These imports are declared through the public producer plan, independently
 * of call lowering. Package validation must inspect the complete import table,
 * including dependencies not reached by a function call. */
TEST_CASE(prepared_external_closure_generations)
{
    script_catalog_capability variant[5];
    memcpy(variant, capabilities, sizeof variant);
    const script_catalog_parameter flag = { .name = "amount",
                                            .type = { .storage = PIN_BOOL } };
    catalog = (script_catalog){ .capabilities = variant, .capability_count = 5 };
    const char *leaves[] = {
        ("behavior Leaf { on Add(amount:int) {} function "
         "Next()->(amount:int){return(1);} }"),
        ("behavior Leaf { on Add(amount:int) {} function "
         "Next()->(amount:int){return(1);} }"),
        ("behavior Leaf { on Add(amount:int) {} function "
         "Next()->(amount:int){return(2);} }"),
        ("behavior EventLeaf { on Add(amount:bool) {} function "
         "Next()->(amount:int){return(1);} }"),
    };
    const char *middle[] = {
        "library Middle { function Next()->(amount:int){return(1);} }",
        "library GoodMiddle { function Next()->(amount:int){return(1);} }",
        "library BadMiddle { function Next()->(amount:int){return(1);} }",
        "library EventMiddle { function Next()->(amount:int){return(1);} }",
    };
    const char *roots[] = {
        "behavior Closure { on Begin {} }",
        "behavior GoodClosure { on Begin {} }",
        "behavior BadClosure { on Begin {} }",
        "behavior EventClosure { on Begin {} }",
    };
    const char *leaf_files[] = { "leaf.prepared",
                                 NULL,
                                 "changed-leaf.prepared",
                                 "event-leaf.prepared" };
    const char *middle_files[] = { "middle.prepared",
                                   "good-middle.prepared",
                                   "bad-middle.prepared",
                                   "event-middle.prepared" };
    const char *root_files[] = { "closure.prepared",
                                 "good-closure.prepared",
                                 "bad-closure.prepared",
                                 "event-closure.prepared" };
    for (int i = 0; i < 4; i++) {
        variant[2].parameters = i == 3 ? &flag : &amount;
        REQUIRE(compile_source(leaves[i]));
        if (leaf_files[i]) {
            REQUIRE(save(leaf_files[i]));
        }
        dependency = module;
        module = NULL;
        REQUIRE(compile_source(middle[i]));
        REQUIRE(execution_module_import_count(module) == 1);
        REQUIRE(save(middle_files[i]));
        execution_module_free(dependency);
        dependency = module;
        module = NULL;
        REQUIRE(compile_source(roots[i]));
        REQUIRE(execution_module_import_count(module) == 1);
        REQUIRE(save(root_files[i]));
        execution_module_free(dependency);
        dependency = NULL;
        execution_module_free(module);
        module = NULL;
    }
    catalog = (script_catalog){ 0 };
    return true;
}

TEST_CASE(prepared_state_action_generations_write_after_catalog_retirement)
{
    const script_catalog base_catalog = {
        .capabilities = capabilities,
        .capability_count = 3,
        .globals = &owner,
        .global_count = 1,
        .entity_type = 700,
        .tick_event = 11,
    };
    const native_capability_pack base = { .catalog = &base_catalog };
    char error[256];
    state_native = native_capabilities_from_definition_packs(
        &base,
        1,
        &NATIVE_STATE_DEFINITION_PACK,
        1,
        error,
        sizeof error
    );
    if (!state_native) {
        fprintf(stderr, "prepared state catalog: %s\n", error);
    }
    REQUIRE(state_native);
    catalog = *native_capabilities_catalog(state_native);
    REQUIRE(compile_source(
        "behavior StatePersist { count:int=0;"
        " action once=do_once(); action flip=flip_flop();"
        " action pending=retriggerable_delay(Duration:3);"
        " action multi=multi_gate(Loop:false,Random:false,\"Start Index\":0);"
        " on Tick(){once();flip();multi();} on Add(amount){pending();}"
        " after once.Completed{count+=100;}"
        " after flip.A{count+=10;} after flip.B{count+=1;}"
        " after multi.Out0{count+=1000;} after multi.Out1{count+=10000;}"
        " after pending.Completed{count+=100000;} }"
    ));
    REQUIRE(state_id);
    native_capabilities_free(state_native);
    state_native = NULL;
    catalog = (script_catalog){ 0 };
    REQUIRE(save("state-actions.prepared"));
    char path[PREPARED_TEST_MAX_PATH];
    int n = snprintf(path, sizeof path, "%s/state-action-id.txt", directory);
    REQUIRE(n > 0 && n < (int)sizeof path);
    FILE *file = fopen(path, "w");
    REQUIRE(file);
    int written = fprintf(file, "%llu\n", (unsigned long long)state_id);
    bool closed = fclose(file) == 0;
    REQUIRE(written > 0 && closed);
    return true;
}

TEST_CASE(prepared_field_observer_generation_preserves_authoritative_schema)
{
    const script_catalog_parameter changed = { .name = "changed",
                                               .type = { .storage = PIN_I32 } };
    const script_catalog_parameter destination = {
        .name = "destination",
        .type = { .storage = PIN_HANDLE, .nominal = 700 },
    };
    const script_catalog_capability events[] = {
        { .id = 80,
          .name = "ValueChanged",
          .kind = SCRIPT_CAPABILITY_EVENT,
          .receiver = 101,
          .component_field = 811,
          .parameters = &changed,
          .parameter_count = 1,
          .completion_outcome = -1 },
        { .id = 81,
          .name = "Retarget",
          .kind = SCRIPT_CAPABILITY_EVENT,
          .parameters = &destination,
          .parameter_count = 1,
          .completion_outcome = -1 },
    };
    type = (script_catalog_type){
        .id = 101,
        .name = prepared_counter_type.name,
        .type = { .storage = PIN_STRUCT, .nominal = 101 },
        .fields = &field,
        .field_count = 1,
        .size = prepared_counter_type.size,
        .layout = type_layout_hash(&prepared_counter_type),
        .component_id = prepared_counter_component.id,
        .component = true,
    };
    catalog = (script_catalog){
        .types = &type,
        .type_count = 1,
        .capabilities = events,
        .capability_count = 2,
        .globals = &owner,
        .global_count = 1,
        .entity_type = 700,
    };
    state_id = target_id = 0;
    REQUIRE(compile_source(
        "behavior FieldWatch { count:int=0; target:entity=0;"
        " on Retarget(destination){target=destination;}"
        " on target.PersistCounter.ValueChanged(changed){count+=changed;} }"
    ));
    REQUIRE(state_id && target_id);
    catalog = (script_catalog){ 0 };
    type = (script_catalog_type){ 0 };
    REQUIRE(save("field-observer.prepared"));
    char path[PREPARED_TEST_MAX_PATH];
    int n = snprintf(path, sizeof path, "%s/field-observer-ids.txt", directory);
    REQUIRE(n > 0 && n < (int)sizeof path);
    FILE *file = fopen(path, "w");
    REQUIRE(file);
    int written = fprintf(file, "%llu %llu\n", (unsigned long long)state_id, (unsigned long long)target_id);
    bool closed = fclose(file) == 0;
    REQUIRE(written > 0 && closed);
    return true;
}

TEST_CASE(prepared_debug_source_points_write_after_analysis_retirement)
{
    catalog = (script_catalog){ .capabilities = capabilities,
                                .capability_count = 3,
                                .globals = &owner,
                                .global_count = 1,
                                .entity_type = 700,
                                .tick_event = 11 };
    state_id = 0;
    REQUIRE(compile_source(
        "behavior Inspect {count:int=0; function "
        "Work()->(Value:int){count+=1;return count;}"
        "on Tick(){last:=Work();count+=last;}}"
    ));
    REQUIRE(state_id);
    catalog = (script_catalog){ 0 };
    REQUIRE(save("source-debug.prepared"));
    char path[PREPARED_TEST_MAX_PATH];
    int n = snprintf(path, sizeof path, "%s/source-debug-state.txt", directory);
    REQUIRE(n > 0 && n < (int)sizeof path);
    FILE *file = fopen(path, "w");
    REQUIRE(file);
    int written = fprintf(file, "%llu\n", (unsigned long long)state_id);
    bool closed = fclose(file) == 0;
    REQUIRE(written > 0 && closed);
    return true;
}

TEST_CASE(prepared_arithmetic_generation_keeps_transient_verifier_storage_separate)
{
    catalog = (script_catalog){ .capabilities = capabilities, .capability_count = 2 };
    char text[8192] = "behavior Arithmetic { count:int=0;"
                      "on Tick { count=count";
    size_t used = strlen(text);
    for (int i = 1; i <= PREPARED_TEST_ARITHMETIC_STEPS; i++) {
        int written = snprintf(text + used, sizeof text - used, "+%d", i);
        REQUIRE(written > 0 && (size_t)written < sizeof text - used);
        used += (size_t)written;
    }
    REQUIRE(used + 4 <= sizeof text);
    memcpy(text + used, ";}}", 4);
    REQUIRE(compile_source(text));
    REQUIRE(save("arithmetic.prepared"));
    char path[PREPARED_TEST_MAX_PATH];
    int n = snprintf(path, sizeof path, "%s/arithmetic-state.txt", directory);
    REQUIRE(n > 0 && n < (int)sizeof path);
    FILE *file = fopen(path, "w");
    REQUIRE(file);
    int written = fprintf(file, "%llu\n", (unsigned long long)state_id);
    bool closed = fclose(file) == 0;
    REQUIRE(written > 0 && closed);
    return true;
}

TEST_CASE(prepared_statement_generation_keeps_sparse_pause_liveness)
{
    catalog = (script_catalog){ .capabilities = capabilities, .capability_count = 2 };
    char text[8192] = "behavior Statements { count:int=0; on Tick {carry := "
                      "count+7;";
    size_t used = strlen(text);
    for (int i = 1; i <= PREPARED_TEST_STATEMENTS; i++) {
        int written = snprintf(text + used, sizeof text - used, "count=count+%d;", i);
        REQUIRE(written > 0 && (size_t)written < sizeof text - used);
        used += (size_t)written;
    }
    const char end[] = "count=count+carry;}}";
    REQUIRE(used + sizeof end <= sizeof text);
    memcpy(text + used, end, sizeof end);
    REQUIRE(compile_source(text));
    REQUIRE(save("statements.prepared"));
    char path[PREPARED_TEST_MAX_PATH];
    int n = snprintf(path, sizeof path, "%s/statements-state.txt", directory);
    REQUIRE(n > 0 && n < (int)sizeof path);
    FILE *file = fopen(path, "w");
    REQUIRE(file);
    int written = fprintf(file, "%llu\n", (unsigned long long)state_id);
    bool closed = fclose(file) == 0;
    REQUIRE(written > 0 && closed);
    return true;
}

void test_language_prepared_write(const char *path)
{
    directory = path;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(prepared_source_generations_write_after_analysis_retirement);
    RUN_TEST_CASE(prepared_external_closure_generations);
    RUN_TEST_CASE(prepared_state_action_generations_write_after_catalog_retirement);
    RUN_TEST_CASE(prepared_field_observer_generation_preserves_authoritative_schema);
    RUN_TEST_CASE(prepared_debug_source_points_write_after_analysis_retirement);
    RUN_TEST_CASE(prepared_arithmetic_generation_keeps_transient_verifier_storage_separate);
    RUN_TEST_CASE(prepared_statement_generation_keeps_sparse_pause_liveness);
    pu_clear_setup();
}
