#include "execution/prepared_internal.h"
#include "support/array.h"
#include "types/array.h"
#include "types/catalog.h"
#include "types/record.h"
#include "types/schema.h"

#include <stdlib.h>
#include <string.h>

/* Computational work is separate from encoded records and owned bytes. */
enum
{
    PREPARED_RECORD_WORK = 64 * 1024 * 1024
};
struct prepared_record_cache
{
    size_t name_bytes, validation_work, hash_work;
    union
    {
        arena *owner;
        /* A canonical or function-owned alias proved within this write only. */
        const pin_record *validated_alias;
    };
    const pin_record *owned;
    /* The type at this position in name order, and in id order. */
    int by_name, by_id;
    bool authored;
    type_field *fields;
    int *children;
    int count;
    const type_desc *linked;
};
static int name_position(prepared_context *, const char *, size_t, int, bool *);
static bool work(prepared_context *c, size_t amount)
{
    size_t limit = c->record_work_limit ? c->record_work_limit : PREPARED_RECORD_WORK;
    if (c->wire.error) {
        return false;
    }
    size_t *used = c->shared_record_work ? c->shared_record_work : &c->record_work;
    if (*used > limit || amount > limit - *used) {
        c->wire.error = "prepared authored layout work limit exceeded";
        return false;
    }
    *used += amount;
    return true;
}
static size_t name_bytes(prepared_context *c, const char *name)
{
    size_t limit = c->record_work_limit ? c->record_work_limit : PREPARED_RECORD_WORK;
    size_t used = c->shared_record_work ? *c->shared_record_work : c->record_work;
    if (!name || c->wire.error || used > limit) {
        return 0;
    }
    size_t left = limit - used;
    size_t bytes = strnlen(name, left + 1);
    if (bytes > left || !work(c, bytes + 1)) {
        c->wire.error = "prepared authored layout work limit exceeded";
        return 0;
    }
    return bytes + 1;
}
static size_t validation_work(prepared_context *c, int count, size_t names)
{
    if (count <= 0 || (size_t)count > EXECUTION_WIRE_MAX_RECORDS ||
        names > SIZE_MAX - (size_t)count - 1 ||
        names + (size_t)count + 1 > SIZE_MAX / ((size_t)count + 8)) {
        c->wire.error = "invalid prepared authored layout size";
        return 0;
    }
    return ((size_t)count + 8) * (names + (size_t)count + 1);
}
static size_t validate(prepared_context *c, const pin_record *record, bool current)
{
    if (!record || record->bytes < sizeof *record ||
        record->type.fields != (const type_field *)(record + 1) ||
        record->type.field_count <= 0 ||
        (size_t)record->type.field_count > EXECUTION_WIRE_MAX_RECORDS ||
        (size_t)record->type.field_count >
            (record->bytes - sizeof *record) / sizeof(type_field) ||
        !work(c, (size_t)record->type.field_count)) {
        if (!c->wire.error) {
            c->wire.error = "invalid prepared authored layout";
        }
        return 0;
    }
    size_t names = name_bytes(c, record->type.name);
    for (int i = 0; i < record->type.field_count && !c->wire.error; i++) {
        size_t next = name_bytes(c, record->type.fields[i].name);
        if (!next || next > SIZE_MAX - names) {
            c->wire.error = "invalid prepared authored field name";
            return 0;
        }
        names += next;
        const char *extra[] = { record->type.fields[i].type_name,
                                record->type.fields[i].unit };
        for (int j = 0; j < 2; j++) {
            if (!extra[j]) {
                continue;
            }
            next = name_bytes(c, extra[j]);
            if (!next || next > SIZE_MAX - names) {
                c->wire.error = "invalid prepared authored field metadata";
                return 0;
            }
            names += next;
        }
    }
    size_t cost = validation_work(c, record->type.field_count, names);
    size_t root_bytes = sizeof *record +
                        (size_t)record->type.field_count * sizeof(type_field);
    if (names > SIZE_MAX - root_bytes) {
        c->wire.error = "invalid prepared authored layout size";
        return 0;
    }
    root_bytes += names;
    /* Root fields and names are already covered by cost. Frozen closures can
     * additionally own child descriptors; only those add traversal work. */
    size_t children = record->frozen && record->bytes > root_bytes
                        ? record->bytes - root_bytes
                        : 0;
    if (!cost || !work(c, cost) || !work(c, children) ||
        !(current ? pin_record_fields_current(record) : pin_record_valid(record))) {
        if (!c->wire.error) {
            c->wire.error = "prepared authored nested layout is retired or "
                            "incompatible";
        }
        return 0;
    }
    return cost;
}
static const pin_record *copy_record(prepared_context *c, const pin_record *source, size_t cost)
{
    if (!work(c, cost)) {
        return NULL;
    }
    void *memory = execution_wire_allocate(&c->wire, source->bytes);
    if (!memory) {
        return NULL;
    }
    const pin_record *record = pin_record_init(
        memory,
        source->bytes,
        source->type.name,
        source->id,
        source->type.fields,
        source->type.field_count
    );
    if (!record) {
        c->wire.error = "invalid prepared authored layout copy";
    }
    return record;
}
static int field_requirement(prepared_context *c, const type_field *field)
{
    if (field->type != PIN_STRUCT) {
        return -1;
    }
    size_t bytes = name_bytes(c, field->type_name);
    if (field->type_name && !c->wire.error) {
        bool found;
        int at = name_position(c, field->type_name, bytes, c->plan->type_count, &found);
        if (found && !c->wire.error) {
            int i = c->records[at].by_name;
            const execution_type_requirement *type = &c->plan->types[i];
            const type_desc *layout = type_field_type(field, (type_resolver){ 0 });
            if (type->storage != PIN_STRUCT) {
                c->wire
                    .error = "prepared child layout lacks a type requirement";
                return -1;
            }
            if (!layout || layout->size != type->size ||
                type_layout_hash(layout) != type->layout) {
                c->wire.error = "prepared child layout differs from its "
                                "requirement";
                return -1;
            }
            const pin_record *child = layout->carrier;
            bool authored = child && !child->native;
            if (authored != (type->record != NULL) ||
                (authored && !pin_record_same(child, type->record))) {
                c->wire.error = "prepared child identity differs from its "
                                "requirement";
                return -1;
            }
            return i;
        }
    }
    if (!c->wire.error) {
        c->wire.error = "prepared child layout lacks a type requirement";
    }
    return -1;
}
static void record_type(prepared_context *c, execution_type_requirement *v, int index)
{
    execution_wire *w = &c->wire;
    struct prepared_record_cache *cache = &c->records[index];
    if (!w->reading && v->record && v->record->native) {
        w->error = "native type requirements use linked descriptors";
        return;
    }
    bool authored = !w->reading && v->record;
    execution_wire_bool(w, &authored);
    cache->authored = authored;
    if (w->error) {
        return;
    }
    if ((v->storage == PIN_POINTER || v->storage == PIN_QUERY)) {
        if (authored || v->size != sizeof(void *)) {
            w->error = "invalid prepared pointer type";
        }
        return;
    }
    if (!authored) {
        if (!w->reading) {
            return;
        }
        execution_prepared_type actual = { 0 };
        if (!c->linker || !c->linker->type ||
            !c->linker->type(c->linker->user, v->id, &actual) || !actual.name ||
            strcmp(actual.name, v->name) || actual.storage != v->storage ||
            actual.size != v->size || actual.layout != v->layout) {
            w->error = "missing or incompatible type requirement";
            return;
        }
        if (v->storage == PIN_STRUCT) {
            const type_desc *t = actual.type ? actual.type
                                             : type_catalog_find(v->name);
            if (!t || !t->name || strcmp(t->name, v->name) ||
                t->size != v->size || type_layout_hash(t) != v->layout) {
                w->error = "linked type catalog layout differs";
            }
            cache->linked = t;
        }
        return;
    }
    if (v->storage != PIN_STRUCT) {
        w->error = "prepared authored requirement must be a struct";
        return;
    }
    if (!w->reading) {
        cache->validation_work = validate(c, v->record, true);
        if (w->error) {
            return;
        }
        if (v->id != v->record->id || v->layout != v->record->layout ||
            v->size != v->record->type.size || strcmp(v->name, v->record->type.name)) {
            w->error = "prepared authored requirement differs from its layout";
            return;
        }
    }
    int count = w->reading ? 0 : v->record->type.field_count;
    execution_wire_count(w, &count, 2);
    if (w->error) {
        return;
    }
    if (count <= 0 ||
        (size_t)count > (SIZE_MAX - sizeof(pin_record)) / sizeof(type_field)) {
        w->error = "invalid prepared authored field count";
        return;
    }
    type_field *fields = NULL;
    int *children = NULL;
    if (w->reading) {
        fields = execution_wire_allocate(w, (size_t)count * sizeof *fields);
        children = execution_wire_allocate(w, (size_t)count * sizeof *children);
        if (!fields || !children) {
            return;
        }
        memset(fields, 0, (size_t)count * sizeof *fields);
    }
    size_t names = cache->name_bytes;
    for (int i = 0; i < count && !w->error; i++) {
        const char *name = w->reading ? NULL : v->record->type.fields[i].name;
        const char *type_name = w->reading ? NULL : v->record->type.fields[i].type_name;
        const char *unit = w->reading ? NULL : v->record->type.fields[i].unit;
        int kind = w->reading ? 0 : v->record->type.fields[i].type;
        execution_wire_string(w, &name);
        execution_wire_int(w, &kind);
        execution_wire_string(w, &type_name);
        execution_wire_string(w, &unit);
        bool text_descriptor = w->reading ? false : v->record->type.fields[i].text_descriptor;
        execution_wire_bool(w, &text_descriptor);
        int container = w->reading ? 0 : v->record->type.fields[i].container;
        int child = w->reading ? -1
                               : field_requirement(c, &v->record->type.fields[i]);
        execution_wire_int(w, &container);
        execution_wire_int(w, &child);
        if (w->error) {
            break;
        }
        if ((container != PIN_CONTAINER_SINGLE && container != PIN_CONTAINER_ARRAY) ||
            text_descriptor != (kind == PIN_STR && container == PIN_CONTAINER_SINGLE) ||
            (kind == PIN_STRUCT ? child < 0 : child != -1)) {
            w->error = "invalid prepared authored field representation";
        }
        if (w->error) {
            break;
        }
        size_t bytes = name_bytes(c, name);
        if (!name || !*name || !bytes || bytes > SIZE_MAX - names ||
            !((kind >= PIN_BOOL && kind <= PIN_ENUM) || kind == PIN_POINTER ||
              kind == PIN_QUERY || kind == PIN_UNION)) {
            w->error = "invalid prepared authored field";
            break;
        }
        names += bytes;
        const char *extra[] = { type_name, unit };
        for (int j = 0; j < 2; j++) {
            if (!extra[j]) {
                continue;
            }
            bytes = name_bytes(c, extra[j]);
            if (!bytes || bytes > SIZE_MAX - names) {
                w->error = "invalid prepared authored field metadata";
                break;
            }
            names += bytes;
        }
        if (w->reading) {
            fields[i] = (type_field){ .name = name,
                                      .type = kind,
                                      .type_name = type_name,
                                      .unit = unit,
                                      .text_descriptor = text_descriptor,
                                      .container = container };
            children[i] = child;
        }
    }
    if (w->error) {
        return;
    }
    if (!w->reading) {
        cache->hash_work = 8 * (names + (size_t)count + 1);
        for (int i = 0; i < count; i++) {
            if (v->record->type.fields[i].type == PIN_STRUCT) {
                cache->hash_work = cache->validation_work;
                break;
            }
        }
        if (work(c, sizeof cache->validated_alias)) {
            cache->validated_alias = v->record;
        }
        return;
    }
    cache->validation_work = validation_work(c, count, names);
    if (w->error) {
        return;
    }
    /* Flat headers hash only fixed field metadata and names. Duplicate-name
     * validation is quadratic, but only construction/copy and the final
     * verifier perform it. Nested native layouts retain the existing charge:
     * their hash follows catalog descriptors beyond these owned field names. */
    cache->hash_work = 8 * (names + (size_t)count + 1);
    for (int i = 0; i < count; i++) {
        if (fields[i].type == PIN_STRUCT) {
            cache->hash_work = cache->validation_work;
            break;
        }
    }
    cache->fields = fields;
    cache->children = children;
    cache->count = count;
}
/* Type requirements may be listed before their children. Resolve complete
 * acyclic layouts after every header is known, without catalog lookup. */
