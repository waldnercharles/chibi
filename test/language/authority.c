#include "execution/authority.h"

#include "backend.h"
#include "ecs/ecs.h"
#include "execution/authority_internal.h"
#include "execution/definition.h"
#include "execution/delay.h"
#include "execution/function_builder.h"
#include "execution/inspection.h"
#include "execution/request.h"
#include "execution/status.h"
#include "language/lower.h"
#include "runtime/behaviors/source.h"
#include "runtime/behaviors/source_authority.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Nine finite cases, both execution backends: 22 scenario preparations plus four
 * imported helpers. One entity, at most two registrations, a four-event batch
 * plus one callback-emitted followup and two retained host requests. Direct
 * entries have 1000 instructions; source fan-out uses its fixed 10000 budget.
 * No clocks, files, threads, frames or sleeps.
 * Inspector cases also require compiled entries. */
enum
{
    RUN = 10,
    CONTROL,
    WAIT,
    MESSAGE,
    PROBE = 20,
    RECORD,
    DELAY,
    HOLD,
    READY,
    REMOTE,
    REQUEST_PROBE = 90,
    OBSERVE = 0,
    ENQUEUE,
    CANCEL_SELF,
    CANCEL_QUEUED,
    REBIND,
    MISSING_EVENT,
    RELEASE_SELF,
    CANCEL_RELEASE_SELF,
    MAX_OBSERVATIONS = 16,
    BUDGET = 1000
};
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static const script_catalog_parameter operation = { .name = "operation",
                                                    .type = { .storage = PIN_I32 } };
static const script_catalog_parameter text_parameter = { .name = "text",
                                                         .type = { .storage = PIN_STR } };
static const script_catalog_parameter duration = { .name = "duration",
                                                   .type = { .storage = PIN_F64 } };
static const char *const completed[] = { "Completed" };
static const script_catalog_capability capabilities[] = {
    { .id = RUN, .name = "Run", .kind = SCRIPT_CAPABILITY_EVENT },
    { .id = CONTROL, .name = "Control", .kind = SCRIPT_CAPABILITY_EVENT },
    { .id = WAIT, .name = "Wait", .kind = SCRIPT_CAPABILITY_EVENT },
    { .id = MESSAGE,
      .name = "Message",
      .kind = SCRIPT_CAPABILITY_EVENT,
      .parameters = &text_parameter,
      .parameter_count = 1 },
    { .id = PROBE,
      .name = "Probe",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &operation,
      .parameter_count = 1,
      .effects = SCRIPT_EFFECT_HOST },
    { .id = RECORD,
      .name = "Record",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &text_parameter,
      .parameter_count = 1,
      .effects = SCRIPT_EFFECT_HOST },
    { .id = DELAY,
      .name = "Delay",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .parameters = &duration,
      .parameter_count = 1,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_SUSPEND },
    { .id = HOLD,
      .name = "Hold",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_SUSPEND },
    { .id = READY,
      .name = "Ready",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .outcomes = completed,
      .outcome_count = 1,
      .completion_outcome = 0,
      .effects = SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_SUSPEND },
    { .id = REMOTE,
      .name = "Remote",
      .qualified_name = "Helper.Remote",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .effects = SCRIPT_EFFECT_HOST },
    { .id = REQUEST_PROBE,
      .name = "WorkProbe",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .parameters = &operation,
      .parameter_count = 1,
      .effects = SCRIPT_EFFECT_HOST },
};
static const script_catalog catalog = {
    .capabilities = capabilities,
    .capability_count = sizeof capabilities / sizeof *capabilities,
};
typedef struct
{
    execution_authority proof;
    const void *service;
    bool valid, original_current, after;
    bool state_valid, state_after;
    pin_value state;
} observation;
static struct
{
    execution_module *module, *imported;
    execution_program *program;
    execution_instance *instance;
    execution_inspector *inspector;
    source_registry *empty_registry;
    ecs_world *world;
    execution_dispatch *tokens[4];
    flow_host_request *requests[2];
    service_binding *services;
    behavior_run_scope runs;
    uint64_t owner, state_id;
    observation observations[MAX_OBSERVATIONS];
    int observed, request_count, recorded, nested_status, rebind_status;
    char records[4][32];
    bool fabricated_rejected, fail_batch, cancelled_pending;
} fixture;
static int first_service, second_service;

static void cleanup()
{
    if (fixture.runs.active) {
        behavior_run_scope_end(&fixture.runs);
    }
    execution_program_free(fixture.program);
    execution_inspector_free(fixture.inspector);
    if (fixture.empty_registry) {
        source_registry_free(fixture.empty_registry);
    }
    for (int i = 0; i < fixture.request_count; i++) {
        flow_host_release(fixture.requests[i]);
    }
    for (int i = 0; i < 4; i++) {
        execution_dispatch_free(fixture.tokens[i]);
    }
    execution_module_free(fixture.module);
    execution_module_free(fixture.imported);
    ecs_world_free(fixture.world);
    afree(fixture.services);
    memset(&fixture, 0, sizeof fixture);
}
static const char *probe(
    const behavior_function_context *context,
    void *service,
    const pin_value *in,
    pin_value *
)
{
    assert(in[0].type == PIN_I32 && fixture.observed < MAX_OBSERVATIONS);
    observation *item = &fixture.observations[fixture.observed++];
    item->service = service;
    item->valid = execution_context_authority(context, &item->proof);
    item->original_current = execution_authority_current(
        context,
        &fixture.observations[0].proof
    );
    item->after = item->valid;
    item->state_valid = execution_context_state(context, fixture.state_id, &item->state);
    /* Even a byte-for-byte context copy, or one with hostile pointer members,
     * is not a runtime-owned live frame. Neither pointer may be dereferenced. */
    behavior_function_context forged = *context;
    execution_authority ignored;
    fixture.fabricated_rejected &= !execution_context_authority(&forged, &ignored);
    pin_value state;
    fixture.fabricated_rejected &= !execution_context_state(&forged, fixture.state_id, &state);
    forged.inspection_user = (void *)(uintptr_t)1;
    forged.parent = (behavior_function_context *)(uintptr_t)1;
    fixture.fabricated_rejected &= !execution_context_authority(&forged, &ignored);
    fixture.fabricated_rejected &= !execution_context_state(&forged, fixture.state_id, &state);
    fixture.fabricated_rejected &= !execution_context_state(context, 0, &state);
    switch (in[0].i) {
        case OBSERVE: break;
        case ENQUEUE:
            fixture.nested_status = execution_program_dispatch_tagged(
                fixture.program,
                fixture.tokens[1],
                CONTROL,
                fixture.owner,
                NULL,
                0,
                0,
                0,
                BUDGET
            );
            item->after = execution_authority_current(context, &item->proof);
            break;
        case CANCEL_SELF:
            execution_dispatch_cancel(fixture.tokens[0]);
            item->after = execution_context_authority(context, &ignored);
            break;
        case CANCEL_QUEUED: {
            execution_dispatch_cancel(fixture.tokens[1]);
            fixture.cancelled_pending = execution_dispatch_pending(fixture.tokens[1]);
            char text[] = "new";
            pin_value value = { .type = PIN_STR, .str = text };
            fixture.nested_status = execution_program_emit(
                fixture.program,
                MESSAGE,
                fixture.owner,
                &value,
                1
            );
            memset(text, '?', sizeof text - 1);
            if (fixture.fail_batch) {
                return "authority fixture queue failure";
            }
            break;
        }
        case REBIND:
            fixture.rebind_status = execution_program_bind_service(
                fixture.program,
                "authority.test",
                &second_service
            );
            item->after = execution_authority_current(context, &item->proof);
            break;
        case MISSING_EVENT:
            fixture.nested_status = source_dispatch_all(fixture.empty_registry, "Missing", NULL, 0, 0, 0);
            item->after = execution_authority_current(context, &item->proof);
            break;
        case RELEASE_SELF:
        case CANCEL_RELEASE_SELF: {
            int token = in[0].i == RELEASE_SELF ? 0 : 1;
            if (fixture.tokens[token]) {
                if (in[0].i == CANCEL_RELEASE_SELF) {
                    execution_dispatch_cancel(fixture.tokens[token]);
                }
                execution_dispatch_free(fixture.tokens[token]);
                fixture.tokens[token] = NULL;
            }
            item->after = execution_authority_current(context, &item->proof);
            break;
        }
        default: assert(!"unknown authority probe");
    }
    item->state_after = execution_context_state(context, fixture.state_id, &state);
    return NULL;
}
static const char *record(const behavior_function_context *, void *, const pin_value *in, pin_value *)
{
    assert(in[0].type == PIN_STR && fixture.recorded < 4);
    assert(strlen(in[0].str) < sizeof fixture.records[0]);
    snprintf(
        fixture.records[fixture.recorded++],
        sizeof fixture.records[0],
        "%s",
        in[0].str
    );
    return NULL;
}
static int hold(void *, const char *, const pin_value *, int count, flow_host_request *request, const char **)
{
    assert(!count && fixture.request_count < 2);
    /* A successful begin transfers exactly one host reference to this fixture. */
    fixture.requests[fixture.request_count++] = request;
    return FLOW_OK;
}
static int ready(void *, const char *, const pin_value *, int count, flow_host_request *request, const char **)
{
    assert(!count);
    bool complete = flow_host_complete(request, -1, (pin_value){ 0 });
    assert(complete);
    (void)complete;
    flow_host_release(request);
    return FLOW_OK;
}
static const node_host_call probe_call = { .service = "authority.test",
                                           .context = probe };
