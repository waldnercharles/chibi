#include "authoring/studio_definition.h"
#include "execution/function.h"

#include <assert.h>
#include <stddef.h>
#include <string.h>
static const char MISSING[] = "Editor nodes require Studio";
#define TEXT_FIELD(owner, field, text)                                         \
    { .name = #field,                                                          \
      .type = PIN_STR,                                                         \
      .offset = offsetof(owner, field),                                        \
      .size = sizeof(const char *),                                            \
      .type_name = "const char *",                                             \
      .description = text }
#define INT_FIELD(owner, field, text)                                          \
    { .name = #field,                                                          \
      .type = PIN_I32,                                                         \
      .offset = offsetof(owner, field),                                        \
      .size = sizeof(int),                                                     \
      .type_name = "int",                                                      \
      .description = text }
#define BOOL_FIELD(owner, field, text)                                         \
    { .name = #field,                                                          \
      .type = PIN_BOOL,                                                        \
      .offset = offsetof(owner, field),                                        \
      .size = sizeof(bool),                                                    \
      .type_name = "bool",                                                     \
      .description = text }

static const type_field OBJECT_ROW_FIELDS[] = {
    TEXT_FIELD(studio_object_row, key, "The object's key: its entity as text."),
    TEXT_FIELD(studio_object_row, label, "The object's name."),
    TEXT_FIELD(studio_object_row, icon, "Its prototype's icon, a glyph."),
    TEXT_FIELD(studio_object_row, prototype, "Its prototype's name."),
    TEXT_FIELD(studio_object_row, drop, "The drag types a drop on it takes."),
    INT_FIELD(studio_object_row, depth, "Its depth under the scene's root."),
    INT_FIELD(studio_object_row, behaviors, "How many behaviors it runs."),
    BOOL_FIELD(studio_object_row, active, "It is the active object."),
    BOOL_FIELD(studio_object_row, marked, "It is selected, and not active."),
    BOOL_FIELD(studio_object_row, hidden, "Hidden in the editor."),
    BOOL_FIELD(studio_object_row, locked, "Locked: the Scene cannot pick it."),
    BOOL_FIELD(studio_object_row, renaming, "Its name is being edited in place."),
};
const type_desc STUDIO_OBJECT_ROW_LAYOUT = {
    .name = STUDIO_OBJECT_ROW_TYPE,
    .size = sizeof(studio_object_row),
    .fields = OBJECT_ROW_FIELDS,
    .field_count = sizeof OBJECT_ROW_FIELDS / sizeof *OBJECT_ROW_FIELDS,
};

static const type_field CHOICE_FIELDS[] = {
    TEXT_FIELD(studio_choice, value, "What choosing it sends."),
    TEXT_FIELD(studio_choice, label, "Its label."),
    TEXT_FIELD(studio_choice, icon, "Its icon, a glyph."),
};
const type_desc STUDIO_CHOICE_LAYOUT = {
    .name = STUDIO_CHOICE_TYPE,
    .size = sizeof(studio_choice),
    .fields = CHOICE_FIELDS,
    .field_count = sizeof CHOICE_FIELDS / sizeof *CHOICE_FIELDS,
};

static const type_desc *const STUDIO_TYPES[] = {
    &STUDIO_OBJECT_ROW_LAYOUT,
    &STUDIO_CHOICE_LAYOUT,
};
const type_catalog_pack STUDIO_TYPE_PACK = {
    .types = STUDIO_TYPES,
    .type_count = sizeof STUDIO_TYPES / sizeof *STUDIO_TYPES,
};

static const char *read_host(
    const behavior_function_context *context,
    void *host,
    studio_node_op op,
    const pin_value *inputs,
    pin_value *outputs,
    int count
)
{
    const studio_node_service *service = host;
    pin_value values[4] = { 0 };
    assert(count <= 4 && context->value_storage);
    const char *error = service->run(service->studio, op, inputs, context->value_storage, values);
    if (error) {
        return error;
    }
    for (int i = 0; i < count; i++) {
        pin_value *out = &outputs[i];
        pin_value value = values[i];
        if (out->type != value.type || out->container != value.container) {
            return "Studio result differs from its declared type";
        }
        if (value.type == PIN_STRUCT && value.container == PIN_CONTAINER_SINGLE) {
            const type_desc
                *type = !strcmp(out->type_name, STUDIO_OBJECT_ROW_LAYOUT.name)
                          ? &STUDIO_OBJECT_ROW_LAYOUT
                      : !strcmp(out->type_name, STUDIO_CHOICE_LAYOUT.name)
                          ? &STUDIO_CHOICE_LAYOUT
                          : NULL;
            assert(type && out->ptr && value.ptr);
            memcpy(out->ptr, value.ptr, type->size);
            continue;
        }
        value.type_name = out->type_name;
        *out = value;
    }
    return NULL;
}

static const char *effect_node(void *service, studio_node_op op, const pin_value *in)
{
    const studio_node_service *studio = service;
    return studio->run(studio->studio, op, in, NULL, NULL);
}

#define CALLBACK(ID, OP, NAME, SOURCE, KIND, PURE, EFFECT, RESULTS)                     \
    static const char *ID##_context(                                                    \
        const behavior_function_context *context,                                       \
        void *service,                                                                  \
        const pin_value *inputs,                                                        \
        pin_value *outputs                                                              \
    )                                                                                   \
    {                                                                                   \
        return read_host(context, service, OP, inputs, outputs, RESULTS);               \
    }                                                                                   \
    static const char *ID##_invoke(void *service, const pin_value *inputs, pin_value *) \
    {                                                                                   \
        return effect_node(service, OP, inputs);                                        \
    }
STUDIO_OPERATIONS(CALLBACK)
#undef CALLBACK
#define CALL(ID, OP, NAME, SOURCE, KIND, PURE, EFFECT, RESULTS)                \
    [OP] = { .service = "studio",                                              \
             .missing_service = MISSING,                                       \
             .context = EFFECT ? NULL : ID##_context,                          \
             .invoke = EFFECT ? ID##_invoke : NULL },
const node_host_call STUDIO_CALLS[STUDIO_NODE_COUNT] = { STUDIO_OPERATIONS(CALL) };
#undef CALL
const node_access STUDIO_READ_ACCESS = { .flags = NODE_EFFECT_WORLD_READ };
const node_access STUDIO_WRITE_ACCESS = { .flags = NODE_EFFECT_HOST_SINK };
const native_type_definition STUDIO_TYPE_DEFINITIONS[2] = {
    { .layout = &STUDIO_OBJECT_ROW_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &STUDIO_CHOICE_LAYOUT, .storage = PIN_STRUCT },
};
