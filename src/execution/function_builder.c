#include "execution/function_builder.h"

#include "ecs/ecs.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/preparation_budget.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/record.h"

#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct behavior_function_builder
{
    behavior_function *function;
    bool decoded;
    size_t arena_budget;
    int *slot_roots, *local_roots;
};
behavior_function_builder *behavior_function_builder_new()
{
    behavior_function_builder *b = xcalloc(1, sizeof *b);
    b->function = xcalloc(1, sizeof *b->function);
    b->function->artifact = execution_artifact_new();
    apush(b->function->sources, ((behavior_function_source){ .parent = -1 }));
    apush(
        b->function->frames,
        ((behavior_function_frame){ .source = { .parent = -1 }, .stack = -1 })
    );
    return b;
}
void behavior_function_builder_free(behavior_function_builder *b)
{
    behavior_function_free(b->function);
    afree(b->slot_roots);
    afree(b->local_roots);
    free(b);
}
void behavior_function_builder_signature(
    behavior_function_builder *b,
    const pin_value *arguments,
    int argument_count,
    const pin_value *results,
    int result_count,
    const pin_value *variables,
    int variable_count
)
{
    behavior_function *f = b->function;
    assert(argument_count >= 0 && result_count >= 0 && variable_count >= 0);
    assert(!f->argument_defaults && !f->result_defaults && !f->variable_defaults);
    f->argument_count = argument_count;
    f->result_count = result_count;
    f->variable_count = variable_count;
    for (int i = 0; i < argument_count; i++) {
        apush(f->argument_defaults, arguments[i]);
        apush(f->argument_kinds, BEHAVIOR_VALUE);
        apush(f->parameters, ((behavior_function_parameter){ arguments[i], 0, i }));
    }
    for (int i = 0; i < result_count; i++) {
        apush(f->result_defaults, results[i]);
        apush(f->result_kinds, BEHAVIOR_VALUE);
        if (results[i].type == PIN_STRUCT &&
            results[i].container == PIN_CONTAINER_SINGLE) {
            const type_desc *type = pin_value_type(&results[i]);
            apush(f->results, ((behavior_function_result){ i, type }));
        }
    }
    for (int i = 0; i < variable_count; i++) {
        apush(f->variable_defaults, variables[i]);
    }
}
int behavior_function_builder_slot(behavior_function_builder *b, pin_type type)
{
    int index = acount(b->function->slots);
    apush(b->function->slots, type);
    apush(b->function->slot_kinds, BEHAVIOR_VALUE);
    return index;
}
bool behavior_function_builder_kind(
    behavior_function_builder *b,
    behavior_value_bank bank,
    int index,
    behavior_value_kind kind
)
{
    if (index < 0 || kind < BEHAVIOR_VALUE || kind > BEHAVIOR_QUERY_CURSOR) {
        return false;
    }
    behavior_function *f = b->function;
    behavior_value_kind *values = NULL;
    switch (bank) {
        case BEHAVIOR_BANK_SLOT: values = f->slot_kinds; break;
        case BEHAVIOR_BANK_ARGUMENT: values = f->argument_kinds; break;
        case BEHAVIOR_BANK_RESULT: values = f->result_kinds; break;
        case BEHAVIOR_BANK_COMMAND_ARGUMENT:
            values = f->command_argument_kinds;
            break;
        case BEHAVIOR_BANK_COMMAND_RESULT:
            values = f->command_result_kinds;
            break;
        case BEHAVIOR_BANK_LOCAL:
            if (index >= acount(f->locals)) {
                return false;
            }
            f->locals[index].kind = kind;
            return true;
        case BEHAVIOR_BANK_DATA:
            if (index >= acount(f->data_values)) {
                return false;
            }
            f->data_values[index].kind = kind;
            return true;
        default: return false;
    }
    if (index >= acount(values)) {
        return false;
    }
    values[index] = kind;
    return true;
}
int behavior_function_builder_local(behavior_function_builder *b, pin_value initial)
{
    int index = acount(b->function->locals);
    apush(b->function->locals, ((behavior_function_local){ .initial = initial }));
    return index;
}
void behavior_function_builder_root(
    behavior_function_builder *b,
    behavior_value_bank bank,
    int index,
    pin_value shape
)
{
    assert(bank == BEHAVIOR_BANK_SLOT || bank == BEHAVIOR_BANK_LOCAL);
    assert(index >= 0 && (shape.container != PIN_CONTAINER_SINGLE || shape.type == PIN_STRUCT));
    shape.ptr = NULL;
    int **indices = bank == BEHAVIOR_BANK_SLOT ? &b->slot_roots : &b->local_roots;
    while (acount(*indices) <= index) {
        apush(*indices, -1);
    }
    int found = (*indices)[index];
    if (found >= 0) {
        assert(
            b->function->value_roots[found].shape.type == shape.type &&
            b->function->value_roots[found].shape.container == shape.container
        );
        return;
    }
    (*indices)[index] = acount(b->function->value_roots);
    apush(b->function->value_roots, ((behavior_value_root){ bank, index, shape }));
}
int behavior_function_builder_origin(behavior_function_builder *b, behavior_function_source source)
{
    int index = acount(b->function->sources);
    apush(b->function->sources, source);
    return index;
}
int behavior_function_builder_source_point(behavior_function_builder *b, behavior_source_point point)
{
    int index = acount(b->function->source_points);
    apush(b->function->source_points, point);
    return index;
}
int behavior_function_builder_source_watch(behavior_function_builder *b, behavior_source_watch watch)
{
    int index = acount(b->function->source_watches);
    apush(b->function->source_watches, watch);
    return index;
}
int behavior_function_builder_emit(behavior_function_builder *b, behavior_function_instruction instruction)
{
    int index = acount(b->function->instructions);
    apush(b->function->instructions, instruction);
    return index;
}
bool behavior_function_builder_patch_target(behavior_function_builder *b, int instruction, int target)
{
    if (instruction < 0 || instruction >= acount(b->function->instructions)) {
        return false;
    }
    b->function->instructions[instruction].target = target;
    return true;
}
static void data_value(behavior_function *f, pin_value value)
{
    size_t size = 0;
    if (value.type == PIN_STRUCT && value.container == PIN_CONTAINER_SINGLE) {
        const type_desc *type = pin_value_type(&value);
        size = type ? type->size : 0;
    }
    apush(f->data_values, ((behavior_function_data_value){ .initial = value, .size = size }));
}
int behavior_function_builder_values(behavior_function_builder *b, const pin_value *defaults, int count)
{
    assert(count >= 0 && (!count || defaults));
    int first = acount(b->function->data_values);
    for (int i = 0; i < count; i++) {
        data_value(b->function, defaults[i]);
    }
    return first;
}
int behavior_function_builder_host(
    behavior_function_builder *b,
    const node_host_call *call,
    const node_access *access,
    const pin_value *arguments,
    int argument_count,
    const pin_value *results,
    int result_count
)
{
    assert(argument_count >= 0 && result_count >= 0);
    behavior_function *f = b->function;
    /* Relationship reads need the execution world even when the function never
     * accesses an ordinary component column. This flag also publishes the
     * required ecs.world service and survives prepared-function serialization. */
    if (call == &EXECUTION_query_target_call || call == &EXECUTION_query_source_call) {
        f->component_access = true;
    }
    int index = acount(f->host_calls), first = acount(f->data_values);
    for (int i = 0; i < argument_count; i++) {
        data_value(f, arguments[i]);
    }
    int result_first = acount(f->data_values);
    for (int i = 0; i < result_count; i++) {
        data_value(f, results[i]);
    }
    apush(
        f->host_calls,
        ((behavior_function_host_call){ .call = call,
                                        .access = access,
                                        .first = first,
                                        .result_first = result_first,
                                        .result_count = result_count })
    );
    return index;
}
bool behavior_function_builder_host_identity(behavior_function_builder *b, int index, uint64_t id, uint64_t abi)
{
    if (index < 0 || index >= acount(b->function->host_calls) || !id || !abi) {
        return false;
    }
    b->function->host_calls[index].capability_id = id;
    b->function->host_calls[index].capability_abi = abi;
    return true;
}
void behavior_function_builder_host_owned_arguments(behavior_function_builder *b, int index)
{
    assert(index >= 0 && index < acount(b->function->host_calls));
    b->function->host_calls[index].owned_arguments = true;
}
void behavior_function_builder_call_owned_arguments(behavior_function_builder *b, int index)
{
    assert(index >= 0 && index < acount(b->function->calls));
    b->function->calls[index].owned_arguments = true;
}
static int compare_outcome(const void *a, const void *b)
{
    uint64_t left = *(const uint64_t *)a, right = *(const uint64_t *)b;
    return (left > right) - (left < right);
}
static bool outcome_ids_valid(const uint64_t *ids, int count)
{
    if (count <= 0 || count > 65536 || !ids) {
        return false;
    }
    uint64_t *sorted = xmalloc((size_t)count * sizeof *sorted);
    memcpy(sorted, ids, (size_t)count * sizeof *sorted);
    qsort(sorted, (size_t)count, sizeof *sorted, compare_outcome);
    bool valid = sorted[0] != 0;
    for (int i = 1; valid && i < count; i++) {
        valid = sorted[i] != sorted[i - 1];
    }
    free(sorted);
    return valid;
}
bool behavior_function_builder_host_outcomes(
    behavior_function_builder *b,
    int index,
    const uint64_t *ids,
    int count
)
{
    if (index < 0 || index >= acount(b->function->host_calls) ||
        !outcome_ids_valid(ids, count)) {
        return false;
    }
    b->function->host_calls[index].outcomes = ids;
    b->function->host_calls[index].outcome_count = count;
    return true;
}
bool behavior_function_builder_query_identity(behavior_function_builder *b, int index, uint64_t id, uint64_t abi)
{
    if (index < 0 || index >= acount(b->function->queries) || !id || !abi) {
        return false;
    }
    b->function->queries[index].capability_id = id;
    b->function->queries[index].capability_abi = abi;
    return true;
}
bool behavior_function_builder_command_identity(
    behavior_function_builder *b,
    int index,
    uint64_t id,
    uint64_t abi
)
{
    if (index < 0 || index >= acount(b->function->commands) || !id || !abi) {
        return false;
    }
    b->function->commands[index].capability_id = id;
    b->function->commands[index].capability_abi = abi;
    return true;
}
bool behavior_function_builder_command_event_identity(
    behavior_function_builder *b,
    int index,
    uint64_t id,
    uint64_t abi
)
{
    if (index < 0 || index >= acount(b->function->commands) || !id || !abi) {
        return false;
    }
    b->function->commands[index].event_id = id;
    b->function->commands[index].event_abi = abi;
    return true;
}
int behavior_function_builder_host_first(const behavior_function_builder *b, int host)
{
    assert(host >= 0 && host < acount(b->function->host_calls));
    return b->function->host_calls[host].first;
}
int behavior_function_builder_host_results(const behavior_function_builder *b, int host)
{
    assert(host >= 0 && host < acount(b->function->host_calls));
    return b->function->host_calls[host].result_first;
}
const behavior_function *behavior_function_builder_definition(const behavior_function_builder *b)
{
    return b->function;
}
int behavior_function_builder_call(
    behavior_function_builder *b,
    const behavior_function *callee,
    const pin_value *arguments,
    int argument_count,
    const pin_value *results,
    int result_count
)
{
    assert(callee && argument_count >= 0 && result_count >= 0);
    behavior_function *f = b->function;
    int index = acount(f->calls), first = acount(f->data_values);
    for (int i = 0; i < argument_count; i++) {
        data_value(f, arguments[i]);
    }
    int result_first = acount(f->data_values);
    for (int i = 0; i < result_count; i++) {
        data_value(f, results[i]);
    }
    apush(
        f->calls,
        ((behavior_function_invocation){ .function = callee,
                                         .first = first,
                                         .count = argument_count,
                                         .result_first = result_first,
                                         .result_count = result_count })
    );
    return index;
}
int behavior_function_builder_task(
    behavior_function_builder *b,
    const behavior_function *callee,
    const pin_value *arguments,
    int argument_count
)
{
    int index = behavior_function_builder_call(
        b,
        callee,
        arguments,
        argument_count,
        &(pin_value){ .type = PIN_TASK },
        1
    );
    b->function->calls[index].task = true;
    return index;
}
int behavior_function_builder_call_first(const behavior_function_builder *b, int call)
{
    assert(call >= 0 && call < acount(b->function->calls));
    return b->function->calls[call].first;
}
int behavior_function_builder_call_results(const behavior_function_builder *b, int call)
{
    assert(call >= 0 && call < acount(b->function->calls));
    return b->function->calls[call].result_first;
}
int behavior_function_builder_query(
    behavior_function_builder *b,
    const char *service,
    const char *key,
    node_query_fn read
)
{
    int index = acount(b->function->queries);
    apush(
        b->function->queries,
        ((behavior_function_query){ .service = service, .key = key, .read = read })
    );
    return index;
}
int behavior_function_builder_command(
    behavior_function_builder *b,
    const node_command *command,
    const char *reference,
    const pin_value *arguments,
    int argument_count,
    const pin_value *results,
    int result_count,
    uint64_t node_id
)
{
    assert(command && argument_count >= 0 && result_count >= 0);
    behavior_function *f = b->function;
    int index = acount(f->commands), first = acount(f->command_arguments);
    for (int i = 0; i < argument_count; i++) {
        apush(f->command_arguments, arguments[i]);
        apush(f->command_argument_kinds, BEHAVIOR_VALUE);
    }
    int result_first = acount(f->command_results);
    for (int i = 0; i < result_count; i++) {
        apush(f->command_results, results[i]);
        apush(f->command_result_kinds, BEHAVIOR_VALUE);
    }
    apush(
        f->commands,
        ((behavior_function_command){ .command = command,
                                      .reference = reference,
                                      .first = first,
                                      .count = argument_count,
                                      .node_id = node_id,
                                      .result_first = result_first,
                                      .result_count = result_count })
    );
    return index;
}
bool behavior_function_builder_command_results_map(
    behavior_function_builder *b,
    int command,
    const int *pins,
    int count
)
{
    if (command < 0 || command >= acount(b->function->commands) ||
        count != b->function->commands[command].result_count || count < 0 ||
        (count && !pins)) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        if (pins[i] < 0) {
            return false;
        }
        for (int j = 0; j < i; j++) {
            if (pins[j] == pins[i]) {
                return false;
            }
        }
    }
    b->function->commands[command].result_pins = pins;
    return true;
}
int behavior_function_builder_command_first(const behavior_function_builder *b, int command)
{
    assert(command >= 0 && command < acount(b->function->commands));
    return b->function->commands[command].first;
}
int behavior_function_builder_command_results(const behavior_function_builder *b, int command)
{
    assert(command >= 0 && command < acount(b->function->commands));
    return b->function->commands[command].result_first;
}
int behavior_function_builder_component(behavior_function_builder *b, uint64_t id, bool binding, bool written)
{
    behavior_function *f = b->function;
    f->component_bindings |= binding;
    f->component_access |= !binding;
    if (written) {
        apush(f->component_writes, id);
    }
    for (int i = 0; i < acount(f->components); i++) {
        if (f->components[i].id == id && f->components[i].binding == binding) {
            return i;
        }
    }
    int index = acount(f->components);
    apush(f->components, ((behavior_function_component){ id, binding }));
    return index;
}
void behavior_function_builder_limits(behavior_function_builder *b, unsigned steps, unsigned depth)
{
    b->function->root_steps = steps;
    b->function->depth_limit = depth;
}
static bool index_valid(int index, int count)
{
    return index >= 0 && index < count;
}
static bool scalar_type(pin_type type)
{
    return (type >= PIN_BOOL && type <= PIN_HANDLE) || type == PIN_ENUM ||
           type == PIN_TASK || type == PIN_UNION || type == PIN_POINTER ||
           type == PIN_QUERY;
}
static bool same_type(pin_value a, pin_value b)
{
    return a.type == b.type && a.container == b.container &&
           pin_value_layout_same(&a, &b) &&
           ((!a.type_name && !b.type_name) ||
            (a.type_name && b.type_name && !strcmp(a.type_name, b.type_name)));
}
static bool fits(size_t offset, size_t width, size_t size)
{
    return offset <= size && width <= size - offset;
}
static bool scalar_bank(pin_value value, pin_type type)
{
    return value.container == PIN_CONTAINER_SINGLE && value.type == type &&
           scalar_type(type);
}
static bool reflected_scalar_field(const type_desc *type, size_t offset, pin_type value, int depth)
{
    if (depth >= 32 || !fits(offset, behavior_scalar_size(value), type->size)) {
        return false;
    }
    if (type->alias_of) {
        const type_desc *alias = type_alias_type(type, (type_resolver){ 0 });
        return alias && reflected_scalar_field(alias, offset, value, depth + 1);
    }
    for (int i = 0; i < type->field_count; i++) {
        const type_field *field = &type->fields[i];
        /* An array field holds a pin_array pointer, never its element type. */
        if (field->container != PIN_CONTAINER_SINGLE) {
            continue;
        }
        if (field->type == value && field->offset == offset &&
            field->size == behavior_scalar_size(value) &&
            (value != PIN_STR || field->text_descriptor)) {
            return true;
        }
        if (field->type == PIN_STRUCT && field->type_name && offset >= field->offset &&
            fits(offset - field->offset, behavior_scalar_size(value), field->size)) {
            const type_desc *nested = type_field_type(field, (type_resolver){ 0 });
            if (nested &&
                reflected_scalar_field(nested, offset - field->offset, value, depth + 1)) {
                return true;
            }
        }
    }
    return false;
}
static bool struct_field(pin_value value, const behavior_function_instruction *i)
{
    if (value.type != PIN_STRUCT || value.container != PIN_CONTAINER_SINGLE ||
        !value.type_name) {
        return false;
    }
    const type_desc *type = pin_value_type(&value);
    return type && reflected_scalar_field(type, i->offset, i->type, 0);
}

