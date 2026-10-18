#include "authoring/studio_outliner_schema.h"

#include "capabilities/catalog.h"
#include "types/record.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>

#define FIELD(OWNER, MEMBER, TYPE, NAME)                                       \
    { .name = #MEMBER,                                                         \
      .type = TYPE,                                                            \
      .offset = offsetof(OWNER, MEMBER),                                       \
      .size = sizeof(((OWNER *)0)->MEMBER),                                    \
      .type_name = NAME }
static const type_field ACTOR_FIELDS[] = {
    FIELD(studio_outliner_actor, actor, PIN_HANDLE, "ecs.entity"),
    FIELD(studio_outliner_actor, name, PIN_STR, "const char *"),
    FIELD(studio_outliner_actor, parent, PIN_I32, "int"),
    FIELD(studio_outliner_actor, prototype, PIN_STR, "const char *"),
    FIELD(studio_outliner_actor, prototypeIndex, PIN_I32, "int"),
    FIELD(studio_outliner_actor, fieldFirst, PIN_I32, "int"),
    FIELD(studio_outliner_actor, fieldCount, PIN_I32, "int"),
    FIELD(studio_outliner_actor, viewFlags, PIN_I32, "int"),
    FIELD(studio_outliner_actor, selected, PIN_BOOL, "bool"),
};
static const type_field PROTOTYPE_FIELDS[] = {
    FIELD(studio_outliner_prototype, componentFirst, PIN_I32, "int"),
    FIELD(studio_outliner_prototype, componentCount, PIN_I32, "int"),
};
static const type_field SCENE_FIELDS[] = {
    FIELD(studio_outliner_scene, activeIndex, PIN_I32, "int"),
    FIELD(studio_outliner_scene, rename, PIN_HANDLE, "ecs.entity"),
    FIELD(studio_outliner_scene, renameOutliner, PIN_BOOL, "bool"),
    FIELD(studio_outliner_scene, editable, PIN_BOOL, "bool"),
    FIELD(studio_outliner_scene, filter, PIN_STR, "const char *"),
    FIELD(studio_outliner_scene, scenePath, PIN_STR, "const char *"),
};
#undef FIELD
#define LAYOUT(NAME, TYPE, FIELDS)                                             \
    { .name = NAME,                                                            \
      .size = sizeof(TYPE),                                                    \
      .fields = FIELDS,                                                        \
      .field_count = sizeof FIELDS / sizeof *FIELDS }
const type_desc STUDIO_OUTLINER_ACTOR_LAYOUT = LAYOUT(STUDIO_OUTLINER_ACTOR_TYPE, studio_outliner_actor, ACTOR_FIELDS);
const type_desc STUDIO_OUTLINER_PROTOTYPE_LAYOUT = LAYOUT(
    STUDIO_OUTLINER_PROTOTYPE_TYPE,
    studio_outliner_prototype,
    PROTOTYPE_FIELDS
);
const type_desc STUDIO_OUTLINER_SCENE_LAYOUT = LAYOUT(STUDIO_OUTLINER_SCENE_TYPE, studio_outliner_scene, SCENE_FIELDS);
#undef LAYOUT
static const type_desc *const TYPES[] = {
    &STUDIO_OUTLINER_ACTOR_LAYOUT,
    &STUDIO_OUTLINER_PROTOTYPE_LAYOUT,
    &STUDIO_OUTLINER_SCENE_LAYOUT,
};
const type_catalog_pack STUDIO_OUTLINER_TYPE_PACK = { .types = TYPES,
                                                      .type_count = sizeof TYPES /
                                                                    sizeof *TYPES };

