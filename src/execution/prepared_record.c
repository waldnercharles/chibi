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
    arena *owner;
    const pin_record *owned;
    /* The type at this position in name order, and in id order. */
    int by_name, by_id;
};
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
static size_t validate(prepared_context *c, const pin_record *record)
{
    if (!record || !record->type.fields || record->type.field_count <= 0 ||
        (size_t)record->type.field_count > EXECUTION_WIRE_MAX_RECORDS ||
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
    if (!cost || !work(c, cost) || !pin_record_valid(record)) {
        if (!c->wire.error) {
            c->wire.error = "invalid prepared authored layout";
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
        }
        return;
    }
    if (v->storage != PIN_STRUCT) {
        w->error = "prepared authored requirement must be a struct";
        return;
    }
    if (!w->reading) {
        if (!pin_record_fields_current(v->record)) {
            w->error = "prepared authored nested layout is retired or "
                       "incompatible";
            return;
        }
        cache->validation_work = validate(c, v->record);
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
    if (w->reading) {
        fields = execution_wire_allocate(w, (size_t)count * sizeof *fields);
        if (!fields) {
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
        if (text_descriptor != (kind == PIN_STR)) {
            w->error = "invalid prepared authored string representation";
        }
        if (w->error) {
            break;
        }
        size_t bytes = name_bytes(c, name);
        if (!name || !*name || !bytes || bytes > SIZE_MAX - names ||
            !((kind >= PIN_BOOL && kind <= PIN_ENUM) || kind == PIN_POINTER ||
              kind == PIN_QUERY)) {
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
                                      .text_descriptor = text_descriptor };
        }
    }
    if (w->error || !w->reading) {
        return;
    }
    size_t bytes = sizeof(pin_record) + (size_t)count * sizeof(type_field);
    if (names > SIZE_MAX - bytes) {
        w->error = "prepared authored layout size overflow";
        return;
    }
    bytes += names;
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
    if (!work(c, cache->validation_work)) {
        return;
    }
    void *memory = execution_wire_allocate(w, bytes);
    if (!memory) {
        return;
    }
    v->record = pin_record_init(memory, bytes, v->name, v->id, fields, count);
    if (!v->record || v->record->layout != v->layout || v->record->type.size != v->size) {
        w->error = "prepared authored layout differs from target";
        return;
    }
    v->name = v->record->type.name;
}
/* The first of the `count` name-ordered types whose name is not before
 * `name`, found in log2(count) comparisons, each charged `bytes`. */
static int name_position(prepared_context *c, const char *name, size_t bytes, int count)
{
    int low = 0, high = count;
    while (low < high && work(c, bytes)) {
        int middle = low + (high - low) / 2;
        if (strcmp(c->plan->types[c->records[middle].by_name].name, name) < 0) {
            low = middle + 1;
        } else {
            high = middle;
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
        c->records[i].name_bytes = name_bytes(c, v->name);
        if (!v->id || !v->name || !*v->name || v->storage < PIN_BOOL ||
            v->storage > PIN_QUERY) {
            w->error = "invalid prepared type requirement";
            break;
        }
        /* The earlier types are kept in name and id order, so a repeat is
         * found by two searches rather than a comparison with each. Making
         * room for this type moves the indexes after both positions, which
         * is charged too: a table listed in reverse order moves count^2. */
        int at = name_position(c, v->name, c->records[i].name_bytes, i);
        int id_at = id_position(c, v->id, i);
        if (!work(c, (size_t)(i - at + i - id_at) * sizeof c->records->by_name)) {
            break;
        }
        if ((at < i && !strcmp(p->types[c->records[at].by_name].name, v->name)) ||
            (id_at < i && p->types[c->records[id_at].by_id].id == v->id)) {
            w->error = "duplicate prepared type requirement";
            break;
        }
        for (int k = i; k > at; k--) {
            c->records[k].by_name = c->records[k - 1].by_name;
        }
        c->records[at].by_name = i;
        for (int k = i; k > id_at; k--) {
            c->records[k].by_id = c->records[k - 1].by_id;
        }
        c->records[id_at].by_id = i;
        record_type(c, v, i);
    }
}
static int requirement(prepared_context *c, const pin_value *v)
{
    size_t bytes = name_bytes(c, v->type_name);
    int at = name_position(c, v->type_name, bytes + 1, c->plan->type_count);
    if (at < c->plan->type_count && !c->wire.error) {
        int index = c->records[at].by_name;
        const execution_type_requirement *type = &c->plan->types[index];
        if (type->storage == v->type && !strcmp(type->name, v->type_name)) {
            return index;
        }
    }
    if (!c->wire.error) {
        c->wire.error = "value lacks stable nominal type requirement";
    }
    return -1;
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
        if (v->record) {
            validate(c, v->record);
            if (w->error) {
                return NULL;
            }
            if (!pin_record_same(type->record, v->record)) {
                w->error = "prepared authored value layout differs";
                return NULL;
            }
        }
        if (v->container != PIN_CONTAINER_SINGLE && v->ptr) {
            const pin_array *array = v->ptr;
            if (array->record) {
                validate(c, array->record);
            }
            if (w->error) {
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
