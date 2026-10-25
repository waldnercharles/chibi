#include "execution/prepared.h"
#include "runtime/arrays_definition.h"
#include "runtime/native_prepared.h"
#include "runtime/text_definition.h"
#include "support/sha256.h"
#include "toolchain/compiler_artifact.h"
#include "toolchain/compiler_host.h"
#include "toolchain/compiler_json.h"
#include "toolchain/compiler_syntax.h"
#include "types/catalog.h"
#include "types/value_wire.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* This private host admits no game/native reflected type packs. */
int type_catalog_pack_count()
{
    return 0;
}
type_catalog_pack type_catalog_pack_at(int index)
{
    (void)index;
    assert(!"compiler host has no native type packs");
    return (type_catalog_pack){ 0 };
}

#include "capabilities/catalog.h"
#include "execution/definition.h"
#include "execution/function_builder.h"
#include "execution/module_internal.h"
#include "execution/status.h"
#include "support/array.h"
#include "types/array.h"
#include "types/map.h"
#include "types/record.h"
#include "types/text.h"
#include "types/union.h"
#define PICO_UNIT_IMPLEMENTATION
#include "third_party/pico_headers/pico_unit.h"
static arena input;
static const execution_module *writer_module;
enum
{
    TABLES = 23,
    BUDGET = 200000000
};
static const pin_record *loaded_record(const char *name)
{
    char key[256], expected[256];
    snprintf(key, sizeof key, "project:prepared_writer:13:%s", name);
    snprintf(expected, sizeof expected, "prepared_writer.%s", name);
    uint64_t id = script_catalog_id("package-symbol", key);
    for (int i = 0; i < writer_module->plan.type_count; i++) {
        const execution_type_requirement *t = &writer_module->plan.types[i];
        if (t->id == id && t->record && !strcmp(t->record->type.name, expected)) {
            return t->record;
        }
    }
    fprintf(stderr, "missing decoded writer record %s id=%" PRIx64 "\n", name, id);
    return NULL;
}
static void box(arena *storage, const pin_value *value, int depth);
static void box_record(arena *storage, const pin_value *value, int depth)
{
    const type_desc *type = pin_value_type(value);
    for (int i = 0; type && i < type->field_count; i++) {
        pin_value field;
        {
            const char *error = pin_read_field(storage, value->ptr, &type->fields[i], &field);
            if (error) {
                abort();
            }
        };
        if (field.type == PIN_UNION) {
            box(storage, &field, depth + 1);
        }
    }
}
static void box(arena *storage, const pin_value *value, int depth)
{
    if (!value->ptr || depth > 64) {
        return;
    }
    if (value->container == PIN_CONTAINER_ARRAY || value->container == PIN_CONTAINER_SET) {
        pin_array *array = value->ptr;
        if (array->stride) {
            pin_value *items = array->count
                                 ? arena_alloc(storage, (size_t)array->count * sizeof *items)
                                 : NULL;
            for (int i = 0; i < array->count; i++) {
                items[i] = pin_array_get(array, i);
            }
            *array = (pin_array){ .element_type = array->element_type,
                                  .type_name = array->type_name,
                                  .record = array->record,
                                  .count = array->count,
                                  .capacity = array->count,
                                  .items = items };
        }
        for (int i = 0; i < array->count; i++) {
            box(storage, &array->items[i], depth + 1);
        }
        return;
    }
    if (value->container == PIN_CONTAINER_MAP) {
        pin_map *map = value->ptr;
        for (int i = 0; i < map->count; i++) {
            box(storage, &map->entries[i].key, depth + 1);
            box(storage, &map->entries[i].value, depth + 1);
        }
        return;
    }
    if (value->type == PIN_UNION) {
        pin_union *tagged = value->ptr;
        for (int i = 0; i < tagged->count; i++) {
            box(storage, &tagged->values[i], depth + 1);
        }
        return;
    }
    if (value->type == PIN_STRUCT && value->record) {
        box_record(storage, value, depth);
    }
}
static int adapter_union(arena *storage, uint64_t tag, const pin_value *values, int count, pin_value *out)
{
    if (pin_union_value(storage, tag, values, count, out)) {
        return -1;
    }
    box(storage, out, 0);
    return 0;
}
typedef struct
{
    pin_value *wide, *values, *ints, *requirements, *fields, *events,
        *event_fields, *imports, *functions, *spans, *roots, *instructions,
        *locals, *parameters, *frames, *sources, *results, *data, *hosts,
        *calls, *references, *components, *points, *watches, *states, *entries;
    const behavior_function **order;
    const execution_module_plan *plan;
    int backend;
} model;

