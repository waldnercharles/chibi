# Optional generic source-authoring services. Studio is an authored consumer.
add_library(chibi_workspace_documents STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_documents.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_documents_definition.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_documents_native.c")
add_library(Chibi::workspace_documents ALIAS chibi_workspace_documents)
set_target_properties(chibi_workspace_documents PROPERTIES EXPORT_NAME workspace_documents)
chibi_sdk_headers(chibi_workspace_documents PUBLIC authoring/workspace_documents.h
    authoring/workspace_documents_definition.h)
target_link_libraries(chibi_workspace_documents PUBLIC chibi_native_capabilities
    PRIVATE chibi_execution chibi_types chibi_support)
target_compile_definitions(chibi_workspace_documents PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)

add_library(chibi_workspace_presentation STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_presentation.c")
add_library(Chibi::workspace_presentation ALIAS chibi_workspace_presentation)
set_target_properties(chibi_workspace_presentation PROPERTIES EXPORT_NAME workspace_presentation)
chibi_sdk_headers(chibi_workspace_presentation PUBLIC authoring/workspace_presentation.h)
target_link_libraries(chibi_workspace_presentation PUBLIC chibi_component_presentation chibi_workspace_documents)
list(APPEND CHIBI_SDK_TARGETS chibi_workspace_presentation)

add_library(chibi_workspace_resources STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_resources.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_resources_definition.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_resources_native.c")
add_library(Chibi::workspace_resources ALIAS chibi_workspace_resources)
set_target_properties(chibi_workspace_resources PROPERTIES EXPORT_NAME workspace_resources)
chibi_sdk_headers(chibi_workspace_resources PUBLIC authoring/workspace_resources.h
    authoring/workspace_resources_definition.h)
target_link_libraries(chibi_workspace_resources PUBLIC chibi_native_capabilities chibi_reference_index chibi_types chibi_asset_values
    PRIVATE chibi_execution chibi_resource_catalog chibi_asset_json chibi_project_document chibi_asset_text chibi_support)
target_compile_definitions(chibi_workspace_resources PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)

add_library(chibi_document_semantics STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/document_semantics.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_layout.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_layout_definition.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_layout_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_observation.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_observation_definition.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_observation_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_source_inventory.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_source_inventory_definition.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_source_inventory_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_semantics_definition.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_semantics_native.c")
add_library(Chibi::document_semantics ALIAS chibi_document_semantics)
set_target_properties(chibi_document_semantics PROPERTIES EXPORT_NAME document_semantics)
chibi_sdk_headers(chibi_document_semantics PUBLIC authoring/document_semantics.h authoring/document_layout.h authoring/document_layout_definition.h
    authoring/document_observation.h authoring/document_observation_definition.h
    authoring/document_source_inventory.h authoring/document_source_inventory_definition.h
    authoring/document_semantics_definition.h)
target_link_libraries(chibi_document_semantics PUBLIC chibi_workspace_documents chibi_source_presentation chibi_ui_authoring
    PRIVATE chibi_text_search chibi_language chibi_graph_authoring chibi_graph_compilation chibi_value_document chibi_value_json chibi_asset_text chibi_asset_authoring chibi_native_capabilities chibi_execution chibi_types chibi_support chibi_text_capabilities)

add_library(chibi_workspace_assets STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_assets.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_assets_document.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_assets_scene.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_assets_scope.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_assets_definition.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_assets_native.c")
add_library(Chibi::workspace_assets ALIAS chibi_workspace_assets)
set_target_properties(chibi_workspace_assets PROPERTIES EXPORT_NAME workspace_assets)
chibi_sdk_headers(chibi_workspace_assets PUBLIC authoring/workspace_assets.h authoring/workspace_assets_definition.h authoring/workspace_assets_scope.h)
target_link_libraries(chibi_workspace_assets PUBLIC chibi_workspace_documents chibi_workspace_resources chibi_declared_components chibi_asset_authoring chibi_native_capabilities
    PRIVATE chibi_component_presentation chibi_workspace_presentation chibi_scene_document chibi_asset_json chibi_graph_authoring chibi_asset_text chibi_native_types chibi_language chibi_execution chibi_types chibi_support)

add_library(chibi_visual_authoring STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/visual_authoring.c"
    "${CHIBI_SDK_ROOT}/src/authoring/visual_authoring_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/visual_authoring_preview_definition.c")
add_library(Chibi::visual_authoring ALIAS chibi_visual_authoring)
set_target_properties(chibi_visual_authoring PROPERTIES EXPORT_NAME visual_authoring)
chibi_sdk_headers(chibi_visual_authoring PUBLIC authoring/visual_authoring.h
    authoring/visual_authoring_definition.h authoring/visual_authoring_preview_definition.h)
target_link_libraries(chibi_visual_authoring PUBLIC chibi_workspace_documents chibi_material_authoring chibi_native_capabilities
    PRIVATE chibi_graph_authoring chibi_asset_text chibi_execution chibi_types chibi_support)

add_library(chibi_ui_source_sessions STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/ui_authoring.c"
    "${CHIBI_SDK_ROOT}/src/authoring/ui_authoring_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/ui_authoring_definition.c")
add_library(Chibi::ui_source_sessions ALIAS chibi_ui_source_sessions)
set_target_properties(chibi_ui_source_sessions PROPERTIES EXPORT_NAME ui_source_sessions)
chibi_sdk_headers(chibi_ui_source_sessions PUBLIC authoring/ui_authoring.h authoring/ui_authoring_definition.h)
target_link_libraries(chibi_ui_source_sessions PUBLIC chibi_workspace_documents chibi_native_capabilities chibi_types
    PRIVATE chibi_ui_authoring chibi_execution chibi_support)

add_library(chibi_workspace_catalog STATIC "${CHIBI_SDK_ROOT}/src/authoring/workspace_catalog.c")
add_library(Chibi::workspace_catalog ALIAS chibi_workspace_catalog)
set_target_properties(chibi_workspace_catalog PROPERTIES EXPORT_NAME workspace_catalog)
chibi_sdk_headers(chibi_workspace_catalog PUBLIC authoring/workspace_catalog.h)
target_link_libraries(chibi_workspace_catalog PUBLIC chibi_native_capabilities chibi_types
    PRIVATE chibi_workspace_documents chibi_workspace_resources chibi_document_semantics chibi_visual_authoring
    chibi_host_native_default chibi_visual_catalog chibi_support chibi_workspace_assets chibi_authoring_platform_declarations chibi_ui_source_sessions)
list(APPEND CHIBI_SDK_TARGETS chibi_workspace_documents chibi_workspace_resources chibi_document_semantics
    chibi_visual_authoring chibi_workspace_catalog chibi_workspace_assets chibi_ui_source_sessions)
target_link_libraries(chibi_project_compilation PRIVATE chibi_workspace_catalog chibi_visual_authoring chibi_authoring_platform_declarations)

add_library(chibi_document_graph STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/document_members.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_members_edit.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_members_definition.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_members_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_graph.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_graph_scope.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_graph_selection.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_graph_selection_definition.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_graph_selection_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_port_schema.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_port_schema_definition.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_port_schema_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_graph_definition.c"
    "${CHIBI_SDK_ROOT}/src/authoring/document_graph_native.c"
)
add_library(Chibi::document_graph ALIAS chibi_document_graph)
set_target_properties(chibi_document_graph PROPERTIES EXPORT_NAME document_graph)
chibi_sdk_headers(chibi_document_graph PUBLIC authoring/document_graph.h authoring/document_graph_scope.h authoring/document_graph_definition.h
    authoring/document_graph_selection.h authoring/document_graph_selection_definition.h
    authoring/document_port_schema.h authoring/document_port_schema_definition.h
    authoring/document_members.h authoring/document_members_definition.h)
target_link_libraries(chibi_document_graph PUBLIC chibi_document_semantics PRIVATE chibi_graph_authoring chibi_graph_compilation chibi_graph_model chibi_asset_text chibi_value_document chibi_support chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_document_graph)

add_library(chibi_visual_model STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/model_evidence.c"
    "${CHIBI_SDK_ROOT}/src/authoring/visual_model.c"
    "${CHIBI_SDK_ROOT}/src/authoring/visual_model_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_file.c"
)
add_library(Chibi::visual_model ALIAS chibi_visual_model)
set_target_properties(chibi_visual_model PROPERTIES EXPORT_NAME visual_model)
chibi_sdk_headers(chibi_visual_model PUBLIC authoring/visual_model.h authoring/visual_model_definition.h authoring/workspace_file.h authoring/model_evidence.h)
target_link_libraries(chibi_visual_model PUBLIC chibi_workspace_documents chibi_graph_compilation chibi_model_bake chibi_model_runtime PRIVATE chibi_model_export_artifacts chibi_graph_authoring chibi_support chibi_execution chibi_image_preparation chibi_material_authoring chibi_project_document chibi_asset_json)
list(APPEND CHIBI_SDK_TARGETS chibi_visual_model)

add_library(chibi_ui_dom_declarations STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/ui_dom_types.c"
    "${CHIBI_SDK_ROOT}/src/authoring/ui_dom_definition.c"
)
add_library(Chibi::ui_dom_declarations ALIAS chibi_ui_dom_declarations)
set_target_properties(chibi_ui_dom_declarations PROPERTIES EXPORT_NAME ui_dom_declarations)
chibi_sdk_headers(chibi_ui_dom_declarations PUBLIC authoring/ui_dom.h authoring/ui_dom_definition.h)
target_link_libraries(chibi_ui_dom_declarations PUBLIC chibi_native_capabilities chibi_types PRIVATE chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_ui_dom_declarations)

add_library(chibi_observations STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/observations.c"
    "${CHIBI_SDK_ROOT}/src/authoring/observations_definition.c"
)
add_library(Chibi::observations ALIAS chibi_observations)
set_target_properties(chibi_observations PROPERTIES EXPORT_NAME observations)
chibi_sdk_headers(chibi_observations PUBLIC authoring/observations.h authoring/observations_definition.h)
target_link_libraries(chibi_observations PUBLIC chibi_native_capabilities chibi_types PRIVATE chibi_execution chibi_support)
list(APPEND CHIBI_SDK_TARGETS chibi_observations)

add_library(chibi_runtime_properties STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/runtime_properties.c")
add_library(Chibi::runtime_properties ALIAS chibi_runtime_properties)
set_target_properties(chibi_runtime_properties PROPERTIES EXPORT_NAME runtime_properties)
chibi_sdk_headers(chibi_runtime_properties PUBLIC authoring/runtime_properties.h)
target_link_libraries(chibi_runtime_properties PUBLIC chibi_component_presentation chibi_ecs chibi_types chibi_support
    PRIVATE chibi_native_capabilities chibi_value_document chibi_value_json chibi_asset_json chibi_utf8)
list(APPEND CHIBI_SDK_TARGETS chibi_runtime_properties)

add_library(chibi_workspace_runtime_declarations STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_runtime_definition.c"
)
add_library(Chibi::workspace_runtime_declarations ALIAS chibi_workspace_runtime_declarations)
set_target_properties(chibi_workspace_runtime_declarations PROPERTIES EXPORT_NAME workspace_runtime_declarations)
chibi_sdk_headers(chibi_workspace_runtime_declarations PUBLIC authoring/workspace_runtime_definition.h)
target_link_libraries(chibi_workspace_runtime_declarations PUBLIC chibi_native_capabilities chibi_runtime_properties PRIVATE chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_workspace_runtime_declarations)

target_link_libraries(chibi_workspace_catalog PRIVATE chibi_document_graph chibi_visual_model chibi_ui_dom_declarations chibi_observations chibi_workspace_runtime_declarations)

add_library(chibi_browser_messages STATIC "${CHIBI_SDK_ROOT}/src/host/workspace_overlays.c" "${CHIBI_SDK_ROOT}/src/host/workspace_files_web.c" "${CHIBI_SDK_ROOT}/src/host/browser_messages.c" "${CHIBI_SDK_ROOT}/src/host/browser_messages_definition.c")
add_library(Chibi::browser_messages ALIAS chibi_browser_messages)
set_target_properties(chibi_browser_messages PROPERTIES EXPORT_NAME browser_messages)
chibi_sdk_headers(chibi_browser_messages PUBLIC host/workspace_overlays.h host/workspace_files_web.h host/browser_messages.h host/browser_messages_definition.h)
target_link_libraries(chibi_browser_messages PUBLIC chibi_native_capabilities chibi_json_query PRIVATE chibi_workspace_documents chibi_json chibi_support chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_browser_messages)
target_link_libraries(chibi_workspace_catalog PRIVATE chibi_browser_messages)

add_library(chibi_workspace_debug STATIC "${CHIBI_SDK_ROOT}/src/authoring/workspace_debug.c" "${CHIBI_SDK_ROOT}/src/authoring/workspace_debug_definition.c" "${CHIBI_SDK_ROOT}/src/authoring/workspace_debug_native.c")
add_library(Chibi::workspace_debug ALIAS chibi_workspace_debug)
set_target_properties(chibi_workspace_debug PROPERTIES EXPORT_NAME workspace_debug)
chibi_sdk_headers(chibi_workspace_debug PUBLIC authoring/workspace_debug.h authoring/workspace_debug_definition.h)
target_link_libraries(chibi_workspace_debug PUBLIC chibi_behavior_runtime chibi_declared_components PRIVATE chibi_entity_runtime chibi_execution chibi_value_json chibi_support chibi_workspace_assets chibi_workspace_resources chibi_workspace_documents)
list(APPEND CHIBI_SDK_TARGETS chibi_workspace_debug)
target_link_libraries(chibi_workspace_catalog PRIVATE chibi_workspace_debug)

add_library(chibi_project_semantics STATIC "${CHIBI_SDK_ROOT}/src/authoring/project_semantics.c" "${CHIBI_SDK_ROOT}/src/authoring/project_semantics_definition.c" "${CHIBI_SDK_ROOT}/src/authoring/project_semantics_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/ui_binding_navigation.c"
    "${CHIBI_SDK_ROOT}/src/authoring/project_ui_bindings.c")
add_library(Chibi::project_semantics ALIAS chibi_project_semantics)
set_target_properties(chibi_project_semantics PROPERTIES EXPORT_NAME project_semantics)
chibi_sdk_headers(chibi_project_semantics PUBLIC authoring/project_semantics.h authoring/project_semantics_definition.h authoring/ui_binding_navigation.h authoring/project_ui_bindings.h)
target_link_libraries(chibi_project_semantics PUBLIC chibi_workspace_documents chibi_source_catalog chibi_source_presentation chibi_ui_authoring chibi_prototype_document PRIVATE chibi_scene_document chibi_support chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_project_semantics)
target_link_libraries(chibi_workspace_catalog PRIVATE chibi_project_semantics)
add_library(chibi_resource_previews_declarations STATIC "${CHIBI_SDK_ROOT}/src/authoring/resource_previews_definition.c")
add_library(Chibi::resource_previews_declarations ALIAS chibi_resource_previews_declarations)
set_target_properties(chibi_resource_previews_declarations PROPERTIES EXPORT_NAME resource_previews_declarations)
chibi_sdk_headers(chibi_resource_previews_declarations PUBLIC authoring/resource_previews_definition.h)
target_link_libraries(chibi_resource_previews_declarations PUBLIC chibi_native_capabilities PRIVATE chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_resource_previews_declarations)
target_link_libraries(chibi_workspace_catalog PRIVATE chibi_resource_previews_declarations)

add_library(chibi_browser_compiler STATIC "${CHIBI_SDK_ROOT}/src/host/browser_compiler.c")
add_library(Chibi::browser_compiler ALIAS chibi_browser_compiler)
set_target_properties(chibi_browser_compiler PROPERTIES EXPORT_NAME browser_compiler)
chibi_sdk_headers(chibi_browser_compiler PUBLIC host/browser_compiler.h)
target_link_libraries(chibi_browser_compiler PUBLIC chibi_workspace_documents PRIVATE chibi_browser_messages chibi_rooted_files chibi_utf8 chibi_json chibi_support)
list(APPEND CHIBI_SDK_TARGETS chibi_browser_compiler)

add_library(chibi_workspace_info STATIC "${CHIBI_SDK_ROOT}/src/authoring/workspace_info.c" "${CHIBI_SDK_ROOT}/src/authoring/workspace_info_definition.c" "${CHIBI_SDK_ROOT}/src/authoring/workspace_info_native.c")
add_library(Chibi::workspace_info ALIAS chibi_workspace_info)
set_target_properties(chibi_workspace_info PROPERTIES EXPORT_NAME workspace_info)
chibi_sdk_headers(chibi_workspace_info PUBLIC authoring/workspace_info.h authoring/workspace_info_definition.h)
target_link_libraries(chibi_workspace_info PUBLIC chibi_native_capabilities PRIVATE chibi_utf8 chibi_rooted_files chibi_workspace_documents chibi_project_document chibi_asset_text chibi_support chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_workspace_info)
target_link_libraries(chibi_workspace_catalog PRIVATE chibi_workspace_info)
if(CHIBI_SDK_BUILD_DESKTOP)
    add_library(chibi_workspace_imports STATIC "${CHIBI_SDK_ROOT}/src/authoring/workspace_imports.c" "${CHIBI_SDK_ROOT}/src/authoring/workspace_imports_definition.c")
    add_library(Chibi::workspace_imports ALIAS chibi_workspace_imports)
    set_target_properties(chibi_workspace_imports PROPERTIES EXPORT_NAME workspace_imports)
    chibi_sdk_headers(chibi_workspace_imports PUBLIC authoring/workspace_imports.h authoring/workspace_imports_definition.h)
    target_link_libraries(chibi_workspace_imports PUBLIC chibi_model_import chibi_native_capabilities PRIVATE chibi_support chibi_execution)
    list(APPEND CHIBI_SDK_TARGETS chibi_workspace_imports)
    target_link_libraries(chibi_workspace_catalog PRIVATE chibi_workspace_imports)
    target_compile_definitions(chibi_workspace_catalog PRIVATE CHIBI_HAS_WORKSPACE_IMPORTS)
endif()

add_library(chibi_workspace_scaffold STATIC "${CHIBI_SDK_ROOT}/src/authoring/workspace_scaffold.c")
add_library(Chibi::workspace_scaffold ALIAS chibi_workspace_scaffold)
set_target_properties(chibi_workspace_scaffold PROPERTIES EXPORT_NAME workspace_scaffold)
chibi_sdk_headers(chibi_workspace_scaffold PUBLIC authoring/workspace_scaffold.h)
target_link_libraries(chibi_workspace_scaffold PUBLIC chibi_workspace_info PRIVATE chibi_rooted_files chibi_project_document chibi_json chibi_support)
list(APPEND CHIBI_SDK_TARGETS chibi_workspace_scaffold)

target_link_libraries(chibi_visual_model PRIVATE chibi_rooted_files chibi_utf8)

target_link_libraries(chibi_workspace_documents PRIVATE chibi_rooted_files)
target_link_libraries(chibi_workspace_resources PRIVATE chibi_rooted_files)

add_library(chibi_ui_source_provider STATIC "${CHIBI_SDK_ROOT}/src/authoring/ui_source_provider.c")
add_library(Chibi::ui_source_provider ALIAS chibi_ui_source_provider)
set_target_properties(chibi_ui_source_provider PROPERTIES EXPORT_NAME ui_source_provider)
chibi_sdk_headers(chibi_ui_source_provider PUBLIC authoring/ui_source_provider.h)
target_link_libraries(chibi_ui_source_provider PUBLIC chibi_workspace_documents chibi_ui_loader
    PRIVATE chibi_ui_authoring chibi_ui_document chibi_ui_style chibi_rooted_files chibi_utf8 chibi_support)
add_library(chibi_ui_source_preview_declarations STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/ui_source_preview_types.c"
    "${CHIBI_SDK_ROOT}/src/authoring/ui_source_preview_definition.c")
add_library(Chibi::ui_source_preview_declarations ALIAS chibi_ui_source_preview_declarations)
set_target_properties(chibi_ui_source_preview_declarations PROPERTIES EXPORT_NAME ui_source_preview_declarations)
chibi_sdk_headers(chibi_ui_source_preview_declarations PUBLIC authoring/ui_source_preview.h authoring/ui_source_preview_definition.h)
target_link_libraries(chibi_ui_source_preview_declarations PUBLIC chibi_workspace_documents chibi_native_capabilities chibi_types PRIVATE chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_ui_source_provider chibi_ui_source_preview_declarations)
target_link_libraries(chibi_workspace_catalog PRIVATE chibi_ui_source_preview_declarations)

# CPU RCSS language metadata and scalar conversion; no graphics or workspace service.
add_library(chibi_css_values STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/css_values.c"
    "${CHIBI_SDK_ROOT}/src/authoring/css_values_metadata.inc"
    "${CHIBI_SDK_ROOT}/src/authoring/css_values_definition.c"
    "${CHIBI_SDK_ROOT}/src/authoring/css_values_native.c")
add_library(Chibi::css_values ALIAS chibi_css_values)
set_target_properties(chibi_css_values PROPERTIES EXPORT_NAME css_values)
chibi_sdk_headers(chibi_css_values PUBLIC authoring/css_values.h authoring/css_values_definition.h)
target_link_libraries(chibi_css_values PUBLIC chibi_native_capabilities PRIVATE chibi_color_value chibi_types chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_css_values)
target_link_libraries(chibi_workspace_catalog PRIVATE chibi_css_values)

add_library(chibi_workspace_file_history STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_file_history.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_file_history_definition.c")
add_library(Chibi::workspace_file_history ALIAS chibi_workspace_file_history)
set_target_properties(chibi_workspace_file_history PROPERTIES EXPORT_NAME workspace_file_history)
chibi_sdk_headers(chibi_workspace_file_history PUBLIC authoring/workspace_file_history.h authoring/workspace_file_history_definition.h)
target_link_libraries(chibi_workspace_file_history PUBLIC chibi_workspace_documents chibi_native_capabilities
    PRIVATE chibi_asset_text chibi_rooted_files chibi_support chibi_execution chibi_types)
list(APPEND CHIBI_SDK_TARGETS chibi_workspace_file_history)
target_link_libraries(chibi_workspace_catalog PRIVATE chibi_workspace_file_history)

target_link_libraries(chibi_visual_authoring PRIVATE chibi_asset_json)

add_library(chibi_resource_preview_data STATIC "${CHIBI_SDK_ROOT}/src/authoring/resource_preview_data.c")
add_library(Chibi::resource_preview_data ALIAS chibi_resource_preview_data)
set_target_properties(chibi_resource_preview_data PROPERTIES EXPORT_NAME resource_preview_data)
chibi_sdk_headers(chibi_resource_preview_data PUBLIC authoring/resource_preview_data.h)
target_link_libraries(chibi_resource_preview_data PUBLIC chibi_graph_model PRIVATE chibi_ui_authoring chibi_css_values chibi_color_value chibi_graph_authoring chibi_support)
list(APPEND CHIBI_SDK_TARGETS chibi_resource_preview_data)

add_library(chibi_workspace_simulation_declarations STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_simulation_definition.c")
add_library(Chibi::workspace_simulation_declarations ALIAS chibi_workspace_simulation_declarations)
set_target_properties(chibi_workspace_simulation_declarations PROPERTIES EXPORT_NAME workspace_simulation_declarations)
chibi_sdk_headers(chibi_workspace_simulation_declarations PUBLIC authoring/workspace_simulation_definition.h)
target_link_libraries(chibi_workspace_simulation_declarations PUBLIC chibi_native_capabilities)
list(APPEND CHIBI_SDK_TARGETS chibi_workspace_simulation_declarations)
target_link_libraries(chibi_workspace_catalog PRIVATE chibi_workspace_simulation_declarations)

# Typed source metadata shared by CLI compilation and authored tool panels.
add_library(chibi_workspace_tools_declarations STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_tools_definition.c")
add_library(Chibi::workspace_tools_declarations ALIAS chibi_workspace_tools_declarations)
set_target_properties(chibi_workspace_tools_declarations PROPERTIES EXPORT_NAME workspace_tools_declarations)
chibi_sdk_headers(chibi_workspace_tools_declarations PUBLIC
    authoring/workspace_tools_definition.h authoring/workspace_saved.h authoring/workspace_css.h)
target_link_libraries(chibi_workspace_tools_declarations PUBLIC chibi_native_capabilities chibi_types chibi_save_runtime PRIVATE chibi_execution chibi_support)
list(APPEND CHIBI_SDK_TARGETS chibi_workspace_tools_declarations)
target_link_libraries(chibi_workspace_catalog PRIVATE chibi_workspace_tools_declarations)