static const char *work_probe(const behavior_function_context *, void *, const pin_value *, pin_value *);
static const node_host_call work_probe_call = { .context = work_probe };
static const node_host_call record_call = { .context = record };
static const node_command hold_command = { .begin = hold };
static const node_command ready_command = { .begin = ready };
static bool host(void *, uint64_t id, script_lower_host *out)
{
    switch (id) {
        case PROBE:
            *out = (script_lower_host){ .call = &probe_call };
            return true;
        case REQUEST_PROBE:
            *out = (script_lower_host){ .call = &work_probe_call };
            return true;
        case RECORD:
            *out = (script_lower_host){ .call = &record_call };
            return true;
        case DELAY:
            *out = (script_lower_host){ .command = &EXECUTION_Delay };
            return true;
        case HOLD:
            *out = (script_lower_host){ .command = &hold_command };
            return true;
        case READY:
            *out = (script_lower_host){ .command = &ready_command };
            return true;
        case REMOTE:
            if (!fixture.imported) {
                return false;
            }
            for (int i = 0; i < execution_module_function_count(fixture.imported); i++) {
                execution_module_function_info
                    function = execution_module_function_at(fixture.imported, i);
                if (function.kind == EXECUTION_FUNCTION_AUTHORED) {
                    *out = (script_lower_host){ .function = function.body,
                                                .module = fixture.imported };
                    return true;
                }
            }
            return false;
        default: return false;
    }
}
static execution_module *compile(const char *source, behavior_backend backend)
{
    script_syntax *syntax = script_parse(source);
    script_analysis *analysis = script_analyze(syntax, &catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "authority semantic: %s\n", analysis->diagnostics[i].message);
    }
    const script_lower_bindings bindings = { .host = host };
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_STATE &&
            !strcmp(analysis->symbols[i].name, "Seen")) {
            fixture.state_id = analysis->symbols[i].id;
        }
    }
    execution_module_plan *plan = analysis->valid
                                    ? script_lower(analysis, &catalog, &bindings)
                                    : NULL;
    for (int i = 0; plan && i < plan->diagnostic_count; i++) {
        fprintf(stderr, "authority lower: %s\n", plan->diagnostics[i].message);
    }
    char error[256] = "invalid source";
    execution_module *module = plan && plan->valid
                                 ? execution_module_finish(&plan, backend, error, sizeof error)
                                 : NULL;
    if (!module) {
        fprintf(stderr, "authority module: %s\n", error);
    }
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    return module;
}
static bool prepare_limits(const char *source, behavior_backend backend, bool imported, const uintptr_t *limits)
{
    fixture.fabricated_rejected = true;
    if (imported) {
        fixture.imported = compile("Helper :: library { Remote :: () { Probe(0); } }", backend);
        REQUIRE(fixture.imported);
    }
    fixture.module = compile(source, backend);
    REQUIRE(fixture.module && execution_module_backend(fixture.module) == backend);
    fixture.world = ecs_world_new();
    apush(fixture.services, ((service_binding){ "authority.test", &first_service }));
    fixture
        .program = execution_program_new(fixture.module, fixture.world, fixture.services);
    REQUIRE(fixture.program);
    fixture.owner = ecs_entity_to_u64(ecs_world_create_entity(fixture.world));
    fixture.instance = execution_program_attach(fixture.program, fixture.owner);
    REQUIRE(fixture.instance);
    for (int i = 0; i < 4; i++) {
        fixture.tokens[i] = limits && limits[i]
                              ? execution_dispatch_new_work(limits[i])
                              : execution_dispatch_new();
        REQUIRE(execution_dispatch_id(fixture.tokens[i]));
        REQUIRE(!execution_dispatch_cancelled(fixture.tokens[i]));
        REQUIRE(!execution_dispatch_pending(fixture.tokens[i]));
    }
    behavior_run_scope_begin(&fixture.runs);
    return true;
}
static bool prepare(const char *source, behavior_backend backend, bool imported)
{
    return prepare_limits(source, backend, imported, NULL);
}
static int dispatch(int token, int event, double now)
{
    return execution_program_dispatch_tagged(
        fixture.program,
        fixture.tokens[token],
        event,
        fixture.owner,
        NULL,
        0,
        now,
        0,
        BUDGET
    );
}
static int pump(double now)
{
    return execution_program_pump(fixture.program, now, 0, BUDGET);
}
static bool exercised(behavior_backend backend)
{
    const behavior_run_counts *counts = &fixture.runs.counts;
    REQUIRE(!counts->overflow && !counts->inspection_bytecode_fallbacks);
    REQUIRE(
        backend == BEHAVIOR_BYTECODE ? counts->bytecode_entries > 0 : !counts->bytecode_entries
    );
    REQUIRE(backend != BEHAVIOR_SLJIT || counts->sljit_entries > 0);
    REQUIRE(backend != BEHAVIOR_WASM || counts->wasm_entries > 0);
    REQUIRE(fixture.fabricated_rejected);
    if (fixture.state_id) {
        for (int i = 0; i < fixture.observed; i++) {
            const observation *item = &fixture.observations[i];
            REQUIRE(item->state_valid == item->valid && item->state_after == item->after);
        }
    }
    return true;
}
static bool outside_rejected()
{
    behavior_function_context context = { 0 };
    execution_authority ignored;
    pin_value value;
    return !execution_context_authority(&context, &ignored) &&
           !execution_authority_current(&context, &fixture.observations[0].proof) &&
           !execution_context_state(&context, fixture.state_id, &value);
}
TEST_CASE(authority_tagged_handler_and_synchronous_helpers_share_proof)
{
    const char *source = "Authority :: behavior { Seen:bool=false; "
                         "Leaf :: () { Seen=false; Probe(0); } "
                         "Local :: () { Leaf(); } on Run { Seen=true; "
                         "Probe(0); Local(); "
                         "Helper.Remote(); Seen=true; Probe(0); } }";
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(source, backends[b], true));
        REQUIRE(fixture.state_id);
        REQUIRE(dispatch(0, RUN, 0) == FLOW_OK && fixture.observed == 4);
        REQUIRE(!execution_dispatch_pending(fixture.tokens[0]));
        const execution_authority *first = &fixture.observations[0].proof;
        REQUIRE(first->program == fixture.program && first->module == fixture.module);
        REQUIRE(first->owner == fixture.owner && first->event == RUN);
        REQUIRE(first->request == execution_dispatch_id(fixture.tokens[0]));
        REQUIRE(first->activation && first->slice && first->function);
        bool root = false;
        for (int i = 0; i < execution_module_function_count(fixture.module); i++) {
            execution_module_function_info
                info = execution_module_function_at(fixture.module, i);
            root |= info.kind == EXECUTION_FUNCTION_HANDLER &&
                    info.event == RUN && info.body == first->function;
        }
        REQUIRE(root && execution_module_import_count(fixture.module) == 1);
        for (int i = 0; i < 4; i++) {
            REQUIRE(fixture.observations[i].valid && fixture.observations[i].original_current);
            REQUIRE(fixture.observations[i].state.type == PIN_BOOL);
            REQUIRE(fixture.observations[i].state.b == (i == 0 || i == 3));
        }
        REQUIRE(outside_rejected() && exercised(backends[b]));
        cleanup();
    }
    return true;
}
TEST_CASE(authority_untagged_and_direct_invocations_reject_fabrication)
{
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare("Authority :: behavior { on Run { Probe(0); } }", backends[b], true));
        REQUIRE(
            execution_program_dispatch(fixture.program, RUN, fixture.owner, NULL, 0, 0, 0, BUDGET) == FLOW_OK
        );
        REQUIRE(fixture.observed == 1 && !fixture.observations[0].valid);
        const behavior_function *function = NULL;
        for (int i = 0; i < execution_module_function_count(fixture.imported); i++) {
            execution_module_function_info
                info = execution_module_function_at(fixture.imported, i);
            if (info.kind == EXECUTION_FUNCTION_AUTHORED) {
                function = info.body;
            }
        }
        REQUIRE(function);
        behavior_function_activation activation = { 0 };
        bool resumable = behavior_function_resumable(function);
        if (resumable) {
            behavior_function_activation_init(function, &activation);
        }
        arena storage = { 0 };
        behavior_function_context context = {
            .activation = resumable ? &activation : NULL,
            .budget = BUDGET,
            .program_name = "Authority",
            .inspection_user = (void *)(uintptr_t)1,
        };
        behavior_function_bind_storage(&context, &storage);
        bool bound = behavior_function_bind(
            function,
            &context,
            fixture.services,
            fixture.owner
        );
        int status = bound ? behavior_function_run(function, &context, b != 0)
                           : FLOW_ERR_STATE;
        behavior_function_release_host(&context);
        behavior_function_activation_free(&activation);
        arena_free(&storage);
        REQUIRE(bound && status == FLOW_OK);
        REQUIRE(fixture.observed == 2 && !fixture.observations[1].valid);
        REQUIRE(outside_rejected() && exercised(backends[b]));
        cleanup();
    }
    return true;
}
TEST_CASE(authority_nested_enqueue_permanently_invalidates_outer_slice)
{
    const char *source = "Authority :: behavior { Seen:bool=false; on Run { "
                         "Probe(0); Probe(1); "
                         "Probe(0); } on Control { Probe(0); } "
                         "on Wait { Probe(0); Probe(5); Probe(0); } }";
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(source, backends[b], false));
        fixture.empty_registry = source_registry_new(fixture.world);
        REQUIRE(fixture.empty_registry);
        REQUIRE(dispatch(0, RUN, 0) == FLOW_OK && fixture.nested_status == FLOW_OK);
        REQUIRE(!execution_dispatch_pending(fixture.tokens[0]));
        REQUIRE(execution_dispatch_pending(fixture.tokens[1]));
        REQUIRE(fixture.observed == 3 && fixture.observations[0].valid);
        REQUIRE(fixture.observations[1].valid && !fixture.observations[1].after);
        REQUIRE(!fixture.observations[2].valid && !fixture.observations[2].original_current);
        REQUIRE(pump(1) == FLOW_OK && fixture.observed == 4);
        REQUIRE(!execution_dispatch_pending(fixture.tokens[1]));
        REQUIRE(fixture.observations[3].valid && !fixture.observations[3].original_current);
        REQUIRE(
            fixture.observations[3].proof.request ==
            execution_dispatch_id(fixture.tokens[1])
        );
        REQUIRE(dispatch(2, WAIT, 2) == FLOW_OK && fixture.observed == 7);
        REQUIRE(fixture.nested_status == 0 && fixture.observations[4].valid);
        REQUIRE(fixture.observations[5].valid && !fixture.observations[5].after);
        REQUIRE(!fixture.observations[6].valid);
        REQUIRE(exercised(backends[b]));
        cleanup();
    }
    return true;
}
TEST_CASE(authority_wait_and_debugger_resume_never_restore_proof)
{
    const char *source = "Authority :: behavior { on Run { Probe(0); await "
                         "Delay(1); Probe(0); } on Control { Probe(0); } }";
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(source, backends[b], false));
        REQUIRE(dispatch(0, RUN, 0) == FLOW_OK && fixture.observed == 1);
        REQUIRE(execution_dispatch_pending(fixture.tokens[0]));
        REQUIRE(
            fixture.observations[0].valid &&
            execution_instance_pending(fixture.instance) == 1
        );
        REQUIRE(pump(.5) == FLOW_OK && fixture.observed == 1);
        REQUIRE(pump(1) == FLOW_OK && fixture.observed == 2);
        REQUIRE(!execution_dispatch_pending(fixture.tokens[0]));
        REQUIRE(!fixture.observations[1].valid && !fixture.observations[1].original_current);
        fixture.inspector = execution_program_inspector(fixture.program);
        REQUIRE(
            fixture.inspector &&
            execution_inspector_pause(fixture.inspector, fixture.owner)
        );
        REQUIRE(
            dispatch(1, CONTROL, 2) == FLOW_OK &&
            execution_program_paused(fixture.program)
        );
        REQUIRE(fixture.observed == 2 && outside_rejected());
        REQUIRE(execution_inspector_control(fixture.inspector, EXECUTION_DEBUG_RESUME) == FLOW_OK);
        REQUIRE(pump(2) == FLOW_OK && fixture.observed == 3);
        REQUIRE(!fixture.observations[2].valid && !fixture.observations[2].original_current);
        REQUIRE(!execution_program_paused(fixture.program));
        REQUIRE(execution_inspector_pause(fixture.inspector, fixture.owner));
        REQUIRE(
            dispatch(2, CONTROL, 3) == FLOW_OK &&
            execution_program_paused(fixture.program)
        );
        REQUIRE(execution_dispatch_pending(fixture.tokens[2]));
        execution_dispatch_cancel(fixture.tokens[2]);
        REQUIRE(pump(3) == FLOW_OK && fixture.observed == 3);
        REQUIRE(!execution_program_paused(fixture.program));
        REQUIRE(!execution_dispatch_pending(fixture.tokens[2]));
        REQUIRE(dispatch(3, CONTROL, 4) == FLOW_OK && fixture.observed == 4);
        REQUIRE(fixture.observations[3].valid);
        REQUIRE(!execution_instance_pending(fixture.instance) && exercised(backends[b]));
        cleanup();
    }
    return true;
}
TEST_CASE(authority_first_run_task_and_continuation_inherit_only_cancellation)
{
    const char *sources[] = {
        "Authority :: behavior { Child :: () { Probe(2); yield; Probe(0); } "
        "on Run { Probe(0); child := Child(); } on Control { Probe(0); } }",
        "Authority :: behavior { action ready = Ready(); after "
        "ready.Completed { Probe(2); } on Run { Probe(0); ready(); } "
        "on Control { Probe(0); } }",
    };
    for (int b = 0; b < 2; b++) {
        for (int mode = 0; mode < 2; mode++) {
            REQUIRE(prepare(sources[mode], backends[b], false));
            REQUIRE(dispatch(0, RUN, 0) == FLOW_OK);
            REQUIRE(fixture.observed == 2 && fixture.observations[0].valid);
            REQUIRE(!fixture.observations[1].valid && !fixture.observations[1].after);
            REQUIRE(execution_dispatch_cancelled(fixture.tokens[0]));
            REQUIRE(pump(1) == FLOW_OK && fixture.observed == 2);
            REQUIRE(!execution_dispatch_pending(fixture.tokens[0]));
            REQUIRE(!execution_instance_pending(fixture.instance));
            REQUIRE(dispatch(1, CONTROL, 2) == FLOW_OK && fixture.observed == 3);
            REQUIRE(fixture.observations[2].valid && !fixture.observations[2].original_current);
            REQUIRE(!execution_program_error(fixture.program));
            REQUIRE(exercised(backends[b]));
            cleanup();
        }
    }
    return true;
}
TEST_CASE(authority_detached_batch_cancellation_preserves_payloads_and_requeue_order)
{
    const char *source = "Authority :: behavior { on Run { Probe(3); } "
                         "on Message(text:string) { Record(text); } }";
    for (int b = 0; b < 2; b++) {
        for (int fail = 0; fail < 2; fail++) {
            REQUIRE(prepare(source, backends[b], false));
            fixture.fail_batch = fail != 0;
            REQUIRE(
                execution_program_emit_tagged(
                    fixture.program,
                    fixture.tokens[0],
                    RUN,
                    fixture.owner,
                    NULL,
                    0
                ) == FLOW_OK
            );
            char payloads[3][16] = { "retired", "first", "second" };
            for (int i = 0; i < 3; i++) {
                pin_value value = { .type = PIN_STR, .str = payloads[i] };
                REQUIRE(
                    execution_program_emit_tagged(
                        fixture.program,
                        fixture.tokens[i + 1],
                        MESSAGE,
                        fixture.owner,
                        &value,
                        1
                    ) == FLOW_OK
                );
                memset(payloads[i], '?', strlen(payloads[i]));
            }
            for (int i = 0; i < 4; i++) {
                REQUIRE(execution_dispatch_pending(fixture.tokens[i]));
            }
            /* Native callable failures are HOST_DATA, including void calls. */
            REQUIRE(pump(0) == (fail ? FLOW_ERR_INVALID_GRAPH : FLOW_OK));
            REQUIRE(
                fixture.nested_status == FLOW_OK &&
                execution_dispatch_cancelled(fixture.tokens[1])
            );
            REQUIRE(
                fixture.cancelled_pending &&
                !execution_dispatch_pending(fixture.tokens[1])
            );
            REQUIRE(fixture.recorded == (fail ? 0 : 2));
            if (fail) {
                const execution_program_failure *error = execution_program_error(
                    fixture.program
                );
                REQUIRE(error && error->status == FLOW_ERR_INVALID_GRAPH);
                REQUIRE(error->kind == BEHAVIOR_FUNCTION_HOST_DATA);
                REQUIRE(!strcmp(error->message, "authority fixture queue failure"));
                execution_program_clear_error(fixture.program);
            }
            REQUIRE(pump(1) == FLOW_OK && fixture.recorded == 3);
            REQUIRE(!strcmp(fixture.records[0], "first"));
            REQUIRE(!strcmp(fixture.records[1], "second"));
            REQUIRE(!strcmp(fixture.records[2], "new"));
            REQUIRE(pump(2) == FLOW_OK && fixture.recorded == 3);
            for (int i = 0; i < 4; i++) {
                REQUIRE(!execution_dispatch_pending(fixture.tokens[i]));
            }
            REQUIRE(execution_program_memory_usage(fixture.program).payload_capacity == 0);
            REQUIRE(exercised(backends[b]));
            cleanup();
        }
    }
    return true;
}
TEST_CASE(authority_cancelled_host_request_rejects_late_completion_without_harming_sibling)
{
    const char *source = "Authority :: behavior { on Run { Probe(0); "
                         "await Hold(); Probe(0); } }";
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(source, backends[b], false));
        REQUIRE(dispatch(0, RUN, 0) == FLOW_OK && dispatch(1, RUN, 0) == FLOW_OK);
        REQUIRE(fixture.request_count == 2 && fixture.observed == 2);
        REQUIRE(fixture.observations[0].valid && fixture.observations[1].valid);
        REQUIRE(execution_request_entity(fixture.requests[0]) == fixture.owner);
        REQUIRE(execution_request_entity(fixture.requests[1]) == fixture.owner);
        REQUIRE(execution_instance_pending(fixture.instance) == 2);
        REQUIRE(execution_dispatch_pending(fixture.tokens[0]));
        REQUIRE(execution_dispatch_pending(fixture.tokens[1]));
        execution_dispatch_cancel(fixture.tokens[0]);
        REQUIRE(execution_dispatch_pending(fixture.tokens[0]));
        REQUIRE(flow_host_status(fixture.requests[0]) == FLOW_HOST_CANCELLED);
        REQUIRE(!flow_host_complete(fixture.requests[0], -1, (pin_value){ 0 }));
        REQUIRE(pump(1) == FLOW_OK && execution_instance_pending(fixture.instance) == 1);
        /* The fixture still owns the cancelled request's host reference. */
        REQUIRE(!execution_dispatch_pending(fixture.tokens[0]));
        REQUIRE(execution_dispatch_pending(fixture.tokens[1]));
        execution_dispatch_free(fixture.tokens[0]);
        fixture.tokens[0] = NULL;
        REQUIRE(flow_host_status(fixture.requests[0]) == FLOW_HOST_CANCELLED);
        REQUIRE(!execution_request_owner(fixture.requests[0]));
        REQUIRE(!execution_request_owner_valid(fixture.requests[0]));
        REQUIRE(!flow_host_complete(fixture.requests[0], -1, (pin_value){ 0 }));
        REQUIRE(flow_host_resume(fixture.requests[0]) == FLOW_ERR_STATE);
        REQUIRE(execution_request_tick(fixture.requests[0], 1) == FLOW_ERR_STATE);
        REQUIRE(flow_host_status(fixture.requests[1]) == FLOW_HOST_PENDING);
        REQUIRE(flow_host_complete(fixture.requests[1], -1, (pin_value){ 0 }));
        REQUIRE(pump(2) == FLOW_OK && fixture.observed == 3);
        REQUIRE(!fixture.observations[2].valid && !fixture.observations[2].original_current);
        REQUIRE(!execution_instance_pending(fixture.instance) && exercised(backends[b]));
        cleanup();
    }
    return true;
}
TEST_CASE(authority_source_fanout_owns_token_after_callback_releases_caller_reference)
{
    const char *source = "Authority :: behavior { on Run { Probe(6); } "
                         "on Control { Probe(7); } }";
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(source, backends[b], false));
        execution_program_free(fixture.program);
        fixture.program = NULL;
        fixture.instance = NULL;
        fixture.empty_registry = source_registry_new(fixture.world);
        REQUIRE(fixture.empty_registry);
        const char *names[] = { "First", "Second" };
        ecs_entity entity = ecs_entity_from_u64(fixture.owner);
        for (int i = 0; i < 2; i++) {
            REQUIRE(
                source_register(
                    fixture.empty_registry,
                    names[i],
                    fixture.module,
                    fixture.services
                ) == FLOW_OK
            );
            REQUIRE(
                source_attach(fixture.empty_registry, entity, names[i], false, 0, 0) == FLOW_OK
            );
        }
        uint64_t released = execution_dispatch_id(fixture.tokens[0]);
        REQUIRE(
            source_dispatch_all_tagged(fixture.empty_registry, fixture.tokens[0], "Run", NULL, 0, 0, 0) == 2
        );
        REQUIRE(!fixture.tokens[0] && fixture.observed == 2);
        for (int i = 0; i < 2; i++) {
            const observation *item = &fixture.observations[i];
            REQUIRE(item->valid && item->after && item->proof.request == released);
            REQUIRE(item->proof.program == source_program(fixture.empty_registry, names[i]));
        }
        REQUIRE(
            fixture.observations[0].proof.activation !=
            fixture.observations[1].proof.activation
        );
        uint64_t cancelled = execution_dispatch_id(fixture.tokens[1]);
        REQUIRE(
            source_dispatch_all_tagged(fixture.empty_registry, fixture.tokens[1], "Control", NULL, 0, 1, 0) == 1
        );
        REQUIRE(!fixture.tokens[1] && fixture.observed == 3);
        REQUIRE(fixture.observations[2].valid && !fixture.observations[2].after);
        REQUIRE(fixture.observations[2].proof.request == cancelled);
        REQUIRE(source_pump(fixture.empty_registry, 2, 0) == FLOW_OK && fixture.observed == 3);
        REQUIRE(!source_error(fixture.empty_registry)[0]);
        REQUIRE(exercised(backends[b]));
        cleanup();
    }
    return true;
}
TEST_CASE(authority_rebind_detach_replacement_and_cache_reuse_retire_old_proofs)
{
    const char *source = "Authority :: behavior { on Run { Probe(0); } "
                         "on Control { Probe(4); Probe(0); } }";
    for (int b = 0; b < 2; b++) {
        REQUIRE(prepare(source, backends[b], false));
        REQUIRE(dispatch(0, RUN, 0) == FLOW_OK && fixture.observed == 1);
        REQUIRE(execution_program_memory_usage(fixture.program).activation_cache_bytes > 0);
        REQUIRE(dispatch(0, RUN, 1) == FLOW_OK && fixture.observed == 2);
        REQUIRE(fixture.observations[1].valid && !fixture.observations[1].original_current);
        const execution_authority first = fixture.observations[0].proof;
        const execution_authority second = fixture.observations[1].proof;
        REQUIRE(first.request == second.request && first.function == second.function);
        REQUIRE(first.activation != second.activation && first.slice != second.slice);
        REQUIRE(dispatch(0, CONTROL, 2) == FLOW_OK && fixture.observed == 4);
        REQUIRE(fixture.rebind_status == FLOW_OK && fixture.observations[2].valid);
        REQUIRE(!fixture.observations[2].after && !fixture.observations[3].valid);
        REQUIRE(fixture.observations[2].service == &first_service);
        REQUIRE(fixture.observations[3].service == &first_service);
        REQUIRE(dispatch(0, RUN, 3) == FLOW_OK && fixture.observed == 5);
        REQUIRE(fixture.observations[4].valid && fixture.observations[4].service == &second_service);
        fixture.inspector = execution_program_inspector(fixture.program);
        REQUIRE(
            fixture.inspector &&
            execution_inspector_pause(fixture.inspector, fixture.owner)
        );
        REQUIRE(
            dispatch(0, RUN, 4) == FLOW_OK && execution_program_paused(fixture.program)
        );
        execution_program_detach(fixture.program, fixture.owner);
        REQUIRE(!execution_program_paused(fixture.program));
        REQUIRE(!execution_program_instance(fixture.program, fixture.owner));
        fixture.instance = execution_program_attach(fixture.program, fixture.owner);
        REQUIRE(fixture.instance && dispatch(0, RUN, 5) == FLOW_OK && fixture.observed == 6);
        REQUIRE(fixture.observations[5].valid && !fixture.observations[5].original_current);
        REQUIRE(execution_inspector_pause(fixture.inspector, fixture.owner));
        REQUIRE(
            dispatch(0, RUN, 6) == FLOW_OK && execution_program_paused(fixture.program)
        );
        REQUIRE(execution_program_replace(fixture.program, fixture.module, false) == FLOW_OK);
        fixture.instance = execution_program_instance(fixture.program, fixture.owner);
        REQUIRE(fixture.instance && !execution_program_paused(fixture.program));
        REQUIRE(dispatch(0, RUN, 7) == FLOW_OK && fixture.observed == 7);
        REQUIRE(fixture.observations[6].valid && !fixture.observations[6].original_current);
        REQUIRE(!execution_instance_pending(fixture.instance));
        REQUIRE(outside_rejected() && exercised(backends[b]));
        cleanup();
    }
    return true;
}
void test_language_authority()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(authority_tagged_handler_and_synchronous_helpers_share_proof);
    RUN_TEST_CASE(authority_untagged_and_direct_invocations_reject_fabrication);
    RUN_TEST_CASE(authority_nested_enqueue_permanently_invalidates_outer_slice);
    RUN_TEST_CASE(authority_wait_and_debugger_resume_never_restore_proof);
    RUN_TEST_CASE(authority_first_run_task_and_continuation_inherit_only_cancellation);
    RUN_TEST_CASE(authority_detached_batch_cancellation_preserves_payloads_and_requeue_order);
    RUN_TEST_CASE(authority_cancelled_host_request_rejects_late_completion_without_harming_sibling);
    RUN_TEST_CASE(authority_source_fanout_owns_token_after_callback_releases_caller_reference);
    RUN_TEST_CASE(authority_rebind_detach_replacement_and_cache_reuse_retire_old_proofs);
    pu_clear_setup();
}