static void materialize_records(prepared_context *c)
{
    execution_module_plan *p = c->plan;
    int remaining = 0;
    for (int i = 0; i < p->type_count; i++) {
        remaining += c->records[i].authored;
    }
    while (remaining && !c->wire.error) {
        int before = remaining;
        for (int i = 0; i < p->type_count && !c->wire.error; i++) {
            execution_type_requirement *v = &p->types[i];
            struct prepared_record_cache *cache = &c->records[i];
            if (!cache->authored || v->record) {
                continue;
            }
            if (!work(c, (size_t)cache->count + 1)) {
                return;
            }
            bool ready = true;
            for (int j = 0; j < cache->count; j++) {
                type_field *field = &cache->fields[j];
                if (field->type != PIN_STRUCT) {
                    continue;
                }
                int child = cache->children[j];
                if (child < 0 || child >= p->type_count || child == i ||
                    p->types[child].storage != PIN_STRUCT || !field->type_name ||
                    strcmp(p->types[child].name, field->type_name)) {
                    c->wire.error = "invalid prepared child type reference";
                    return;
                }
                if (c->records[child].authored) {
                    if (!p->types[child].record) {
                        ready = false;
                        continue;
                    }
                    field->layout = &p->types[child].record->type;
                } else {
                    field->layout = c->records[child].linked;
                    if (!field->layout) {
                        c->wire.error = "missing prepared child layout";
                        return;
                    }
                    /* Keep legacy native-only declarations tied to their
                     * registered catalog layout. An array or authored child
                     * makes the containing record own all children instead. */
                    const type_desc *catalog = type_catalog_find(field->type_name);
                    if (field->container == PIN_CONTAINER_SINGLE && catalog &&
                        catalog->size == p->types[child].size &&
                        type_layout_hash(catalog) == p->types[child].layout) {
                        field->layout = NULL;
                    }
                }
            }
            if (!ready) {
                continue;
            }
            /* Closing a parent copies each child closure. Charge those bytes
             * before the size/layout walk, including repeated child uses. */
            for (int j = 0; j < cache->count; j++) {
                const type_desc *child = cache->fields[j].layout;
                if (child && child->carrier && !work(c, child->carrier->bytes)) {
                    return;
                }
            }
            size_t bytes = pin_record_create_bytes(
                v->name,
                v->id,
                cache->fields,
                cache->count
            );
            if (!bytes || !work(c, cache->validation_work) || !work(c, bytes)) {
                if (!c->wire.error) {
                    c->wire.error = "invalid prepared authored layout size";
                }
                return;
            }
            void *memory = execution_wire_allocate(&c->wire, bytes);
            if (!memory) {
                return;
            }
            v->record = pin_record_init(
                memory,
                bytes,
                v->name,
                v->id,
                cache->fields,
                cache->count
            );
            if (!v->record || v->record->layout != v->layout ||
                v->record->type.size != v->size) {
                c->wire.error = "prepared authored layout differs from target";
                return;
            }
            v->name = v->record->type.name;
            /* Hashing flat metadata does not visit descriptor allocation
             * padding. Nested authored layouts add their actual hash walk. */
            for (int j = 0; j < cache->count; j++) {
                int child = cache->children[j];
                if (child < 0) {
                    continue;
                }
                size_t child_work = c->records[child].hash_work;
                if (child_work > SIZE_MAX - cache->hash_work) {
                    c->wire.error = "prepared authored hash work overflow";
                    return;
                }
                cache->hash_work += child_work;
            }
            remaining--;
        }
        if (before == remaining && !c->wire.error) {
            c->wire.error = "recursive prepared authored layout is unsupported";
        }
    }
}
/* The first of the `count` name-ordered types whose name is not before
 * `name`, found in log2(count) comparisons, each charged `bytes`; `found`
 * says whether its name is `name`. Names are unique, so once one equals
 * `name`, every later comparison moves the low end up to it. */
