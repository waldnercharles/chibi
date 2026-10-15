#include "ecs/execution.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/intrinsics.h"
#include "execution/status.h"
#include "export_compiler.h"
#include "language/lower.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "test/tools/fixture.h"
#include "third_party/pico_headers/pico_unit.h"

#include <assert.h>
#include <dlfcn.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum
{
    ALIAS_FIXTURES = 12,
    BORROW_FIXTURE = ALIAS_FIXTURES + 1,
    BORROW_CALL_FIXTURE = BORROW_FIXTURE + 1,
    READ_FIXTURE = BORROW_CALL_FIXTURE + 1,
    READ_FIXTURES = 3,
    FIXTURES = READ_FIXTURE + READ_FIXTURES
};
static behavior_function *functions[FIXTURES];
static behavior_function_builder *builder;
static char directory[128];
static FILE *source;
static void *library;
static script_syntax *syntax;
static script_analysis *analysis;
static execution_module_plan *plan;
static behavior_function_context pause_context;
static behavior_function_activation pause_activation;
static arena pause_storage;

static void release_pause()
{
    behavior_function_release_host(&pause_context);
    behavior_function_activation_free(&pause_activation);
    arena_free(&pause_storage);
    pause_context = (behavior_function_context){ 0 };
}

static void cleanup()
{
    release_pause();
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    plan = NULL;
    analysis = NULL;
    syntax = NULL;
    if (source) {
        fclose(source);
    }
    source = NULL;
    if (library) {
        dlclose(library);
    }
    library = NULL;
    if (builder) {
        behavior_function_builder_free(builder);
    }
    builder = NULL;
    for (int i = 0; i < FIXTURES; i++) {
        behavior_function_free(functions[i]);
        functions[i] = NULL;
    }
    if (directory[0]) {
        tool_remove_tree(directory);
    }
    directory[0] = 0;
}
static pin_value integer(pin_type type, int64_t number)
{
    pin_value value = { .type = type };
    if (type == PIN_I32) {
        value.i = (int32_t)number;
    } else {
        value.i64 = number;
    }
    return value;
}
static int64_t number(pin_value value)
{
    return value.type == PIN_I32 ? value.i : value.i64;
}
static bool fixture(int index)
{
    pin_type type = index < 6 ? PIN_I32 : PIN_I64;
    bool modulo = index % 6 >= 3;
    int alias = index % 3;
    pin_value values[] = { { .type = type }, { .type = type } };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, values, 2, values, 2, NULL, 0);
    int operands[2];
    for (int i = 0; i < 2; i++) {
        operands[i] = behavior_function_builder_slot(builder, type);
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_ARGUMENT,
                                             .type = type,
                                             .output = operands[i],
                                             .left = i,
                                             .value = values[i] }
        );
    }
    int output = alias == 2 ? behavior_function_builder_slot(builder, type)
                            : operands[1 - alias];
    int secondary = alias == 2 ? output : operands[alias];
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = modulo ? BF_MOD : BF_DIV,
                                         .type = type,
                                         .output = output,
                                         .left = operands[0],
                                         .right = operands[1],
                                         .target = secondary }
    );
    int results[] = { output, secondary };
    for (int i = 0; i < 2; i++) {
        behavior_function_builder_emit(
            builder,
            (
                behavior_function_instruction
            ){ .op = BF_RESULT, .type = type, .left = results[i], .target = i }
        );
    }
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_RETURN });
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    functions[index] = behavior_function_builder_finish(builder, error, sizeof error);
    if (!functions[index]) {
        fprintf(stderr, "C export fixture: %s\n", error);
    }
    REQUIRE(functions[index]);
    behavior_function_builder_free(builder);
    builder = NULL;
    char name[40];
    snprintf(name, sizeof name, "export_alias_%d", index);
    behavior_function_write_c(functions[index], source, name);
    return true;
}
static int inspect(void *user, const behavior_function_point *point)
{
    (void)user;
    (void)point;
    return 0;
}

