#include "execution/delay.h"
#include "execution/intrinsics.h"
#include "execution/prepared_internal.h"
#include "execution/task.h"
#include "support/array.h"
#include "types/catalog.h"
#include "types/record.h"

#include <limits.h>
#include <string.h>

#define INT(v) execution_wire_int(w, &(v))
#define U64(v) execution_wire_u64(w, &(v))
#define SIZE(v) execution_wire_size(w, &(v))
#define BOOL(v) execution_wire_bool(w, &(v))
#define STRING(v) execution_wire_string(w, &(v))
#define VALUE(v) prepared_value(c, &(v))
#define ENUM(v)                                                                \
    do {                                                                       \
        int n = (v);                                                           \
        INT(n);                                                                \
        if (w->reading) (v) = n;                                               \
    } while (0)
#define UNSIGNED(v)                                                            \
    do {                                                                       \
        uint64_t n = (v);                                                      \
        U64(n);                                                                \
        if (n > UINT_MAX) w->error = "unsigned value exceeds domain";          \
        if (w->reading) (v) = (unsigned)n;                                     \
    } while (0)
#define ARRAY(bank, code)                                                      \
    do {                                                                       \
        int count = acount(f->bank);                                           \
        prepared_table(w, &count, sizeof *f->bank);                            \
        if (w->reading && !w->error) {                                         \
            prepared_vector(w, (void **)&f->bank, count, sizeof *f->bank);     \
        }                                                                      \
        for (int index = 0; index < count && !w->error; index++) {             \
            code;                                                              \
        }                                                                      \
    } while (0)