static const char *verify_instruction(const behavior_function *f, int pc)
{
    const behavior_function_instruction *i = &f->instructions[pc];
#define CHECK(condition, message)                                              \
    do {                                                                       \
        if (!(condition)) return message;                                      \
    } while (0)
#define SLOT(index, type) (f->slots[index] == (type))
    CHECK(i->op >= BF_CONSTANT && i->op <= BF_END, "invalid opcode");
    CHECK(
        index_valid(i->frame, acount(f->frames)) &&
            index_valid(i->source, acount(f->sources)),
        "invalid source or frame"
    );
    CHECK(i->stack == -1 || i->stack == 0, "invalid stack origin");
    CHECK(i->op >= BF_STORE || index_valid(i->output, acount(f->slots)), "invalid output slot");
    CHECK(!behavior_function_left_slot(i->op) || index_valid(i->left, acount(f->slots)), "invalid left operand slot");
    CHECK(!behavior_function_right_slot(i->op) || index_valid(i->right, acount(f->slots)), "invalid right operand slot");
    CHECK(!behavior_function_third_slot(i->op) || index_valid(i->target, acount(f->slots)), "invalid third operand slot");
    CHECK(i->op >= BF_STORE || scalar_type(i->type), "invalid scalar instruction type");
    bool comparison = i->op >= BF_LT && i->op <= BF_NE;
    CHECK(i->op >= BF_STORE || SLOT(i->output, comparison ? PIN_BOOL : i->type), "invalid output slot type");
    if (i->indirect) {
        CHECK(
            i->op == BF_ARGUMENT || i->op == BF_VARIABLE || i->op == BF_STORE ||
                i->op == BF_RESULT,
            "indirect access requires a signature bank"
        );
        CHECK(scalar_type(i->type), "invalid indirect field type");
        int index = i->op == BF_ARGUMENT || i->op == BF_VARIABLE ? i->left : i->target;
        int count = i->op == BF_ARGUMENT ? f->argument_count
                  : i->op == BF_RESULT   ? f->result_count
                                         : f->variable_count;
        CHECK(index_valid(index, count), "invalid indirect bank index");
        const pin_value *bank = i->op == BF_ARGUMENT ? f->argument_defaults
                              : i->op == BF_RESULT   ? f->result_defaults
                                                     : f->variable_defaults;
        CHECK(struct_field(bank[index], i), "invalid indirect struct field range");
        CHECK((i->op != BF_STORE && i->op != BF_RESULT) || SLOT(i->left, i->type), "invalid indirect store type");
        CHECK(i->op != BF_ARGUMENT || scalar_bank(i->value, i->type), "invalid indirect argument default");
        return NULL;
    }
    if (behavior_function_bitwise(i->op)) {
        CHECK(i->type >= PIN_I8 && i->type <= PIN_U64, "bitwise requires an integer");
        CHECK(SLOT(i->left, i->type), "invalid bitwise left type");
        CHECK(i->op == BF_BIT_NOT || SLOT(i->right, i->type), "invalid bitwise right type");
        return NULL;
    }
    if ((i->op >= BF_ADD && i->op <= BF_MAX) || comparison) {
        CHECK(SLOT(i->left, i->type), "invalid arithmetic left type");
        CHECK(i->op == BF_NEG || i->op == BF_ABS || SLOT(i->right, i->type), "invalid arithmetic right type");
        bool floating = i->type == PIN_F32 || i->type == PIN_F64;
        bool integer = i->type >= PIN_I8 && i->type <= PIN_U64;
        CHECK(
            floating || integer ||
                (scalar_type(i->type) && (i->op == BF_EQ || i->op == BF_NE)),
            "unsupported arithmetic type"
        );
        CHECK(i->op != BF_MOD || integer, "MOD requires an integer");
        if ((i->op == BF_DIV || i->op == BF_MOD) && integer && i->target >= 0) {
            CHECK(index_valid(i->target, acount(f->slots)) && SLOT(i->target, i->type), "invalid division secondary result");
        }
        return NULL;
    }
    if (behavior_function_math(i->op)) {
        pin_type type = i->op == BF_MOD_F32 ? PIN_F32 : PIN_F64;
        CHECK(i->type == type && SLOT(i->left, type), "invalid math operand type");
        CHECK((i->op >= BF_SIN && i->op <= BF_ROUND) || SLOT(i->right, type), "invalid math right type");
        CHECK(i->op != BF_FMA || SLOT(i->target, type), "invalid FMA third type");
        return NULL;
    }
    switch (i->op) {
        case BF_CONSTANT:
            CHECK(scalar_bank(i->value, i->type), "invalid scalar constant");
            break;
        case BF_VARIABLE:
            CHECK(index_valid(i->left, f->variable_count), "invalid variable");
            CHECK(scalar_bank(f->variable_defaults[i->left], i->type), "invalid variable type");
            break;
        case BF_ARGUMENT:
            CHECK(index_valid(i->left, f->argument_count), "invalid argument");
            CHECK(
                scalar_bank(f->argument_defaults[i->left], i->type) &&
                    scalar_bank(i->value, i->type),
                "invalid argument type or default"
            );
            break;
        case BF_LOCAL:
            CHECK(index_valid(i->left, acount(f->locals)), "invalid local");
            CHECK(scalar_bank(f->locals[i->left].initial, i->type), "invalid local type");
            break;
        case BF_SAVE:
            CHECK(index_valid(i->target, acount(f->locals)), "invalid local destination");
            CHECK(
                scalar_bank(f->locals[i->target].initial, i->type) &&
                    SLOT(i->left, i->type),
                "invalid local store type"
            );
            break;
        case BF_STORE:
        case BF_RESULT: {
            int count = i->op == BF_STORE ? f->variable_count : f->result_count;
            CHECK(index_valid(i->target, count), "invalid signature destination");
            const pin_value *bank = i->op == BF_STORE ? f->variable_defaults
                                                      : f->result_defaults;
            CHECK(scalar_bank(bank[i->target], i->type) && SLOT(i->left, i->type), "invalid signature store type");
            break;
        }
        case BF_ARRAY:
        case BF_PRESENT:
        case BF_TYPE_NAME: {
            CHECK(
                i->target == BF_VARIABLE || i->target == BF_ARGUMENT ||
                    (i->op != BF_PRESENT && i->target == BF_CONSTANT) ||
                    (i->op == BF_TYPE_NAME && i->target == BF_OWNER),
                "invalid header selector"
            );
            const pin_value *value = &i->value;
            if (i->target == BF_VARIABLE || i->target == BF_ARGUMENT) {
                CHECK(
                    index_valid(i->left, i->target == BF_VARIABLE ? f->variable_count : f->argument_count),
                    "invalid header bank index"
                );
                value = &(
                    i->target == BF_VARIABLE ? f->variable_defaults : f->argument_defaults
                )[i->left];
            }
            CHECK(i->type == (i->op == BF_PRESENT ? PIN_BOOL : BF_POINTER_TYPE), "invalid header output type");
            CHECK(
                i->op != BF_ARRAY || value->type == PIN_STRUCT ||
                    value->type == PIN_UNION || value->container != PIN_CONTAINER_SINGLE,
                "ARRAY requires an aggregate header"
            );
            CHECK(
                i->op != BF_PRESENT || (value->type == PIN_STRUCT &&
                                        value->container == PIN_CONTAINER_SINGLE),
                "PRESENT requires a struct header"
            );
            CHECK(
                i->target != BF_ARGUMENT || i->op != BF_ARRAY ||
                    same_type(*value, i->value),
                "invalid argument header default"
            );
            CHECK(
                i->target != BF_ARGUMENT || i->op != BF_PRESENT ||
                    scalar_bank(i->value, PIN_BOOL),
                "invalid presence default"
            );
            break;
        }
        case BF_STORAGE_NAME:
            CHECK(index_valid(i->left, f->variable_count) && i->type == BF_POINTER_TYPE, "invalid storage name");
            break;
        case BF_STORE_VALUE:
            CHECK(
                index_valid(i->target, f->variable_count) &&
                    index_valid(i->left, acount(f->data_values)),
                "invalid managed store bank"
            );
            CHECK(i->offset == 0 || i->offset == BF_STORE_VALUE_SET_ADD, "invalid managed store operation");
            if (i->offset == BF_STORE_VALUE_SET_ADD) {
                pin_value variable = f->variable_defaults[i->target],
                          value = f->data_values[i->left].initial;
                CHECK(
                    variable.container == PIN_CONTAINER_SET &&
                        value.container == PIN_CONTAINER_SINGLE,
                    "invalid set append header"
                );
                variable.container = PIN_CONTAINER_SINGLE;
                CHECK(same_type(variable, value), "invalid set append type");
            } else {
                CHECK(
                    same_type(
                        f->variable_defaults[i->target],
                        f->data_values[i->left].initial
                    ),
                    "invalid managed store type"
                );
            }
            break;
        case BF_STORE_HEADER:
        case BF_STORE_TYPE_NAME:
            CHECK(index_valid(i->target, f->variable_count), "invalid variable header destination");
            CHECK(
                !f->variable_defaults[i->target].record ||
                    f->variable_defaults[i->target].record->native,
                "native header opcode cannot replace an authored record"
            );
            CHECK(SLOT(i->left, BF_POINTER_TYPE), "invalid header name type");
            CHECK(
                i->op != BF_STORE_HEADER ||
                    (SLOT(i->right, PIN_BOOL) &&
                     f->variable_defaults[i->target].type == PIN_STRUCT &&
                     f->variable_defaults[i->target].container == PIN_CONTAINER_SINGLE),
                "invalid struct variable header"
            );
            break;
        case BF_RESULT_PRESENT:
        case BF_RESULT_TYPE_NAME:
        case BF_RESULT_ARRAY:
            CHECK(index_valid(i->target, f->result_count), "invalid result header destination");
            CHECK(
                i->op == BF_RESULT_PRESENT || !f->result_defaults[i->target].record ||
                    f->result_defaults[i->target].record->native,
                "native result opcode cannot replace an authored record"
            );
            CHECK(SLOT(i->left, i->op == BF_RESULT_PRESENT ? PIN_BOOL : BF_POINTER_TYPE), "invalid result header source");
            CHECK(
                i->op != BF_RESULT_PRESENT ||
                    (f->result_defaults[i->target].type == PIN_STRUCT &&
                     f->result_defaults[i->target].container == PIN_CONTAINER_SINGLE),
                "invalid struct result header"
            );
            CHECK(
                i->op != BF_RESULT_ARRAY ||
                    (SLOT(i->right, BF_POINTER_TYPE) &&
                     f->result_defaults[i->target].container != PIN_CONTAINER_SINGLE &&
                     same_type(f->result_defaults[i->target], i->value) &&
                     i->type == i->value.type),
                "invalid array result header"
            );
            break;
        case BF_HOST_SLOT:
            CHECK(
                index_valid(i->left, acount(f->data_values)) &&
                    i->type == BF_POINTER_TYPE && (i->target == 0 || i->target == 1),
                "invalid host value"
            );
            CHECK(!i->target || f->data_values[i->left].size, "host value has no struct buffer");
            break;
        case BF_HOST_SERVICE:
            CHECK(index_valid(i->target, acount(f->host_calls)) && i->type == BF_POINTER_TYPE, "invalid host service");
            break;
        case BF_HOST_CALL:
            CHECK(index_valid(i->target, acount(f->host_calls)), "invalid host capability");
            CHECK(!i->right, "runtime host flags are not canonical instructions");
            CHECK(
                i->left == BEHAVIOR_FUNCTION_HOST_DATA ||
                    i->left == BEHAVIOR_FUNCTION_HOST_FLOW ||
                    i->left == BEHAVIOR_FUNCTION_ARGUMENT,
                "invalid host failure kind"
            );
            break;
        case BF_INVOKE:
            CHECK(index_valid(i->left, acount(f->calls)), "invalid function invocation");
            break;
        case BF_QUERY:
            CHECK(index_valid(i->target, acount(f->queries)) && i->type == BF_POINTER_TYPE, "invalid query");
            break;
        case BF_COMMAND_SERVICE:
            CHECK(index_valid(i->target, acount(f->commands)) && i->type == BF_POINTER_TYPE, "invalid command service");
            break;
        case BF_COMMAND:
        case BF_COMMAND_BEGIN:
            CHECK(index_valid(i->target, acount(f->commands)), "invalid command");
            CHECK(
                i->op == BF_COMMAND
                    ? f->commands[i->target].command->invoke != NULL
                    : (f->commands[i->target].command->begin != NULL ||
                       f->commands[i->target].command->intrinsic != NODE_COMMAND_NATIVE),
                "missing command implementation"
            );
            break;
        case BF_COMMAND_ARGUMENT:
        case BF_COMMAND_OUTPUT:
        case BF_COMMAND_RESULT: {
            bool argument = i->op == BF_COMMAND_ARGUMENT;
            int index = i->op == BF_COMMAND_RESULT ? i->left : i->target;
            CHECK(
                index_valid(index, argument ? acount(f->command_arguments) : acount(f->command_results)),
                "invalid command value bank"
            );
            CHECK(
                scalar_type(i->type) &&
                    fits(i->offset, behavior_scalar_size(i->type), sizeof(pin_value)),
                "invalid command value field"
            );
            const pin_value *bank = argument ? f->command_arguments : f->command_results;
            pin_value declared = bank[index];
            pin_type physical = declared.type == PIN_STRUCT ||
                                        declared.container != PIN_CONTAINER_SINGLE
                                  ? BF_POINTER_TYPE
                                  : declared.type;
            CHECK(
                (i->offset == offsetof(pin_value, d) && i->type == physical) ||
                    (i->offset == offsetof(pin_value, type_name) &&
                     i->type == BF_POINTER_TYPE),
                "command value field differs from declared type"
            );
            CHECK(i->op == BF_COMMAND_RESULT || SLOT(i->left, i->type), "invalid command store type");
            break;
        }
        case BF_STATE_SELECT:
            CHECK(
                index_valid(i->target, acount(f->state_selections)) && i->type == PIN_I32 &&
                    SLOT(i->left, PIN_I32) && SLOT(i->right, PIN_I32),
                "invalid state selection"
            );
            break;
        case BF_DELTA: CHECK(i->type == PIN_F32, "DELTA requires float"); break;
        case BF_OWNER:
            CHECK(i->type == PIN_HANDLE || i->type == PIN_U64, "OWNER requires handle");
            break;
        case BF_QUERY_VARIABLE:
            CHECK(i->type == PIN_HANDLE && i->target >= 1 && i->target <= ECS_QUERY_VARS, "invalid query variable");
            break;
        case BF_WORLD:
            CHECK(i->type == BF_POINTER_TYPE && (i->target == 0 || i->target == 1), "invalid world selector");
            break;
        case BF_CONTEXT:
            CHECK(
                (i->target == 0 &&
                 fits(i->offset, behavior_scalar_size(i->type), sizeof(behavior_function_context))) ||
                    (i->target == 1 && i->type == BF_POINTER_TYPE && i->offset == 0),
                "invalid context selector or field range"
            );
            break;
        case BF_LOAD:
        case BF_READ:
            CHECK(
                SLOT(i->left, BF_POINTER_TYPE) &&
                    i->offset <= SIZE_MAX - behavior_scalar_size(i->type),
                "invalid memory read"
            );
            break;
        case BF_WRITE:
            CHECK(
                scalar_type(i->type) && SLOT(i->left, i->type) &&
                    SLOT(i->right, BF_POINTER_TYPE) &&
                    i->offset <= SIZE_MAX - behavior_scalar_size(i->type),
                "invalid memory write"
            );
            break;
        case BF_INDEX:
            CHECK(
                i->type == BF_POINTER_TYPE && SLOT(i->left, BF_POINTER_TYPE) &&
                    SLOT(i->right, PIN_I32) && i->offset,
                "invalid pointer index"
            );
            break;
        case BF_ENTITY:
            CHECK(
                i->type == PIN_U32 && SLOT(i->left, BF_POINTER_TYPE) &&
                    (SLOT(i->right, PIN_HANDLE) || SLOT(i->right, PIN_U64)),
                "invalid entity lookup"
            );
            break;
        case BF_COMPONENT_READ:
        case BF_COMPONENT_WRITE:
            CHECK(index_valid(i->component, acount(f->components)), "invalid scalar component binding");
            CHECK(
                !f->components[i->component].binding && i->value.type == PIN_U64 &&
                    i->value.u64 == f->components[i->component].id,
                "scalar component ID differs from current-world binding"
            );
            CHECK(
                (SLOT(i->left, PIN_HANDLE) || SLOT(i->left, PIN_U64)) &&
                    i->type >= PIN_BOOL && i->type <= PIN_F64 &&
                    (i->op != BF_COMPONENT_WRITE || SLOT(i->right, i->type)),
                "invalid scalar component operands"
            );
            CHECK(
                i->layout && i->offset && i->target >= 0 &&
                    fits((size_t)i->target, behavior_scalar_size(i->type), i->offset),
                "invalid scalar component field range"
            );
            break;
        case BF_COMPONENT:
        case BF_COLUMN:
        case BF_COLUMN_TRY:
            CHECK(index_valid(i->component, acount(f->components)), "invalid component binding");
            CHECK(
                SLOT(i->left, BF_POINTER_TYPE) &&
                    (i->op != BF_COMPONENT || SLOT(i->right, PIN_U32)) &&
                    (i->type == BF_POINTER_TYPE || i->type == PIN_BOOL),
                "invalid component operands"
            );
            CHECK(i->value.u64 == f->components[i->component].id, "component ID differs from binding");
            CHECK(i->type == PIN_BOOL || (i->offset && i->layout), "missing component layout");
            CHECK(index_valid(i->target, BEHAVIOR_FUNCTION_COMMAND + 1), "invalid component failure kind");
            break;
        case BF_ROW:
            CHECK(
                i->type == BF_POINTER_TYPE && SLOT(i->left, BF_POINTER_TYPE) &&
                    SLOT(i->right, PIN_U32) && i->offset,
                "invalid component row"
            );
            break;
        case BF_CAST:
            CHECK(behavior_scalar_cast_supported(f->slots[i->left], i->type), "invalid numeric cast");
            CHECK(
                i->target == BEHAVIOR_CAST_EXACT ||
                    ((i->target == BEHAVIOR_CAST_WRAP || i->target == BEHAVIOR_CAST_CLAMP) &&
                     behavior_scalar_integer(f->slots[i->left]) &&
                     behavior_scalar_integer(i->type)),
                "invalid numeric cast mode"
            );
            break;
        case BF_CONVERT:
            CHECK(
                (i->type == PIN_F32 && SLOT(i->left, PIN_F64)) ||
                    (i->type == PIN_F64 && SLOT(i->left, PIN_F32)),
                "invalid float conversion"
            );
            break;
        case BF_FLAGS:
            CHECK(i->type == PIN_BOOL && SLOT(i->left, PIN_U32) && SLOT(i->right, PIN_U32), "invalid flag test");
            break;
        case BF_SELECT:
            CHECK(
                SLOT(i->left, PIN_BOOL) && SLOT(i->right, i->type) &&
                    SLOT(i->target, i->type),
                "invalid select operand types"
            );
            break;
        case BF_RANDOM: CHECK(i->type == PIN_I32, "RANDOM requires int"); break;
        case BF_RANDOM_RANGE:
            CHECK(i->type == PIN_F64 && SLOT(i->left, PIN_F64) && SLOT(i->right, PIN_F64), "invalid random range");
            break;
        case BF_OWN_NAME:
            CHECK(i->type == BF_POINTER_TYPE && SLOT(i->left, BF_POINTER_TYPE), "invalid name ownership");
            break;
        case BF_COPY_ARRAY:
            CHECK(i->type == BF_POINTER_TYPE && SLOT(i->left, BF_POINTER_TYPE), "invalid array copy");
            break;
        case BF_COPY_STRING:
            CHECK(behavior_scalar_string(i->type) && SLOT(i->left, i->type), "invalid string copy");
            break;
        case BF_INTERN_STRING:
            CHECK(
                behavior_scalar_string(i->type) && SLOT(i->left, i->type) &&
                    SLOT(i->right, BF_POINTER_TYPE),
                "invalid string intern"
            );
            break;
        case BF_STRING_EQUAL:
            CHECK(
                i->type == PIN_BOOL &&
                    (SLOT(i->left, BF_POINTER_TYPE) ||
                     behavior_scalar_string(f->slots[i->left])) &&
                    (SLOT(i->right, BF_POINTER_TYPE) ||
                     behavior_scalar_string(f->slots[i->right])),
                "invalid string comparison"
            );
            CHECK(
                (f->slots[i->left] == PIN_STR) == (f->slots[i->right] == PIN_STR),
                "string comparison mixes text descriptors and native names"
            );
            break;
        case BF_BRANCH:
            CHECK(SLOT(i->left, PIN_BOOL), "branch condition must be bool"); /* fall through */
        case BF_JUMP:
            CHECK(index_valid(i->target, acount(f->instructions)), "invalid branch destination");
            break;
        case BF_DELAY:
            CHECK(SLOT(i->left, PIN_F64), "delay requires double"); /* fall through */
        case BF_SUSPEND:
            CHECK(pc + 1 < acount(f->instructions), "suspension requires a continuation");
            break;
        case BF_SOURCE_POINT:
            CHECK(index_valid(i->left, acount(f->source_points)), "invalid source point");
            CHECK(pc + 1 < acount(f->instructions), "source point requires a continuation");
            break;
        case BF_SOURCE_WATCH: {
            CHECK(index_valid(i->target, acount(f->source_watches)), "invalid source watch");
            const behavior_source_watch *watch = &f->source_watches[i->target];
            pin_type physical = watch->reference ||
                                        watch->initial.type == PIN_STRUCT ||
                                        watch->initial.container != PIN_CONTAINER_SINGLE
                                  ? BF_POINTER_TYPE
                                  : watch->initial.type;
            CHECK(i->type == physical && SLOT(i->left, physical), "invalid source watch payload");
            break;
        }
        case BF_ASSERT:
            CHECK(
                index_valid(i->target, BEHAVIOR_FUNCTION_COMMAND + 1) &&
                    (SLOT(i->left, PIN_BOOL) || SLOT(i->left, BF_POINTER_TYPE)),
                "invalid assertion"
            );
            break;
        case BF_REQUIRE:
            CHECK(SLOT(i->left, PIN_BOOL), "require condition must be bool");
            break;
        case BF_FAIL:
            CHECK(index_valid(i->left, BEHAVIOR_FUNCTION_COMMAND + 1), "invalid failure kind");
            break;
        case BF_CALL_CHECK: CHECK(i->left > 0, "invalid depth limit"); break;
        case BF_CHECK:
        case BF_RETURN:
        case BF_END: break;
        /* This builder does not expose the inspection/frame tables needed by
         * these internal lowering operations. Never accept dangling indices. */
        case BF_ENTER:
        case BF_LEAVE:
        case BF_RETRIGGER:
        case BF_POINT:
        case BF_WATCH_BEGIN:
        case BF_WATCH_RESET:
        case BF_WATCH_VALUE:
        case BF_WATCH_PRESENT:
        case BF_WATCH_END:
        case BF_TRACE: return "operation requires internal lowering metadata";
        default: return "unsupported instruction";
    }
#undef SLOT
#undef CHECK
    return NULL;
}
/* Both the prepared reader and this verifier build the same non-evicting
 * pointer table. It owns no descriptors and never survives a verification
 * attempt or a change to the current native type catalog. */