static bool pause_fixture()
{
    const script_catalog catalog = { 0 };
    const script_lower_bindings bindings = { 0 };
    syntax = script_parse(
        "library Scalar {\n"
        "Eval :: pure (amount: number) -> number {\n"
        "value: number | Cancelled = amount;\n"
        "if value is number found { return found; }\n"
        "return 0; } }"
    );
    REQUIRE(syntax && !syntax->diagnostic_count);
    analysis = script_analyze(syntax, &catalog);
    REQUIRE(analysis && analysis->valid);
    plan = script_lower(analysis, &catalog, &bindings);
    REQUIRE(plan && plan->valid);
    for (int i = 0; i < plan->function_count; i++) {
        behavior_function *body = plan->functions[i].body;
        if (body && body->argument_count == 1 && body->result_count == 1) {
            functions[ALIAS_FIXTURES] = body;
            plan->functions[i].body = NULL;
            break;
        }
    }
    REQUIRE(functions[ALIAS_FIXTURES]);
    behavior_function_write_c(functions[ALIAS_FIXTURES], source, "export_pause");
    return true;
}

static int pause_line(void *, behavior_function_context *, const behavior_source_point *point)
{
    return point->line == 4 && point->kind == BEHAVIOR_SOURCE_STATEMENT;
}

static bool resumes_exported_pause()
{
    typedef int (*entry)(behavior_function_context *);
    entry generated = (entry)dlsym(library, "export_pause");
    REQUIRE(generated);
    behavior_function *f = functions[ALIAS_FIXTURES];
    const double samples[] = { 3.75, -0.0 };
    for (int i = 0; i < 2; i++) {
        release_pause();
        pin_value input = { .type = PIN_F64, .d = samples[i] };
        pin_value output = { .type = PIN_F64, .d = 99 };
        behavior_function_activation_init(f, &pause_activation);
        pause_context = (behavior_function_context){
            .activation = &pause_activation,
            .arguments = &input,
            .argument_count = 1,
            .results = &output,
            .budget = 100,
            .source_pause = pause_line,
            .source_function = f,
            .source_points = f->source_points,
            .source_watches = f->source_watches,
        };
        behavior_function_bind_storage(&pause_context, &pause_storage);
        REQUIRE(behavior_function_bind(f, &pause_context, NULL, 0));
        behavior_function_prepare_host(f, &pause_context);
        REQUIRE(generated(&pause_context) == FLOW_PAUSED_RESULT);
        pause_context.source_pause = NULL;
        REQUIRE(generated(&pause_context) == FLOW_OK);
        REQUIRE(!memcmp(&output.d, &input.d, sizeof output.d));
    }
    return true;
}

static int borrow_value(behavior_function_instruction instruction, bool address)
{
    instruction.output = behavior_function_builder_slot(builder, instruction.type);
    if (address) {
        bool typed = behavior_function_builder_kind(
            builder,
            BEHAVIOR_BANK_SLOT,
            instruction.output,
            BEHAVIOR_ADDRESS
        );
        assert(typed);
        (void)typed;
    }
    behavior_function_builder_emit(builder, instruction);
    return instruction.output;
}

