#include "execution/query.h"

#include "ecs/execution.h"
#include "execution/definition.h"
#include "execution/intrinsics.h"
#include "execution/module_internal.h"
#include "execution/runner.h"
#include "execution/service.h"
#include "execution/status.h"
#include "support/xalloc.h"
#include "types/query.h"
#include "types/record.h"
#include "types/scalar.h"
#include "types/schema.h"
#include "types/trace.h"

#include <assert.h>
#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct
{
    pin_query *description;
    uint64_t *identities;
    int count, cursor, width;
    bool ended;
} query_cursor;

static void query_trace(const void *data, const void *, arena_marker mark, void *user)
{
    const pin_query *query = data;
    mark(user, query->values);
    for (int i = 0; i < query->count; i++) {
        pin_value_trace(&query->values[i], mark, user);
    }
}
static void query_free(void *data)
{
    const pin_query *query = data;
    execution_module_free((execution_module *)query->module);
}
static pin_query *query_new(
    arena *storage,
    const execution_module *module,
    int description,
    pin_query_kind kind,
    int count
)
{
    pin_query *query = arena_alloc(storage, sizeof *query);
    *query = (pin_query){ .kind = kind,
                          .storage = storage,
                          .module = module,
                          .description = description,
                          .count = count };
    query->values = arena_alloc(storage, (size_t)(count + 1) * sizeof *query->values);
    memset(query->values, 0, (size_t)(count + 1) * sizeof *query->values);
    execution_module_retain((execution_module *)module);
    arena_set_trace(storage, query, query_trace, NULL);
    arena_set_cleanup(storage, query, query_free, query, 0);
    return query;
}
static bool function_contains(const behavior_function *root, const behavior_function *f, int depth)
{
    if (root == f) {
        return true;
    }
    if (!root || depth > 64) {
        return false;
    }
    if (function_contains(root->fast, f, depth + 1)) {
        return true;
    }
    for (int i = 0; i < acount(root->definitions); i++) {
        if (function_contains(root->definitions[i], f, depth + 1)) {
            return true;
        }
    }
    return false;
}
static const execution_module *function_module(
    const execution_module *module,
    const behavior_function *f,
    int depth
)
{
    if (!module || depth > 64) {
        return NULL;
    }
    const execution_module_plan *plan = &module->plan;
    for (int i = 0; i < plan->function_count; i++) {
        if (function_contains(plan->functions[i].body, f, 0) ||
            function_contains(plan->functions[i].subscription, f, 0)) {
            return module;
        }
    }
    for (int i = 0; i < plan->description_count; i++) {
        if (function_contains(plan->descriptions[i].evaluate, f, 0)) {
            return module;
        }
    }
    for (int i = 0; i < plan->import_count; i++) {
        const execution_module *found = function_module(plan->imports[i], f, depth + 1);
        if (found) {
            return found;
        }
    }
    return NULL;
}
static bool dynamic_terms(const execution_module *module, int root)
{
    const execution_module_query *term = &module->plan.queries[root];
    if (term->kind == EXECUTION_QUERY_VALUE || term->kind == EXECUTION_QUERY_CHANGED) {
        return true;
    }
    for (int i = 0; i < term->child_count; i++) {
        if (dynamic_terms(module, term->children[i])) {
            return true;
        }
    }
    return false;
}
static const execution_module_query *named_disabled(const execution_module *module, int root)
{
    const execution_module_query *term = &module->plan.queries[root];
    if (term->component == ECS_DISABLED) {
        return term;
    }
    for (int i = 0; i < term->child_count; i++) {
        const execution_module_query *named = named_disabled(module, term->children[i]);
        if (named) {
            return named;
        }
    }
    return NULL;
}
static void terms(const execution_module *module, int root, ecs_oper oper, bool ordered, ecs_term **out)
{
    const execution_module_query *query = &module->plan.queries[root];
    if (ordered && query->kind != EXECUTION_QUERY_GENERATOR &&
        !query->required && query->kind != EXECUTION_QUERY_ALL) {
        return;
    }
    if (query->kind == EXECUTION_QUERY_VALUE || query->kind == EXECUTION_QUERY_CHANGED) {
        return;
    }
    if (query->kind == EXECUTION_QUERY_HAS || query->kind == EXECUTION_QUERY_RELATION ||
        query->kind == EXECUTION_QUERY_GENERATOR) {
        apush(
            *out,
            ((ecs_term){ .oper = oper,
                         .id = query->component,
                         .src_var = query->source_variable,
                         .target_var = query->kind == EXECUTION_QUERY_RELATION
                                         ? ECS_WILDCARD
                                         : query->target_variable,
                         .src_rel = query->source_traversal ? 0 : query->source_relationship,
                         .trav = (ecs_trav)query->source_traversal,
                         .trav_rel = query->source_relationship })
        );
        return;
    }
    bool scope = query->kind != EXECUTION_QUERY_ALL || oper != ECS_AND;
    if (scope) {
        apush(*out, ((ecs_term){ .kind = ECS_TERM_OPEN, .oper = oper }));
    }
    ecs_oper child = query->kind == EXECUTION_QUERY_ANY ? ECS_OR
                   : query->kind == EXECUTION_QUERY_NOT ? ECS_NOT
                                                        : ECS_AND;
    for (int i = 0; i < query->child_count; i++) {
        terms(module, query->children[i], child, ordered, out);
    }
    if (scope) {
        apush(*out, ((ecs_term){ .kind = ECS_TERM_CLOSE }));
    }
}
void execution_query_terms(const execution_module *module, int root, ecs_term **out)
{
    int first = acount(*out);
    terms(module, root, ECS_AND, dynamic_terms(module, root), out);
    const execution_module_query *disabled = named_disabled(module, root);
    if (!disabled) {
        return;
    }
    for (int i = first; i < acount(*out); i++) {
        if ((*out)[i].id == ECS_DISABLED) {
            return;
        }
    }
    /* Naming Disabled opts into those entities even when an ordered guard
     * stays in the evaluator. The optional term preserves that planner policy
     * without evaluating the guard before its earlier header stages. */
    apush(
        *out,
        ((ecs_term){ .oper = ECS_OPTIONAL,
                     .id = ECS_DISABLED,
                     .src_var = disabled->source_variable,
                     .src_rel = disabled->source_traversal ? 0 : disabled->source_relationship,
                     .trav = (ecs_trav)disabled->source_traversal,
                     .trav_rel = disabled->source_relationship })
    );
}
const char *execution_query_evaluate(
    behavior_function_context *parent,
    const execution_module *module,
    int description,
    uint64_t owner,
    const uint64_t *variables,
    const pin_value *captures,
    int count,
    pin_value *row,
    bool *accepted
)
{
    const execution_module_query_description *query = &module->plan.descriptions[description];
    const behavior_function *function = query->evaluate;
    assert(count == function->argument_count && parent->managed_heap);
    query_evaluator *e = program_query_evaluator_take(parent->program, module, function);
    pin_value *results = xcalloc((size_t)function->result_count, sizeof *results);
    for (int i = 0; i < function->result_count; i++) {
        results[i] = function->result_defaults[i];
    }
    behavior_function_prepare_results(function, results, &e->storage);
    behavior_function_context *context = &e->context;
    context->parent = parent;
    context->activation = &e->activation;
    context->module = module;
    context->program = parent->program;
    context->arguments = captures;
    context->argument_count = count;
    context->results = results;
    context->variables = parent->variables;
    context->states = parent->states;
    context->variable_storage = parent->variable_storage;
    context->query_variables = variables;
    context->owner = owner;
    context->query_test = parent->query_test;
    context->value_owner = parent->value_owner;
    context->owner_type_name = "ecs.entity";
    context->services = parent->services;
    context->managed_heap = parent->managed_heap;
    context->budget = parent->budget;
    context->now = parent->now;
    context->delta_seconds = parent->delta_seconds;
    behavior_function_bind_storage(context, &e->storage);
    const char *error = NULL;
    if (!behavior_function_bind(function, context, parent->services, owner)) {
        error = "Query evaluator world binding is unavailable";
    } else {
        behavior_function_prepare_host(function, context);
        int status = behavior_function_run(function, context, module->backend != BEHAVIOR_BYTECODE);
        if (status) {
            error = context->error_message ? context->error_message
                                           : "Query evaluator failed";
        } else {
            *accepted = results[0].b;
            if (*accepted) {
                arena *heap = &parent->managed_heap->storage;
                pin_query *value = query_new(heap, module, description, PIN_QUERY_ROW, function->result_count - 1);
                for (int i = 0; i < value->count && !error; i++) {
                    error = behavior_managed_storage_owner_error(heap, &results[i + 1]);
                    if (!error &&
                        pin_value_snapshot_owned_to_arena(heap, &value->values[i], &results[i + 1])) {
                        error = "Cannot retain query row field";
                    }
                }
                *row = query->row_type;
                row->ptr = value;
            }
        }
    }
    if (error) {
        arena *destination = parent->value_storage ? parent->value_storage
                                                   : &parent->managed_heap->storage;
        size_t bytes = strlen(error) + 1;
        char *message = arena_alloc(destination, bytes);
        memcpy(message, error, bytes);
        error = message;
    }
    if (error) {
        program_query_evaluator_free(e);
    } else {
        program_query_evaluator_keep(parent->program, e);
    }
    free(results);
    return error;
}
static const execution_type_requirement *native_requirement(
    const execution_module *module,
    const char *name
)
{
    if (!name) {
        return NULL;
    }
    for (int i = 0; i < module->plan.type_count; i++) {
        const execution_type_requirement *type = &module->plan.types[i];
        if (type->storage == PIN_STRUCT && !type->record && !strcmp(type->name, name)) {
            return type;
        }
    }
    for (int i = 0; i < module->plan.import_count; i++) {
        const execution_type_requirement *type = native_requirement(module->plan.imports[i], name);
        if (type) {
            return type;
        }
    }
    return NULL;
}
static const pin_schema *value_schema(pin_value value)
{
    if (value.schema) {
        return value.schema;
    }
    if ((value.container == PIN_CONTAINER_ARRAY || value.container == PIN_CONTAINER_SET) &&
        value.ptr) {
        return ((const pin_array *)value.ptr)->schema;
    }
    return NULL;
}
static bool same_type_at(const execution_module *, pin_value, pin_value, int);
static uint64_t collection_id(const char *name)
{
    if (!name || strncmp(name, "__collection_", 13)) {
        return 0;
    }
    uint64_t id = 0;
    const char *digits = name + 13;
    if (!*digits || *digits == '0') {
        return 0;
    }
    for (const char *p = digits; *p; p++) {
        if (*p < '0' || *p > '9' || id > (UINT64_MAX - (*p - '0')) / 10) {
            return 0;
        }
        id = id * 10 + (*p - '0');
    }
    return id;
}
/* The serialized collection-name protocol uses FNV identities. Keep this
 * native subset independent of the compiler catalog and its process state. */
