#include "prepared_writer_chibi.h"

#include "compiler_fixture.h"
#include "execution/definition.h"
#include "execution/module.h"
#include "execution/module_internal.h"
#include "execution/prepared.h"
#include "language/lower.h"
#include "package_core_source.h"
#include "package_paths_chibi_source.h"
#include "prepared_writer_chibi_source.h"
#include "support/array.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "toolchain/compiler_artifact.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/text.h"
#include "types/type.h"
#include "types/union.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The authored prepared writer must reproduce execution_prepared_write byte
 * for byte. One fixture prepares the writer with a fixed corpus; every corpus
 * module, and a behavior's module, is marshalled into the authored model and
 * written on bytecode and the compiled backend. Modules above 512 KiB, the
 * writer's own 1.3 MiB and core:strings' 0.8 MiB, take 38 s and 22 s on
 * bytecode under ASan, so they are written on the compiled backend only. */
static const char PACKAGE[] = "project:prepared_writer";
static const char SAMPLE
    [] = "package sample\n"
         "import strings \"core:strings\"\n"
         "Point :: struct { x: int; y: int; label: string; }\n"
         "Shape :: union { Circle(radius: int) | Path(points: Point[]) | Empty "
         "}\n"
         "Limit: int : 3;\n"
         "Area :: (shape: Shape) -> int {\n"
         "    switch shape {\n"
         "        case .Circle(radius): return radius * radius * Limit;\n"
         "        case .Path(points):\n"
         "            total: int = 0;\n"
         "            for (point in points) { total += point.x * point.y; }\n"
         "            return total;\n"
         "        case .Empty: return 0;\n"
         "    }\n"
         "}\n"
         "Build :: (count: int, label: string) -> Shape {\n"
         "    points: Point[] = [];\n"
         "    i: int = 0;\n"
         "    while (i < count) {\n"
         "        point: Point = {x: i, y: strings.ByteLength(label), label: "
         "label};\n"
         "        points = append(points, point);\n"
         "        i += 1;\n"
         "    }\n"
         "    if (count == 0) { return Shape.Empty; }\n"
         "    return Shape.Path(points);\n"
         "}\n"
         "Wide :: (seed: u64) -> u64 { return (seed ^ "
         "0x9e3779b97f4a7c15) * 3; }\n"
         "Ratio :: (a: f64, b: number) -> f64 { return a / b + 0.5; }\n";
static const char *const CORPUS[] = { "project:prepared_writer",
                                      "core:strings",
                                      "project:syntax_diagnostics",
                                      "project:package_paths",
                                      "project:sample" };
enum
{
    CORPUS_COUNT = sizeof CORPUS / sizeof *CORPUS,
    TABLES = 28,
    BUDGET = 200000000,
    BYTECODE_MODULE_LIMIT = 512 * 1024
};
/* A behavior with retained state, handlers of an event with and without a
 * payload, and a write to a field of the owner's component. */
static const char BEHAVIOR[] = "Counter :: behavior {\n"
                               "    steps: int = 0\n"
                               "    on Tick :: () {\n"
                               "        steps += 1\n"
                               "        self.Tally.value = steps\n"
                               "    }\n"
                               "    on Add :: (amount: int) {\n"
                               "        steps += amount\n"
                               "    }\n"
                               "}\n";
static const char QUERY_BEHAVIOR[] = "Counter :: behavior {\n"
                                     "    on Add :: (amount: int) {\n"
                                     "        matches := query(tally: ^Tally, "
                                     "if tally.value > amount)\n"
                                     "        for (row in matches) {\n"
                                     "            row.tally.value += amount\n"
                                     "            break\n"
                                     "        }\n"
                                     "    }\n"
                                     "}\n";
static const char QUERY_SYSTEM[] = "Count :: system(\n"
                                   "    tally: ^Tally,\n"
                                   "    if tally.value > 0,\n"
                                   "    amount := tally.value * 2,\n"
                                   ") { tally.value += amount }\n";
