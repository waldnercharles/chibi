#include "authoring/ui_dom_definition.h"

#include <stddef.h>
#define FIELD(T, F, N, P, S)                                                   \
    { .name = N,                                                               \
      .type = P,                                                               \
      .type_name = S,                                                          \
      .offset = offsetof(T, F),                                                \
      .size = sizeof(((T *)0)->F) }
#define R(F, N, P, S) FIELD(ui_dom_row, F, N, P, S)
static const type_field ROW_FIELDS[] = {
    R(key, "Key", PIN_STR, "const char *"),
    R(parent, "Parent", PIN_STR, "const char *"),
    R(tag, "Tag", PIN_STR, "const char *"),
    R(id, "Id", PIN_STR, "const char *"),
    R(classes, "Classes", PIN_STR, "const char *"),
    R(text, "Text", PIN_STR, "const char *"),
    R(depth, "Depth", PIN_I32, "int"),
    R(x, "X", PIN_F64, "double"),
    R(y, "Y", PIN_F64, "double"),
    R(width, "Width", PIN_F64, "double"),
    R(height, "Height", PIN_F64, "double"),
    R(content_width, "ContentWidth", PIN_F64, "double"),
    R(content_height, "ContentHeight", PIN_F64, "double"),
    R(margin_top, "MarginTop", PIN_F64, "double"),
    R(margin_right, "MarginRight", PIN_F64, "double"),
    R(margin_bottom, "MarginBottom", PIN_F64, "double"),
    R(margin_left, "MarginLeft", PIN_F64, "double"),
    R(border_top, "BorderTop", PIN_F64, "double"),
    R(border_right, "BorderRight", PIN_F64, "double"),
    R(border_bottom, "BorderBottom", PIN_F64, "double"),
    R(border_left, "BorderLeft", PIN_F64, "double"),
    R(padding_top, "PaddingTop", PIN_F64, "double"),
    R(padding_right, "PaddingRight", PIN_F64, "double"),
    R(padding_bottom, "PaddingBottom", PIN_F64, "double"),
    R(padding_left, "PaddingLeft", PIN_F64, "double"),
    R(dp_ratio, "DpRatio", PIN_F64, "double"),
    R(source_asset, "SourceAsset", PIN_STR, "const char *"),
    R(source_key, "SourceKey", PIN_STR, "const char *"),
    R(source_line, "SourceLine", PIN_I32, "int"),
    R(source_start, "SourceStart", PIN_I32, "int"),
    R(visible, "Visible", PIN_BOOL, "bool")
};
#define P(F, N, T, S) FIELD(ui_dom_property, F, N, T, S)
static const type_field PROPERTY_FIELDS[] = {
    P(name, "Name", PIN_STR, "const char *"),
    P(value, "Value", PIN_STR, "const char *"),
    P(path, "Path", PIN_STR, "const char *"),
    P(rule, "Rule", PIN_STR, "const char *"),
    P(line, "Line", PIN_I32, "int"),
    P(specificity, "Specificity", PIN_I32, "int")
};
#define S(F, N, T, K) FIELD(ui_dom_status, F, N, T, K)
static const type_field STATUS_FIELDS[] = { S(ok, "Ok", PIN_BOOL, "bool"),
                                            S(complete, "Complete", PIN_BOOL, "bool"),
                                            S(revision, "Revision", PIN_I32, "int"),
                                            S(total, "Total", PIN_I32, "int"),
                                            S(error, "Error", PIN_STR, "const char *") };
#define COUNT(A) ((int)(sizeof A / sizeof *A))
static const type_field CONTEXT_FIELDS[] = {
    FIELD(ui_dom_context_row, key, "Key", PIN_STR, "const char *"),
    FIELD(ui_dom_context_row, selected, "Selected", PIN_BOOL, "bool")
};
const type_desc UI_DOM_CONTEXT_LAYOUT = { .name = "UiDomContext",
                                          .size = sizeof(ui_dom_context_row),
                                          .fields = CONTEXT_FIELDS,
                                          .field_count = COUNT(CONTEXT_FIELDS) };
const type_desc UI_DOM_ROW_LAYOUT = { .name = "UiDomRow",
                                      .size = sizeof(ui_dom_row),
                                      .fields = ROW_FIELDS,
                                      .field_count = COUNT(ROW_FIELDS) },
                UI_DOM_PROPERTY_LAYOUT = { .name = "UiDomProperty",
                                           .size = sizeof(ui_dom_property),
                                           .fields = PROPERTY_FIELDS,
                                           .field_count = COUNT(PROPERTY_FIELDS) },
                UI_DOM_STATUS_LAYOUT = { .name = "UiDomStatus",
                                         .size = sizeof(ui_dom_status),
                                         .fields = STATUS_FIELDS,
                                         .field_count = COUNT(STATUS_FIELDS) };
static const type_desc *const TYPES[] = { &UI_DOM_ROW_LAYOUT,
                                          &UI_DOM_PROPERTY_LAYOUT,
                                          &UI_DOM_STATUS_LAYOUT,
                                          &UI_DOM_CONTEXT_LAYOUT };
const type_catalog_pack UI_DOM_TYPE_PACK = { .types = TYPES,
                                             .type_count = COUNT(TYPES) };
const native_type_definition UI_DOM_TYPE_DEFINITIONS[4] = {
    { .layout = &UI_DOM_ROW_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &UI_DOM_PROPERTY_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &UI_DOM_STATUS_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &UI_DOM_CONTEXT_LAYOUT, .storage = PIN_STRUCT }
};
