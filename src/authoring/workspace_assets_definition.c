#include "authoring/workspace_assets_definition.h"

#include <stddef.h>
#define TEXT(F)                                                                \
    { .name = #F,                                                              \
      .type = PIN_STR,                                                         \
      .type_name = "const char *",                                             \
      .offset = offsetof(workspace_asset_kind_row, F),                         \
      .size = sizeof(const char *) },
#define BOOL(F)                                                                \
    { .name = #F,                                                              \
      .type = PIN_BOOL,                                                        \
      .type_name = "bool",                                                     \
      .offset = offsetof(workspace_asset_kind_row, F),                         \
      .size = sizeof(bool) },
static const type_field KIND_FIELDS[] = { TEXT(kind) TEXT(folder) TEXT(extension)
                                              TEXT(description) BOOL(executable)
                                                  BOOL(complete) };
const type_desc WORKSPACE_ASSET_KIND_LAYOUT = { .name = "WorkspaceAssetKind",
                                                .size = sizeof(workspace_asset_kind_row),
                                                .fields = KIND_FIELDS,
                                                .field_count = 6 };
static const type_field FIELD_FIELDS[] = {
    { .name = "computed",
      .type = PIN_BOOL,
      .type_name = "bool",
      .offset = offsetof(workspace_asset_field, computed),
      .size = sizeof(bool) },
    { .name = "added",
      .type = PIN_BOOL,
      .type_name = "bool",
      .offset = offsetof(workspace_asset_field, added),
      .size = sizeof(bool) },
    { .name = "overridden",
      .type = PIN_BOOL,
      .type_name = "bool",
      .offset = offsetof(workspace_asset_field, overridden),
      .size = sizeof(bool) },

    { .name = "component",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_field, component),
      .size = sizeof(const char *) },
    { .name = "presentation_category",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_field, presentation_category),
      .size = sizeof(const char *) },
    { .name = "icon",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_field, icon),
      .size = sizeof(const char *) },
    { .name = "json",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_field, json),
      .size = sizeof(const char *) },
    { .name = "path",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_field, path),
      .size = sizeof(const char *) },
    { .name = "name",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_field, name),
      .size = sizeof(const char *) },
    { .name = "type",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_field, type),
      .size = sizeof(const char *) },
    { .name = "text",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_field, text),
      .size = sizeof(const char *) },
    { .name = "description",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_field, description),
      .size = sizeof(const char *) },
    { .name = "reference",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_field, reference),
      .size = sizeof(const char *) },
    { .name = "parent",
      .type = PIN_I32,
      .type_name = "int",
      .offset = offsetof(workspace_asset_field, parent),
      .size = sizeof(int) },
    { .name = "depth",
      .type = PIN_I32,
      .type_name = "int",
      .offset = offsetof(workspace_asset_field, depth),
      .size = sizeof(int) },
    { .name = "present",
      .type = PIN_BOOL,
      .type_name = "bool",
      .offset = offsetof(workspace_asset_field, present),
      .size = sizeof(bool) },
    { .name = "required",
      .type = PIN_BOOL,
      .type_name = "bool",
      .offset = offsetof(workspace_asset_field, required),
      .size = sizeof(bool) },
    { .name = "boolean",
      .type = PIN_BOOL,
      .type_name = "bool",
      .offset = offsetof(workspace_asset_field, boolean),
      .size = sizeof(bool) },
    { .name = "number",
      .type = PIN_F64,
      .type_name = "double",
      .offset = offsetof(workspace_asset_field, number),
      .size = sizeof(double) },
    { .name = "type_name",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_field, type_name),
      .size = sizeof(const char *) },
    { .name = "unit",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_field, unit),
      .size = sizeof(const char *) },
    { .name = "bounded",
      .type = PIN_BOOL,
      .type_name = "bool",
      .offset = offsetof(workspace_asset_field, bounded),
      .size = sizeof(bool) },
    { .name = "color",
      .type = PIN_BOOL,
      .type_name = "bool",
      .offset = offsetof(workspace_asset_field, color),
      .size = sizeof(bool) },
    { .name = "min",
      .type = PIN_F64,
      .type_name = "double",
      .offset = offsetof(workspace_asset_field, min),
      .size = sizeof(double) },
    { .name = "max",
      .type = PIN_F64,
      .type_name = "double",
      .offset = offsetof(workspace_asset_field, max),
      .size = sizeof(double) },
    { .name = "relative",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_field, relative),
      .size = sizeof(const char *) },
    { .name = "common",
      .type = PIN_BOOL,
      .type_name = "bool",
      .offset = offsetof(workspace_asset_field, common),
      .size = sizeof(bool) },
    { .name = "mixed",
      .type = PIN_BOOL,
      .type_name = "bool",
      .offset = offsetof(workspace_asset_field, mixed),
      .size = sizeof(bool) },
};
static const type_desc FIELD_LAYOUT = { .name = "WorkspaceAssetField",
                                        .size = sizeof(workspace_asset_field),
                                        .fields = FIELD_FIELDS,
                                        .field_count = NATIVE_COUNT(FIELD_FIELDS) };