static bool borrow_fixture(bool caller)
{
    /* Retain the protocol bank for ownership assertions. Authored lowering
     * correctly clears consumed banks after loading the result into a slot. */
    const pin_value shape = { .type = PIN_F64, .container = PIN_CONTAINER_ARRAY };
    const pin_value output = { .type = PIN_F64 };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &shape, 1, &output, 1, NULL, 0);
    int original = borrow_value(
        (behavior_function_instruction){ .op = BF_ARRAY,
                                         .type = BF_POINTER_TYPE,
                                         .left = 0,
                                         .target = BF_ARGUMENT,
                                         .value = shape },
        true
    );
    int host = behavior_function_builder_host(
        builder,
        &EXECUTION_snapshot_call,
        &EXECUTION_value_access,
        &shape,
        1,
        &shape,
        1
    );
    uint64_t id = execution_intrinsic_id(&EXECUTION_snapshot_call);
    REQUIRE(
        behavior_function_builder_host_identity(builder, host, id, execution_intrinsic_abi(id))
    );
    int input = borrow_value(
        (behavior_function_instruction){
            .op = BF_HOST_SLOT,
            .type = BF_POINTER_TYPE,
            .left = behavior_function_builder_host_first(builder, host) },
        true
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_WRITE,
                                         .type = BF_POINTER_TYPE,
                                         .left = original,
                                         .right = input,
                                         .offset = offsetof(pin_value, d) }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_HOST_CALL,
                                         .target = host,
                                         .left = BEHAVIOR_FUNCTION_HOST_DATA }
    );
    int result = borrow_value(
        (behavior_function_instruction){
            .op = BF_HOST_SLOT,
            .type = BF_POINTER_TYPE,
            .left = behavior_function_builder_host_results(builder, host) },
        true
    );
    int held = borrow_value(
        (behavior_function_instruction){ .op = BF_LOAD,
                                         .type = BF_POINTER_TYPE,
                                         .left = result,
                                         .offset = offsetof(pin_value, d) },
        true
    );
    behavior_function_builder_root(builder, BEHAVIOR_BANK_SLOT, held, shape);
    if (caller) {
        int call = behavior_function_builder_call(builder, functions[BORROW_FIXTURE], &shape, 1, &output, 1);
        int argument = borrow_value(
            (behavior_function_instruction){
                .op = BF_HOST_SLOT,
                .type = BF_POINTER_TYPE,
                .left = behavior_function_builder_call_first(builder, call) },
            true
        );
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_WRITE,
                                             .type = BF_POINTER_TYPE,
                                             .left = held,
                                             .right = argument,
                                             .offset = offsetof(pin_value, d) }
        );
        behavior_function_builder_emit(
            builder,
            (behavior_function_instruction){ .op = BF_INVOKE, .left = call }
        );
    }
    int count = borrow_value(
        (behavior_function_instruction){ .op = BF_READ,
                                         .type = PIN_I32,
                                         .left = held,
                                         .offset = offsetof(pin_array, count) },
        false
    );
    int converted = borrow_value(
        (behavior_function_instruction){ .op = BF_CAST, .type = PIN_F64, .left = count },
        false
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_RESULT, .type = PIN_F64, .left = converted }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    int index = caller ? BORROW_CALL_FIXTURE : BORROW_FIXTURE;
    functions[index] = behavior_function_builder_finish(builder, error, sizeof error);
    if (!functions[index]) {
        fprintf(stderr, "C borrow fixture: %s\n", error);
    }
    REQUIRE(functions[index]);
    behavior_function_builder_free(builder);
    builder = NULL;
    REQUIRE(functions[index]->runtime_map);
    int guarded = 0;
    for (int i = 0; i < acount(functions[index]->runtime_body); i++) {
        const behavior_function_instruction *instruction = &functions[index]->runtime_body[i];
        guarded += instruction->op == BF_HOST_CALL &&
                   instruction->right == BF_HOST_BORROW_ARRAY;
    }
    REQUIRE(guarded == 1);
    behavior_function_write_c(functions[index], source, caller ? "export_borrow_call" : "export_borrow");
    return true;
}

static void export_call_leave(behavior_function_context *, int) {}

