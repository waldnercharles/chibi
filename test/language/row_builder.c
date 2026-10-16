#include "authoring/native_bindings.h"
#include "authoring/source_catalog_snapshot.h"
#include "backend.h"
#include "execution/definition.h"
#include "execution/inspection.h"
#include "execution/module.h"
#include "execution/prepared.h"
#include "execution/program_internal.h"
#include "execution/status.h"
#include "execution/wasm_jit.h"
#include "language/identity.h"
#include "language/lower.h"
#include "runtime/arrays.h"
#include "runtime/native_capabilities.h"
#include "support/arena.h"
#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/array_internal.h"
#include "types/copy.h"
#include "types/record.h"
#include "types/trace.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Eight fixed counted-construction cases. Exact workload and observations are
 * independently admitted before execution. Private plan/helper inspection is
 * deliberate: no test-only public API or runtime flag is introduced. */
static arena producer, storage;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static execution_module *module;
static native_capabilities *native;
static uint64_t function_id, expected_id, expected_layout;
static behavior_function_context context;
static behavior_function_activation activation;
static behavior_run_scope runs;
static pin_copy_scope copies;
static pin_value result;
static execution_module *alternate;
static execution_program *program;
static ecs_world *world;
static execution_inspector *inspector;
static arena catalog_storage;
static pin_value arguments[3];
static unsigned char *image, *decoded_bytes;
static size_t image_size;
static bool replace_binding;
static int custom_calls, observations, depth_override;
static uint64_t assignment;
static const int sizes[] = { -1, 0, 1, 4, 16 };
static const behavior_backend backends[] = { BEHAVIOR_BYTECODE, TEST_COMPILED_BACKEND };
static const type_field fields[] = {
    { .name = "key", .type = PIN_STR },
    { .name = "label", .type = PIN_STR },
    { .name = "icon", .type = PIN_STR },
    { .name = "prototype", .type = PIN_STR },
    { .name = "drop", .type = PIN_STR },
    { .name = "depth", .type = PIN_I32 },
    { .name = "behaviors", .type = PIN_I32 },
    { .name = "active", .type = PIN_BOOL },
    { .name = "marked", .type = PIN_BOOL },
    { .name = "hidden", .type = PIN_BOOL },
    { .name = "locked", .type = PIN_BOOL },
    { .name = "renaming", .type = PIN_BOOL },
};
static void retire_module()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    execution_module_free(module);
    module = NULL;
    execution_module_plan_free(plan);
    plan = NULL;
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    native_capabilities_free(native);
    native = NULL;
}
static void cleanup()
{
    if (copies.active) {
        pin_copy_scope_end(&copies);
    }
    if (runs.active) {
        behavior_run_scope_end(&runs);
    }
    if (program) {
        /* Restore the fixture-only detached link even after a failed oracle. */
        if (!program->inspector) {
            program->inspector = inspector;
        }
        execution_program_free(program);
        program = NULL;
    }
    if (inspector) {
        execution_inspector_free(inspector);
        inspector = NULL;
    }
    retire_module();
    if (world) {
        ecs_world_free(world);
        world = NULL;
    }
    execution_module_free(alternate);
    alternate = NULL;
    arena_free(&catalog_storage);
    free(decoded_bytes);
    decoded_bytes = NULL;
    free(image);
    image = NULL;
    image_size = 0;
    arena_free(&producer);
    arena_free(&storage);
    result = (pin_value){ 0 };
    replace_binding = false;
    depth_override = 0;
}
static char *text(arena *a, const char *value)
{
    size_t bytes = strlen(value) + 1;
    char *out = arena_alloc(a, bytes);
    memcpy(out, value, bytes);
    return out;
}
static const pin_record *layout(uint64_t id)
{
    return pin_record_create(&producer, "Rows.Row", id, fields, 12);
}
static pin_value member(const pin_value *value, int field)
{
    pin_value out;
    pin_read_field(value->ptr, &value->record->type.fields[field], &out);
    return out;
}
static pin_value row(const pin_record *record, int number)
{
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = record->type.name,
                        .record = record,
                        .ptr = arena_alloc(&producer, record->type.size) };
    memset(value.ptr, 0, record->type.size);
    for (int f = 0; f < 12; f++) {
        pin_value v = { .type = fields[f].type };
        if (v.type == PIN_STR) {
            char buffer[20];
            snprintf(buffer, sizeof buffer, "row-%02d-%d", number, f);
            v.str = text(&producer, buffer);
        } else if (v.type == PIN_I32) {
            v.i = number + f;
        } else {
            v.b = (number + f) % 2;
        }
        pin_write_field(value.ptr, &record->type.fields[f], v);
    }
    return value;
}
static pin_value array_header(pin_array *a, const pin_record *record, pin_value *items, int count)
{
    *a = (pin_array){ .element_type = PIN_STRUCT,
                      .type_name = record->type.name,
                      .record = record,
                      .count = count,
                      .capacity = count,
                      .items = items };
    return (pin_value){ .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .type_name = record->type.name,
                        .record = record,
                        .ptr = a };
}
static bool check_row(const pin_value *value, int number, const char *shared_text, const arena *owner)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_SINGLE);
    REQUIRE(value->record && pin_record_valid(value->record));
    REQUIRE(value->record->id == expected_id && value->record->layout == expected_layout);
    REQUIRE(value->type_name == value->record->type.name);
    REQUIRE(!strcmp(value->type_name, "Rows.Row"));
    REQUIRE(
        !owner ||
        (arena_contains(owner, value->record) && arena_contains(owner, value->ptr))
    );
    for (int f = 0; f < 12; f++) {
        pin_value v = member(value, f);
        REQUIRE(v.type == fields[f].type);
        if (v.type == PIN_STR) {
            char buffer[20];
            snprintf(buffer, sizeof buffer, "row-%02d-%d", number, f);
            REQUIRE(v.str && !strcmp(v.str, shared_text ? shared_text : buffer));
            REQUIRE(!owner || arena_contains(owner, v.str));
        } else if (v.type == PIN_I32) {
            REQUIRE(v.i == (depth_override && !number && f == 5 ? depth_override : number + f));
        } else {
            REQUIRE(v.b == (bool)((number + f) % 2));
        }
    }
    return true;
}
static bool check_array(const pin_value *value, int count, const char *shared_text)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_ARRAY);
    REQUIRE(value->record && pin_record_valid(value->record));
    REQUIRE(value->record->id == expected_id && value->record->layout == expected_layout);
    REQUIRE(value->type_name == value->record->type.name);
    REQUIRE(!strcmp(value->type_name, "Rows.Row"));
    REQUIRE(arena_contains(&storage, value->record));
    const pin_array *a = value->ptr;
    REQUIRE(a && arena_contains(&storage, a));
    REQUIRE(a->count == count && a->element_type == PIN_STRUCT);
    REQUIRE(a->record == value->record && a->type_name == value->type_name);
    REQUIRE(!a->stride); /* These rows contain managed strings. */
    REQUIRE(!count || arena_contains(&storage, a->items));
    for (int i = 0; i < count; i++) {
        REQUIRE(a->items[i].record == a->record);
        REQUIRE(check_row(&a->items[i], i, shared_text, &storage));
    }
    return true;
}
#define ROW                                                                    \
    "Row :: struct { key:string; label:string; icon:string=\"node\"; "         \
    "prototype:string=\"\"; drop:string=\"\"; depth:int=1; behaviors:int=0; "  \
    "active:bool=true; marked:bool=false; hidden:bool=false; "                 \
    "locked:bool=false; renaming:bool=false; } "
