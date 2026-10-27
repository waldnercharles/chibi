#include "signature_fixture.h"

#include "support/array.h"
#include "third_party/pico_headers/pico_unit.h"
#include "types/array.h"
#include "types/text.h"
#include "types/union.h"

#include <stdlib.h>
#include <string.h>

enum
{
    TYPES = 64,
    IDS = 128,
    SYMBOLS = 32,
    ROWS = 8,
    SPANS = 16
};
static const char NAMED[] = "project:named_binding",
                  LEXICAL[] = "project:lexical_binding",
                  CONSTRUCTORS[] = "project:type_constructors",
                  READER[] = "project:signature_reader";
typedef struct
{
    const compiler_fixture *fixture;
    int backend;
    arena *storage;
    script_type types[TYPES];
    uint64_t ids[IDS];
    int type_count, id_count;
} packing;
static pin_value integer(int n)
{
    return (pin_value){ .type = PIN_I32, .i = n };
}
static pin_value boolean(bool n)
{
    return (pin_value){ .type = PIN_BOOL, .b = n };
}
static pin_value text(packing *p, const char *s)
{
    s = s ? s : "";
    pin_value result;
    const char *error = pin_value_text(p->storage, &result, s, strlen(s));
    if (error) {
        abort();
    }
    return result;
}
static int bits(unsigned value)
{
    int result;
    _Static_assert(sizeof value == sizeof result, "flags width");
    memcpy(&result, &value, sizeof result);
    return result;
}
static bool put(packing *p, pin_value *row, const char *name, pin_value value)
{
    const type_field *field = pin_type_field_path(pin_value_type(row), name);
    REQUIRE(field && field->type == value.type);
    REQUIRE(!pin_write_field(p->storage, row->ptr, field, value));
    return true;
}
static bool range(packing *p, pin_value *row, script_range r)
{
    REQUIRE(
        put(p, row, "start", integer(r.start)) &&
        put(p, row, "end", integer(r.end)) && put(p, row, "line", integer(r.line)) &&
        put(p, row, "column", integer(r.column)) &&
        put(p, row, "document", integer(r.document))
    );
    return true;
}
static bool rows(packing *p, const char *package, const char *name, int count, pin_value *out)
{
    REQUIRE(count >= 0 && count <= TYPES);
    const pin_record *shape = compiler_fixture_record(p->fixture, p->backend, package, name, p->storage);
    REQUIRE(shape);
    pin_array *
        array = pin_array_snapshot_allocate(p->storage, PIN_STRUCT, shape->type.name, count);
    REQUIRE(array);
    array->record = shape;
    *out = (pin_value){ .type = PIN_STRUCT,
                        .container = PIN_CONTAINER_ARRAY,
                        .record = shape,
                        .type_name = shape->type.name,
                        .ptr = array };
    for (int i = 0; i < count; i++) {
        array->items[i] = (pin_value){
            .type = PIN_STRUCT,
            .record = shape,
            .type_name = shape->type.name,
            .ptr = arena_alloc(p->storage, shape->type.size)
        };
        memset(array->items[i].ptr, 0, shape->type.size);
    }
    return true;
}
static pin_value array(packing *p, pin_type type, int count)
{
    return (pin_value){ .type = type,
                        .container = PIN_CONTAINER_ARRAY,
                        .ptr = pin_array_snapshot_allocate(p->storage, type, NULL, count) };
}
static bool tagged(
    packing *p,
    const char *package,
    const char *name,
    const char *member,
    pin_value *fields,
    int count,
    pin_value *out
)
{
    const authoring_package *owner = compiler_fixture_package(p->fixture, p->backend, package);
    REQUIRE(owner && owner->exports && owner->exports->metadata);
    const script_catalog_type *type = script_catalog_type_by_id(
        owner->exports->metadata,
        compiler_fixture_export(p->fixture, p->backend, package, name)
    );
    REQUIRE(type);
    const script_catalog_variant *tag = NULL;
    for (int i = 0; i < type->variant_count; i++) {
        if (!strcmp(type->variants[i].name, member)) {
            tag = &type->variants[i];
            break;
        }
    }
    REQUIRE(tag && tag->field_count == count);
    REQUIRE(!compiler_fixture_union(p->storage, tag->tag, fields, count, out));
    return true;
}
static int slot(packing *p, uint64_t id)
{
    if (!id) {
        return -1;
    }
    for (int i = 0; i < p->id_count; i++) {
        if (p->ids[i] == id) {
            return i;
        }
    }
    if (p->id_count == IDS) {
        return -2;
    }
    p->ids[p->id_count] = id;
    return p->id_count++;
}
static int type_slot(packing *p, script_type type)
{
    for (int i = 0; i < p->type_count; i++) {
        script_type t = p->types[i];
        if (t.storage == type.storage && t.container == type.container &&
            t.flags == type.flags && t.nominal == type.nominal &&
            t.parameter == type.parameter && t.collection == type.collection) {
            return i;
        }
    }
    if (p->type_count == TYPES) {
        return -1;
    }
    p->types[p->type_count] = type;
    return p->type_count++;
}
static bool lexical(packing *p, const script_signature_facts *facts, int scope_count, pin_value *out)
{
    const script_lexical_facts *f = &facts->named.lexical;
    pin_value v[5];
    REQUIRE(
        rows(p, LEXICAL, "Scope", scope_count, &v[0]) &&
        rows(p, LEXICAL, "Symbol", f->symbol_count, &v[1]) &&
        rows(p, LEXICAL, "Function", facts->function_count, &v[2])
    );
    v[3] = array(p, PIN_U64, f->symbol_count);
    v[4] = array(p, PIN_STR, facts->named.document_count);
    pin_array *scopes = v[0].ptr, *symbols = v[1].ptr, *functions = v[2].ptr,
              *ids = v[3].ptr, *keys = v[4].ptr;
    for (int i = 0; i < scope_count; i++) {
        REQUIRE(
            put(p, &scopes->items[i], "parent", integer(f->scopes[i].parent)) &&
            put(p, &scopes->items[i], "function", integer(f->scopes[i].function)) &&
            range(p, &scopes->items[i], f->scopes[i].range)
        );
    }
    for (int i = 0; i < f->symbol_count; i++) {
        const script_symbol *s = &f->symbols[i];
        REQUIRE(s->name);
        REQUIRE(
            put(p, &symbols->items[i], "name", text(p, s->name)) &&
            put(p, &symbols->items[i], "kind", integer(s->kind)) &&
            put(p, &symbols->items[i], "scope", integer(s->scope)) &&
            put(p, &symbols->items[i], "id_slot", integer(i)) &&
            range(p, &symbols->items[i], s->declaration)
        );
        ids->items[i] = (pin_value){ .type = PIN_U64, .u64 = s->id };
    }
    for (int i = 0; i < facts->function_count; i++) {
        REQUIRE(
            put(p, &functions->items[i], "symbol", integer(facts->functions[i].symbol)) &&
            put(p, &functions->items[i], "scope", integer(facts->functions[i].scope))
        );
    }
    for (int i = 0; i < facts->named.document_count; i++) {
        REQUIRE(f->documents[i].key);
        keys->items[i] = text(p, f->documents[i].key);
    }
    REQUIRE(tagged(p, LEXICAL, "Environment", "Tables", v, 5, out));
    return true;
}
static bool entries(packing *p, const script_catalog_type *types, int count, pin_value *out)
{
    REQUIRE(count >= 0 && count <= ROWS && rows(p, NAMED, "Entry", count, out));
    pin_array *rows = out->ptr;
    for (int i = 0; i < count; i++) {
        const script_catalog_type *t = &types[i];
        pin_value *row = &rows->items[i];
        int id = slot(p, t->id), type = type_slot(p, t->type);
        REQUIRE(id >= -1 && type >= 0 && t->name);
        REQUIRE(
            put(p, row, "name", text(p, t->name)) &&
            put(p, row, "source_name", text(p, t->source_name)) &&
            put(p, row, "key", text(p, t->key)) &&
            put(p, row, "id_slot", integer(id)) && put(p, row, "type", integer(type)) &&
            put(p, row, "has_source_name", boolean(t->source_name != NULL)) &&
            put(p, row, "has_key", boolean(t->key != NULL)) &&
            put(p, row, "internal", boolean(t->internal)) &&
            put(p, row, "component", boolean(t->component))
        );
    }
    return true;
}
static bool names(packing *p, const script_signature_facts *facts, int scope_count, pin_value v[15])
{
    const script_named_facts *f = &facts->named;
    REQUIRE(lexical(p, facts, scope_count, &v[0]));
    REQUIRE(
        entries(p, f->catalog->types, f->catalog->type_count, &v[3]) &&
        entries(p, f->own_types, f->own_type_count, &v[4]) &&
        entries(p, f->native ? f->native->types : NULL, f->native ? f->native->type_count : 0, &v[5])
    );
    v[6] = array(p, PIN_I32, f->lexical.symbol_count);
    pin_array *symbol_types = v[6].ptr;
    for (int i = 0; i < f->lexical.symbol_count; i++) {
        int type = type_slot(p, f->lexical.symbols[i].type);
        REQUIRE(type >= 0);
        symbol_types->items[i] = integer(type);
    }
    int export_count = 0;
    for (int i = 0; i < f->import_count; i++) {
        if (f->imports[i].exports) {
            REQUIRE(
                f->imports[i].exports->member_count >= 0 &&
                f->imports[i].exports->member_count <= ROWS
            );
            export_count += f->imports[i].exports->member_count;
        }
    }
    REQUIRE(export_count <= ROWS);
    REQUIRE(
        rows(p, NAMED, "Import", f->import_count, &v[7]) &&
        rows(p, NAMED, "Export", export_count, &v[8]) &&
        rows(p, NAMED, "Use", f->use_count, &v[9])
    );
    pin_array *imports = v[7].ptr, *exports = v[8].ptr, *uses = v[9].ptr;
    int at = 0;
    for (int i = 0; i < f->import_count; i++) {
        const script_package_import *t = &f->imports[i];
        pin_value *row = &imports->items[i];
        int count = t->exports ? t->exports->member_count : 0;
        REQUIRE(t->identity);
        REQUIRE(
            put(p, row, "alias", text(p, t->alias)) &&
            put(p, row, "identity", text(p, t->identity)) &&
            put(p, row, "has_alias", boolean(t->alias != NULL)) &&
            put(p, row, "document", integer(t->document)) &&
            put(p, row, "export_start", integer(at)) &&
            put(p, row, "export_count", integer(count))
        );
        for (int j = 0; j < count; j++) {
            const script_package_export *e = &t->exports->members[j];
            int id = slot(p, e->id);
            REQUIRE(id >= -1 && e->name);
            REQUIRE(
                put(p, &exports->items[at], "name", text(p, e->name)) &&
                put(p, &exports->items[at], "kind", integer(e->kind)) &&
                put(p, &exports->items[at], "id_slot", integer(id))
            );
            at++;
        }
    }
    for (int i = 0; i < f->use_count; i++) {
        const script_use *u = &f->uses[i];
        int member = slot(p, u->member);
        REQUIRE(member >= -1);
        REQUIRE(
            put(p, &uses->items[i], "kind", integer(u->kind)) &&
            put(p, &uses->items[i], "symbol", integer(u->symbol)) &&
            put(p, &uses->items[i], "member_slot", integer(member)) &&
            range(p, &uses->items[i], u->range)
        );
    }
    v[10] = text(p, f->lexical.identity);
    v[11] = integer(f->package_scope);
    v[12] = boolean(f->native != NULL);
    v[13] = boolean(f->document_count != 0);
    v[14] = (pin_value){ .type = PIN_U64, .u64 = f->catalog->entity_type };
    return true;
}
static bool constructors(packing *p, const script_signature_facts *facts, pin_value v[8])
{
    const script_catalog *catalog = facts->named.catalog;
    int variant_count = 0, result_count = 0;
    for (int i = 0; i < catalog->type_count; i++) {
        REQUIRE(catalog->types[i].variant_count >= 0 && catalog->types[i].variant_count <= ROWS);
        variant_count += catalog->types[i].variant_count;
    }
    for (int i = 0; i < facts->function_count; i++) {
        result_count += acount(facts->functions[i].results);
    }
    REQUIRE(variant_count <= ROWS && result_count <= SPANS);
    REQUIRE(
        rows(p, CONSTRUCTORS, "Definition", catalog->type_count, &v[2]) &&
        rows(p, CONSTRUCTORS, "RawVariant", variant_count, &v[3]) &&
        rows(p, CONSTRUCTORS, "Capability", catalog->capability_count, &v[4]) &&
        rows(p, CONSTRUCTORS, "Function", facts->function_count, &v[5]) &&
        rows(p, CONSTRUCTORS, "Symbol", facts->named.lexical.symbol_count, &v[6])
    );
    v[7] = array(p, PIN_I32, result_count);
    pin_array *definitions = v[2].ptr, *variants = v[3].ptr,
              *capabilities = v[4].ptr, *functions = v[5].ptr,
              *symbols = v[6].ptr, *results = v[7].ptr;
    int at = 0;
    for (int i = 0; i < catalog->type_count; i++) {
        const script_catalog_type *t = &catalog->types[i];
        int id = slot(p, t->id);
        REQUIRE(id >= -1);
        REQUIRE(
            put(p, &definitions->items[i], "id_slot", integer(id)) &&
            put(p, &definitions->items[i], "variant_start", integer(at)) &&
            put(p, &definitions->items[i], "variant_count", integer(t->variant_count)) &&
            put(p,
                &definitions->items[i],
                "element_type",
                integer(t->type.storage == PIN_POINTER ? type_slot(p, t->element_type) : -1))
        );
        for (int j = 0; j < t->variant_count; j++) {
            int tag = slot(p, t->variants[j].tag),
                type = type_slot(p, t->variants[j].type);
            REQUIRE(tag >= -1 && type >= 0);
            REQUIRE(
                put(p, &variants->items[at], "tag_slot", integer(tag)) &&
                put(p, &variants->items[at], "type", integer(type))
            );
            at++;
        }
    }
    for (int i = 0; i < catalog->capability_count; i++) {
        int id = slot(p, catalog->capabilities[i].id);
        REQUIRE(
            id >= -1 && catalog->capabilities[i].result_count >= 0 &&
            catalog->capabilities[i].result_count <= SPANS
        );
        REQUIRE(
            put(p, &capabilities->items[i], "id_slot", integer(id)) &&
            put(p,
                &capabilities->items[i],
                "result_count",
                integer(catalog->capabilities[i].result_count))
        );
    }
    at = 0;
    for (int i = 0; i < facts->function_count; i++) {
        const script_function *f = &facts->functions[i];
        REQUIRE(f->symbol >= -1 && f->symbol < facts->named.lexical.symbol_count);
        REQUIRE(
            put(p, &functions->items[i], "symbol", integer(f->symbol)) &&
            put(p, &functions->items[i], "result_start", integer(at)) &&
            put(p, &functions->items[i], "result_count", integer(acount(f->results)))
        );
        for (int j = 0; j < acount(f->results); j++) {
            REQUIRE(f->results[j] >= 0 && f->results[j] < facts->named.lexical.symbol_count);
            results->items[at++] = integer(f->results[j]);
        }
    }
    for (int i = 0; i < facts->named.lexical.symbol_count; i++) {
        int id = slot(p, facts->named.lexical.symbols[i].id);
        REQUIRE(id >= -1);
        REQUIRE(put(p, &symbols->items[i], "id_slot", integer(id)));
    }
    return true;
}
static bool pools(packing *p, pin_value *types, pin_value *ids)
{
    REQUIRE(rows(p, NAMED, "Type", p->type_count, types));
    pin_array *rows = types->ptr;
    for (int i = 0; i < p->type_count; i++) {
        script_type t = p->types[i];
        int nominal = slot(p, t.nominal), parameter = slot(p, t.parameter);
        REQUIRE(nominal >= -1 && parameter >= -1);
        REQUIRE(
            put(p, &rows->items[i], "storage", integer(t.storage)) &&
            put(p, &rows->items[i], "container", integer(t.container)) &&
            put(p, &rows->items[i], "flags", integer(bits(t.flags))) &&
            put(p, &rows->items[i], "nominal_slot", integer(nominal)) &&
            put(p, &rows->items[i], "parameter_slot", integer(parameter)) &&
            put(p, &rows->items[i], "collection", (pin_value){ .type = PIN_U64, .u64 = t.collection })
        );
    }
    *ids = array(p, PIN_U64, p->id_count);
    pin_array *values = ids->ptr;
    for (int i = 0; i < p->id_count; i++) {
        values->items[i] = (pin_value){ .type = PIN_U64, .u64 = p->ids[i] };
    }
    return true;
}
bool signature_fixture_environment(
    const compiler_fixture *fixture,
    int backend,
    const script_signature_facts *facts,
    int scope_count,
    arena *storage,
    pin_value *out
)
{
    REQUIRE(
        fixture && backend >= 0 && backend < 2 && fixture->owners[backend] &&
        authoring_packages_valid(fixture->owners[backend])
    );
    REQUIRE(
        facts && facts->named.catalog && storage && out && scope_count >= 0 &&
        scope_count <= ROWS
    );
    REQUIRE(
        facts->named.lexical.symbol_count >= 0 &&
        facts->named.lexical.symbol_count <= SYMBOLS &&
        facts->function_count >= 0 && facts->function_count <= ROWS
    );
    REQUIRE(
        facts->named.document_count >= 0 && facts->named.document_count <= ROWS &&
        facts->named.import_count >= 0 && facts->named.import_count <= ROWS &&
        facts->named.use_count >= 0 && facts->named.use_count <= SPANS
    );
    REQUIRE(
        facts->named.catalog->type_count >= 0 &&
        facts->named.catalog->type_count <= ROWS &&
        facts->named.catalog->capability_count >= 0 &&
        facts->named.catalog->capability_count <= ROWS
    );
    REQUIRE(facts->pointer_size <= INT32_MAX);
    packing p = { .fixture = fixture, .backend = backend, .storage = storage };
    pin_value n[15], c[8], fields[3];
    REQUIRE(
        names(&p, facts, scope_count, n) && constructors(&p, facts, c) &&
        pools(&p, &n[1], &n[2])
    );
    c[0] = n[1];
    c[1] = n[2];
    REQUIRE(
        tagged(&p, NAMED, "Environment", "Tables", n, 15, &fields[0]) &&
        tagged(&p, CONSTRUCTORS, "Environment", "Tables", c, 8, &fields[1])
    );
    fields[2] = integer((int)facts->pointer_size);
    REQUIRE(tagged(&p, READER, "Environment", "Tables", fields, 3, out));
    return true;
}
bool signature_fixture_tree(
    const compiler_fixture *fixture,
    int backend,
    const compiler_fixture_node *nodes,
    int count,
    const char *source,
    arena *storage,
    pin_value *out
)
{
    REQUIRE(
        fixture && backend >= 0 && backend < 2 && fixture->owners[backend] &&
        authoring_packages_valid(fixture->owners[backend]) && nodes &&
        count > 0 && source && storage && out
    );
    packing p = { .fixture = fixture, .backend = backend, .storage = storage };
    pin_value fields[5];
    REQUIRE(
        compiler_fixture_pack_nodes(fixture, backend, nodes, count, storage, &fields[0], &fields[1])
    );
    fields[2] = array(&p, PIN_BOOL, count);
    fields[3] = array(&p, PIN_BOOL, count);
    fields[4] = text(&p, source);
    pin_array *named = fields[2].ptr, *extra = fields[3].ptr;
    for (int i = 0; i < count; i++) {
        named->items[i] = boolean(ts_node_is_named(nodes[i].node));
        extra->items[i] = boolean(ts_node_is_extra(nodes[i].node));
    }
    REQUIRE(tagged(&p, READER, "Tree", "Raw", fields, 5, out));
    return true;
}
