#include "effect_analysis_chibi.h"

#include "compiler_fixture.h"
#include "effect_analysis_chibi_source.h"
#include "language/effect_analysis.h"
#include "package_core_source.h"
#include "support/array.h"
#include "syntax_chibi_source.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 24 fixed Analyze graphs, six Order graphs and seventeen actual analyzed replays:
 * 94 authored calls,500k steps each. Effects/Diagnostics/CoreStrings prepared
 * once per backend: six parses, plus seventeen tiny actual sources =23 parses.
 * Fact sources<512B/compiler sources<48KiB. Raw caps24expr (one fixed85expr depth witness)/16stmt/6functions/
 * 8symbols/8scopes/2queries/4bindings/8arguments/16children/6capabilities/
 * 3catalogtypes/16read-writeIDs. Actual caps64expr/64stmt/8functions/32symbols/
 * 16scopes/5queries/8bindings/8descriptions/32stages/4catalogtypes (component,
 * pointer, query row and coroutine task). Both:128IDs/256ranges,32diagnostics,text240B.
 * A component, computed-stage marker and guard form three query leaves and
 * two intersections.
 * Raw seeds and resolved edges test propagation; actual replays already contain
 * final effects and verify real schema/diagnostics/ownership, not seed discovery.
 * No IO, tasks, native callbacks, generated combinations or timing loops. */