static bool schema(
    const pin_value *value,
    const char *name,
    const char *identity,
    pin_container container,
    const char *const *names,
    const pin_type *types,
    int count
)
{
    if (!value || value->type != PIN_STRUCT || value->container != container ||
        !value->type_name || strcmp(value->type_name, name) ||
        !pin_record_valid(value->record)) {
        return false;
    }
    const pin_record *record = value->record;
    if (record->id != script_catalog_id("symbol", identity) ||
        strcmp(record->type.name, name) || record->type.field_count != count) {
        return false;
    }
    /* pin_record_valid checks canonical target offsets, sizes and layout hash. */
    for (int i = 0; i < count; i++) {
        const type_field *field = &record->type.fields[i];
        if (strcmp(field->name, names[i]) || field->type != types[i] || field->type_name ||
            field->range_contract || field->unit || field->min || field->max) {
            return false;
        }
    }
    return true;
}
void studio_outliner_records(arena *storage, const pin_record **row, const pin_record **projection)
{
    static const type_field rows[] = {
        { .name = "key", .type = PIN_STR },
        { .name = "label", .type = PIN_STR },
        { .name = "icon", .type = PIN_STR },
        { .name = "prototype", .type = PIN_STR },
        { .name = "drop", .type = PIN_STR },
        { .name = "depth", .type = PIN_I32 },
        { .name = "behaviors", .type = PIN_I32 },
        { .name = "active", .type = PIN_BOOL },
        { .name = "marked", .type = PIN_BOOL },
        { .name = "hidden", .type = PIN_BOOL },
        { .name = "locked", .type = PIN_BOOL },
        { .name = "renaming", .type = PIN_BOOL },
    };
    static const type_field projections[] = {
        { .name = "rootLabel", .type = PIN_STR },
        { .name = "rootIcon", .type = PIN_STR },
        { .name = "rootParent", .type = PIN_BOOL },
        { .name = "rootDepth", .type = PIN_I32 },
        { .name = "rootOpen", .type = PIN_BOOL },
        { .name = "rootShown", .type = PIN_BOOL },
        { .name = "count", .type = PIN_I32 },
        { .name = "total", .type = PIN_I32 },
        { .name = "filtered", .type = PIN_BOOL },
        { .name = "notice", .type = PIN_STR },
        { .name = "revealRow", .type = PIN_I32 },
    };
    *row = pin_record_create(
        storage,
        "OutlinerRowPolicies.Row",
        script_catalog_id("symbol", "libraries/OutlinerRowPolicies.chibi:1:Row"),
        rows,
        sizeof rows / sizeof *rows
    );
    *projection = pin_record_create(
        storage,
        "OutlinerRowPolicies.Projection",
        script_catalog_id("symbol", "libraries/OutlinerRowPolicies.chibi:1:Projection"),
        projections,
        sizeof projections / sizeof *projections
    );
    assert(*row && *projection);
}
bool studio_outliner_rows_shape(const pin_value *value)
{
    static const char *const names[] = { "key",       "label",  "icon",
                                         "prototype", "drop",   "depth",
                                         "behaviors", "active", "marked",
                                         "hidden",    "locked", "renaming" };
    static const pin_type types[] = { PIN_STR,  PIN_STR,  PIN_STR,  PIN_STR,
                                      PIN_STR,  PIN_I32,  PIN_I32,  PIN_BOOL,
                                      PIN_BOOL, PIN_BOOL, PIN_BOOL, PIN_BOOL };
    return schema(
        value,
        "OutlinerRowPolicies.Row",
        "libraries/OutlinerRowPolicies.chibi:1:Row",
        PIN_CONTAINER_ARRAY,
        names,
        types,
        sizeof types / sizeof *types
    );
}
bool studio_outliner_projection_shape(const pin_value *value)
{
    static const char *const names[] = { "rootLabel", "rootIcon", "rootParent",
                                         "rootDepth", "rootOpen", "rootShown",
                                         "count",     "total",    "filtered",
                                         "notice",    "revealRow" };
    static const pin_type types[] = { PIN_STR,  PIN_STR,  PIN_BOOL, PIN_I32,
                                      PIN_BOOL, PIN_BOOL, PIN_I32,  PIN_I32,
                                      PIN_BOOL, PIN_STR,  PIN_I32 };
    return schema(
        value,
        "OutlinerRowPolicies.Projection",
        "libraries/OutlinerRowPolicies.chibi:1:Projection",
        PIN_CONTAINER_SINGLE,
        names,
        types,
        sizeof types / sizeof *types
    );
}