#define MAKE_ROW                                                               \
    "row:Row={key:text,label:text,icon:text,prototype:text,drop:text,"         \
    "depth:i+5,behaviors:i+6,active:i%2==0,marked:i%2==1,hidden:i%2==0,"       \
    "locked:i%2==1,renaming:i%2==0}; "
#define LOOP                                                                   \
    "rows:Row[]=[]; i:int=0; while i<count { " MAKE_ROW                        \
    "rows=array_add(rows,row); i=i+1; } "
static const char eligible[] = "Rows :: library { " ROW
                               "Build::(count:int,text:string)->Row[] { " LOOP
                               "return rows; } }";
static const behavior_function *body()
{
    for (int i = 0; i < execution_module_function_count(module); i++) {
        execution_module_function_info f = execution_module_function_at(module, i);
        if (f.id == function_id) {
            return f.body;
        }
    }
    return NULL;
}
static const char *borrow_prefix(arena *a, const pin_value *args, int count, pin_value *out)
{
    (void)a;
    if (count != 2) {
        return "fixture add arity";
    }
    custom_calls++;
    *out = args[0];
    return NULL;
}
static const node_host_call replacement = { .value = borrow_prefix };
static bool resolve(void *user, uint64_t id, script_lower_host *out)
{
    script_lower_bindings bindings = authoring_native_bindings(user);
    if (!bindings.host(bindings.user, id, out)) {
        return false;
    }
    if (replace_binding && out->call && out->call->value == pin_array_add_call) {
        out->call = &replacement;
    }
    return true;
}
static bool catalog()
{
    char error[256];
    native = native_capabilities_from_definition_packs(
        NULL,
        0,
        &NATIVE_ARRAY_DEFINITION_PACK,
        1,
        error,
        sizeof error
    );
    if (!native) {
        fprintf(stderr, "row-builder catalog: %s\n", error);
    }
    REQUIRE(native);
    return true;
}
static bool analyze(const char *source, const script_catalog *catalog)
{
    syntax = script_parse(source);
    for (int i = 0; i < syntax->diagnostic_count; i++) {
        fprintf(stderr, "row-builder parse: %s\n", syntax->diagnostics[i].message);
    }
    REQUIRE(!syntax->diagnostic_count);
    analysis = script_analyze(syntax, catalog);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(stderr, "row-builder semantic: %s\n", analysis->diagnostics[i].message);
    }
    REQUIRE(analysis->valid);
    return true;
}
static bool finish(const script_catalog *catalog, behavior_backend backend, bool event)
{
    script_lower_bindings bindings = { .user = native, .host = resolve };
    plan = script_lower(analysis, catalog, &bindings);
    for (int i = 0; plan && i < plan->diagnostic_count; i++) {
        fprintf(stderr, "row-builder lower: %s\n", plan->diagnostics[i].message);
    }
    REQUIRE(plan && plan->valid);
    char error[256];
    module = execution_module_finish(&plan, backend, error, sizeof error);
    if (!module) {
        fprintf(stderr, "row-builder finish: %s\n", error);
    }
    REQUIRE(module && execution_module_backend(module) == backend);
    REQUIRE(execution_module_function_count(module) == 1);
    execution_module_function_info f = execution_module_function_at(module, 0);
    REQUIRE(!event || f.event == 91);
    function_id = f.id;
    REQUIRE(body());
    const pin_record *shape = NULL;
    for (int i = 0; i < acount(body()->data_values); i++) {
        if (body()->data_values[i].initial.record) {
            shape = body()->data_values[i].initial.record;
        }
    }
    REQUIRE(shape && pin_record_valid(shape));
    expected_id = shape->id;
    expected_layout = shape->layout;
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    return true;
}
static bool compile(const char *source, behavior_backend backend)
{
    REQUIRE(catalog());
    REQUIRE(analyze(source, native_capabilities_catalog(native)));
    REQUIRE(finish(native_capabilities_catalog(native), backend, false));
    return true;
}
static void finish_call()
{
    behavior_function_release_host(&context);
    behavior_function_activation_free(&activation);
    context = (behavior_function_context){ 0 };
    activation = (behavior_function_activation){ 0 };
    arena_free(&storage);
}
static bool begin(int count, const char *text)
{
    finish_call();
    const behavior_function *f = body();
    REQUIRE(f && f->result_count == 1);
    result = f->result_defaults[0];
    behavior_function_prepare_results(f, &result, &storage);
    arguments[0] = (pin_value){ .type = PIN_I32, .i = count };
    arguments[1] = (pin_value){ .type = PIN_STR, .str = text };
    context = (behavior_function_context){ .activation = &activation,
                                           .arguments = arguments,
                                           .argument_count = f->argument_count,
                                           .results = &result,
                                           .budget = 10000 };
    behavior_function_bind_storage(&context, &storage);
    REQUIRE(behavior_function_bind(f, &context, NULL, 0));
    behavior_function_prepare_host(f, &context);
    if (behavior_function_resumable(f)) {
        behavior_function_activation_init(f, &activation);
    }
    return true;
}
static bool entries(behavior_backend backend, bool direct)
{
    REQUIRE(!runs.counts.overflow && !runs.counts.inspection_bytecode_fallbacks);
    if (direct) {
        /* Direct public entry bypasses runner attribution by design. The
         * explicit call, canonical copy count and result prove this boundary. */
        REQUIRE(backend == BEHAVIOR_SLJIT);
        REQUIRE(
            !runs.counts.bytecode_entries && !runs.counts.sljit_entries &&
            !runs.counts.wasm_entries
        );
    } else if (backend == BEHAVIOR_BYTECODE) {
        REQUIRE(
            runs.counts.bytecode_entries == 1 && !runs.counts.sljit_entries &&
            !runs.counts.wasm_entries
        );
    } else if (backend == BEHAVIOR_SLJIT) {
        REQUIRE(
            runs.counts.sljit_entries == 1 && !runs.counts.bytecode_entries &&
            !runs.counts.wasm_entries
        );
    } else {
        REQUIRE(
            runs.counts.wasm_entries == 1 && !runs.counts.bytecode_entries &&
            !runs.counts.sljit_entries
        );
    }
    return true;
}
static int execute(behavior_backend backend, bool direct)
{
    behavior_run_scope_reset(&runs);
    pin_copy_scope_reset(&copies);
    behavior_run_scope_begin(&runs);
    pin_copy_scope_begin(&copies);
    int status;
    if (direct) {
        int (*entry)(
            behavior_function_context *
        ) = (int (*)(behavior_function_context *))behavior_function_entry(body());
        status = entry(&context);
    } else {
        status = behavior_function_run(body(), &context, backend != BEHAVIOR_BYTECODE);
    }
    pin_copy_scope_end(&copies);
    behavior_run_scope_end(&runs);
    printf(
        "row-builder backend=%d direct=%d count=%d status=%d row_bytes=%" PRIu64
        " text=%" PRIu64 " headers=%" PRIu64 " bytecode=%" PRIu64
        " sljit=%" PRIu64 " wasm=%" PRIu64 "\n",
        backend,
        direct,
        arguments[0].i,
        status,
        copies.counts.record_bytes,
        copies.counts.text_bytes,
        copies.counts.value_header_moves,
        runs.counts.bytecode_entries,
        runs.counts.sljit_entries,
        runs.counts.wasm_entries
    );
    return status;
}
/* One bounded failure-only dump of the exact canonical input to the proof.
 * No extra preparation/invocation and no production diagnostics are added. */
