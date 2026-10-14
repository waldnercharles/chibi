#include "authoring/workspace_catalog.h"

#include "authoring/css_values_definition.h"
#include "authoring/workspace_info_definition.h"
#ifdef CHIBI_HAS_WORKSPACE_IMPORTS
#include "authoring/workspace_imports_definition.h"
#endif

#include "authoring/document_graph_definition.h"
#include "authoring/document_graph_selection_definition.h"
#include "authoring/document_layout_definition.h"
#include "authoring/document_members_definition.h"
#include "authoring/document_observation_definition.h"
#include "authoring/document_port_schema_definition.h"
#include "authoring/document_semantics_definition.h"
#include "authoring/document_source_inventory_definition.h"
#include "authoring/observations_definition.h"
#include "authoring/project_semantics_definition.h"
#include "authoring/resource_previews_definition.h"
#include "authoring/runtime_properties.h"
#include "authoring/ui_authoring_definition.h"
#include "authoring/ui_dom_definition.h"
#include "authoring/ui_source_preview_definition.h"
#include "authoring/visual_authoring_definition.h"
#include "authoring/visual_authoring_preview_definition.h"
#include "authoring/visual_model_definition.h"
#include "authoring/workspace_assets_definition.h"
#include "authoring/workspace_debug_definition.h"
#include "authoring/workspace_documents_definition.h"
#include "authoring/workspace_file_history_definition.h"
#include "authoring/workspace_resources_definition.h"
#include "authoring/workspace_runtime_definition.h"
#include "authoring/workspace_simulation_definition.h"
#include "authoring/workspace_tools_definition.h"
#include "host/authoring_platform_definition.h"
#include "host/browser_messages_definition.h"
#include "host/native_default.h"
#include "render/shader_parameters.h"
#include "support/array.h"

#include <assert.h>
#include <stdio.h>

static const type_desc *const VISUAL_TYPES[] = { &VISUAL_PARAMETER_LAYOUT,
                                                 &VISUAL_COSTS_LAYOUT,
                                                 &VISUAL_SAMPLE_LAYOUT,
                                                 &VISUAL_MATERIAL_SETTINGS_LAYOUT };
static const type_desc *const DEBUG_TYPES[] = { &WORKSPACE_DEBUG_ROW_LAYOUT,
                                                &WORKSPACE_DEBUG_ACTIVITY_LAYOUT };
static const type_desc *const MODEL_TYPES[] = { &VISUAL_MODEL_PARAMETER_LAYOUT,
                                                &VISUAL_MODEL_INFO_LAYOUT,
                                                &VISUAL_MODEL_PART_LAYOUT,
                                                &VISUAL_MODEL_ANCHOR_LAYOUT,
                                                &VISUAL_MODEL_REFERENCE_LAYOUT };
