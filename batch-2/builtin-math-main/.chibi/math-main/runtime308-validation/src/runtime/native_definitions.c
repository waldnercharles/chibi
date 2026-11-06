#include "runtime/native_definitions.h"

#include "execution/operation.h"
#include "support/array.h"
#include "support/xalloc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEFINITION_MAX_PACKS 4096
#define DEFINITION_MAX_RECORDS 65536

static bool fail(char *error, size_t size, const char *name, const char *message)
{
    if (size) {
        snprintf(error, size, "%s: %s", name ? name : "Native definition pack", message);
    }
    return false;
}
static unsigned effects(const native_capability_binding *binding, bool pure)
{
    unsigned result = 0;
    const node_access *a = binding->access;
    if (a) {
        if (a->read_count || a->read_refs || (a->flags & NODE_EFFECT_WORLD_READ)) {
            result |= SCRIPT_EFFECT_READ;
        }
        if (a->write_count) {
            result |= SCRIPT_EFFECT_WRITE;
        }
        if (a->flags & NODE_EFFECT_STRUCTURAL) {
            result |= SCRIPT_EFFECT_STRUCTURAL;
        }
        if ((a->flags & NODE_EFFECT_HOST_SINK) && !(a->flags & NODE_EFFECT_RANDOM)) {
            result |= SCRIPT_EFFECT_HOST;
        }
        if (a->flags & NODE_EFFECT_RANDOM) {
            result |= SCRIPT_EFFECT_RANDOM;
        }
    }
    if (binding->command) {
        result |= SCRIPT_EFFECT_HOST;
    }
    if (binding->call && binding->call->service) {
        result |= pure ? SCRIPT_EFFECT_READ : SCRIPT_EFFECT_HOST;
    }
    if (binding->query) {
        result |= SCRIPT_EFFECT_READ;
    }
    if (binding->intrinsic == BF_DELAY) {
        result |= SCRIPT_EFFECT_SUSPEND;
    }
    return result;
}
static bool prepare(
    const native_definition_pack *source,
    native_definition_catalog **catalog,
    native_capability_binding **bindings,
    native_global_binding **globals,
    native_capability_pack *pack,
    char *error,
    size_t size
)
{
    if (source->definition_count < 0 || source->definition_count > DEFINITION_MAX_RECORDS ||
        source->binding_count < 0 || source->binding_count > source->definition_count ||
        (source->definition_count && !source->definitions) ||
        (source->binding_count && !source->bindings) || source->global_count < 0 ||
        source->global_count > DEFINITION_MAX_RECORDS ||
        source->global_binding_count < 0 ||
        source->global_binding_count > source->global_count ||
        (source->global_count && !source->global_definitions) ||
        (source->global_binding_count && !source->global_bindings)) {
        return fail(error, size, NULL, "Invalid definition or binding count");
    }
    native_capability_definition *definitions = xmalloc(
        (size_t)source->definition_count * sizeof *definitions
    );
    if (source->definition_count) {
        memcpy(definitions, source->definitions, (size_t)source->definition_count * sizeof *definitions);
    }
    bool valid = true;
    for (int i = 0; valid && i < source->binding_count; ++i) {
        const native_definition_binding *binding = &source->bindings[i];
        const native_capability_binding *b = &binding->implementation;
        if (!binding->canonical_name || b->id || b->abi || b->result_pins ||
            b->result_count) {
            valid = fail(error, size, binding->canonical_name, "Implementation must borrow a canonical definition");
            break;
        }
        int found = -1;
        for (int j = 0; j < source->definition_count; ++j) {
            if ((definitions[j].kind == SCRIPT_CAPABILITY_FUNCTION ||
                 definitions[j].kind == SCRIPT_CAPABILITY_ACTION) &&
                definitions[j].canonical_name &&
                !strcmp(definitions[j].canonical_name, binding->canonical_name)) {
                found = j;
                break;
            }
        }
        if (found < 0) {
            valid = fail(error, size, binding->canonical_name, "Binding has no shared definition");
            break;
        }
        if (definitions[found].operation != SCRIPT_OPERATION_UNKNOWN) {
            valid = fail(error, size, binding->canonical_name, "Engine operation binding is generated from its definition");
            break;
        }
        for (int j = 0; j < i; ++j) {
            if (!strcmp(source->bindings[j].canonical_name, binding->canonical_name)) {
                valid = fail(error, size, binding->canonical_name, "Duplicate native binding");
                break;
            }
        }
        if (definitions[found].kind != SCRIPT_CAPABILITY_FUNCTION &&
            definitions[found].kind != SCRIPT_CAPABILITY_ACTION) {
            valid = fail(error, size, binding->canonical_name, "Only callable definitions bind native implementations");
            break;
        }
        definitions[found].language_values = b->call && b->call->language_values;
        definitions[found].owned_result = b->call && b->call->owned_result;
        definitions[found].effects |= effects(b, definitions[found].pure);
    }
    if (valid) {
        *catalog = native_definition_catalog_new(
            &source->base,
            definitions,
            source->definition_count,
            source->global_definitions,
            source->global_count,
            error,
            size
        );
        valid = *catalog != NULL;
    }
    /* The intermediate borrows names/default payloads, but copies all definition
     * records into catalog arrays. No pointer into this temporary vector escapes. */
    free(definitions);
    if (!valid) {
        return false;
    }
    for (int i = 0; i < source->binding_count; ++i) {
        native_capability_binding b = source->bindings[i].implementation;
        b.id = script_catalog_id("capability", source->bindings[i].canonical_name);
        b.result_pins = native_definition_result_pins(*catalog, b.id, &b.result_count);
        assert(b.result_count >= 0);
        apush(*bindings, b);
    }
    const script_catalog *view = native_definition_catalog_view(*catalog);
    for (int i = 0; i < view->capability_count; i++) {
        const script_catalog_capability *declaration = &view->capabilities[i];
        if (declaration->operation == SCRIPT_OPERATION_UNKNOWN) {
            continue;
        }
        native_capability_binding binding = {
            .id = declaration->id,
            .intrinsic = behavior_operation_instruction(declaration->operation),
        };
        binding.result_pins = native_definition_result_pins(
            *catalog,
            binding.id,
            &binding.result_count
        );
        apush(*bindings, binding);
    }
    for (int i = 0; i < source->global_binding_count; ++i) {
        const native_definition_global_binding *binding = &source->global_bindings[i];
        int found = -1;
        for (int j = 0; binding->canonical_name && j < source->global_count; ++j) {
            if (!strcmp(source->global_definitions[j].canonical_name, binding->canonical_name)) {
                if (found >= 0) {
                    return fail(error, size, binding->canonical_name, "Ambiguous global binding");
                }
                found = j;
            }
        }
        if (found < 0) {
            return fail(error, size, binding->canonical_name, "Global binding has no shared definition");
        }
        const script_catalog *view = native_definition_catalog_view(*catalog);
        script_type type = view->globals[found].type;
        pin_type physical = type.storage == PIN_STRUCT || type.container != PIN_CONTAINER_SINGLE
                              ? BF_POINTER_TYPE
                              : type.storage;
        if (binding->instruction.type != physical) {
            return fail(error, size, binding->canonical_name, "Global producer type differs from declaration");
        }
        uint64_t id = native_global_definition_id(&source->global_definitions[found]);
        for (int j = 0; j < acount(*globals); ++j) {
            if ((*globals)[j].id == id) {
                return fail(error, size, binding->canonical_name, "Duplicate global binding");
            }
        }
        apush(
            *globals,
            ((native_global_binding){ .id = id, .instruction = binding->instruction })
        );
    }
    *pack = (native_capability_pack){ .catalog = native_definition_catalog_view(*catalog),
                                      .bindings = *bindings,
                                      .binding_count = acount(*bindings),
                                      .globals = *globals,
                                      .global_count = acount(*globals),
                                      .layouts = source->layouts,
                                      .layout_count = source->layout_count };
    return true;
}
native_capabilities *native_capabilities_from_definition_packs(
    const native_capability_pack *existing,
    int existing_count,
    const native_definition_pack *source,
    int count,
    char *error,
    size_t size
)
{
    assert(!size || error);
    if (size) {
        error[0] = 0;
    }
    if (count < 0 || count > DEFINITION_MAX_PACKS || (count && !source) ||
        existing_count < 0 || existing_count > DEFINITION_MAX_PACKS ||
        (existing_count && !existing) || count + existing_count > DEFINITION_MAX_PACKS) {
        fail(error, size, NULL, "Invalid pack count");
        return NULL;
    }
    native_definition_catalog **catalogs = xcalloc((size_t)count, sizeof *catalogs);
    native_capability_binding **bindings = xcalloc((size_t)count, sizeof *bindings);
    native_global_binding **globals = xcalloc((size_t)count, sizeof *globals);
    native_capability_pack *packs = xcalloc((size_t)(count + existing_count), sizeof *packs);
    if (existing_count) {
        memcpy(packs, existing, (size_t)existing_count * sizeof *packs);
    }
    bool valid = true;
    for (int i = 0; valid && i < count; ++i) {
        valid = prepare(
            &source[i],
            &catalogs[i],
            &bindings[i],
            &globals[i],
            &packs[existing_count + i],
            error,
            size
        );
    }
    native_capabilities *out = valid
                                 ? native_capabilities_new(packs, count + existing_count, error, size)
                                 : NULL;
    for (int i = 0; i < count; ++i) {
        native_definition_catalog_free(catalogs[i]);
        afree(bindings[i]);
        afree(globals[i]);
    }
    free(globals);
    free(catalogs);
    free(bindings);
    free(packs);
    return out;
}
