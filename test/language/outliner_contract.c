#include "authoring/native_bindings.h"
#include "authoring/source_collection.h"
#include "authoring/studio_outliner_definition.h"
#include "backend.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/module_internal.h"
#include "execution/prepared_producer.h"
#include "host/native_catalog.h"
#include "runtime/arrays.h"
#include "runtime/text_format.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/catalog.h"
#include "types/record.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Bounded preparation-only fixture: two tiny modules, eight roots, four scalar
 * states and four KDL declarations. Two positive preparations per backend;
 * negative cases alter and restore private test-owned finalized metadata.
 * No world, execution, renderer, network, sleeps or generated executable. */
static const char
    LIBRARY[] = "OutlinerRowPolicies :: library { "
                "Row :: struct { key:string; label:string; icon:string; "
                "prototype:string; drop:string; "
                "depth:int; behaviors:int; active:bool; marked:bool; "
                "hidden:bool; locked:bool; renaming:bool; } "
                "Projection :: struct { rootLabel:string; rootIcon:string; "
                "rootParent:bool; rootDepth:int; "
                "rootOpen:bool; rootShown:bool; count:int; total:int; "
                "filtered:bool; notice:string; revealRow:int; } "
                "Project :: (scene:StudioOutlinerScene)->Projection { return { "
                "rootLabel:scene.scenePath, rootIcon:\"icon\", "
                "rootParent:true, rootDepth:0, "
                "rootOpen:true, rootShown:true, count:0, total:1, "
                "filtered:false, notice:scene.filter, revealRow:-1 }; } "
                "}";
static const char BEHAVIOR
    [] = "OutlinerRows :: behavior { "
         "First:int=0; Count:int=0; Collapsed:bool=false; ShownOpen:bool=true; "
         "on Begin {} "
         "on ShowRows { state.First=event.First; state.Count=event.Count; } "
         "on Refresh { facts := capture_outliner(); projection := "
         "OutlinerRowPolicies.Project(facts.Scene); "
         "rows:OutlinerRowPolicies.Row[]=[]; "
         "publish_outliner(projection,rows); } "
         "on FoldRoot { Collapsed=true; } on UnfoldRoot { Collapsed=false; } "
         "on ToggleRoot { Collapsed=!Collapsed; } "
         "on RootClicked { Collapsed=!event.Open; } "
         "on RevealObject { Collapsed=false; } "
         "}";
static char root[512];
static native_capabilities *native;
static authoring_sources *sources;
static execution_module *producer;
static bool catalog_set;