static bool borrows_exported_plain_arrays()
{
    typedef int (*entry)(behavior_function_context *);
    entry generated = (entry)dlsym(library, "export_borrow");
    REQUIRE(generated);
    behavior_function *f = functions[BORROW_FIXTURE];
    int result = -1, guarded = 0;
    for (int i = 0; i < acount(f->runtime_body); i++) {
        const behavior_function_instruction *instruction = &f->runtime_body[i];
        if (instruction->op == BF_HOST_CALL &&
            instruction->right == BF_HOST_BORROW_ARRAY) {
            REQUIRE(!f->instructions[i].right);
            result = f->host_calls[instruction->target].result_first;
            guarded++;
        }
    }
    REQUIRE(guarded == 1 && result >= 0);
    for (int sample = 0; sample < 5; sample++) {
        for (int inspection = 0; inspection < 2; inspection++) {
            release_pause();
            pin_value items[] = { { .type = PIN_F64, .d = 1 },
                                  { .type = PIN_F64, .d = 2 },
                                  { .type = PIN_F64, .d = 3 } };
            pin_array values = { .element_type = PIN_F64,
                                 .count = 3,
                                 .capacity = 3,
                                 .items = items };
            if (sample == 1) {
                values.element_type = PIN_STR;
                for (int item = 0; item < 3; item++) {
                    items[item] = (pin_value){ .type = PIN_STR, .str = "managed" };
                }
            } else if (sample == 2) {
                /* A plain header cannot prove that each payload is plain. */
                items[1] = (pin_value){ .type = PIN_STR, .str = "managed" };
            } else if (sample == 3) {
                /* Native inputs can alias the public saved-value bank even
                 * when this function itself never suspends. */
                int saved = acount(f->slots) + acount(f->locals) + acount(f->frames);
                REQUIRE((size_t)saved * sizeof *pause_activation.values >= sizeof(pin_value));
                pause_activation
                    .values = xcalloc((size_t)saved, sizeof *pause_activation.values);
                values.items = (pin_value *)pause_activation.values;
                values.items[0] = items[0];
                values.count = values.capacity = 1;
            }
            pin_value input = { .type = PIN_F64,
                                .container = PIN_CONTAINER_ARRAY,
                                .ptr = &values };
            pin_value output = { .type = PIN_F64, .d = -1 };
            pause_context = (behavior_function_context){
                .arguments = &input,
                .argument_count = 1,
                .results = &output,
                .activation = sample == 3 ? &pause_activation : NULL,
                .budget = 1000,
                .call_leave = sample == 4 ? export_call_leave : NULL,
                .inspect = inspection ? inspect : NULL
            };
            behavior_function_bind_storage(&pause_context, &pause_storage);
            REQUIRE(behavior_function_bind(f, &pause_context, NULL, 0));
            behavior_function_prepare_host(f, &pause_context);
            REQUIRE(generated(&pause_context) == FLOW_OK);
            REQUIRE(output.d == values.count);
            const pin_array *snapshot = pause_context.data->values[result].ptr;
            if (!sample && !inspection) {
                REQUIRE(snapshot == &values);
                REQUIRE(!arena_storage_bytes(&pause_storage));
            } else {
                REQUIRE(
                    snapshot && snapshot != &values && snapshot->count == values.count
                );
                REQUIRE(snapshot->items != values.items);
                REQUIRE(arena_storage_bytes(&pause_storage) > 0);
                if (sample == 1 || sample == 2) {
                    REQUIRE(snapshot->items[1].str != items[1].str);
                    REQUIRE(!strcmp(snapshot->items[1].str, "managed"));
                } else {
                    REQUIRE(snapshot->items[0].type == PIN_F64);
                    REQUIRE(snapshot->items[0].d == 1);
                }
            }
        }
    }
    return true;
}

