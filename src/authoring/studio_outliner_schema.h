#ifndef CHIBI_STUDIO_OUTLINER_SCHEMA_H
#define CHIBI_STUDIO_OUTLINER_SCHEMA_H

#include "execution/function.h"
#include "types/catalog.h"

/* Copied facts only. Presentation policy belongs to OutlinerRowPolicies. */
typedef struct
{
    uint64_t actor;
    const char *name;
    int parent;
    const char *prototype;
    int prototypeIndex, fieldFirst, fieldCount, viewFlags;
    bool selected;
} studio_outliner_actor;
typedef struct
{
    int componentFirst, componentCount;
} studio_outliner_prototype;
typedef struct
{
    int activeIndex;
    uint64_t rename;
    bool renameOutliner, editable;
    const char *filter, *scenePath;
} studio_outliner_scene;

#define STUDIO_OUTLINER_ACTOR_TYPE "StudioOutlinerActor"
#define STUDIO_OUTLINER_PROTOTYPE_TYPE "StudioOutlinerPrototype"
#define STUDIO_OUTLINER_SCENE_TYPE "StudioOutlinerScene"
#define STUDIO_OUTLINER_SERVICE "studio.outliner"

enum
{
    STUDIO_OUTLINER_ACTORS,
    STUDIO_OUTLINER_PROTOTYPES,
    STUDIO_OUTLINER_COMPONENT_ICONS,
    STUDIO_OUTLINER_FIELD_KINDS,
    STUDIO_OUTLINER_ATTACHMENT_ENTITIES,
    STUDIO_OUTLINER_SCENE,
    STUDIO_OUTLINER_CAPTURE_OUTPUTS
};

/* The adapter checks runtime authority and the original request generations.
 * Capture fills all six declared headers/payloads in context->value_storage.
 * Publish borrows values for this call and reports acceptance separately from
 * execution failure. Neither callback may retain the context or input values. */
typedef struct
{
    void *user;
    const char *(*capture)(const behavior_function_context *, void *, pin_value *);
    const char *(*publish)(
        const behavior_function_context *,
        void *,
        const pin_value *,
        const pin_value *,
        bool *
    );
} studio_outliner_service;

extern const type_desc STUDIO_OUTLINER_ACTOR_LAYOUT,
    STUDIO_OUTLINER_PROTOTYPE_LAYOUT, STUDIO_OUTLINER_SCENE_LAYOUT;
extern const type_catalog_pack STUDIO_OUTLINER_TYPE_PACK;

/* Exact authored schemas, shared by preparation and the runtime publication
 * boundary. These inspect shapes, not presentation policy or authority. */
bool studio_outliner_projection_shape(const pin_value *);
bool studio_outliner_rows_shape(const pin_value *);
/* Trusted authored layouts, owned by storage. Row is returned first. */
void studio_outliner_records(arena *, const pin_record **row, const pin_record **projection);

#endif