static const type_field CHOICE_FIELDS[] = {
    { .name = "path",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_choice, path),
      .size = sizeof(const char *) },
    { .name = "value",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_asset_choice, value),
      .size = sizeof(const char *) },
};
static const type_desc CHOICE_LAYOUT = { .name = "WorkspaceAssetChoice",
                                         .size = sizeof(workspace_asset_choice),
                                         .fields = CHOICE_FIELDS,
                                         .field_count = 2 };

static const type_field COMPONENT_FIELDS[] = {
    { .name = "key",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_component_row, key),
      .size = sizeof(const char *) },
    { .name = "name",
      .type = PIN_STR,
      .type_name = "const char *",
      .offset = offsetof(workspace_component_row, name),
      .size = sizeof(const char *) },
    { .name = "fields",
      .type = PIN_I32,
      .type_name = "int",
      .offset = offsetof(workspace_component_row, fields),
      .size = sizeof(int) },
    { .name = "marker",
      .type = PIN_BOOL,
      .type_name = "bool",
      .offset = offsetof(workspace_component_row, marker),
      .size = sizeof(bool) }
};
static const type_desc COMPONENT_LAYOUT = { .name = "WorkspaceComponent",
                                            .size = sizeof(workspace_component_row),
                                            .fields = COMPONENT_FIELDS,
                                            .field_count = 4 };
static const type_desc *const TYPES[] = { &WORKSPACE_ASSET_KIND_LAYOUT,
                                          &FIELD_LAYOUT,
                                          &CHOICE_LAYOUT,
                                          &COMPONENT_LAYOUT };
const native_type_definition WORKSPACE_ASSETS_TYPE_DEFINITIONS[4] = {
    { .layout = &WORKSPACE_ASSET_KIND_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &FIELD_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &CHOICE_LAYOUT, .storage = PIN_STRUCT },
    { .layout = &COMPONENT_LAYOUT, .storage = PIN_STRUCT }
};
const type_catalog_pack WORKSPACE_ASSETS_TYPE_PACK = { .types = TYPES,
                                                       .type_count = sizeof TYPES /
                                                                     sizeof *TYPES };
#define PORT(NAME, TYPE, NOMINAL)                                              \
    { .name = NAME, .kind = NATIVE_PARAMETER_DATA, .storage = TYPE, .type_name = NOMINAL },
static const native_parameter_definition TEMPLATE_IN[] = {
    PORT("Kind", PIN_STR, "const char *") PORT("Name", PIN_STR, "const char *")
};
static const native_parameter_definition TEMPLATE_OUT[] = {
    PORT("Ok", PIN_BOOL, "bool") PORT("Error", PIN_STR, "const char *")
        PORT("Path", PIN_STR, "const char *") PORT("Text", PIN_STR, "const char *")
};
static const native_parameter_definition KINDS_OUT[] = {
    { .name = "Kinds",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STRUCT,
      .type_name = "WorkspaceAssetKind",
      .container = PIN_CONTAINER_ARRAY }
};