static bool exported_call_snapshots_keep_the_standalone_abi()
{
    typedef int (*entry)(behavior_function_context *);
    entry generated = (entry)dlsym(library, "export_borrow_call");
    REQUIRE(generated);
    behavior_function *f = functions[BORROW_CALL_FIXTURE];
    REQUIRE(f->readonly_collections);
    release_pause();
    pin_value item = { .type = PIN_F64, .d = 7 }, output = { .type = PIN_F64 };
    pin_array array = { .element_type = PIN_F64, .count = 1, .capacity = 1, .items = &item };
    pin_value input = { .type = PIN_F64, .container = PIN_CONTAINER_ARRAY, .ptr = &array };
    behavior_function_activation_init(f, &pause_activation);
    pause_context = (behavior_function_context){ .arguments = &input,
                                                 .argument_count = 1,
                                                 .results = &output,
                                                 .activation = &pause_activation,
                                                 .budget = 1000 };
    behavior_function_bind_storage(&pause_context, &pause_storage);
    REQUIRE(behavior_function_bind(f, &pause_context, NULL, 0));
    behavior_function_prepare_host(f, &pause_context);
    REQUIRE(generated(&pause_context) == BEHAVIOR_FUNCTION_CALL_RESULT);
    const pin_array *held = pause_context.data->values[f->calls[0].first].ptr;
    REQUIRE(held && held != &array && held->items != &item);
    item.d = 99;
    REQUIRE(held->items[0].d == 7);
    return true;
}

#define READ_COMPONENT UINT64_C(0x123456789abcdef0)
#define READ_LAYOUT UINT64_C(0x23456789abcdef01)
#define READ_ENTITY ((UINT64_C(7) << 32) | 1)

typedef struct
{
    int prefix;
    bool enabled;
    uint64_t wide;
    double real;
} export_read_row;
static const struct
{
    pin_type type;
    size_t offset, size;
} read_fields[READ_FIXTURES] = {
    { PIN_BOOL, offsetof(export_read_row, enabled), sizeof(bool) },
    { PIN_U64, offsetof(export_read_row, wide), sizeof(uint64_t) },
    { PIN_F64, offsetof(export_read_row, real), sizeof(double) }
};

typedef struct
{
    export_read_row rows[2];
    uint32_t generations[2];
    unsigned char occupied[2];
    uint64_t membership;
    int lookup[2];
    ecs_execution_column column;
    ecs_execution_view view;
} export_read_world;

static void read_world(export_read_world *world, int sample)
{
    *world = (export_read_world){ 0 };
    world->rows[1].enabled = !sample;
    world->rows[1].wide = sample ? UINT64_C(0x8000000000000001)
                                 : UINT64_C(0xfedcba9876543210);
    uint64_t bits = sample ? UINT64_C(0x7ff8123456789abc)
                           : UINT64_C(0x8000000000000000);
    memcpy(&world->rows[1].real, &bits, sizeof bits);
    world->generations[1] = 7;
    world->occupied[1] = 1;
    world->membership = 2;
    world->column = (ecs_execution_column){ .id = READ_COMPONENT,
                                            .layout = READ_LAYOUT,
                                            .stride = sizeof(export_read_row),
                                            .data = world->rows,
                                            .occupancy = &world->membership,
                                            .count = 1,
                                            .singleton = 1 };
    uint64_t hash = READ_COMPONENT * UINT64_C(11400714819323198485);
    world->lookup[(hash ^ (hash >> 32)) & 1] = 1;
    world->view = (ecs_execution_view){ .generations = world->generations,
                                        .occupied = world->occupied,
                                        .slot_count = 2,
                                        .columns = &world->column,
                                        .lookup = world->lookup,
                                        .capacity = 2 };
}

