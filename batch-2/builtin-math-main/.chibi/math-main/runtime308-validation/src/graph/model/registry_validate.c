#include "graph/model/registry.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Authoring descriptor invariants are checked when explicit packs are composed.
 * A native execution contract does not require a graph evaluator callback. */
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
    if (d->flow_kind < NODE_FLOW_NONE || d->flow_kind > NODE_FLOW_CUSTOM) {
        return 0;
    }
    for (int direction = 0; direction < 2; direction++) {
        const pin_spec *pins = direction ? d->outputs : d->inputs;
        int count = direction ? d->output_count : d->input_count;
        for (int i = 0; i < count; i++) {
            if (!pins[i].name ||
                (pins[i].kind != PIN_DATA && pins[i].kind != PIN_EXEC)) {
                return 0;
            }
            if (pins[i].kind == PIN_EXEC && pins[i].type != PIN_FLOW) {
                return 0;
            }
        }
    }
    if (d->metadata_only) {
        return d->pure_kind >= NODE_PURE_CONST &&
               d->pure_kind <= NODE_PURE_READ && !d->foldable && !d->fn &&
               !d->read && !d->read_inputs && !d->flow && !d->lower;
    }
    switch (d->pure_kind) {
        case NODE_PURE_CONST: break;
        case NODE_PURE_COPY:
            if (d->input_count != 1 || d->output_count != 1) {
                return 0;
            }
            break;
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
            if ((!d->fn && !d->execution_call && !d->host_call) ||
                d->output_count != 1) {
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
        case NODE_PURE_READ:
            if ((!d->read && !d->read_inputs && !d->execution_call &&
                 !d->host_call && !d->access) ||
                (d->read && d->read_inputs) || !node_desc_is_pure(d) ||
                (d->read && d->input_count != 0) || d->output_count < 1 ||
                (d->read && d->output_count != 1)) {
                return 0;
            }
            break;
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

static int enum_desc_validate(const enum_desc *enumeration)
{
    if (!enumeration || !enumeration->name ||
        !array_shape(enumeration->values, enumeration->value_count)) {
        return 0;
    }
    for (int i = 0; i < enumeration->value_count; i++) {
        const enum_value_desc *value = &enumeration->values[i];
        if (!value->name || !value->name[0] || !value->label || !value->label[0]) {
            return 0;
        }
        for (int j = 0; j < i; j++) {
            if (enumeration->values[j].value == value->value) {
                return 0;
            }
        }
    }
    return 1;
}

static int pack_shape(const registry_pack *pack)
{
    return pack && pack->name && array_shape(pack->nodes, pack->node_count) &&
           array_shape(pack->types, pack->type_count) &&
           array_shape(pack->services, pack->service_count) &&
           array_shape(pack->enums, pack->enum_count);
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

static int enum_identity_unique(
    const registry_pack *const packs[],
    int pack_index,
    int item_index,
    const char *name
)
{
    for (int i = 0; i <= pack_index; i++) {
        const registry_pack *pack = packs[i];
        int limit = i == pack_index ? item_index : pack->enum_count;
        for (int j = 0; j < limit; j++) {
            if (strcmp(pack->enums[j]->name, name) == 0) {
                return 0;
            }
        }
    }
    return 1;
}

/* A pack is valid when every descriptor is valid and all identities are
 * unique in the supplied, explicit pack order. */
int registry_validate_packs(const registry_pack *const packs[], int pack_count)
{
    if (pack_count < 0 || (pack_count > 0 && !packs)) {
        return 0;
    }
    for (int i = 0; i < pack_count; i++) {
        const registry_pack *pack = packs[i];
        if (!pack_shape(pack)) {
            fprintf(stderr, "Invalid registry pack: %s\n", pack && pack->name ? pack->name : "<unnamed>");
            return 0;
        }
        for (int prior = 0; prior < i; prior++) {
            if (strcmp(packs[prior]->name, pack->name) == 0) {
                fprintf(stderr, "Duplicate registry pack: %s\n", pack->name);
                return 0;
            }
        }
        for (int j = 0; j < pack->node_count; j++) {
            const node_desc *node = pack->nodes[j];
            if (!node_desc_validate(node)) {
                fprintf(
                    stderr,
                    "Invalid node descriptor: pack %s, node %s\n",
                    pack->name,
                    node && node->name ? node->name : "<unnamed>"
                );
                return 0;
            }
            if (!node_identity_unique(packs, i, j, node->name)) {
                fprintf(stderr, "Duplicate node descriptor: pack %s, node %s\n", pack->name, node->name);
                return 0;
            }
        }
        for (int j = 0; j < pack->type_count; j++) {
            const type_desc *type = pack->types[j];
            if (!type_desc_validate(type) ||
                !type_identity_unique(packs, i, j, type->name)) {
                fprintf(
                    stderr,
                    "Invalid or duplicate type descriptor: pack %s, type %s\n",
                    pack->name,
                    type && type->name ? type->name : "<unnamed>"
                );
                return 0;
            }
        }
        for (int j = 0; j < pack->service_count; j++) {
            const service_desc *service = pack->services[j];
            if (!service || !service->name || !service->name[0] || !service->stable_id ||
                !service_identity_unique(packs, i, j, service)) {
                fprintf(
                    stderr,
                    "Invalid or duplicate service descriptor: pack %s, service "
                    "%s\n",
                    pack->name,
                    service && service->name ? service->name : "<unnamed>"
                );
                return 0;
            }
        }
        for (int j = 0; j < pack->enum_count; j++) {
            if (!enum_desc_validate(pack->enums[j]) ||
                !enum_identity_unique(packs, i, j, pack->enums[j]->name)) {
                const enum_desc *value = pack->enums[j];
                fprintf(
                    stderr,
                    "Invalid or duplicate enum descriptor: pack %s, enum %s\n",
                    pack->name,
                    value && value->name ? value->name : "<unnamed>"
                );
                return 0;
            }
        }
    }
    return 1;
}
