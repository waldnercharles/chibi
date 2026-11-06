#include "execution/function_builder.h"
#include "execution/module_effects.h"
#include "execution/preparation_budget.h"
#include "execution/prepared_internal.h"
#include "execution/storage.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/catalog.h"
#include "types/map.h"
#include "types/record.h"
#include "types/union.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PREPARED_MAGIC UINT64_C(0x4348494249505245)
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
#define ARRAY(bank, countfield, code)                                          \
    do {                                                                       \
        int count = acount(p->bank);                                           \
        prepared_table(w, &count, sizeof *p->bank);                            \
        if (w->reading && !w->error) {                                         \
            prepared_vector(w, (void **)&p->bank, count, sizeof *p->bank);     \
            if (!w->error) p->countfield = count;                              \
        }                                                                      \
        for (int index = 0; index < count && !w->error; index++) {             \
            code;                                                              \
        }                                                                      \
    } while (0)

void prepared_table(execution_wire *w, int *count, size_t item_size)
{
    execution_wire_count(w, count, 1);
    if (!w->reading || w->error) {
        return;
    }
    if (item_size && (size_t)*count > SIZE_MAX / item_size) {
        w->error = "decoded definition storage exceeds budget";
        return;
    }
    execution_wire_reserve(w, (size_t)*count * item_size);
}
void prepared_vector(execution_wire *w, void **values, int count, size_t item_size)
{
    assert(w->reading && !*values && count >= 0);
    if (w->error || !count) {
        return;
    }
    int capacity = count < 16 ? 16 : count;
    size_t extra = sizeof(dyna_hdr) + (size_t)(capacity - count) * item_size;
    if (!execution_wire_reserve(w, extra)) {
        return;
    }
    dyna_hdr *header = xcalloc(1, sizeof *header + (size_t)capacity * item_size);
    header->len = count;
    header->cap = capacity;
    header->cookie = DYNA_COOKIE;
    *values = header + 1;
}
typedef struct prepared_dependency
{
    const execution_module *module;
    uint64_t fingerprint;
    bool visiting;
} prepared_dependency;
static bool write_depth(
    const execution_module *,
    unsigned char **,
    size_t *,
    const char **,
    unsigned,
    prepared_dependency **,
    size_t *,
    uint64_t *,
    execution_wire *,
    size_t *,
    size_t
);
static bool scratch_growth(execution_wire *budget, const void *values, size_t item, size_t *charged)
{
    int count = alen(values), capacity = acap(values);
    if (!budget || count < capacity) {
        return true;
    }
    int next = capacity ? capacity * 2 : 16;
    size_t bytes = (size_t)(next - capacity) * item +
                   (capacity ? 0 : sizeof(dyna_hdr));
    if (!execution_wire_reserve(budget, bytes)) {
        return false;
    }
    if (charged) {
        *charged += bytes;
    }
    return true;
}
static bool dependency_fingerprint(
    prepared_context *c,
    const execution_module *module,
    unsigned depth,
    uint64_t *out
)
{
    prepared_dependency **cache = c->dependencies;
    for (int i = 0; i < acount(*cache); i++) {
        if ((*cache)[i].module != module) {
            continue;
        }
        if ((*cache)[i].visiting) {
            c->wire.error = "cyclic prepared imports";
            return false;
        }
        *out = (*cache)[i].fingerprint;
        return true;
    }
    if (acount(*cache) >= EXECUTION_WIRE_MAX_RECORDS) {
        c->wire.error = "too many imported modules";
        return false;
    }
    if (!scratch_growth(c->fingerprint_budget, *cache, sizeof **cache, NULL)) {
        c->wire.error = c->fingerprint_budget->error;
        return false;
    }
    int at = acount(*cache);
    apush(*cache, ((prepared_dependency){ .module = module, .visiting = true }));
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t length = 0;
    const char *error = NULL;
    if (!write_depth(
            module,
            NULL,
            &length,
            &error,
            depth,
            cache,
            c->dependency_bytes,
            &hash,
            c->fingerprint_budget,
            c->shared_record_work,
            c->record_work_limit
        )) {
        c->wire.error = error;
        return false;
    }
    if (length > EXECUTION_WIRE_MAX_BYTES - *c->dependency_bytes) {
        c->wire.error = "import serialization exceeds aggregate byte budget";
        return false;
    }
    *c->dependency_bytes += length;
    *out = hash ? hash : 1;
    (*cache)[at].fingerprint = *out;
    (*cache)[at].visiting = false;
    return true;
}
static bool add_function(prepared_context *c, behavior_function *f, unsigned depth)
{
    if (!f) {
        return true;
    }
    if (depth > EXECUTION_WIRE_MAX_DEPTH) {
        c->wire.error = "definition ownership depth exceeds limit";
        return false;
    }
    for (int i = 0; i < acount(c->functions); i++) {
        if (c->functions[i] == f) {
            c->wire.error = "definition has multiple owning roots or a cycle";
            return false;
        }
    }
    if (acount(c->functions) >= EXECUTION_WIRE_MAX_RECORDS) {
        c->wire.error = "too many definitions";
        return false;
    }
    if (!scratch_growth(c->fingerprint_budget, c->functions, sizeof *c->functions, &c->function_bytes)) {
        c->wire.error = c->fingerprint_budget->error;
        return false;
    }
    apush(c->functions, f);
    for (int i = 0; i < acount(f->definitions); i++) {
        if (!add_function(c, f->definitions[i], depth + 1)) {
            return false;
        }
    }
    return true;
}
static void root_ref(prepared_context *c, behavior_function **f)
{
    const behavior_function *v = *f;
    prepared_function_ref(c, &v);
    if (c->wire.reading) {
        *f = (behavior_function *)v;
    }
}
static void ints(execution_wire *w, int **values, int *count)
{
    prepared_table(w, count, sizeof **values);
    if (w->reading && !w->error) {
        *values = xcalloc((size_t)*count, sizeof **values);
    }
    for (int i = 0; i < *count && !w->error; i++) {
        execution_wire_int(w, &(*values)[i]);
    }
}
static void ids(execution_wire *w, uint64_t **values, int *count)
{
    prepared_table(w, count, sizeof **values);
    if (w->reading && !w->error) {
        *values = xcalloc((size_t)*count, sizeof **values);
    }
    for (int i = 0; i < *count && !w->error; i++) {
        execution_wire_u64(w, &(*values)[i]);
    }
}
static uint64_t context_abi()
{
    /* BF_CONTEXT uses byte offsets. Include every public runtime field so equal
     * struct sizes cannot hide a reordered ABI. Only layout facts are hashed. */
#define FIELD(name)                                                            \
    offsetof(behavior_function_context, name),                                 \
        sizeof(((behavior_function_context *)0)->name)
    const size_t fields[] = { sizeof(float),
                              sizeof(double),
                              FLT_RADIX,
                              FLT_MANT_DIG,
                              DBL_MANT_DIG,
                              sizeof(pin_value),
                              offsetof(pin_value, type),
                              offsetof(pin_value, u64),
                              offsetof(pin_value, container),
                              offsetof(pin_value, type_name),
                              offsetof(pin_value, record),
                              offsetof(pin_value, schema),
                              sizeof(pin_string),
                              offsetof(pin_string, bytes),
                              offsetof(pin_string, length),
                              offsetof(pin_string, owner),
                              PIN_CONTAINER_VIEW,
                              sizeof(node_host_call),
                              offsetof(node_host_call, language_values),
                              offsetof(node_host_call, owned_result),
                              sizeof(pin_record),
                              offsetof(pin_record, type),
                              offsetof(pin_record, native),
                              offsetof(pin_record, frozen),
                              sizeof(type_desc),
                              offsetof(type_desc, alias),
                              offsetof(type_desc, carrier),
                              sizeof(type_field),
                              offsetof(type_field, layout),
                              sizeof(pin_array),
                              offsetof(pin_array, element_type),
                              offsetof(pin_array, type_name),
                              offsetof(pin_array, record),
                              offsetof(pin_array, schema),
                              offsetof(pin_array, count),
                              offsetof(pin_array, capacity),
                              offsetof(pin_array, items),
                              offsetof(pin_array, data),
                              offsetof(pin_array, stride),
                              offsetof(pin_array, buckets),
                              offsetof(pin_array, bucket_mask),
                              offsetof(pin_array, storage),
                              offsetof(pin_array, offset),
                              offsetof(pin_array, pointer),
                              offsetof(pin_array, backing),
                              sizeof(pin_map),
                              offsetof(pin_map, key_type),
                              offsetof(pin_map, value_type),
                              offsetof(pin_map, count),
                              offsetof(pin_map, capacity),
                              offsetof(pin_map, entries),
                              offsetof(pin_map, slots),
                              offsetof(pin_map, slot_mask),
                              offsetof(pin_map, backing),
                              offsetof(pin_map, storage),
                              sizeof(behavior_function_host_call),
                              offsetof(behavior_function_host_call, owned_arguments),
                              sizeof(behavior_function_invocation),
                              offsetof(behavior_function_invocation, owned_arguments),
                              sizeof(pin_map_entry),
                              offsetof(pin_map_entry, key),
                              offsetof(pin_map_entry, value),
                              sizeof(behavior_variable_storage),
                              offsetof(behavior_variable_storage, data),
                              offsetof(behavior_variable_storage, type_name),
                              offsetof(behavior_variable_storage, type),
                              offsetof(behavior_variable_storage, layout),
                              sizeof(behavior_function_context),
                              FIELD(parent),
                              FIELD(call_enter),
                              FIELD(call_leave),
                              FIELD(activation),
                              FIELD(retrigger),
                              FIELD(data),
                              FIELD(now),
                              FIELD(wake_time),
                              FIELD(program_name),
                              FIELD(instance_time),
                              FIELD(inspection_user),
                              FIELD(inspect),
                              FIELD(points),
                              FIELD(variables),
                              FIELD(states),
                              FIELD(arguments),
                              FIELD(argument_count),
                              FIELD(arguments_stabilized),
                              FIELD(argument_source),
                              FIELD(owned_host),
                              FIELD(owned_data),
                              FIELD(collection_owners),
                              FIELD(results),
                              FIELD(returned_node_id),
                              FIELD(returned),
                              FIELD(exit_requested),
                              FIELD(returned_outcome),
                              FIELD(pending_commands),
                              FIELD(action_result),
                              FIELD(complete_action),
                              FIELD(bind_instance),
                              FIELD(commit_instance),
                              FIELD(store_component),
                              FIELD(reduce_component),
                              FIELD(delta_seconds),
                              FIELD(steps),
                              FIELD(budget),
                              FIELD(node_id),
                              FIELD(call_depth),
                              FIELD(error_frame),
                              FIELD(error_source),
                              FIELD(error_stack),
                              FIELD(error_pin),
                              FIELD(error_steps),
                              FIELD(failure),
                              FIELD(error_message),
                              FIELD(services),
                              FIELD(queries),
                              FIELD(commands),
                              FIELD(command_arguments),
                              FIELD(command_results),
                              FIELD(begin_command),
                              FIELD(start_task),
                              FIELD(release_local),
                              FIELD(error_status),
                              FIELD(owner),
                              FIELD(owner_type_name),
                              FIELD(world),
                              FIELD(binding_world),
                              FIELD(columns),
                              FIELD(query_variables),
                              FIELD(query_row),
                              FIELD(query_test),
                              FIELD(module),
                              FIELD(program),
                              FIELD(variable_storage),
                              FIELD(value_owner),
                              FIELD(value_storage),
                              FIELD(managed_heap),
                              FIELD(result_storage),
                              FIELD(copy_string),
                              FIELD(copy_text),
                              FIELD(copy_array),
                              FIELD(store_value),
                              FIELD(add_set_value),
                              FIELD(own_type_name),
                              FIELD(watch),
                              FIELD(trace),
                              FIELD(watches),
                              FIELD(edges),
                              FIELD(watched),
                              FIELD(watch_storage),
                              FIELD(select_state),
                              FIELD(state_inputs),
                              FIELD(state_transition),
                              FIELD(retrigger_action),
                              FIELD(component_membership) };
#undef FIELD
    uint64_t hash = UINT64_C(14695981039346656037);
    for (size_t i = 0; i < sizeof fields / sizeof *fields; i++) {
        uint64_t n = fields[i];
        for (int byte = 0; byte < 8; byte++) {
            hash = (hash ^ (unsigned char)(n >> (byte * 8))) * UINT64_C(1099511628211);
        }
    }
    return hash;
}
uint64_t prepared_abi(const char *const *names, int count)
{
    uint64_t hash = context_abi();
    static const unsigned char domain[] = "chibi.prepared.instructions";
    for (size_t i = 0; i < sizeof domain; i++) {
        hash = (hash ^ domain[i]) * UINT64_C(1099511628211);
    }
    for (int op = 0; op < count; op++) {
        /* Include the NUL boundary, then the ordinal as eight little-endian
         * bytes, independent of enum size, pointers and compiler padding. */
        const unsigned char *name = (const unsigned char *)names[op];
        do {
            hash = (hash ^ *name) * UINT64_C(1099511628211);
        } while (*name++);
        uint64_t ordinal = (uint64_t)op;
        for (int byte = 0; byte < 8; byte++) {
            hash = (hash ^ (unsigned char)(ordinal >> (byte * 8))) *
                   UINT64_C(1099511628211);
        }
    }
    return hash;
}
static const char *const instruction_names[] = {
#define FUNCTION_NAME(op) [BF_##op] = #op,
#define MATH_NAME(op, name, fn) FUNCTION_NAME(op)
    BEHAVIOR_FUNCTION_OPS(FUNCTION_NAME, MATH_NAME)
#undef MATH_NAME
#undef FUNCTION_NAME
};
uint64_t execution_prepared_abi()
{
    return prepared_abi(instruction_names, BF_END + 1);
}
int execution_prepared_instruction_count()
{
    return BF_END + 1;
}
const char *execution_prepared_instruction_name(int ordinal)
{
    assert(ordinal >= 0 && ordinal <= BF_END);
    return instruction_names[ordinal];
}
static void header(execution_wire *w)
{
    uint64_t expected_abi = execution_prepared_abi();
    uint64_t magic = PREPARED_MAGIC, version = EXECUTION_PREPARED_VERSION,
             pointer_size = sizeof(void *), abi = expected_abi;
    U64(magic);
    U64(version);
    U64(pointer_size);
    U64(abi);
    if (magic != PREPARED_MAGIC || version != EXECUTION_PREPARED_VERSION) {
        w->error = "unknown prepared format version; rebuild prepared code";
    }
    if (pointer_size != sizeof(void *) || abi != expected_abi) {
        w->error = "prepared target ABI differs";
    }
}
static void imports(prepared_context *c, unsigned depth)
{
    execution_wire *w = &c->wire;
    execution_module_plan *p = c->plan;
    ARRAY(imports, import_count, {
        uint64_t id = 0;
        uint64_t abi = 0;
        if (!w->reading) {
            id = p->imports[index]->plan.id;
            if (!dependency_fingerprint(c, p->imports[index], depth + 1, &abi)) {
                break;
            }
        }
        U64(id);
        U64(abi);
        if (!id || !abi) {
            w->error = "import lacks stable identity";
            break;
        }
        if (w->reading) {
            if (!c->linker || !c->linker->import ||
                (p->imports[index] = c->linker->import(c->linker->user, id, abi)) == NULL ||
                p->imports[index]->plan.id != id) {
                w->error = "missing or incompatible import";
                break;
            }
            uint64_t actual = 0;
            if (!dependency_fingerprint(c, p->imports[index], depth + 1, &actual)) {
                break;
            }
            if (actual != abi) {
                w->error = "import fingerprint differs";
            }
        }
    });
}
static void events(prepared_context *c)
{
    execution_wire *w = &c->wire;
    execution_module_plan *p = c->plan;
    ARRAY(events, event_count, {
        execution_module_event_declaration *event = &p->events[index];
        U64(event->id);
        U64(event->abi);
        STRING(event->name);
        if (w->reading && !w->error && c->linker && c->linker->event &&
            !c->linker->event(c->linker->user, event->id, event->abi)) {
            w->error = "missing or incompatible event declaration";
            break;
        }
        int count = event->field_count;
        prepared_table(w, &count, sizeof *event->fields);
        if (w->reading && !w->error) {
            event->fields = xcalloc((size_t)count, sizeof *event->fields);
            event->field_count = count;
        }
        for (int i = 0; i < count && !w->error; i++) {
            execution_module_event_field *field = &event->fields[i];
            STRING(field->name);
            U64(field->nominal);
            BOOL(field->has_default);
            BOOL(field->default_owner);
            VALUE(field->initial);
        }
    });
}
static void captures(prepared_context *c, execution_module_capture **values, int *count)
{
    execution_wire *w = &c->wire;
    prepared_table(w, count, sizeof **values);
    if (w->reading && !w->error) {
        *values = xcalloc((size_t)*count, sizeof **values);
    }
    for (int i = 0; i < *count && !w->error; i++) {
        U64((*values)[i].id);
        INT((*values)[i].value);
        VALUE((*values)[i].type);
    }
}
static void payload(prepared_context *c)
{
    execution_wire *w = &c->wire;
    execution_module_plan *p = c->plan;
    ARRAY(states, state_count, {
        U64(p->states[index].id);
        VALUE(p->states[index].initial);
        BOOL(p->states[index].shared);
        BOOL(p->states[index].contains_tasks);
    });
    ARRAY(functions, function_count, {
        execution_module_function *v = &p->functions[index];
        U64(v->id);
        U64(v->event);
        U64(v->event_abi);
        U64(v->observer.component);
        U64(v->observer.layout);
        U64(v->observer.field);
        SIZE(v->observer.offset);
        VALUE(v->observer.type);
        INT(v->observer.argument);
        if (v->event && !v->event_abi) {
            w->error = "event handler lacks declaration ABI";
        }
        U64(v->action);
        U64(v->parent);
        U64(v->outcome);
        captures(c, &v->captures, &v->local_capture_count);
        INT(v->capture_count);
        BOOL(v->continuation_wait);
        ENUM(v->kind);
        const char *entry = v->entry;
        const char *event = v->event_name;
        STRING(entry);
        STRING(event);
        if (w->reading && !w->error) {
            if (entry) {
                size_t bytes = strlen(entry) + 1;
                v->entry = execution_wire_allocate_owned(w, bytes);
                if (v->entry) {
                    memcpy(v->entry, entry, bytes);
                }
            }
            if (event) {
                size_t bytes = strlen(event) + 1;
                v->event_name = execution_wire_allocate_owned(w, bytes);
                if (v->event_name) {
                    memcpy(v->event_name, event, bytes);
                }
            }
        }
        uint64_t effects = v->effects;
        U64(effects);
        if (effects > UINT_MAX) {
            w->error = "invalid effects";
        }
        if (w->reading) {
            v->effects = (unsigned)effects;
        }
        root_ref(c, &v->body);
        root_ref(c, &v->subscription);
    });
    ARRAY(queries, query_count, {
        execution_module_query *v = &p->queries[index];
        ENUM(v->kind);
        BOOL(v->required);
        U64(v->component);
        U64(v->source_relationship);
        INT(v->source_traversal);
        INT(v->source_variable);
        INT(v->target_variable);
        root_ref(c, &v->predicate);
        ints(w, &v->children, &v->child_count);
    });
    INT(p->query);
    INT(p->query_variable_count);
    INT(p->query_filter);
    ARRAY(descriptions, description_count, {
        execution_module_query_description *v = &p->descriptions[index];
        INT(v->query);
        INT(v->query_variable_count);
        VALUE(v->row_type);
        root_ref(c, &v->evaluate);
    });
    INT(p->system_description);
    ARRAY(bindings, binding_count, {
        execution_module_binding *v = &p->bindings[index];
        U64(v->target);
        U64(v->component);
        U64(v->layout);
        SIZE(v->offset);
        VALUE(v->target_type);
        ids(w, &v->reads, &v->read_count);
        ids(w, &v->writes, &v->write_count);
        int count = acount(v->fields);
        prepared_table(w, &count, sizeof *v->fields);
        if (w->reading && !w->error) {
            prepared_vector(w, (void **)&v->fields, count, sizeof *v->fields);

            v->field_count = count;
        }
        for (int j = 0; j < count && !w->error; j++) {
            execution_field_access *field = &v->fields[j];
            U64(field->component);
            SIZE(field->offset);
            SIZE(field->size);
            BOOL(field->any_entity);
        }
        root_ref(c, &v->dependencies);
        INT(v->dependency_count);
        root_ref(c, &v->body);
    });
    ARRAY(actions, action_count, {
        execution_module_action *v = &p->actions[index];
        U64(v->id);
        U64(v->capability);
        U64(v->completion);
        ENUM(v->state_kind);
        BOOL(v->loop);
        prepared_table(w, &v->result_count, sizeof *v->defaults);
        if (w->reading && !w->error) {
            prepared_vector(w, (void **)&v->defaults, v->result_count, sizeof *v->defaults);
        }
        for (int j = 0; j < v->result_count && !w->error; j++) {
            VALUE(v->defaults[j]);
        }
        ids(w, &v->outcomes, &v->outcome_count);
    });
}
/* Ensure each decoded definition has exactly one owner before trusting any
 * destructor or recursive verifier. Calls borrow and do not count as owners. */