static int name_position(prepared_context *c, const char *name, size_t bytes, int count, bool *found)
{
    int low = 0, high = count;
    *found = false;
    while (low < high && work(c, bytes)) {
        int middle = low + (high - low) / 2;
        int order = strcmp(c->plan->types[c->records[middle].by_name].name, name);
        if (order < 0) {
            low = middle + 1;
        } else {
            high = middle;
            *found = !order;
        }
    }
    return low;
}
/* The first of the `count` id-ordered types whose id is not below `id`,
 * found in log2(count) comparisons, each charged as eight bytes. */
static int id_position(prepared_context *c, uint64_t id, int count)
{
    int low = 0, high = count;
    while (low < high && work(c, sizeof id)) {
        int middle = low + (high - low) / 2;
        if (c->plan->types[c->records[middle].by_id].id < id) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}
static bool index_type(prepared_context *c, int i)
{
    execution_type_requirement *v = &c->plan->types[i];
    c->records[i].name_bytes = name_bytes(c, v->name);
    if (!v->id || !v->name || !*v->name || v->storage < PIN_BOOL ||
        v->storage > PIN_QUERY) {
        c->wire.error = "invalid prepared type requirement";
        return false;
    }
    /* Insertion moves are charged as well as comparisons, including reverse
     * order. Writers build the complete index before encoding child links. */
    bool found;
    int at = name_position(c, v->name, c->records[i].name_bytes, i, &found);
    int id_at = id_position(c, v->id, i);
    if (!work(c, (size_t)(i - at + i - id_at) * sizeof c->records->by_name)) {
        return false;
    }
    if (found || (id_at < i && c->plan->types[c->records[id_at].by_id].id == v->id)) {
        c->wire.error = "duplicate prepared type requirement";
        return false;
    }
    for (int k = i; k > at; k--) {
        c->records[k].by_name = c->records[k - 1].by_name;
    }
    c->records[at].by_name = i;
    for (int k = i; k > id_at; k--) {
        c->records[k].by_id = c->records[k - 1].by_id;
    }
    c->records[id_at].by_id = i;
    return true;
}
void prepared_types(prepared_context *c)
{
    execution_wire *w = &c->wire;
    execution_module_plan *p = c->plan;
    int count = acount(p->types);
    prepared_table(w, &count, sizeof *p->types);
    if (w->reading && !w->error) {
        prepared_vector(w, (void **)&p->types, count, sizeof *p->types);
        if (!w->error) {
            p->type_count = count;
        }
    }
    if (w->error) {
        return;
    }
    execution_wire *budget = c->fingerprint_budget ? c->fingerprint_budget : w;
    c->record_cache_bytes = (size_t)count * sizeof *c->records;
    c->records = execution_wire_allocate_owned(budget, c->record_cache_bytes);
    if (budget->error) {
        w->error = budget->error;
        c->record_cache_bytes = 0;
        return;
    }
    for (int i = 0; !w->reading && i < count && !w->error; i++) {
        index_type(c, i);
    }
    for (int i = 0; i < count && !w->error; i++) {
        execution_type_requirement *v = &p->types[i];
        execution_wire_u64(w, &v->id);
        execution_wire_u64(w, &v->layout);
        int storage = w->reading ? 0 : v->storage;
        execution_wire_int(w, &storage);
        if (w->reading) {
            v->storage = storage;
        }
        execution_wire_string(w, &v->name);
        execution_wire_size(w, &v->size);
        if (w->error) {
            break;
        }
        if (w->reading && !index_type(c, i)) {
            break;
        }
        record_type(c, v, i);
    }
    if (w->reading && !w->error) {
        materialize_records(c);
    }
}
static int requirement(prepared_context *c, const pin_value *v)
{
    size_t bytes = name_bytes(c, v->type_name);
    bool found;
    int at = name_position(c, v->type_name, bytes + 1, c->plan->type_count, &found);
    if (found && !c->wire.error) {
        int index = c->records[at].by_name;
        if (c->plan->types[index].storage == v->type) {
            return index;
        }
    }
    if (!c->wire.error) {
        c->wire.error = "value lacks stable nominal type requirement";
    }
    return -1;
}
/* Descriptors and the source module remain immutable for a write. Decoded
 * functions own aliases of their module's layout, so validate each new alias
 * before retaining its proof. Repeated headers still pay a nonzero charge. */
static bool matches_record(
    prepared_context *c,
    int index,
    const pin_record *record,
    bool current,
    const char *message
)
{
    struct prepared_record_cache *cache = &c->records[index];
    if (!work(c, cache->hash_work)) {
        return false;
    }
    if (record == cache->validated_alias) {
        /* Native children can retire between headers even when the authored
         * descriptor is immutable. Their full check retains its full charge. */
        if (current && cache->hash_work == cache->validation_work &&
            !pin_record_fields_current(record)) {
            c->wire.error = "prepared authored nested layout is retired or "
                            "incompatible";
            return false;
        }
        return true;
    }
    validate(c, record, current);
    if (c->wire.error) {
        return false;
    }
    if (!pin_record_same(c->plan->types[index].record, record)) {
        c->wire.error = message;
        return false;
    }
    if (!work(c, sizeof cache->validated_alias)) {
        return false;
    }
    cache->validated_alias = record;
    return true;
}
static const pin_record *resolve(execution_wire *w, void *user, const pin_value *v, bool authored)
{
    prepared_context *c = user;
    if (w->error) {
        return NULL;
    }
    /* Collection descriptor names identify semantic key/element shapes, not
     * a reflected scalar/record requirement. Their nested values resolve
     * their own nominal requirements through the value codec. */
    if (!authored && v->container != PIN_CONTAINER_SINGLE &&
        (v->type == PIN_UNION ||
         (v->type_name && !strncmp(v->type_name, "__collection_", 13)))) {
        return NULL;
    }
    if (authored && (v->type != PIN_STRUCT || !v->type_name)) {
        w->error = "invalid prepared authored header";
        return NULL;
    }
    if (!v->type_name || v->type < PIN_HANDLE || v->type == PIN_TASK) {
        if (authored) {
            w->error = "invalid prepared authored header";
        }
        return NULL;
    }
    int index = requirement(c, v);
    if (index < 0) {
        return NULL;
    }
    const execution_type_requirement *type = &c->plan->types[index];
    if (!!type->record != authored) {
        w->error = "prepared authored/native type identity differs";
        return NULL;
    }
    if (!w->reading && v->record && v->record->native) {
        if (authored || !pin_record_valid(v->record) ||
            v->record->layout != type->layout || v->record->type.size != type->size ||
            strcmp(v->record->type.name, type->name)) {
            w->error = "prepared native value layout differs";
            return NULL;
        }
        return v->record;
    }
    if (!w->reading) {
        /* Only the outer value's retained schema exempts its native children
         * from catalog freshness. The backing array keeps its separate
         * structural and identity checks. */
        if (v->record &&
            !matches_record(c, index, v->record, !v->schema, "prepared authored value layout differs")) {
            return NULL;
        }
        if (v->container != PIN_CONTAINER_SINGLE && v->ptr) {
            const pin_array *array = v->ptr;
            if (array->record &&
                !matches_record(c, index, array->record, false, "prepared authored/native collection identity differs")) {
                return NULL;
            }
            if (!pin_record_same(array->record, v->record)) {
                w->error = "prepared authored/native collection identity "
                           "differs";
            }
        }
        return v->record;
    }
    if (!authored) {
        return NULL;
    }
    struct prepared_record_cache *cache = &c->records[index];
    if (!work(c, cache->hash_work)) {
        return NULL;
    }
    if (w->storage == &c->plan->storage) {
        return type->record;
    }
    if (cache->owner != w->storage) {
        cache->owned = copy_record(c, type->record, cache->validation_work);
        cache->owner = w->storage;
    }
    return cache->owned;
}
void prepared_value(prepared_context *c, pin_value *v)
{
    execution_wire *w = &c->wire;
    /* A retired native catalog is not needed to save a payload-free header.
     * Its finalized module requirement supplies the storage/layout proof;
     * reading still requires and validates the linked native descriptor. */
    if (!w->reading && !w->error && v->type == PIN_STRUCT &&
        v->container == PIN_CONTAINER_SINGLE && !v->record && !v->ptr && v->type_name) {
        int index = requirement(c, v);
        if (index < 0) {
            return;
        }
        const execution_type_requirement *type = &c->plan->types[index];
        const type_desc *native = pin_value_type(v);
        if (type->record || !type->id || !type->layout || !type->size ||
            type->size > EXECUTION_WIRE_MAX_BYTES ||
            (native && (native->size != type->size ||
                        (v->schema ? pin_schema_layout_type(v->schema, native)
                                   : type_layout_hash(native)) != type->layout))) {
            w->error = "prepared native header lacks a stable layout "
                       "requirement";
            return;
        }
        if (!execution_wire_record(w)) {
            return;
        }
        int tag = PIN_STRUCT, container = PIN_CONTAINER_SINGLE, fields = 0;
        bool authored = false, present = false;
        uint64_t layout = type->layout;
        const char *name = v->type_name;
        execution_wire_int(w, &tag);
        execution_wire_int(w, &container);
        execution_wire_string(w, &name);
        execution_wire_bool(w, &authored);
        execution_wire_u64(w, &layout);
        execution_wire_bool(w, &present);
        execution_wire_count(w, &fields, 0);
        return;
    }
    execution_wire_value_owned(&c->wire, v, resolve, c);
}
static bool header_work(
    prepared_context *c,
    pin_value value,
    behavior_record_check *checks,
    int count,
    bool diagnostic
)
{
    if (!value.record || value.record->native) {
        return true;
    }
    int at = requirement(c, &value);
    if (at < 0) {
        return false;
    }
    int found = behavior_record_check_find(checks, count, value.record);
    if (!diagnostic && found >= 0 && checks[found].seen) {
        return true;
    }
    if (!work(c, c->records[at].validation_work)) {
        return false;
    }
    if (found >= 0) {
        checks[found].seen = true;
    }
    return true;
}
bool prepared_records_finalize(prepared_context *c)
{
    for (int i = 0; i < c->plan->type_count; i++) {
        if (!work(c, c->records[i].validation_work)) {
            return false;
        }
    }
    for (int j = 0; j < acount(c->functions); j++) {
        const behavior_function *f = c->functions[j];
        size_t bytes;
        size_t overhead = behavior_function_record_check_budget(f, &bytes);
        if (overhead == SIZE_MAX || bytes > SIZE_MAX / 2) {
            c->wire.error = "record validation scratch budget overflow";
            return false;
        }
        if (!work(c, overhead)) {
            return false;
        }
        /* Both the temporary preflight table and the later verifier table are
         * charged before either allocation. Canonical reconciliation refunds
         * this scratch when the module is accepted. */
        if (!execution_wire_reserve(&c->wire, 2 * bytes)) {
            return false;
        }
        int count;
        behavior_record_check *checks = behavior_function_record_checks(f, bytes, &count);
        const pin_value *banks[] = { f->argument_defaults,
                                     f->result_defaults,
                                     f->variable_defaults,
                                     f->command_arguments,
                                     f->command_results };
        int counts[] = { acount(f->argument_defaults),
                         acount(f->result_defaults),
                         acount(f->variable_defaults),
                         acount(f->command_arguments),
                         acount(f->command_results) };
        for (int b = 0; b < 5; b++) {
            for (int i = 0; i < counts[b]; i++) {
                if (!header_work(c, banks[b][i], checks, count, false)) {
                    goto invalid;
                }
            }
        }
        for (int i = 0; i < acount(f->data_values); i++) {
            if (!header_work(c, f->data_values[i].initial, checks, count, false)) {
                goto invalid;
            }
        }
        for (int i = 0; i < acount(f->value_roots); i++) {
            if (!header_work(c, f->value_roots[i].shape, checks, count, false)) {
                goto invalid;
            }
        }
        for (int i = 0; i < acount(f->source_watches); i++) {
            /* A failed descriptor is never a proof. The diagnostic rescan
             * can revisit an invalid watch, so retain its full extra charge. */
            if (!header_work(c, f->source_watches[i].initial, checks, count, false) ||
                !header_work(c, f->source_watches[i].initial, checks, count, true)) {
                goto invalid;
            }
        }
        free(checks);
        continue;
    invalid:
        free(checks);
        return false;
    }
    return !c->wire.error;
}