enum
{
    MAX_ROWS = 256,
    MAX_IDS = 128
};
static const char PACKAGE[] = "project:effect_analysis";
static compiler_fixture fixture;
static arena input, detached, oracle, saved_storage[2], saved_oracle[2];
static pin_value saved[2][2];
static script_effect_result saved_expected[2];
static uint64_t saved_tags[2];
static script_syntax *syntax;
static script_analysis *analysis;
static bool prepared;
static int calls;
static uint64_t ids[MAX_IDS];
static int id_count;
static script_range ranges[MAX_ROWS];
static int range_count;
static void cleanup()
{
    arena_free(&input);
    arena_free(&detached);
    arena_free(&oracle);
    script_analysis_free(analysis);
    analysis = NULL;
    script_syntax_free(syntax);
    syntax = NULL;
}
static pin_value integer(int n)
{
    return (pin_value){ .type = PIN_I32, .i = n };
}
static pin_value boolean(bool b)
{
    return (pin_value){ .type = PIN_BOOL, .b = b };
}
static pin_value text(const char *s)
{
    s = s ? s : "";
    pin_value result;
    const char *error = pin_value_text(&input, &result, s, strlen(s));
    if (error) {
        abort();
    }
    return result;
}
static bool put(pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(&input, row->ptr, field, value));
    return true;
}
static bool rows(int backend, const char *name, int count, pin_value *out)
{
    REQUIRE(count >= 0 && count <= MAX_ROWS);
    const pin_record *shape = compiler_fixture_record(&fixture, backend, PACKAGE, name, &input);
    REQUIRE(shape);
    pin_array
        *array = pin_array_snapshot_allocate(&input, PIN_STRUCT, shape->type.name, count);
    REQUIRE(array);
    array->record = shape;
    *out = (pin_value){ .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .record = shape,
                        .type_name = shape->type.name,
                        .ptr = array };
    for (int i = 0; i < count; i++) {
        array->items[i] = (pin_value){ .type = PIN_STRUCT,
                                       .record = shape,
                                       .type_name = shape->type.name,
                                       .ptr = arena_alloc(&input, shape->type.size) };
        memset(array->items[i].ptr, 0, shape->type.size);
    }
    return true;
}
static pin_value *row(pin_value *array, int index)
{
    return &((pin_array *)array->ptr)->items[index];
}
static int slot(uint64_t id)
{
    if (!id) {
        return -1;
    }
    for (int i = 0; i < id_count; i++) {
        if (ids[i] == id) {
            return i;
        }
    }
    if (id_count == MAX_IDS) {
        return -2;
    }
    ids[id_count] = id;
    return id_count++;
}
static int location(script_range range)
{
    if (range_count == MAX_ROWS) {
        return -1;
    }
    ranges[range_count] = range;
    return range_count++;
}
static const script_catalog_variant *variant(int b, const char *name, const char *tag)
{
    const authoring_package *package = compiler_fixture_package(&fixture, b, PACKAGE);
    if (!package || !package->exports || !package->exports->metadata) {
        return NULL;
    }
    const script_catalog_type *type = script_catalog_type_by_id(
        package->exports->metadata,
        compiler_fixture_export(&fixture, b, PACKAGE, name)
    );
    for (int i = 0; type && i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, tag)) {
            return &type->variants[i];
        }
    }
    return NULL;
}
static bool pack(int b, const script_effect_facts *f, pin_value *out)
{
    const script_catalog *c = f->catalog;
    REQUIRE(
        c && f->expression_count <= 85 && f->statement_count <= 64 &&
        f->function_count <= 8 && f->symbol_count <= 32 &&
        f->scope_count <= 16 && f->query_count <= 5 && f->binding_count <= 8 &&
        f->description_count <= 8 && c->capability_count <= 6 && c->type_count <= 4
    );
    int arguments = 0, children = 0, reads = 0, writes = 0, stages = 0;
    for (int i = 0; i < f->expression_count; i++) {
        arguments += f->expressions[i].argument_count;
    }
    for (int i = 0; i < f->statement_count; i++) {
        children += f->statements[i].child_count;
    }
    for (int i = 0; i < f->binding_count; i++) {
        reads += f->bindings[i].read_count;
        writes += f->bindings[i].write_count;
    }
    for (int i = 0; i < f->description_count; i++) {
        stages += acount(f->descriptions[i].stages);
    }
    REQUIRE(stages <= 32);
    REQUIRE(arguments <= 8 && children <= 128 && reads <= 16 && writes <= 16);
    pin_value a[17];
    memset(a, 0, sizeof a);
    const char *names[] = { "Expression",  NULL,     "Statement", NULL,
                            "Function",    "Symbol", "Scope",     "Query",
                            "Binding",     NULL,     NULL,        "Capability",
                            "CatalogType", NULL,     "Range",     "Description",
                            "Stage" };
    int counts[] = { f->expression_count,
                     arguments,
                     f->statement_count,
                     children,
                     f->function_count,
                     f->symbol_count,
                     f->scope_count,
                     f->query_count,
                     f->binding_count,
                     reads,
                     writes,
                     c->capability_count,
                     c->type_count,
                     0,
                     0,
                     f->description_count,
                     stages };
    for (int i = 0; i < 17; i++) {
        if (names[i] && i != 14) {
            REQUIRE(rows(b, names[i], counts[i], &a[i]));
        }
    }
    const int array_indices[] = { 1, 3, 9, 10 };
    for (int i = 0; i < 4; i++) {
        int index = array_indices[i];
        pin_type type = index < 4 ? PIN_I32 : PIN_U64;
        pin_array *v = pin_array_snapshot_allocate(&input, type, NULL, counts[index]);
        REQUIRE(v);
        a[index] = (pin_value){ .type = type, .container = PIN_CONTAINER_ARRAY, .ptr = v };
    }
    int arg = 0, child = 0, read = 0, write = 0;
    id_count = 0;
    range_count = 0;
    for (int i = 0; i < f->expression_count; i++) {
        const script_expression *e = &f->expressions[i];
        pin_value *v = row(&a[0], i);
        int nominal = slot(e->type.nominal), capability = slot(e->capability),
            reader = slot(e->read), writer = slot(e->write);
        REQUIRE(nominal >= -1 && capability >= -1 && reader >= -1 && writer >= -1);
        const char *keys[] = { "kind",      "op",           "effects",
                               "left",      "right",        "arg_start",
                               "arg_count", "symbol",       "flags",
                               "container", "nominal_slot", "capability_slot",
                               "read_slot", "write_slot" };
        int values[] = { e->kind,
                         e->op,
                         (int)e->effects,
                         e->left,
                         e->right,
                         arg,
                         e->argument_count,
                         e->symbol,
                         (int)e->type.flags,
                         e->type.container,
                         nominal,
                         capability,
                         reader,
                         writer };
        for (int k = 0; k < 14; k++) {
            REQUIRE(put(v, keys[k], integer(values[k])));
        }
        int range = location(e->range);
        REQUIRE(
            range >= 0 && put(v, "task", boolean(e->task)) &&
            put(v, "range", integer(range)) &&
            put(v, "storage", integer(e->type.storage))
        );
        for (int j = 0; j < e->argument_count; j++) {
            row(&a[1], arg++)[0] = integer(e->arguments[j].value);
        }
    }
    for (int i = 0; i < f->statement_count; i++) {
        const script_statement *s = &f->statements[i];
        pin_value *v = row(&a[2], i);
        int range = location(s->range);
        REQUIRE(range >= 0);
        const char *keys[] = { "kind",        "effects",     "expression",
                               "second",      "third",       "body",
                               "alternative", "child_start", "child_count",
                               "reduction",   "range" };
        int values[] = {
            s->kind,        (int)s->effects, s->expression,  s->second,
            s->third,       s->body,         s->alternative, child,
            s->child_count, s->reduction,    range
        };
        for (int k = 0; k < 11; k++) {
            REQUIRE(put(v, keys[k], integer(values[k])));
        }
        REQUIRE(put(v, "symbol", integer(s->symbol)));
        for (int j = 0; j < s->child_count; j++) {
            row(&a[3], child++)[0] = integer(s->children[j]);
        }
    }
    for (int i = 0; i < f->function_count; i++) {
        const script_function *s = &f->functions[i];
        pin_value *v = row(&a[4], i);
        int range = location(s->range);
        REQUIRE(range >= 0);
        REQUIRE(
            put(v, "kind", integer(s->kind)) &&
            put(v, "parent", integer(s->parent)) && put(v, "body", integer(s->body)) &&
            put(v, "effects", integer((int)s->effects)) &&
            put(v, "pure", boolean(s->pure)) && put(v, "range", integer(range))
        );
    }
    for (int i = 0; i < f->symbol_count; i++) {
        const script_symbol *s = &f->symbols[i];
        pin_value *v = row(&a[5], i);
        int nominal = slot(s->type.nominal),
            component = slot(s->component_type.nominal),
            range = location(s->declaration);
        REQUIRE(nominal >= -1 && component >= -1 && range >= 0);
        REQUIRE(
            put(v, "kind", integer(s->kind)) && put(v, "scope", integer(s->scope)) &&
            put(v, "initializer", integer(s->initializer)) &&
            put(v, "nominal_slot", integer(nominal)) &&
            put(v, "range", integer(range)) &&
            put(v, "flags", integer((int)s->type.flags)) &&
            put(v, "query_description", integer(s->query_description)) &&
            put(v, "component_slot", integer(component)) &&
            put(v, "storage", integer(s->type.storage)) &&
            put(v, "mutations", integer(s->mutations))
        );
    }
    for (int i = 0; i < f->scope_count; i++) {
        REQUIRE(put(row(&a[6], i), "function", integer(f->scopes[i].function)));
    }
    for (int i = 0; i < f->query_count; i++) {
        const script_query *q = &f->queries[i];
        pin_value *v = row(&a[7], i);
        int component = slot(q->component), range = location(q->range);
        REQUIRE(component >= -1 && range >= 0);
        REQUIRE(
            put(v, "kind", integer(q->kind)) && put(v, "value", integer(q->value)) &&
            put(v, "component_slot", integer(component)) &&
            put(v, "description", integer(q->description)) &&
            put(v, "range", integer(range))
        );
    }
    for (int i = 0; i < f->binding_count; i++) {
        const script_binding *binding = &f->bindings[i];
        pin_value *v = row(&a[8], i);
        int range = location(binding->range);
        REQUIRE(range >= 0);
        REQUIRE(
            put(v, "target", integer(binding->target)) &&
            put(v, "value", integer(binding->value)) &&
            put(v, "read_start", integer(read)) &&
            put(v, "read_count", integer(binding->read_count)) &&
            put(v, "write_start", integer(write)) &&
            put(v, "write_count", integer(binding->write_count)) &&
            put(v, "range", integer(range))
        );
        for (int j = 0; j < binding->read_count; j++) {
            row(&a[9], read++)[0] = (pin_value){ .type = PIN_U64,
                                                 .u64 = binding->reads[j] };
        }
        for (int j = 0; j < binding->write_count; j++) {
            row(&a[10], write++)[0] = (pin_value){ .type = PIN_U64,
                                                   .u64 = binding->writes[j] };
        }
    }
    for (int i = 0; i < c->capability_count; i++) {
        const script_catalog_capability *cap = &c->capabilities[i];
        pin_value *v = row(&a[11], i);
        int id = slot(cap->id), component = slot(cap->component);
        REQUIRE(id >= -1 && component >= -1);
        REQUIRE(
            put(v, "id_slot", integer(id)) &&
            put(v, "component_access", integer(cap->component_access)) &&
            put(v, "state_access", integer(cap->state_access)) &&
            put(v, "component_slot", integer(component)) &&
            put(v, "effects", integer((int)cap->effects))
        );
    }
    for (int i = 0; i < c->type_count; i++) {
        const script_catalog_type *t = &c->types[i];
        pin_value *v = row(&a[12], i);
        int id = slot(t->id), component = slot(t->component_id);
        REQUIRE(id >= -1 && component >= -1 && t->name && strlen(t->name) <= 240);
        REQUIRE(
            put(v, "id_slot", integer(id)) &&
            put(v, "component_id_slot", integer(component)) &&
            put(v, "name", text(t->name))
        );
    }
    int stage = 0;
    for (int i = 0; i < f->description_count; i++) {
        const script_query_description *description = &f->descriptions[i];
        pin_value *v = row(&a[15], i);
        REQUIRE(
            put(v, "system", boolean(description->system)) &&
            put(v, "stage_start", integer(stage)) &&
            put(v, "stage_count", integer(acount(description->stages)))
        );
        for (int j = 0; j < acount(description->stages); j++) {
            const script_query_stage *entry = &description->stages[j];
            pin_value *current = row(&a[16], stage++);
            int range = location(entry->range);
            REQUIRE(
                range >= 0 && put(current, "symbol", integer(entry->symbol)) &&
                put(current, "expression", integer(entry->expression)) &&
                put(current, "range", integer(range))
            );
        }
    }
    pin_array *numbers = pin_array_snapshot_allocate(&input, PIN_U64, NULL, id_count);
    REQUIRE(numbers);
    a[13] = (pin_value){ .type = PIN_U64, .container = PIN_CONTAINER_ARRAY, .ptr = numbers };
    for (int i = 0; i < id_count; i++) {
        numbers->items[i] = (pin_value){ .type = PIN_U64, .u64 = ids[i] };
    }
    REQUIRE(rows(b, "Range", range_count, &a[14]));
    for (int i = 0; i < range_count; i++) {
        script_range r = ranges[i];
        pin_value *v = row(&a[14], i);
        REQUIRE(
            put(v, "start", integer(r.start)) && put(v, "end", integer(r.end)) &&
            put(v, "line", integer(r.line)) && put(v, "column", integer(r.column)) &&
            put(v, "document", integer(r.document))
        );
    }
    const script_catalog_variant *v = variant(b, "Environment", "Tables");
    REQUIRE(v);
    REQUIRE(!compiler_fixture_union(&input, v->tag, a, 17, out));
    return true;
}
static bool get(const pin_value *value, const char *name, pin_value *out)
{
    REQUIRE(value->type == PIN_STRUCT && value->ptr && value->record);
    const type_field *field = pin_type_field_path(pin_value_type(value), name);
    REQUIRE(field);
    REQUIRE(!pin_read_field(&input, value->ptr, field, out));
    return true;
}
static bool int_field(const pin_value *v, const char *name, int *out)
{
    pin_value field;
    REQUIRE(get(v, name, &field) && field.type == PIN_I32);
    *out = field.i;
    return true;
}
static bool string_field(const pin_value *v, const char *name, const char **out)
{
    pin_value field;
    REQUIRE(get(v, name, &field) && field.type == PIN_STR && pin_text(field).bytes);
    *out = pin_text(field).bytes;
    return true;
}
static bool array(const pin_value *value, int count, const pin_array **out)
{
    REQUIRE(value->type == PIN_STRUCT && value->container == PIN_CONTAINER_ARRAY);
    *out = value->ptr;
    REQUIRE(
        count >= 0 && ((!*out && !count) || (*out && (*out)->count == count &&
                                             (!count || (*out)->items)))
    );
    return true;
}
static bool diagnostic(const pin_value *value, script_diagnostic *out)
{
    const char *message;
    REQUIRE(
        int_field(value, "start", &out->range.start) &&
        int_field(value, "end", &out->range.end) &&
        int_field(value, "line", &out->range.line) &&
        int_field(value, "column", &out->range.column) &&
        int_field(value, "document", &out->range.document) &&
        string_field(value, "message", &message)
    );
    REQUIRE(strlen(message) < sizeof out->message);
    strcpy(out->message, message);
    return true;
}
static bool equal_diagnostic(const script_diagnostic *a, const script_diagnostic *b)
{
    return a->range.start == b->range.start && a->range.end == b->range.end &&
           a->range.line == b->range.line && a->range.column == b->range.column &&
           a->range.document == b->range.document && !strcmp(a->message, b->message);
}
static bool owned(arena *owner, const pin_value *value, int depth)
{
    REQUIRE(depth <= 5);
    if (value->container == PIN_CONTAINER_ARRAY) {
        if (!value->ptr) {
            return true;
        }
        const pin_array *a = value->ptr;
        REQUIRE(
            arena_contains(owner, a) && a->count >= 0 && a->count <= 85 &&
            (!a->count || (a->items && arena_contains(owner, a->items)))
        );
        for (int i = 0; i < a->count; i++) {
            REQUIRE(owned(owner, &a->items[i], depth + 1));
        }
        return true;
    }
    if (value->type == PIN_UNION) {
        const pin_union *u = value->ptr;
        REQUIRE(u && arena_contains(owner, u) && u->count >= 2 && u->count <= 4);
        for (int i = 0; i < u->count; i++) {
            REQUIRE(owned(owner, &u->values[i], depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STRUCT) {
        REQUIRE(
            value->ptr && value->record && arena_contains(owner, value->ptr) &&
            arena_contains(owner, value->record)
        );
        const type_desc *type = pin_value_type(value);
        REQUIRE(
            type && type->name && arena_contains(owner, type->name) &&
            type->field_count <= 6 &&
            (!type->field_count || (type->fields && arena_contains(owner, type->fields)))
        );
        for (int i = 0; i < type->field_count; i++) {
            pin_value field;
            REQUIRE(!pin_read_field(owner, value->ptr, &type->fields[i], &field));
            REQUIRE(owned(owner, &field, depth + 1));
        }
        return true;
    }
    if (value->type == PIN_STR) {
        REQUIRE(pin_text(*value).bytes && arena_contains(owner, value->text));
        return true;
    }
    REQUIRE(value->type == PIN_I32);
    return true;
}
static bool integer_array(const pin_value *value, int count, const unsigned *expected)
{
    REQUIRE(value->type == PIN_I32 && value->container == PIN_CONTAINER_ARRAY);
    const pin_array *a = value->ptr;
    REQUIRE((!a && !count) || (a && a->count == count && (!count || a->items)));
    for (int i = 0; i < count; i++) {
        REQUIRE(a->items[i].type == PIN_I32 && (unsigned)a->items[i].i == expected[i]);
    }
    return true;
}
static bool diagnostics_value(const pin_value *value, const script_diagnostic *expected, int count)
{
    const pin_array *a;
    REQUIRE(array(value, count, &a));
    for (int i = 0; i < count; i++) {
        script_diagnostic d;
        REQUIRE(diagnostic(&a->items[i], &d) && equal_diagnostic(&d, &expected[i]));
    }
    return true;
}
static bool result_value(const pin_value *value, uint64_t tag, const script_effect_result *expected)
{
    REQUIRE(value->type == PIN_UNION && value->ptr);
    const pin_union *p = value->ptr;
    REQUIRE(p->tag == tag && p->count == 4);
    REQUIRE(
        integer_array(&p->values[0], expected->expression_count, expected->expression_effects) &&
        integer_array(&p->values[1], expected->statement_count, expected->statement_effects) &&
        integer_array(&p->values[2], expected->function_count, expected->function_effects) &&
        diagnostics_value(&p->values[3], expected->diagnostics, expected->diagnostic_count)
    );
    return true;
}
static bool compare(const script_effect_facts *facts, int retain, script_effect_result *out)
{
    arena_free(&oracle);
    script_effect_result expected = script_effect_analyze(&oracle, facts);
    REQUIRE(
        expected.expression_count == facts->expression_count &&
        expected.statement_count == facts->statement_count &&
        expected.function_count == facts->function_count &&
        expected.diagnostic_count >= 0 && expected.diagnostic_count <= 32
    );
    for (int b = 0; b < 2; b++) {
        arena_free(&input);
        arena_free(&detached);
        pin_value argument, result;
        REQUIRE(pack(b, facts, &argument));
        const script_catalog_variant *v = variant(b, "Analysis", "Completed");
        REQUIRE(v);
        uint64_t tag = v->tag;
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, "Analyze", &argument, 1, 500000, &detached, &result)
        );
        calls++;
        arena_free(&input);
        REQUIRE(owned(&detached, &result, 0) && result_value(&result, tag, &expected));
        if (retain >= 0) {
            REQUIRE(!compiler_fixture_snapshot(&saved_storage[b], &saved[b][retain], &result));
            saved_tags[b] = tag;
        }
    }
    if (retain >= 0) {
        saved_expected[retain] = expected;
        saved_oracle[retain] = oracle;
        oracle = (arena){ 0 };
    }
    if (out) {
        *out = expected;
    }
    return true;
}
static bool order(const script_effect_facts *facts, script_binding_order *out)
{
    arena_free(&oracle);
    script_binding_order expected = script_effect_order(&oracle, facts);
    REQUIRE(
        expected.count == facts->binding_count && expected.count <= 8 &&
        expected.diagnostic_count >= 0 && expected.diagnostic_count <= 1
    );
    for (int b = 0; b < 2; b++) {
        arena_free(&input);
        arena_free(&detached);
        pin_value argument, result;
        REQUIRE(pack(b, facts, &argument));
        const script_catalog_variant *v = variant(b, "Ordering", "Ordered");
        REQUIRE(v);
        uint64_t tag = v->tag;
        REQUIRE(
            compiler_fixture_call(&fixture, b, PACKAGE, "Order", &argument, 1, 500000, &detached, &result)
        );
        calls++;
        arena_free(&input);
        REQUIRE(
            owned(&detached, &result, 0) && result.type == PIN_UNION && result.ptr
        );
        const pin_union *p = result.ptr;
        REQUIRE(p->tag == tag && p->count == 2);
        unsigned indices[8];
        for (int i = 0; i < expected.count; i++) {
            indices[i] = (unsigned)expected.permutation[i];
        }
        REQUIRE(
            integer_array(&p->values[0], expected.count, indices) &&
            diagnostics_value(&p->values[1], expected.diagnostics, expected.diagnostic_count)
        );
    }
    if (out) {
        *out = expected;
    }
    return true;
}
static bool prepare()
{
    source_package_input files[] = {
        { "/compiler/effect_analysis/Effects.chibi",
          EFFECT_ANALYSIS_CHIBI_SOURCE,
          sizeof EFFECT_ANALYSIS_CHIBI_SOURCE - 1 },
        { "/compiler/syntax_diagnostics/Diagnostics.chibi",
          SYNTAX_CHIBI_SOURCE,
          sizeof SYNTAX_CHIBI_SOURCE - 1 },
        { "/sdk/core/strings/Strings.chibi", CORE_STRINGS_SOURCE, sizeof CORE_STRINGS_SOURCE - 1 }
    };
    for (int i = 0; i < 3; i++) {
        REQUIRE(files[i].size < 48 * 1024);
    }
    prepared = compiler_fixture_prepare(&fixture, files, 3);
    REQUIRE(prepared);
    return true;
}
typedef struct
{
    script_effect_facts facts;
    script_catalog catalog;
    script_expression expressions[85];
    script_statement statements[16];
    script_function functions[6];
    script_symbol symbols[8];
    script_scope scopes[8];
    script_query queries[2];
    script_binding bindings[4];
    script_argument arguments[8];
    int children[16];
    script_catalog_capability capabilities[6];
    script_catalog_type types[3];
    uint64_t reads[16], writes[16];
} raw_facts;
static script_range range_at(int i)
{
    return (script_range){ 20 + i * 7, 23 + i * 7, 3 + i, 2 + i, i % 3 };
}
static void raw_init(raw_facts *r)
{
    memset(r, 0, sizeof *r);
    r->catalog = (script_catalog){ .capabilities = r->capabilities, .types = r->types };
    r->facts = (script_effect_facts){
        &r->catalog, r->expressions, 0, r->statements, 0, r->functions,
        0,           r->symbols,     0, r->scopes,     0, r->queries,
        0,           r->bindings,    0, NULL,          0
    };
}
static int expr(raw_facts *r, script_expression_kind kind, int left, int right, unsigned effects)
{
    int i = r->facts.expression_count++;
    r->expressions[i] = (script_expression){ .kind = kind,
                                             .left = left,
                                             .right = right,
                                             .symbol = -1,
                                             .effects = effects,
                                             .type = { .storage = PIN_F64 },
                                             .range = range_at(i) };
    return i;
}
static int statement(raw_facts *r, script_statement_kind kind, int expression)
{
    int i = r->facts.statement_count++;
    r->statements[i] = (script_statement){ .kind = kind,
                                           .expression = expression,
                                           .second = -1,
                                           .third = -1,
                                           .body = -1,
                                           .alternative = -1,
                                           .symbol = -1,
                                           .range = range_at(i + 30) };
    return i;
}
static int function(raw_facts *r, int body, script_function_kind kind, int parent, bool pure)
{
    int i = r->facts.function_count++;
    r->functions[i] = (script_function){ .kind = kind,
                                         .body = body,
                                         .parent = parent,
                                         .pure = pure,
                                         .range = range_at(i + 60) };
    return i;
}
static int symbol(raw_facts *r, script_symbol_kind kind, int scope, uint64_t nominal)
{
    int i = r->facts.symbol_count++;
    r->symbols[i] = (script_symbol){
        .kind = kind,
        .scope = scope,
        .initializer = -1,
        .type = { .storage = PIN_STRUCT, .nominal = nominal },
        .component_type = { .storage = PIN_STRUCT, .nominal = nominal },
        .declaration = range_at(i + 70)
    };
    return i;
}
static int symbol_expr(raw_facts *r, int index)
{
    int i = expr(r, SCRIPT_EXPR_SYMBOL, -1, -1, 0);
    r->expressions[i].symbol = index;
    return i;
}
static void query(raw_facts *r, int value)
{
    int i = r->facts.query_count++;
    r->queries[i] = (script_query){ .kind = SCRIPT_QUERY_VALUE,
                                    .value = value,
                                    .range = range_at(i + 80) };
}
static void binding(raw_facts *r, int target, int value)
{
    int i = r->facts.binding_count++;
    r->bindings[i] = (script_binding){ .target = target,
                                       .value = value,
                                       .range = range_at(i + 90) };
}
static uint64_t component(raw_facts *r, int i)
{
    uint64_t id = UINT64_MAX - 100 - (uint64_t)i;
    r->types[i] = (script_catalog_type){
        .id = id,
        .component_id = UINT64_MAX - 200 - (uint64_t)i,
        .name = i ? "Other" : "Position",
        .type = { .storage = PIN_STRUCT, .nominal = id },
        .component = true
    };
    r->catalog.type_count = i + 1;
    return id;
}
static int component_expr(raw_facts *r, uint64_t id, bool optional)
{
    int i = expr(r, SCRIPT_EXPR_COMPONENT, -1, -1, SCRIPT_EFFECT_READ);
    r->expressions[i].capability = id;
    r->expressions[i].type = (script_type){ .storage = PIN_STRUCT,
                                            .nominal = id,
                                            .flags = optional ? SCRIPT_TYPE_OPTIONAL
                                                              : 0 };
    return i;
}
static int member(raw_facts *r, int receiver)
{
    return expr(r, SCRIPT_EXPR_MEMBER, receiver, -1, SCRIPT_EFFECT_READ);
}
static int reduction(raw_facts *r, int target, int value)
{
    int i = statement(r, SCRIPT_STMT_REDUCE, target);
    r->statements[i].second = value;
    r->statements[i].effects = SCRIPT_EFFECT_WRITE;
    return i;
}
static void system_body(raw_facts *r, int first, int count)
{
    int block = statement(r, SCRIPT_STMT_BLOCK, -1);
    for (int i = 0; i < count; i++) {
        r->children[i] = first + i;
    }
    r->statements[block].children = r->children;
    r->statements[block].child_count = count;
    function(r, block, SCRIPT_FUNCTION_SYSTEM, -1, false);
}
static int capability(
    raw_facts *r,
    script_component_access access,
    script_state_access state,
    uint64_t component_id,
    unsigned effects
)
{
    int i = r->catalog.capability_count++;
    r->capabilities[i] = (script_catalog_capability){ .id = UINT64_MAX - 300 -
                                                            (uint64_t)i,
                                                      .component_access = access,
                                                      .state_access = state,
                                                      .component = component_id,
                                                      .effects = effects };
    return i;
}
static int native_call(raw_facts *r, int cap)
{
    int i = expr(r, SCRIPT_EXPR_CALL, -1, -1, r->capabilities[cap].effects);
    r->expressions[i].capability = r->capabilities[cap].id;
    return i;
}
TEST_CASE(effect_analysis_raw_seeds_dependencies_and_restrictions_match)
{
    REQUIRE(prepare());
    char long_name[241];
    memset(long_name, 'x', 240);
    long_name[240] = 0;
    const char prefix[] = "Reduction producer cannot read or write component '";
    int split = 190 - (int)(sizeof prefix - 1);
    long_name[split] = (char)0xc3;
    long_name[split + 1] = (char)0xa9;
    for (int c = 0; c < 24; c++) {
        raw_facts r;
        raw_init(&r);
        switch (c) {
            case 0: break;
            case 1: {
                expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, SCRIPT_EFFECT_READ);
                expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, SCRIPT_EFFECT_WRITE);
                expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, SCRIPT_EFFECT_HOST);
                int e = expr(&r, SCRIPT_EXPR_BINARY, 0, 1, SCRIPT_EFFECT_STRUCTURAL);
                r.arguments[0] = (script_argument){ .value = 2 };
                r.expressions[e].arguments = r.arguments;
                r.expressions[e].argument_count = 1;
                int s = statement(&r, SCRIPT_STMT_EXPRESSION, e);
                r.statements[s].second = 1;
                r.statements[s].third = 2;
                statement(&r, SCRIPT_STMT_EXPRESSION, 0);
                r.statements[1].effects = SCRIPT_EFFECT_RANDOM;
                statement(&r, SCRIPT_STMT_EXPRESSION, 1);
                int block = statement(&r, SCRIPT_STMT_BLOCK, -1);
                r.statements[block].body = 0;
                r.statements[block].alternative = 1;
                r.children[0] = 2;
                r.statements[block].children = r.children;
                r.statements[block].child_count = 1;
                r.statements[block].effects = SCRIPT_EFFECT_SUSPEND;
                function(&r, block, SCRIPT_FUNCTION_AUTHORED, -1, false);
                break;
            }
            case 2:
            case 3: {
                /* This forward edge stays at its seed when the second and
                 * final pass first propagates the callee into expression 1.
                 * Running to a fixed point would change expression 0. */
                expr(&r, SCRIPT_EXPR_UNARY, 1, -1, SCRIPT_EFFECT_READ);
                expr(&r, c == 2 ? SCRIPT_EXPR_CALL : SCRIPT_EXPR_JOIN, -1, 0, 0);
                expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, c == 2 ? SCRIPT_EFFECT_WRITE : SCRIPT_EFFECT_SUSPEND);
                int body = statement(&r, SCRIPT_STMT_EXPRESSION, 2);
                function(&r, body, SCRIPT_FUNCTION_AUTHORED, -1, false);
                break;
            }
            case 4: {
                expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, SCRIPT_EFFECT_READ | SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_SUSPEND);
                expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, SCRIPT_EFFECT_SUSPEND);
                int a = expr(&r, SCRIPT_EXPR_CALL, -1, 0, 0),
                    b = expr(&r, SCRIPT_EXPR_CALL, -1, 0, 0);
                r.expressions[a].task = true;
                r.expressions[b].task = true;
                r.arguments[0] = (script_argument){ .value = 1 };
                r.expressions[b].arguments = r.arguments;
                r.expressions[b].argument_count = 1;
                int body = statement(&r, SCRIPT_STMT_EXPRESSION, 0);
                function(&r, body, SCRIPT_FUNCTION_AUTHORED, -1, false);
                break;
            }
            case 5: {
                for (int i = 0; i < 4; i++) {
                    expr(
                        &r,
                        i == 3 ? SCRIPT_EXPR_CONSTANT : SCRIPT_EXPR_CALL,
                        -1,
                        i == 3 ? -1 : i + 1,
                        i == 3 ? SCRIPT_EFFECT_HOST : 0
                    );
                    statement(&r, SCRIPT_STMT_EXPRESSION, i);
                    function(&r, i, SCRIPT_FUNCTION_AUTHORED, -1, false);
                }
                break;
            }
            case 6: {
                expr(&r, SCRIPT_EXPR_CALL, -1, 1, 0);
                expr(&r, SCRIPT_EXPR_CALL, -1, 0, 0);
                expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, SCRIPT_EFFECT_HOST);
                statement(&r, SCRIPT_STMT_EXPRESSION, 0);
                r.statements[0].second = 2;
                statement(&r, SCRIPT_STMT_EXPRESSION, 1);
                function(&r, 0, SCRIPT_FUNCTION_AUTHORED, -1, true);
                function(&r, 1, SCRIPT_FUNCTION_AUTHORED, -1, false);
                break;
            }
            case 7: {
                expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, SCRIPT_EFFECT_READ);
                statement(&r, SCRIPT_STMT_EXPRESSION, 0);
                function(&r, -1, SCRIPT_FUNCTION_AUTHORED, -1, true);
                r.functions[0].effects = SCRIPT_EFFECT_RANDOM;
                function(&r, 0, SCRIPT_FUNCTION_CONTINUATION, 0, false);
                r.functions[1].effects = SCRIPT_EFFECT_WRITE;
                break;
            }
            case 8: {
                unsigned bits[] = {
                    SCRIPT_EFFECT_READ,       SCRIPT_EFFECT_WRITE,
                    SCRIPT_EFFECT_STRUCTURAL, SCRIPT_EFFECT_HOST,
                    SCRIPT_EFFECT_SUSPEND,    SCRIPT_EFFECT_RANDOM
                };
                for (int i = 0; i < 6; i++) {
                    expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, bits[i]);
                    statement(&r, SCRIPT_STMT_EXPRESSION, i);
                    function(&r, i, SCRIPT_FUNCTION_AUTHORED, -1, true);
                }
                break;
            }
            case 9: {
                expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, SCRIPT_EFFECT_READ);
                expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, SCRIPT_EFFECT_HOST);
                binding(&r, 0, 0);
                binding(&r, 0, 1);
                int a = symbol(&r, SCRIPT_STATE, -1, 0),
                    b = symbol(&r, SCRIPT_SHARED_STATE, -1, 0);
                r.symbols[a].initializer = 0;
                r.symbols[b].initializer = 1;
                break;
            }
            case 10: {
                r.scopes[0] = (script_scope){ .function = -1 };
                r.scopes[1] = (script_scope){ .function = 0 };
                r.facts.scope_count = 2;
                function(&r, -1, SCRIPT_FUNCTION_AUTHORED, -1, false);
                int allowed = symbol_expr(&r, symbol(&r, SCRIPT_PARAMETER, 1, 0));
                r.expressions[allowed].effects = SCRIPT_EFFECT_READ;
                query(&r, allowed);
                int root = symbol_expr(&r, symbol(&r, SCRIPT_PARAMETER, 0, 0));
                script_symbol_kind kinds[] = { SCRIPT_STATE, SCRIPT_SHARED_STATE, SCRIPT_LOCAL };
                for (int i = 0; i < 3; i++) {
                    int next = symbol_expr(&r, symbol(&r, kinds[i], 0, 0));
                    root = expr(&r, SCRIPT_EXPR_BINARY, root, next, 0);
                }
                query(&r, root);
                break;
            }
            case 11: {
                expr(&r, SCRIPT_EXPR_CALL, -1, 0, 0);
                expr(&r, SCRIPT_EXPR_CALL, -1, 1, 0);
                expr(&r, SCRIPT_EXPR_CALL, -1, 0, 0);
                int task = expr(&r, SCRIPT_EXPR_CALL, -1, -1, 0);
                r.expressions[task].task = true;
                int invocation = symbol_expr(&r, symbol(&r, SCRIPT_STATE, -1, 0));
                statement(&r, SCRIPT_STMT_EXPRESSION, 1);
                r.statements[0].second = task;
                statement(&r, SCRIPT_STMT_EXPRESSION, 2);
                r.statements[1].second = invocation;
                function(&r, 0, SCRIPT_FUNCTION_AUTHORED, -1, false);
                function(&r, 1, SCRIPT_FUNCTION_AUTHORED, -1, false);
                int root = 0;
                for (int i = 0; i < 80; i++) {
                    root = expr(&r, SCRIPT_EXPR_CAST, root, -1, 0);
                }
                query(&r, root);
                break;
            }
            case 12: {
                int relation = expr(&r, SCRIPT_EXPR_RELATION, -1, -1, 0),
                    impure = expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, SCRIPT_EFFECT_STRUCTURAL | SCRIPT_EFFECT_SUSPEND),
                    ordinary = expr(&r, SCRIPT_EXPR_ARRAY, -1, -1, 0),
                    read = expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, SCRIPT_EFFECT_READ);
                int badbody = statement(&r, SCRIPT_STMT_EXPRESSION, impure),
                    goodbody = statement(&r, SCRIPT_STMT_EXPRESSION, read);
                int a = statement(&r, SCRIPT_STMT_FOREACH, relation);
                r.statements[a].body = badbody;
                int b = statement(&r, SCRIPT_STMT_FOREACH, ordinary);
                r.statements[b].body = badbody;
                int d = statement(&r, SCRIPT_STMT_FOREACH, relation);
                r.statements[d].body = goodbody;
                break;
            }
            case 13: {
                uint64_t id = component(&r, 0);
                int a = component_expr(&r, id, false), b = member(&r, a),
                    value = expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, 0);
                reduction(&r, b, value);
                int s = symbol_expr(&r, symbol(&r, SCRIPT_COMPONENT, -1, id));
                int field = member(&r, s);
                reduction(&r, field, value);
                system_body(&r, 0, 2);
                break;
            }
            case 14: {
                uint64_t id = component(&r, 0);
                int target = member(&r, component_expr(&r, id, false)),
                    value = expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, 0);
                reduction(&r, target, value);
                reduction(&r, target, value);
                r.statements[1].reduction = SCRIPT_REDUCTION_ADD_UNORDERED;
                reduction(&r, target, value);
                system_body(&r, 0, 3);
                break;
            }
            case 15: {
                uint64_t id = component(&r, 0), other = component(&r, 1);
                int target = member(&r, component_expr(&r, id, false)),
                    different = member(&r, component_expr(&r, other, false));
                reduction(&r, target, target);
                reduction(&r, target, different);
                system_body(&r, 0, 2);
                break;
            }
            case 16: {
                uint64_t id = component(&r, 0);
                int entity = member(&r, component_expr(&r, id, false));
                int base = component_expr(&r, id, false);
                r.expressions[base].left = entity;
                int target = member(&r, base),
                    value = expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, 0);
                reduction(&r, target, value);
                system_body(&r, 0, 1);
                break;
            }
            case 17: {
                uint64_t a = component(&r, 0), b = component(&r, 1);
                int optional_a = component_expr(&r, a, true),
                    optional_b = component_expr(&r, b, true),
                    target_a = member(&r, optional_a),
                    target_b = member(&r, optional_b),
                    value = expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, 0);
                reduction(&r, target_a, value);
                reduction(&r, target_b, value);
                int not = expr(&r, SCRIPT_EXPR_UNARY, optional_a, -1, 0);
                r.expressions[not].op = SCRIPT_OP_NOT;
                int both = expr(&r, SCRIPT_EXPR_BINARY, not, optional_a, 0);
                r.expressions[both].op = SCRIPT_OP_AND;
                int nil = expr(&r, SCRIPT_EXPR_NIL, -1, -1, 0),
                    equal = expr(&r, SCRIPT_EXPR_BINARY, optional_a, nil, 0);
                r.expressions[equal].op = SCRIPT_OP_NE;
                int direct = statement(&r, SCRIPT_STMT_IF, optional_a);
                r.statements[direct].body = 0;
                int loop = statement(&r, SCRIPT_STMT_WHILE, both);
                r.statements[loop].body = 0;
                int check = statement(&r, SCRIPT_STMT_IF, equal);
                r.statements[check].body = 1;
                int native = expr(&r, SCRIPT_EXPR_CALL, -1, -1, 0);
                r.arguments[0] = (script_argument){ .value = optional_b };
                r.expressions[native].arguments = r.arguments;
                r.expressions[native].argument_count = 1;
                query(&r, both);
                query(&r, native);
                system_body(&r, 0, 5);
                break;
            }
            case 18: {
                uint64_t a = component(&r, 0), b = component(&r, 1);
                int value = expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, 0);
                reduction(&r, member(&r, component_expr(&r, a, false)), value);
                reduction(&r, member(&r, component_expr(&r, b, false)), value);
                r.queries[0] = (script_query){ .kind = SCRIPT_QUERY_CHANGED,
                                               .component = r.types[0].component_id,
                                               .value = -1 };
                r.queries[1] = (script_query){ .kind = SCRIPT_QUERY_CHANGED,
                                               .component = b,
                                               .value = -1 };
                r.facts.query_count = 2;
                system_body(&r, 0, 2);
                break;
            }
            case 19: {
                uint64_t a = component(&r, 0), b = component(&r, 1),
                         outside = component(&r, 2);
                int value = expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, 0);
                reduction(&r, member(&r, component_expr(&r, a, false)), value);
                reduction(&r, member(&r, component_expr(&r, b, false)), value);
                int has = capability(&r, SCRIPT_COMPONENT_ACCESS_HAS, SCRIPT_STATE_ACCESS_NONE, 0, SCRIPT_EFFECT_HOST),
                    state = capability(&r, SCRIPT_COMPONENT_ACCESS_NONE, SCRIPT_STATE_ACCESS_READ, 0, SCRIPT_EFFECT_HOST),
                    other = capability(
                        &r,
                        SCRIPT_COMPONENT_ACCESS_READ,
                        SCRIPT_STATE_ACCESS_NONE,
                        outside,
                        SCRIPT_EFFECT_WRITE
                    ),
                    same = capability(&r, SCRIPT_COMPONENT_ACCESS_READ, SCRIPT_STATE_ACCESS_NONE, a, SCRIPT_EFFECT_READ);
                statement(&r, SCRIPT_STMT_EXPRESSION, native_call(&r, has));
                statement(&r, SCRIPT_STMT_EXPRESSION, native_call(&r, state));
                statement(&r, SCRIPT_STMT_EXPRESSION, native_call(&r, other));
                statement(&r, SCRIPT_STMT_EXPRESSION, native_call(&r, same));
                system_body(&r, 0, 6);
                break;
            }
            case 20: {
                uint64_t a = component(&r, 0), b = component(&r, 1);
                int value = expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, 0);
                int target = member(&r, component_expr(&r, a, false));
                reduction(&r, target, value);
                reduction(&r, member(&r, component_expr(&r, b, false)), value);
                int opaque = capability(&r, SCRIPT_COMPONENT_ACCESS_READ, SCRIPT_STATE_ACCESS_NONE, 0, 0),
                    host = capability(&r, SCRIPT_COMPONENT_ACCESS_NONE, SCRIPT_STATE_ACCESS_READ, 0, SCRIPT_EFFECT_HOST);
                r.capabilities[opaque].id = 0; /* External calls inspect catalog ID zero. */
                statement(&r, SCRIPT_STMT_EXPRESSION, native_call(&r, opaque));
                statement(&r, SCRIPT_STMT_EXPRESSION, native_call(&r, host));
                statement(&r, SCRIPT_STMT_EXPRESSION, target);
                system_body(&r, 0, 5);
                break;
            }
            case 21: {
                uint64_t a = component(&r, 0);
                int target = member(&r, component_expr(&r, a, false)),
                    value = expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, 0);
                reduction(&r, target, value);
                int root = expr(&r, SCRIPT_EXPR_CALL, -1, 1, 0);
                statement(&r, SCRIPT_STMT_EXPRESSION, root);
                int call2 = expr(&r, SCRIPT_EXPR_CALL, -1, 2, 0),
                    call1 = expr(&r, SCRIPT_EXPR_CALL, -1, 1, 0);
                int body1 = statement(&r, SCRIPT_STMT_EXPRESSION, call2),
                    body2 = statement(&r, SCRIPT_STMT_EXPRESSION, call1);
                int cap = capability(&r, SCRIPT_COMPONENT_ACCESS_READ, SCRIPT_STATE_ACCESS_NONE, a, 0);
                r.expressions[call1].write = r.capabilities[cap].id;
                system_body(&r, 0, 2);
                function(&r, body1, SCRIPT_FUNCTION_AUTHORED, -1, false);
                function(&r, body2, SCRIPT_FUNCTION_AUTHORED, -1, false);
                break;
            }
            case 22: {
                uint64_t a = component(&r, 0);
                int target = member(&r, component_expr(&r, a, false)),
                    value = expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, 0);
                reduction(&r, target, value);
                int task = expr(&r, SCRIPT_EXPR_CALL, -1, -1, 0);
                r.expressions[task].task = true;
                statement(&r, SCRIPT_STMT_EXPRESSION, task);
                statement(
                    &r,
                    SCRIPT_STMT_EXPRESSION,
                    expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, SCRIPT_EFFECT_SUSPEND)
                );
                system_body(&r, 0, 3);
                break;
            }
            case 23: {
                uint64_t a = component(&r, 0), b = component(&r, 1);
                r.types[0].name = long_name;
                int left = member(&r, component_expr(&r, a, false)),
                    right = member(&r, component_expr(&r, b, false)),
                    value = expr(&r, SCRIPT_EXPR_CONSTANT, -1, -1, 0);
                reduction(&r, left, value);
                reduction(&r, right, value);
                binding(&r, left, right);
                system_body(&r, 0, 2);
                break;
            }
        }
        REQUIRE(
            r.facts.expression_count <= (c == 11 ? 85 : 24) &&
            r.facts.statement_count <= 16 && r.facts.function_count <= 6 &&
            r.facts.symbol_count <= 8 && r.facts.scope_count <= 8 &&
            r.facts.query_count <= 2 && r.facts.binding_count <= 4
        );
        script_effect_result expected;
        REQUIRE(compare(&r.facts, c == 23 ? 0 : -1, &expected));
        /* Purity is inferred, so cases 6 and 8 report no declared-purity diagnostic. */
        const int count[] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 3, 1, 3,
                              1, 0, 1, 1, 1, 1, 1, 1, 2, 1, 1, 2 };
        REQUIRE(expected.diagnostic_count == count[c]);
        if (c == 1) {
            REQUIRE(expected.function_effects[0] == 63);
        }
        if (c == 2 || c == 3) {
            REQUIRE(expected.expression_effects[0] == SCRIPT_EFFECT_READ);
            REQUIRE(
                expected.expression_effects[1] ==
                (c == 2 ? SCRIPT_EFFECT_WRITE : SCRIPT_EFFECT_SUSPEND)
            );
        }
        if (c == 4) {
            REQUIRE(
                expected.expression_effects[2] ==
                    (SCRIPT_EFFECT_READ | SCRIPT_EFFECT_HOST) &&
                expected.expression_effects[3] ==
                    (SCRIPT_EFFECT_READ | SCRIPT_EFFECT_HOST | SCRIPT_EFFECT_SUSPEND)
            );
        }
        if (c == 5 || c == 6) {
            for (int i = 0; i < r.facts.function_count; i++) {
                REQUIRE(expected.function_effects[i] == SCRIPT_EFFECT_HOST);
            }
        }
        if (c == 7) {
            REQUIRE(
                expected.function_effects[0] == SCRIPT_EFFECT_READ &&
                expected.function_effects[1] == SCRIPT_EFFECT_READ
            );
        }
        if (c == 14) {
            REQUIRE(expected.diagnostics[0].range.start == r.statements[1].range.start);
        }
        if (c == 17) {
            REQUIRE(expected.diagnostics[0].range.start == r.statements[1].range.start);
        }
        if (c == 18 || c == 19) {
            REQUIRE(expected.diagnostics[0].range.start == r.statements[0].range.start);
        }
        if (c == 20) {
            for (int i = 0; i < 2; i++) {
                REQUIRE(strstr(expected.diagnostics[i].message, "opaque component accessor"));
            }
        }
        if (c == 23) {
            REQUIRE(
                strlen(expected.diagnostics[0].message) == 191 &&
                (unsigned char)expected.diagnostics[0].message[190] == 0xc3
            );
        }
    }
    memset(long_name, 'z', 240);
    REQUIRE(calls == 48);
    return true;
}
TEST_CASE(effect_analysis_binding_order_is_stable_and_cycles_keep_original)
{
    REQUIRE(prepared);
    const uint64_t A = UINT64_MAX - 1, B = UINT64_MAX - 2, C = UINT64_MAX - 3,
                   D = UINT64_MAX - 4;
    for (int c = 0; c < 6; c++) {
        raw_facts r;
        raw_init(&r);
        if (c == 1) {
            for (int i = 0; i < 3; i++) {
                binding(&r, -1, -1);
                r.writes[i] = A - (uint64_t)i;
                r.bindings[i].writes = &r.writes[i];
                r.bindings[i].write_count = 1;
            }
        }
        if (c == 2) {
            for (int i = 0; i < 3; i++) {
                binding(&r, -1, -1);
            }
            r.writes[0] = C;
            r.writes[1] = B;
            r.writes[2] = A;
            r.reads[0] = B;
            r.reads[1] = A;
            for (int i = 0; i < 3; i++) {
                r.bindings[i].writes = &r.writes[i];
                r.bindings[i].write_count = 1;
            }
            for (int i = 0; i < 2; i++) {
                r.bindings[i].reads = &r.reads[i];
                r.bindings[i].read_count = 1;
            }
        }
        if (c == 3) {
            for (int i = 0; i < 4; i++) {
                binding(&r, -1, -1);
            }
            r.writes[0] = D;
            r.writes[1] = B;
            r.writes[2] = C;
            r.writes[3] = A;
            r.reads[0] = B;
            r.reads[1] = C;
            r.reads[2] = A;
            for (int i = 0; i < 4; i++) {
                r.bindings[i].writes = &r.writes[i];
                r.bindings[i].write_count = 1;
            }
            r.bindings[0].reads = r.reads;
            r.bindings[0].read_count = 2;
            for (int i = 1; i < 3; i++) {
                r.bindings[i].reads = &r.reads[2];
                r.bindings[i].read_count = 1;
            }
        }
        if (c == 4) {
            binding(&r, -1, -1);
            r.reads[0] = A;
            r.writes[0] = A;
            r.bindings[0].reads = r.reads;
            r.bindings[0].read_count = 1;
            r.bindings[0].writes = r.writes;
            r.bindings[0].write_count = 1;
        }
        if (c == 5) {
            for (int i = 0; i < 3; i++) {
                binding(&r, -1, -1);
            }
            r.writes[0] = A;
            r.writes[1] = B;
            r.writes[2] = C;
            r.reads[0] = C;
            r.reads[1] = B;
            for (int i = 0; i < 3; i++) {
                r.bindings[i].writes = &r.writes[i];
                r.bindings[i].write_count = 1;
            }
            for (int i = 1; i < 3; i++) {
                r.bindings[i].reads = &r.reads[i - 1];
                r.bindings[i].read_count = 1;
            }
        }
        script_binding_order expected;
        REQUIRE(order(&r.facts, &expected));
        REQUIRE(expected.diagnostic_count == (c >= 4 ? 1 : 0));
        const int permutations[6][4] = { { 0 },       { 0, 1, 2 },
                                         { 2, 1, 0 }, { 3, 1, 2, 0 },
                                         { 0 },       { 0, 1, 2 } };
        for (int i = 0; i < expected.count; i++) {
            REQUIRE(expected.permutation[i] == permutations[c][i]);
        }
        if (c >= 4) {
            REQUIRE(
                expected.diagnostics[0].range.start == r.bindings[0].range.start &&
                !strcmp(expected.diagnostics[0].message, "Binding dependency cycle")
            );
        }
    }
    REQUIRE(calls == 60);
    return true;
}
TEST_CASE(effect_analysis_actual_final_facts_replay_without_seed_discovery_claim)
{
    REQUIRE(prepared);
    const char *sources[] = {
        "E :: system(position: ^Position , if Touch()) { Touch :: () -> bool { "
        "mutate(); return "
        "true; } }",
        "E :: system(position: ^Position , if Deferred()) { Deferred :: () -> "
        "bool { yield; return "
        "true; } }",
        "E :: system(position: ^Position) { reduce position.x += position.y; }",
        "E :: behavior { bind self.Position.x = self.Position.y; bind "
        "self.Position.y = self.Position.x; }",
        "E :: system(p: Position, amount := p.x * 2, if amount > 0) {}",
        "E :: system(p: Position, amount := saved) { saved: retain number = 2; "
        "}",
        "E :: system(p: Position, amount := Twice(p.x), if amount > 0) { Twice "
        ":: (value: number) -> number { return value * 2; } }",
        "E :: system(p: Position, amount := Read()) { Read :: () -> number { "
        "return p.x; } }",
        "E :: system(p: Position, amount := noise()) {}",
        "E :: system(p: Position, amount := Deferred()) { Deferred :: () -> "
        "number { yield; return 2; } }",
        "E :: library { Run :: (threshold: number) { q := query(p: Position, "
        "if "
        "p.x > threshold); } }",
        "E :: behavior { threshold: number = 2; Threshold :: () -> number { "
        "return threshold; } Run :: () { q := query(p: Position, if p.x > "
        "Threshold()); } }",
        "E :: behavior { threshold: number = 2; Run :: () { q := query(p: "
        "Position, amount := p.x + threshold, if amount > 0); } }",
        "E :: system(p: Position, amount := Touch()) { Touch :: () -> number { "
        "mutate(); return 1; } }",
        "E :: system(snapshot: Position, live: ^Position) { reduce live.x += "
        "1; }",
        "E :: system(live: ^Position, seen := live.x) { reduce live.x += 1; }",
        "E :: system(live: ^Position, alias := live, unused := live) { reduce "
        "alias.x += 1; }",
    };
    const bool valid[] = { false, false, false, false, true, false,
                           true,  false, false, false, true, false,
                           true,  false, false, false, true };
    enum
    {
        CASES = sizeof sources / sizeof *sources
    };
    script_catalog_field fields[] = {
        { .id = 20, .name = "x", .type = { .storage = PIN_F32 }, .writable = true },
        { .id = 21,
          .name = "y",
          .type = { .storage = PIN_F32 },
          .writable = true,
          .offset = sizeof(float) }
    };
    script_catalog_type type = { .id = 10,
                                 .name = "Position",
                                 .type = { .storage = PIN_STRUCT, .nominal = 10 },
                                 .fields = fields,
                                 .field_count = 2,
                                 .size = 2 * sizeof(float),
                                 .component = true,
                                 .component_id = 501 };
    const script_catalog_parameter noise_result = { .type = { .storage = PIN_F32 } };
    script_catalog_capability caps[] = {
        { .id = 32, .name = "mutate", .effects = SCRIPT_EFFECT_WRITE },
        { .id = 33,
          .name = "noise",
          .effects = SCRIPT_EFFECT_RANDOM,
          .results = &noise_result,
          .result_count = 1 },
    };
    script_catalog native = { .types = &type,
                              .type_count = 1,
                              .capabilities = caps,
                              .capability_count = 2,
                              .entity_type = 40 };
    for (int c = 0; c < CASES; c++) {
        script_analysis_free(analysis);
        analysis = NULL;
        script_syntax_free(syntax);
        syntax = NULL;
        REQUIRE(strlen(sources[c]) < 512);
        syntax = script_parse(sources[c]);
        if (syntax && syntax->diagnostic_count) {
            fprintf(
                stderr,
                "effect fact source %d: %s\n",
                c,
                syntax->diagnostics[0].message
            );
        }
        REQUIRE(syntax && !syntax->diagnostic_count);
        analysis = script_analyze_document(syntax, &native, "effect-fact-source");
        bool bounded = analysis && analysis->expression_count <= 64 &&
                       analysis->statement_count <= 64 &&
                       analysis->function_count <= 8 && analysis->symbol_count <= 32 &&
                       analysis->scope_count <= 16 && analysis->query_count <= 5 &&
                       analysis->binding_count <= 8 && analysis->valid == valid[c] &&
                       analysis->diagnostic_count <= 32 &&
                       native.type_count + analysis->type_count <= 4;
        if (!bounded && analysis) {
            fprintf(
                stderr,
                "effect fact case %d: valid=%d expected=%d expressions=%d "
                "statements=%d functions=%d symbols=%d scopes=%d queries=%d "
                "bindings=%d types=%d diagnostics=%d\n",
                c,
                analysis->valid,
                valid[c],
                analysis->expression_count,
                analysis->statement_count,
                analysis->function_count,
                analysis->symbol_count,
                analysis->scope_count,
                analysis->query_count,
                analysis->binding_count,
                native.type_count + analysis->type_count,
                analysis->diagnostic_count
            );
            for (int i = 0; i < analysis->diagnostic_count; i++) {
                fprintf(
                    stderr,
                    "effect fact diagnostic: %s\n",
                    analysis->diagnostics[i].message
                );
            }
        }
        REQUIRE(bounded);
        script_catalog_type types[4];
        types[0] = type;
        for (int i = 0; i < analysis->type_count; i++) {
            types[1 + i] = analysis->types[i];
        }
        script_catalog catalog = native;
        catalog.types = types;
        catalog.type_count = 1 + analysis->type_count;
        script_effect_facts facts = { &catalog,
                                      analysis->expressions,
                                      analysis->expression_count,
                                      analysis->statements,
                                      analysis->statement_count,
                                      analysis->functions,
                                      analysis->function_count,
                                      analysis->symbols,
                                      analysis->symbol_count,
                                      analysis->scopes,
                                      analysis->scope_count,
                                      analysis->queries,
                                      analysis->query_count,
                                      analysis->bindings,
                                      analysis->binding_count,
                                      analysis->descriptions,
                                      analysis->description_count };
        const script_diagnostic *diagnostics;
        int count;
        if (c == 3) {
            script_binding_order expected;
            REQUIRE(order(&facts, &expected));
            diagnostics = expected.diagnostics;
            count = expected.diagnostic_count;
        } else {
            script_effect_result expected;
            REQUIRE(compare(&facts, c == 2 ? 1 : -1, &expected));
            diagnostics = expected.diagnostics;
            count = expected.diagnostic_count;
            for (int i = 0; i < analysis->expression_count; i++) {
                REQUIRE(
                    expected.expression_effects[i] == analysis->expressions[i].effects
                );
            }
            for (int i = 0; i < analysis->statement_count; i++) {
                REQUIRE(expected.statement_effects[i] == analysis->statements[i].effects);
            }
            for (int i = 0; i < analysis->function_count; i++) {
                REQUIRE(expected.function_effects[i] == analysis->functions[i].effects);
            }
        }
        REQUIRE(valid[c] ? count == 0 : count > 0);
        if (c == 14 || c == 15) {
            REQUIRE(
                count == 1 &&
                !strcmp(
                    diagnostics[0].message,
                    "Reduction producer cannot read or write component "
                    "'Position' outside reductions"
                )
            );
        }
        int next = 0;
        for (int i = 0; i < count; i++) {
            while (next < analysis->diagnostic_count &&
                   !equal_diagnostic(&diagnostics[i], &analysis->diagnostics[next])) {
                next++;
            }
            REQUIRE(next < analysis->diagnostic_count);
            next++;
        }
    }
    REQUIRE(calls == 60 + 2 * CASES);
    return true;
}
TEST_CASE(effect_analysis_original_oracle_and_detached_results_outlive_inputs)
{
    cleanup();
    compiler_fixture_free(&fixture);
    REQUIRE(calls == 94);
    for (int i = 0; i < 2; i++) {
        const script_effect_result *e = &saved_expected[i];
        REQUIRE(
            e->expression_count > 0 && e->statement_count > 0 &&
            e->function_count > 0 && e->diagnostic_count > 0 &&
            arena_contains(&saved_oracle[i], e->expression_effects) &&
            arena_contains(&saved_oracle[i], e->statement_effects) &&
            arena_contains(&saved_oracle[i], e->function_effects) &&
            arena_contains(&saved_oracle[i], e->diagnostics)
        );
        for (int b = 0; b < 2; b++) {
            REQUIRE(
                saved[b][i].type == PIN_UNION && saved[b][i].ptr &&
                owned(&saved_storage[b], &saved[b][i], 0) &&
                result_value(&saved[b][i], saved_tags[b], e)
            );
        }
    }
    return true;
}
void test_language_effect_analysis_chibi()
{
    prepared = false;
    calls = 0;
    pu_setup(NULL, cleanup);
    RUN_TEST_CASE(effect_analysis_raw_seeds_dependencies_and_restrictions_match);
    RUN_TEST_CASE(effect_analysis_binding_order_is_stable_and_cycles_keep_original);
    RUN_TEST_CASE(effect_analysis_actual_final_facts_replay_without_seed_discovery_claim);
    RUN_TEST_CASE(effect_analysis_original_oracle_and_detached_results_outlive_inputs);
    pu_clear_setup();
    compiler_fixture_free(&fixture);
    for (int i = 0; i < 2; i++) {
        arena_free(&saved_storage[i]);
        arena_free(&saved_oracle[i]);
    }
    memset(saved, 0, sizeof saved);
    memset(saved_expected, 0, sizeof saved_expected);
}