static void clear_sources()
{
    authoring_sources_free(sources);
    sources = NULL;
    producer = NULL;
}
static void cleanup()
{
    clear_sources();
    native_capabilities_free(native);
    native = NULL;
    if (catalog_set) {
        type_catalog_set_project(NULL);
        catalog_set = false;
    }
    if (root[0]) {
        assert(!rmdir(root));
        root[0] = 0;
    }
}
static bool prepare(behavior_backend backend, const char *library)
{
    clear_sources();
    char error[256];
    if (!native) {
        type_catalog_set_project(&STUDIO_OUTLINER_TYPE_PACK);
        catalog_set = true;
        native_type_catalog *types = native_type_catalog_new(
            STUDIO_OUTLINER_TYPE_DEFINITIONS,
            3,
            error,
            sizeof error
        );
        REQUIRE(types);
        uint64_t entity_id = script_catalog_id("type", "ecs.entity");
        script_catalog_type entity = {
            .id = entity_id,
            .name = "ecs.entity",
            .type = { .storage = PIN_HANDLE, .nominal = entity_id },
            .size = sizeof(uint64_t),
        };
        script_catalog base = { .types = &entity, .type_count = 1, .entity_type = entity_id };
        const native_capability_pack type_packs[] = {
            native_type_catalog_pack(types),
            { .catalog = &base },
        };
        const native_definition_pack packs[] = {
            STUDIO_OUTLINER_DEFINITION_PACK,
            NATIVE_ARRAY_DEFINITION_PACK,
            NATIVE_TEXT_DEFINITION_PACK,
        };
        /* The language driver has only its selected runtime layouts. Use the
         * producer's packs and authoritative events, without unrelated engine
         * defaults that require the full application's type registry. */
        native = host_native_catalog_new(
            type_packs,
            NATIVE_COUNT(type_packs),
            packs,
            NATIVE_COUNT(packs),
            error,
            sizeof error
        );
        native_type_catalog_free(types);
        if (!native) {
            fprintf(stderr, "Outliner contract catalog: %s\n", error);
        }
        REQUIRE(native);
    }
    if (!root[0]) {
        const char *temporary = getenv("TMPDIR");
        snprintf(root, sizeof root, "%s/chibi-outliner-contract-XXXXXX", temporary ? temporary : "/tmp");
        REQUIRE(mkdtemp(root));
    }
    static const char *const paths[] = {
        "libraries/OutlinerRowPolicies.chibi",
        "behaviors/OutlinerRows.chibi",
        "events/ShowRows.kdl",
        "events/Refresh.kdl",
        "events/RootClicked.kdl",
        "events/RevealObject.kdl",
        "ui/Outliner.rml",
    };
    const char *texts[] = {
        library,
        BEHAVIOR,
        "name \"ShowRows\"\n(array)variables { - name=\"First\" type=\"int\"; "
        "- name=\"Count\" type=\"int\"; }\n",
        "name \"Refresh\"\n",
        "name \"RootClicked\"\n(array)variables { - name=\"Open\" "
        "type=\"bool\"; }\n",
        "name \"RevealObject\"\n(array)variables { - name=\"Object\" "
        "type=\"const char *\"; }\n",
        "<rml><body><div data-event-click=\"emit('FoldRoot'); "
        "emit('UnfoldRoot'); emit('ToggleRoot')\"></div></body></rml>",
    };
    char full[7][640];
    source_catalog_edit edits[7] = { 0 };
    for (int i = 0; i < 7; i++) {
        snprintf(full[i], sizeof full[i], "%s/%s", root, paths[i]);
        edits[i] = (source_catalog_edit){ .path = full[i], .text = texts[i] };
    }
    source_catalog *snapshot = source_catalog_replaced(root, native_capabilities_catalog(native), edits, 7);
    script_lower_bindings bindings = authoring_native_bindings(native);
    sources = authoring_sources_prepare_catalog(snapshot, &bindings, backend);
    for (int i = 0; i < authoring_sources_catalog_diagnostic_count(sources); i++) {
        fprintf(
            stderr,
            "Outliner contract declaration: %s\n",
            authoring_sources_catalog_diagnostic_at(sources, i)->message
        );
    }
    for (int i = 0; i < authoring_sources_count(sources); i++) {
        const authoring_source *source = authoring_sources_at(sources, i);
        for (int j = 0; j < source->diagnostic_count; j++) {
            fprintf(
                stderr,
                "Outliner contract source: %s\n",
                source->diagnostics[j].message
            );
        }
        if (source->analysis && source->analysis->name &&
            !strcmp(source->analysis->name, "OutlinerRows")) {
            producer = source->module;
        }
    }
    REQUIRE(authoring_sources_valid(sources) && producer);
    return true;
}
static bool valid()
{
    char error[256];
    bool accepted = studio_outliner_module_valid(producer, error, sizeof error);
    if (!accepted) {
        fprintf(stderr, "Outliner contract proof: %s\n", error);
    }
    return accepted;
}
static bool rejected()
{
    char error[256] = { 0 };
    REQUIRE(!studio_outliner_module_valid(producer, error, sizeof error));
    REQUIRE(error[0]);
    return true;
}
static const char *substituted(
    const behavior_function_context *context,
    void *service,
    const pin_value *arguments,
    pin_value *results
)
{
    (void)context;
    (void)service;
    (void)arguments;
    (void)results;
    return NULL;
}
static bool backing_probe(behavior_backend backend, int clearing)
{
    execution_module_plan *plan = execution_module_plan_new();
    plan->id = UINT64_C(947320);
    plan->valid = true;
    const pin_record *row, *projection;
    studio_outliner_records(&plan->storage, &row, &projection);
    (void)projection;
    pin_value shape = { .type = PIN_STRUCT, .type_name = row->type.name, .record = row };
    behavior_function_builder *builder = behavior_function_builder_new();
    pin_value argument = { .type = PIN_BOOL }, result = { .type = PIN_I32 };
    behavior_function_builder_signature(builder, &argument, 1, &result, 1, NULL, 0);
    int bank = behavior_function_builder_values(builder, &shape, 1);
    int header = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
    int zero = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
    int condition = behavior_function_builder_slot(builder, PIN_BOOL);
    int payload = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
    int depth = behavior_function_builder_slot(builder, PIN_I32);
    REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, header, BEHAVIOR_ADDRESS));
    REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, zero, BEHAVIOR_ADDRESS));
    REQUIRE(behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, payload, BEHAVIOR_ADDRESS));
    behavior_function_builder_emit(
        builder,
        (
            behavior_function_instruction
        ){ .op = BF_HOST_SLOT, .type = BF_POINTER_TYPE, .output = header, .left = bank }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = BF_POINTER_TYPE,
                                         .output = zero,
                                         .value = { .type = BF_POINTER_TYPE } }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_ARGUMENT,
                                         .type = PIN_BOOL,
                                         .output = condition,
                                         .left = 0,
                                         .value = { .type = PIN_BOOL } }
    );
    int branch = -1;
    if (clearing == 2) {
        branch = behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_BRANCH, .left = condition }
        );
    }
    if (clearing) {
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_WRITE,
                                             .type = BF_POINTER_TYPE,
                                             .left = zero,
                                             .right = header,
                                             .offset = offsetof(pin_value, d) }
        );
    }
    int load = behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = BF_POINTER_TYPE,
                                         .left = header,
                                         .output = payload,
                                         .offset = offsetof(pin_value, d) }
    );
    if (branch >= 0) {
        REQUIRE(behavior_function_builder_patch_target(builder, branch, load));
    }
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = PIN_I32,
                                         .left = payload,
                                         .output = depth,
                                         .offset = row->type.fields[5].offset }
    );
    behavior_function_builder_emit(
        builder,
        (
            behavior_function_instruction
        ){ .op = BF_RESULT, .type = PIN_I32, .left = depth, .target = 0 }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    behavior_function *function = behavior_function_builder_finish(builder, error, sizeof error);
    behavior_function_builder_free(builder);
    REQUIRE(function);
    apush(
        plan->functions,
        ((execution_module_function){ .id = 1,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .continuation_wait = true,
                                      .body = function })
    );
    plan->function_count = 1;
    execution_module *module = execution_module_finish(&plan, backend, error, sizeof error);
    REQUIRE(module);
    execution_producer_contract contract = { .module = UINT64_C(947320) };
    bool accepted = execution_prepared_producer_valid(module, &contract, error, sizeof error);
    if (!clearing && !accepted) {
        fprintf(stderr, "Outliner backing probe: %s\n", error);
    }
    bool correct = clearing ? !accepted && strstr(error, "unbacked") : accepted;
    execution_module_free(module);
    execution_module_plan_free(plan);
    REQUIRE(correct);
    return true;
}
TEST_CASE(outliner_contract_proves_record_backing_on_every_incoming_path)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        REQUIRE(backing_probe(backends[i], 0));
        REQUIRE(backing_probe(backends[i], 1));
        REQUIRE(backing_probe(backends[i], 2));
    }
    return true;
}
static bool floating_probe(behavior_backend backend, pin_value literal)
{
    execution_module_plan *plan = execution_module_plan_new();
    execution_module *module = NULL;
    behavior_function_builder *builder = behavior_function_builder_new();
    bool correct = false;
    char error[256] = { 0 };
    plan->id = UINT64_C(947321);
    plan->valid = true;
    pin_value result = { .type = literal.type };
    behavior_function_builder_signature(builder, NULL, 0, &result, 1, NULL, 0);
    int context = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
    int index = behavior_function_builder_slot(builder, PIN_I32);
    int value = behavior_function_builder_slot(builder, literal.type);
    int header = behavior_function_builder_slot(builder, BF_POINTER_TYPE);
    if (!behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, context, BEHAVIOR_ADDRESS) ||
        !behavior_function_builder_kind(builder, BEHAVIOR_BANK_SLOT, header, BEHAVIOR_ADDRESS)) {
        goto done;
    }
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){
            .op = BF_CONTEXT,
            .type = BF_POINTER_TYPE,
            .output = context,
            .offset = offsetof(behavior_function_context, results) }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_CONSTANT,
                                         .type = PIN_I32,
                                         .output = index,
                                         .value = { .type = PIN_I32 } }
    );
    behavior_function_builder_emit(
        builder,
        (
            behavior_function_instruction
        ){ .op = BF_CONSTANT, .type = literal.type, .output = value, .value = literal }
    );
    int indexed = behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_INDEX,
                                         .type = BF_POINTER_TYPE,
                                         .output = header,
                                         .left = context,
                                         .right = index,
                                         .offset = sizeof(pin_value) }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_WRITE,
                                         .type = literal.type,
                                         .left = value,
                                         .right = header,
                                         .offset = offsetof(pin_value, d) }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    behavior_function *function = behavior_function_builder_finish(builder, error, sizeof error);
    if (!function) {
        goto done;
    }
    apush(
        plan->functions,
        ((execution_module_function){ .id = 1,
                                      .kind = EXECUTION_FUNCTION_AUTHORED,
                                      .continuation_wait = true,
                                      .body = function })
    );
    plan->function_count = 1;
    module = execution_module_finish(&plan, backend, error, sizeof error);
    if (!module) {
        goto done;
    }
    execution_producer_contract contract = { .module = UINT64_C(947321) };
    if (!execution_prepared_producer_valid(module, &contract, error, sizeof error)) {
        goto done;
    }
    /* A floating literal is a value, never an integer result-header index.
     * Mutate only test-owned finalized metadata, and restore before release. */
    function = module->plan.functions[0].body;
    function->instructions[indexed].right = value;
    correct = !execution_prepared_producer_valid(module, &contract, error, sizeof error) &&
              strstr(error, "Unproved producer instruction");
    function->instructions[indexed].right = index;