static pin_value record(const model *m, const char *name)
{
    (void)m;
    const pin_record *shape = loaded_record(name);
    assert(shape);
    pin_value v = { .type = PIN_STRUCT,
                    .record = shape,
                    .type_name = shape->type.name,
                    .ptr = arena_alloc(&input, shape->type.size) };
    memset(v.ptr, 0, shape->type.size);
    return v;
}
static void put(pin_value *v, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(v), name);
    assert(field && field->type == value.type);
    {
        const char *error = pin_write_field(&input, v->ptr, field, value);
        if (error) {
            abort();
        }
    };
}
static void put_int(pin_value *v, const char *name, int n)
{
    put(v, name, (pin_value){ .type = PIN_I32, .i = n });
}
static void put_bool(pin_value *v, const char *name, bool b)
{
    put(v, name, (pin_value){ .type = PIN_BOOL, .b = b });
}
static void put_text(pin_value *v, const char *name, const char *flag, const char *text)
{
    size_t size = text ? strlen(text) + 1 : 1;
    char *copy = arena_alloc(&input, size);
    memcpy(copy, text ? text : "", size);
    put(v,
        name,
        (pin_value){ .type = PIN_STR,
                     .text = &(const pin_string){ .bytes = copy,
                                                  .length = (uint32_t)strlen(copy) } });
    if (flag) {
        put_bool(v, flag, text != NULL);
    }
}
static int wide(model *m, uint64_t n)
{
    apush(m->wide, ((pin_value){ .type = PIN_U64, .u64 = n }));
    return acount(m->wide) - 1;
}
static int value_rows(model *m, const pin_value *values, int count);
static uint64_t scalar_bits(pin_value v)
{
    if (v.type == PIN_F32) {
        uint32_t bits;
        memcpy(&bits, &v.f, sizeof bits);
        return bits;
    }
    if (v.type == PIN_F64) {
        uint64_t bits;
        memcpy(&bits, &v.d, sizeof bits);
        return bits;
    }
    return v.type == PIN_FLOW ? 0 : behavior_scalar_bits(v);
}
static pin_value value_row(model *m, const pin_value *v)
{
    pin_value row = record(m, "Value");
    put_int(&row, "tag", v->type);
    put_int(&row, "container", v->container);
    put_text(&row, "type_name", "has_type_name", v->type_name);
    put_bool(&row, "authored", v->record != NULL);
    put_int(&row, "wide", 0);
    put_text(&row, "text", "has_text", NULL);
    if (v->container != PIN_CONTAINER_SINGLE) {
        const pin_array *array = v->ptr;
        put_bool(&row, "present", array != NULL);
        put_int(&row, "count", array ? array->count : 0);
        pin_value *items = NULL;
        for (int i = 0; array && i < array->count; i++) {
            apush(items, pin_array_get(array, i));
        }
        put_int(&row, "first", array ? value_rows(m, items, array->count) : 0);
        afree(items);
        return row;
    }
    if (v->type == PIN_STRUCT) {
        const type_desc *type = pin_value_type(v);
        assert(type);
        put_int(&row, "wide", wide(m, type_layout_hash(type)));
        put_bool(&row, "present", v->ptr != NULL);
        /* A native type's absent header carries no fields. */
        put_int(&row, "count", v->ptr || v->record ? type->field_count : 0);
        if (v->ptr) {
            pin_value *members = NULL;
            for (int i = 0; i < type->field_count; i++) {
                pin_value member = { 0 };
                {
                    const char *error = pin_read_field(&input, v->ptr, &type->fields[i], &member);
                    if (error) {
                        abort();
                    }
                };
                apush(members, member);
            }
            put_int(&row, "first", value_rows(m, members, type->field_count));
            afree(members);
        }
        return row;
    }
    if (v->type == PIN_UNION) {
        const pin_union *u = v->ptr;
        put_bool(&row, "present", u != NULL);
        if (u) {
            put_int(&row, "wide", wide(m, u->tag));
            put_int(&row, "count", u->count);
            put_int(&row, "first", value_rows(m, u->values, u->count));
        }
        return row;
    }
    if (v->type == PIN_NAME) {
        put_text(&row, "text", "has_text", v->str);
        return row;
    }
    if (v->type == PIN_STR) {
        pin_string text = pin_text(*v);
        pin_value value;
        const char *error = pin_value_text(&input, &value, text.bytes, text.length);
        if (error) {
            abort();
        }
        put(&row, "text", value);
        put_bool(&row, "has_text", true);
        return row;
    }
    put_int(&row, "wide", wide(m, scalar_bits(*v)));
    return row;
}
/* Siblings occupy one contiguous range; their own children follow it. */
static int value_rows(model *m, const pin_value *values, int count)
{
    int first = acount(m->values);
    for (int i = 0; i < count; i++) {
        apush(m->values, (pin_value){ 0 });
    }
    for (int i = 0; i < count; i++) {
        pin_value row = value_row(m, &values[i]);
        m->values[first + i] = row;
    }
    return first;
}
static int value(model *m, const pin_value *v)
{
    return value_rows(m, v, 1);
}
static int own_index(const model *m, const behavior_function *f)
{
    for (int i = 0; i < acount(m->order); i++) {
        if (m->order[i] == f) {
            return i;
        }
    }
    return -1;
}
static int reference(model *m, const behavior_function *f)
{
    pin_value row = record(m, "Reference");
    int imported = -1, own = -1, role = 0;
    uint64_t id = 0;
    if (f) {
        own = own_index(m, f);
        for (int i = 0; i < m->plan->import_count && own < 0; i++) {
            const execution_module_plan *p = &m->plan->imports[i]->plan;
            for (int j = 0; j < p->function_count; j++) {
                bool body = p->functions[j].body == f,
                     subscription = p->functions[j].subscription == f;
                if (body || subscription) {
                    imported = i;
                    id = p->functions[j].id;
                    role = subscription;
                    own = 0;
                    break;
                }
            }
        }
        assert(own >= 0);
    }
    put_int(&row, "imported", imported);
    put_int(&row, "own", own);
    put_int(&row, "function", imported >= 0 ? wide(m, id) : 0);
    put_int(&row, "role", role);
    apush(m->references, row);
    return acount(m->references) - 1;
}
static int source(model *m, const behavior_function_source *s)
{
    pin_value row = record(m, "Source");
    put_int(&row, "graph", wide(m, s->graph_id));
    put_int(&row, "call", wide(m, s->call_id));
    put_int(&row, "parent", s->parent);
    put_text(&row, "asset", "has_asset", s->asset_name);
    put_int(&row, "start", s->start);
    put_int(&row, "end", s->end);
    put_int(&row, "line", s->line);
    put_int(&row, "column", s->column);
    apush(m->sources, row);
    return acount(m->sources) - 1;
}
static int point(model *m, const behavior_source_point *p)
{
    pin_value row = record(m, "Point");
    put_int(&row, "occurrence", wide(m, p->occurrence));
    put_int(&row, "meaning", wide(m, p->meaning));
    put_int(&row, "start", p->start);
    put_int(&row, "end", p->end);
    put_int(&row, "line", p->line);
    put_int(&row, "column", p->column);
    put_int(&row, "kind", p->kind);
    put_bool(&row, "completion", p->completion);
    apush(m->points, row);
    return acount(m->points) - 1;
}
static void span(model *m, int at, int first, int count)
{
    put_int(&m->spans[at], "first", first);
    put_int(&m->spans[at], "count", count);
}
static int ints(model *m, const int *values, int count)
{
    int first = acount(m->ints);
    for (int i = 0; i < count; i++) {
        apush(m->ints, ((pin_value){ .type = PIN_I32, .i = values[i] }));
    }
    return first;
}
static bool function(model *m, const behavior_function *f)
{
    REQUIRE(!acount(f->queries) && !acount(f->commands) && !acount(f->state_selections));
    REQUIRE(!acount(f->points) && !acount(f->watches) && !acount(f->edges));
    int base = acount(m->spans);
    for (int i = 0; i < TABLES; i++) {
        apush(m->spans, record(m, "Span"));
    }
    pin_value row = record(m, "Function");
    put_int(&row, "argument_count", f->argument_count);
    put_int(&row, "result_count", f->result_count);
    put_int(&row, "variable_count", f->variable_count);
    put_int(&row, "root_steps", (int)f->root_steps);
    put_int(&row, "child_budget", (int)f->child_budget);
    put_int(&row, "depth_bound", (int)f->depth_limit);
    put_int(&row, "pure_depth_limit", (int)f->pure_depth_limit);
    put_bool(&row, "breakpoints", f->breakpoints);
    put_bool(&row, "batchable", f->batchable);
    put_bool(&row, "managed", f->managed);
    put_bool(&row, "custom_lowering", f->custom_lowering);
    put_bool(&row, "checkpoints", f->checkpoints);
    put_bool(&row, "pin_watches", f->pin_watches);
    put_int(&row, "watch_size", (int)f->watch_size);
    put_bool(&row, "component_bindings", f->component_bindings);
    put_bool(&row, "component_access", f->component_access);
    put_int(&row, "spans", base);
    span(
        m,
        base + 0,
        value_rows(m, f->argument_defaults, acount(f->argument_defaults)),
        acount(f->argument_defaults)
    );
    int *kinds = NULL;
    for (int i = 0; i < acount(f->argument_kinds); i++) {
        apush(kinds, (int)f->argument_kinds[i]);
    }
    span(m, base + 1, ints(m, kinds, acount(kinds)), acount(kinds));
    asetlen(kinds, 0);
    span(m, base + 2, value_rows(m, f->result_defaults, acount(f->result_defaults)), acount(f->result_defaults));
    for (int i = 0; i < acount(f->result_kinds); i++) {
        apush(kinds, (int)f->result_kinds[i]);
    }
    span(m, base + 3, ints(m, kinds, acount(kinds)), acount(kinds));
    asetlen(kinds, 0);
    span(
        m,
        base + 4,
        value_rows(m, f->variable_defaults, acount(f->variable_defaults)),
        acount(f->variable_defaults)
    );
    for (int i = 0; i < acount(f->slots); i++) {
        apush(kinds, (int)f->slots[i]);
    }
    span(m, base + 5, ints(m, kinds, acount(kinds)), acount(kinds));
    asetlen(kinds, 0);
    for (int i = 0; i < acount(f->slot_kinds); i++) {
        apush(kinds, (int)f->slot_kinds[i]);
    }
    span(m, base + 6, ints(m, kinds, acount(kinds)), acount(kinds));
    afree(kinds);
    int first = acount(m->roots);
    for (int i = 0; i < acount(f->value_roots); i++) {
        pin_value root = record(m, "Root");
        put_int(&root, "bank", f->value_roots[i].bank);
        put_int(&root, "index", f->value_roots[i].index);
        put_int(&root, "shape", value(m, &f->value_roots[i].shape));
        apush(m->roots, root);
    }
    span(m, base + 7, first, acount(f->value_roots));
    first = acount(m->instructions);
    for (int i = 0; i < acount(f->instructions); i++) {
        const behavior_function_instruction *v = &f->instructions[i];
        pin_value code = record(m, "Instruction");
        const int fields[] = { v->op,    v->type,   v->output, v->left,
                               v->right, v->target, v->frame,  v->source,
                               v->stack, v->pin };
        const char *names[] = { "op",     "type",  "output", "left",  "right",
                                "target", "frame", "source", "stack", "pin" };
        for (int k = 0; k < 10; k++) {
            put_int(&code, names[k], fields[k]);
        }
        put_int(&code, "offset", (int)v->offset);
        put_bool(&code, "indirect", v->indirect);
        put_int(&code, "component_index", v->component);
        put_int(&code, "node", wide(m, v->node_id));
        put_int(&code, "value", value(m, &v->value));
        put_int(&code, "layout", wide(m, v->layout));
        put_text(&code, "message", "has_message", v->message);
        apush(m->instructions, code);
    }
    span(m, base + 8, first, acount(f->instructions));
    first = acount(m->locals);
    for (int i = 0; i < acount(f->locals); i++) {
        const behavior_function_local *v = &f->locals[i];
        pin_value local = record(m, "Local");
        put_int(&local, "kind", v->kind);
        put_int(&local, "initial", value(m, &v->initial));
        put_int(&local, "frame", v->frame);
        put_int(&local, "node", wide(m, v->node_id));
        put_int(&local, "pin", v->pin);
        put_int(&local, "offset", (int)v->offset);
        put_bool(&local, "parameter", v->parameter);
        put_bool(&local, "array_default", v->array_default);
        put_int(&local, "data_index", v->data_index);
        apush(m->locals, local);
    }
    span(m, base + 9, first, acount(f->locals));
    first = acount(m->parameters);
    for (int i = 0; i < acount(f->parameters); i++) {
        pin_value parameter = record(m, "Parameter");
        put_int(&parameter, "initial", value(m, &f->parameters[i].initial));
        put_int(&parameter, "frame", f->parameters[i].frame);
        put_int(&parameter, "pin", f->parameters[i].pin);
        apush(m->parameters, parameter);
    }
    span(m, base + 10, first, acount(f->parameters));
    first = acount(m->frames);
    for (int i = 0; i < acount(f->frames); i++) {
        const behavior_function_frame *v = &f->frames[i];
        pin_value frame = record(m, "Frame");
        put_int(&frame, "source", source(m, &v->source));
        put_int(&frame, "depth", v->depth);
        put_int(&frame, "resume", v->resume);
        put_int(&frame, "source_index", v->source_index);
        put_int(&frame, "stack", v->stack);
        apush(m->frames, frame);
    }
    span(m, base + 11, first, acount(f->frames));
    first = acount(m->sources);
    for (int i = 0; i < acount(f->sources); i++) {
        source(m, &f->sources[i]);
    }
    span(m, base + 12, first, acount(f->sources));
    first = acount(m->sources);
    for (int i = 0; i < acount(f->stacks); i++) {
        source(m, &f->stacks[i]);
    }
    span(m, base + 13, first, acount(f->stacks));
    first = acount(m->results);
    for (int i = 0; i < acount(f->results); i++) {
        pin_value result = record(m, "ResultLayout");
        put_int(&result, "index", f->results[i].index);
        put_text(&result, "name", NULL, f->results[i].type->name);
        apush(m->results, result);
    }
    span(m, base + 14, first, acount(f->results));
    first = acount(m->data);
    for (int i = 0; i < acount(f->data_values); i++) {
        pin_value data = record(m, "Data");
        put_int(&data, "kind", f->data_values[i].kind);
        put_int(&data, "initial", value(m, &f->data_values[i].initial));
        put_int(&data, "size", (int)f->data_values[i].size);
        apush(m->data, data);
    }
    span(m, base + 15, first, acount(f->data_values));
    first = acount(m->hosts);
    for (int i = 0; i < acount(f->host_calls); i++) {
        const behavior_function_host_call *v = &f->host_calls[i];
        pin_value host = record(m, "Host");
        put_int(&host, "capability", wide(m, v->capability_id));
        put_int(&host, "abi", wide(m, v->capability_abi));
        put_int(&host, "first", v->first);
        put_int(&host, "result_first", v->result_first);
        put_int(&host, "result_count", v->result_count);
        int outcomes = acount(m->wide);
        for (int k = 0; k < v->outcome_count; k++) {
            wide(m, v->outcomes[k]);
        }
        put_int(&host, "outcome_first", outcomes);
        put_int(&host, "outcome_count", v->outcome_count);
        apush(m->hosts, host);
    }
    span(m, base + 16, first, acount(f->host_calls));
    first = acount(m->calls);
    for (int i = 0; i < acount(f->calls); i++) {
        const behavior_function_invocation *v = &f->calls[i];
        pin_value call = record(m, "Call");
        put_int(&call, "function", reference(m, v->function));
        put_int(&call, "first", v->first);
        put_int(&call, "count", v->count);
        put_int(&call, "result_first", v->result_first);
        put_int(&call, "result_count", v->result_count);
        put_bool(&call, "task", v->task);
        apush(m->calls, call);
    }
    span(m, base + 17, first, acount(f->calls));
    int *definitions = NULL;
    for (int i = 0; i < acount(f->definitions); i++) {
        apush(definitions, reference(m, f->definitions[i]));
    }
    span(m, base + 18, ints(m, definitions, acount(definitions)), acount(definitions));
    afree(definitions);
    first = acount(m->components);
    for (int i = 0; i < acount(f->components); i++) {
        pin_value component = record(m, "Component");
        put_int(&component, "id", wide(m, f->components[i].id));
        put_bool(&component, "binding", f->components[i].binding);
        apush(m->components, component);
    }
    span(m, base + 19, first, acount(f->components));
    int *writes = NULL;
    for (int i = 0; i < acount(f->component_writes); i++) {
        apush(writes, wide(m, f->component_writes[i]));
    }
    span(m, base + 20, ints(m, writes, acount(writes)), acount(writes));
    afree(writes);
    first = acount(m->points);
    for (int i = 0; i < acount(f->source_points); i++) {
        point(m, &f->source_points[i]);
    }
    span(m, base + 21, first, acount(f->source_points));
    first = acount(m->watches);
    for (int i = 0; i < acount(f->source_watches); i++) {
        const behavior_source_watch *v = &f->source_watches[i];
        pin_value watch = record(m, "Watch");
        put_int(&watch, "origin", point(m, &v->origin));
        put_int(&watch, "initial", value(m, &v->initial));
        put_int(&watch, "ordinal", v->ordinal);
        put_bool(&watch, "output", v->output);
        put_bool(&watch, "reference", v->reference);
        apush(m->watches, watch);
    }
    span(m, base + 22, first, acount(f->source_watches));
    apush(m->functions, row);
    return true;
}
/* Definitions form a tree, as the native writer requires, in its order. */
static void order(model *m, const behavior_function *f)
{
    if (!f) {
        return;
    }
    assert(own_index(m, f) < 0);
    apush(m->order, f);
    for (int i = 0; i < acount(f->definitions); i++) {
        order(m, f->definitions[i]);
    }
}
static pin_value rows(model *m, const char *name, pin_type type, pin_value *items)
{
    (void)m;
    const pin_record *shape = name ? loaded_record(name) : NULL;
    pin_array *array = pin_array_snapshot_allocate(
        &input,
        type,
        shape ? shape->type.name : NULL,
        acount(items)
    );
    assert(array);
    array->record = shape;
    for (int i = 0; i < acount(items); i++) {
        array->items[i] = items[i];
    }
    return (pin_value){ .type = type,
                        .container = PIN_CONTAINER_ARRAY,
                        .record = shape,
                        .type_name = shape ? shape->type.name : NULL,
                        .ptr = array };
}
static uint64_t tag(int b, const char *name, const char *variant)
{
    (void)b;
    char key[256];
    snprintf(key, sizeof key, "project:prepared_writer:13:%s", name);
    uint64_t nominal = script_catalog_id("package-symbol", key);
    snprintf(key, sizeof key, "%" PRIu64 ":%s", nominal, variant);
    return script_catalog_id("union-variant", key);
}
/* The header's target ABI hashes C layouts, so the authored writer takes it
 * as an input: the fourth varint of the native encoding. */