static bool read_fixture(int field)
{
    pin_value input = { .type = PIN_HANDLE },
              output = { .type = read_fields[field].type };
    builder = behavior_function_builder_new();
    behavior_function_builder_signature(builder, &input, 1, &output, 1, NULL, 0);
    int entity = behavior_function_builder_slot(builder, PIN_HANDLE);
    int result = behavior_function_builder_slot(builder, output.type);
    int origin = behavior_function_builder_origin(
        builder,
        (behavior_function_source){ .asset_name = "ExportRead",
                                    .parent = -1,
                                    .start = 7,
                                    .end = 19,
                                    .line = 2,
                                    .column = 3 }
    );
    behavior_function_builder_emit(
        builder,
        (
            behavior_function_instruction
        ){ .op = BF_ARGUMENT, .type = PIN_HANDLE, .output = entity, .value = input }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_CHECK });
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){
            .op = BF_COMPONENT_READ,
            .type = output.type,
            .output = result,
            .left = entity,
            .component = behavior_function_builder_component(builder, READ_COMPONENT, false, false),
            .value = { .type = PIN_U64, .u64 = READ_COMPONENT },
            .layout = READ_LAYOUT,
            .offset = sizeof(export_read_row),
            .target = (int)read_fields[field].offset,
            .source = origin,
            .pin = 23 + field,
            .node_id = UINT64_C(0x3456789abcdef012) + (uint64_t)field,
            .message = "must use the component read diagnostic" }
    );
    behavior_function_builder_emit(
        builder,
        (behavior_function_instruction){ .op = BF_RESULT, .type = output.type, .left = result }
    );
    behavior_function_builder_emit(builder, (behavior_function_instruction){ .op = BF_END });
    char error[256];
    functions[READ_FIXTURE + field] = behavior_function_builder_finish(builder, error, sizeof error);
    if (!functions[READ_FIXTURE + field]) {
        fprintf(stderr, "C component read fixture: %s\n", error);
    }
    REQUIRE(functions[READ_FIXTURE + field]);
    behavior_function_builder_free(builder);
    builder = NULL;
    char name[40];
    snprintf(name, sizeof name, "export_read_%d", field);
    behavior_function_write_c(functions[READ_FIXTURE + field], source, name);
    return true;
}

static bool read_metadata(const behavior_function_context *context, const behavior_function_instruction *read)
{
    REQUIRE(context->failure == BEHAVIOR_FUNCTION_HOST_DATA);
    REQUIRE(context->node_id == read->node_id);
    REQUIRE(context->error_frame == read->frame && context->error_source == read->source);
    REQUIRE(context->error_stack == read->stack && context->error_pin == read->pin);
    REQUIRE(context->error_steps == 8 && context->steps == 8);
    REQUIRE(context->error_status == -87);
    return true;
}

