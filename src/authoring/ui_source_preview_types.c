#include "authoring/ui_source_preview_definition.h"

#include <stddef.h>
#define F(FIELD, NAME, TYPE, SPELLING)                                         \
    { .name = NAME,                                                            \
      .type = TYPE,                                                            \
      .type_name = SPELLING,                                                   \
      .offset = offsetof(ui_source_preview_result, FIELD),                     \
      .size = sizeof(((ui_source_preview_result *)0)->FIELD) }
static const type_field FIELDS[] = {
    F(ok, "Ok", PIN_BOOL, "bool"),
    F(stale, "Stale", PIN_BOOL, "bool"),
    F(preview, "Preview", PIN_I32, "int"),
    F(document, "Document", PIN_I32, "int"),
    F(revision, "Revision", PIN_I32, "int"),
    F(session, "Session", PIN_I32, "int"),
    F(width, "Width", PIN_I32, "int"),
    F(height, "Height", PIN_I32, "int"),
    F(texture, "Texture", PIN_U64, "uint64"),
    F(texture_revision, "TextureRevision", PIN_U64, "uint64"),
    F(context, "Context", PIN_STR, "const char *"),
    F(error, "Error", PIN_STR, "const char *")
};
const type_desc UI_SOURCE_PREVIEW_RESULT_LAYOUT = {
    .name = "UiSourcePreviewResult",
    .size = sizeof(ui_source_preview_result),
    .fields = FIELDS,
    .field_count = (int)(sizeof FIELDS / sizeof *FIELDS)
};
static const type_desc *const TYPES[] = { &UI_SOURCE_PREVIEW_RESULT_LAYOUT };
const native_type_definition UI_SOURCE_PREVIEW_TYPE_DEFINITIONS[1] = {
    { .layout = &UI_SOURCE_PREVIEW_RESULT_LAYOUT, .storage = PIN_STRUCT }
};
const type_catalog_pack UI_SOURCE_PREVIEW_TYPE_PACK = { .types = TYPES,
                                                        .type_count = 1 };