static uint64_t collection_identity(const char *domain, const char *text)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char *p = (const unsigned char *)domain; *p; p++) {
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    }
    hash = (hash ^ ':') * UINT64_C(1099511628211);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        hash = (hash ^ *p) * UINT64_C(1099511628211);
    }
    return hash ? hash : 1;
}
static uint64_t collection_element_tag(pin_value value, uint64_t nominal, unsigned flags)
{
    char name[128];
    snprintf(name, sizeof name, "%d:%d:%" PRIu64 ":0:%u", value.type, value.container, nominal, flags);
    uint64_t nested = collection_id(value.type_name);
    if (nested) {
        size_t used = strlen(name);
        snprintf(name + used, sizeof name - used, ":%" PRIu64, nested);
    }
    return collection_identity("union-type", name);
}
/* Managed snapshots own a native layout as a native record. It still
 * describes the native type its name declares, unlike an authored record. */
static bool authored(const pin_record *record)
{
    return record && !record->native;
}
static bool native_collection_shape(const execution_module *module, pin_value value, uint64_t nominal, int depth)
{
    if (depth >= 64 || value.type != PIN_STRUCT || authored(value.record) ||
        (value.container != PIN_CONTAINER_ARRAY && value.container != PIN_CONTAINER_SET)) {
        return false;
    }
    if (!value.type_name || strncmp(value.type_name, "__collection_", 13)) {
        const execution_type_requirement
            *type = native_requirement(module, value.type_name);
        return type && type->id == nominal &&
               same_type_at(module, value, value, depth);
    }
    uint64_t identity = collection_id(value.type_name);
    if (!identity) {
        return false;
    }
    if (!value.ptr) {
        return true;
    }
    const pin_array *array = value.ptr;
    if (!pin_array_valid(array) || array->element_type != value.type || !array->type_name ||
        strcmp(array->type_name, value.type_name) || authored(array->record)) {
        return false;
    }
    for (int i = 0; i < array->count; i++) {
        pin_value item = pin_array_get(array, i);
        if (!native_collection_shape(module, item, nominal, depth + 1)) {
            return false;
        }
        bool matches = false;
        /* Value headers erase these source qualifiers. They do not change
         * the native leaf identity or the physical collection shape. */
        enum
        {
            TYPE_OPTIONAL = 2,
            TYPE_RESULTS = 8
        };
        const unsigned flags[] = { 0, TYPE_OPTIONAL, TYPE_RESULTS, TYPE_OPTIONAL | TYPE_RESULTS };
        for (int j = 0; j < 4 && !matches; j++) {
            char key[96];
            snprintf(
                key,
                sizeof key,
                "%d:%" PRIu64 ":%" PRIu64,
                value.container,
                collection_identity("union-type", "0:0:0:0:0"),
                collection_element_tag(item, nominal, flags[j])
            );
            matches = identity == collection_identity("collection-type", key);
        }
        if (!matches) {
            return false;
        }
    }
    return true;
}
static bool known_native_collection_shape(
    const execution_module *module,
    const execution_module *types,
    pin_value value,
    int depth
)
{
    if (depth >= 64) {
        return false;
    }
    for (int i = 0; i < types->plan.type_count; i++) {
        const execution_type_requirement *type = &types->plan.types[i];
        if (type->storage == PIN_STRUCT && !type->record &&
            native_collection_shape(module, value, type->id, 0)) {
            return true;
        }
    }
    for (int i = 0; i < types->plan.import_count; i++) {
        if (known_native_collection_shape(module, types->plan.imports[i], value, depth + 1)) {
            return true;
        }
    }
    return false;
}
static bool same_type_at(const execution_module *module, pin_value a, pin_value b, int depth)
{
    if (depth >= 64 || a.type != b.type || a.container != b.container ||
        authored(a.record) != authored(b.record) ||
        (authored(a.record) && !pin_record_same(a.record, b.record)) ||
        !((!a.type_name && !b.type_name) ||
          (a.type_name && b.type_name && !strcmp(a.type_name, b.type_name)))) {
        return false;
    }
    if (a.type != PIN_STRUCT || authored(a.record) || !b.ptr) {
        return true;
    }
    /* Synthetic names encode the nested semantic element identity. Check
     * that identity against the payload, rather than only its homogeneity. */
    if ((b.container == PIN_CONTAINER_ARRAY || b.container == PIN_CONTAINER_SET) &&
        b.type_name && !strncmp(b.type_name, "__collection_", 13)) {
        return known_native_collection_shape(module, module, b, 0);
    }
    if (b.container == PIN_CONTAINER_ARRAY || b.container == PIN_CONTAINER_SET) {
        const pin_array *array = b.ptr;
        if (!pin_array_valid(array) || array->element_type != b.type || !array->type_name ||
            strcmp(array->type_name, b.type_name) || authored(array->record)) {
            return false;
        }
    }
    const execution_type_requirement *required = native_requirement(module, a.type_name);
    const type_desc *expected = pin_value_type(&a), *actual = pin_value_type(&b);
    if (!actual || (!required && !expected)) {
        return false;
    }
    size_t expected_size = required ? required->size : expected->size;
    const pin_schema *expected_schema = value_schema(a),
                     *actual_schema = value_schema(b);
    uint64_t expected_layout = required ? required->layout
                             : expected_schema
                                 ? pin_schema_layout_type(expected_schema, expected)
                                 : type_layout_hash(expected);
    uint64_t actual_layout = actual_schema
                               ? pin_schema_layout_type(actual_schema, actual)
                               : type_layout_hash(actual);
    if (expected_size != actual->size || expected_layout != actual_layout) {
        return false;
    }
    if (b.container == PIN_CONTAINER_ARRAY || b.container == PIN_CONTAINER_SET) {
        const pin_array *array = b.ptr;
        pin_value element = b;
        element.ptr = NULL;
        element.container = PIN_CONTAINER_SINGLE;
        for (int i = 0; i < array->count; i++) {
            pin_value item = pin_array_get(array, i);
            if (!same_type_at(module, element, item, depth + 1)) {
                return false;
            }
        }
    }
    return true;
}
static bool same_type(const execution_module *module, pin_value a, pin_value b)
{
    return same_type_at(module, a, b, 0);
}
static const char *make_query(arena *, const pin_value *args, int count, pin_value *out)
{
    assert(count >= 2);
    behavior_function_context *context = (void *)(uintptr_t)args[0].u64;
    const execution_module *module = function_module(context->module, context->source_function, 0);
    if (!module) {
        module = context->module;
    }
    int index = args[1].i;
    if (!module || index < 0 || index >= module->plan.description_count ||
        !context->managed_heap || !context->managed_heap->storage.collectable) {
        return "Query description requires its owning managed execution "
               "context";
    }
    const execution_module_query_description *description = &module->plan.descriptions[index];
    if (count - 2 != description->evaluate->argument_count) {
        return "Query capture count differs from its description";
    }
    arena *heap = &context->managed_heap->storage;
    for (int i = 2; i < count; i++) {
        if (!same_type(module, description->evaluate->argument_defaults[i - 2], args[i])) {
            return "Query capture type differs from its description";
        }
        const char *error = behavior_managed_storage_owner_error(heap, &args[i]);
        if (error) {
            return error;
        }
    }
    pin_query *query = query_new(heap, module, index, PIN_QUERY_DESCRIPTION, count - 2);
    for (int i = 0; i < query->count; i++) {
        if (pin_value_snapshot_owned_to_arena(heap, &query->values[i], &args[i + 2])) {
            return "Cannot retain query capture";
        }
    }
    out->ptr = query;
    out->type = PIN_QUERY;
    return NULL;
}
static void cursor_trace(const void *data, const void *, arena_marker mark, void *user)
{
    const query_cursor *cursor = data;
    mark(user, cursor->description);
    mark(user, cursor->identities);
}
static void cursor_free(void *data)
{
    query_cursor *cursor = data;
    cursor->identities = NULL;
    cursor->description = NULL;
    cursor->ended = true;
}
static const char *begin_query(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2);
    (void)count;
    behavior_function_context *context = (void *)(uintptr_t)args[0].u64;
    pin_query *query = args[1].ptr;
    if (!query || query->kind != PIN_QUERY_DESCRIPTION ||
        !same_type(
            query->module,
            args[1],
            query->module->plan.descriptions[query->description].row_type
        )) {
        return "Iteration requires a compatible query description";
    }
    ecs_world *world = NULL;
    for (int i = 0; i < acount(context->services); i++) {
        if (!strcmp(context->services[i].type_name, "ecs.world")) {
            world = context->services[i].value;
            break;
        }
    }
    if (!world || !storage->collectable) {
        return "Query iteration requires an invocation and ECS world";
    }
    const execution_module_query_description
        *description = &query->module->plan.descriptions[query->description];
    ecs_term *native_terms = NULL;
    execution_query_terms(query->module, description->query, &native_terms);
    ecs_query native;
    int status = ecs_query_begin_terms(world, &native, native_terms, acount(native_terms));
    afree(native_terms);
    if (status) {
        return "Cannot begin query plan";
    }
    query_cursor *cursor = arena_alloc(storage, sizeof *cursor);
    *cursor = (query_cursor){ .description = query,
                              .width = description->query_variable_count + 1 };
    ecs_entity entity;
    uint64_t *identities = NULL;
    while (ecs_query_next(&native, &entity)) {
        size_t requested = (size_t)acount(identities) + cursor->width;
        size_t limit = ((size_t)INT_MAX - sizeof(dyna_hdr)) / sizeof *identities;
        if (requested > limit || (requested > (size_t)acap(identities) &&
                                  (size_t)acap(identities) > limit / 2)) {
            ecs_query_end(&native);
            afree(identities);
            cursor_free(cursor);
            return "Query iterator snapshot exceeds its index range";
        }
        apush(identities, ecs_entity_to_u64(entity));
        for (int i = 1; i < cursor->width; i++) {
            apush(identities, ecs_entity_to_u64(ecs_query_var(&native, i)));
        }
        cursor->count++;
    }
    ecs_query_end(&native);
    size_t bytes = (size_t)acount(identities) * sizeof *identities;
    cursor->identities = arena_alloc(storage, bytes ? bytes : sizeof *identities);
    if (bytes) {
        memcpy(cursor->identities, identities, bytes);
    }
    afree(identities);
    arena_set_trace(storage, cursor, cursor_trace, NULL);
    arena_set_cleanup(storage, cursor, cursor_free, cursor, 0);
    *out = (pin_value){ .type = BF_POINTER_TYPE, .u64 = (uintptr_t)cursor };
    return NULL;
}
static int identity_matches(
    const execution_module *module,
    int index,
    const ecs_execution_view *world,
    const uint64_t *identity,
    bool required_only
)
{
    const execution_module_query *term = &module->plan.queries[index];
    uint64_t source = identity[term->source_variable];
    switch (term->kind) {
        case EXECUTION_QUERY_HAS:
            if (required_only && !term->required) {
                return -1;
            }
            return world->query_source && world->query_source(
                                              world,
                                              source,
                                              term->component,
                                              term->source_relationship,
                                              term->source_traversal
                                          ) != 0;
        case EXECUTION_QUERY_GENERATOR:
        case EXECUTION_QUERY_RELATION:
            if (required_only && term->kind == EXECUTION_QUERY_RELATION) {
                return -1;
            }
            if (!world->query_target) {
                return false;
            }
            for (int i = 0;; i++) {
                uint64_t target = world->query_target(world, source, term->component, i);
                if (!target) {
                    return false;
                }
                if (term->kind == EXECUTION_QUERY_RELATION ||
                    target == identity[term->target_variable]) {
                    return true;
                }
            }
        case EXECUTION_QUERY_VALUE:
        case EXECUTION_QUERY_CHANGED: return -1;
        case EXECUTION_QUERY_NOT: {
            int match = identity_matches(module, term->children[0], world, identity, required_only);
            return match < 0 ? -1 : !match;
        }
        case EXECUTION_QUERY_ALL:
        case EXECUTION_QUERY_ANY: {
            bool unknown = false;
            for (int i = 0; i < term->child_count; i++) {
                int match = identity_matches(module, term->children[i], world, identity, required_only);
                unknown |= match < 0;
                if ((term->kind == EXECUTION_QUERY_ALL && match == 0) ||
                    (term->kind == EXECUTION_QUERY_ANY && match > 0)) {
                    return match;
                }
            }
            return unknown ? -1 : term->kind == EXECUTION_QUERY_ALL;
        }
    }
    return false;
}
bool execution_query_membership_matches(
    const execution_module *module,
    int root,
    const ecs_execution_view *world,
    uint64_t owner,
    const uint64_t *variables
)
{
    uint64_t identity[ECS_QUERY_VARS + 1] = { owner };
    memcpy(identity + 1, variables, (size_t)module->plan.query_variable_count * sizeof *variables);
    return identity_matches(module, root, world, identity, false) != 0;
}
static const char *next_query(
    const behavior_function_context *caller,
    void *,
    const pin_value *args,
    pin_value *out
)
{
    behavior_function_context *context = (void *)caller;
    query_cursor *cursor = (void *)(uintptr_t)args[1].u64;
    if (!cursor || cursor->ended) {
        return "Query iterator has ended";
    }
    if (!context->world) {
        for (int i = 0; i < acount(context->services); i++) {
            if (!strcmp(context->services[i].type_name, "ecs.world")) {
                context->world = ecs_world_execution(context->services[i].value);
                break;
            }
        }
    }
    out[0] = (pin_value){ .type = PIN_BOOL };
    out[1].ptr = NULL;
    while (cursor->cursor < cursor->count) {
        const uint64_t *identity = cursor->identities +
                                   (size_t)cursor->cursor++ * cursor->width;
        if (!context->world || !ecs_execution_entity(context->world, identity[0])) {
            continue;
        }
        bool live = true;
        for (int i = 1; i < cursor->width; i++) {
            live &= ecs_execution_entity(context->world, identity[i]) != 0;
        }
        if (!live) {
            continue;
        }
        pin_query *description = cursor->description;
        int root = description->module->plan
                       .descriptions[description->description]
                       .query;
        if (identity_matches(
                description->module,
                root,
                context->world,
                identity,
                dynamic_terms(description->module, root)
            ) == 0) {
            continue;
        }
        bool accepted = false;
        const char *error = execution_query_evaluate(
            context,
            description->module,
            description->description,
            identity[0],
            identity + 1,
            description->values,
            description->count,
            &out[1],
            &accepted
        );
        if (error) {
            return error;
        }
        if (accepted) {
            out[0].b = true;
            return NULL;
        }
    }
    return NULL;
}
static const char *end_query(arena *, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2);
    (void)count;
    query_cursor *cursor = (void *)(uintptr_t)args[1].u64;
    if (cursor && !cursor->ended) {
        cursor_free(cursor);
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = true };
    return NULL;
}
static const char *query_row_get(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2);
    (void)count;
    const pin_query *row = args[0].ptr;
    int index = args[1].i;
    if (!row || row->kind != PIN_QUERY_ROW || index < 0 || index >= row->count ||
        !same_type(
            row->module,
            args[0],
            row->module->plan.descriptions[row->description].row_type
        )) {
        return "Query row field is unavailable";
    }
    if (!same_type(row->module, *out, row->values[index])) {
        return "Query row field type differs from its declaration";
    }
    return pin_value_snapshot_to_arena(storage, out, &row->values[index])
             ? "Cannot read query row field"
             : NULL;
}
static const char *query_current_row(arena *, const pin_value *args, int count, pin_value *out)
{
    assert(count == 1);
    (void)count;
    const behavior_function_context *context = (void *)(uintptr_t)args[0].u64;
    if (!context->query_row) {
        return "Scheduled query row is unavailable";
    }
    *out = *context->query_row;
    return NULL;
}
const node_host_call EXECUTION_query_make_call = { .value = make_query,
                                                   .variadic = true };