static size_t record_headers(const behavior_function *f, behavior_record_check *checks, size_t *visited)
{
    const pin_value *banks[] = { f->argument_defaults,
                                 f->result_defaults,
                                 f->variable_defaults,
                                 f->command_arguments,
                                 f->command_results };
    size_t count = 0;
    *visited = 0;
#define HEADER(value)                                                          \
    do {                                                                       \
        if (*visited == SIZE_MAX) return SIZE_MAX;                             \
        (*visited)++;                                                          \
        if ((value).record) {                                                  \
            if (count == INT_MAX) return SIZE_MAX;                             \
            if (checks) checks[count].record = (value).record;                 \
            count++;                                                           \
        }                                                                      \
    } while (0)
    for (int b = 0; b < 5; b++) {
        for (int i = 0; i < acount(banks[b]); i++) {
            HEADER(banks[b][i]);
        }
    }
    for (int i = 0; i < acount(f->data_values); i++) {
        HEADER(f->data_values[i].initial);
    }
    for (int i = 0; i < acount(f->value_roots); i++) {
        HEADER(f->value_roots[i].shape);
    }
    for (int i = 0; i < acount(f->source_watches); i++) {
        HEADER(f->source_watches[i].initial);
    }
#undef HEADER
    return count;
}
size_t behavior_function_record_check_budget(const behavior_function *f, size_t *bytes)
{
    size_t visited;
    size_t count = record_headers(f, NULL, &visited);
    *bytes = SIZE_MAX;
    if (count == SIZE_MAX || count > SIZE_MAX / sizeof(behavior_record_check)) {
        return SIZE_MAX;
    }
    *bytes = count * sizeof(behavior_record_check);
    size_t levels = 2;
    for (size_t n = count; n > 1; n /= 2) {
        levels++;
    }
    /* Two table constructions (preflight and verification): counting/filling,
     * zeroing, bounded heap sorting and compaction. Binary lookups include
     * metadata, intrinsic schema checks, and the watch diagnostic rescan.
     * Every intrinsic currently checks at most one of its declared data headers.
     * Unknown future headers still validate normally, without adding a proof. */
    if (visited > SIZE_MAX / 4 || *bytes > SIZE_MAX / 2 ||
        count > SIZE_MAX / (8 * levels + 4)) {
        return SIZE_MAX;
    }
    size_t work = 4 * visited;
    size_t construction = 2 * *bytes + count * (8 * levels + 4);
    if (construction < 2 * *bytes || construction > SIZE_MAX - work) {
        return SIZE_MAX;
    }
    work += construction;
    size_t watches = (size_t)acount(f->source_watches),
           hosts = (size_t)acount(f->host_calls);
    if (count > (SIZE_MAX - hosts) / 2 || watches > (SIZE_MAX - hosts - 2 * count) / 2) {
        return SIZE_MAX;
    }
    size_t lookups = 2 * count + 2 * watches + hosts;
    if (lookups > (SIZE_MAX - work) / levels) {
        return SIZE_MAX;
    }
    return work + lookups * levels;
}
static void record_sift(behavior_record_check *checks, int at, int count)
{
    while (at < count / 2) {
        int child = 2 * at + 1;
        if (child + 1 < count && (uintptr_t)checks[child].record <
                                     (uintptr_t)checks[child + 1].record) {
            child++;
        }
        if ((uintptr_t)checks[at].record >= (uintptr_t)checks[child].record) {
            return;
        }
        behavior_record_check swap = checks[at];
        checks[at] = checks[child];
        checks[child] = swap;
        at = child;
    }
}
behavior_record_check *behavior_function_record_checks(const behavior_function *f, size_t bytes, int *count)
{
    *count = 0;
    if (!bytes) {
        return NULL;
    }
    behavior_record_check *checks = xcalloc(1, bytes);
    size_t visited;
    size_t headers = record_headers(f, checks, &visited);
    assert(headers <= INT_MAX && headers * sizeof *checks == bytes);
    int n = (int)headers;
    for (int at = n / 2; at > 0;) {
        record_sift(checks, --at, n);
    }
    for (int end = n; end > 1;) {
        behavior_record_check swap = checks[0];
        checks[0] = checks[--end];
        checks[end] = swap;
        record_sift(checks, 0, end);
    }
    for (int i = 0; i < n; i++) {
        if (!*count || checks[i].record != checks[*count - 1].record) {
            checks[(*count)++] = checks[i];
        }
    }
    return checks;
}
int behavior_record_check_find(const behavior_record_check *checks, int count, const pin_record *record)
{
    int first = 0, end = count;
    while (first < end) {
        int middle = first + (end - first) / 2;
        if ((uintptr_t)checks[middle].record < (uintptr_t)record) {
            first = middle + 1;
        } else {
            end = middle;
        }
    }
    return first < count && checks[first].record == record ? first : -1;
}
typedef struct
{
    behavior_record_check *checks;
    int count;
} record_validation;
static bool record_valid(record_validation *records, const pin_record *record)
{
    int at = behavior_record_check_find(records->checks, records->count, record);
    if (at >= 0 && records->checks[at].seen) {
        return true;
    }
    if (!pin_record_valid(record)) {
        return false;
    }
    if (at >= 0) {
        records->checks[at].seen = true;
    }
    return true;
}
static bool value_type_valid(record_validation *records, pin_value value)
{
    if (value.container < PIN_CONTAINER_SINGLE || value.container > PIN_CONTAINER_VIEW ||
        (!scalar_type(value.type) && value.type != PIN_STRUCT && value.type != PIN_UNION)) {
        return false;
    }
    if (value.container == PIN_CONTAINER_VIEW && value.ptr) {
        return false;
    }
    if (value.record &&
        (value.type != PIN_STRUCT || !record_valid(records, value.record))) {
        return false;
    }
    /* Nested collections carry each element's complete header, including its
     * record layout. The outer descriptor is not itself a scalar record. */
    if (!value.record &&
        (value.container == PIN_CONTAINER_ARRAY || value.container == PIN_CONTAINER_SET ||
         value.container == PIN_CONTAINER_VIEW) &&
        value.type_name && !strncmp(value.type_name, "__collection_", 13)) {
        return true;
    }
    return value.type != PIN_STRUCT || pin_value_type(&value);
}
static bool map_bank(pin_value value)
{
    return value.type == PIN_UNION && value.container == PIN_CONTAINER_MAP &&
           !value.record && value.type_name;
}
static bool array_bank(record_validation *records, pin_value value)
{
    return (value.container == PIN_CONTAINER_ARRAY ||
            value.container == PIN_CONTAINER_VIEW) &&
           value_type_valid(records, value);
}
static bool sequence_bank(record_validation *records, pin_value value)
{
    return scalar_bank(value, PIN_STR) || array_bank(records, value);
}
/* Intrinsic callbacks consume physical value banks, not authored signatures.
 * Validate their ABI before a producer or decoded definition can execute them. */
