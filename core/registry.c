#include "registry.h"

#include <string.h>

int registry_node_count(void)
{
    int count = 0;
    for (int i = 0; i < REGISTRY_PACK_COUNT; i++) {
        count += REGISTRY_PACKS[i]->node_count;
    }
    return count;
}

const node_desc *registry_node(int index)
{
    if (index < 0) {
        return NULL;
    }
    for (int i = 0; i < REGISTRY_PACK_COUNT; i++) {
        const registry_pack *pack = REGISTRY_PACKS[i];
        if (index < pack->node_count) {
            return pack->nodes[index];
        }
        index -= pack->node_count;
    }
    return NULL;
}

const node_desc *registry_node_by_name(const char *name)
{
    if (!name) {
        return NULL;
    }
    for (int i = 0; i < REGISTRY_PACK_COUNT; i++) {
        const registry_pack *pack = REGISTRY_PACKS[i];
        for (int j = 0; j < pack->node_count; j++) {
            if (strcmp(pack->nodes[j]->name, name) == 0) {
                return pack->nodes[j];
            }
        }
    }
    return NULL;
}

int registry_type_count(void)
{
    int count = 0;
    for (int i = 0; i < REGISTRY_PACK_COUNT; i++) {
        count += REGISTRY_PACKS[i]->type_count;
    }
    return count;
}

const type_desc *registry_type(int index)
{
    if (index < 0) {
        return NULL;
    }
    for (int i = 0; i < REGISTRY_PACK_COUNT; i++) {
        const registry_pack *pack = REGISTRY_PACKS[i];
        if (index < pack->type_count) {
            return pack->types[index];
        }
        index -= pack->type_count;
    }
    return NULL;
}

const type_desc *registry_type_by_name(const char *name)
{
    if (!name) {
        return NULL;
    }
    for (int i = 0; i < REGISTRY_PACK_COUNT; i++) {
        const registry_pack *pack = REGISTRY_PACKS[i];
        for (int j = 0; j < pack->type_count; j++) {
            if (strcmp(pack->types[j]->name, name) == 0) {
                return pack->types[j];
            }
        }
    }
    return NULL;
}

int registry_service_count(void)
{
    int count = 0;
    for (int i = 0; i < REGISTRY_PACK_COUNT; i++) {
        count += REGISTRY_PACKS[i]->service_count;
    }
    return count;
}

const service_desc *registry_service(int index)
{
    if (index < 0) {
        return NULL;
    }
    for (int i = 0; i < REGISTRY_PACK_COUNT; i++) {
        const registry_pack *pack = REGISTRY_PACKS[i];
        if (index < pack->service_count) {
            return pack->services[index];
        }
        index -= pack->service_count;
    }
    return NULL;
}

const service_desc *registry_service_by_name(const char *name)
{
    if (!name) {
        return NULL;
    }
    for (int i = 0; i < REGISTRY_PACK_COUNT; i++) {
        const registry_pack *pack = REGISTRY_PACKS[i];
        for (int j = 0; j < pack->service_count; j++) {
            if (strcmp(pack->services[j]->name, name) == 0) {
                return pack->services[j];
            }
        }
    }
    return NULL;
}

int registry_operator_count(void)
{
    int count = 0;
    for (int i = 0; i < REGISTRY_PACK_COUNT; i++) {
        count += REGISTRY_PACKS[i]->operator_count;
    }
    return count;
}

const operator_signature *registry_operator(int index)
{
    if (index < 0) {
        return NULL;
    }
    for (int i = 0; i < REGISTRY_PACK_COUNT; i++) {
        const registry_pack *pack = REGISTRY_PACKS[i];
        if (index < pack->operator_count) {
            return &pack->operators[index];
        }
        index -= pack->operator_count;
    }
    return NULL;
}
