#include "runtime/native_capabilities.h"

#include "capabilities/catalog_index.h"
#include "execution/operation.h"
#include "support/arena.h"
#include "support/array.h"
#include "support/xalloc.h"
#include "types/array.h"

#include <assert.h>
#include <limits.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct native_capabilities
{
    int references;
    uint64_t generation;
    arena storage;
    script_catalog catalog;
    script_catalog_type *types;
    script_catalog_capability *capabilities;
    script_catalog_global *declarations;
    script_catalog_relationship *relationships;
    native_capability_binding *bindings;
    native_global_binding *globals;
    type_desc *layouts;
};
static atomic_uint_fast64_t next_generation = 1;

static const char *own(native_capabilities *n, const char *text)
{
    if (!text) {
        return NULL;
    }
    size_t size = strlen(text) + 1;
    char *copy = arena_alloc(&n->storage, size);
    memcpy(copy, text, size);
    return copy;
}
static bool reject(char *error, size_t size, const char *message, uint64_t id)
{
    if (size) {
        snprintf(error, size, "%s (%llu)", message, (unsigned long long)id);
    }
    return false;
}
static bool same_text(const char *a, const char *b)
{
    return a == b || (a && b && !strcmp(a, b));
}
static bool add_relationship(
    native_capabilities *n,
    const script_catalog_relationship *source,
    char *error,
    size_t size
)
{
    if (!source->id || !source->name || !source->name[0] ||
        (source->traversable && !source->exclusive)) {
        return reject(
            error,
            size,
            "Invalid native relationship declaration",
            source->id
        );
    }
    bool child_of = !strcmp(source->name, "ChildOf");
    if ((source->id == SCRIPT_CHILD_OF || child_of) &&
        (source->id != SCRIPT_CHILD_OF || !child_of || !source->exclusive ||
         !source->traversable)) {
        return reject(
            error,
            size,
            "Conflicting built-in ChildOf relationship",
            source->id
        );
    }
    for (int i = 0; i < acount(n->relationships); i++) {
        const script_catalog_relationship *prior = &n->relationships[i];
        if (prior->id == source->id || !strcmp(prior->name, source->name)) {
            return (prior->id == source->id && !strcmp(prior->name, source->name) &&
                    prior->exclusive == source->exclusive &&
                    prior->traversable == source->traversable) ||
                   reject(
                       error,
                       size,
                       "Conflicting native relationship declarations",
                       source->id
                   );
        }
    }
    script_catalog_relationship copy = *source;
    copy.name = own(n, source->name);
    apush(n->relationships, copy);
    return true;
}
static bool same_type(const script_catalog_type *a, const script_catalog_type *b)
{
    if (!same_text(a->name, b->name) || !same_text(a->source_name, b->source_name) ||
        !same_text(a->key, b->key) || !script_type_equal(a->type, b->type) ||
        a->size != b->size || a->layout != b->layout ||
        a->underlying != b->underlying || a->component_id != b->component_id ||
        a->component != b->component || a->field_count != b->field_count) {
        return false;
    }
    for (int i = 0; i < a->field_count; ++i) {
        const script_catalog_field *x = &a->fields[i], *y = &b->fields[i];
        if (x->id != y->id || !same_text(x->name, y->name) ||
            !script_type_equal(x->type, y->type) || x->offset != y->offset ||
            x->read != y->read || x->write != y->write || x->writable != y->writable) {
            return false;
        }
    }
    return true;
}
static bool add_type(
    native_capabilities *n,
    const script_catalog_type *source,
    const type_desc *const *layouts,
    int layout_count,
    char *error,
    size_t size
)
{
    if (!source->id || !source->name || source->field_count < 0 ||
        (source->field_count && !source->fields)) {
        return reject(error, size, "Invalid native type declaration", source->id);
    }
    script_catalog_type resolved = *source;
    const type_desc *layout = NULL;
    for (int i = 0; i < layout_count; ++i) {
        if (!strcmp(layouts[i]->name, source->name)) {
            layout = layouts[i];
            break;
        }
    }
    if (source->type.storage == PIN_STRUCT || layout) {
        if (!layout || layout->size != source->size ||
            layout->field_count != source->field_count) {
            return reject(
                error,
                size,
                "Native struct has no matching target layout",
                source->id
            );
        }
        for (int i = 0; i < source->field_count; ++i) {
            const script_catalog_field *field = &source->fields[i];
            const type_field *actual = &layout->fields[i];
            if (!same_text(field->name, actual->name) ||
                field->offset != actual->offset || field->type.storage != actual->type ||
                field->type.container != PIN_CONTAINER_SINGLE) {
                return reject(
                    error,
                    size,
                    "Native field declaration differs from target layout",
                    source->id
                );
            }
        }
        resolved.layout = type_layout_hash_explicit(layout, layouts, layout_count);
        if (!resolved.layout || (source->layout && source->layout != resolved.layout)) {
            return reject(
                error,
                size,
                "Native target layout identity conflict",
                source->id
            );
        }
        source = &resolved;
    }
    for (int i = 0; i < acount(n->types); ++i) {
        if (n->types[i].id == source->id) {
            return same_type(&n->types[i], source) ||
                   reject(error, size, "Conflicting native type", source->id);
        }
    }
    script_catalog_type copy = *source;
    copy.name = own(n, source->name);
    copy.source_name = own(n, source->source_name);
    copy.key = own(n, source->key);
    script_catalog_field
        *fields = arena_alloc(&n->storage, sizeof(*fields) * (size_t)source->field_count);
    for (int i = 0; i < source->field_count; ++i) {
        fields[i] = source->fields[i];
        fields[i].name = own(n, fields[i].name);
        /* Reflected declarations leave units to the target layout. */
        fields[i].unit = own(
            n,
            layout && !fields[i].unit ? layout->fields[i].unit : fields[i].unit
        );
        /* Only the owning target layout can establish a stored-value range. */
        fields[i].range_contract = layout && layout->fields[i].range_contract;
        fields[i].min = layout ? layout->fields[i].min : 0;
        fields[i].max = layout ? layout->fields[i].max : 0;
    }
    copy.fields = fields;
    apush(n->types, copy);
    return true;
}
static bool parameters(
    native_capabilities *n,
    const script_catalog_parameter *source,
    int count,
    const script_catalog_parameter **out
)
{
    if (count < 0 || (count && !source)) {
        return false;
    }
    script_catalog_parameter *copy = arena_alloc(&n->storage, sizeof(*copy) * (size_t)count);
    for (int i = 0; i < count; ++i) {
        copy[i] = source[i];
        copy[i].name = own(n, source[i].name);
        copy[i].unit = own(n, source[i].unit);
        if (source[i].has_default && pin_value_capture_to_arena(
                                         &n->storage,
                                         &copy[i].default_value,
                                         &source[i].default_value
                                     )) {
            return false;
        }
    }
    *out = copy;
    return true;
}
static bool names(native_capabilities *n, const char *const *source, int count, const char *const **out)
{
    if (count < 0 || (count && !source)) {
        return false;
    }
    const char **copy = arena_alloc(&n->storage, sizeof(*copy) * (size_t)count);
    for (int i = 0; i < count; ++i) {
        if (!source[i]) {
            return false;
        }
        copy[i] = own(n, source[i]);
    }
    *out = copy;
    return true;
}
static bool add_capability(
    native_capabilities *n,
    catalog_index *index,
    const script_catalog_capability *source,
    char *error,
    size_t size
)
{
    if (!source->id || !source->name) {
        return reject(
            error,
            size,
            "Invalid native capability declaration",
            source->id
        );
    }
    if (catalog_index_add(index, source->id, acount(n->capabilities)) >= 0) {
        return reject(error, size, "Duplicate native capability", source->id);
    }
    if (!script_catalog_operation_valid(source)) {
        return reject(
            error,
            size,
            "Invalid engine operation declaration",
            source->id
        );
    }
    script_catalog_capability copy = *source;
    copy.name = own(n, source->name);
    copy.qualified_name = own(n, source->qualified_name);
    copy.operator_name = own(n, source->operator_name);
    copy.method_name = own(n, source->method_name);
    copy.category = own(n, source->category);
    copy.keywords = own(n, source->keywords);
    copy.description = own(n, source->description);
    if (!parameters(n, source->parameters, source->parameter_count, &copy.parameters) ||
        !parameters(n, source->results, source->result_count, &copy.results) ||
        !names(n, source->entries, source->entry_count, &copy.entries) ||
        !names(n, source->outcomes, source->outcome_count, &copy.outcomes)) {
        return reject(
            error,
            size,
            "Invalid native capability schema or default",
            source->id
        );
    }
    apush(n->capabilities, copy);
    return true;
}
static bool add_global(native_capabilities *n, const script_catalog_global *source, char *error, size_t size)
{
    if (!source->id || !source->name) {
        return reject(error, size, "Invalid native global declaration", source->id);
    }
    for (int i = 0; i < acount(n->declarations); ++i) {
        if (n->declarations[i].id == source->id) {
            return reject(error, size, "Duplicate native global", source->id);
        }
    }
    script_catalog_global copy = *source;
    copy.name = own(n, source->name);
    copy.unit = own(n, source->unit);
    apush(n->declarations, copy);
    return true;
}
static bool add_binding(
    native_capabilities *n,
    const catalog_index *index,
    native_capability_binding copy,
    char *error,
    size_t size
)
{
    int row = catalog_index_find(index, copy.id);
    const script_catalog_capability *declaration = row >= 0
                                                     ? &n->catalog.capabilities[row]
                                                     : NULL;
    if (!declaration || copy.abi || copy.result_count < 0 ||
        (copy.result_count && !copy.result_pins) ||
        (!!copy.call + !!copy.command + !!copy.query + !!copy.intrinsic) != 1) {
        return reject(error, size, "Invalid native execution binding", copy.id);
    }
    if (copy.call && ((!!copy.call->invoke + !!copy.call->value +
                       !!copy.call->context + !!copy.call->action) != 1 ||
                      (copy.call->value && copy.call->service))) {
        return reject(error, size, "Invalid native host descriptor", copy.id);
    }
    if (declaration->owned_result != (copy.call && copy.call->owned_result) ||
        (declaration->owned_result && (!copy.call->value || copy.call->service))) {
        return reject(error, size, "Native result ownership does not match declaration", copy.id);
    }
    if (declaration->language_values != (copy.call && copy.call->language_values) ||
        (declaration->language_values && (!copy.call->value || copy.call->service))) {
        return reject(error, size, "Native language-value policy does not match declaration", copy.id);
    }
    if (copy.command &&
        ((copy.command->intrinsic == NODE_COMMAND_NATIVE &&
          (!!copy.command->invoke + !!copy.command->begin) != 1) ||
         (copy.command->intrinsic == NODE_COMMAND_DELAY &&
          (copy.command->invoke || copy.command->begin || copy.command->service)) ||
         copy.command->intrinsic > NODE_COMMAND_DELAY)) {
        return reject(error, size, "Invalid native command descriptor", copy.id);
    }
    if (declaration->operation != SCRIPT_OPERATION_UNKNOWN
            ? copy.intrinsic != behavior_operation_instruction(declaration->operation) ||
                  copy.access || copy.instance_state || copy.query_service
            : copy.intrinsic && copy.intrinsic != BF_ASSERT) {
        return reject(error, size, "Native binding differs from engine operation contract", copy.id);
    }
    for (int i = 0; i < acount(n->bindings); ++i) {
        if (n->bindings[i].id == copy.id) {
            return reject(error, size, "Duplicate native execution binding", copy.id);
        }
    }
    if (copy.result_count && copy.result_count != declaration->result_count) {
        return reject(error, size, "Native result map does not match declaration", copy.id);
    }
    int *pins = arena_alloc(&n->storage, sizeof(*pins) * (size_t)copy.result_count);
    for (int i = 0; i < copy.result_count; ++i) {
        if (copy.result_pins[i] < 0) {
            return reject(error, size, "Invalid native result pin", copy.id);
        }
        for (int j = 0; j < i; ++j) {
            if (copy.result_pins[i] == pins[j]) {
                return reject(error, size, "Duplicate native result pin", copy.id);
            }
        }
        pins[i] = copy.result_pins[i];
    }
    copy.result_pins = pins;
    copy.query_service = own(n, copy.query_service);
    copy.abi = script_catalog_capability_abi(&n->catalog, declaration);
    apush(n->bindings, copy);
    return true;
}
static bool field_nominals(
    native_capabilities *n,
    const type_desc *const *layouts,
    int count,
    char *error,
    size_t size
)
{
    for (int i = 0; i < count; ++i) {
        int index = script_catalog_find_type(&n->catalog, layouts[i]->name);
        if (index < 0) {
            continue;
        }
        const script_catalog_type *owner = &n->types[index];
        for (int j = 0; j < owner->field_count; ++j) {
            const script_catalog_field *field = &owner->fields[j];
            const type_field *actual = &layouts[i]->fields[j];
            if (actual->type != PIN_STRUCT && actual->type != PIN_ENUM &&
                actual->type != PIN_HANDLE) {
                continue;
            }
            int nominal = actual->type_name
                            ? script_catalog_find_type(&n->catalog, actual->type_name)
                            : -1;
            const script_catalog_type
                *declared = script_catalog_type_by_id(&n->catalog, field->type.nominal);
            if (nominal < 0 || !declared ||
                (script_catalog_canonical_type(&n->catalog, field->type).flags &
                 SCRIPT_TYPE_ERROR) ||
                (script_catalog_canonical_type(&n->catalog, n->types[nominal].type)
                     .flags &
                 SCRIPT_TYPE_ERROR) ||
                !script_catalog_type_assignable(
                    &n->catalog,
                    field->type,
                    n->types[nominal].type
                ) ||
                (actual->type == PIN_STRUCT &&
                 (declared->layout != n->types[nominal].layout ||
                  declared->size != actual->size))) {
                return reject(
                    error,
                    size,
                    "Native field nominal differs from target layout",
                    owner->id
                );
            }
        }
    }
    return true;
}