static bool reads_exported_current_world_scalars()
{
    for (int field = 0; field < READ_FIXTURES; field++) {
        char name[40];
        snprintf(name, sizeof name, "export_read_%d", field);
        typedef int (*entry)(behavior_function_context *);
        entry generated = (entry)dlsym(library, name);
        REQUIRE(generated);
        const behavior_function *f = functions[READ_FIXTURE + field];
        const behavior_function_instruction *read = NULL;
        for (int pc = 0; pc < acount(f->instructions); pc++) {
            if (f->instructions[pc].op == BF_COMPONENT_READ) {
                REQUIRE(!read);
                read = &f->instructions[pc];
            }
        }
        REQUIRE(read && read->source > 0 && !f->root_steps);
        for (int inspection = 0; inspection < 2; inspection++) {
            export_read_world worlds[2];
            read_world(&worlds[0], 0);
            read_world(&worlds[1], 1);
            ecs_execution_column stale = worlds[0].column;
            stale.layout = 0;
            const ecs_execution_column *columns[] = { &stale };
            ecs_execution_view binding = worlds[0].view;
            binding.columns = &stale;
            pin_value input = { .type = PIN_HANDLE, .u64 = READ_ENTITY };
            pin_value output = { .type = read_fields[field].type };
            behavior_function_context context = { .arguments = &input,
                                                  .argument_count = 1,
                                                  .results = &output,
                                                  .budget = 32,
                                                  .error_status = -87,
                                                  .columns = columns,
                                                  .binding_world = &binding,
                                                  .inspect = inspection ? inspect
                                                                        : NULL };
            for (int sample = 0; sample < 2; sample++) {
                context.world = &worlds[sample].view;
                context.steps = 7;
                context.error_message = "stale component read error";
                REQUIRE(generated(&context) == FLOW_OK);
                REQUIRE(
                    !context.error_message && output.type == read_fields[field].type
                );
                REQUIRE(!memcmp(
                    &output.d,
                    (const char *)&worlds[sample].rows[1] + read_fields[field].offset,
                    read_fields[field].size
                ));
            }
            /* The same context recovers against a different current world.
             * Neither the stale column table nor binding world is rebound. */
            for (int failure = 0; failure < 10; failure++) {
                read_world(&worlds[0], 0);
                read_world(&worlds[1], 1);
                export_read_world *world = &worlds[failure % 2];
                context.world = &world->view;
                input.u64 = READ_ENTITY;
                switch (failure) {
                    case 0: context.world = NULL; break;
                    case 1: input.u64 = UINT64_C(7) << 32; break;
                    case 2: input.u64 = (UINT64_C(7) << 32) | 2; break;
                    case 3: world->occupied[1] = 0; break;
                    case 4: world->generations[1]++; break;
                    case 5: world->membership = 0; break;
                    case 6: world->view.capacity = 0; break;
                    case 7: world->column.id++; break;
                    case 8: world->column.layout ^= 1; break;
                    case 9: world->column.stride++; break;
                }
                const pin_value sentinel = { .type = read_fields[field].type,
                                             .u64 = UINT64_C(0xa5a5a5a5a5a5a5a5) };
                output = sentinel;
                context.steps = 7;
                context.error_message = "stale component read error";
                context.failure = BEHAVIOR_FUNCTION_ARGUMENT;
                context.node_id = 99;
                context.error_frame = context.error_source = 99;
                context.error_stack = context.error_pin = 99;
                context.error_steps = 99;
                REQUIRE(generated(&context) == FLOW_ERR_INVALID_GRAPH);
                REQUIRE(
                    output.type == sentinel.type &&
                    !memcmp(&output.d, &sentinel.d, sizeof output.d)
                );
                REQUIRE(
                    context.error_message &&
                    !strcmp(context.error_message, "Referenced storage is unavailable")
                );
                REQUIRE(read_metadata(&context, read));
                world = &worlds[1 - failure % 2];
                context.world = &world->view;
                input.u64 = READ_ENTITY;
                context.steps = 7;
                REQUIRE(generated(&context) == FLOW_OK);
                REQUIRE(
                    !context.error_message && output.type == read_fields[field].type
                );
                REQUIRE(!memcmp(
                    &output.d,
                    (const char *)&world->rows[1] + read_fields[field].offset,
                    read_fields[field].size
                ));
                REQUIRE(read_metadata(&context, read));
            }
        }
    }
    return true;
}

