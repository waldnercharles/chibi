#ifndef CHIBI_RUNTIME_PROPERTIES_H
#define CHIBI_RUNTIME_PROPERTIES_H
#include "authoring/component_presentation.h"
#include "ecs/ecs.h"
#include "support/arena.h"
#include "types/array.h"
#include "types/catalog.h"

typedef struct
{
    const char *component, *field, *label, *kind, *json, *text, *target, *unit,
        *presentation_category, *icon;
    double number, min, max;
    bool boolean, writable, bounded, color, computed;
} runtime_property;
typedef struct
{
    const char *component, *field, *label, *json;
} runtime_property_choice;
extern const type_desc RUNTIME_PROPERTY_LAYOUT, RUNTIME_PROPERTY_CHOICE_LAYOUT;
extern const type_catalog_pack RUNTIME_PROPERTY_TYPE_PACK;
/* Tokens include both ECS slot and generation. A caller must additionally bind
 * them to its committed world/session lifetime. */
void runtime_subject_token(ecs_entity, char out[64]);
bool runtime_subject_resolve(const ecs_world *, const char *, ecs_entity *);
/* Present leaf fields only; depth8,4096 leaves,256/page,1024 enum choices,
 *64KiB/value,1MiB returned JSON. Unsupported fields remain explicitly read-only.
 * Reflection catalog scope belongs to caller. Result payloads belong to arena.  */
bool runtime_properties_query(
    const ecs_world *,
    ecs_entity,
    int first,
    int count,
    arena *,
    pin_array **rows,
    pin_array **choices,
    int *total,
    char *error,
    size_t
);
void runtime_properties_present(const ecs_world *, pin_array *, arena *, const component_presentation *);
/* Exact JSON scalar decode; owning ECS publication copies managed strings and
 * validates contracts. Caller separately enforces session/role policy. */
bool runtime_properties_write(
    ecs_world *,
    ecs_entity,
    const char *component,
    const char *field,
    const char *json,
    char *error,
    size_t
);
#endif
