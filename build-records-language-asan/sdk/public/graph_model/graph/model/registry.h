#ifndef CHIBI_REGISTRY_H
#define CHIBI_REGISTRY_H

#include "execution/service.h"
#include "graph/model/node.h"
#include "types/catalog.h"

typedef struct registry_pack
{
    const char *name;
    node_desc *const *nodes;
    int node_count;
    const type_desc *const *types;
    int type_count;
    const service_desc *const *services;
    int service_count;
    const enum_desc *const *enums;
    int enum_count;
} registry_pack;

/* Copies the application pack list; descriptors and packs must outlive
 * all graphs. Set only at a quiescent authoring boundary. */
void registry_set_packs(const registry_pack *const *, int count);
int registry_pack_count();
const registry_pack *registry_pack_at(int index);

/* Native application composition declarations, never read by the model. */
extern const registry_pack *const REGISTRY_PACKS[];
extern const int REGISTRY_PACK_COUNT;

int registry_validate_packs(const registry_pack *const *, int count);
void registry_validate(void);

int registry_node_count(void);
const node_desc *registry_node(int index);
const node_desc *registry_node_by_name(const char *name);

int registry_service_count(void);
const service_desc *registry_service(int index);

#endif