done:
    if (!correct) {
        fprintf(stderr, "Outliner floating probe: %s\n", error);
    }
    behavior_function_builder_free(builder);
    execution_module_free(module);
    execution_module_plan_free(plan);
    REQUIRE(correct);
    return true;
}
TEST_CASE(outliner_contract_keeps_floating_constants_typed)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int i = 0; i < 2; i++) {
        REQUIRE(floating_probe(backends[i], (pin_value){ .type = PIN_F32, .f = 0.0f }));
        REQUIRE(floating_probe(backends[i], (pin_value){ .type = PIN_F64, .d = 0.0 }));
    }
    return true;
}
TEST_CASE(outliner_contract_accepts_real_document_identity_and_policy_change)
{
    const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(backends[b], LIBRARY));
        REQUIRE(valid());
        char changed[sizeof LIBRARY];
        memcpy(changed, LIBRARY, sizeof changed);
        char *icon = strstr(changed, "\"icon\"");
        REQUIRE(icon);
        memcpy(icon + 1, "edit", 4);
        REQUIRE(prepare(backends[b], changed));
        REQUIRE(valid());
    }
    return true;
}
TEST_CASE(outliner_contract_rejects_finalized_metadata_and_callback_substitution)
{
    REQUIRE(prepare(BEHAVIOR_BYTECODE, LIBRARY));
    REQUIRE(valid());
    execution_module_plan *plan = &producer->plan;
    uint64_t identity = plan->id;
    plan->id++;
    REQUIRE(rejected());
    plan->id = identity;
    execution_module_state *state = &plan->states[0];
    pin_value initial = state->initial;
    state->initial.i++;
    REQUIRE(rejected());
    state->initial = initial;
    state->shared = true;
    REQUIRE(rejected());
    state->shared = false;
    uint64_t state_id = state->id;
    state->id++;
    REQUIRE(rejected());
    state->id = state_id;
    execution_function_kind kind = plan->functions[0].kind;
    plan->functions[0].kind = EXECUTION_FUNCTION_SYSTEM;
    REQUIRE(rejected());
    plan->functions[0].kind = kind;
    uint64_t event_abi = plan->events[0].abi;
    plan->events[0].abi++;
    REQUIRE(rejected());
    plan->events[0].abi = event_abi;
    REQUIRE(plan->import_count > 0);
    execution_module_plan *library = &((execution_module *)plan->imports[0])->plan;
    execution_type_requirement *record = NULL;
    for (int i = 0; i < library->type_count; i++) {
        if (library->types[i].record &&
            !strcmp(library->types[i].name, "OutlinerRowPolicies.Projection")) {
            record = &library->types[i];
        }
    }
    REQUIRE(record);
    uint64_t record_id = record->id, record_layout = record->layout;
    record->id++;
    REQUIRE(rejected());
    record->id = record_id;
    record->layout++;
    REQUIRE(rejected());
    record->layout = record_layout;
    bool null_root = false;
    for (int f = 0; f < library->function_count && !null_root; f++) {
        behavior_function *function = library->functions[f].body;
        for (int r = 0; r < acount(function->value_roots) && !null_root; r++) {
            const behavior_value_root *root = &function->value_roots[r];
            if (root->bank != BEHAVIOR_BANK_LOCAL) {
                continue;
            }
            bool used = false;
            for (int i = 0; i < acount(function->instructions); i++) {
                const behavior_function_instruction *instruction = &function->instructions[i];
                used |= (instruction->op == BF_LOCAL &&
                         instruction->left == root->index) ||
                        (instruction->op == BF_SAVE &&
                         instruction->target == root->index);
            }
            behavior_function_local *local = &function->locals[root->index];
            if (!used && local->kind == BEHAVIOR_ADDRESS && !local->initial.u64) {
                local->initial.u64 = 1;
                REQUIRE(rejected());
                local->initial.u64 = 0;
                null_root = true;
            }
        }
    }
    REQUIRE(null_root);
    bool exit_write = false;
    for (int f = 0; f < library->function_count && !exit_write; f++) {
        behavior_function *function = library->functions[f].body;
        for (int c = 0; c < acount(function->instructions) && !exit_write; c++) {
            behavior_function_instruction *context = &function->instructions[c];
            if (context->op != BF_CONTEXT || context->target != 1) {
                continue;
            }
            for (int w = c + 1; w < acount(function->instructions) && !exit_write; w++) {
                behavior_function_instruction *write = &function->instructions[w];
                if (write->op != BF_WRITE || write->right != context->output ||
                    write->offset !=
                        offsetof(behavior_function_context, exit_requested)) {
                    continue;
                }
                size_t offset = write->offset;
                write->offset = offsetof(behavior_function_context, returned);
                REQUIRE(rejected());
                write->offset = offset;
                for (int v = c + 1; v < w; v++) {
                    behavior_function_instruction *value = &function->instructions[v];
                    if (value->op == BF_CONSTANT && value->output == write->left) {
                        REQUIRE(value->value.type == PIN_BOOL && value->value.b);
                        value->value.b = false;
                        REQUIRE(rejected());
                        value->value.b = true;
                        exit_write = true;
                        break;
                    }
                }
            }
        }
    }
    REQUIRE(exit_write);
    behavior_function_host_call *capture = NULL;
    behavior_function *body = NULL;
    for (int i = 0; i < plan->function_count; i++) {
        behavior_function *function = plan->functions[i].body;
        for (int j = 0; j < acount(function->host_calls); j++) {
            if (function->host_calls[j].capability_id ==
                script_catalog_id("capability", STUDIO_OUTLINER_CAPTURE_NAME)) {
                capture = &function->host_calls[j];
                body = function;
            }
        }
    }
    REQUIRE(capture && body);
    const node_host_call *original = capture->call;
    node_host_call altered = *original;
    altered.context = substituted;
    capture->call = &altered;
    REQUIRE(rejected());
    altered = *original;
    altered.service = "studio.unapproved";
    REQUIRE(rejected());
    altered = *original;
    altered.optional_service = true;
    REQUIRE(rejected());
    altered = *original;
    altered.reusable = true;
    REQUIRE(rejected());
    altered = *original;
    altered.variadic = true;
    REQUIRE(rejected());
    capture->call = original;
    uint64_t capability = capture->capability_id;
    capture->capability_id++;
    REQUIRE(rejected());
    capture->capability_id = capability;
    uint64_t abi = capture->capability_abi;
    capture->capability_abi++;
    REQUIRE(rejected());
    capture->capability_abi = abi;
    const node_access *access = capture->access;
    node_access altered_access = *access;
    altered_access.flags = NODE_EFFECT_PURE;
    capture->access = &altered_access;
    REQUIRE(rejected());
    capture->access = access;
    bool address_case = false;
    for (int i = 0; i < acount(body->instructions); i++) {
        if (body->instructions[i].op != BF_HOST_SLOT) {
            continue;
        }
        behavior_function_instruction saved = body->instructions[i];
        body->instructions[i].op = BF_CONTEXT;
        body->instructions[i].target = 1;
        REQUIRE(rejected());
        body->instructions[i] = saved;
        address_case = true;
        break;
    }
    REQUIRE(address_case && valid());
    return true;
}
void test_language_outliner_contract()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(outliner_contract_accepts_real_document_identity_and_policy_change);
    RUN_TEST_CASE(outliner_contract_rejects_finalized_metadata_and_callback_substitution);
    RUN_TEST_CASE(outliner_contract_proves_record_backing_on_every_incoming_path);
    RUN_TEST_CASE(outliner_contract_keeps_floating_constants_typed);
    pu_clear_setup();
}