static bool intrinsic_schema(
    record_validation *records,
    const behavior_function *f,
    const behavior_function_host_call *host
)
{
    uint64_t id = execution_intrinsic_id(host->call);
    if (!id) {
        return true;
    }
    if (host->owned_arguments) {
        return false;
    }
    int count = host->result_first - host->first;
    const behavior_function_data_value *values = f->data_values + host->first;
    pin_value output = f->data_values[host->result_first].initial;
#define INPUT(index, type) scalar_bank(values[index].initial, type)
#define OUTPUT(type) scalar_bank(output, type)
    switch (execution_intrinsic_kind(id)) {
        case EXECUTION_INTRINSIC_VALUE_COPY:
            return count == 1 && value_type_valid(records, values[0].initial) &&
                   same_type(values[0].initial, output);
        case EXECUTION_INTRINSIC_VALUE_DROP:
            return count == 1 && value_type_valid(records, values[0].initial) &&
                   OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_REFERENCE_UNION:
            return count == 4 && INPUT(0, BF_POINTER_TYPE) &&
                   INPUT(1, PIN_U64) && INPUT(2, PIN_I32) &&
                   value_type_valid(records, values[3].initial) &&
                   OUTPUT(BF_POINTER_TYPE);
        case EXECUTION_INTRINSIC_MAP_DELETE_PLACE:
            return count == 2 && INPUT(0, BF_POINTER_TYPE) &&
                   pin_map_key_supported(values[1].initial) && OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE:
            if (count < 2 || count > 4 || !INPUT(0, BF_POINTER_TYPE) ||
                !INPUT(1, PIN_I32) || !OUTPUT(PIN_BOOL) ||
                (count == 4 && !INPUT(2, PIN_I32))) {
                return false;
            }
            for (int i = 2; i < count; i++) {
                if (!value_type_valid(records, values[i].initial)) {
                    return false;
                }
            }
            return true;
        case EXECUTION_INTRINSIC_SEQUENCE_VIEW:
            return count == 4 && (INPUT(0, PIN_POINTER) || INPUT(0, PIN_STR)) &&
                   INPUT(1, PIN_I32) && INPUT(2, PIN_I32) &&
                   INPUT(3, PIN_U64) && OUTPUT(PIN_UNION);
        case EXECUTION_INTRINSIC_SEQUENCE_SLICE:
            return count == 4 && sequence_bank(records, values[0].initial) &&
                   INPUT(1, PIN_I32) && INPUT(2, PIN_I32) &&
                   INPUT(3, PIN_U64) && OUTPUT(PIN_UNION);
        case EXECUTION_INTRINSIC_SEQUENCE_CLONE: {
            if (count != 1 || !sequence_bank(records, values[0].initial)) {
                return false;
            }
            pin_value source = values[0].initial;
            if (source.container != PIN_CONTAINER_SINGLE) {
                source.container = PIN_CONTAINER_ARRAY;
            }
            return same_type(source, output);
        }
        case EXECUTION_INTRINSIC_SEQUENCE_LENGTH:
            return count == 1 && sequence_bank(records, values[0].initial) &&
                   OUTPUT(PIN_I32);
        case EXECUTION_INTRINSIC_SEQUENCE_GET:
        case EXECUTION_INTRINSIC_SEQUENCE_BORROW: {
            if (count != 2 || !sequence_bank(records, values[0].initial) ||
                !INPUT(1, PIN_I32) || !value_type_valid(records, output)) {
                return false;
            }
            pin_value source = values[0].initial;
            if (source.container == PIN_CONTAINER_SINGLE) {
                return OUTPUT(PIN_I32);
            }
            if (source.type_name && !strncmp(source.type_name, "__collection_", 13)) {
                /* Nested/optional elements have complete dynamic headers;
                 * the collection descriptor supplies their physical storage. */
                return output.type == source.type;
            }
            source.container = PIN_CONTAINER_SINGLE;
            return same_type(source, output);
        }
        case EXECUTION_INTRINSIC_STRING_CURSOR:
            return count == 1 && INPUT(0, PIN_STR) && OUTPUT(BF_POINTER_TYPE);
        case EXECUTION_INTRINSIC_STRING_BYTES:
            return count == 1 && INPUT(0, BF_POINTER_TYPE) && OUTPUT(PIN_I32);
        case EXECUTION_INTRINSIC_STRING_ITEM:
        case EXECUTION_INTRINSIC_STRING_NEXT:
            return count == 2 && INPUT(0, BF_POINTER_TYPE) && INPUT(1, PIN_I32) &&
                   OUTPUT(
                       execution_intrinsic_kind(id) == EXECUTION_INTRINSIC_STRING_ITEM
                           ? PIN_STR
                           : PIN_I32
                   );
        case EXECUTION_INTRINSIC_VALUE_EQUAL:
            return count == 2 && OUTPUT(PIN_BOOL) &&
                   (scalar_bank(values[0].initial, PIN_POINTER) ||
                    map_bank(values[0].initial) ||
                    (values[0].initial.type == PIN_STRUCT &&
                     values[0].initial.container == PIN_CONTAINER_SINGLE &&
                     values[0].initial.record)) &&
                   same_type(values[0].initial, values[1].initial);
        case EXECUTION_INTRINSIC_MAP_MAKE:
            if (count < 3 || !(count & 1) || !map_bank(output) ||
                !same_type(values[0].initial, output) ||
                !pin_map_key_supported(values[1].initial) ||
                !value_type_valid(records, values[2].initial)) {
                return false;
            }
            for (int i = 3; i < count; i += 2) {
                if (!same_type(values[i].initial, values[1].initial) ||
                    !same_type(values[i + 1].initial, values[2].initial)) {
                    return false;
                }
            }
            return true;
        case EXECUTION_INTRINSIC_MAP_GET:
            return (count == 2 ||
                    (count == 3 && same_type(values[2].initial, output))) &&
                   map_bank(values[0].initial) &&
                   pin_map_key_supported(values[1].initial) &&
                   value_type_valid(records, output);
        case EXECUTION_INTRINSIC_MAP_HAS:
            return count == 2 && map_bank(values[0].initial) &&
                   pin_map_key_supported(values[1].initial) && OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_MAP_REMOVE:
            return count == 2 && map_bank(values[0].initial) &&
                   pin_map_key_supported(values[1].initial) &&
                   same_type(values[0].initial, output);
        case EXECUTION_INTRINSIC_MAP_ITEM:
            return count == 3 && map_bank(values[0].initial) && INPUT(1, PIN_I32) &&
                   INPUT(2, PIN_BOOL) && value_type_valid(records, output);
        case EXECUTION_INTRINSIC_REFERENCE_KEY:
            return count == 3 && INPUT(0, BF_POINTER_TYPE) &&
                   value_type_valid(records, values[1].initial) &&
                   pin_map_key_supported(values[2].initial) &&
                   OUTPUT(BF_POINTER_TYPE);
        case EXECUTION_INTRINSIC_SNAPSHOT:
            return count == 1 && (same_type(values[0].initial, output) ||
                                  (INPUT(0, PIN_NAME) && OUTPUT(PIN_STR)) ||
                                  (INPUT(0, PIN_STR) && OUTPUT(PIN_NAME)));
        case EXECUTION_INTRINSIC_UNION_MAKE:
            return count >= 1 && INPUT(0, PIN_U64) && OUTPUT(PIN_UNION);
        case EXECUTION_INTRINSIC_UNION_KEEP:
            return count >= 2 && INPUT(0, PIN_U64) && INPUT(1, PIN_U64) &&
                   OUTPUT(PIN_UNION);
        case EXECUTION_INTRINSIC_UNION_TEST:
            if (count < 2 || !INPUT(0, PIN_UNION) || !OUTPUT(PIN_BOOL)) {
                return false;
            }
            for (int i = 1; i < count; i++) {
                if (!INPUT(i, PIN_U64)) {
                    return false;
                }
            }
            return true;
        case EXECUTION_INTRINSIC_UNION_GET:
            if (count < 4 || !INPUT(0, PIN_UNION) || !INPUT(1, PIN_U64) ||
                !INPUT(2, PIN_I32) || !INPUT(3, PIN_BOOL)) {
                return false;
            }
            for (int i = 4; i < count; i++) {
                if (!INPUT(i, PIN_U64)) {
                    return false;
                }
            }
            return !values[3].initial.b || OUTPUT(PIN_UNION);
        case EXECUTION_INTRINSIC_CONSTRUCT:
            if (count < 1 || output.container == PIN_CONTAINER_MAP ||
                output.container == PIN_CONTAINER_VIEW ||
                !same_type(values[0].initial, output)) {
                return false;
            }
            if (output.container != PIN_CONTAINER_SINGLE) {
                pin_value element = output;
                element.container = PIN_CONTAINER_SINGLE;
                if (count > 1 && output.type_name &&
                    !strncmp(output.type_name, "__collection_", 13)) {
                    element = values[1].initial;
                    if (element.type != output.type ||
                        !value_type_valid(records, element)) {
                        return false;
                    }
                }
                for (int i = 1; i < count; i++) {
                    if (!same_type(values[i].initial, element)) {
                        return false;
                    }
                }
                return true;
            }
            if (output.type != PIN_STRUCT || !(count & 1)) {
                return false;
            }
            for (int i = 1; i < count; i += 2) {
                if (!INPUT(i, PIN_U64)) {
                    return false;
                }
            }
            return true;
        case EXECUTION_INTRINSIC_STATE_DEMAND:
            return count == 3 && INPUT(0, BF_POINTER_TYPE) &&
                   INPUT(1, PIN_U64) && INPUT(2, PIN_I32) && OUTPUT(PIN_I32);
        case EXECUTION_INTRINSIC_STATE_TRANSITION:
            if (count < 3 || count > 6 || !INPUT(0, BF_POINTER_TYPE) ||
                !INPUT(1, PIN_U64) || !INPUT(2, PIN_I32) || !OUTPUT(PIN_I32)) {
                return false;
            }
            for (int i = 3; i < count; i++) {
                if (!INPUT(i, PIN_BOOL) && !INPUT(i, PIN_I32)) {
                    return false;
                }
            }
            return true;
        case EXECUTION_INTRINSIC_RETRIGGER_ACTION:
            return count == 3 && INPUT(0, BF_POINTER_TYPE) &&
                   INPUT(1, PIN_U64) && INPUT(2, PIN_F64) && OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_QUERY_TEST:
            return count == 2 && INPUT(0, BF_POINTER_TYPE) &&
                   INPUT(1, PIN_I32) && OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_QUERY_MAKE:
            return count >= 2 && INPUT(0, BF_POINTER_TYPE) &&
                   INPUT(1, PIN_I32) && OUTPUT(PIN_QUERY);
        case EXECUTION_INTRINSIC_QUERY_BEGIN:
            return count == 2 && INPUT(0, BF_POINTER_TYPE) &&
                   INPUT(1, PIN_QUERY) && OUTPUT(BF_POINTER_TYPE);
        case EXECUTION_INTRINSIC_QUERY_NEXT:
            return count == 2 && INPUT(0, BF_POINTER_TYPE) &&
                   INPUT(1, BF_POINTER_TYPE) && host->result_count == 2 &&
                   OUTPUT(PIN_BOOL) &&
                   scalar_bank(f->data_values[host->result_first + 1].initial, PIN_QUERY);
        case EXECUTION_INTRINSIC_QUERY_END:
            return count == 2 && INPUT(0, BF_POINTER_TYPE) &&
                   INPUT(1, BF_POINTER_TYPE) && OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_QUERY_ROW_GET:
            return count == 2 && INPUT(0, PIN_QUERY) && INPUT(1, PIN_I32);
        case EXECUTION_INTRINSIC_QUERY_CURRENT_ROW:
            return count == 1 && INPUT(0, BF_POINTER_TYPE) && OUTPUT(PIN_QUERY);
        case EXECUTION_INTRINSIC_COMPONENT_SNAPSHOT:
            return count == 6 && INPUT(0, BF_POINTER_TYPE) && INPUT(1, PIN_HANDLE) &&
                   INPUT(2, PIN_U64) && values[3].initial.type == PIN_STRUCT &&
                   same_type(values[3].initial, output) && INPUT(4, PIN_U64) &&
                   INPUT(5, PIN_U64);
        case EXECUTION_INTRINSIC_COMPONENT_POINTER:
            return count == 6 && INPUT(0, BF_POINTER_TYPE) && INPUT(1, PIN_HANDLE) &&
                   INPUT(2, PIN_U64) && values[3].initial.type == PIN_STRUCT &&
                   value_type_valid(records, values[3].initial) &&
                   INPUT(4, PIN_U64) && INPUT(5, PIN_U64) && OUTPUT(PIN_POINTER);
        case EXECUTION_INTRINSIC_POINTER_MAKE:
            return count == 3 && INPUT(0, BF_POINTER_TYPE) &&
                   same_type(values[1].initial, values[2].initial) &&
                   OUTPUT(PIN_POINTER);
        case EXECUTION_INTRINSIC_POINTER_READ:
            return count == 2 && INPUT(0, PIN_POINTER) &&
                   same_type(values[1].initial, output);
        case EXECUTION_INTRINSIC_POINTER_WRITE:
            return count == 2 && INPUT(0, PIN_POINTER) && OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_POINTER_FIELD:
            return count == 5 && INPUT(0, PIN_POINTER) && INPUT(2, PIN_U64) &&
                   INPUT(3, PIN_BOOL) && values[4].initial.type == PIN_STRUCT &&
                   values[4].initial.container == PIN_CONTAINER_SINGLE &&
                   value_type_valid(records, values[4].initial) &&
                   OUTPUT(PIN_POINTER);
        case EXECUTION_INTRINSIC_POINTER_REFERENCE:
            return count == 1 && INPUT(0, PIN_POINTER) && OUTPUT(BF_POINTER_TYPE);
        case EXECUTION_INTRINSIC_REFERENCE_MAKE:
            return count == 9 && INPUT(0, BF_POINTER_TYPE) && INPUT(2, PIN_I32) &&
                   INPUT(3, PIN_I32) && INPUT(4, BF_POINTER_TYPE) &&
                   INPUT(5, PIN_HANDLE) && INPUT(6, PIN_U64) && INPUT(7, PIN_U64) &&
                   INPUT(8, PIN_U64) && OUTPUT(BF_POINTER_TYPE);
        case EXECUTION_INTRINSIC_REFERENCE_FIELD:
            return count == 4 && INPUT(0, BF_POINTER_TYPE) && INPUT(3, PIN_BOOL) &&
                   INPUT(2, values[3].initial.b ? PIN_I32 : PIN_U64) &&
                   OUTPUT(BF_POINTER_TYPE);
        case EXECUTION_INTRINSIC_REFERENCE_READ:
            return (count == 1 ||
                    (count == 2 && same_type(values[1].initial, output))) &&
                   INPUT(0, BF_POINTER_TYPE);
        case EXECUTION_INTRINSIC_REFERENCE_WRITE:
            return count == 2 && INPUT(0, BF_POINTER_TYPE) && OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_REFERENCE_REDUCE:
            return count == 3 && INPUT(0, BF_POINTER_TYPE) &&
                   values[1].initial.container == PIN_CONTAINER_SINGLE &&
                   values[1].initial.type >= PIN_I8 &&
                   values[1].initial.type <= PIN_F64 && INPUT(2, PIN_I32) &&
                   OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_LOCAL_RELEASE:
            return count == 1 && INPUT(0, BF_POINTER_TYPE) && OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_ARRAY_PUSH:
            return count == 2 && INPUT(0, BF_POINTER_TYPE) &&
                   values[1].initial.container == PIN_CONTAINER_SINGLE &&
                   OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_ARRAY_EXTEND:
            return count == 2 && INPUT(0, BF_POINTER_TYPE) &&
                   values[1].initial.container == PIN_CONTAINER_ARRAY &&
                   OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_ARRAY_STORE:
            return count == 3 && INPUT(0, BF_POINTER_TYPE) && INPUT(1, PIN_I32) &&
                   values[2].initial.container == PIN_CONTAINER_SINGLE &&
                   OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_MAP_STORE:
            return count == 3 && INPUT(0, BF_POINTER_TYPE) && OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_MAP_DELETE:
            return count == 2 && INPUT(0, BF_POINTER_TYPE) && OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_ACTION_READ:
            return count == 3 && INPUT(0, BF_POINTER_TYPE) &&
                   INPUT(1, PIN_U64) && INPUT(2, PIN_I32);
        case EXECUTION_INTRINSIC_ACTION_COMPLETE:
            return count >= 2 && INPUT(0, BF_POINTER_TYPE) &&
                   INPUT(1, PIN_U64) && OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_COMPONENT_HAS:
            return count == 3 && INPUT(0, BF_POINTER_TYPE) &&
                   INPUT(1, PIN_HANDLE) && INPUT(2, PIN_U64) && OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_QUERY_SOURCE:
            return count == 5 && INPUT(0, BF_POINTER_TYPE) &&
                   INPUT(1, PIN_HANDLE) && INPUT(2, PIN_U64) &&
                   INPUT(3, PIN_U64) && INPUT(4, PIN_I32) && OUTPUT(PIN_HANDLE);
        case EXECUTION_INTRINSIC_QUERY_TARGET:
            return count == 4 && INPUT(0, BF_POINTER_TYPE) && INPUT(1, PIN_HANDLE) &&
                   INPUT(2, PIN_U64) && INPUT(3, PIN_I32) && OUTPUT(PIN_HANDLE);
        case EXECUTION_INTRINSIC_COMPONENT_MEMBERSHIP:
            return count == 4 && INPUT(0, BF_POINTER_TYPE) && INPUT(1, PIN_HANDLE) &&
                   INPUT(2, PIN_U64) && INPUT(3, PIN_BOOL) && OUTPUT(PIN_BOOL);
        case EXECUTION_INTRINSIC_BREAK_REQUEST:
        case EXECUTION_INTRINSIC_BREAK_TAKE:
            return count == 2 && INPUT(0, BF_POINTER_TYPE) &&
                   INPUT(1, PIN_U64) && OUTPUT(PIN_BOOL);
        default: return false;
    }
#undef INPUT
#undef OUTPUT
}
static bool value_kind_valid(behavior_value_kind kind, pin_value initial)
{
    return kind >= BEHAVIOR_VALUE && kind <= BEHAVIOR_QUERY_CURSOR &&
           (kind == BEHAVIOR_VALUE || ((scalar_bank(initial, BF_POINTER_TYPE) ||
                                        scalar_bank(initial, PIN_UNION)) &&
                                       !initial.u64));
}
static behavior_value_kind payload_kind(pin_value value, behavior_value_kind kind)
{
    return value.type == PIN_STRUCT || value.container != PIN_CONTAINER_SINGLE
             ? BEHAVIOR_ADDRESS
             : kind;
}
/* Only a direct canonical header address establishes its declared payload
 * provenance. Arbitrary raw loads cannot manufacture logical references. */