static const native_parameter_definition INSPECT_IN[] = {
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Kind", PIN_STR, "const char *")
};
static const native_parameter_definition INSPECT_OUT[] = {
    PORT("Ok", PIN_BOOL, "bool") PORT("Error", PIN_STR, "const char *"){
        .name = "Fields",
        .kind = NATIVE_PARAMETER_DATA,
        .storage = PIN_STRUCT,
        .type_name = "WorkspaceAssetField",
        .container = PIN_CONTAINER_ARRAY },
    { .name = "Choices",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STRUCT,
      .type_name = "WorkspaceAssetChoice",
      .container = PIN_CONTAINER_ARRAY }
};
static const native_parameter_definition EDIT_IN[] = {
    { .name = "Exec", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Kind", PIN_STR, "const char *") PORT("Operation", PIN_STR, "const char *")
            PORT("Path", PIN_STR, "const char *") PORT("Value", PIN_STR, "const char *")
};
static const native_parameter_definition EDIT_OUT[] = {
    { .name = "Then", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    PORT("Ok", PIN_BOOL, "bool") PORT("Error", PIN_STR, "const char *")
};
static const native_parameter_definition SCENE0_IN[] = {
    { .name = "Exec", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Parent", PIN_STR, "const char *") PORT("Name", PIN_STR, "const char *")
            PORT("Prototype", PIN_STR, "const char *")
};
static const native_parameter_definition SCENE1_IN[] = {
    { .name = "Exec", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Entity", PIN_STR, "const char *") PORT("Name", PIN_STR, "const char *")
};
static const native_parameter_definition SCENE2_IN[] = {
    { .name = "Exec", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Entity", PIN_STR, "const char *")
};
static const native_parameter_definition SCENE3_IN[] = {
    { .name = "Exec", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Entity", PIN_STR, "const char *") PORT("Name", PIN_STR, "const char *")
};
static const native_parameter_definition SCENE4_IN[] = {
    { .name = "Exec", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Entity", PIN_STR, "const char *") PORT("Parent", PIN_STR, "const char *")
            PORT("Before", PIN_STR, "const char *")
};
static const native_parameter_definition SCENE_OUT[] = {
    { .name = "Then", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    PORT("Ok", PIN_BOOL, "bool") PORT("Error", PIN_STR, "const char *")
};
static const native_parameter_definition IDENTITY_IN[] = {
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
};
static const native_parameter_definition IDENTITY_OUT[] = {
    PORT("Ok", PIN_BOOL, "bool") PORT("Kind", PIN_STR, "const char *")
        PORT("Name", PIN_STR, "const char *") PORT("Error", PIN_STR, "const char *")
};

static const native_parameter_definition COMPONENTS_OUT[] = {
    { .name = "Components",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STRUCT,
      .type_name = "WorkspaceComponent",
      .container = PIN_CONTAINER_ARRAY },
    PORT("Ok", PIN_BOOL, "bool") PORT("Error", PIN_STR, "const char *")
};
static const native_parameter_definition COMPONENT_IN[] = {
    { .name = "Exec", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Kind", PIN_STR, "const char *") PORT("Entity", PIN_STR, "const char *")
            PORT("Component", PIN_STR, "const char *") PORT("Add", PIN_BOOL, "bool")
};
static const native_parameter_definition OBJECT_IN[] = {
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Kind", PIN_STR, "const char *") PORT("Entity", PIN_STR, "const char *")
};
static const native_parameter_definition OBJECTS_IN[] = {
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Kind", PIN_STR, "const char *"){ .name = "Entities",
                                               .kind = NATIVE_PARAMETER_DATA,
                                               .storage = PIN_STR,
                                               .type_name = "const char *",
                                               .container = PIN_CONTAINER_ARRAY },
};
static const native_parameter_definition OBJECT_EDIT_IN[] = {
    { .name = "Exec", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Kind", PIN_STR, "const char *"){ .name = "Entities",
                                               .kind = NATIVE_PARAMETER_DATA,
                                               .storage = PIN_STR,
                                               .type_name = "const char *",
                                               .container = PIN_CONTAINER_ARRAY },
    PORT("Relative", PIN_STR, "const char *") PORT("Operation", PIN_STR, "const char *")
        PORT("Value", PIN_STR, "const char *")
};
static const native_parameter_definition OBJECT_COMPONENT_IN[] = {
    { .name = "Exec", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Kind", PIN_STR, "const char *"){ .name = "Entities",
                                               .kind = NATIVE_PARAMETER_DATA,
                                               .storage = PIN_STR,
                                               .type_name = "const char *",
                                               .container = PIN_CONTAINER_ARRAY },
    PORT("Component", PIN_STR, "const char *") PORT("Add", PIN_BOOL, "bool")
};
static const native_parameter_definition CLIPBOARD_IN[] = {
    PORT("Document", PIN_I32, "int")
        PORT("Revision", PIN_I32, "int"){ .name = "Entities",
                                          .kind = NATIVE_PARAMETER_DATA,
                                          .storage = PIN_STR,
                                          .type_name = "const char *",
                                          .container = PIN_CONTAINER_ARRAY },
    PORT("Cut", PIN_BOOL, "bool")
};
static const native_parameter_definition CLIPBOARD_OUT[] = {
    PORT("Ok", PIN_BOOL, "bool") PORT("Error", PIN_STR, "const char *")
        PORT("Payload", PIN_STR, "const char *") PORT("Text", PIN_STR, "const char *")
};
static const native_parameter_definition PASTE_IN[] = {
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Parent", PIN_STR, "const char *") PORT("Payload", PIN_STR, "const char *")
};
static const native_parameter_definition PASTE_OUT[] = {
    PORT("Ok", PIN_BOOL, "bool") PORT("Error", PIN_STR, "const char *")
        PORT("Text", PIN_STR, "const char *"){ .name = "Paths",
                                               .kind = NATIVE_PARAMETER_DATA,
                                               .storage = PIN_STR,
                                               .type_name = "const char *",
                                               .container = PIN_CONTAINER_ARRAY }
};
static const native_parameter_definition PLACE_IN[] = {
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Parent", PIN_STR, "const char *") PORT("Prototype", PIN_STR, "const char *")
            PORT("Name", PIN_STR, "const char *") PORT("X", PIN_F32, "float")
                PORT("Y", PIN_F32, "float") PORT("Z", PIN_F32, "float")
                    PORT("Source", PIN_STR, "const char *")
};
static const native_parameter_definition PLACE_OUT[] = {
    PORT("Ok", PIN_BOOL, "bool") PORT("Text", PIN_STR, "const char *")
        PORT("Start", PIN_I32, "int") PORT("End", PIN_I32, "int")
            PORT("Replacement", PIN_STR, "const char *"){
                .name = "Paths",
                .kind = NATIVE_PARAMETER_DATA,
                .storage = PIN_STR,
                .type_name = "const char *",
                .container = PIN_CONTAINER_ARRAY },
    PORT("Error", PIN_STR, "const char *")
};
static const native_parameter_definition CANDIDATE_IN[] = {
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Kind", PIN_STR, "const char *"){ .name = "Entities",
                                               .kind = NATIVE_PARAMETER_DATA,
                                               .storage = PIN_STR,
                                               .type_name = "const char *",
                                               .container = PIN_CONTAINER_ARRAY },
    PORT("Relative", PIN_STR, "const char *") PORT("Operation", PIN_STR, "const char *")
        PORT("Value", PIN_STR, "const char *") PORT("Source", PIN_STR, "const char *")
};
static const native_parameter_definition CANDIDATE_OUT[] = {
    PORT("Ok", PIN_BOOL, "bool") PORT("Text", PIN_STR, "const char *")
        PORT("Error", PIN_STR, "const char *") PORT("Start", PIN_I32, "int")
            PORT("End", PIN_I32, "int") PORT("Replacement", PIN_STR, "const char *"){
                .name = "Fields",
                .kind = NATIVE_PARAMETER_DATA,
                .storage = PIN_STRUCT,
                .type_name = "WorkspaceAssetField",
                .container = PIN_CONTAINER_ARRAY },
    { .name = "Choices",
      .kind = NATIVE_PARAMETER_DATA,
      .storage = PIN_STRUCT,
      .type_name = "WorkspaceAssetChoice",
      .container = PIN_CONTAINER_ARRAY }
};
static const native_parameter_definition PROTOTYPE_SOURCE_IN[] = {
    PORT("Name", PIN_STR, "const char *")
};
static const native_parameter_definition PROTOTYPE_SOURCE_OUT[] = {
    PORT("Ok", PIN_BOOL, "bool") PORT("Path", PIN_STR, "const char *")
        PORT("Text", PIN_STR, "const char *") PORT("Error", PIN_STR, "const char *")
};
static const native_parameter_definition PROTOTYPE_CANDIDATE_IN[] = {
    PORT("Document", PIN_I32, "int") PORT("Revision", PIN_I32, "int")
        PORT("Kind", PIN_STR, "const char *") PORT("Entity", PIN_STR, "const char *")
            PORT("PrototypeDocument", PIN_I32, "int")
                PORT("PrototypeRevision", PIN_I32, "int"){
                    .name = "Paths",
                    .kind = NATIVE_PARAMETER_DATA,
                    .storage = PIN_STR,
                    .type_name = "const char *",
                    .container = PIN_CONTAINER_ARRAY },
    PORT("Operation", PIN_STR, "const char *") PORT("Name", PIN_STR, "const char *")
};
static const native_parameter_definition PROTOTYPE_CANDIDATE_OUT[] = {
    PORT("Ok", PIN_BOOL, "bool") PORT("Error", PIN_STR, "const char *")
        PORT("ObjectText", PIN_STR, "const char *")
            PORT("PrototypeText", PIN_STR, "const char *")
                PORT("Path", PIN_STR, "const char *")
};
static const native_parameter_definition ASSET_PREPARE_OUT[] = {
    { .name = "Then", .kind = NATIVE_PARAMETER_EXEC, .storage = PIN_FLOW },
    PORT("Ok", PIN_BOOL, "bool") PORT("Text", PIN_STR, "const char *")
        PORT("Error", PIN_STR, "const char *")
};
static const native_capability_definition DEFINITIONS[] = {
    { .canonical_name = "Prepare Workspace Asset Edit",
      .source_name = "prepare_workspace_asset_edit",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .inputs = EDIT_IN,
      .input_count = NATIVE_COUNT(EDIT_IN),
      .outputs = ASSET_PREPARE_OUT,
      .output_count = NATIVE_COUNT(ASSET_PREPARE_OUT),
      .completion_output = 0 },

    { .canonical_name = "Workspace Scene Place Candidate",
      .source_name = "workspace_scene_place_candidate",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = PLACE_IN,
      .input_count = 9,
      .outputs = PLACE_OUT,
      .output_count = 7,
      .completion_output = -1 },
    { .canonical_name = "Workspace Scene Clipboard",
      .source_name = "workspace_scene_clipboard",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = CLIPBOARD_IN,
      .input_count = 4,
      .outputs = CLIPBOARD_OUT,
      .output_count = 4,
      .completion_output = -1 },
    { .canonical_name = "Workspace Scene Paste",
      .source_name = "workspace_scene_paste",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = PASTE_IN,
      .input_count = 4,
      .outputs = PASTE_OUT,
      .output_count = 4,
      .completion_output = -1 },
    { .canonical_name = "Workspace Prototype Source",
      .source_name = "workspace_prototype_source",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = PROTOTYPE_SOURCE_IN,
      .input_count = 1,
      .outputs = PROTOTYPE_SOURCE_OUT,
      .output_count = 4,
      .completion_output = -1 },
    { .canonical_name = "Workspace Prototype Candidate",
      .source_name = "workspace_prototype_candidate",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = PROTOTYPE_CANDIDATE_IN,
      .input_count = 9,
      .outputs = PROTOTYPE_CANDIDATE_OUT,
      .output_count = 5,
      .completion_output = -1 },
    { .canonical_name = "Workspace Objects Candidate",
      .source_name = "workspace_objects_candidate",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = CANDIDATE_IN,
      .input_count = 8,
      .outputs = CANDIDATE_OUT,
      .output_count = 8,
      .completion_output = -1 },
    { .canonical_name = "Workspace Objects Component",
      .source_name = "workspace_objects_component",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .pure = false,
      .inputs = OBJECT_COMPONENT_IN,
      .input_count = 7,
      .outputs = EDIT_OUT,
      .output_count = 3,
      .completion_output = 0 },
    { .canonical_name = "Workspace Objects Inspect",
      .source_name = "workspace_objects_inspect",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = OBJECTS_IN,
      .input_count = 4,
      .outputs = INSPECT_OUT,
      .output_count = 4,
      .completion_output = -1 },
    { .canonical_name = "Workspace Object Inspect",
      .source_name = "workspace_object_inspect",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = OBJECT_IN,
      .input_count = 4,
      .outputs = INSPECT_OUT,
      .output_count = 4,
      .completion_output = -1 },
    { .canonical_name = "Workspace Objects Edit",
      .source_name = "workspace_objects_edit",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .pure = false,
      .inputs = OBJECT_EDIT_IN,
      .input_count = 8,
      .outputs = EDIT_OUT,
      .output_count = 3,
      .completion_output = 0 },
    { .canonical_name = "Workspace Component Catalog",
      .source_name = "workspace_component_catalog",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .outputs = COMPONENTS_OUT,
      .output_count = 3,
      .completion_output = -1 },
    { .canonical_name = "Workspace Asset Component",
      .source_name = "workspace_asset_component",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .pure = false,
      .inputs = COMPONENT_IN,
      .input_count = 7,
      .outputs = SCENE_OUT,
      .output_count = 3,
      .completion_output = 0 },
    { .canonical_name = "Workspace Asset Identity",
      .source_name = "workspace_asset_identity",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = IDENTITY_IN,
      .input_count = 2,
      .outputs = IDENTITY_OUT,
      .output_count = 4,
      .completion_output = -1 },
    { .canonical_name = "Workspace Scene add",
      .source_name = "workspace_scene_add",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .pure = false,
      .inputs = SCENE0_IN,
      .input_count = 6,
      .outputs = SCENE_OUT,
      .output_count = 3,
      .completion_output = 0 },
    { .canonical_name = "Workspace Scene rename",
      .source_name = "workspace_scene_rename",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .pure = false,
      .inputs = SCENE1_IN,
      .input_count = 5,
      .outputs = SCENE_OUT,
      .output_count = 3,
      .completion_output = 0 },
    { .canonical_name = "Workspace Scene remove",
      .source_name = "workspace_scene_remove",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .pure = false,
      .inputs = SCENE2_IN,
      .input_count = 4,
      .outputs = SCENE_OUT,
      .output_count = 3,
      .completion_output = 0 },
    { .canonical_name = "Workspace Scene duplicate",
      .source_name = "workspace_scene_duplicate",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .pure = false,
      .inputs = SCENE3_IN,
      .input_count = 5,
      .outputs = SCENE_OUT,
      .output_count = 3,
      .completion_output = 0 },
    { .canonical_name = "Workspace Scene move",
      .source_name = "workspace_scene_move",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .pure = false,
      .inputs = SCENE4_IN,
      .input_count = 6,
      .outputs = SCENE_OUT,
      .output_count = 3,
      .completion_output = 0 },

    { .canonical_name = "Workspace Asset Inspect",
      .source_name = "workspace_asset_inspect",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = INSPECT_IN,
      .input_count = 3,
      .outputs = INSPECT_OUT,
      .output_count = 4,
      .completion_output = -1 },
    { .canonical_name = "Workspace Asset Edit",
      .source_name = "workspace_asset_edit",
      .kind = SCRIPT_CAPABILITY_ACTION,
      .pure = false,
      .inputs = EDIT_IN,
      .input_count = 7,
      .outputs = EDIT_OUT,
      .output_count = 3,
      .completion_output = 0 },
    { .canonical_name = "Workspace Asset Template",
      .source_name = "workspace_asset_template",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .inputs = TEMPLATE_IN,
      .input_count = 2,
      .outputs = TEMPLATE_OUT,
      .output_count = 4,
      .completion_output = -1 },
    { .canonical_name = "Workspace Asset Kinds",
      .source_name = "workspace_asset_kinds",
      .kind = SCRIPT_CAPABILITY_FUNCTION,
      .pure = true,
      .outputs = KINDS_OUT,
      .output_count = 1,
      .completion_output = -1 }
};
static const native_definition_binding BINDINGS[] = {
    { .canonical_name = "Workspace Scene Place Candidate",
      .implementation = { .call = &WORKSPACE_SCENE_PLACE_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } },
    { .canonical_name = "Workspace Scene Clipboard",
      .implementation = { .call = &WORKSPACE_SCENE_CLIPBOARD_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } },
    { .canonical_name = "Workspace Scene Paste",
      .implementation = { .call = &WORKSPACE_SCENE_PASTE_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } },
    { .canonical_name = "Prepare Workspace Asset Edit",
      .implementation = { .call = &WORKSPACE_ASSET_PREPARE_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } },

    { .canonical_name = "Workspace Prototype Source",
      .implementation = { .call = &WORKSPACE_PROTOTYPE_SOURCE_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } },
    { .canonical_name = "Workspace Prototype Candidate",
      .implementation = { .call = &WORKSPACE_PROTOTYPE_CANDIDATE_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } },
    { .canonical_name = "Workspace Objects Candidate",
      .implementation = { .call = &WORKSPACE_OBJECTS_CANDIDATE_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } },
    { .canonical_name = "Workspace Objects Component",
      .implementation = { .call = &WORKSPACE_OBJECTS_COMPONENT_CALL,
                          .access = &WORKSPACE_ASSETS_WRITE_ACCESS } },
    { .canonical_name = "Workspace Objects Inspect",
      .implementation = { .call = &WORKSPACE_OBJECTS_INSPECT_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } },
    { .canonical_name = "Workspace Object Inspect",
      .implementation = { .call = &WORKSPACE_OBJECT_INSPECT_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } },
    { .canonical_name = "Workspace Objects Edit",
      .implementation = { .call = &WORKSPACE_OBJECTS_EDIT_CALL,
                          .access = &WORKSPACE_ASSETS_WRITE_ACCESS } },
    { .canonical_name = "Workspace Component Catalog",
      .implementation = { .call = &WORKSPACE_COMPONENT_CATALOG_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } },
    { .canonical_name = "Workspace Asset Component",
      .implementation = { .call = &WORKSPACE_ASSET_COMPONENT_CALL,
                          .access = &WORKSPACE_ASSETS_WRITE_ACCESS } },
    { .canonical_name = "Workspace Asset Identity",
      .implementation = { .call = &WORKSPACE_ASSET_IDENTITY_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } },
    { .canonical_name = "Workspace Scene add",
      .implementation = { .call = &WORKSPACE_SCENE_CALLS[0],
                          .access = &WORKSPACE_ASSETS_WRITE_ACCESS } },
    { .canonical_name = "Workspace Scene rename",
      .implementation = { .call = &WORKSPACE_SCENE_CALLS[1],
                          .access = &WORKSPACE_ASSETS_WRITE_ACCESS } },
    { .canonical_name = "Workspace Scene remove",
      .implementation = { .call = &WORKSPACE_SCENE_CALLS[2],
                          .access = &WORKSPACE_ASSETS_WRITE_ACCESS } },
    { .canonical_name = "Workspace Scene duplicate",
      .implementation = { .call = &WORKSPACE_SCENE_CALLS[3],
                          .access = &WORKSPACE_ASSETS_WRITE_ACCESS } },
    { .canonical_name = "Workspace Scene move",
      .implementation = { .call = &WORKSPACE_SCENE_CALLS[4],
                          .access = &WORKSPACE_ASSETS_WRITE_ACCESS } },

    { .canonical_name = "Workspace Asset Inspect",
      .implementation = { .call = &WORKSPACE_ASSET_INSPECT_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } },
    { .canonical_name = "Workspace Asset Edit",
      .implementation = { .call = &WORKSPACE_ASSET_EDIT_CALL,
                          .access = &WORKSPACE_ASSETS_WRITE_ACCESS } },
    { .canonical_name = "Workspace Asset Template",
      .implementation = { .call = &WORKSPACE_ASSET_TEMPLATE_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } },
    { .canonical_name = "Workspace Asset Kinds",
      .implementation = { .call = &WORKSPACE_ASSET_KINDS_CALL,
                          .access = &WORKSPACE_ASSETS_READ_ACCESS } }
};
const native_definition_pack WORKSPACE_ASSETS_DEFINITION_PACK = {
    .definitions = DEFINITIONS,
    .definition_count = 23,
    .bindings = BINDINGS,
    .binding_count = 23
};
