#include "graph/model/registry.h"

#include "support/xalloc.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static const registry_pack **packs;
static int pack_count;

void registry_set_packs(const registry_pack *const *value, int count)
{
    assert(count >= 0 && (!count || value));
    assert(registry_validate_packs(value, count));
    const registry_pack **copy = count ? xmalloc(sizeof *copy * (size_t)count) : NULL;
    if (count) {
        memcpy(copy, value, sizeof *copy * (size_t)count);
    }
    free(packs);
    packs = copy;
    pack_count = count;
}

void registry_validate(void)
{
    assert(registry_validate_packs(packs, pack_count));
}

int registry_pack_count()
{
    return pack_count;
}

const registry_pack *registry_pack_at(int index)
{
    assert(index >= 0 && index < pack_count);
    return packs[index];
}

int registry_node_count(void)
{
    int count = 0;
    for (int i = 0; i < pack_count; i++) {
        count += packs[i]->node_count;
    }
    return count;
}

const node_desc *registry_node(int index)
{
    if (index < 0) {
        return NULL;
    }
    for (int i = 0; i < pack_count; i++) {
        const registry_pack *pack = packs[i];
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
    for (int i = 0; i < pack_count; i++) {
        const registry_pack *pack = packs[i];
        for (int j = 0; j < pack->node_count; j++) {
            if (strcmp(pack->nodes[j]->name, name) == 0) {
                return pack->nodes[j];
            }
        }
    }
    return NULL;
}

int registry_service_count(void)
{
    int count = 0;
    for (int i = 0; i < pack_count; i++) {
        count += packs[i]->service_count;
    }
    return count;
}

const service_desc *registry_service(int index)
{
    if (index < 0) {
        return NULL;
    }
    for (int i = 0; i < pack_count; i++) {
        const registry_pack *pack = packs[i];
        if (index < pack->service_count) {
            return pack->services[index];
        }
        index -= pack->service_count;
    }
    return NULL;
}
