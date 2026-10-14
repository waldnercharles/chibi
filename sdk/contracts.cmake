# Package dependencies are deliberate. Adding a cross-package link requires
# changing this declaration, including when built inside the product checkout.
function(chibi_sdk_contract target directory)
    get_target_property(sources ${target} SOURCES)
    if(sources)
        foreach(source IN LISTS sources)
            get_filename_component(path "${source}" ABSOLUTE BASE_DIR "${directory}")
            string(FIND "${path}" "${directory}/" prefix)
            get_source_file_property(generated "${source}" GENERATED)
            if(NOT prefix EQUAL 0 AND NOT generated)
                message(FATAL_ERROR "${target} owns source outside ${directory}: ${path}")
            endif()
        endforeach()
    endif()
    foreach(property LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(dependencies ${target} ${property})
        if(NOT dependencies)
            continue()
        endif()
        foreach(dependency IN LISTS dependencies)
            if(dependency MATCHES "^\\$<LINK_ONLY:(.*)>$")
                set(dependency "${CMAKE_MATCH_1}")
            endif()
            if(TARGET "${dependency}")
                get_target_property(alias "${dependency}" ALIASED_TARGET)
                if(alias)
                    set(dependency "${alias}")
                endif()
            endif()
            if(NOT dependency IN_LIST ARGN)
                message(FATAL_ERROR "${target} has an undeclared package dependency: ${dependency}")
            endif()
        endforeach()
    endforeach()
endfunction()

chibi_sdk_contract(chibi_support "${CHIBI_SDK_ROOT}/src/support" m)
chibi_sdk_contract(chibi_types "${CHIBI_SDK_ROOT}/src/types" chibi_support m)
chibi_sdk_contract(chibi_execution_contract "${CHIBI_SDK_ROOT}/src/execution" chibi_types)
chibi_sdk_contract(chibi_tasks "${CHIBI_SDK_ROOT}/src/runtime")
chibi_sdk_contract(chibi_input_action "${CHIBI_SDK_ROOT}/src/runtime")
chibi_sdk_contract(chibi_asset_values "${CHIBI_SDK_ROOT}/src/support" chibi_support)
if(TARGET chibi_builtin_fonts)
    chibi_sdk_contract(chibi_builtin_fonts "${CHIBI_SDK_ROOT}/src/runtime/ui" chibi_ui_documents)
endif()
chibi_sdk_contract(chibi_ui_document_values "${CHIBI_SDK_ROOT}/src/ui" chibi_support)
chibi_sdk_contract(chibi_text_search "${CHIBI_SDK_ROOT}/src/support" chibi_support)
chibi_sdk_contract(chibi_image_catalog "${CHIBI_SDK_ROOT}/src/runtime/images" chibi_shader_runtime chibi_types chibi_support m)
if(TARGET chibi_image_preparation)
    chibi_sdk_contract(chibi_image_preparation "${CHIBI_SDK_ROOT}/src/authoring" chibi_image_catalog chibi_support m)
endif()
chibi_sdk_contract(chibi_ui_documents "${CHIBI_SDK_ROOT}/src/runtime/ui" chibi_types chibi_support)
chibi_sdk_contract(chibi_value_document "${CHIBI_SDK_ROOT}/src/types" chibi_types chibi_asset_values)
chibi_sdk_contract(chibi_capabilities "${CHIBI_SDK_ROOT}/src/capabilities" chibi_types)
chibi_sdk_contract(chibi_native_capabilities "${CHIBI_SDK_ROOT}/src/runtime"
    chibi_capabilities chibi_execution_contract)
chibi_sdk_contract(chibi_core_capabilities "${CHIBI_SDK_ROOT}/src/runtime"
    chibi_native_capabilities chibi_execution)
chibi_sdk_contract(chibi_array_capabilities "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities)
chibi_sdk_contract(chibi_field_capabilities "${CHIBI_SDK_ROOT}/src/runtime/fields" chibi_native_capabilities)
chibi_sdk_contract(chibi_state_capabilities "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities)
chibi_sdk_contract(chibi_selection_capabilities "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities)
chibi_sdk_contract(chibi_set_capabilities "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities)
chibi_sdk_contract(chibi_comparison_capabilities "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities)
chibi_sdk_contract(chibi_conversion_capabilities "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities)
chibi_sdk_contract(chibi_text_capabilities "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities chibi_fmt)
chibi_sdk_contract(chibi_native_prepared "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities chibi_behavior_runtime chibi_input_action)
chibi_sdk_contract(chibi_host_native_catalog "${CHIBI_SDK_ROOT}/src/host" chibi_native_capabilities chibi_events)
chibi_sdk_contract(chibi_ecs "${CHIBI_SDK_ROOT}/src/ecs" chibi_types)
chibi_sdk_contract(chibi_execution "${CHIBI_SDK_ROOT}/src/execution"
    chibi_types chibi_execution_contract chibi_ecs chibi_execution_sljit chibi_tasks)
chibi_sdk_contract(chibi_events "${CHIBI_SDK_ROOT}/src/runtime" chibi_types)
chibi_sdk_contract(chibi_behavior_runtime "${CHIBI_SDK_ROOT}/src/runtime"
    chibi_execution chibi_events)
chibi_sdk_contract(chibi_scene_runtime "${CHIBI_SDK_ROOT}/src/runtime/scenes"
    chibi_behavior_runtime chibi_ecs chibi_types chibi_tasks)
chibi_sdk_contract(chibi_scheduling "${CHIBI_SDK_ROOT}/src/runtime/systems"
    chibi_ecs chibi_execution_contract chibi_tasks)
chibi_sdk_contract(chibi_fields "${CHIBI_SDK_ROOT}/src/runtime/fields" chibi_types chibi_tasks)
chibi_sdk_contract(chibi_field_catalog "${CHIBI_SDK_ROOT}/src/runtime" chibi_fields)
chibi_sdk_contract(chibi_shader_runtime "${CHIBI_SDK_ROOT}/src/runtime/shaders" chibi_support chibi_types m)
chibi_sdk_contract(chibi_model_runtime "${CHIBI_SDK_ROOT}/src/runtime/models" chibi_shader_runtime chibi_types chibi_support m)
if(TARGET chibi_mesh_runtime)
    chibi_sdk_contract(chibi_mesh_runtime "${CHIBI_SDK_ROOT}/src/runtime/geometry" chibi_math chibi_support chibi_types)
    chibi_sdk_contract(chibi_model_bake "${CHIBI_SDK_ROOT}/src/runtime/models" chibi_model_runtime chibi_mesh_runtime chibi_support)
endif()
if(CHIBI_SDK_BUILD_COMPILER OR CHIBI_SDK_BUILD_NATIVE)
    chibi_sdk_contract(chibi_vector_capabilities "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_capabilities chibi_math chibi_execution)
    chibi_sdk_contract(chibi_math_capabilities "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_capabilities chibi_execution)
    chibi_sdk_contract(chibi_arithmetic_capabilities "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_capabilities chibi_execution)
endif()
if(CHIBI_SDK_BUILD_NATIVE)
    chibi_sdk_contract(chibi_audio_runtime "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities)
    chibi_sdk_contract(chibi_url_runtime "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities chibi_support)
    chibi_sdk_contract(chibi_rhythm_runtime "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_types chibi_native_capabilities chibi_execution)
    chibi_sdk_contract(chibi_particle_runtime "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_types chibi_native_capabilities)
    set(CHIBI_SAVE_PLATFORM_DEPENDENCIES)
    if(WIN32)
        list(APPEND CHIBI_SAVE_PLATFORM_DEPENDENCIES chibi_rooted_files)
    endif()
    chibi_sdk_contract(chibi_save_runtime "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities chibi_json ${CHIBI_SAVE_PLATFORM_DEPENDENCIES})
    chibi_sdk_contract(chibi_authoring_platform_declarations "${CHIBI_SDK_ROOT}/src/host"
        chibi_native_capabilities chibi_types)
    chibi_sdk_contract(chibi_value_json "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_types chibi_native_capabilities chibi_value_document chibi_asset_json chibi_support chibi_json)
    chibi_sdk_contract(chibi_ui_runtime "${CHIBI_SDK_ROOT}/src/runtime" chibi_asset_values chibi_native_capabilities chibi_value_document chibi_math)
    chibi_sdk_contract(chibi_input_runtime "${CHIBI_SDK_ROOT}/src/runtime" chibi_input_action chibi_native_types chibi_native_capabilities chibi_execution_contract chibi_behavior_runtime chibi_scheduling chibi_events chibi_spatial_runtime)
    chibi_sdk_contract(chibi_declared_components "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_types chibi_capabilities chibi_execution chibi_types chibi_support)
    chibi_sdk_contract(chibi_player "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_engine chibi_behavior_runtime chibi_scene_runtime chibi_field_catalog chibi_declared_components)
    chibi_sdk_contract(chibi_visibility_runtime "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_entity_runtime chibi_native_types chibi_native_capabilities chibi_physics_runtime chibi_spatial_runtime chibi_support)
    chibi_sdk_contract(chibi_engine "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_behavior_runtime chibi_entity_runtime chibi_native_types chibi_scheduling
        chibi_physics_runtime chibi_physics3d_runtime chibi_material_runtime chibi_visibility_runtime chibi_particle_runtime chibi_audio_runtime chibi_url_runtime
        chibi_ui_runtime chibi_save_runtime chibi_input_runtime chibi_task_pool_runtime
        chibi_spatial_runtime chibi_visual_motion_runtime chibi_execution chibi_support)
    chibi_sdk_contract(chibi_visual_motion_runtime "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_ecs chibi_native_capabilities chibi_native_types)
    chibi_sdk_contract(chibi_material_runtime "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_ecs chibi_tasks chibi_field_catalog chibi_native_capabilities chibi_native_types
        chibi_entity_runtime chibi_particle_runtime chibi_spatial_runtime chibi_task_pool_runtime
        chibi_execution chibi_support)
    chibi_sdk_contract(chibi_physics_runtime "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_types chibi_entity_runtime chibi_material_runtime chibi_task_pool_runtime
        chibi_native_capabilities chibi_spatial_runtime chibi_execution chibi_support chibi_box2d)
    chibi_sdk_contract(chibi_physics3d_runtime "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_types chibi_entity_runtime chibi_native_capabilities chibi_task_pool_runtime
        chibi_spatial_runtime chibi_mesh_runtime chibi_execution chibi_support chibi_box3d)
    chibi_sdk_contract(chibi_task_pool_runtime "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_tasks chibi_support chibi_task_scheduler Tracy::TracyClient)
    chibi_sdk_contract(chibi_host_native_default "${CHIBI_SDK_ROOT}/src/host"
        chibi_host_native_catalog chibi_core_capabilities chibi_array_capabilities chibi_field_capabilities
        chibi_state_capabilities chibi_selection_capabilities chibi_set_capabilities chibi_comparison_capabilities
        chibi_conversion_capabilities chibi_text_capabilities chibi_native_bindings chibi_entity_runtime
        chibi_audio_runtime chibi_url_runtime chibi_rhythm_runtime chibi_vector_capabilities chibi_math_capabilities
        chibi_arithmetic_capabilities chibi_particle_runtime chibi_save_runtime chibi_ui_runtime chibi_value_json chibi_json_query chibi_color_value
        chibi_visual_motion_runtime chibi_physics_runtime chibi_physics3d_runtime chibi_material_runtime
        chibi_visibility_runtime chibi_input_runtime)
    chibi_sdk_contract(chibi_tilemap_runtime "${CHIBI_SDK_ROOT}/src/runtime" chibi_math chibi_support)
    chibi_sdk_contract(chibi_spatial_runtime "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_types)
    get_filename_component(native_generated "${CHIBI_NATIVE_TYPES_SOURCE}" DIRECTORY)
    chibi_sdk_contract(chibi_native_types "${native_generated}"
        chibi_ecs chibi_math chibi_execution_contract)
    chibi_sdk_contract(chibi_native_bindings "${native_generated}"
        chibi_native_types chibi_native_capabilities chibi_spatial_runtime)
    chibi_sdk_contract(chibi_entity_runtime "${CHIBI_SDK_ROOT}/src/runtime/behaviors"
        chibi_native_types chibi_execution chibi_scene_runtime chibi_behavior_runtime chibi_native_capabilities
        chibi_spatial_runtime chibi_tilemap_runtime)
endif()

if(CHIBI_SDK_BUILD_COMPILER)
    chibi_sdk_contract(chibi_asset_json "${CHIBI_SDK_ROOT}/src/support" chibi_asset_values chibi_json)
    chibi_sdk_contract(chibi_graph_compilation "${CHIBI_SDK_ROOT}/src/graph"
        chibi_graph_authoring chibi_shader_runtime chibi_model_runtime chibi_fields chibi_field_catalog chibi_field_capabilities)
    chibi_sdk_contract(chibi_source_catalog "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_authoring chibi_input_map chibi_asset_text chibi_ui_document)
    chibi_sdk_contract(chibi_source_presentation "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_authoring chibi_graph_model chibi_text_source chibi_json chibi_graph_authoring)
    chibi_sdk_contract(chibi_graph_catalog "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_graph_model chibi_native_capabilities chibi_ecs chibi_source_presentation chibi_graph_compilation)
    chibi_sdk_contract(chibi_graph_authoring "${CHIBI_SDK_ROOT}/src/graph"
        chibi_graph_model chibi_ecs chibi_events chibi_asset_values chibi_asset_text chibi_value_document chibi_conversion_capabilities chibi_capabilities chibi_core_capabilities chibi_math_capabilities chibi_arithmetic_capabilities
        chibi_comparison_capabilities chibi_text_capabilities chibi_vector_capabilities chibi_state_capabilities
        chibi_array_capabilities chibi_selection_capabilities chibi_set_capabilities)
    chibi_sdk_contract(chibi_graph_model "${CHIBI_SDK_ROOT}/src/graph/model" chibi_types chibi_execution_contract chibi_asset_values)
    chibi_sdk_contract(chibi_text_source "${CHIBI_SDK_ROOT}/src/support" chibi_support chibi_text_search)
    chibi_sdk_contract(chibi_ui_document "${CHIBI_SDK_ROOT}/src" chibi_text_source chibi_ui_document_values)
    chibi_sdk_contract(chibi_ui_authoring "${CHIBI_SDK_ROOT}/src/assets"
        chibi_text_source chibi_ui_document chibi_ui_syntax chibi_syntax_runtime)
    chibi_sdk_contract(chibi_scene_document "${CHIBI_SDK_ROOT}/src/assets"
        chibi_ecs chibi_asset_values chibi_scene_runtime chibi_asset_text chibi_support)
    chibi_sdk_contract(chibi_prototype_document "${CHIBI_SDK_ROOT}/src/assets"
        chibi_ecs chibi_asset_values chibi_scene_document chibi_asset_text)
    chibi_sdk_contract(chibi_reference_index "${CHIBI_SDK_ROOT}/src/authoring" chibi_support chibi_events)
    chibi_sdk_contract(chibi_input_map "${CHIBI_SDK_ROOT}/src/assets"
        chibi_input_action chibi_types chibi_asset_values chibi_asset_text chibi_support)
    chibi_sdk_contract(chibi_project_document "${CHIBI_SDK_ROOT}/src"
        chibi_input_action chibi_asset_text chibi_language_syntax chibi_json chibi_support chibi_rooted_files chibi_asset_json)
    chibi_sdk_contract(chibi_component_presentation "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_ecs chibi_asset_text)
    chibi_sdk_contract(chibi_asset_text "${CHIBI_SDK_ROOT}/src/support"
        chibi_asset_values chibi_asset_kdl)
    chibi_sdk_contract(chibi_language_syntax "${CHIBI_SDK_ROOT}/src/language"
        chibi_support chibi_syntax_runtime)
    chibi_sdk_contract(chibi_language "${CHIBI_SDK_ROOT}/src/language"
        chibi_language_syntax chibi_capabilities chibi_json)
    chibi_sdk_contract(chibi_compiler "${CHIBI_SDK_ROOT}/src/language"
        chibi_language chibi_execution)
    chibi_sdk_contract(chibi_authoring "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_compiler chibi_native_capabilities chibi_reference_index chibi_json chibi_asset_text chibi_input_action)
endif()

if(CHIBI_SDK_BUILD_DESKTOP)
    chibi_sdk_contract(chibi_platform "${CHIBI_SDK_ROOT}/src/support" chibi_json chibi_support
        "$<BUILD_INTERFACE:cute>" "$<INSTALL_INTERFACE:${CHIBI_DESKTOP_CUTE}>")
    chibi_sdk_contract(chibi_ui_host "${CHIBI_SDK_ROOT}/src" chibi_ui_style chibi_platform chibi_json chibi_ui_document_values chibi_ui_documents chibi_builtin_fonts chibi_text_search chibi_support
        "$<BUILD_INTERFACE:RmlUi::Core>" "$<INSTALL_INTERFACE:${CHIBI_DESKTOP_RMLUI}>" c++ stdc++)
    chibi_sdk_contract(chibi_renderer "${CHIBI_SDK_ROOT}/src" chibi_ui_loader chibi_platform chibi_ui_host chibi_engine chibi_player
        chibi_shader_runtime chibi_model_runtime chibi_mesh_runtime chibi_native_types chibi_native_capabilities
        chibi_particle_runtime chibi_material_runtime chibi_ui_runtime chibi_save_runtime chibi_ui_document_values chibi_ui_documents chibi_visual_catalog
        chibi_image_catalog chibi_spatial_runtime chibi_support chibi_json chibi_asset_values chibi_task_pool_runtime chibi_rhythm_runtime chibi_tilemap_runtime
        "-framework Metal" "-framework Foundation")
endif()
if(CHIBI_SDK_BUILD_NATIVE)
    chibi_sdk_contract(chibi_visual_catalog "${CHIBI_SDK_ROOT}/src/render"
        chibi_native_types chibi_shader_runtime chibi_model_runtime chibi_mesh_runtime
        chibi_native_capabilities chibi_image_catalog chibi_types chibi_support)
endif()
if(CHIBI_SDK_BUILD_COMPILER AND CHIBI_SDK_BUILD_NATIVE)
    chibi_sdk_contract(chibi_workspace_documents "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_rooted_files         chibi_native_capabilities chibi_execution chibi_types chibi_support)
    chibi_sdk_contract(chibi_workspace_resources "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_rooted_files         chibi_native_capabilities chibi_reference_index chibi_types chibi_asset_values chibi_execution chibi_resource_catalog
        chibi_asset_json chibi_project_document chibi_asset_text chibi_support)
    chibi_sdk_contract(chibi_document_semantics "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_workspace_documents chibi_source_presentation chibi_ui_authoring chibi_text_search chibi_language chibi_graph_authoring chibi_graph_compilation chibi_value_document chibi_value_json chibi_asset_text chibi_asset_authoring chibi_native_capabilities chibi_execution chibi_types chibi_support chibi_text_capabilities)
    chibi_sdk_contract(chibi_workspace_assets "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_component_presentation chibi_workspace_presentation chibi_scene_document chibi_workspace_documents chibi_workspace_resources chibi_declared_components chibi_asset_authoring chibi_native_capabilities chibi_asset_json chibi_graph_authoring chibi_asset_text chibi_native_types chibi_language chibi_execution chibi_types chibi_support)
    chibi_sdk_contract(chibi_visual_authoring "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_workspace_documents chibi_material_authoring chibi_native_capabilities chibi_graph_authoring chibi_asset_text chibi_asset_json
        chibi_execution chibi_types chibi_support)
    chibi_sdk_contract(chibi_ui_source_sessions "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_workspace_documents chibi_native_capabilities chibi_types chibi_ui_authoring chibi_execution chibi_support)
    chibi_sdk_contract(chibi_workspace_catalog "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_native_capabilities chibi_types chibi_workspace_documents chibi_workspace_resources chibi_document_semantics
        chibi_visual_authoring chibi_host_native_default chibi_visual_catalog chibi_support chibi_workspace_assets chibi_authoring_platform_declarations chibi_ui_source_sessions chibi_document_graph chibi_visual_model chibi_ui_dom_declarations chibi_observations chibi_workspace_runtime_declarations chibi_workspace_simulation_declarations chibi_browser_messages chibi_workspace_debug chibi_project_semantics chibi_resource_previews_declarations chibi_workspace_info chibi_workspace_imports chibi_workspace_file_history chibi_ui_source_preview_declarations chibi_css_values chibi_workspace_tools_declarations)
    chibi_sdk_contract(chibi_scene_authoring "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_native_types chibi_scene_document)
    chibi_sdk_contract(chibi_asset_authoring "${CHIBI_SDK_ROOT}/src"
        chibi_prototype_document chibi_graph_model chibi_reference_index chibi_field_catalog chibi_authoring
        chibi_asset_text chibi_project_document chibi_scene_document chibi_ui_document chibi_graph_authoring chibi_graph_compilation
        chibi_native_types chibi_text_source chibi_declared_components)
    chibi_sdk_contract(chibi_material_authoring "${CHIBI_SDK_ROOT}/src"
        chibi_graph_model chibi_shader_runtime chibi_asset_authoring chibi_graph_authoring
        chibi_graph_compilation chibi_asset_text chibi_image_preparation chibi_project_document chibi_asset_json chibi_support)
    chibi_sdk_contract(chibi_resource_catalog "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_asset_values chibi_image_preparation chibi_mesh_runtime chibi_audio_runtime chibi_support)
    chibi_sdk_contract(chibi_model_export_artifacts "${CHIBI_SDK_ROOT}/src/assets" chibi_model_bake chibi_model_runtime chibi_mesh_runtime chibi_json chibi_support chibi_types)
    chibi_sdk_contract(chibi_model_authoring "${CHIBI_SDK_ROOT}/src/assets"
        chibi_graph_model chibi_model_bake chibi_model_export_artifacts chibi_image_preparation chibi_shader_preparation chibi_graph_authoring chibi_graph_compilation
        chibi_visual_catalog chibi_asset_text chibi_json chibi_support)
    if(TARGET chibi_model_import)
        chibi_sdk_contract(chibi_model_import "${CHIBI_SDK_ROOT}/src/assets"
            chibi_graph_model chibi_shader_runtime chibi_material_authoring chibi_platform chibi_asset_authoring chibi_prototype_document chibi_project_document
            chibi_image_preparation chibi_mesh_runtime chibi_native_types chibi_graph_authoring
            chibi_graph_compilation chibi_asset_text chibi_json chibi_support)
    endif()
    chibi_sdk_contract(chibi_scenario_document "${CHIBI_SDK_ROOT}/src/assets"
        chibi_asset_values chibi_input_runtime chibi_native_types chibi_asset_text)
    chibi_sdk_contract(chibi_shader_preparation "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_visual_catalog chibi_graph_model chibi_player chibi_graph_compilation chibi_graph_authoring chibi_asset_text chibi_json chibi_image_catalog)
    chibi_sdk_contract(chibi_project_compilation "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_source_catalog chibi_prototype_document chibi_scene_document chibi_player chibi_native_prepared
        chibi_shader_preparation chibi_graph_catalog chibi_host_native_default chibi_graph_compilation chibi_graph_authoring
        chibi_asset_authoring chibi_asset_text chibi_project_document chibi_ui_authoring chibi_builtin_fonts
        chibi_image_preparation chibi_json chibi_workspace_catalog chibi_visual_authoring chibi_authoring_platform_declarations)
    if(CHIBI_SDK_BUILD_DESKTOP)
        chibi_sdk_contract(chibi_visual_previews "${CHIBI_SDK_ROOT}/src/authoring"
            chibi_visual_authoring chibi_project_preparation chibi_workspace_resources chibi_shader_preparation chibi_image_preparation chibi_renderer chibi_platform chibi_graph_model chibi_image_catalog chibi_support chibi_visual_model chibi_rooted_files)
        chibi_sdk_contract(chibi_authoring_host "${CHIBI_SDK_ROOT}/src/host"
            chibi_native_capabilities chibi_behavior_runtime chibi_player chibi_workspace_documents chibi_workspace_resources chibi_workspace_assets
            chibi_document_semantics chibi_visual_authoring chibi_ui_source_sessions chibi_visual_previews chibi_workspace_catalog chibi_graph_catalog
            chibi_project_preparation chibi_project_document chibi_renderer chibi_image_catalog chibi_native_types chibi_support chibi_platform chibi_json chibi_workspace_runtime chibi_authoring_platform chibi_ui_dom chibi_observations chibi_document_graph chibi_visual_model chibi_workspace_debug chibi_resource_previews chibi_browser_messages chibi_image_preparation chibi_project_semantics chibi_workspace_info chibi_workspace_imports chibi_workspace_file_history chibi_ui_source_preview chibi_workspace_simulation chibi_project_compilation chibi_native_prepared chibi_workspace_tools chibi_ui_source_provider)
        chibi_sdk_contract(chibi_project_preparation "${CHIBI_SDK_ROOT}/src/authoring"
            chibi_project_compilation chibi_source_presentation chibi_engine chibi_ui_host chibi_renderer
            chibi_graph_catalog chibi_workspace_catalog chibi_host_native_default chibi_graph_authoring
            chibi_graph_compilation chibi_asset_authoring chibi_asset_text chibi_project_document chibi_ui_authoring chibi_image_preparation
            chibi_declared_components)
    endif()
endif()

# Consumers select exported targets as package components, so ChibiConfig
# must list each one for check_required_components to accept it.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/ChibiConfig.cmake.in" chibi_config)
string(REGEX MATCH "foreach\\(component (support[^)]*)\\)" chibi_component_list "${chibi_config}")
string(REGEX REPLACE "[ \n]+" ";" chibi_components "${CMAKE_MATCH_1}")
foreach(target IN LISTS CHIBI_SDK_TARGETS)
    get_target_property(component ${target} EXPORT_NAME)
    if(NOT component IN_LIST chibi_components)
        message(FATAL_ERROR "ChibiConfig.cmake.in does not list ${target}'s component ${component}")
    endif()
endforeach()

if(TARGET chibi_json_query)
    chibi_sdk_contract(chibi_json_query "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities chibi_json chibi_support chibi_execution)
endif()
if(TARGET chibi_color_value)
    chibi_sdk_contract(chibi_color_value "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities chibi_execution chibi_support)
endif()
if(TARGET chibi_document_graph)
    chibi_sdk_contract(chibi_document_graph "${CHIBI_SDK_ROOT}/src/authoring" chibi_document_semantics chibi_graph_authoring chibi_graph_compilation chibi_graph_model chibi_asset_text chibi_value_document chibi_support chibi_execution)
endif()
if(TARGET chibi_visual_model)
    chibi_sdk_contract(chibi_visual_model "${CHIBI_SDK_ROOT}/src/authoring" chibi_model_export_artifacts chibi_workspace_documents chibi_graph_compilation chibi_model_bake chibi_model_runtime chibi_graph_authoring chibi_support chibi_execution chibi_image_preparation chibi_material_authoring chibi_project_document chibi_asset_json chibi_rooted_files chibi_utf8)
endif()
if(TARGET chibi_ui_dom_declarations)
    chibi_sdk_contract(chibi_ui_dom_declarations "${CHIBI_SDK_ROOT}/src/authoring" chibi_native_capabilities chibi_types chibi_execution)
endif()
if(TARGET chibi_observations)
    chibi_sdk_contract(chibi_observations "${CHIBI_SDK_ROOT}/src/authoring" chibi_native_capabilities chibi_types chibi_execution chibi_support)
endif()
if(TARGET chibi_runtime_properties)
    chibi_sdk_contract(chibi_runtime_properties "${CHIBI_SDK_ROOT}/src/authoring" chibi_component_presentation chibi_ecs chibi_types chibi_support chibi_native_capabilities chibi_value_document chibi_value_json chibi_asset_json chibi_utf8)
endif()
if(TARGET chibi_workspace_runtime_declarations)
    chibi_sdk_contract(chibi_workspace_runtime_declarations "${CHIBI_SDK_ROOT}/src/authoring" chibi_native_capabilities chibi_runtime_properties chibi_execution)
endif()
if(TARGET chibi_ui_dom)
    chibi_sdk_contract(chibi_ui_dom "${CHIBI_SDK_ROOT}/src/authoring" chibi_ui_dom_declarations chibi_ui_host chibi_support chibi_execution)
endif()
if(TARGET chibi_authoring_platform)
    chibi_sdk_contract(chibi_authoring_platform "${CHIBI_SDK_ROOT}/src/host" chibi_rooted_files chibi_renderer chibi_authoring_platform_declarations chibi_workspace_scaffold chibi_browser_messages chibi_workspace_documents chibi_project_document chibi_platform chibi_asset_text chibi_asset_json chibi_support chibi_execution)
endif()
if(TARGET chibi_workspace_runtime)
    chibi_sdk_contract(chibi_workspace_runtime "${CHIBI_SDK_ROOT}/src/authoring" chibi_visual_catalog chibi_component_presentation chibi_workspace_presentation chibi_scene_document chibi_runtime_properties chibi_workspace_documents chibi_workspace_runtime_declarations chibi_player chibi_value_json chibi_authoring_platform chibi_browser_compiler chibi_project_preparation chibi_project_document chibi_renderer chibi_platform chibi_native_types chibi_support chibi_execution chibi_visual_model chibi_image_preparation chibi_material_authoring chibi_asset_json chibi_rooted_files)
endif()

if(TARGET chibi_browser_messages)
 chibi_sdk_contract(chibi_browser_messages "${CHIBI_SDK_ROOT}/src/host" chibi_native_capabilities chibi_workspace_documents chibi_json_query chibi_json chibi_support chibi_execution)
endif()

if(TARGET chibi_workspace_debug)
chibi_sdk_contract(chibi_workspace_debug "${CHIBI_SDK_ROOT}/src/authoring" chibi_entity_runtime chibi_behavior_runtime chibi_declared_components chibi_execution chibi_value_json chibi_support chibi_workspace_assets chibi_workspace_resources chibi_workspace_documents)
endif()

if(TARGET chibi_project_semantics)
chibi_sdk_contract(chibi_project_semantics "${CHIBI_SDK_ROOT}/src/authoring" chibi_workspace_documents chibi_source_catalog chibi_source_presentation chibi_ui_authoring chibi_prototype_document chibi_scene_document chibi_support chibi_execution)
chibi_sdk_contract(chibi_resource_previews_declarations "${CHIBI_SDK_ROOT}/src/authoring" chibi_native_capabilities chibi_execution)
endif()
if(TARGET chibi_resource_previews)
chibi_sdk_contract(chibi_resource_previews "${CHIBI_SDK_ROOT}/src/authoring" chibi_builtin_fonts chibi_asset_text chibi_ui_source_provider chibi_document_semantics chibi_workspace_runtime chibi_workspace_info chibi_resource_preview_data chibi_rooted_files chibi_resource_previews_declarations chibi_workspace_documents chibi_workspace_resources chibi_image_catalog chibi_graph_model chibi_visual_previews chibi_image_preparation chibi_renderer chibi_platform chibi_graph_authoring chibi_support chibi_execution)
endif()

if(TARGET chibi_browser_compiler)
chibi_sdk_contract(chibi_browser_compiler "${CHIBI_SDK_ROOT}/src/host" chibi_workspace_documents chibi_browser_messages chibi_rooted_files chibi_utf8 chibi_json chibi_support)
endif()

if(TARGET chibi_workspace_info)
chibi_sdk_contract(chibi_workspace_info "${CHIBI_SDK_ROOT}/src/authoring" chibi_native_capabilities chibi_utf8 chibi_rooted_files chibi_workspace_documents chibi_project_document chibi_asset_text chibi_support chibi_execution)
endif()
if(TARGET chibi_workspace_imports)
chibi_sdk_contract(chibi_workspace_imports "${CHIBI_SDK_ROOT}/src/authoring" chibi_model_import chibi_native_capabilities chibi_support chibi_execution)
endif()

if(TARGET chibi_workspace_scaffold)
chibi_sdk_contract(chibi_workspace_scaffold "${CHIBI_SDK_ROOT}/src/authoring" chibi_workspace_info chibi_rooted_files chibi_project_document chibi_json chibi_support)
endif()

chibi_sdk_contract(chibi_utf8 "${CHIBI_SDK_ROOT}/src/support")
chibi_sdk_contract(chibi_rooted_files "${CHIBI_SDK_ROOT}/src/support" chibi_utf8 chibi_support ntdll "-framework CoreFoundation")

if(TARGET chibi_ui_replay)
    chibi_sdk_contract(chibi_ui_replay "${CHIBI_SDK_ROOT}/src/host" chibi_json chibi_json_query chibi_support)
endif()

chibi_sdk_contract(chibi_ui_style "${CHIBI_SDK_ROOT}/src/support" chibi_support)
chibi_sdk_contract(chibi_ui_loader "${CHIBI_SDK_ROOT}/src/render" chibi_ui_document_values)
if(TARGET chibi_ui_source_provider)
    chibi_sdk_contract(chibi_ui_source_provider "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_workspace_documents chibi_ui_loader chibi_ui_authoring chibi_ui_document chibi_ui_style chibi_rooted_files chibi_utf8 chibi_support)
    chibi_sdk_contract(chibi_ui_source_preview_declarations "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_workspace_documents chibi_native_capabilities chibi_types chibi_execution)
endif()
if(TARGET chibi_ui_source_preview)
    chibi_sdk_contract(chibi_ui_source_preview "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_ui_source_preview_declarations chibi_ui_source_provider chibi_workspace_runtime chibi_ui_dom chibi_renderer chibi_platform chibi_declared_components chibi_ecs chibi_support chibi_execution)
endif()

if(TARGET chibi_css_values)
    chibi_sdk_contract(chibi_css_values "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_native_capabilities chibi_color_value chibi_types chibi_execution)
endif()

if(TARGET chibi_workspace_file_history)
chibi_sdk_contract(chibi_workspace_file_history "${CHIBI_SDK_ROOT}/src/authoring"
    chibi_workspace_documents chibi_native_capabilities chibi_asset_text chibi_rooted_files chibi_support chibi_execution chibi_types)
endif()

if(TARGET chibi_resource_preview_data)
    chibi_sdk_contract(chibi_resource_preview_data "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_graph_model chibi_graph_authoring chibi_ui_authoring chibi_css_values chibi_color_value chibi_support)
endif()

if(TARGET chibi_workspace_simulation_declarations)
    chibi_sdk_contract(chibi_workspace_simulation_declarations "${CHIBI_SDK_ROOT}/src/authoring" chibi_native_capabilities)
endif()
if(TARGET chibi_workspace_simulation)
    chibi_sdk_contract(chibi_workspace_simulation "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_workspace_simulation_declarations chibi_workspace_runtime chibi_input_runtime chibi_ui_host chibi_renderer
        chibi_material_runtime chibi_asset_json chibi_rooted_files chibi_utf8 chibi_declared_components chibi_native_types
        chibi_ecs chibi_types chibi_support chibi_execution)
endif()

if(TARGET chibi_workspace_presentation)
    chibi_sdk_contract(chibi_workspace_presentation "${CHIBI_SDK_ROOT}/src/authoring" chibi_component_presentation chibi_workspace_documents)
endif()

if(TARGET chibi_workspace_tools_declarations)
    chibi_sdk_contract(chibi_workspace_tools_declarations "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_native_capabilities chibi_types chibi_save_runtime chibi_execution chibi_support)
endif()
if(TARGET chibi_workspace_tools)
    chibi_sdk_contract(chibi_workspace_tools "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_workspace_tools_declarations chibi_workspace_runtime chibi_save_runtime chibi_workspace_documents
        chibi_ui_source_provider chibi_ui_document chibi_ui_style chibi_asset_text chibi_support chibi_execution chibi_types)
endif()
