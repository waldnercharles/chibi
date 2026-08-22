#include "registry.h"

#include <assert.h>
#include <string.h>

/* Descriptor invariants, enforced once at registry init instead of on every
 * activation. Runtime state (arena allocation, a missing struct input) stays
 * in eval.c where it belongs. */
static int node_desc_validate(const node_desc *d)
{
    if (!d || !d->name) {
        return 0;
    }
    if (d->input_count < 0 || d->output_count < 0) {
        return 0;
    }
    if ((d->input_count == 0) != (d->inputs == NULL)) {
        return 0;
    }
    if ((d->output_count == 0) != (d->outputs == NULL)) {
        return 0;
    }
    switch (d->pure_kind) {
        case NODE_PURE_CONST: break;
        case NODE_PURE_MAKE:
            if (!d->type || d->input_count != d->type->field_count ||
                d->output_count != 1 || d->outputs[0].type != PIN_STRUCT) {
                return 0;
            }
            break;
        case NODE_PURE_BREAK:
            if (!d->type || d->input_count != 1 || d->inputs[0].type != PIN_STRUCT ||
                d->output_count != d->type->field_count) {
                return 0;
            }
            break;
        case NODE_PURE_CALL:
            if (!d->fn || d->output_count != 1) {
                return 0;
            }
            break;
        case NODE_PURE_GET:
        case NODE_PURE_SELF:
            if (d->input_count != 0 || d->output_count != 1 || !d->outputs ||
                d->outputs[0].kind != PIN_DATA) {
                return 0;
            }
            break;
        case NODE_PURE_FUNCTION:
        case NODE_PURE_VOID_CALL: break;
        default: return 0;
    }
    return 1;
}

static int array_shape(const void *items, int count)
{
    return count >= 0 && (count == 0 || items);
}

static int type_desc_validate(const type_desc *type)
{
    if (!type || !type->name || !array_shape(type->fields, type->field_count)) {
        return 0;
    }
    for (int i = 0; i < type->field_count; i++) {
        if (!type->fields[i].name || !type->fields[i].type_name) {
            return 0;
        }
    }
    return 1;
}

static int pack_shape(const registry_pack *pack)
{
    return pack && pack->name && array_shape(pack->nodes, pack->node_count) &&
           array_shape(pack->types, pack->type_count) &&
           array_shape(pack->services, pack->service_count) &&
           array_shape(pack->operators, pack->operator_count);
}

static int node_identity_unique(
    const registry_pack *const packs[],
    int pack_index,
    int item_index,
    const char *name
)
{
    for (int i = 0; i <= pack_index; i++) {
        const registry_pack *pack = packs[i];
        int limit = i == pack_index ? item_index : pack->node_count;
        for (int j = 0; j < limit; j++) {
            if (strcmp(pack->nodes[j]->name, name) == 0) {
                return 0;
            }
        }
    }
    return 1;
}

static int type_identity_unique(
    const registry_pack *const packs[],
    int pack_index,
    int item_index,
    const char *name
)
{
    for (int i = 0; i <= pack_index; i++) {
        const registry_pack *pack = packs[i];
        int limit = i == pack_index ? item_index : pack->type_count;
        for (int j = 0; j < limit; j++) {
            if (strcmp(pack->types[j]->name, name) == 0) {
                return 0;
            }
        }
    }
    return 1;
}

static int service_identity_unique(
    const registry_pack *const packs[],
    int pack_index,
    int item_index,
    const service_desc *service
)
{
    for (int i = 0; i <= pack_index; i++) {
        const registry_pack *pack = packs[i];
        int limit = i == pack_index ? item_index : pack->service_count;
        for (int j = 0; j < limit; j++) {
            const service_desc *prior = pack->services[j];
            if (strcmp(prior->name, service->name) == 0 ||
                prior->stable_id == service->stable_id) {
                return 0;
            }
        }
    }
    return 1;
}

static int operator_identity_unique(
    const registry_pack *const packs[],
    int pack_index,
    int item_index,
    const operator_signature *signature
)
{
    for (int i = 0; i <= pack_index; i++) {
        const registry_pack *pack = packs[i];
        int limit = i == pack_index ? item_index : pack->operator_count;
        for (int j = 0; j < limit; j++) {
            const operator_signature *prior = &pack->operators[j];
            if (strcmp(prior->name, signature->name) == 0 &&
                prior->left == signature->left && prior->right == signature->right) {
                return 0;
            }
        }
    }
    return 1;
}

int registry_validate_packs(const registry_pack *const packs[], int pack_count)
{
    if (pack_count < 0 || (pack_count > 0 && !packs)) {
        return 0;
    }
    for (int i = 0; i < pack_count; i++) {
        const registry_pack *pack = packs[i];
        if (!pack_shape(pack)) {
            return 0;
        }
        for (int prior = 0; prior < i; prior++) {
            if (strcmp(packs[prior]->name, pack->name) == 0) {
                return 0;
            }
        }
        for (int j = 0; j < pack->node_count; j++) {
            if (!node_desc_validate(pack->nodes[j]) ||
                !node_identity_unique(packs, i, j, pack->nodes[j]->name)) {
                return 0;
            }
        }
        for (int j = 0; j < pack->type_count; j++) {
            if (!type_desc_validate(pack->types[j]) ||
                !type_identity_unique(packs, i, j, pack->types[j]->name)) {
                return 0;
            }
        }
        for (int j = 0; j < pack->service_count; j++) {
            const service_desc *service = pack->services[j];
            if (!service || !service->name || !service->name[0] || !service->stable_id ||
                !service_identity_unique(packs, i, j, service)) {
                return 0;
            }
        }
        for (int j = 0; j < pack->operator_count; j++) {
            const operator_signature *signature = &pack->operators[j];
            if (!signature->name || !signature->name[0] ||
                !operator_identity_unique(packs, i, j, signature)) {
                return 0;
            }
        }
    }
    return 1;
}

void registry_validate(void)
{
    assert(registry_validate_packs(REGISTRY_PACKS, REGISTRY_PACK_COUNT));
}