static bool reached(prepared_context *c, const behavior_function *f, bool *seen, unsigned depth)
{
    if (!f) {
        return true;
    }
    int at = -1;
    for (int i = 0; i < acount(c->functions); i++) {
        if (c->functions[i] == f) {
            at = i;
        }
    }
    if (at < 0 || seen[at] || depth > EXECUTION_WIRE_MAX_DEPTH) {
        return false;
    }
    seen[at] = true;
    for (int i = 0; i < acount(f->definitions); i++) {
        if (!reached(c, f->definitions[i], seen, depth + 1)) {
            return false;
        }
    }
    return true;
}
static bool owners(prepared_context *c)
{
    int count = acount(c->functions);
    if (!execution_wire_reserve(&c->wire, (size_t)count * (sizeof(int) + sizeof(bool)))) {
        return false;
    }
    int *owned = xcalloc((size_t)count, sizeof *owned);
    execution_module_plan *p = c->plan;
    bool valid = true;
#define OWNER(v)                                                               \
    do {                                                                       \
        if (v) {                                                               \
            int at = -1;                                                       \
            for (int n = 0; n < count; n++)                                    \
                if (c->functions[n] == (v)) at = n;                            \
            if (at < 0 || ++owned[at] != 1) valid = false;                     \
        }                                                                      \
    } while (0)
    for (int i = 0; i < p->function_count; i++) {
        OWNER(p->functions[i].body);
        OWNER(p->functions[i].subscription);
    }
    for (int i = 0; i < p->binding_count; i++) {
        OWNER(p->bindings[i].body);
        OWNER(p->bindings[i].dependencies);
    }
    for (int i = 0; i < p->description_count; i++) {
        OWNER(p->descriptions[i].evaluate);
    }
    for (int i = 0; i < p->query_count; i++) {
        OWNER(p->queries[i].predicate);
    }
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < acount(c->functions[i]->definitions); j++) {
            OWNER(c->functions[i]->definitions[j]);
        }
    }
    for (int i = 0; i < count; i++) {
        if (owned[i] != 1) {
            valid = false;
        }
    }
    /* A forest with single ownership can still contain a disconnected cycle;
     * bounded traversal from roots proves every record is reachable. */
    free(owned);
    bool *seen = xcalloc((size_t)count, sizeof *seen);
    for (int i = 0; i < p->function_count; i++) {
        valid &= reached(c, p->functions[i].body, seen, 0);
        valid &= reached(c, p->functions[i].subscription, seen, 0);
    }
    for (int i = 0; i < p->binding_count; i++) {
        valid &= reached(c, p->bindings[i].body, seen, 0);
        valid &= reached(c, p->bindings[i].dependencies, seen, 0);
    }
    for (int i = 0; i < p->description_count; i++) {
        valid &= reached(c, p->descriptions[i].evaluate, seen, 0);
    }
    for (int i = 0; i < p->query_count; i++) {
        valid &= reached(c, p->queries[i].predicate, seen, 0);
    }
    for (int i = 0; i < count; i++) {
        valid &= seen[i];
    }
    free(seen);
    if (!valid) {
        c->wire.error = "invalid definition ownership";
    }
    return valid;