const node_host_call EXECUTION_query_begin_call = { .value = begin_query };
const node_host_call EXECUTION_query_next_call = { .context = next_query };
const node_host_call EXECUTION_query_end_call = { .value = end_query };
const node_host_call EXECUTION_query_row_get_call = { .value = query_row_get };
const node_host_call EXECUTION_query_current_row_call = { .value = query_current_row };

static const char *query_test(arena *, const pin_value *args, int count, pin_value *out)
{
    assert(count == 2);
    (void)count;
    behavior_function_context *context = (void *)(uintptr_t)args[0].u64;
    if (!context->query_test) {
        return "Changed query tests require a scheduled system row";
    }
    int result = context->query_test(context, args[1].i);
    if (result < 0) {
        return "Query term evaluation failed";
    }
    *out = (pin_value){ .type = PIN_BOOL, .b = result > 0 };
    return NULL;
}
const node_host_call EXECUTION_query_test_call = { .value = query_test };

bool execution_query_cursor_value(pin_value value, pin_value *out)
{
    const query_cursor *cursor = (void *)(uintptr_t)value.u64;
    if (!cursor || cursor->ended) {
        return false;
    }
    *out = (pin_value){ .type = PIN_QUERY, .ptr = cursor->description };
    return true;
}

static const char *component_snapshot(arena *storage, const pin_value *args, int count, pin_value *out)
{
    assert(count == 6);
    (void)count;
    const behavior_function_context *context = (void *)(uintptr_t)args[0].u64;
    if (!context->world) {
        return "Component snapshot requires an execution world";
    }
    const ecs_execution_column
        *column = ecs_execution_find_column(context->world, args[2].u64);
    if (!column || !column->type || args[3].type != PIN_STRUCT ||
        args[3].container != PIN_CONTAINER_SINGLE || !args[3].type_name ||
        strcmp(args[3].type_name, column->type->name) || args[4].u64 != column->layout ||
        args[5].u64 != column->stride || args[5].u64 != column->type->size) {
        return "Component snapshot differs from its declared layout";
    }
    pin_value value = args[3];
    value.ptr = NULL;
    uint32_t entity = ecs_execution_entity(context->world, args[1].u64);
    if (ecs_execution_present(column, entity)) {
        value.ptr = (char *)column->data + (size_t)entity * column->stride;
        if (!value.record) {
            value.schema = pin_schema_create(storage, column->type, (type_resolver){ 0 });
            if (!value.schema) {
                return "Cannot retain component snapshot schema";
            }
        }
    }
    return pin_value_snapshot_to_arena(storage, out, &value)
             ? "Cannot retain component snapshot"
             : NULL;
}
const node_host_call EXECUTION_component_snapshot_call = { .value = component_snapshot };