static bool context_address(size_t offset)
{
#define FIELD(name) case offsetof(behavior_function_context, name):
    /* clang-format reflows this macro run differently on each pass. */
    // clang-format off
    switch (offset) {
        FIELD(parent)
        FIELD(activation)
        FIELD(data)
        FIELD(program_name)
        FIELD(instance_time)
        FIELD(inspection_user)
        FIELD(points)
        FIELD(variables)
        FIELD(states)
        FIELD(arguments)
        FIELD(results)
        FIELD(error_message)
        FIELD(services)
        FIELD(queries)
        FIELD(commands)
        FIELD(command_arguments)
        FIELD(command_results)
        FIELD(owner_type_name)
        FIELD(world)
        FIELD(binding_world)
        FIELD(columns)
        FIELD(variable_storage)
        FIELD(value_owner)
        FIELD(value_storage)
        FIELD(result_storage)
        FIELD(watches)
        FIELD(edges)
        FIELD(watch_storage)
            return true;
        default: return false;
    }
#undef FIELD
    // clang-format on
}
static const char *verify_instruction_kinds(const behavior_function *f, int pc, const int *headers)
{
    const behavior_function_instruction *i = &f->instructions[pc];
#define KIND(slot) f->slot_kinds[slot]
#define CHECK_KIND(condition)                                                  \
    do {                                                                       \
        if (!(condition)) return "provenance mismatch: " #condition;           \
    } while (0)
    if (i->indirect) {
        CHECK_KIND(
            i->op >= BF_STORE ? KIND(i->left) == BEHAVIOR_VALUE : KIND(i->output) == BEHAVIOR_VALUE
        );
        return NULL;
    }
    behavior_value_kind output = BEHAVIOR_VALUE;
    bool produces = i->op < BF_STORE;
    switch (i->op) {
        case BF_CONSTANT:
            output = KIND(i->output);
            CHECK_KIND(value_kind_valid(output, i->value));
            break;
        case BF_ARGUMENT: output = f->argument_kinds[i->left]; break;
        case BF_LOCAL: output = f->locals[i->left].kind; break;
        case BF_SAVE:
            CHECK_KIND(KIND(i->left) == f->locals[i->target].kind);
            break;
        case BF_RESULT:
            CHECK_KIND(KIND(i->left) == f->result_kinds[i->target]);
            break;
        case BF_STORE: CHECK_KIND(KIND(i->left) == BEHAVIOR_VALUE); break;
        case BF_STORE_VALUE:
            CHECK_KIND(f->data_values[i->left].kind == BEHAVIOR_VALUE);
            break;
        case BF_ARRAY:
        case BF_TYPE_NAME:
        case BF_STORAGE_NAME:
        case BF_HOST_SLOT:
        case BF_HOST_SERVICE:
        case BF_COMMAND_SERVICE:
        case BF_QUERY:
        case BF_WORLD: output = BEHAVIOR_ADDRESS; break;
        case BF_CONTEXT:
            output = i->target == 1 || context_address(i->offset) ? BEHAVIOR_ADDRESS
                                                                  : BEHAVIOR_VALUE;
            CHECK_KIND(
                output != BEHAVIOR_ADDRESS || i->type == BF_POINTER_TYPE || i->type == PIN_UNION
            );
            break;
        case BF_LOAD:
        case BF_READ: {
            CHECK_KIND(KIND(i->left) == BEHAVIOR_ADDRESS);
            int bank = headers[i->left];
            output = KIND(i->output);
            if (bank >= 0 && i->offset == offsetof(pin_value, d)) {
                CHECK_KIND(
                    output == payload_kind(
                                  f->data_values[bank].initial,
                                  f->data_values[bank].kind
                              )
                );
            } else if (
                bank >= 0 && (i->offset == offsetof(pin_value, type_name) ||
                              i->offset == offsetof(pin_value, record) ||
                              i->offset == offsetof(pin_value, schema))
            ) {
                CHECK_KIND(output == BEHAVIOR_ADDRESS);
            } else {
                CHECK_KIND(output == BEHAVIOR_VALUE || output == BEHAVIOR_ADDRESS);
                CHECK_KIND(
                    output != BEHAVIOR_ADDRESS || i->type == BF_POINTER_TYPE ||
                    i->type == PIN_UNION
                );
            }
            break;
        }
        case BF_WRITE: {
            CHECK_KIND(KIND(i->right) == BEHAVIOR_ADDRESS);
            int bank = headers[i->right];
            if (bank >= 0 && i->offset == offsetof(pin_value, d)) {
                CHECK_KIND(
                    KIND(i->left) == payload_kind(
                                         f->data_values[bank].initial,
                                         f->data_values[bank].kind
                                     )
                );
            } else if (
                bank >= 0 && (i->offset == offsetof(pin_value, type_name) ||
                              i->offset == offsetof(pin_value, record) ||
                              i->offset == offsetof(pin_value, schema))
            ) {
                CHECK_KIND(KIND(i->left) == BEHAVIOR_ADDRESS);
            } else if (bank >= 0) {
                /* Snapshots and constructors copy by the runtime type and
                 * container, which must stay the declared ones. */
                return "data cell header field is not writable";
            }
            break;
        }
        case BF_ADD:
            CHECK_KIND(KIND(i->right) == BEHAVIOR_VALUE);
            CHECK_KIND(KIND(i->left) == BEHAVIOR_VALUE || KIND(i->left) == BEHAVIOR_ADDRESS);
            if (KIND(i->left) == BEHAVIOR_ADDRESS) {
                CHECK_KIND(i->type == BF_POINTER_TYPE);
                output = BEHAVIOR_ADDRESS;
            }
            break;
        case BF_INDEX:
        case BF_ROW:
            CHECK_KIND(KIND(i->left) == BEHAVIOR_ADDRESS && KIND(i->right) == BEHAVIOR_VALUE);
            output = BEHAVIOR_ADDRESS;
            break;
        case BF_COMPONENT_READ:
            CHECK_KIND(KIND(i->left) == BEHAVIOR_VALUE);
            break;
        case BF_COMPONENT_WRITE:
            CHECK_KIND(KIND(i->left) == BEHAVIOR_VALUE && KIND(i->right) == BEHAVIOR_VALUE);
            break;
        case BF_ENTITY:
            CHECK_KIND(KIND(i->left) == BEHAVIOR_ADDRESS && KIND(i->right) == BEHAVIOR_VALUE);
            break;
        case BF_COMPONENT:
        case BF_COLUMN:
        case BF_COLUMN_TRY:
            CHECK_KIND(KIND(i->left) == BEHAVIOR_ADDRESS);
            CHECK_KIND(i->op != BF_COMPONENT || KIND(i->right) == BEHAVIOR_VALUE);
            output = i->type == PIN_BOOL ? BEHAVIOR_VALUE : BEHAVIOR_ADDRESS;
            break;
        case BF_OWN_NAME:
        case BF_COPY_ARRAY:
            CHECK_KIND(KIND(i->left) == BEHAVIOR_ADDRESS);
            output = BEHAVIOR_ADDRESS;
            break;
        case BF_INTERN_STRING:
            CHECK_KIND(KIND(i->left) == BEHAVIOR_VALUE && KIND(i->right) == BEHAVIOR_ADDRESS);
            break;
        case BF_STRING_EQUAL:
            CHECK_KIND(KIND(i->left) <= BEHAVIOR_ADDRESS && KIND(i->right) <= BEHAVIOR_ADDRESS);
            CHECK_KIND(
                KIND(i->left) != BEHAVIOR_VALUE ||
                behavior_scalar_string(f->slots[i->left])
            );
            CHECK_KIND(
                KIND(i->right) != BEHAVIOR_VALUE ||
                behavior_scalar_string(f->slots[i->right])
            );
            break;
        case BF_SELECT:
            CHECK_KIND(KIND(i->left) == BEHAVIOR_VALUE && KIND(i->right) == KIND(i->target));
            output = KIND(i->right);
            break;
        case BF_EQ:
        case BF_NE: CHECK_KIND(KIND(i->left) == KIND(i->right)); break;
        case BF_STORE_HEADER:
        case BF_STORE_TYPE_NAME:
            CHECK_KIND(KIND(i->left) == BEHAVIOR_ADDRESS);
            CHECK_KIND(i->op != BF_STORE_HEADER || KIND(i->right) == BEHAVIOR_VALUE);
            break;
        case BF_RESULT_PRESENT:
            CHECK_KIND(KIND(i->left) == BEHAVIOR_VALUE);
            break;
        case BF_RESULT_TYPE_NAME:
        case BF_RESULT_ARRAY:
            CHECK_KIND(KIND(i->left) == BEHAVIOR_ADDRESS);
            CHECK_KIND(i->op != BF_RESULT_ARRAY || KIND(i->right) == BEHAVIOR_ADDRESS);
            break;
        case BF_COMMAND_ARGUMENT:
        case BF_COMMAND_OUTPUT:
        case BF_COMMAND_RESULT: {
            bool argument = i->op == BF_COMMAND_ARGUMENT;
            int index = i->op == BF_COMMAND_RESULT ? i->left : i->target;
            pin_value initial = (argument ? f->command_arguments : f->command_results)[index];
            output = i->offset == offsetof(pin_value, type_name)
                       ? BEHAVIOR_ADDRESS
                       : payload_kind(
                             initial,
                             (argument ? f->command_argument_kinds
                                       : f->command_result_kinds)[index]
                         );
            CHECK_KIND(i->op == BF_COMMAND_RESULT || KIND(i->left) == output);
            break;
        }
        case BF_SOURCE_WATCH: {
            const behavior_source_watch *watch = &f->source_watches[i->target];
            CHECK_KIND(
                KIND(i->left) ==
                (watch->reference ? BEHAVIOR_REFERENCE
                                  : payload_kind(watch->initial, BEHAVIOR_VALUE))
            );
            break;
        }
        case BF_ASSERT:
            break; /* Presence checks do not dereference or retag. */
        default:
            CHECK_KIND(!behavior_function_left_slot(i->op) || KIND(i->left) == BEHAVIOR_VALUE);
            CHECK_KIND(!behavior_function_right_slot(i->op) || KIND(i->right) == BEHAVIOR_VALUE);
            CHECK_KIND(!behavior_function_third_slot(i->op) || KIND(i->target) == BEHAVIOR_VALUE);
            break;
    }
    CHECK_KIND(!produces || KIND(i->output) == output);
    if ((i->op == BF_DIV || i->op == BF_MOD) && i->target >= 0) {
        CHECK_KIND(KIND(i->target) == BEHAVIOR_VALUE);
    }
    return NULL;
#undef CHECK_KIND
#undef KIND
}
static const char *verify_provenance(const behavior_function *f, int *failed)
{
    if ((size_t)acount(f->slots) > (64u * 1024u * 1024u) / sizeof(int)) {
        return "definition exceeds provenance storage budget";
    }
    int *headers = xmalloc((size_t)acount(f->slots) * sizeof *headers);
    for (int slot = 0; slot < acount(f->slots); slot++) {
        headers[slot] = -2;
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const behavior_function_instruction *i = &f->instructions[pc];
        if (i->op >= BF_STORE) {
            continue;
        }
        int bank = i->op == BF_HOST_SLOT && !i->target ? i->left : -1;
        int *origin = &headers[i->output];
        *origin = *origin == -2 || *origin == bank ? bank : -1;
    }
    const char *error = NULL;
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        error = verify_instruction_kinds(f, pc, headers);
        if (error) {
            *failed = pc;
            break;
        }
    }
    free(headers);
    return error;
}
static const char *verify_kinds(const behavior_function *f)
{
    if (acount(f->slot_kinds) != acount(f->slots) ||
        acount(f->argument_kinds) != f->argument_count ||
        acount(f->result_kinds) != f->result_count ||
        acount(f->command_argument_kinds) != acount(f->command_arguments) ||
        acount(f->command_result_kinds) != acount(f->command_results)) {
        return "provenance bank count differs from declaration";
    }
    for (int i = 0; i < acount(f->slots); i++) {
        behavior_value_kind kind = f->slot_kinds[i];
        if (kind < BEHAVIOR_VALUE || kind > BEHAVIOR_QUERY_CURSOR ||
            (kind != BEHAVIOR_VALUE && f->slots[i] != BF_POINTER_TYPE &&
             f->slots[i] != PIN_UNION)) {
            return "invalid slot provenance";
        }
    }
    const pin_value *banks[] = { f->argument_defaults,
                                 f->result_defaults,
                                 f->command_arguments,
                                 f->command_results };
    const behavior_value_kind *kinds[] = { f->argument_kinds,
                                           f->result_kinds,
                                           f->command_argument_kinds,
                                           f->command_result_kinds };
    for (int bank = 0; bank < 4; bank++) {
        for (int i = 0; i < acount(kinds[bank]); i++) {
            /* Cursors belong to this invocation, never to callable/event
             * ingress, results, tasks or external command payloads. */
            if ((kinds[bank][i] == BEHAVIOR_STRING_CURSOR ||
                 kinds[bank][i] == BEHAVIOR_QUERY_CURSOR)) {
                return "string cursor cannot cross an invocation boundary";
            }
            if (!value_kind_valid(kinds[bank][i], banks[bank][i])) {
                return "invalid signature or command provenance";
            }
        }
    }
    for (int i = 0; i < acount(f->locals); i++) {
        if (!value_kind_valid(f->locals[i].kind, f->locals[i].initial)) {
            return "invalid local provenance";
        }
    }
    for (int i = 0; i < acount(f->data_values); i++) {
        if (!value_kind_valid(f->data_values[i].kind, f->data_values[i].initial)) {
            return "invalid host value provenance";
        }
    }
    return NULL;
}
static bool intrinsic_kinds(const behavior_function *f, const behavior_function_host_call *host)
{
    uint64_t id = execution_intrinsic_id(host->call);
    if (!id) {
        for (int i = host->first; i < host->result_first + host->result_count; i++) {
            if (f->data_values[i].kind != BEHAVIOR_VALUE) {
                return false;
            }
        }
        return true;
    }
    id &= UINT64_C(0xffffffff);
    int count = host->result_first - host->first;
    const behavior_function_data_value *values = f->data_values + host->first;
    behavior_value_kind result = f->data_values[host->result_first].kind;
    switch (id) {
        case EXECUTION_INTRINSIC_SEQUENCE_SLICE:
        case EXECUTION_INTRINSIC_SEQUENCE_VIEW:
        case EXECUTION_INTRINSIC_SEQUENCE_CLONE:
        case EXECUTION_INTRINSIC_SEQUENCE_GET:
        case EXECUTION_INTRINSIC_SEQUENCE_BORROW:
        case EXECUTION_INTRINSIC_SEQUENCE_LENGTH:
        case EXECUTION_INTRINSIC_VALUE_COPY:
        case EXECUTION_INTRINSIC_VALUE_DROP:
            /* These banks hold complete typed values. Raw addresses, logical
             * references and cursors cannot masquerade as sequence payloads. */
            for (int i = 0; i < count; i++) {
                if (values[i].kind != BEHAVIOR_VALUE) {
                    return false;
                }
            }
            return result == BEHAVIOR_VALUE;
        default: break;
    }
    for (int i = 0; i < count; i++) {
        behavior_value_kind expected = BEHAVIOR_VALUE;
        if (i == 0) {
            switch (id) {
                case EXECUTION_INTRINSIC_STRING_BYTES:
                case EXECUTION_INTRINSIC_STRING_ITEM:
                case EXECUTION_INTRINSIC_STRING_NEXT:
                    expected = BEHAVIOR_STRING_CURSOR;
                    break;
                case EXECUTION_INTRINSIC_REFERENCE_FIELD:
                case EXECUTION_INTRINSIC_REFERENCE_KEY:
                case EXECUTION_INTRINSIC_REFERENCE_READ:
                case EXECUTION_INTRINSIC_REFERENCE_WRITE:
                case EXECUTION_INTRINSIC_REFERENCE_REDUCE:
                case EXECUTION_INTRINSIC_LOCAL_RELEASE:
                case EXECUTION_INTRINSIC_ARRAY_MUTATE_PLACE:
                case EXECUTION_INTRINSIC_REFERENCE_UNION:
                case EXECUTION_INTRINSIC_MAP_DELETE_PLACE:
                    expected = BEHAVIOR_REFERENCE;
                    break;
                case EXECUTION_INTRINSIC_QUERY_TEST:
                case EXECUTION_INTRINSIC_QUERY_MAKE:
                case EXECUTION_INTRINSIC_QUERY_BEGIN:
                case EXECUTION_INTRINSIC_QUERY_NEXT:
                case EXECUTION_INTRINSIC_QUERY_END:
                case EXECUTION_INTRINSIC_QUERY_CURRENT_ROW:
                case EXECUTION_INTRINSIC_COMPONENT_SNAPSHOT:
                case EXECUTION_INTRINSIC_COMPONENT_POINTER:
                case EXECUTION_INTRINSIC_POINTER_MAKE:
                case EXECUTION_INTRINSIC_REFERENCE_MAKE:
                case EXECUTION_INTRINSIC_ACTION_READ:
                case EXECUTION_INTRINSIC_ACTION_COMPLETE:
                case EXECUTION_INTRINSIC_COMPONENT_HAS:
                case EXECUTION_INTRINSIC_QUERY_SOURCE:
                case EXECUTION_INTRINSIC_QUERY_TARGET:
                case EXECUTION_INTRINSIC_COMPONENT_MEMBERSHIP:
                case EXECUTION_INTRINSIC_BREAK_REQUEST:
                case EXECUTION_INTRINSIC_BREAK_TAKE:
                case EXECUTION_INTRINSIC_STATE_DEMAND:
                case EXECUTION_INTRINSIC_STATE_TRANSITION:
                case EXECUTION_INTRINSIC_RETRIGGER_ACTION:
                case EXECUTION_INTRINSIC_ARRAY_PUSH:
                case EXECUTION_INTRINSIC_ARRAY_STORE:
                case EXECUTION_INTRINSIC_ARRAY_EXTEND:
                case EXECUTION_INTRINSIC_MAP_STORE:
                case EXECUTION_INTRINSIC_MAP_DELETE:
                    expected = BEHAVIOR_ADDRESS;
                    break;
            }
        }
        if ((id == EXECUTION_INTRINSIC_QUERY_NEXT || id == EXECUTION_INTRINSIC_QUERY_END) &&
            i == 1) {
            expected = BEHAVIOR_QUERY_CURSOR;
        }
        if (id == EXECUTION_INTRINSIC_REFERENCE_MAKE && i == 4) {
            expected = BEHAVIOR_ADDRESS;
        }
        if (values[i].kind != expected) {
            return false;
        }
    }
    if (id == EXECUTION_INTRINSIC_STRING_CURSOR) {
        return result == BEHAVIOR_STRING_CURSOR;
    }
    if (id == EXECUTION_INTRINSIC_QUERY_BEGIN) {
        return result == BEHAVIOR_QUERY_CURSOR;
    }
    return result == (id == EXECUTION_INTRINSIC_POINTER_REFERENCE ||
                              id == EXECUTION_INTRINSIC_REFERENCE_UNION ||
                              id == EXECUTION_INTRINSIC_REFERENCE_MAKE ||
                              id == EXECUTION_INTRINSIC_REFERENCE_FIELD ||
                              id == EXECUTION_INTRINSIC_REFERENCE_KEY
                          ? BEHAVIOR_REFERENCE
                          : BEHAVIOR_VALUE);
}
static bool source_point_valid(behavior_source_point point)
{
    return point.occurrence && point.start >= 0 && point.end >= point.start &&
           point.line >= 0 && point.column >= 0 && point.kind >= 0 &&
           point.kind < BEHAVIOR_SOURCE_KIND_COUNT;
}
static const char *source_watch_error(record_validation *records, const behavior_source_watch *watch)
{
    if (!source_point_valid(watch->origin)) {
        return "origin";
    }
    if (watch->ordinal < 0) {
        return "ordinal";
    }
    if (!value_type_valid(records, watch->initial)) {
        return "value header";
    }
    return NULL;
}
static const char *verify_value_roots(record_validation *records, const behavior_function *f)
{
    if (!acount(f->value_roots)) {
        return NULL;
    }
    size_t slots = (size_t)acount(f->slots), locals = (size_t)acount(f->locals);
    bool *seen = xcalloc(slots + locals + 1, sizeof *seen);
    const char *error = NULL;
    for (int i = 0; i < acount(f->value_roots); i++) {
        const behavior_value_root *root = &f->value_roots[i];
        bool slot = root->bank == BEHAVIOR_BANK_SLOT;
        if ((!slot && root->bank != BEHAVIOR_BANK_LOCAL) || root->index < 0 ||
            root->index >= (slot ? acount(f->slots) : acount(f->locals)) ||
            (slot ? f->slots[root->index] : f->locals[root->index].initial.type) !=
                BF_POINTER_TYPE ||
            behavior_function_kind(f, root->bank, root->index) != BEHAVIOR_ADDRESS ||
            !value_type_valid(records, root->shape) || root->shape.ptr ||
            (root->shape.container == PIN_CONTAINER_SINGLE &&
             root->shape.type != PIN_STRUCT)) {
            error = "invalid saved aggregate root metadata";
            break;
        }
        size_t index = (size_t)root->index + (slot ? 0 : slots);
        if (seen[index]) {
            error = "duplicate saved aggregate root metadata";
            break;
        }
        seen[index] = true;
    }
    free(seen);
    return error;
}
static const char *verify_metadata(record_validation *records, const behavior_function *f)
{
    if (f->argument_count != acount(f->argument_defaults) ||
        f->result_count != acount(f->result_defaults) ||
        f->variable_count != acount(f->variable_defaults)) {
        return "signature bank count differs from declaration";
    }
    const char *kind_error = verify_kinds(f);
    if (kind_error) {
        return kind_error;
    }
    const char *root_error = verify_value_roots(records, f);
    if (root_error) {
        return root_error;
    }
    for (int i = 0; i < acount(f->source_points); i++) {
        if (!source_point_valid(f->source_points[i])) {
            return "invalid source point origin";
        }
    }
    for (int i = 0; i < acount(f->source_watches); i++) {
        const behavior_source_watch *watch = &f->source_watches[i];
        if (source_watch_error(records, watch)) {
            return "invalid source watch metadata";
        }
    }
    int values = acount(f->data_values);
    for (int i = 0; i < acount(f->host_calls); i++) {
        const behavior_function_host_call *v = &f->host_calls[i];
        if (v->first < 0 || v->result_first < v->first || v->result_first > values ||
            v->result_count < 0 || v->result_count > values - v->result_first) {
            return "host bank range exceeds values";
        }
    }
    for (int i = 0; i < acount(f->calls); i++) {
        const behavior_function_invocation *v = &f->calls[i];
        if (v->first < 0 || v->first > values || v->count < 0 ||
            v->count > values - v->first || v->result_first < 0 ||
            v->result_first > values || v->result_count < 0 ||
            v->result_count > values - v->result_first) {
            return "invocation bank range exceeds values";
        }
    }
    for (int i = 0; i < acount(f->commands); i++) {
        const behavior_function_command *v = &f->commands[i];
        int arguments = acount(f->command_arguments),
            results = acount(f->command_results);
        if (v->first < 0 || v->first > arguments || v->count < 0 ||
            v->count > arguments - v->first || v->result_first < 0 ||
            v->result_first > results || v->result_count < 0 ||
            v->result_count > results - v->result_first) {
            return "command bank range exceeds values";
        }
    }

    const pin_value *banks[] = { f->argument_defaults,
                                 f->result_defaults,
                                 f->variable_defaults,
                                 f->command_arguments,
                                 f->command_results };
    int counts[] = { f->argument_count,
                     f->result_count,
                     f->variable_count,
                     acount(f->command_arguments),
                     acount(f->command_results) };
    for (int bank = 0; bank < 5; bank++) {
        for (int i = 0; i < counts[bank]; i++) {
            if (!value_type_valid(records, banks[bank][i])) {
                return "invalid signature or command value type";
            }
        }
    }
    for (int i = 0; i < acount(f->results); i++) {
        int index = f->results[i].index;
        if (index < 0 || index >= f->result_count ||
            f->result_defaults[index].type != PIN_STRUCT ||
            f->result_defaults[index].container != PIN_CONTAINER_SINGLE ||
            f->results[i].type != pin_value_type(&f->result_defaults[index])) {
            return "result buffer layout differs from its declared header";
        }
        for (int j = 0; j < i; j++) {
            if (f->results[j].index == index) {
                return "result buffer index is duplicated";
            }
        }
    }
    for (int i = 0; i < acount(f->sources); i++) {
        const behavior_function_source *source = &f->sources[i];
        if (source->parent < -1 || source->parent >= i || source->start < 0 ||
            source->end < source->start || source->line < 0 || source->column < 0) {
            return "invalid source origin";
        }
    }
    for (int i = 0; i < acount(f->data_values); i++) {
        if (!value_type_valid(records, f->data_values[i].initial)) {
            return "invalid host value type";
        }
        const behavior_function_data_value *v = &f->data_values[i];
        const type_desc *type = v->initial.type == PIN_STRUCT &&
                                        v->initial.container == PIN_CONTAINER_SINGLE
                                  ? pin_value_type(&v->initial)
                                  : NULL;
        if (v->size && (!type || v->size != type->size)) {
            return "host buffer size differs from reflected layout";
        }
    }
    for (int i = 0; i < acount(f->host_calls); i++) {
        const behavior_function_host_call *host = &f->host_calls[i];
        const node_host_call *call = host->call;
        if (!call ||
            (!!call->invoke + !!call->value + !!call->context + !!call->action != 1) ||
            (call->value && (call->service || host->result_count != 1))) {
            return "host capability requires one implementation and a valid "
                   "result bank";
        }
        if (call->action) {
            if (call->reusable ||
                !outcome_ids_valid(host->outcomes, host->outcome_count)) {
                return "action requires unique outcomes and cannot be reused";
            }
        } else if (host->outcome_count || host->outcomes) {
            return "nonaction capability has action outcomes";
        }
        if (!intrinsic_schema(records, f, host)) {
            return "invalid intrinsic host value schema";
        }
        if (!intrinsic_kinds(f, host)) {
            return "invalid host value provenance schema";
        }
        const node_access *access = host->access;
        if (access && (access->read_count < 0 || access->write_count < 0 ||
                       (access->read_count && !access->reads) ||
                       (access->write_count && !access->writes) ||
                       access->target_input < 0 ||
                       access->target_input > host->result_first - host->first)) {
            return "invalid host access metadata";
        }
    }
    for (int i = 0; i < acount(f->queries); i++) {
        if (!f->queries[i].service || !f->queries[i].read) {
            return "query requires a service and implementation";
        }
    }
    for (int i = 0; i < acount(f->commands); i++) {
        const node_command *command = f->commands[i].command;
        const behavior_function_command *spec = &f->commands[i];
        if (!command) {
            return "command requires a descriptor";
        }
        for (int j = 0; j < spec->count; j++) {
            if (f->command_argument_kinds[spec->first + j] != BEHAVIOR_VALUE) {
                return "native command arguments require logical values";
            }
        }
        for (int j = 0; j < spec->result_count; j++) {
            if (f->command_result_kinds[spec->result_first + j] != BEHAVIOR_VALUE) {
                return "native command results require logical values";
            }
        }
        switch (command->intrinsic) {
            case NODE_COMMAND_NATIVE:
                if (!command->invoke && !command->begin) {
                    return "command requires an implementation";
                }
                break;
            case NODE_COMMAND_DELAY:
                if (command->invoke || command->begin || command->service ||
                    spec->count != 1 || spec->result_count ||
                    !index_valid(spec->first, acount(f->command_arguments)) ||
                    f->command_arguments[spec->first].type != PIN_F64 ||
                    f->command_arguments[spec->first].container != PIN_CONTAINER_SINGLE) {
                    return "invalid delay command schema";
                }
                break;
            case NODE_COMMAND_YIELD:
            case NODE_COMMAND_TASK_JOIN:
            case NODE_COMMAND_TASK_CANCEL:
            case NODE_COMMAND_TASK_RUNNING:
            case NODE_COMMAND_TASK_CANCEL_AFTER: {
                bool handle = command->intrinsic != NODE_COMMAND_YIELD;
                bool join = command->intrinsic == NODE_COMMAND_TASK_JOIN;
                bool timed = command->intrinsic == NODE_COMMAND_TASK_CANCEL_AFTER;
                if (command->invoke || command->begin || command->service ||
                    spec->count != (int)handle + (int)(join || timed) ||
                    (handle && f->command_arguments[spec->first].type != PIN_TASK) ||
                    ((join || timed) && f->command_arguments[spec->first + 1].type !=
                                            (join ? PIN_U64 : PIN_F64)) ||
                    (join && (spec->result_count != 1 ||
                              f->command_results[spec->result_first].type != PIN_UNION)) ||
                    (command->intrinsic == NODE_COMMAND_TASK_RUNNING
                         ? spec->result_count != 1 ||
                               f->command_results[spec->result_first].type != PIN_BOOL
                         : command->intrinsic != NODE_COMMAND_TASK_JOIN &&
                               spec->result_count)) {
                    return "invalid task command schema";
                }
                break;
            }
            default: return "invalid command intrinsic";
        }
        if (spec->result_pins) {
            for (int pin = 0; pin < spec->result_count; pin++) {
                if (spec->result_pins[pin] < 0) {
                    return "invalid command completion pin";
                }
                for (int before = 0; before < pin; before++) {
                    if (spec->result_pins[before] == spec->result_pins[pin]) {
                        return "duplicate command completion pin";
                    }
                }
            }
        }
    }
    for (int i = 0; i < acount(f->components); i++) {
        if (!f->components[i].id) {
            return "component requires a nonzero identity";
        }
    }
    for (int i = 0; i < acount(f->calls); i++) {
        const behavior_function_invocation *call = &f->calls[i];
        const behavior_function *callee = call->function;
        if (!callee || callee->argument_count != acount(callee->argument_defaults) ||
            callee->result_count != acount(callee->result_defaults) ||
            callee->argument_count != acount(callee->argument_kinds) ||
            callee->result_count != acount(callee->result_kinds) ||
            call->count != callee->argument_count ||
            call->result_count != (call->task ? 1 : callee->result_count)) {
            return "callee signature count differs from invocation";
        }
        for (int argument = 0; argument < call->count; argument++) {
            if (f->data_values[call->first + argument].kind !=
                    callee->argument_kinds[argument] ||
                !same_type(
                    f->data_values[call->first + argument].initial,
                    callee->argument_defaults[argument]
                )) {
                return "callee argument type differs from invocation";
            }
        }
        if (call->task && f->data_values[call->result_first].initial.type != PIN_TASK) {
            return "task invocation requires one task handle";
        }
        for (int result = 0; !call->task && result < call->result_count; result++) {
            if (f->data_values[call->result_first + result].kind !=
                    callee->result_kinds[result] ||
                !same_type(
                    f->data_values[call->result_first + result].initial,
                    callee->result_defaults[result]
                )) {
                return "callee result type differs from invocation";
            }
        }
    }
    return NULL;
}
/* Every cycle must cross a runtime step check. Remove check vertices and prove
 * the remaining control graph acyclic with a linear topological traversal.
 * Fixed-step entry proofs cannot bound any cycle, even one containing checks. */