enum
{
    TALLY = 101,
    TALLY_COMPONENT = 901,
    TICK = 11,
    ADD = 12,
    OWNER = 30,
    ENTITY = 700
};
static const type_field tally_fields[] = {
    { .name = "value", .type = PIN_I32, .type_name = "int", .offset = 0, .size = sizeof(int32_t) },
};
static const type_desc tally_type = { .name = "Tally",
                                      .size = sizeof(int32_t),
                                      .fields = tally_fields,
                                      .field_count = 1 };
static compiler_fixture fixture;
static arena input;

typedef struct
{
    pin_value *wide, *values, *ints, *requirements, *fields, *events,
        *event_fields, *imports, *functions, *spans, *roots, *instructions,
        *locals, *parameters, *frames, *sources, *results, *data, *hosts,
        *calls, *references, *components, *points, *watches, *states, *entries,
        *queries, *descriptions;
    const behavior_function **order;
    const execution_module_plan *plan;
    int backend;
} model;

static void cleanup()
{
    compiler_fixture_free(&fixture);
    arena_free(&input);
}
static pin_value record(const model *m, const char *name)
{
    const pin_record *shape = compiler_fixture_record(&fixture, m->backend, PACKAGE, name, &input);
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
    if (v.type == PIN_QUERY) {
        assert(!v.ptr);
        return 0;
    }
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
    span(m, base + 22, first, acount(f->calls));
    int *definitions = NULL;
    for (int i = 0; i < acount(f->definitions); i++) {
        apush(definitions, reference(m, f->definitions[i]));
    }
    span(m, base + 23, ints(m, definitions, acount(definitions)), acount(definitions));
    afree(definitions);
    first = acount(m->components);
    for (int i = 0; i < acount(f->components); i++) {
        pin_value component = record(m, "Component");
        put_int(&component, "id", wide(m, f->components[i].id));
        put_bool(&component, "binding", f->components[i].binding);
        apush(m->components, component);
    }
    span(m, base + 24, first, acount(f->components));
    int *writes = NULL;
    for (int i = 0; i < acount(f->component_writes); i++) {
        apush(writes, wide(m, f->component_writes[i]));
    }
    span(m, base + 25, ints(m, writes, acount(writes)), acount(writes));
    afree(writes);
    first = acount(m->points);
    for (int i = 0; i < acount(f->source_points); i++) {
        point(m, &f->source_points[i]);
    }
    span(m, base + 26, first, acount(f->source_points));
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
    span(m, base + 27, first, acount(f->source_watches));
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
    const pin_record *shape = name
                                ? compiler_fixture_record(&fixture, m->backend, PACKAGE, name, &input)
                                : NULL;
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
    const authoring_package *p = compiler_fixture_package(&fixture, b, PACKAGE);
    const script_catalog_type *t = script_catalog_type_by_id(
        p->exports->metadata,
        compiler_fixture_export(&fixture, b, PACKAGE, name)
    );
    for (int i = 0; t && i < t->variant_count; i++) {
        if (!strcmp(t->variants[i].name, variant)) {
            return t->variants[i].tag;
        }
    }
    return 0;
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
    REQUIRE(!p->binding_count && !p->action_count);
    for (int i = 0; i < p->query_count; i++) {
        order(m, p->queries[i].predicate);
    }
    for (int i = 0; i < p->description_count; i++) {
        order(m, p->descriptions[i].evaluate);
    }
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
            const type_field *member = &v->record->type.fields[f];
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
            put_int(&field, "container", member->container);
            int child = -1;
            if (member->type == PIN_STRUCT) {
                for (int j = 0; j < p->type_count; j++) {
                    if (p->types[j].storage == PIN_STRUCT && member->type_name &&
                        !strcmp(p->types[j].name, member->type_name)) {
                        child = j;
                        break;
                    }
                }
                REQUIRE(child >= 0);
            }
            put_int(&field, "record", child);
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
    for (int i = 0; i < p->query_count; i++) {
        const execution_module_query *v = &p->queries[i];
        pin_value row = record(m, "Query");
        put_int(&row, "kind", v->kind);
        put_bool(&row, "required", v->required);
        put_int(&row, "component_id", wide(m, v->component));
        put_int(&row, "source_relationship", wide(m, v->source_relationship));
        put_int(&row, "source_traversal", v->source_traversal);
        put_int(&row, "source_variable", v->source_variable);
        put_int(&row, "target_variable", v->target_variable);
        put_int(&row, "predicate", v->predicate ? reference(m, v->predicate) : -1);
        put_int(&row, "first", ints(m, v->children, v->child_count));
        put_int(&row, "count", v->child_count);
        apush(m->queries, row);
    }
    for (int i = 0; i < p->description_count; i++) {
        const execution_module_query_description *v = &p->descriptions[i];
        pin_value row = record(m, "QueryDescription");
        put_int(&row, "query_root", v->query);
        put_int(&row, "query_variable_count", v->query_variable_count);
        put_int(&row, "row_type", value(m, &v->row_type));
        put_int(&row, "evaluate", reference(m, v->evaluate));
        apush(m->descriptions, row);
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
        rows(m, "Command", PIN_STRUCT, NULL),
        rows(m, "Call", PIN_STRUCT, m->calls),
        rows(m, "Reference", PIN_STRUCT, m->references),
        rows(m, "Component", PIN_STRUCT, m->components),
        rows(m, "Point", PIN_STRUCT, m->points),
        rows(m, "Watch", PIN_STRUCT, m->watches),
        rows(m, "State", PIN_STRUCT, m->states),
        rows(m, "Entry", PIN_STRUCT, m->entries),
        rows(m, "Query", PIN_STRUCT, m->queries),
        { .type = PIN_I32, .i = p->query },
        { .type = PIN_I32, .i = p->query_variable_count },
        { .type = PIN_I32, .i = p->query_filter },
        rows(m, "QueryDescription", PIN_STRUCT, m->descriptions),
        { .type = PIN_I32, .i = p->system_description },
    };
    uint64_t tables = tag(m->backend, "Module", "Tables");
    REQUIRE(tables);
    REQUIRE(
        !compiler_fixture_union(&input, tables, payload, (int)(sizeof payload / sizeof *payload), out)
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
    afree(m->queries);
    afree(m->descriptions);
    afree(m->order);
}
static size_t first_difference(const unsigned char *a, size_t a_size, const pin_array *b)
{
    size_t n = a_size < (size_t)b->count ? a_size : (size_t)b->count;
    for (size_t i = 0; i < n; i++) {
        if (a[i] != pin_array_get(b, i).u8) {
            return i;
        }
    }
    return n;
}
static bool prepare_corpus()
{
    const source_package_input inputs[] = {
        { "prepared_writer/Writer.chibi",
          PREPARED_WRITER_CHIBI_SOURCE,
          sizeof PREPARED_WRITER_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi",
          CORE_STRINGS_SOURCE,
          sizeof CORE_STRINGS_SOURCE - 1 },
        { "syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          sizeof SYNTAX_CHIBI_SOURCE - 1 },
        { "package_paths/Paths.chibi",
          PACKAGE_PATHS_CHIBI_SOURCE,
          sizeof PACKAGE_PATHS_CHIBI_SOURCE - 1 },
        { "sample/Sample.chibi", SAMPLE, sizeof SAMPLE - 1 },
    };
    return compiler_fixture_prepare(&fixture, inputs, (int)(sizeof inputs / sizeof *inputs));
}
/* Whether the writer on backend `b` writes `module` as the native writer
 * does. A module above the bytecode limit is written on the compiled backend
 * only. */
static bool writes_like_native(int b, const execution_module *module, const char *name)
{
    unsigned char *expected = NULL;
    size_t length = 0;
    char error[192];
    REQUIRE(execution_prepared_write(module, &expected, &length, error, sizeof error));
    if (!b && length > BYTECODE_MODULE_LIMIT) {
        free(expected);
        return true;
    }
    model m = { .backend = b };
    pin_value authored;
    REQUIRE(marshal(&m, module, header_abi(expected), &authored));
    arena detached = { 0 };
    pin_value written = { 0 };
    bool called = compiler_fixture_call(&fixture, b, PACKAGE, "Write", &authored, 1, BUDGET, &detached, &written);
    const pin_array *bytes = written.ptr;
    bool same = called && written.type == PIN_U8 && bytes &&
                (size_t)bytes->count == length;
    for (size_t i = 0; same && i < length; i++) {
        same = pin_array_get(bytes, i).u8 == expected[i];
    }
    if (!same) {
        fprintf(
            stderr,
            "prepared writer: backend=%d module=%s native=%zu "
            "authored=%d first difference=%zu\n",
            b,
            name,
            length,
            bytes ? bytes->count : -1,
            bytes ? first_difference(expected, length, bytes) : 0
        );
        size_t at = bytes ? first_difference(expected, length, bytes) : length;
        for (size_t i = at > 24 ? at - 24 : 0;
             bytes && i < at + 24 && i < length && i < (size_t)bytes->count;
             i++) {
            fprintf(stderr, " %02x/%02x", expected[i], pin_array_get(bytes, i).u8);
        }
        fprintf(stderr, "\n");
    }
    arena_free(&detached);
    model_free(&m);
    free(expected);
    return same;
}
static bool resolve_owner(void *, uint64_t id, behavior_function_instruction *out)
{
    if (id != OWNER) {
        return false;
    }
    *out = (behavior_function_instruction){ .op = BF_OWNER, .type = PIN_HANDLE };
    return true;
}
/* BEHAVIOR's module, lowered against a catalog of its component, events and
 * owner, while the component's type is registered. */
static execution_module *source_module(const char *source)
{
    static const script_catalog_field value = { .id = 811,
                                                .name = "value",
                                                .type = { .storage = PIN_I32 },
                                                .writable = true };
    static const script_catalog_parameter amount = { .name = "amount",
                                                     .type = { .storage = PIN_I32 } };
    static const script_catalog_capability events[] = {
        { .id = TICK, .name = "Tick", .kind = SCRIPT_CAPABILITY_EVENT, .completion_outcome = -1 },
        { .id = ADD,
          .name = "Add",
          .kind = SCRIPT_CAPABILITY_EVENT,
          .parameters = &amount,
          .parameter_count = 1,
          .completion_outcome = -1 },
    };
    static const script_catalog_global owner = { .id = OWNER,
                                                 .name = "self",
                                                 .type = { .storage = PIN_HANDLE,
                                                           .nominal = ENTITY } };
    const script_catalog_type tally = { .id = TALLY,
                                        .name = tally_type.name,
                                        .type = { .storage = PIN_STRUCT, .nominal = TALLY },
                                        .fields = &value,
                                        .field_count = 1,
                                        .size = tally_type.size,
                                        .layout = type_layout_hash(&tally_type),
                                        .component_id = TALLY_COMPONENT,
                                        .component = true };
    const script_catalog catalog = { .types = &tally,
                                     .type_count = 1,
                                     .capabilities = events,
                                     .capability_count = 2,
                                     .globals = &owner,
                                     .global_count = 1,
                                     .entity_type = ENTITY,
                                     .tick_event = TICK };
    script_syntax *syntax = script_parse(source);
    script_analysis *analysis = script_analyze(syntax, &catalog);
    const script_lower_bindings bindings = { .global = resolve_owner };
    execution_module_plan *plan = script_lower(analysis, &catalog, &bindings);
    for (int i = 0; i < analysis->diagnostic_count; i++) {
        fprintf(
            stderr,
            "prepared writer semantic: %s\n",
            analysis->diagnostics[i].message
        );
    }
    for (int i = 0; i < plan->diagnostic_count; i++) {
        fprintf(stderr, "prepared writer lowering: %s\n", plan->diagnostics[i].message);
    }
    char error[192] = { 0 };
    execution_module *module = analysis->diagnostic_count || plan->diagnostic_count
                                 ? NULL
                                 : execution_module_finish(&plan, BEHAVIOR_BYTECODE, error, sizeof error);
    if (!module) {
        fprintf(stderr, "prepared writer finish: %s\n", error);
    }
    execution_module_plan_free(plan);
    script_analysis_free(analysis);
    script_syntax_free(syntax);
    return module;
}
/* Whether a handler of `p` writes the owner's component. */
static bool writes_component(const execution_module_plan *p)
{
    for (int i = 0; i < p->function_count; i++) {
        const behavior_function *body = p->functions[i].body;
        if (acount(body->components) && acount(body->component_writes)) {
            return true;
        }
    }
    return false;
}
TEST_CASE(prepared_writer_reproduces_native_modules)
{
    REQUIRE(prepare_corpus());
    for (int b = 0; b < 2; b++) {
        for (int c = 0; c < CORPUS_COUNT; c++) {
            const authoring_package *package = compiler_fixture_package(&fixture, b, CORPUS[c]);
            REQUIRE(package && package->module);
            REQUIRE(writes_like_native(b, package->module, CORPUS[c]));
        }
    }
    /* The module's values name the component's type, so it stays registered
     * while both writers read them. */
    const type_desc *const reflected[] = { &tally_type };
    const type_catalog_pack pack = { .types = reflected, .type_count = 1 };
    type_catalog_set_project(&pack);
    execution_module *behavior = source_module(BEHAVIOR);
    const execution_module_plan *p = behavior ? &behavior->plan : NULL;
    bool covered = p && p->state_count == 1 && p->event_count == 2 &&
                   p->function_count == 2 && writes_component(p);
    bool written = covered && writes_like_native(0, behavior, "behavior") &&
                   writes_like_native(1, behavior, "behavior");
    execution_module_free(behavior);
    type_catalog_set_project(NULL);
    REQUIRE(covered);
    REQUIRE(written);
    return true;
}
TEST_CASE(prepared_writer_reproduces_native_queries)
{
    REQUIRE(prepare_corpus());
    const type_desc *const reflected[] = { &tally_type };
    const type_catalog_pack pack = { .types = reflected, .type_count = 1 };
    type_catalog_set_project(&pack);
    const char *const sources[] = { QUERY_BEHAVIOR, QUERY_SYSTEM };
    bool covered = true, written = true;
    for (int i = 0; i < 2; i++) {
        execution_module *module = source_module(sources[i]);
        const execution_module_plan *p = module ? &module->plan : NULL;
        bool has_query = p && p->query_count > 0 && p->description_count == 1 &&
                         p->system_description == i && p->descriptions[0].evaluate &&
                         p->descriptions[0].row_type.type == PIN_QUERY &&
                         p->descriptions[0].evaluate->argument_count == (i ? 0 : 1);
        covered &= has_query;
        written &= has_query && writes_like_native(0, module, "query") &&
                   writes_like_native(1, module, "query");
        execution_module_free(module);
    }
    type_catalog_set_project(NULL);
    REQUIRE(covered);
    REQUIRE(written);
    return true;
}
static pin_value byte_array(const unsigned char *data, size_t length)
{
    pin_value *items = NULL;
    for (size_t i = 0; i < length; i++) {
        apush(items, ((pin_value){ .type = PIN_U8, .u8 = data[i] }));
    }
    model m = { 0 };
    pin_value array = rows(&m, NULL, PIN_U8, items);
    afree(items);
    return array;
}
static bool same_bytes(const pin_value *written, const unsigned char *expected, size_t length)
{
    const pin_array *bytes = written->ptr;
    REQUIRE(written->type == PIN_U8 && bytes && (size_t)bytes->count == length);
    REQUIRE(first_difference(expected, length, bytes) == length);
    return true;
}
/* The authored fingerprint and closure match the native ones over the four
 * smaller corpus modules' native bytes. */
TEST_CASE(prepared_writer_fingerprints_and_closes_modules)
{
    REQUIRE(prepare_corpus());
    for (int b = 0; b < 2; b++) {
        unsigned char *modules[CORPUS_COUNT - 1] = { 0 };
        package_module records[CORPUS_COUNT - 1];
        pin_value *ids = NULL, *names = NULL, *starts = NULL, *lengths = NULL;
        unsigned char *data = NULL;
        for (int c = 1; c < CORPUS_COUNT; c++) {
            const execution_module
                *module = compiler_fixture_package(&fixture, b, CORPUS[c])->module;
            size_t length = 0;
            uint64_t fingerprint = 0;
            char error[192];
            REQUIRE(
                execution_prepared_write(module, &modules[c - 1], &length, error, sizeof error)
            );
            REQUIRE(execution_prepared_measure(module, &length, &fingerprint, error, sizeof error));
            records[c - 1] = (package_module){ .name = CORPUS[c],
                                               .id = module->plan.id,
                                               .data = modules[c - 1],
                                               .length = length };
            pin_value encoded = byte_array(modules[c - 1], length), hash = { 0 };
            arena detached = { 0 };
            REQUIRE(
                compiler_fixture_call(&fixture, b, PACKAGE, "Fingerprint", &encoded, 1, BUDGET, &detached, &hash)
            );
            REQUIRE(hash.type == PIN_U64 && hash.u64 == (fingerprint ? fingerprint : 1));
            arena_free(&detached);
            apush(ids, ((pin_value){ .type = PIN_U64, .u64 = module->plan.id }));
            pin_value name;
            REQUIRE(!pin_value_text(&input, &name, CORPUS[c], strlen(CORPUS[c])));
            apush(names, name);
            apush(starts, ((pin_value){ .type = PIN_I32, .i = acount(data) }));
            apush(lengths, ((pin_value){ .type = PIN_I32, .i = (int)length }));
            for (size_t i = 0; i < length; i++) {
                apush(data, modules[c - 1][i]);
            }
        }
        unsigned char *expected = NULL;
        size_t length = 0;
        char error[192];
        uint64_t root = records[CORPUS_COUNT - 2].id;
        REQUIRE(
            compiler_artifact_write(records, CORPUS_COUNT - 1, root, &expected, &length, error, sizeof error)
        );
        model m = { 0 };
        pin_value arguments[] = {
            { .type = PIN_U64, .u64 = root },
            rows(&m, NULL, PIN_U64, ids),
            rows(&m, NULL, PIN_STR, names),
            byte_array(data, (size_t)acount(data)),
            rows(&m, NULL, PIN_I32, starts),
            rows(&m, NULL, PIN_I32, lengths),
        };
        arena detached = { 0 };
        pin_value closure = { 0 };
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, "Closure", arguments, 6, BUDGET, &detached, &closure)
        );
        REQUIRE(same_bytes(&closure, expected, length));
        arena_free(&detached);
        free(expected);
        for (int c = 0; c < CORPUS_COUNT - 1; c++) {
            free(modules[c]);
        }
        afree(ids);
        afree(names);
        afree(starts);
        afree(lengths);
        afree(data);
    }
    return true;
}
void test_language_prepared_writer_chibi()
{
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(prepared_writer_reproduces_native_modules);
    RUN_TEST_CASE(prepared_writer_reproduces_native_queries);
    RUN_TEST_CASE(prepared_writer_fingerprints_and_closes_modules);
    pu_clear_setup();
}