static const type_desc *const PREVIEW_TYPES[] = { &VISUAL_PUBLICATION_LAYOUT };
int authoring_workspace_type_pack_count()
{
#ifdef CHIBI_HAS_WORKSPACE_IMPORTS
    return 21;
#else
    return 20;
#endif
}
type_catalog_pack authoring_workspace_type_pack_at(int index)
{
    switch (index) {
#ifdef CHIBI_HAS_WORKSPACE_IMPORTS
        case 20: return WORKSPACE_IMPORTS_TYPE_PACK;
#endif
        case 19: return WORKSPACE_TOOLS_TYPE_PACK;
        case 18: return DOCUMENT_MEMBERS_TYPE_PACK;
        case 17: return RUNTIME_PROPERTY_TYPE_PACK;
        case 15: return DOCUMENT_SOURCE_INVENTORY_TYPE_PACK;
        case 16: return DOCUMENT_PORT_SCHEMA_TYPE_PACK;
        case 14: return DOCUMENT_GRAPH_SELECTION_TYPE_PACK;
        case 12: return PROJECT_SEMANTICS_TYPE_PACK;
        case 13: return UI_SOURCE_PREVIEW_TYPE_PACK;
        case 11:
            return (type_catalog_pack){ .types = DEBUG_TYPES,
                                        .type_count = NATIVE_COUNT(DEBUG_TYPES) };
        case 7: return DOCUMENT_GRAPH_TYPE_PACK;
        case 8:
            return (type_catalog_pack){ .types = MODEL_TYPES,
                                        .type_count = NATIVE_COUNT(MODEL_TYPES) };
        case 9: return UI_DOM_TYPE_PACK;
        case 10: return AUTHORING_OBSERVATIONS_TYPE_PACK;
        case 0: return WORKSPACE_RESOURCE_TYPE_PACK;
        case 1: return DOCUMENT_SEMANTICS_TYPE_PACK;
        case 2:
            return (type_catalog_pack){ .types = VISUAL_TYPES,
                                        .type_count = NATIVE_COUNT(VISUAL_TYPES) };
        case 3: return WORKSPACE_ASSETS_TYPE_PACK;
        case 4: return AUTHORING_PLATFORM_TYPE_PACK;
        case 6: return UI_AUTHORING_TYPE_PACK;
        case 5:
            return (type_catalog_pack){ .types = PREVIEW_TYPES, .type_count = 1 };
    }
    assert(false);
    return (type_catalog_pack){ 0 };
}
static bool has_definitions(const native_definition_pack *extra, int count, native_definition_pack pack)
{
    for (int i = 0; i < count; i++) {
        if (extra[i].definitions == pack.definitions) {
            return true;
        }
    }
    return false;
}
native_capabilities *authoring_workspace_catalog_new(
    const native_definition_pack *extra,
    int count,
    char *error,
    size_t size
)
{
    if (count < 0 || count > 16 || (count && !extra)) {
        if (size) {
            snprintf(error, size, "Invalid additional workspace capability packs");
        }
        return NULL;
    }
    native_type_definition *types = NULL;
    for (int i = 0; i < NATIVE_COUNT(WORKSPACE_RESOURCE_TYPE_DEFINITIONS); i++) {
        apush(types, WORKSPACE_RESOURCE_TYPE_DEFINITIONS[i]);
    }
    for (int i = 0; i < DOCUMENT_SEMANTICS_TYPE_COUNT; i++) {
        apush(types, DOCUMENT_SEMANTICS_TYPE_DEFINITIONS[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(VISUAL_AUTHORING_TYPES); i++) {
        apush(types, VISUAL_AUTHORING_TYPES[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(WORKSPACE_ASSETS_TYPE_DEFINITIONS); i++) {
        apush(types, WORKSPACE_ASSETS_TYPE_DEFINITIONS[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(AUTHORING_PLATFORM_TYPE_DEFINITIONS); i++) {
        apush(types, AUTHORING_PLATFORM_TYPE_DEFINITIONS[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(VISUAL_PREVIEW_TYPES); i++) {
        apush(types, VISUAL_PREVIEW_TYPES[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(UI_AUTHORING_TYPE_DEFINITIONS); i++) {
        apush(types, UI_AUTHORING_TYPE_DEFINITIONS[i]);
    }
    for (int i = 0; i < DOCUMENT_GRAPH_TYPE_COUNT; i++) {
        apush(types, DOCUMENT_GRAPH_TYPE_DEFINITIONS[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(DOCUMENT_GRAPH_SELECTION_TYPES); i++) {
        apush(types, DOCUMENT_GRAPH_SELECTION_TYPES[i]);
    }
    for (int i = 0; i < DOCUMENT_SOURCE_INVENTORY_TYPE_COUNT; i++) {
        apush(types, DOCUMENT_SOURCE_INVENTORY_TYPE_DEFINITIONS[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(DOCUMENT_PORT_SCHEMA_TYPE_DEFINITIONS); i++) {
        apush(types, DOCUMENT_PORT_SCHEMA_TYPE_DEFINITIONS[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(DOCUMENT_MEMBERS_TYPES); i++) {
        apush(types, DOCUMENT_MEMBERS_TYPES[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(RUNTIME_PROPERTY_TYPE_DEFINITIONS); i++) {
        apush(types, RUNTIME_PROPERTY_TYPE_DEFINITIONS[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(VISUAL_MODEL_TYPES); i++) {
        apush(types, VISUAL_MODEL_TYPES[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(WORKSPACE_TOOLS_TYPES); i++) {
        apush(types, WORKSPACE_TOOLS_TYPES[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(UI_DOM_TYPE_DEFINITIONS); i++) {
        apush(types, UI_DOM_TYPE_DEFINITIONS[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(UI_SOURCE_PREVIEW_TYPE_DEFINITIONS); i++) {
        apush(types, UI_SOURCE_PREVIEW_TYPE_DEFINITIONS[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(AUTHORING_OBSERVATIONS_TYPES); i++) {
        apush(types, AUTHORING_OBSERVATIONS_TYPES[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(WORKSPACE_DEBUG_TYPES); i++) {
        apush(types, WORKSPACE_DEBUG_TYPES[i]);
    }
    for (int i = 0; i < NATIVE_COUNT(PROJECT_SEMANTICS_TYPE_DEFINITIONS); i++) {
        apush(types, PROJECT_SEMANTICS_TYPE_DEFINITIONS[i]);
    }
#ifdef CHIBI_HAS_WORKSPACE_IMPORTS
    for (int i = 0; i < NATIVE_COUNT(WORKSPACE_IMPORTS_TYPES); i++) {
        apush(types, WORKSPACE_IMPORTS_TYPES[i]);
    }
#endif
    native_type_catalog *catalog = native_type_catalog_new(types, acount(types), error, size);
    afree(types);
    if (!catalog) {
        return NULL;
    }
    native_capability_pack pack = native_type_catalog_pack(catalog);
    native_definition_pack *definitions = NULL;
    apush(definitions, RENDER_SHADER_PARAMETER_DEFINITION_PACK);
    apush(definitions, WORKSPACE_DOCUMENTS_DEFINITION_PACK);
    apush(definitions, WORKSPACE_FILE_HISTORY_DEFINITION_PACK);
    apush(definitions, WORKSPACE_RESOURCE_DEFINITION_PACK);
    apush(definitions, DOCUMENT_SEMANTICS_DEFINITION_PACK);
    apush(definitions, DOCUMENT_LAYOUT_DEFINITION_PACK);
    apush(definitions, DOCUMENT_MEMBERS_DEFINITION_PACK);
    apush(definitions, DOCUMENT_OBSERVATION_DEFINITION_PACK);
    apush(definitions, DOCUMENT_SOURCE_INVENTORY_DEFINITION_PACK);
    apush(definitions, DOCUMENT_PORT_SCHEMA_DEFINITION_PACK);
    apush(definitions, VISUAL_AUTHORING_DEFINITION_PACK);
    apush(definitions, WORKSPACE_ASSETS_DEFINITION_PACK);
    apush(definitions, UI_AUTHORING_DEFINITION_PACK);
    apush(definitions, CSS_VALUES_DEFINITION_PACK);
    apush(definitions, DOCUMENT_GRAPH_DEFINITION_PACK);
    apush(definitions, DOCUMENT_GRAPH_SELECTION_DEFINITION_PACK);
    apush(definitions, VISUAL_MODEL_DEFINITION_PACK);
    if (!has_definitions(extra, count, WORKSPACE_TOOLS_DECLARATION_PACK)) {
        apush(definitions, WORKSPACE_TOOLS_DECLARATION_PACK);
    }
    if (!has_definitions(extra, count, UI_DOM_DECLARATION_PACK)) {
        apush(definitions, UI_DOM_DECLARATION_PACK);
    }
    if (!has_definitions(extra, count, UI_SOURCE_PREVIEW_DECLARATION_PACK)) {
        apush(definitions, UI_SOURCE_PREVIEW_DECLARATION_PACK);
    }
    apush(definitions, AUTHORING_OBSERVATIONS_DEFINITION_PACK);
    if (!has_definitions(extra, count, WORKSPACE_RUNTIME_DECLARATION_PACK)) {
        apush(definitions, WORKSPACE_RUNTIME_DECLARATION_PACK);
    }
    if (!has_definitions(extra, count, WORKSPACE_SIMULATION_DECLARATION_PACK)) {
        apush(definitions, WORKSPACE_SIMULATION_DECLARATION_PACK);
    }
    if (!has_definitions(extra, count, VISUAL_PREVIEW_DECLARATION_PACK)) {
        apush(definitions, VISUAL_PREVIEW_DECLARATION_PACK);
    }
    if (!has_definitions(extra, count, AUTHORING_PLATFORM_DECLARATION_PACK)) {
        apush(definitions, AUTHORING_PLATFORM_DECLARATION_PACK);
    }
    apush(definitions, BROWSER_MESSAGES_DEFINITION_PACK);
    apush(definitions, WORKSPACE_DEBUG_DEFINITION_PACK);
    apush(definitions, PROJECT_SEMANTICS_DEFINITION_PACK);
    apush(definitions, WORKSPACE_INFO_DEFINITION_PACK);
#ifdef CHIBI_HAS_WORKSPACE_IMPORTS
    apush(definitions, WORKSPACE_IMPORTS_DEFINITION_PACK);
#endif
    if (!has_definitions(extra, count, RESOURCE_PREVIEWS_DECLARATION_PACK)) {
        apush(definitions, RESOURCE_PREVIEWS_DECLARATION_PACK);
    }
    for (int i = 0; i < count; i++) {
        apush(definitions, extra[i]);
    }
    native_capabilities *result = host_native_default_catalog_new(
        &pack,
        1,
        definitions,
        acount(definitions),
        error,
        size
    );
    afree(definitions);
    native_type_catalog_free(catalog);
    return result;
}