/* Separate finite request-work route. The existing authority runner above does
 * not execute these cases. Both backends prepare five behavior modules plus
 * one imported helper each: 12 modules total. Every preparation creates four
 * tokens (40 total, at most four live); at most seven of 16 observation slots
 * are used per preparation. At most one host request is retained.
 * All limits/grants are <=1000, recursion
 * has three calls, and the longest authored loop has 32 iterations. The final
 * case also finalizes three tiny definitions per backend (six total) and makes
 * seven direct calls plus one outlined child entry per backend. Across
 * both backends there are ten queued emits, 20 dispatch API calls, 12 pumps,
 * two updates, two task starts, two continuation starts and two host
 * completions. The 14 synchronous helper calls include the two outlined
 * tiny-definition children. These cover direct checkpoint, outlined-child
 * failure and prepaid-body rejection. No files, clocks, frames, threads,
 * generated inputs or retries with larger limits. */
enum
{
    WORK_NOTE = 100,
    WORK_GRANT,
    WORK_REFUSE,
    WORK_HOSTILE,
    WORK_CANCEL,
    WORK_RELEASE
};
typedef struct
{
    const execution_work *account;
    uintptr_t before, limit_before, after, limit_after;
    bool authority, metered, same_account, granted, rejected;
} work_observation;
static struct
{
    execution_dispatch *current; /* Borrowed only while a runtime owner lives. */
    work_observation observations[MAX_OBSERVATIONS];
    int observed, failures;
    uint64_t failed_steps;
    const behavior_function *failed_function;
    behavior_function_builder *builder;
    behavior_function *definitions[3];
    behavior_function_context context;
    behavior_function_activation activation;
    arena storage;
    pin_value result;
} work_fixture;