int behavior_function_successors(const behavior_function *f, int pc, int targets[2])
{
    behavior_function_op op = f->instructions[pc].op;
    int count = 0;
    if (op == BF_JUMP || op == BF_BRANCH) {
        targets[count++] = f->instructions[pc].target;
    }
    if (pc + 1 < acount(f->instructions) && op != BF_JUMP && op != BF_END &&
        op != BF_RETURN && op != BF_FAIL) {
        targets[count++] = pc + 1;
    }
    return count;
}
static bool checked_control(const behavior_function *f)
{
    int count = acount(f->instructions), vertices = 0, head = 0, tail = 0;
    int *incoming = xcalloc((size_t)count, sizeof *incoming);
    int *queue = xmalloc((size_t)count * sizeof *queue);
    for (int pc = 0; pc < count; pc++) {
        if (!f->root_steps && f->instructions[pc].op == BF_CHECK) {
            continue;
        }
        vertices++;
        int targets[2], n = behavior_function_successors(f, pc, targets);
        for (int edge = 0; edge < n; edge++) {
            int target = targets[edge];
            if (f->root_steps || f->instructions[target].op != BF_CHECK) {
                incoming[target]++;
            }
        }
    }
    for (int pc = 0; pc < count; pc++) {
        if (!incoming[pc] && (f->root_steps || f->instructions[pc].op != BF_CHECK)) {
            queue[tail++] = pc;
        }
    }
    while (head < tail) {
        int targets[2], n = behavior_function_successors(f, queue[head++], targets);
        for (int edge = 0; edge < n; edge++) {
            int target = targets[edge];
            if ((!f->root_steps && f->instructions[target].op == BF_CHECK)) {
                continue;
            }
            if (!--incoming[target]) {
                queue[tail++] = target;
            }
        }
    }
    free(incoming);
    free(queue);
    return tail == vertices;
}
static bool undefined_slot(const uint64_t *bits, int slot)
{
    return (bits[slot / 64] & (UINT64_C(1) << (slot % 64))) != 0;
}
static void define_slot(uint64_t *bits, int slot)
{
    bits[slot / 64] &= ~(UINT64_C(1) << (slot % 64));
}
/* Union possibly undefined slots over reachable predecessors. Only block
 * entries need retained state; instructions within a block are checked in
 * order. Locals have declared defaults outside this scalar temporary bank. */