TEST_CASE(generated_c_preserves_aliased_operands_and_debug_continuations)
{
    snprintf(directory, sizeof directory, "/tmp/chibi-c-export-XXXXXX");
    char *created = mkdtemp(directory);
    if (!created) {
        directory[0] = 0;
    }
    REQUIRE(created);
    char input[256], output[256], log[256], includes[PATH_MAX], debug[PATH_MAX];
    snprintf(input, sizeof input, "%s/aliases.c", directory);
    snprintf(output, sizeof output, "%s/aliases.so", directory);
    snprintf(log, sizeof log, "%s/compiler.log", directory);
    snprintf(includes, sizeof includes, "%s/src", tool_root());
    snprintf(debug, sizeof debug, "%s/src/execution/source_debug.c", tool_root());
    source = fopen(input, "w");
    REQUIRE(source);
    for (int i = 0; i < ALIAS_FIXTURES; i++) {
        REQUIRE(fixture(i));
    }
    REQUIRE(pause_fixture());
    REQUIRE(borrow_fixture(false));
    REQUIRE(borrow_fixture(true));
    for (int field = 0; field < READ_FIXTURES; field++) {
        REQUIRE(read_fixture(field));
    }
    long bytes = ftell(source);
    REQUIRE(bytes > 0 && bytes < 128 * 1024);
    int closed = fclose(source);
    source = NULL;
    REQUIRE(!closed);
    const char **argv = NULL;
    for (int i = 0; export_compiler[i]; i++) {
        apush(argv, export_compiler[i]);
    }
    const char *arguments[] = { "-std=c11", "-shared", "-fPIC", "-I",
                                includes,   input,     debug,   "-o",
                                output,     NULL };
    for (int i = 0; i < (int)(sizeof arguments / sizeof *arguments); i++) {
        apush(argv, arguments[i]);
    }
    int compiled = tool_run(argv, directory, log);
    afree(argv);
    REQUIRE(compiled == 0);
    library = dlopen(output, RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        fprintf(stderr, "C export load: %s\n", dlerror());
    }
    REQUIRE(library);
    for (int i = 0; i < ALIAS_FIXTURES; i++) {
        char name[40];
        snprintf(name, sizeof name, "export_alias_%d", i);
        typedef int (*entry)(behavior_function_context *);
        entry generated = (entry)dlsym(library, name);
        REQUIRE(generated);
        pin_type type = i < 6 ? PIN_I32 : PIN_I64;
        int64_t minimum = type == PIN_I32 ? INT32_MIN : INT64_MIN;
        const int64_t samples[][2] = { { 10, 3 },
                                       { -10, 3 },
                                       { 10, -3 },
                                       { minimum, -1 },
                                       { 1, 0 } };
        for (int sample = 0; sample < 5; sample++) {
            for (int inspection = 0; inspection < 2; inspection++) {
                pin_value inputs[] = { integer(type, samples[sample][0]),
                                       integer(type, samples[sample][1]) };
                pin_value actual[] = { integer(type, 77), integer(type, 88) };
                pin_value expected[] = { integer(type, 77), integer(type, 88) };
                behavior_function_context vm = { .arguments = inputs,
                                                 .argument_count = 2,
                                                 .results = expected,
                                                 .budget = 32,
                                                 .inspect = inspect };
                behavior_function_context compiled_context = {
                    .arguments = inputs,
                    .argument_count = 2,
                    .results = actual,
                    .budget = 32,
                    .inspect = inspection ? inspect : NULL
                };
                REQUIRE(behavior_function_bind(functions[i], &vm, NULL, 0));
                int reference = behavior_function_run(functions[i], &vm, false);
                int status = generated(&compiled_context);
                REQUIRE(status == reference);
                REQUIRE(status == (sample >= 3 ? FLOW_ERR_INVALID_GRAPH : FLOW_OK));
                REQUIRE(number(actual[0]) == number(expected[0]));
                REQUIRE(number(actual[1]) == number(expected[1]));
                if (sample >= 3) {
                    REQUIRE(number(actual[0]) == 77 && number(actual[1]) == 88);
                    REQUIRE(compiled_context.failure == BEHAVIOR_FUNCTION_DATA);
                } else {
                    int64_t quotient = samples[sample][0] / samples[sample][1];
                    int64_t remainder = samples[sample][0] % samples[sample][1];
                    bool modulo = i % 6 >= 3;
                    int64_t primary = modulo ? remainder : quotient;
                    int64_t secondary = i % 3 == 2
                                          ? primary
                                          : (modulo ? quotient : remainder);
                    REQUIRE(number(actual[0]) == primary);
                    REQUIRE(number(actual[1]) == secondary);
                }
            }
        }
    }
    REQUIRE(resumes_exported_pause());
    REQUIRE(borrows_exported_plain_arrays());
    REQUIRE(exported_call_snapshots_keep_the_standalone_abi());
    REQUIRE(reads_exported_current_world_scalars());
    return true;
}
void test_tool_c_export()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(generated_c_preserves_aliased_operands_and_debug_continuations);
    pu_clear_setup();
}