native_capabilities *native_capabilities_new(
    const native_capability_pack *packs,
    int count,
    char *error,
    size_t size
)
{
    assert(count >= 0 && (!count || packs) && (!size || error));
    if (size) {
        error[0] = 0;
    }
    native_capabilities *n = xcalloc(1, sizeof(*n));
    n->references = 1;
    n->generation = atomic_fetch_add_explicit(&next_generation, 1, memory_order_relaxed);
    assert(n->generation && n->generation != UINT64_MAX);
    const type_desc **layouts = NULL;
    catalog_index capabilities = { 0 };
    for (int p = 0; p < count; ++p) {
        if (packs[p].layout_count < 0 || (packs[p].layout_count && !packs[p].layouts)) {
            reject(error, size, "Invalid native layout pack", 0);
            goto fail;
        }
        for (int i = 0; i < packs[p].layout_count; ++i) {
            const type_desc *layout = packs[p].layouts[i];
            if (!layout || !layout->name || !layout->size || layout->field_count < 0 ||
                (layout->field_count && !layout->fields)) {
                reject(error, size, "Invalid native target layout", 0);
                goto fail;
            }
            for (int j = 0; j < layout->field_count; ++j) {
                if (!type_field_range_valid(&layout->fields[j])) {
                    reject(error, size, "Invalid native field range contract", 0);
                    goto fail;
                }
            }
            apush(layouts, layout);
        }
    }
    for (int i = 0; i < acount(layouts); ++i) {
        uint64_t hash = type_layout_hash_explicit(layouts[i], layouts, acount(layouts));
        if (!hash) {
            reject(error, size, "Native layout has an unresolved nested type", 0);
            goto fail;
        }
        for (int j = 0; j < i; ++j) {
            if (same_text(layouts[i]->name, layouts[j]->name) &&
                hash != type_layout_hash_explicit(layouts[j], layouts, acount(layouts))) {
                reject(error, size, "Conflicting native target layouts", 0);
                goto fail;
            }
        }
    }
    for (int p = 0; p < count; ++p) {
        const native_capability_pack *pack = &packs[p];
        const script_catalog *catalog = pack->catalog;
        if (!catalog || catalog->type_count < 0 || catalog->capability_count < 0 ||
            catalog->template_count || catalog->specialization_count ||
            catalog->global_count < 0 || (catalog->type_count && !catalog->types) ||
            (catalog->capability_count && !catalog->capabilities) ||
            (catalog->global_count && !catalog->globals) ||
            catalog->relationship_count < 0 ||
            (catalog->relationship_count && !catalog->relationships) ||
            pack->binding_count < 0 || pack->global_count < 0 ||
            (pack->binding_count && !pack->bindings) ||
            (pack->global_count && !pack->globals)) {
            reject(error, size, "Invalid native pack", 0);
            goto fail;
        }
        if ((n->catalog.entity_type && catalog->entity_type &&
             n->catalog.entity_type != catalog->entity_type) ||
            (n->catalog.tick_event && catalog->tick_event &&
             n->catalog.tick_event != catalog->tick_event)) {
            reject(error, size, "Conflicting native builtins", 0);
            goto fail;
        }
        if (catalog->entity_type) {
            n->catalog.entity_type = catalog->entity_type;
        }
        if (catalog->tick_event) {
            n->catalog.tick_event = catalog->tick_event;
        }
        for (int i = 0; i < catalog->type_count; ++i) {
            if (!add_type(n, &catalog->types[i], layouts, acount(layouts), error, size)) {
                goto fail;
            }
        }
        for (int i = 0; i < catalog->capability_count; ++i) {
            if (!add_capability(n, &capabilities, &catalog->capabilities[i], error, size)) {
                goto fail;
            }
        }
        for (int i = 0; i < catalog->global_count; ++i) {
            if (!add_global(n, &catalog->globals[i], error, size)) {
                goto fail;
            }
        }
        for (int i = 0; i < catalog->relationship_count; ++i) {
            if (!add_relationship(n, &catalog->relationships[i], error, size)) {
                goto fail;
            }
        }
    }
    n->catalog.types = n->types;
    n->catalog.type_count = acount(n->types);
    n->catalog.capabilities = n->capabilities;
    n->catalog.capability_count = acount(n->capabilities);
    n->catalog.globals = n->declarations;
    n->catalog.global_count = acount(n->declarations);
    n->catalog.relationships = n->relationships;
    n->catalog.relationship_count = acount(n->relationships);
    if (!field_nominals(n, layouts, acount(layouts), error, size)) {
        goto fail;
    }
    for (int p = 0; p < count; ++p) {
        for (int i = 0; i < packs[p].binding_count; ++i) {
            if (!add_binding(n, &capabilities, packs[p].bindings[i], error, size)) {
                goto fail;
            }
        }
        for (int i = 0; i < packs[p].global_count; ++i) {
            native_global_binding copy = packs[p].globals[i];
            pin_value value = copy.instruction.value;
            bool declared = false;
            for (int j = 0; j < n->catalog.global_count; ++j) {
                declared |= n->declarations[j].id == copy.id;
            }
            if (!declared || native_capabilities_global(n, copy.id) ||
                pin_value_capture_to_arena(&n->storage, &copy.instruction.value, &value)) {
                reject(error, size, "Invalid native global binding", copy.id);
                goto fail;
            }
            copy.instruction.message = own(n, copy.instruction.message);
            apush(n->globals, copy);
        }
    }
    for (int i = 0; i < n->catalog.capability_count; i++) {
        const script_catalog_capability *declaration = &n->capabilities[i];
        if (declaration->operation != SCRIPT_OPERATION_UNKNOWN &&
            !native_capabilities_find(n, declaration->id, 0)) {
            reject(
                error,
                size,
                "Engine operation has no execution binding",
                declaration->id
            );
            goto fail;
        }
    }
    for (int i = 0; i < acount(layouts); i++) {
        bool exists = false;
        for (int j = 0; j < acount(n->layouts); j++) {
            exists |= same_text(n->layouts[j].name, layouts[i]->name);
        }
        if (exists) {
            continue;
        }
        type_desc copy = *layouts[i];
        copy.name = own(n, copy.name);
        copy.alias_of = own(n, copy.alias_of);
        type_field *fields = arena_alloc(&n->storage, (size_t)copy.field_count * sizeof *fields);
        for (int j = 0; j < copy.field_count; j++) {
            fields[j] = copy.fields[j];
            fields[j].name = own(n, fields[j].name);
            fields[j].type_name = own(n, fields[j].type_name);
            fields[j].description = own(n, fields[j].description);
            fields[j].unit = own(n, fields[j].unit);
        }
        copy.fields = fields;
        apush(n->layouts, copy);
    }
    afree(layouts);
    catalog_index_free(&capabilities);
    return n;
fail:
    afree(layouts);
    catalog_index_free(&capabilities);
    native_capabilities_free(n);
    return NULL;
}
uint64_t native_capabilities_generation(const native_capabilities *n)
{
    assert(n);
    return n->generation;
}
native_capabilities *native_capabilities_retain(native_capabilities *n)
{
    assert(n && n->references > 0 && n->references < INT_MAX);
    n->references++;
    return n;
}
void native_capabilities_free(native_capabilities *n)
{
    if (!n) {
        return;
    }
    assert(n->references > 0);
    if (--n->references) {
        return;
    }
    arena_free(&n->storage);
    afree(n->types);
    afree(n->capabilities);
    afree(n->declarations);
    afree(n->relationships);
    afree(n->bindings);
    afree(n->globals);
    afree(n->layouts);
    free(n);
}
static size_t storage_add(size_t bytes, size_t more)
{
    return more > SIZE_MAX - bytes ? SIZE_MAX : bytes + more;
}
static size_t vector_bytes(const void *values, size_t item)
{
    if (!values) {
        return 0;
    }
    size_t capacity = (size_t)acap(values);
    return capacity > (SIZE_MAX - sizeof(dyna_hdr)) / item
             ? SIZE_MAX
             : sizeof(dyna_hdr) + capacity * item;
}
size_t native_capabilities_storage_bytes(const native_capabilities *n)
{
    assert(n);
    size_t bytes = storage_add(sizeof *n, arena_storage_bytes(&n->storage));
#define COUNT(field)                                                           \
    bytes = storage_add(bytes, vector_bytes(n->field, sizeof *n->field))
    COUNT(types);
    COUNT(capabilities);
    COUNT(declarations);
    COUNT(relationships);
    COUNT(bindings);
    COUNT(globals);
    COUNT(layouts);
#undef COUNT
    return bytes;
}
const script_catalog *native_capabilities_catalog(const native_capabilities *n)
{
    assert(n);
    return &n->catalog;
}
const type_desc *native_capabilities_layout(const native_capabilities *n, uint64_t id)
{
    assert(n);
    const char *name = NULL;
    for (int i = 0; i < n->catalog.type_count; i++) {
        if (n->catalog.types[i].id == id) {
            name = n->catalog.types[i].name;
            break;
        }
    }
    for (int i = 0; name && i < acount(n->layouts); i++) {
        if (!strcmp(name, n->layouts[i].name)) {
            return &n->layouts[i];
        }
    }
    return NULL;
}
const native_capability_binding *native_capabilities_find(
    const native_capabilities *n,
    uint64_t id,
    uint64_t abi
)
{
    assert(n);
    for (int i = 0; i < acount(n->bindings); ++i) {
        if (n->bindings[i].id == id && (!abi || n->bindings[i].abi == abi)) {
            return &n->bindings[i];
        }
    }
    return NULL;
}
const native_global_binding *native_capabilities_global(const native_capabilities *n, uint64_t id)
{
    assert(n);
    for (int i = 0; i < acount(n->globals); ++i) {
        if (n->globals[i].id == id) {
            return &n->globals[i];
        }
    }
    return NULL;
}