static uint64_t header_abi(const unsigned char *bytes)
{
    size_t at = 0;
    uint64_t value = 0;
    for (int word = 0; word < 4; word++) {
        value = 0;
        for (int shift = 0;; shift += 7) {
            value |= (uint64_t)(bytes[at] & 0x7f) << shift;
            if (!(bytes[at++] & 0x80)) {
                break;
            }
        }
    }
    return value;
}
static bool marshal(model *m, const execution_module *module, uint64_t abi, pin_value *out)
{
    const execution_module_plan *p = &module->plan;
    m->plan = p;
    wide(m, 0);
    for (int i = 0; i < p->function_count; i++) {
        order(m, p->functions[i].body);
        order(m, p->functions[i].subscription);
    }
    REQUIRE(!p->binding_count && !p->query_count);
    for (int i = 0; i < p->type_count; i++) {
        const execution_type_requirement *v = &p->types[i];
        pin_value row = record(m, "Requirement");
        put_int(&row, "id", wide(m, v->id));
        put_int(&row, "layout", wide(m, v->layout));
        put_int(&row, "storage", v->storage);
        put_text(&row, "name", NULL, v->name);
        put_int(&row, "size", (int)v->size);
        put_bool(&row, "authored", v->record != NULL);
        put_int(&row, "first", acount(m->fields));
        put_int(&row, "count", v->record ? v->record->type.field_count : 0);
        for (int f = 0; v->record && f < v->record->type.field_count; f++) {
            pin_value field = record(m, "Field");
            put_text(&field, "name", NULL, v->record->type.fields[f].name);
            put_int(&field, "kind", v->record->type.fields[f].type);
            put_text(
                &field,
                "type_name",
                "has_type_name",
                v->record->type.fields[f].type_name
            );
            put_text(&field, "unit", "has_unit", v->record->type.fields[f].unit);
            apush(m->fields, field);
        }
        apush(m->requirements, row);
    }
    for (int i = 0; i < p->event_count; i++) {
        const execution_module_event_declaration *v = &p->events[i];
        pin_value row = record(m, "Event");
        put_int(&row, "id", wide(m, v->id));
        put_int(&row, "abi", wide(m, v->abi));
        put_text(&row, "name", NULL, v->name);
        put_int(&row, "first", acount(m->event_fields));
        put_int(&row, "count", v->field_count);
        for (int f = 0; f < v->field_count; f++) {
            pin_value field = record(m, "EventField");
            put_text(&field, "name", NULL, v->fields[f].name);
            put_int(&field, "nominal", wide(m, v->fields[f].nominal));
            put_bool(&field, "has_default", v->fields[f].has_default);
            put_bool(&field, "default_owner", v->fields[f].default_owner);
            put_int(&field, "initial", value(m, &v->fields[f].initial));
            apush(m->event_fields, field);
        }
        apush(m->events, row);
    }
    for (int i = 0; i < p->import_count; i++) {
        size_t length = 0;
        uint64_t fingerprint = 0;
        char error[192];
        REQUIRE(
            execution_prepared_measure(p->imports[i], &length, &fingerprint, error, sizeof error)
        );
        pin_value row = record(m, "Import");
        put_int(&row, "id", wide(m, p->imports[i]->plan.id));
        put_int(&row, "fingerprint", wide(m, fingerprint ? fingerprint : 1));
        apush(m->imports, row);
    }
    for (int i = 0; i < acount(m->order); i++) {
        REQUIRE(function(m, m->order[i]));
    }
    for (int i = 0; i < p->state_count; i++) {
        pin_value row = record(m, "State");
        put_int(&row, "id", wide(m, p->states[i].id));
        put_int(&row, "initial", value(m, &p->states[i].initial));
        put_bool(&row, "shared", p->states[i].shared);
        put_bool(&row, "contains_tasks", p->states[i].contains_tasks);
        apush(m->states, row);
    }
    for (int i = 0; i < p->function_count; i++) {
        const execution_module_function *v = &p->functions[i];
        REQUIRE(!v->action && !v->parent && !v->outcome);
        REQUIRE(
            !v->observer.component && !v->observer.layout &&
            !v->observer.field && !v->observer.offset
        );
        REQUIRE(v->observer.type.type == PIN_FLOW && !v->observer.argument);
        REQUIRE(!v->local_capture_count && !v->capture_count);
        pin_value row = record(m, "Entry");
        put_bool(&row, "continuation_wait", v->continuation_wait);
        put_int(&row, "id", wide(m, v->id));
        put_int(&row, "kind", v->kind);
        put_text(&row, "entry_name", "has_entry", v->entry);
        put_int(&row, "event_id", wide(m, v->event));
        put_int(&row, "event_abi", wide(m, v->event_abi));
        put_text(&row, "event_name", "has_event_name", v->event_name);
        put_int(&row, "effects", wide(m, v->effects));
        put_int(&row, "body", reference(m, v->body));
        put_int(&row, "subscription", reference(m, v->subscription));
        apush(m->entries, row);
    }
    pin_value payload[] = {
        { .type = PIN_U64, .u64 = p->id },
        { .type = PIN_U64, .u64 = p->owner_type },
        { .type = PIN_U64, .u64 = abi },
        { .type = PIN_I32, .i = (int)sizeof(void *) },
        rows(m, NULL, PIN_U64, m->wide),
        rows(m, "Value", PIN_STRUCT, m->values),
        rows(m, NULL, PIN_I32, m->ints),
        rows(m, "Requirement", PIN_STRUCT, m->requirements),
        rows(m, "Field", PIN_STRUCT, m->fields),
        rows(m, "Event", PIN_STRUCT, m->events),
        rows(m, "EventField", PIN_STRUCT, m->event_fields),
        rows(m, "Import", PIN_STRUCT, m->imports),
        rows(m, "Function", PIN_STRUCT, m->functions),
        rows(m, "Span", PIN_STRUCT, m->spans),
        rows(m, "Root", PIN_STRUCT, m->roots),
        rows(m, "Instruction", PIN_STRUCT, m->instructions),
        rows(m, "Local", PIN_STRUCT, m->locals),
        rows(m, "Parameter", PIN_STRUCT, m->parameters),
        rows(m, "Frame", PIN_STRUCT, m->frames),
        rows(m, "Source", PIN_STRUCT, m->sources),
        rows(m, "ResultLayout", PIN_STRUCT, m->results),
        rows(m, "Data", PIN_STRUCT, m->data),
        rows(m, "Host", PIN_STRUCT, m->hosts),
        rows(m, "Call", PIN_STRUCT, m->calls),
        rows(m, "Reference", PIN_STRUCT, m->references),
        rows(m, "Component", PIN_STRUCT, m->components),
        rows(m, "Point", PIN_STRUCT, m->points),
        rows(m, "Watch", PIN_STRUCT, m->watches),
        rows(m, "State", PIN_STRUCT, m->states),
        rows(m, "Entry", PIN_STRUCT, m->entries),
    };
    uint64_t tables = tag(m->backend, "Module", "Tables");
    REQUIRE(tables);
    REQUIRE(
        !adapter_union(&input, tables, payload, (int)(sizeof payload / sizeof *payload), out)
    );
    out->type_name = "Module";
    return true;
}
static void model_free(model *m)
{
    afree(m->wide);
    afree(m->values);
    afree(m->ints);
    afree(m->requirements);
    afree(m->fields);
    afree(m->events);
    afree(m->event_fields);
    afree(m->imports);
    afree(m->functions);
    afree(m->spans);
    afree(m->roots);
    afree(m->instructions);
    afree(m->locals);
    afree(m->parameters);
    afree(m->frames);
    afree(m->sources);
    afree(m->results);
    afree(m->data);
    afree(m->hosts);
    afree(m->calls);
    afree(m->references);
    afree(m->components);
    afree(m->points);
    afree(m->watches);
    afree(m->states);
    afree(m->entries);
    afree(m->order);
}