static void work_clear_call()
{
    behavior_function_release_host(&work_fixture.context);
    behavior_function_activation_free(&work_fixture.activation);
    arena_free(&work_fixture.storage);
    work_fixture.context = (behavior_function_context){ 0 };
}
static void work_cleanup()
{
    work_clear_call();
    if (work_fixture.builder) {
        behavior_function_builder_free(work_fixture.builder);
    }
    /* The parent definition borrows the checked child's immutable body. */
    for (int i = 2; i >= 0; i--) {
        behavior_function_free(work_fixture.definitions[i]);
    }
    cleanup();
    memset(&work_fixture, 0, sizeof work_fixture);
}
static const char *work_probe(const behavior_function_context *context, void *, const pin_value *in, pin_value *)
{
    assert(in[0].type == PIN_I32 && work_fixture.observed < MAX_OBSERVATIONS);
    work_observation *item = &work_fixture.observations[work_fixture.observed++];
    execution_dispatch *token = work_fixture.current;
    execution_authority proof;
    item->authority = execution_context_authority(context, &proof);
    item->account = context->work;
    item->metered = execution_dispatch_work_usage(token, &item->before, &item->limit_before);
    item->same_account = context->work == program_dispatch_work(token);
    switch (in[0].i) {
        case WORK_NOTE: break;
        case WORK_GRANT:
        case WORK_REFUSE:
            item->granted = execution_context_grant_work(context, token, 1000);
            break;
        case WORK_HOSTILE: {
            behavior_function_context copy = *context, outside = { 0 };
            uintptr_t spent = 0, limit = 0, after = 0, after_limit = 0;
            bool foreign = execution_dispatch_work_usage(fixture.tokens[1], &spent, &limit);
            item->rejected = !execution_context_grant_work(&copy, token, 1000);
            copy.parent = (behavior_function_context *)(uintptr_t)1;
            copy.inspection_user = (void *)(uintptr_t)1;
            copy.work = (execution_work *)(uintptr_t)1;
            item->rejected &= !execution_context_grant_work(&copy, token, 1000);
            item->rejected &= !execution_context_grant_work(&outside, token, 1000);
            item->rejected &= !execution_context_grant_work(NULL, token, 1000);
            item->rejected &= !execution_context_grant_work(context, fixture.tokens[1], 1000);
            item->rejected &= !execution_context_grant_work(context, token, 0);
            item->rejected &= !execution_context_grant_work(context, token, 31);
            item->rejected &= foreign &&
                              execution_dispatch_work_usage(fixture.tokens[1], &after, &after_limit) &&
                              spent == after && limit == after_limit;
            break;
        }
        case WORK_CANCEL:
            execution_dispatch_cancel(token);
            item->granted = execution_context_grant_work(context, token, 1000);
            break;
        case WORK_RELEASE:
            assert(token && token == fixture.tokens[0]);
            item->granted = execution_context_grant_work(context, token, 1000);
            execution_dispatch_free(fixture.tokens[0]);
            fixture.tokens[0] = NULL;
            break;
        default: assert(!"unknown request-work probe");
    }
    /* Even RELEASE is safe here: the current activation retains the token. */
    bool metered = execution_dispatch_work_usage(token, &item->after, &item->limit_after);
    assert(metered == item->metered);
    return NULL;
}
static bool work_observations(int first, int count, bool authority, bool metered)
{
    REQUIRE(first >= 0 && count >= 0 && first + count <= work_fixture.observed);
    for (int i = first; i < first + count; i++) {
        const work_observation *item = &work_fixture.observations[i];
        REQUIRE(item->authority == authority && item->metered == metered);
        REQUIRE(item->same_account && (item->account != NULL) == metered);
        REQUIRE(item->before == item->after);
        REQUIRE(item->before <= item->limit_before && item->after <= item->limit_after);
        if (!item->granted) {
            REQUIRE(item->limit_before == item->limit_after);
        }
    }
    return true;
}
static int work_emit(int token, int event)
{
    return execution_program_emit_tagged(
        fixture.program,
        fixture.tokens[token],
        event,
        fixture.owner,
        NULL,
        0
    );
}
static int work_pump(unsigned budget)
{
    return execution_program_pump(fixture.program, 1, 0, budget);
}
static bool work_state(int expected)
{
    const pin_value *state = execution_instance_state(fixture.instance, fixture.state_id);
    REQUIRE(state && state->type == PIN_I32 && state->i == expected);
    return true;
}
TEST_CASE(request_work_queued_grant_shares_local_imported_and_recursive_helpers)
{
    const char *source = "Authority :: behavior { Seen:int=0; "
                         "Local :: () { WorkProbe(100); Seen=Seen+1; } "
                         "Recur :: (n:int) { WorkProbe(100); Seen=Seen+1; "
                         "if n>0 { Recur(n-1); } } "
                         "on Run { WorkProbe(101); Local(); Helper.Remote(); "
                         "Recur(2); WorkProbe(100); Seen=Seen+1; } }";
    uintptr_t complete = 0;
    for (int b = 0; b < 2; b++) {
        const uintptr_t limits[] = { 32, 0, 0, 0 };
        fixture.imported = compile(
            "Helper :: library { Remote :: () { WorkProbe(100); } }",
            backends[b]
        );
        REQUIRE(fixture.imported && prepare_limits(source, backends[b], false, limits));
        REQUIRE(execution_module_import_count(fixture.module) == 1);
        work_fixture.current = fixture.tokens[0];
        REQUIRE(
            work_emit(0, RUN) == FLOW_OK &&
            execution_dispatch_pending(fixture.tokens[0])
        );
        uintptr_t spent, limit;
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[0], &spent, &limit));
        REQUIRE(!spent && limit == 32 && !work_fixture.observed);
        REQUIRE(work_pump(1) == FLOW_OK && work_fixture.observed == 7);
        REQUIRE(work_observations(0, 7, true, true));
        const work_observation *grant = &work_fixture.observations[0];
        REQUIRE(grant->granted && grant->before > 0 && grant->limit_before == 32);
        REQUIRE(grant->limit_after == 1000);
        for (int i = 1; i < 7; i++) {
            REQUIRE(work_fixture.observations[i].account == grant->account);
            REQUIRE(
                work_fixture.observations[i].before >
                work_fixture.observations[i - 1].after
            );
            REQUIRE(work_fixture.observations[i].limit_before == 1000);
        }
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[0], &spent, &limit));
        REQUIRE(spent > work_fixture.observations[6].after && spent <= 1000 && limit == 1000);
        REQUIRE(!b || spent == complete);
        complete = spent;
        REQUIRE(!execution_dispatch_pending(fixture.tokens[0]) && work_state(5));
        REQUIRE(exercised(backends[b]));
        work_cleanup();
    }
    return true;
}
TEST_CASE(request_work_exhaustion_and_reenqueue_preserve_control_fifo)
{
    const char *source = "Authority :: behavior { "
                         "Leaf :: () { i:int=0; while i<32 { i=i+1; } } "
                         "on Run { Leaf(); } "
                         "on Message(text:string) { Record(text); } "
                         "on Control { WorkProbe(100); WorkProbe(100); } }";
    for (int b = 0; b < 2; b++) {
        const uintptr_t limits[] = { 8, 0, 0, 0 };
        REQUIRE(prepare_limits(source, backends[b], false, limits));
        work_fixture.current = fixture.tokens[0];
        REQUIRE(work_emit(0, RUN) == FLOW_OK);
        char text[2][8] = { "first", "second" };
        for (int i = 0; i < 2; i++) {
            pin_value value = { .type = PIN_STR, .str = text[i] };
            REQUIRE(
                execution_program_emit_tagged(
                    fixture.program,
                    fixture.tokens[i + 1],
                    MESSAGE,
                    fixture.owner,
                    &value,
                    1
                ) == FLOW_OK
            );
            memset(text[i], '?', strlen(text[i]));
        }
        REQUIRE(work_pump(1) == FLOW_ERR_STEP_BUDGET && !fixture.recorded);
        uintptr_t spent, limit;
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[0], &spent, &limit));
        REQUIRE(spent == 8 && limit == 8);
        const execution_program_failure *error = execution_program_error(fixture.program);
        REQUIRE(error && error->kind == BEHAVIOR_FUNCTION_BUDGET);
        REQUIRE(error->status == FLOW_ERR_STEP_BUDGET);
        REQUIRE(!execution_dispatch_pending(fixture.tokens[0]));
        REQUIRE(execution_dispatch_pending(fixture.tokens[1]));
        REQUIRE(execution_dispatch_pending(fixture.tokens[2]));
        execution_program_clear_error(fixture.program);
        work_fixture.current = NULL;
        REQUIRE(work_pump(BUDGET) == FLOW_OK && fixture.recorded == 2);
        REQUIRE(!strcmp(fixture.records[0], "first") && !strcmp(fixture.records[1], "second"));
        work_fixture.current = fixture.tokens[0];
        REQUIRE(work_emit(0, RUN) == FLOW_OK);
        REQUIRE(work_pump(BUDGET) == FLOW_ERR_STEP_BUDGET);
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[0], &spent, &limit));
        REQUIRE(spent == 8 && limit == 8 && fixture.recorded == 2);
        execution_program_clear_error(fixture.program);
        work_fixture.current = fixture.tokens[3];
        REQUIRE(
            execution_program_dispatch_tagged(
                fixture.program,
                fixture.tokens[3],
                CONTROL,
                fixture.owner,
                NULL,
                0,
                0,
                0,
                1
            ) == FLOW_ERR_STEP_BUDGET
        );
        REQUIRE(!execution_dispatch_work_usage(fixture.tokens[3], &spent, &limit));
        execution_program_clear_error(fixture.program);
        REQUIRE(dispatch(3, CONTROL, 0) == FLOW_OK);
        REQUIRE(work_fixture.observed >= 2 && work_fixture.observed <= 3);
        REQUIRE(work_observations(0, work_fixture.observed, true, false));
        REQUIRE(!execution_instance_pending(fixture.instance) && exercised(backends[b]));
        work_cleanup();
    }
    return true;
}
TEST_CASE(request_work_resume_task_and_continuation_keep_balance_without_grants)
{
    const char
        *source = "Authority :: behavior { "
                  "Child :: () { WorkProbe(102); yield; WorkProbe(102); } "
                  "action ready=Ready(); after ready.Completed { "
                  "WorkProbe(102); } "
                  "on Run { WorkProbe(100); await Hold(); WorkProbe(102); } "
                  "on Wait { WorkProbe(100); child:=Child(); } "
                  "on Control { WorkProbe(100); ready(); } }";
    for (int b = 0; b < 2; b++) {
        const uintptr_t limits[] = { 1000, 1000, 1000, 0 };
        REQUIRE(prepare_limits(source, backends[b], false, limits));
        work_fixture.current = fixture.tokens[0];
        REQUIRE(dispatch(0, RUN, 0) == FLOW_OK && work_fixture.observed == 1);
        REQUIRE(fixture.request_count == 1 && execution_dispatch_pending(fixture.tokens[0]));
        uintptr_t waiting, spent, limit;
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[0], &waiting, &limit));
        REQUIRE(waiting > 0 && limit == 1000);
        REQUIRE(flow_host_complete(fixture.requests[0], -1, (pin_value){ 0 }));
        REQUIRE(flow_host_status(fixture.requests[0]) == FLOW_HOST_READY);
        /* Completion delivers the result; the host must also release its
         * reference before the finished wait and activation can retire. */
        flow_host_release(fixture.requests[0]);
        fixture.requests[0] = NULL;
        fixture.request_count = 0;
        REQUIRE(execution_dispatch_pending(fixture.tokens[0]));
        REQUIRE(work_pump(1) == FLOW_OK && work_fixture.observed == 2);
        REQUIRE(work_fixture.observations[1].before > waiting);
        REQUIRE(
            work_fixture.observations[1].account == work_fixture.observations[0].account
        );
        REQUIRE(work_observations(0, 1, true, true) && work_observations(1, 1, false, true));
        REQUIRE(!work_fixture.observations[1].granted);
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[0], &spent, &limit));
        REQUIRE(
            spent > waiting && limit == 1000 &&
            !execution_dispatch_pending(fixture.tokens[0])
        );

        work_fixture.current = fixture.tokens[1];
        REQUIRE(dispatch(1, WAIT, 1) == FLOW_OK && work_fixture.observed == 4);
        REQUIRE(work_observations(2, 1, true, true) && work_observations(3, 1, false, true));
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[1], &waiting, &limit));
        /* Yield resumes on a later update epoch, not on an event pump. */
        int resumed = execution_program_update(fixture.program, 2, 0, 1);
        if (resumed != FLOW_OK || work_fixture.observed != 5) {
            fprintf(
                stderr,
                "request-work child update: backend=%d status=%d observed=%d\n",
                backends[b],
                resumed,
                work_fixture.observed
            );
        }
        REQUIRE(resumed == FLOW_OK && work_fixture.observed == 5);
        REQUIRE(work_observations(4, 1, false, true));
        REQUIRE(
            !work_fixture.observations[3].granted &&
            !work_fixture.observations[4].granted
        );
        REQUIRE(
            work_fixture.observations[3].account == work_fixture.observations[2].account
        );
        REQUIRE(
            work_fixture.observations[4].account == work_fixture.observations[2].account
        );
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[1], &spent, &limit));
        REQUIRE(
            spent > waiting && limit == 1000 &&
            !execution_dispatch_pending(fixture.tokens[1])
        );

        work_fixture.current = fixture.tokens[2];
        REQUIRE(dispatch(2, CONTROL, 2) == FLOW_OK && work_fixture.observed == 7);
        REQUIRE(work_observations(5, 1, true, true) && work_observations(6, 1, false, true));
        REQUIRE(!work_fixture.observations[6].granted);
        REQUIRE(
            work_fixture.observations[6].account == work_fixture.observations[5].account
        );
        REQUIRE(
            work_fixture.observations[6].before > work_fixture.observations[5].after
        );
        REQUIRE(!execution_dispatch_pending(fixture.tokens[2]));
        REQUIRE(!execution_instance_pending(fixture.instance) && exercised(backends[b]));
        work_cleanup();
    }
    return true;
}
TEST_CASE(request_work_rejected_grants_cannot_change_any_balance)
{
    const char *source = "Authority :: behavior { on Run { WorkProbe(103); "
                         "WorkProbe(101); WorkProbe(102); } "
                         "on Control { WorkProbe(104); } }";
    for (int b = 0; b < 2; b++) {
        const uintptr_t limits[] = { 32, 32, 32, 0 };
        REQUIRE(prepare_limits(source, backends[b], false, limits));
        work_fixture.current = fixture.tokens[0];
        REQUIRE(dispatch(0, RUN, 0) == FLOW_OK && work_fixture.observed == 3);
        REQUIRE(work_observations(0, 3, true, true));
        REQUIRE(work_fixture.observations[0].rejected);
        REQUIRE(
            work_fixture.observations[1].granted &&
            !work_fixture.observations[2].granted
        );
        REQUIRE(work_fixture.observations[0].limit_after == 32);
        REQUIRE(work_fixture.observations[1].limit_before == 32);
        REQUIRE(work_fixture.observations[1].limit_after == 1000);
        uintptr_t spent, limit, after, after_limit;
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[0], &spent, &limit));
        behavior_function_context outside = { 0 };
        REQUIRE(!execution_context_grant_work(&outside, fixture.tokens[0], 1000));
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[0], &after, &after_limit));
        REQUIRE(spent == after && limit == after_limit);
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[1], &spent, &limit));
        REQUIRE(!spent && limit == 32);
        REQUIRE(!execution_context_grant_work(&outside, fixture.tokens[1], 1000));
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[1], &after, &after_limit));
        REQUIRE(after == spent && after_limit == limit);
        work_fixture.current = fixture.tokens[2];
        REQUIRE(dispatch(2, CONTROL, 0) == FLOW_OK && work_fixture.observed == 4);
        REQUIRE(work_observations(3, 1, true, true));
        REQUIRE(!work_fixture.observations[3].granted);
        REQUIRE(execution_dispatch_cancelled(fixture.tokens[2]));
        REQUIRE(work_pump(1) == FLOW_OK && !execution_dispatch_pending(fixture.tokens[2]));
        REQUIRE(!execution_program_error(fixture.program) && exercised(backends[b]));
        work_cleanup();
    }
    return true;
}
static void work_failure(
    const behavior_function *function,
    const behavior_function_context *context,
    int status
)
{
    assert(status == FLOW_ERR_STEP_BUDGET);
    work_fixture.failures++;
    work_fixture.failed_function = function;
    work_fixture.failed_steps = context->error_steps;
}
static bool work_definition(int index, behavior_backend backend)
{
    work_fixture.builder = behavior_function_builder_new();
    pin_value number = { .type = PIN_F64 };
    behavior_function_builder_signature(work_fixture.builder, NULL, 0, &number, index == 1 ? 0 : 1, NULL, 0);
    if (index == 2) {
        behavior_function_builder_limits(work_fixture.builder, 1, 0);
    } else {
        behavior_function_builder_emit(
            work_fixture.builder,
            (behavior_function_instruction){ .op = BF_CHECK }
        );
        behavior_function_builder_emit(
            work_fixture.builder,
            (behavior_function_instruction){ .op = BF_CHECK }
        );
        if (index == 1) {
            int call = behavior_function_builder_call(
                work_fixture.builder,
                work_fixture.definitions[0],
                NULL,
                0,
                &number,
                1
            );
            REQUIRE(call >= 0);
            behavior_function_builder_emit(
                work_fixture.builder,
                (behavior_function_instruction){ .op = BF_INVOKE, .left = call }
            );
        }
    }
    if (index != 1) {
        int value = behavior_function_builder_slot(work_fixture.builder, PIN_F64);
        behavior_function_builder_emit(
            work_fixture.builder,
            (behavior_function_instruction){ .op = BF_CONSTANT,
                                             .type = PIN_F64,
                                             .output = value,
                                             .value = { .type = PIN_F64, .d = 7.5 } }
        );
        behavior_function_builder_emit(
            work_fixture.builder,
            (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = value }
        );
    }
    behavior_function_builder_emit(
        work_fixture.builder,
        (behavior_function_instruction){ .op = BF_RETURN }
    );
    behavior_function_builder_emit(
        work_fixture.builder,
        (behavior_function_instruction){ .op = BF_END }
    );
    char error[256];
    work_fixture.definitions[index] = behavior_function_builder_finish(
        work_fixture.builder,
        error,
        sizeof error
    );
    if (!work_fixture.definitions[index]) {
        fprintf(stderr, "request-work definition: %s\n", error);
    }
    REQUIRE(work_fixture.definitions[index]);
    behavior_function_builder_free(work_fixture.builder);
    work_fixture.builder = NULL;
    REQUIRE(work_fixture.definitions[index]->root_steps == (index == 2 ? 1u : 0u));
    if (backend != BEHAVIOR_BYTECODE) {
        REQUIRE(behavior_function_compile(work_fixture.definitions[index]));
    }
    return true;
}
static int work_direct(int index, int token, behavior_backend backend, bool entry, uintptr_t budget)
{
    work_clear_call();
    const behavior_function *function = work_fixture.definitions[index];
    if (behavior_function_resumable(function)) {
        behavior_function_activation_init(function, &work_fixture.activation);
    }
    work_fixture.result = (pin_value){ .type = PIN_F64 };
    work_fixture.context = (behavior_function_context){
        .activation = &work_fixture.activation,
        .budget = budget,
        .results = &work_fixture.result,
        .work = token < 0 ? NULL : program_dispatch_work(fixture.tokens[token]),
        .failure_observer = index == 2 ? NULL : work_failure
    };
    behavior_function_bind_storage(&work_fixture.context, &work_fixture.storage);
    if (entry && backend == BEHAVIOR_SLJIT) {
        void *code = behavior_function_entry(function);
        assert(code);
        return ((int (*)(behavior_function_context *))code)(&work_fixture.context);
    }
    return behavior_function_run(function, &work_fixture.context, backend != BEHAVIOR_BYTECODE);
}
TEST_CASE(request_work_released_caller_checked_tail_cache_and_prepaid_rejection)
{
    const char
        *source = "Authority :: behavior { Seen:int=0; on Run { "
                  "WorkProbe(105); i:int=0; while i<4 { Seen=Seen+1; i=i+1; } "
                  "WorkProbe(100); } on Control { WorkProbe(100); Seen=Seen+1; "
                  "} }";
    for (int b = 0; b < 2; b++) {
        const uintptr_t limits[] = { 32, 0, 3, 3 };
        REQUIRE(prepare_limits(source, backends[b], false, limits));
        work_fixture.current = fixture.tokens[0];
        REQUIRE(dispatch(0, RUN, 0) == FLOW_OK && !fixture.tokens[0]);
        work_fixture.current = NULL; /* Last activation may have freed it. */
        REQUIRE(work_fixture.observed == 2 && work_observations(0, 2, true, true));
        REQUIRE(work_fixture.observations[0].granted);
        REQUIRE(
            work_fixture.observations[1].account == work_fixture.observations[0].account
        );
        REQUIRE(
            work_fixture.observations[1].before > work_fixture.observations[0].after
        );
        REQUIRE(work_state(4));
        REQUIRE(execution_program_memory_usage(fixture.program).activation_cache_bytes > 0);
        work_fixture.current = fixture.tokens[1];
        REQUIRE(dispatch(1, CONTROL, 0) == FLOW_OK && work_fixture.observed == 3);
        REQUIRE(work_observations(2, 1, true, false) && work_state(5));
        work_fixture.current = NULL;
        REQUIRE(
            execution_program_dispatch(fixture.program, CONTROL, fixture.owner, NULL, 0, 0, 0, BUDGET) ==
            FLOW_OK
        );
        REQUIRE(work_fixture.observed == 4 && work_observations(3, 1, false, false));
        REQUIRE(work_state(6));
        for (int i = 0; i < 3; i++) {
            REQUIRE(work_definition(i, backends[b]));
        }
        REQUIRE(work_direct(0, 2, backends[b], false, UINTPTR_MAX) == FLOW_OK);
        REQUIRE(work_fixture.context.returned);
        REQUIRE(work_fixture.result.type == PIN_F64 && work_fixture.result.d == 7.5);
        REQUIRE(work_fixture.context.steps == 2);
        uintptr_t spent, limit;
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[2], &spent, &limit));
        REQUIRE(spent == 2 && limit == 3);
        REQUIRE(work_direct(0, 2, backends[b], false, UINTPTR_MAX) == FLOW_ERR_STEP_BUDGET);
        REQUIRE(work_fixture.context.steps == 1 && work_fixture.context.error_steps == 1);
        REQUIRE(work_fixture.failures == 1 && work_fixture.failed_steps == 1);
        REQUIRE(work_fixture.failed_function == work_fixture.definitions[0]);
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[2], &spent, &limit) && spent == 3);
        REQUIRE(work_direct(0, 2, backends[b], false, UINTPTR_MAX) == FLOW_ERR_STEP_BUDGET);
        REQUIRE(work_fixture.context.steps == 0 && work_fixture.context.error_steps == 0);
        REQUIRE(work_fixture.context.failure == BEHAVIOR_FUNCTION_BUDGET);
        REQUIRE(work_fixture.failures == 2 && work_fixture.failed_steps == 0);
        REQUIRE(work_fixture.failed_function == work_fixture.definitions[0]);
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[2], &spent, &limit) && spent == 3);
        /* The legacy ceiling is checked before charging request work. */
        REQUIRE(work_direct(0, 3, backends[b], false, 1) == FLOW_ERR_STEP_BUDGET);
        REQUIRE(work_fixture.context.steps == 1 && work_fixture.context.error_steps == 1);
        REQUIRE(work_fixture.failures == 3 && work_fixture.failed_steps == 1);
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[3], &spent, &limit));
        REQUIRE(spent == 1 && limit == 3);
        REQUIRE(work_direct(1, 3, backends[b], false, UINTPTR_MAX) == FLOW_ERR_STEP_BUDGET);
        REQUIRE(work_fixture.failures == 4 && work_fixture.failed_steps == 0);
        REQUIRE(work_fixture.failed_function == work_fixture.definitions[0]);
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[3], &spent, &limit));
        REQUIRE(spent == 3 && limit == 3);
        REQUIRE(work_direct(2, 3, backends[b], true, UINTPTR_MAX) == FLOW_ERR_STATE);
        REQUIRE(work_fixture.context.failure == BEHAVIOR_FUNCTION_STATE);
        REQUIRE(work_fixture.context.error_steps == 0 && work_fixture.context.steps == 0);
        REQUIRE(
            !strcmp(work_fixture.context.error_message, "Request work policy requires canonical execution checkpoints")
        );
        REQUIRE(execution_dispatch_work_usage(fixture.tokens[3], &spent, &limit) && spent == 3);
        REQUIRE(work_direct(2, -1, backends[b], false, 1) == FLOW_OK);
        REQUIRE(work_fixture.context.returned);
        REQUIRE(work_fixture.result.type == PIN_F64 && work_fixture.result.d == 7.5);
        REQUIRE(!work_fixture.context.work && !work_fixture.context.failure);
        REQUIRE(exercised(backends[b]));
        work_cleanup();
    }
    return true;
}
void test_language_request_work()
{
    pu_setup(NULL, work_cleanup);
    RUN_TEST_CASE(request_work_queued_grant_shares_local_imported_and_recursive_helpers);
    RUN_TEST_CASE(request_work_exhaustion_and_reenqueue_preserve_control_fifo);
    RUN_TEST_CASE(request_work_resume_task_and_continuation_keep_balance_without_grants);
    RUN_TEST_CASE(request_work_rejected_grants_cannot_change_any_balance);
    RUN_TEST_CASE(request_work_released_caller_checked_tail_cache_and_prepaid_rejection);
    pu_clear_setup();
}