static bool defined_operands(const behavior_function *f, char *error, size_t size)
{
    int count = acount(f->instructions), slots = acount(f->slots);
    size_t words = (size_t)slots / 64 + 1;
    const size_t budget = 64u * 1024u * 1024u;
    /* Bound the instruction-to-block map and worst-case block starts before
     * allocating them, then include both in the complete working-storage cap. */
    if ((size_t)count > (budget / sizeof(int) - 1) / 2) {
        snprintf(error, size, "definition exceeds verifier storage budget");
        return false;
    }
    int *block_at = xcalloc((size_t)count, sizeof *block_at);
    int *starts = xmalloc(((size_t)count + 1) * sizeof *starts);
    block_at[0] = 1;
    for (int pc = 0; pc < count; pc++) {
        behavior_function_op op = f->instructions[pc].op;
        if (op != BF_BRANCH && op != BF_JUMP && op != BF_END &&
            op != BF_RETURN && op != BF_FAIL) {
            continue;
        }
        int targets[2], edges = behavior_function_successors(f, pc, targets);
        for (int edge = 0; edge < edges; edge++) {
            block_at[targets[edge]] = 1;
        }
        if (pc + 1 < count) {
            block_at[pc + 1] = 1;
        }
    }
    int blocks = 0;
    for (int pc = 0; pc < count; pc++) {
        if (block_at[pc]) {
            starts[blocks++] = pc;
        }
        block_at[pc] = blocks - 1;
    }
    starts[blocks] = count;
    size_t scratch = ((size_t)count * 2 + 1) * sizeof(int) +
                     (size_t)blocks * (sizeof(int) + 2 * sizeof(bool)) +
                     words * sizeof(uint64_t);
    if (scratch > budget ||
        (size_t)blocks > (budget - scratch) / sizeof(uint64_t) / words) {
        snprintf(error, size, "definition exceeds verifier storage budget");
        free(block_at);
        free(starts);
        return false;
    }
    uint64_t *before = xcalloc((size_t)blocks * words, sizeof *before);
    uint64_t *next = xmalloc(words * sizeof *next);
    bool *reached = xcalloc((size_t)blocks, sizeof *reached);
    bool *queued = xcalloc((size_t)blocks, sizeof *queued);
    int *queue = xmalloc((size_t)blocks * sizeof *queue);
    for (int slot = 0; slot < slots; slot++) {
        before[slot / 64] |= UINT64_C(1) << (slot % 64);
    }
    reached[0] = queued[0] = true;
    queue[0] = 0;
    int head = 0, tail = 1 % blocks, pending = 1;
    bool valid = true;
    while (pending) {
        int block = queue[head];
        head = (head + 1) % blocks;
        pending--;
        queued[block] = false;
        memcpy(next, before + (size_t)block * words, words * sizeof *next);
        for (int pc = starts[block]; pc < starts[block + 1]; pc++) {
            const behavior_function_instruction *i = &f->instructions[pc];
            if ((behavior_function_left_slot(i->op) && undefined_slot(next, i->left)) ||
                (behavior_function_right_slot(i->op) && undefined_slot(next, i->right)) ||
                (behavior_function_third_slot(i->op) &&
                 undefined_slot(next, i->target))) {
                snprintf(error, size, "instruction %d: scalar operand is not defined on every incoming path", pc);
                valid = false;
                break;
            }
            if (i->op < BF_STORE) {
                define_slot(next, i->output);
            }
            if ((i->op == BF_DIV || i->op == BF_MOD) &&
                behavior_scalar_integer(i->type) && i->target >= 0) {
                define_slot(next, i->target);
            }
        }
        if (!valid) {
            break;
        }
        int targets[2],
            edges = behavior_function_successors(f, starts[block + 1] - 1, targets);
        for (int edge = 0; edge < edges; edge++) {
            int target = block_at[targets[edge]];
            uint64_t *to = before + (size_t)target * words;
            bool changed = !reached[target];
            for (size_t word = 0; word < words; word++) {
                uint64_t value = to[word] | next[word];
                changed |= value != to[word];
                to[word] = value;
            }
            reached[target] = true;
            if (changed && !queued[target]) {
                queue[tail] = target;
                tail = (tail + 1) % blocks;
                queued[target] = true;
                pending++;
            }
        }
    }
    free(block_at);
    free(starts);
    free(before);
    free(next);
    free(reached);
    free(queued);
    free(queue);
    return valid;
}
behavior_function *behavior_function_builder_finish(behavior_function_builder *b, char *error, size_t size)
{
    behavior_function *f = b->function;
    assert(f);
    if (!acount(f->instructions) ||
        f->instructions[acount(f->instructions) - 1].op != BF_END) {
        snprintf(error, size, "definition requires an END instruction");
        return NULL;
    }
    for (int slot = 0; slot < acount(f->slots); slot++) {
        if (!scalar_type(f->slots[slot])) {
            snprintf(error, size, "slot %d: invalid scalar type %d", slot, f->slots[slot]);
            return NULL;
        }
    }
    size_t record_bytes;
    size_t record_work = behavior_function_record_check_budget(f, &record_bytes);
    if (record_work == SIZE_MAX || (b->decoded && record_bytes > b->arena_budget)) {
        snprintf(error, size, "record validation scratch exceeds preparation budget");
        return NULL;
    }
    record_validation records = { 0 };
    records.checks = behavior_function_record_checks(f, record_bytes, &records.count);
    const char *metadata_error = verify_metadata(&records, f);
    if (metadata_error) {
        snprintf(error, size, "%s", metadata_error);
        for (int i = 0; i < acount(f->source_watches); i++) {
            const behavior_source_watch *watch = &f->source_watches[i];
            const char *reason = source_watch_error(&records, watch);
            if (!reason) {
                continue;
            }
            snprintf(
                error,
                size,
                "source watch %d invalid %s (kind %d, range %d..%d, ordinal "
                "%d, header %d/%d '%.64s')",
                i,
                reason,
                watch->origin.kind,
                watch->origin.start,
                watch->origin.end,
                watch->ordinal,
                watch->initial.type,
                watch->initial.container,
                watch->initial.type_name ? watch->initial.type_name : ""
            );
            break;
        }
        free(records.checks);
        return NULL;
    }
    free(records.checks);
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        const char *reason = verify_instruction(f, pc);
        if (reason) {
            const behavior_function_instruction *instruction = &f->instructions[pc];
            if (instruction->op == BF_SOURCE_WATCH &&
                index_valid(instruction->target, acount(f->source_watches))) {
                const behavior_source_watch
                    *watch = &f->source_watches[instruction->target];
                int actual = index_valid(instruction->left, acount(f->slots))
                               ? (int)f->slots[instruction->left]
                               : -1;
                snprintf(
                    error,
                    size,
                    "instruction %d: %s (type %d, slot %d:%d, header %d/%d, "
                    "reference %d)",
                    pc,
                    reason,
                    instruction->type,
                    instruction->left,
                    actual,
                    watch->initial.type,
                    watch->initial.container,
                    watch->reference
                );
            } else if (
                instruction->op == BF_WRITE || instruction->op == BF_CAST ||
                instruction->op == BF_CONVERT
            ) {
                int left = index_valid(instruction->left, acount(f->slots))
                             ? (int)f->slots[instruction->left]
                             : -1;
                int right = index_valid(instruction->right, acount(f->slots))
                              ? (int)f->slots[instruction->right]
                              : -1;
                snprintf(
                    error,
                    size,
                    "instruction %d: %s (type %d, left %d:%d, right %d:%d, "
                    "offset %zu)",
                    pc,
                    reason,
                    (int)instruction->type,
                    instruction->left,
                    left,
                    instruction->right,
                    right,
                    instruction->offset
                );
            } else {
                snprintf(error, size, "instruction %d: %s", pc, reason);
            }
            return NULL;
        }
    }
    int failed = -1;
    const char *provenance_error = verify_provenance(f, &failed);
    if (provenance_error) {
        if (failed >= 0) {
            const behavior_function_instruction *i = &f->instructions[failed];
            int output = i->output >= 0 && i->output < acount(f->slots)
                           ? (int)f->slot_kinds[i->output]
                           : -1;
            int left = i->left >= 0 && i->left < acount(f->slots)
                         ? (int)f->slot_kinds[i->left]
                         : -1;
            int right = i->right >= 0 && i->right < acount(f->slots)
                          ? (int)f->slot_kinds[i->right]
                          : -1;
            snprintf(
                error,
                size,
                "instruction %d op=%d type=%d out=%d:%d left=%d:%d right=%d:%d "
                "target=%d: %s",
                failed,
                i->op,
                i->type,
                i->output,
                output,
                i->left,
                left,
                i->right,
                right,
                i->target,
                provenance_error
            );
        } else {
            snprintf(error, size, "%s", provenance_error);
        }
        return NULL;
    }
    for (int local = 0; local < acount(f->locals); local++) {
        if (!scalar_type(f->locals[local].initial.type) ||
            f->locals[local].initial.container != PIN_CONTAINER_SINGLE) {
            snprintf(error, size, "local %d: requires a scalar value", local);
            return NULL;
        }
    }
    if (!defined_operands(f, error, size)) {
        return NULL;
    }
    if (!checked_control(f)) {
        snprintf(error, size, "control cycle requires a runtime budget check");
        return NULL;
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        behavior_function_op op = f->instructions[pc].op;
        f->waits |= op == BF_DELAY;
        if ((op == BF_SUSPEND || op == BF_DELAY || op == BF_POINT ||
             op == BF_SOURCE_POINT || op == BF_INVOKE) &&
            f->root_steps) {
            snprintf(error, size, "instruction %d: resumable definition cannot use fixed steps", pc);
            return NULL;
        }
    }
    for (int pc = 0; pc < acount(f->instructions); pc++) {
        f->instructions[pc].stack = -1;
    }
    if (!b->decoded) {
        behavior_function_optimize(f);
    }
    if (!behavior_function_finalize(f, b->decoded ? b->arena_budget : SIZE_MAX)) {
        snprintf(error, size, "suspension live sets exceed preparation budget");
        return NULL;
    }
    behavior_function_parallel_proof(f);
    if (b->decoded) {
        if (!behavior_function_own_decoded(f, b->arena_budget)) {
            snprintf(error, size, "prepared descriptor ownership exceeds budget");
            return NULL;
        }
    } else {
        if (!behavior_function_own(f)) {
            snprintf(error, size, "function value ownership is invalid");
            return NULL;
        }
    }
    b->function = NULL;
    if (size) {
        error[0] = 0;
    }
    return f;
}
bool behavior_function_prepare_definition(behavior_function *f, size_t arena_budget, char *error, size_t size)
{
    assert(f && !f->frozen);
    behavior_function_builder b = { .function = f,
                                    .decoded = true,
                                    .arena_budget = arena_budget };
    return behavior_function_builder_finish(&b, error, size) == f;
}

const pin_value *behavior_function_arguments(const behavior_function *f, int *count)
{
    *count = f->argument_count;
    return f->argument_defaults;
}
const pin_value *behavior_function_results(const behavior_function *f, int *count)
{
    *count = f->result_count;
    return f->result_defaults;
}