static void missing_plan(const behavior_function *f)
{
    static bool printed;
    if (printed || f->rows) {
        return;
    }
    printed = true;
    static const char *names[] = {
#define OP_NAME(op) #op,
#define MATH_NAME(op, name, fn) OP_NAME(op)
        BEHAVIOR_FUNCTION_OPS(OP_NAME, MATH_NAME)
#undef MATH_NAME
#undef OP_NAME
    };
    fprintf(
        stderr,
        "row-builder missing plan: instructions=%d slots=%d data=%d hosts=%d "
        "fast=%d batchable=%d waits=%d runtime=%d\n",
        acount(f->instructions),
        acount(f->slots),
        acount(f->data_values),
        acount(f->host_calls),
        !!f->fast,
        f->batchable,
        f->waits,
        !!f->runtime_body
    );
    for (int n = 0; n < acount(f->instructions) && n < 1024; n++) {
        const behavior_function_instruction *i = &f->instructions[n];
        fprintf(
            stderr,
            "row-ir %d %s type=%d out=%d left=%d right=%d target=%d offset=%zu "
            "indirect=%d value_type=%d bits=%" PRIu64 "\n",
            n,
            names[i->op],
            i->type,
            i->output,
            i->left,
            i->right,
            i->target,
            i->offset,
            i->indirect,
            i->value.type,
            i->value.u64
        );
    }
    for (int n = 0; n < acount(f->host_calls) && n < 128; n++) {
        const behavior_function_host_call *h = &f->host_calls[n];
        fprintf(
            stderr,
            "row-host %d first=%d results=%d count=%d id=%" PRIu64
            " abi=%" PRIu64 " add=%d\n",
            n,
            h->first,
            h->result_first,
            h->result_count,
            h->capability_id,
            h->capability_abi,
            h->call->value == pin_array_add_call
        );
    }
    for (int n = 0; n < acount(f->data_values) && n < 256; n++) {
        const behavior_function_data_value *v = &f->data_values[n];
        fprintf(
            stderr,
            "row-bank %d type=%d container=%d kind=%d nominal=%" PRIu64
            " layout=%" PRIu64 "\n",
            n,
            v->initial.type,
            v->initial.container,
            v->kind,
            v->initial.record ? v->initial.record->id : 0,
            v->initial.record ? v->initial.record->layout : 0
        );
    }
}
static bool derived(const behavior_function *f)
{
    missing_plan(f);
    REQUIRE(f->rows && f->runtime_body);
    int add = 0, retain = 0;
    for (int i = 0; i < acount(f->instructions); i++) {
        if (f->instructions[i].op == BF_HOST_CALL) {
            REQUIRE(!f->instructions[i].right);
        }
        if (f->runtime_body[i].op == BF_HOST_CALL) {
            add += f->runtime_body[i].right == BF_HOST_BUILD_ROW;
            retain += f->runtime_body[i].right == BF_HOST_RETAIN_ROWS;
        }
    }
    REQUIRE(add == 1 && retain == 1);
    return true;
}
TEST_CASE(row_builder_eligible_projection)
{
    const uint64_t rows[] = { 0, 0, 5, 17, 65 },
                   text_bytes[] = { 0, 0, 225, 900, 3600 },
                   headers[] = { 4, 4, 21, 69, 261 };
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile(eligible, backends[b]));
        REQUIRE(derived(body()));
        for (int n = 0; n < 5; n++) {
            char input[] = "producer";
            REQUIRE(begin(sizes[n], input));
            REQUIRE(behavior_function_rows_allowed(body(), &context) == (sizes[n] >= 0));
            REQUIRE(execute(backends[b], false) == FLOW_OK && context.returned);
            REQUIRE(entries(backends[b], false));
            REQUIRE(!copies.counts.overflow);
            REQUIRE(copies.counts.record_bytes == rows[n] * result.record->type.size);
            REQUIRE(copies.counts.text_bytes == text_bytes[n]);
            REQUIRE(copies.counts.value_header_moves == headers[n]);
            REQUIRE(!copies.counts.nominal_bytes && !copies.counts.value_buffer_bytes);
            memset(input, 'x', sizeof input - 1);
            REQUIRE(check_array(&result, sizes[n] > 0 ? sizes[n] : 0, "producer"));
        }
        retire_module();
        REQUIRE(check_array(&result, 16, "producer"));
    }
    return true;
}
TEST_CASE(row_builder_static_fallback)
{
    static const char *sources[] = {
        "Rows :: library { " ROW
        "Build::(count:int,text:string)->Row[] { rows:Row[]=[]; "
        "prefix:Row[]=[]; i:int=0; while i<count { " MAKE_ROW
        "rows=array_add(rows,row); if i==0 {prefix=rows;} i=i+1;} "
        "rows[0].key=\"changed\";return prefix;} }",
        "Rows :: library { " ROW "Build::(count:int,text:string)->Row[] { " LOOP
        "rows[0].depth=37;return rows;} }",
        "Rows :: library { " ROW "Build::(count:int,text:string)->Row[] { "
        "rows:Row[]=[];i:int=0;while i<count { " MAKE_ROW
        "rows=array_add(rows,row);if i==1 {break;}i=i+1;}return rows;} }",
        "Rows :: library { " ROW "Build::(count:int,text:string)->Row[] { "
        "rows:Row[]=[];i:int=0;while i<count { " MAKE_ROW
        "rows=array_add(rows,row);i=i+2;}return rows;} }"
    };
    const int counts[] = { 1, 4, 2, 2 };
    for (int k = 0; k < 4; k++) {
        for (int b = 0; b < 2; b++) {
            cleanup();
            REQUIRE(compile(sources[k], backends[b]));
            REQUIRE(!body()->rows);
            char input[] = "producer";
            REQUIRE(begin(4, input));
            REQUIRE(execute(backends[b], false) == FLOW_OK && context.returned);
            REQUIRE(entries(backends[b], false));
            memset(input, 'x', sizeof input - 1);
            if (k == 1) {
                depth_override = 37;
            }
            if (k != 3) {
                REQUIRE(check_array(&result, counts[k], "producer"));
            } else {
                const pin_array *a = result.ptr;
                REQUIRE(a && a->count == 2);
                REQUIRE(check_row(&a->items[0], 0, "producer", &storage));
                REQUIRE(check_row(&a->items[1], 2, "producer", &storage));
            }
            retire_module();
            REQUIRE(result.record && pin_record_valid(result.record));
            REQUIRE(check_row(&((pin_array *)result.ptr)->items[0], 0, "producer", &storage));
        }
    }
    return true;
}
TEST_CASE(row_builder_entry_guard)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        replace_binding = true;
        custom_calls = 0;
        REQUIRE(compile(eligible, backends[b]));
        REQUIRE(!body()->rows);
        REQUIRE(begin(4, "producer"));
        REQUIRE(execute(backends[b], false) == FLOW_OK);
        REQUIRE(entries(backends[b], false));
        REQUIRE(custom_calls == 4 && check_array(&result, 0, "producer"));
        finish_call();
        alternate = module;
        module = NULL;
        native_capabilities_free(native);
        native = NULL;
        replace_binding = false;
        REQUIRE(compile(eligible, backends[b]));
        REQUIRE(derived(body()));
        for (int k = 0; k < 4; k++) {
            int count = k < 2  ? 4
                      : k == 2 ? (int)(65536 / sizeof(pin_value)) + 1
                               : INT32_MAX;
            REQUIRE(begin(count, "producer"));
            const behavior_function *saved = context.data->function;
            behavior_data_storage *original_data = context.data, *alternate_data = NULL;
            custom_calls = 0;
            if (k == 0) {
                context.data->function = NULL;
            }
            if (k == 1) {
                behavior_function_prepare_data(
                    execution_module_function_at(alternate, 0).body,
                    NULL,
                    &alternate_data
                );
                REQUIRE(alternate_data);
                context.data = alternate_data;
            }
            if (k >= 2) {
                /* BF_CHECK counts source checkpoints, not IR instructions:
                 * four entry checks plus four per completed iteration. */
                context.budget = 64;
            }
            bool declined = !behavior_function_rows_allowed(body(), &context);
            if (!declined) {
                context.data = original_data;
                context.data->function = saved;
                behavior_function_free_data(alternate_data);
                REQUIRE(declined);
            }
            bool direct = k == 0 && backends[b] == BEHAVIOR_SLJIT;
            REQUIRE(!direct || behavior_function_entry(body()));
            int status = execute(backends[b], direct);
            context.data = original_data;
            context.data->function = saved;
            behavior_function_free_data(alternate_data);
            REQUIRE(entries(backends[b], direct));
            if (k == 1 && backends[b] != BEHAVIOR_SLJIT) {
                REQUIRE(
                    status == FLOW_OK && custom_calls == 4 &&
                    check_array(&result, 0, "producer")
                );
            } else if (k < 2) {
                REQUIRE(!custom_calls);
                REQUIRE(status == FLOW_OK && check_array(&result, 4, "producer"));
                REQUIRE(copies.counts.record_bytes == 33 * result.record->type.size);
                REQUIRE(copies.counts.text_bytes == 1620 && copies.counts.value_header_moves == 89);
            } else {
                REQUIRE(status == FLOW_ERR_STEP_BUDGET && !context.returned);
                REQUIRE(!result.ptr);
                const pin_array *a = context.data->values[body()->rows->local].ptr;
                size_t used, capacity;
                arena_usage(&storage, &used, &capacity);
                printf(
                    "row-builder budget=%" PRIuPTR " prefix=%d steps=%" PRIuPTR
                    " storage_capacity=%zu\n",
                    context.budget,
                    a ? a->count : -1,
                    context.error_steps,
                    capacity
                );
                REQUIRE(a && a->count > 0 && a->count < 32 && a->capacity == a->count);
                REQUIRE(a->count == 15 && context.error_steps == 64);
                REQUIRE(capacity < 4 * 1024 * 1024);
            }
        }
    }
    return true;
}
static int observe(void *user, behavior_function_context *ctx, const behavior_source_point *point)
{
    (void)user;
    (void)ctx;
    (void)point;
    observations++;
    return observations > 512 ? FLOW_ERR_STATE : FLOW_OK;
}
TEST_CASE(row_builder_error_order)
{
    static const char *sources[] = {
        "Rows :: library { " ROW
        "Build::(count:int,text:string,indices:int[])->Row[] { "
        "rows:Row[]=[];i:int=0;while i<count { "
        "row:Row={key:text,label:text,icon:text,prototype:text,drop:text,depth:"
        "indices[i]+5,behaviors:i+6,active:i%2==0,marked:i%2==1,hidden:i%2==0,"
        "locked:i%2==1,renaming:i%2==0};rows=array_add(rows,row);i=i+1;}return "
        "rows;} }",
        "Rows :: library { " ROW "Build::(count:int,text:string)->Row[] { "
        "rows:Row[]=[];i:int=0;while i<count { "
        "row:Row={key:text,label:text,icon:text,"
        "prototype:text,drop:text,depth:5+1/"
        "(2-i),behaviors:i+6,active:i%2==0,marked:i%2==1,"
        "hidden:i%2==0,locked:i%2==1,renaming:i%2==0};"
        "rows=array_add(rows,row);i=i+1;}return rows;} }"
    };
    for (int k = 0; k < 2; k++) {
        for (int b = 0; b < 2; b++) {
            cleanup();
            REQUIRE(compile(sources[k], backends[b]));
            REQUIRE(derived(body()));
            int first_status = 0, first_source = 0;
            behavior_function_failure first_failure = 0;
            uint64_t first_node = 0;
            bool first_has_message = false;
            char first_message[256] = { 0 };
            pin_value numbers[] = { { .type = PIN_I32, .i = 0 },
                                    { .type = PIN_I32, .i = 1 } };
            pin_array indices = { .element_type = PIN_I32,
                                  .count = 2,
                                  .capacity = 2,
                                  .items = numbers };
            arguments[2] = (pin_value){ .type = PIN_I32,
                                        .container = PIN_CONTAINER_ARRAY,
                                        .ptr = &indices };
            for (int inspected = 0; inspected < 2; inspected++) {
                REQUIRE(begin(4, "producer"));
                pin_value before = result;
                observations = 0;
                context.source_pause = inspected ? observe : NULL;
                REQUIRE(behavior_function_rows_allowed(body(), &context) == !inspected);
                int status = execute(backends[b], false);
                REQUIRE(entries(backends[b], false));
                REQUIRE(status < 0 && status != FLOW_ERR_STEP_BUDGET && !context.returned);
                REQUIRE(status == (k ? FLOW_ERR_INVALID_GRAPH : FLOW_ERR_ARGUMENT));
                REQUIRE(context.failure == (k ? BEHAVIOR_FUNCTION_DATA : BEHAVIOR_FUNCTION_ARGUMENT));
                REQUIRE(!memcmp(&result, &before, sizeof result));
                REQUIRE(observations <= 512 && (!inspected || observations > 0));
                pin_value prefix = context.data->values[body()->rows->local];
                const pin_array *a = prefix.ptr;
                REQUIRE(a && a->count == 2);
                /* The local bank's metadata belongs to the function; the live
                 * array and all rows belong to invocation storage. */
                prefix.record = a->record;
                prefix.type_name = a->type_name;
                REQUIRE(check_array(&prefix, 2, "producer"));
                if (!inspected) {
                    first_status = status;
                    first_source = context.error_source;
                    first_failure = context.failure;
                    first_node = context.node_id;
                    first_has_message = context.error_message != NULL;
                    if (first_has_message) {
                        REQUIRE(strlen(context.error_message) < sizeof first_message);
                        snprintf(
                            first_message,
                            sizeof first_message,
                            "%s",
                            context.error_message
                        );
                    }
                } else {
                    REQUIRE(
                        status == first_status && context.error_source == first_source &&
                        context.failure == first_failure && context.node_id == first_node
                    );
                    REQUIRE((context.error_message != NULL) == first_has_message);
                    REQUIRE(!first_has_message || !strcmp(context.error_message, first_message));
                }
            }
        }
    }
    return true;
}
TEST_CASE(row_builder_accepts_packed_seed_and_preserves_canonical_nullable_add)
{
    const type_field field = { .name = "value", .type = PIN_I32 };
    const pin_record *record = pin_record_create(&producer, "Packed.BuilderRow", 541, &field, 1);
    REQUIRE(record);
    arena_init_collectable(&storage);
    pin_array shape = { .element_type = PIN_STRUCT,
                        .type_name = record->type.name,
                        .record = record };
    pin_array *empty = pin_array_snapshot_like(&storage, &shape, 0);
    REQUIRE(empty && empty->stride && pin_array_valid(empty));
    pin_value prefix = { .type = PIN_STRUCT,
                         .container = PIN_CONTAINER_ARRAY,
                         .type_name = empty->type_name,
                         .record = empty->record,
                         .ptr = empty };
    int32_t number = 23;
    pin_value value = { .type = PIN_STRUCT,
                        .type_name = record->type.name,
                        .record = record,
                        .ptr = &number };
    for (int i = 0; i < 2; i++) {
        REQUIRE(!pin_array_build_row(&storage, &prefix, &value, 2, &prefix));
    }
    const pin_array *built = prefix.ptr;
    REQUIRE(built && !built->stride && built->count == 2);
    pin_value arguments[] = { prefix, value }, packed;
    REQUIRE(!pin_array_add_call(&storage, arguments, 2, &packed));
    REQUIRE(((pin_array *)packed.ptr)->stride && ((pin_array *)packed.ptr)->count == 3);
    arguments[0] = packed;
    arguments[1].ptr = NULL;
    pin_value nullable;
    REQUIRE(!pin_array_add_call(&storage, arguments, 2, &nullable));
    number = 99;
    arena_free(&producer);
    arena_collection collection;
    arena *arenas[] = { &storage };
    arena_collect_begin(&collection, arenas, 1);
    pin_value_trace(&prefix, arena_collect_mark, &collection);
    pin_value_trace(&nullable, arena_collect_mark, &collection);
    arena_collect_end(&collection);
    const pin_array *result = nullable.ptr;
    REQUIRE(result && !result->stride && result->count == 4 && pin_array_valid(result));
    for (int i = 0; i < 3; i++) {
        pin_value row = pin_array_get(result, i), member;
        pin_read_field(row.ptr, &row.record->type.fields[0], &member);
        REQUIRE(member.i == 23);
    }
    REQUIRE(!pin_array_get(result, 3).ptr && built->count == 2);
    pin_value row = pin_array_get(built, 0), member;
    pin_read_field(row.ptr, &row.record->type.fields[0], &member);
    REQUIRE(member.i == 23);
    return true;
}
TEST_CASE(row_builder_roots_and_repetition)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile(eligible, backends[b]));
        REQUIRE(derived(body()));
        for (int n = 0; n < 2; n++) {
            char input[] = "producer";
            REQUIRE(begin(4, input));
            REQUIRE(behavior_function_rows_allowed(body(), &context));
            REQUIRE(execute(backends[b], false) == FLOW_OK);
            REQUIRE(entries(backends[b], false));
            REQUIRE(copies.counts.record_bytes == 17 * result.record->type.size);
            memset(input, 'x', sizeof input - 1);
            REQUIRE(check_array(&result, 4, "producer"));
        }
        retire_module();
        REQUIRE(check_array(&result, 4, "producer"));
    }
    cleanup();
    arena_init_collectable(&storage);
    const pin_record *shape = layout(91);
    expected_id = shape->id;
    expected_layout = shape->layout;
    pin_value values[4];
    for (int i = 0; i < 4; i++) {
        values[i] = row(shape, i);
    }
    pin_array empty;
    pin_value start = array_header(&empty, shape, NULL, 0), prefix = start,
              first = { 0 };
    for (int i = 0; i < 3; i++) {
        pin_value old = prefix;
        REQUIRE(!pin_array_build_row(&storage, &prefix, &values[i], 4, &prefix));
        REQUIRE(((pin_array *)old.ptr)->count == i && old.ptr != prefix.ptr);
        REQUIRE(!i || ((pin_array *)old.ptr)->items == ((pin_array *)prefix.ptr)->items);
        REQUIRE(check_array(&prefix, i + 1, NULL));
        if (!i) {
            first = prefix;
        }
    }
    const pin_record *bad_shape = layout(91), *foreign = layout(92);
    ((pin_record *)bad_shape)->type.size++;
    pin_value bad = values[3];
    bad.record = bad_shape;
    bad.type_name = bad_shape->type.name;
    pin_value sentinel = { .type = PIN_I32, .i = 771 }, out = sentinel;
    REQUIRE(pin_array_build_row(&storage, &prefix, &bad, 4, &out) == -1);
    REQUIRE(!memcmp(&out, &sentinel, sizeof out) && check_array(&prefix, 3, NULL));
    bad = values[3];
    bad.record = foreign;
    bad.type_name = foreign->type.name;
    REQUIRE(pin_array_build_row(&storage, &prefix, &bad, 4, &out) == -1);
    REQUIRE(!memcmp(&out, &sentinel, sizeof out) && check_array(&prefix, 3, NULL));
    for (int i = 0; i < 3; i++) {
        for (int f = 0; f < 5; f++) {
            memset((char *)member(&values[i], f).str, 'x', 8);
        }
    }
    arena *arenas[] = { &storage };
    arena_collection collection;
    arena_collect_begin(&collection, arenas, 1);
    arena_collect_mark(&collection, first.ptr);
    arena_collect_mark(&collection, prefix.ptr);
    arena_collect_end(&collection);
    REQUIRE(((pin_array *)prefix.ptr)->capacity == 4 && check_array(&prefix, 3, NULL));
    REQUIRE(check_array(&first, 1, NULL));
    REQUIRE(!pin_array_build_row(&storage, &prefix, &values[3], 4, &prefix));
    arena_free(&producer);
    REQUIRE(check_array(&prefix, 4, NULL));
    REQUIRE(check_array(&first, 1, NULL));
    arena_collect_begin(&collection, arenas, 1);
    arena_collect_end(&collection);
    size_t used, capacity;
    arena_usage(&storage, &used, &capacity);
    REQUIRE(!used);
    return true;
}
static bool compile_observed(behavior_backend backend)
{
    REQUIRE(catalog());
    REQUIRE(analyze("Rows :: library { " ROW " }", native_capabilities_catalog(native)));
    REQUIRE(analysis->type_count == 1);
    static const script_catalog_parameter parameters[] = {
        { .name = "count", .type = { .storage = PIN_I32 } },
        { .name = "text", .type = { .storage = PIN_STR } }
    };
    script_catalog_capability *capabilities = NULL;
    const script_catalog *base = native_capabilities_catalog(native);
    for (int i = 0; i < base->capability_count; i++) {
        apush(capabilities, base->capabilities[i]);
    }
    apush(
        capabilities,
        ((script_catalog_capability){ .id = 91,
                                      .name = "Build",
                                      .kind = SCRIPT_CAPABILITY_EVENT,
                                      .parameters = parameters,
                                      .parameter_count = 2 })
    );
    script_catalog input = *base, owned;
    input.types = analysis->types;
    input.type_count = 1;
    input.capabilities = capabilities;
    input.capability_count = acount(capabilities);
    bool snap = source_catalog_snapshot(&catalog_storage, &input, &owned);
    afree(capabilities);
    REQUIRE(snap);
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
    static const char
        source[] = "Project :: behavior { on Build(count,text) { "
                   "rows:Rows.Row[]=[];i:int=0;while i<count { "
                   "row:Rows.Row={key:text,label:text,icon:text,prototype:text,"
                   "drop:text,depth:i+5,behaviors:i+6,active:i%2==0,marked:i%2="
                   "=1,hidden:i%2==0,locked:i%2==1,renaming:i%2==0};rows=array_"
                   "add(rows,row);i=i+1;} } }";
    REQUIRE(analyze(source, &owned));
    assignment = 0;
    for (int i = 0; i < analysis->symbol_count; i++) {
        if (analysis->symbols[i].kind == SCRIPT_LOCAL &&
            !strcmp(analysis->symbols[i].name, "rows")) {
            assignment = script_record_id(analysis, SCRIPT_RECORD_SYMBOL, i);
        }
    }
    REQUIRE(assignment);
    REQUIRE(finish(&owned, backend, true));
    arena_free(&catalog_storage);
    REQUIRE(derived(body()));
    return true;
}
static bool watched(const pin_value *value, int count)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_ARRAY);
    REQUIRE(value->record && pin_record_valid(value->record));
    REQUIRE(value->record->id == expected_id && value->record->layout == expected_layout);
    REQUIRE(value->type_name == value->record->type.name);
    const pin_array *a = value->ptr;
    REQUIRE(
        a && a->count == count && a->record == value->record &&
        a->type_name == value->type_name
    );
    for (int i = 0; i < count; i++) {
        REQUIRE(check_row(&a->items[i], i, "producer", NULL));
    }
    return true;
}
static bool watch_pair(int old_count, int new_count, int *old_index, int *new_index)
{
    *old_index = *new_index = -1;
    int count = execution_inspector_watch_count(inspector);
    REQUIRE(count > 0 && count <= 512);
    for (int i = 0; i < count; i++) {
        const execution_debug_watch *w = execution_inspector_watch_at(inspector, i);
        if (!w->value.record) {
            continue;
        }
        if (w->source.reference) {
            REQUIRE(w->availability == EXECUTION_WATCH_UNCONSUMED_REFERENCE);
            continue;
        }
        REQUIRE(w->availability == EXECUTION_WATCH_AVAILABLE);
        if (w->value.container != PIN_CONTAINER_ARRAY || !w->value.ptr) {
            continue;
        }
        const pin_array *a = w->value.ptr;
        if (a->count == old_count) {
            REQUIRE(watched(&w->value, old_count));
            *old_index = i;
        }
        if (a->count == new_count) {
            REQUIRE(watched(&w->value, new_count));
            *new_index = i;
        }
    }
    REQUIRE(*old_index >= 0 && *new_index >= 0);
    return true;
}
TEST_CASE(row_builder_inspected_program)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile_observed(backends[b]));
        /* A detached, coherent binding proves this event body would otherwise
         * use the plan. No invocation is made by this eligibility preflight. */
        arguments[0] = (pin_value){ .type = PIN_I32, .i = 4 };
        arguments[1] = (pin_value){ .type = PIN_STR, .str = "producer" };
        context = (behavior_function_context){ .arguments = arguments,
                                               .argument_count = 2 };
        behavior_function_bind_storage(&context, &storage);
        REQUIRE(behavior_function_bind(body(), &context, NULL, 0));
        behavior_function_prepare_host(body(), &context);
        REQUIRE(behavior_function_rows_allowed(body(), &context));
        finish_call();
        world = ecs_world_new();
        REQUIRE(world);
        uint64_t owner = ecs_entity_to_u64(ecs_world_create_entity(world));
        REQUIRE(ecs_world_apply_commands(world) == ECS_OK);
        program = execution_program_new(module, world, NULL);
        REQUIRE(program);
        execution_instance *instance = execution_program_attach(program, owner);
        REQUIRE(instance);
        inspector = execution_program_inspector(program);
        REQUIRE(inspector && execution_inspector_select_owner(inspector, owner));
        int saved_old = -1, saved_new = -1;
        for (int invocation = 0; invocation < 2; invocation++) {
            char input[] = "producer";
            arguments[1].str = input;
            if (invocation) {
                REQUIRE(execution_inspector_breakpoint(
                    inspector,
                    execution_module_id(module),
                    function_id,
                    assignment,
                    true
                ));
            }
            behavior_run_scope_reset(&runs);
            behavior_run_scope_begin(&runs);
            int status = execution_instance_dispatch(instance, 91, arguments, 2, invocation, 0, 10000);
            behavior_run_scope_end(&runs);
            REQUIRE(status == FLOW_OK);
            REQUIRE(entries(backends[b], false));
            REQUIRE(
                backends[b] == BEHAVIOR_BYTECODE ||
                (backends[b] == BEHAVIOR_SLJIT
                     ? runs.counts.inspected_sljit_entries == 1
                     : runs.counts.inspected_wasm_entries == 1)
            );
            if (!invocation) {
                REQUIRE(!execution_program_paused(program));
                int old_index, new_index;
                REQUIRE(watch_pair(3, 4, &old_index, &new_index));
                continue;
            }
            REQUIRE(execution_program_paused(program));
            REQUIRE(execution_inspector_cursor(inspector).point.occurrence == assignment);
            REQUIRE(acount(instance->activations) == 1);
            execution_activation *active = instance->activations[0];
            const behavior_row_plan *p = body()->rows;
            REQUIRE(!active->context.data->values[p->local].ptr);
            behavior_run_scope_reset(&runs);
            behavior_run_scope_begin(&runs);
            status = execution_inspector_control(inspector, EXECUTION_DEBUG_RESUME);
            behavior_run_scope_end(&runs);
            REQUIRE(status == FLOW_PAUSED_RESULT && execution_program_paused(program));
            REQUIRE(entries(backends[b], false));
            REQUIRE(execution_inspector_cursor(inspector).point.occurrence == assignment);
            pin_value old = active->context.data->values[p->local];
            const behavior_function_host_call *add = &body()->host_calls[p->add];
            pin_value next = active->context.data->values[add->result_first];
            REQUIRE(old.ptr && ((pin_array *)old.ptr)->count == 0);
            /* Reading rows on the RHS emits the same symbol occurrence before
             * add. Resume exactly once more to the pre-assignment symbol point. */
            REQUIRE(!next.ptr);
            behavior_run_scope_reset(&runs);
            behavior_run_scope_begin(&runs);
            status = execution_inspector_control(inspector, EXECUTION_DEBUG_RESUME);
            behavior_run_scope_end(&runs);
            REQUIRE(status == FLOW_PAUSED_RESULT && execution_program_paused(program));
            REQUIRE(entries(backends[b], false));
            REQUIRE(execution_inspector_cursor(inspector).point.occurrence == assignment);
            old = active->context.data->values[p->local];
            next = active->context.data->values[add->result_first];
            REQUIRE(old.ptr && ((pin_array *)old.ptr)->count == 0);
            REQUIRE(next.ptr && ((pin_array *)next.ptr)->count == 1);
            REQUIRE(
                old.ptr != next.ptr &&
                !behavior_function_rows_allowed(body(), &active->context)
            );
            REQUIRE(watch_pair(0, 1, &saved_old, &saved_new));
            REQUIRE(
                execution_inspector_breakpoint(inspector, execution_module_id(module), function_id, assignment, false)
            );
            /* Private fixture setup detaches callback installation while using
             * the real inspector-owned paused activation/resume. The public
             * recording switch does not detach these callbacks. */
            program->inspector = NULL;
            active->context.source_pause = NULL;
            active->context.source_capture = NULL;
            REQUIRE(behavior_function_inspection_required(body(), &active->context));
            REQUIRE(!behavior_function_rows_allowed(body(), &active->context));
            behavior_run_scope_reset(&runs);
            behavior_run_scope_begin(&runs);
            status = execution_inspector_control(inspector, EXECUTION_DEBUG_RESUME);
            behavior_run_scope_end(&runs);
            program->inspector = inspector;
            REQUIRE(status == FLOW_OK);
            REQUIRE(entries(backends[b], false));
            memset(input, 'x', sizeof input - 1);
        }
        execution_program_free(program);
        program = NULL;
        retire_module();
        REQUIRE(execution_inspector_cursor(inspector).retired);
        REQUIRE(watched(&execution_inspector_watch_at(inspector, saved_old)->value, 0));
        REQUIRE(watched(&execution_inspector_watch_at(inspector, saved_new)->value, 1));
    }
    return true;
}
static bool prepared_host(void *user, uint64_t id, execution_prepared_host *out)
{
    const native_capability_binding *binding = native_capabilities_find(user, id, 0);
    if (!binding || !binding->call) {
        return false;
    }
    *out = (execution_prepared_host){ .call = replace_binding && binding->call->value == pin_array_add_call
                                                ? &replacement
                                                : binding->call,
                                      .access = binding->access,
                                      .abi = binding->abi };
    return true;
}
TEST_CASE(row_builder_prepared_rebinding)
{
    for (int b = 0; b < 2; b++) {
        cleanup();
        REQUIRE(compile(eligible, backends[b]));
        REQUIRE(derived(body()));
        char error[256];
        REQUIRE(execution_prepared_write(module, &image, &image_size, error, sizeof error));
        REQUIRE(image && image_size > 32 && image_size < 1024 * 1024);
        execution_module_free(module);
        module = NULL;
        for (int custom = 0; custom < 2; custom++) {
            replace_binding = custom;
            custom_calls = 0;
            decoded_bytes = malloc(image_size);
            REQUIRE(decoded_bytes);
            memcpy(decoded_bytes, image, image_size);
            execution_prepared_linker linker = { .user = native, .host = prepared_host };
            module = execution_prepared_read(decoded_bytes, image_size, &linker, backends[b], error, sizeof error);
            if (!module) {
                fprintf(stderr, "row-builder prepared: %s\n", error);
            }
            REQUIRE(module);
            memset(decoded_bytes, 0xa5, image_size);
            free(decoded_bytes);
            decoded_bytes = NULL;
            REQUIRE(body() && (bool)body()->rows == !custom);
            if (!custom) {
                REQUIRE(derived(body()));
            }
            char input[] = "producer";
            REQUIRE(begin(4, input));
            REQUIRE(execute(backends[b], false) == FLOW_OK && context.returned);
            REQUIRE(entries(backends[b], false));
            REQUIRE(custom_calls == (custom ? 4 : 0));
            if (!custom) {
                REQUIRE(copies.counts.record_bytes == 17 * result.record->type.size);
            }
            memset(input, 'x', sizeof input - 1);
            REQUIRE(check_array(&result, custom ? 0 : 4, "producer"));
            behavior_function_release_host(&context);
            behavior_function_activation_free(&activation);
            context = (behavior_function_context){ 0 };
            activation = (behavior_function_activation){ 0 };
            execution_module_free(module);
            module = NULL;
            REQUIRE(check_array(&result, custom ? 0 : 4, "producer"));
        }
    }
    return true;
}
void test_language_row_builder()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(row_builder_eligible_projection);
    RUN_TEST_CASE(row_builder_static_fallback);
    RUN_TEST_CASE(row_builder_entry_guard);
    RUN_TEST_CASE(row_builder_error_order);
    RUN_TEST_CASE(row_builder_roots_and_repetition);
    RUN_TEST_CASE(row_builder_accepts_packed_seed_and_preserves_canonical_nullable_add);
    RUN_TEST_CASE(row_builder_inspected_program);
    RUN_TEST_CASE(row_builder_prepared_rebinding);
}