#undef OWNER
}
static void discard_functions(prepared_context *c)
{
    for (int i = 0; i < acount(c->functions); i++) {
        afree(c->functions[i]->definitions);
        behavior_function_free(c->functions[i]);
    }
    for (int i = 0; i < c->plan->function_count; i++) {
        c->plan->functions[i].body = NULL;
        c->plan->functions[i].subscription = NULL;
    }
    for (int i = 0; i < c->plan->binding_count; i++) {
        c->plan->bindings[i].body = NULL;
        c->plan->bindings[i].dependencies = NULL;
    }
    for (int i = 0; i < c->plan->description_count; i++) {
        c->plan->descriptions[i].evaluate = NULL;
    }
    for (int i = 0; i < c->plan->query_count; i++) {
        c->plan->queries[i].predicate = NULL;
    }
}
static bool write_depth(
    const execution_module *m,
    unsigned char **data,
    size_t *length,
    const char **error,
    unsigned depth,
    prepared_dependency **cache,
    size_t *dependency_bytes,
    uint64_t *hash,
    execution_wire *budget,
    size_t *record_work,
    size_t record_work_limit
)
{
    if (depth > EXECUTION_WIRE_MAX_DEPTH) {
        *error = "import depth exceeds limit";
        return false;
    }
    prepared_context c = { .plan = (execution_module_plan *)&m->plan,
                           .dependencies = cache,
                           .dependency_bytes = dependency_bytes,
                           .fingerprint_budget = budget,
                           .shared_record_work = record_work,
                           .record_work_limit = record_work_limit };
    c.wire.storage = &c.plan->storage;
    c.wire.hash = hash;
    /* The string table's scratch, released when the table is written. */
    c.wire.storage_budget = budget ? budget->storage_budget : NULL;
    /* A dependency's fingerprint re-encodes a module its importer's linker
     * admitted, so it takes that linker's record bound, not the default. */
    c.wire.record_limit = budget ? budget->record_limit : 0;
    for (int i = 0; i < c.plan->function_count && !c.wire.error; i++) {
        add_function(&c, c.plan->functions[i].body, 0);
        add_function(&c, c.plan->functions[i].subscription, 0);
    }
    for (int i = 0; i < c.plan->binding_count && !c.wire.error; i++) {
        add_function(&c, c.plan->bindings[i].body, 0);
        add_function(&c, c.plan->bindings[i].dependencies, 0);
    }
    for (int i = 0; i < c.plan->description_count && !c.wire.error; i++) {
        add_function(&c, c.plan->descriptions[i].evaluate, 0);
    }
    for (int i = 0; i < c.plan->query_count && !c.wire.error; i++) {
        add_function(&c, c.plan->queries[i].predicate, 0);
    }
    execution_wire *w = &c.wire;
    header(w);
    execution_wire_strings_begin(w);
    U64(c.plan->id);
    U64(c.plan->owner_type);
    if (!w->error && !c.plan->id) {
        w->error = "module lacks stable identity";
    }
    prepared_types(&c);
    events(&c);
    imports(&c, depth);
    int count = acount(c.functions);
    execution_wire_count(w, &count, 1);
    for (int i = 0; i < count && !w->error; i++) {
        prepared_function(&c, c.functions[i]);
    }
    payload(&c);
    execution_wire_strings_finish(w);
    bool success = !w->error;
    if (success) {
        *length = w->size;
        if (!hash) {
            *data = xmalloc(w->size);
            memcpy(*data, w->data, w->size);
        }
    }
    *error = w->error;
    free(c.records);
    afree(c.functions);
    afree(w->data);
    if (budget) {
        size_t scratch = c.function_bytes + c.record_cache_bytes;
        *budget->storage_budget += scratch;
        budget->storage_charged -= scratch;
        budget->owned_bytes -= scratch;
    }
    return success;
}
bool execution_prepared_write(
    const execution_module *m,
    unsigned char **data,
    size_t *length,
    char *error,
    size_t size
)
{
    *data = NULL;
    *length = 0;
    const char *reason = NULL;
    prepared_dependency *cache = NULL;
    size_t bytes = 0;
    size_t remaining = EXECUTION_WIRE_MAX_BYTES, record_work = 0;
    execution_wire budget = { .storage_budget = &remaining };
    bool success = write_depth(m, data, length, &reason, 0, &cache, &bytes, NULL, &budget, &record_work, 0);
    afree(cache);
    if (size) {
        snprintf(error, size, "%s", reason ? reason : "");
    }
    return success;
}
bool execution_prepared_measure(
    const execution_module *m,
    size_t *length,
    uint64_t *fingerprint,
    char *error,
    size_t size
)
{
    *length = 0;
    const char *reason = NULL;
    prepared_dependency *cache = NULL;
    size_t bytes = 0;
    size_t remaining = EXECUTION_WIRE_MAX_BYTES, record_work = 0;
    execution_wire budget = { .storage_budget = &remaining };
    uint64_t hash = UINT64_C(14695981039346656037);
    bool success = write_depth(m, NULL, length, &reason, 0, &cache, &bytes, &hash, &budget, &record_work, 0);
    afree(cache);
    *fingerprint = success ? hash : 0;
    if (size) {
        snprintf(error, size, "%s", reason ? reason : "");
    }
    return success;
}
static bool preparation_budget(prepared_context *c)
{
    execution_wire *w = &c->wire;
    size_t bytes = execution_module_preparation_bytes(c->plan);
    size_t effects = execution_module_effects_bytes(c->plan);
    if (effects > SIZE_MAX - bytes) {
        w->error = "prepared effects exceed allocation budget";
        return false;
    }
    bytes += effects;
    if (bytes == SIZE_MAX) {
        w->error = "prepared module closure exceeds preparation limit";
        return false;
    }
    /* Definitions are prepared one at a time: each keeps its retained
     * storage, while only the largest verification scratch is live at once. */
    size_t scratch = 0;
    for (int i = 0; i < acount(c->functions); i++) {
        behavior_function_preparation next = behavior_function_preparation_bytes(
            c->functions[i]
        );
        if (next.retained == SIZE_MAX || next.scratch == SIZE_MAX ||
            next.retained > SIZE_MAX - bytes) {
            w->error = "prepared finalization count exceeds budget";
            return false;
        }
        bytes += next.retained;
        scratch = next.scratch > scratch ? next.scratch : scratch;
    }
    if (scratch > SIZE_MAX - bytes) {
        w->error = "prepared finalization count exceeds budget";
        return false;
    }
    bytes += scratch;
    size_t services = 1;
    for (int i = 0; i < acount(c->functions); i++) {
        const behavior_function *f = c->functions[i];
        services += 2 + (size_t)acount(f->host_calls) +
                    (size_t)acount(f->commands) + (size_t)acount(f->queries);
    }
    for (int i = 0; i < c->plan->import_count; i++) {
        services += (size_t)c->plan->imports[i]->plan.service_count;
    }
    if (services > EXECUTION_WIRE_MAX_RECORDS) {
        w->error = "prepared service requirement count exceeds budget";
        return false;
    }
    if (!execution_wire_reserve(w, bytes) ||
        !execution_wire_reserve(w, services * sizeof *c->plan->services)) {
        return false;
    }
    prepared_vector(
        w,
        (void **)&c->plan->services,
        (int)services,
        sizeof *c->plan->services
    );
    if (w->error) {
        return false;
    }
    asetlen(c->plan->services, 0);
    return true;
}
execution_module *execution_prepared_read_budget(
    const void *data,
    size_t length,
    const execution_prepared_linker *linker,
    behavior_backend backend,
    size_t *remaining,
    char *error,
    size_t size
)
{
    assert(remaining);
    size_t initial = *remaining;
    if (initial < sizeof(execution_module_plan) + sizeof(execution_module) ||
        length > EXECUTION_WIRE_MAX_BYTES) {
        if (size) {
            snprintf(error, size, "%s", "prepared input exceeds aggregate byte budget");
        }
        return NULL;
    }
    prepared_dependency *cache = NULL;
    size_t dependency_bytes = 0;
    prepared_context c = { .linker = linker,
                           .plan = execution_module_plan_new(),
                           .dependencies = &cache,
                           .dependency_bytes = &dependency_bytes,
                           .record_work_limit = linker ? linker->layout_work : 0 };
    c.wire = (execution_wire){ .data = (unsigned char *)data,
                               .size = length,
                               .reading = true,
                               .storage = &c.plan->storage,
                               .storage_budget = remaining,
                               .owned_limit = linker ? linker->decoded_bytes : 0,
                               .record_limit = linker ? linker->decoded_records : 0 };
    execution_wire *w = &c.wire;
    c.fingerprint_budget = w;
    c.shared_record_work = &c.record_work;
    execution_wire_reserve(w, sizeof(execution_module_plan) + sizeof(execution_module));
    if (length > EXECUTION_WIRE_MAX_BYTES) {
        w->error = "prepared file exceeds byte budget";
    }
    header(w);
    execution_wire_strings_begin(w);
    U64(c.plan->id);
    U64(c.plan->owner_type);
    if (!w->error && !c.plan->id) {
        w->error = "module lacks stable identity";
    }
    prepared_types(&c);
    events(&c);
    imports(&c, 0);
    int count = 0;
    prepared_table(w, &count, sizeof(behavior_function) + sizeof(execution_artifact));
    if (!w->error) {
        execution_wire_reserve(w, (size_t)count * sizeof *c.functions);
        prepared_vector(w, (void **)&c.functions, count, sizeof *c.functions);
    }
    for (int i = 0; i < count && !w->error; i++) {
        behavior_function *f = xcalloc(1, sizeof *f);
        f->artifact = execution_artifact_new();
        c.functions[i] = f;
    }
    for (int i = 0; i < count && !w->error; i++) {
        w->storage = &c.functions[i]->owned;
        prepared_function(&c, c.functions[i]);
    }
    w->storage = &c.plan->storage;
    payload(&c);
    execution_wire_strings_finish(w);
    if (!w->error && w->offset != w->size) {
        w->error = "trailing prepared bytes";
    }
    if (!w->error) {
        owners(&c);
    }
    if (!w->error && prepared_records_finalize(&c)) {
        preparation_budget(&c);
    }
    /* The verifier's own diagnostic, kept for the reader's message. */
    char verification[256] = "";
    if (!w->error) {
        for (int i = count - 1; i >= 0; i--) {
            size_t before = arena_storage_bytes(&c.functions[i]->owned);
            size_t limit = *remaining > SIZE_MAX - before ? SIZE_MAX
                                                          : before + *remaining;
            if (!behavior_function_prepare_definition(c.functions[i], limit, verification, sizeof verification)) {
                w->error = "prepared definition verification failed";
                break;
            }
            size_t growth = arena_storage_bytes(&c.functions[i]->owned) - before;
            assert(growth <= *remaining);
            *remaining -= growth;
            w->storage_charged += growth;
        }
    }
    execution_module *m = NULL;
    if (!w->error) {
        c.plan->valid = true;
        m = execution_module_finish_decoded(&c.plan, backend, error, size);
    }
    if (m) {
        size_t bytes = execution_module_storage_bytes(m);
        size_t available = *remaining + w->storage_charged;
        if (bytes > available) {
            execution_module_free(m);
            m = NULL;
            w->error = "finalized canonical storage exceeds aggregate budget";
        } else {
            *remaining = available - bytes;
        }
    }
    if (!m) {
        *remaining = initial;
        if (w->error && size) {
            snprintf(error, size, *verification ? "%s: %s" : "%s", w->error, verification);
        }
        if (c.plan) {
            discard_functions(&c);
            execution_module_plan_free(c.plan);
        }
    }
    free(c.records);
    afree(c.functions);
    afree(cache);
    return m;
}
execution_module *execution_prepared_read(
    const void *data,
    size_t length,
    const execution_prepared_linker *linker,
    behavior_backend backend,
    char *error,
    size_t size
)
{
    size_t remaining = EXECUTION_WIRE_MAX_BYTES;
    return execution_prepared_read_budget(data, length, linker, backend, &remaining, error, size);
}