static uint64_t instruction_fields(const behavior_function_instruction *v)
{
    /* Absent means the value encodes as the default's five zero bytes, the
     * rule the Chibi writer applies to the encoded value. */
    bool value = v->value.type != PIN_FLOW ||
                 v->value.container != PIN_CONTAINER_SINGLE || v->value.type_name;
    return (v->right ? INSTRUCTION_RIGHT : 0) |
           (v->target ? INSTRUCTION_TARGET : 0) |
           (v->offset ? INSTRUCTION_OFFSET : 0) |
           (value ? INSTRUCTION_VALUE : 0) | (v->frame ? INSTRUCTION_FRAME : 0) |
           (v->stack != -1 ? INSTRUCTION_STACK : 0) |
           (v->pin ? INSTRUCTION_PIN : 0) |
           (v->indirect ? INSTRUCTION_INDIRECT : 0) |
           (v->component ? INSTRUCTION_COMPONENT : 0) |
           (v->node_id ? INSTRUCTION_NODE : 0) |
           (v->layout ? INSTRUCTION_LAYOUT : 0) |
           (v->message ? INSTRUCTION_MESSAGE : 0);
}
void prepared_source(execution_wire *w, behavior_function_source *v)
{
    U64(v->graph_id);
    U64(v->call_id);
    INT(v->parent);
    STRING(v->asset_name);
    INT(v->start);
    INT(v->end);
    INT(v->line);
    INT(v->column);
}
void prepared_function_ref(prepared_context *c, const behavior_function **function)
{
    execution_wire *w = &c->wire;
    int own = -1, imported = -1;
    uint64_t function_id = 0;
    int role = 0;
    if (!w->reading && *function) {
        for (int i = 0; i < acount(c->functions); i++) {
            if (c->functions[i] == *function) {
                own = i;
            }
        }
        /* Imported bodies use stable module/function IDs rather than source addresses. */
        for (int i = 0; i < c->plan->import_count && own < 0; i++) {
            const execution_module_plan *p = &c->plan->imports[i]->plan;
            for (int j = 0; j < p->function_count; j++) {
                const execution_module_function *entry = &p->functions[j];
                bool matched = entry->body == *function;
                if (entry->subscription == *function) {
                    matched = true;
                    role = 1;
                }
                if (matched) {
                    imported = i;
                    function_id = entry->id;
                    own = 0;
                    break;
                }
            }
        }
        if (own < 0) {
            w->error = "callee has no prepared module identity";
            return;
        }
    }
    INT(imported);
    INT(own);
    U64(function_id);
    INT(role);
    if (!w->reading || w->error) {
        return;
    }
    if (imported == -1 && own == -1) {
        *function = NULL;
        return;
    }
    if (imported == -1 && own >= 0 && own < acount(c->functions)) {
        *function = c->functions[own];
        return;
    }
    if (imported >= 0 && imported < c->plan->import_count) {
        const execution_module_plan *p = &c->plan->imports[imported]->plan;
        for (int i = 0; i < p->function_count; i++) {
            if (function_id && p->functions[i].id == function_id) {
                if (role == 0) {
                    *function = p->functions[i].body;
                    return;
                }
                if (role == 1) {
                    *function = p->functions[i].subscription;
                    return;
                }
            }
        }
    }
    w->error = "invalid prepared function reference";
}
static void host(prepared_context *c, behavior_function_host_call *v)
{
    execution_wire *w = &c->wire;
    U64(v->capability_id);
    U64(v->capability_abi);
    INT(v->first);
    INT(v->result_first);
    INT(v->result_count);
    BOOL(v->owned_arguments);
    prepared_table(w, &v->outcome_count, sizeof *v->outcomes);
    if (w->reading && !w->error && v->outcome_count) {
        v->outcomes = execution_wire_allocate(
            w,
            (size_t)v->outcome_count * sizeof *v->outcomes
        );
    }
    for (int i = 0; i < v->outcome_count && !w->error; i++) {
        uint64_t id = w->reading ? 0 : v->outcomes[i];
        U64(id);
        if (w->reading) {
            ((uint64_t *)v->outcomes)[i] = id;
        }
    }
    if (!v->capability_id || !v->capability_abi) {
        w->error = "host capability lacks persistent identity";
        return;
    }
    if (!w->reading || w->error) {
        return;
    }
    v->call = execution_intrinsic_resolve(v->capability_id);
    if (v->call) {
        if (v->capability_abi != execution_intrinsic_abi(v->capability_id)) {
            w->error = "intrinsic ABI mismatch";
            return;
        }
        v->access = execution_intrinsic_access(v->capability_id);
        return;
    }
    execution_prepared_host linked = { 0 };
    if (!c->linker || !c->linker->host ||
        !c->linker->host(c->linker->user, v->capability_id, &linked) ||
        !linked.call || linked.abi != v->capability_abi) {
        w->error = "missing or incompatible host capability";
        return;
    }
    v->call = linked.call;
    v->access = linked.access;
}
static void query(prepared_context *c, behavior_function_query *v)
{
    execution_wire *w = &c->wire;
    U64(v->capability_id);
    U64(v->capability_abi);
    STRING(v->service);
    STRING(v->key);
    if (!v->capability_id || !v->capability_abi) {
        w->error = "query lacks persistent identity";
        return;
    }
    if (!w->reading || w->error) {
        return;
    }
    execution_prepared_query linked = { 0 };
    if (!c->linker || !c->linker->query ||
        !c->linker->query(c->linker->user, v->capability_id, &linked) ||
        !linked.read || linked.abi != v->capability_abi || !linked.service ||
        !v->service || strcmp(linked.service, v->service)) {
        w->error = "missing or incompatible query capability";
        return;
    }
    v->read = linked.read;
}
static void command(prepared_context *c, behavior_function_command *v)
{
    execution_wire *w = &c->wire;
    U64(v->capability_id);
    U64(v->capability_abi);
    U64(v->event_id);
    U64(v->event_abi);
    if (!!v->event_id != !!v->event_abi) {
        w->error = "event requirement lacks identity or ABI";
        return;
    }
    if (w->reading && v->event_id &&
        (!c->linker || !c->linker->event ||
         !c->linker->event(c->linker->user, v->event_id, v->event_abi))) {
        w->error = "missing or incompatible event declaration";
        return;
    }
    STRING(v->reference);
    INT(v->first);
    INT(v->count);
    U64(v->node_id);
    INT(v->frame);
    INT(v->result_first);
    INT(v->result_count);
    bool map = v->result_pins != NULL;
    BOOL(map);
    if (map) {
        int count = v->result_count;
        prepared_table(w, &count, sizeof(int));
        int *pins = (int *)v->result_pins;
        if (w->reading && !w->error) {
            pins = execution_wire_allocate(w, (size_t)count * sizeof *pins);
        }
        for (int i = 0; i < count && !w->error; i++) {
            INT(pins[i]);
        }
        if (w->reading) {
            v->result_pins = pins;
        }
        if (count != v->result_count) {
            w->error = "completion map count differs";
        }
    }
    if (!v->capability_id || !v->capability_abi) {
        w->error = "command lacks persistent identity";
        return;
    }
    if (!w->reading || w->error) {
        return;
    }
    if (v->capability_id == EXECUTION_DELAY_ID &&
        v->capability_abi == EXECUTION_DELAY_ABI) {
        v->command = &EXECUTION_Delay;
        return;
    }
    v->command = execution_task_command(v->capability_id, v->capability_abi);
    if (v->command) {
        return;
    }
    execution_prepared_command linked = { 0 };
    if (!c->linker || !c->linker->command ||
        !c->linker->command(c->linker->user, v->capability_id, v->reference, &linked) ||
        !linked.command || linked.abi != v->capability_abi) {
        w->error = "missing or incompatible command capability";
        return;
    }
    v->command = linked.command;
}
static bool portable_kind(execution_wire *w, behavior_value_kind kind, const pin_value *value)
{
    if (kind < BEHAVIOR_VALUE || kind > BEHAVIOR_QUERY_CURSOR) {
        w->error = "invalid executable value provenance";
        return false;
    }
    if (kind != BEHAVIOR_VALUE && value &&
        (value->type != BF_POINTER_TYPE ||
         value->container != PIN_CONTAINER_SINGLE || value->u64)) {
        w->error = "nonportable executable address initializer";
        return false;
    }
    return true;
}
static void provenance(execution_wire *w, const behavior_function *f)
{
    if (w->error) {
        return;
    }
    if (acount(f->slot_kinds) != acount(f->slots) ||
        acount(f->argument_kinds) != acount(f->argument_defaults) ||
        acount(f->result_kinds) != acount(f->result_defaults) ||
        acount(f->command_argument_kinds) != acount(f->command_arguments) ||
        acount(f->command_result_kinds) != acount(f->command_results)) {
        w->error = "executable provenance bank counts differ";
        return;
    }
    for (int i = 0; i < acount(f->slots) && !w->error; i++) {
        portable_kind(w, f->slot_kinds[i], NULL);
    }
#define BANK(kinds, values)                                                    \
    for (int i = 0; i < acount(f->values) && !w->error; i++) {                 \
        portable_kind(w, f->kinds[i], &f->values[i]);                          \
    }
    BANK(argument_kinds, argument_defaults);
    BANK(result_kinds, result_defaults);
    BANK(command_argument_kinds, command_arguments);
    BANK(command_result_kinds, command_results);
#undef BANK
    for (int i = 0; i < acount(f->locals) && !w->error; i++) {
        portable_kind(w, f->locals[i].kind, &f->locals[i].initial);
    }
    for (int i = 0; i < acount(f->data_values) && !w->error; i++) {
        portable_kind(w, f->data_values[i].kind, &f->data_values[i].initial);
    }
    for (int i = 0; i < acount(f->instructions) && !w->error; i++) {
        const behavior_function_instruction *v = &f->instructions[i];
        if (v->op == BF_CONSTANT && v->output >= 0 &&
            v->output < acount(f->slot_kinds)) {
            portable_kind(w, f->slot_kinds[v->output], &v->value);
        }
    }
}
void prepared_function(prepared_context *c, behavior_function *f)
{
    execution_wire *w = &c->wire;
    INT(f->argument_count);
    INT(f->result_count);
    INT(f->variable_count);
    UNSIGNED(f->root_steps);
    UNSIGNED(f->child_budget);
    UNSIGNED(f->depth_limit);
    UNSIGNED(f->pure_depth_limit);
    BOOL(f->breakpoints);
    BOOL(f->batchable);
    BOOL(f->managed);
    BOOL(f->custom_lowering);
    BOOL(f->checkpoints);
    BOOL(f->pin_watches);
    SIZE(f->watch_size);
    BOOL(f->component_bindings);
    BOOL(f->component_access);
    ARRAY(argument_defaults, VALUE(f->argument_defaults[index]));
    ARRAY(argument_kinds, ENUM(f->argument_kinds[index]));
    ARRAY(result_defaults, VALUE(f->result_defaults[index]));
    ARRAY(result_kinds, ENUM(f->result_kinds[index]));
    ARRAY(variable_defaults, VALUE(f->variable_defaults[index]));
    ARRAY(slots, ENUM(f->slots[index]));
    ARRAY(slot_kinds, ENUM(f->slot_kinds[index]));
    ARRAY(value_roots, {
        behavior_value_root *v = &f->value_roots[index];
        ENUM(v->bank);
        INT(v->index);
        VALUE(v->shape);
    });
    ARRAY(instructions, {
        behavior_function_instruction *v = &f->instructions[index];
        ENUM(v->op);
        ENUM(v->type);
        INT(v->output);
        INT(v->left);
        if (c->version < 28) {
            INT(v->right);
            INT(v->target);
            INT(v->frame);
            INT(v->source);
            INT(v->stack);
            INT(v->pin);
            SIZE(v->offset);
            BOOL(v->indirect);
            INT(v->component);
            U64(v->node_id);
            VALUE(v->value);
            U64(v->layout);
            STRING(v->message);
            continue;
        }
        INT(v->source);
        uint64_t fields = w->reading ? 0 : instruction_fields(v);
        U64(fields);
        if (fields & ~(uint64_t)INSTRUCTION_FIELDS) {
            w->error = "unknown prepared instruction fields";
            break;
        }
        if (w->reading) {
            v->right = v->target = v->frame = v->pin = v->component = 0;
            v->offset = 0;
            v->value = (pin_value){ 0 };
            v->stack = -1;
            v->indirect = false;
            v->node_id = v->layout = 0;
            v->message = NULL;
        }
        if (fields & INSTRUCTION_RIGHT) {
            INT(v->right);
        }
        if (fields & INSTRUCTION_TARGET) {
            INT(v->target);
        }
        if (fields & INSTRUCTION_OFFSET) {
            SIZE(v->offset);
        }
        if (fields & INSTRUCTION_VALUE) {
            VALUE(v->value);
        }
        if (fields & INSTRUCTION_FRAME) {
            INT(v->frame);
        }
        if (fields & INSTRUCTION_STACK) {
            INT(v->stack);
        }
        if (fields & INSTRUCTION_PIN) {
            INT(v->pin);
        }
        if (fields & INSTRUCTION_INDIRECT) {
            BOOL(v->indirect);
        }
        if (fields & INSTRUCTION_COMPONENT) {
            INT(v->component);
        }
        if (fields & INSTRUCTION_NODE) {
            U64(v->node_id);
        }
        if (fields & INSTRUCTION_LAYOUT) {
            U64(v->layout);
        }
        if (fields & INSTRUCTION_MESSAGE) {
            STRING(v->message);
        }
    });
    ARRAY(locals, {
        behavior_function_local *v = &f->locals[index];
        ENUM(v->kind);
        VALUE(v->initial);
        INT(v->frame);
        U64(v->node_id);
        INT(v->pin);
        SIZE(v->offset);
        BOOL(v->parameter);
        BOOL(v->array_default);
        INT(v->data_index);
    });
    ARRAY(parameters, {
        behavior_function_parameter *v = &f->parameters[index];
        VALUE(v->initial);
        INT(v->frame);
        INT(v->pin);
    });
    ARRAY(frames, {
        behavior_function_frame *v = &f->frames[index];
        prepared_source(w, &v->source);
        INT(v->depth);
        INT(v->resume);
        INT(v->source_index);
        INT(v->stack);
    });
    ARRAY(sources, prepared_source(w, &f->sources[index]));
    ARRAY(stacks, prepared_source(w, &f->stacks[index]));
    ARRAY(results, {
        behavior_function_result *v = &f->results[index];
        INT(v->index);
        const char *name = v->type ? v->type->name : NULL;
        STRING(name);
        if (w->error) {
            break;
        }
        if (v->index < 0 || v->index >= acount(f->result_defaults)) {
            w->error = "prepared result index exceeds declared results";
            break;
        }
        const pin_value *header = &f->result_defaults[v->index];
        const type_desc *type = pin_value_type(header);
        if (header->type != PIN_STRUCT ||
            header->container != PIN_CONTAINER_SINGLE || !name || !type ||
            strcmp(name, type->name) || (!w->reading && v->type != type)) {
            w->error = "prepared result layout differs from declared header";
            break;
        }
        if (w->reading) {
            v->type = type;
        }
    });
    ARRAY(data_values, {
        ENUM(f->data_values[index].kind);
        VALUE(f->data_values[index].initial);
        SIZE(f->data_values[index].size);
    });
    ARRAY(host_calls, host(c, &f->host_calls[index]));
    ARRAY(queries, query(c, &f->queries[index]));
    ARRAY(commands, command(c, &f->commands[index]));
    ARRAY(command_arguments, VALUE(f->command_arguments[index]));
    ARRAY(command_argument_kinds, ENUM(f->command_argument_kinds[index]));
    ARRAY(command_results, VALUE(f->command_results[index]));
    ARRAY(command_result_kinds, ENUM(f->command_result_kinds[index]));
    ARRAY(calls, {
        behavior_function_invocation *v = &f->calls[index];
        prepared_function_ref(c, &v->function);
        INT(v->first);
        INT(v->count);
        INT(v->result_first);
        INT(v->result_count);
        BOOL(v->task);
        BOOL(v->owned_arguments);
    });
    ARRAY(definitions, {
        const behavior_function *v = f->definitions[index];
        prepared_function_ref(c, &v);
        if (w->reading) {
            f->definitions[index] = (behavior_function *)v;
        }
    });
    ARRAY(components, {
        U64(f->components[index].id);
        BOOL(f->components[index].binding);
    });
    ARRAY(component_writes, U64(f->component_writes[index]));
    ARRAY(state_selections, {
        INT(f->state_selections[index].index);
        INT(f->state_selections[index].outputs);
    });
    ARRAY(source_points, {
        behavior_source_point *v = &f->source_points[index];
        U64(v->occurrence);
        U64(v->meaning);
        INT(v->start);
        INT(v->end);
        INT(v->line);
        INT(v->column);
        INT(v->kind);
        BOOL(v->completion);
    });
    ARRAY(source_watches, {
        behavior_source_watch *v = &f->source_watches[index];
        U64(v->origin.occurrence);
        U64(v->origin.meaning);
        INT(v->origin.start);
        INT(v->origin.end);
        INT(v->origin.line);
        INT(v->origin.column);
        INT(v->origin.kind);
        BOOL(v->origin.completion);
        VALUE(v->initial);
        INT(v->ordinal);
        BOOL(v->output);
        BOOL(v->reference);
    });
    ARRAY(points, {
        behavior_function_point *v = &f->points[index];
        U64(v->graph_id);
        U64(v->node_id);
        INT(v->frame);
        INT(v->stack);
        INT(v->depth);
        ENUM(v->kind);
        BOOL(v->breakpoint);
    });
    ARRAY(watches, {
        behavior_function_watch *v = &f->watches[index];
        U64(v->graph_id);
        U64(v->node_id);
        INT(v->frame);
        INT(v->pin);
        INT(v->pending_frame);
        BOOL(v->output);
        VALUE(v->initial);
    });
    ARRAY(edges, {
        behavior_function_edge *v = &f->edges[index];
        U64(v->from_id);
        U64(v->from_pin_id);
        U64(v->to_id);
        U64(v->to_pin_id);
        INT(v->kind);
        INT(v->frame);
    });
    provenance(w, f);
}
