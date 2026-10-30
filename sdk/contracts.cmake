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
chibi_sdk_contract(chibi_execution_contract "${CHIBI_SDK_ROOT}/src/execution" chibi_types chibi_capabilities)
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
chibi_sdk_contract(chibi_host_clock "${CHIBI_SDK_ROOT}/src/host" chibi_native_capabilities)
chibi_sdk_contract(chibi_core_capabilities "${CHIBI_SDK_ROOT}/src/runtime"
    chibi_native_capabilities chibi_execution)
chibi_sdk_contract(chibi_array_capabilities "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities)
chibi_sdk_contract(chibi_field_capabilities "${CHIBI_SDK_ROOT}/src/runtime/fields" chibi_native_capabilities chibi_fields)
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
endif()
if(CHIBI_SDK_BUILD_COMPILER OR CHIBI_SDK_BUILD_NATIVE)
    chibi_sdk_contract(chibi_vector_capabilities "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_capabilities chibi_math)
    chibi_sdk_contract(chibi_math_capabilities "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_capabilities chibi_execution)
    chibi_sdk_contract(chibi_arithmetic_capabilities "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_capabilities chibi_execution)
endif()
if(CHIBI_SDK_BUILD_NATIVE)
    chibi_sdk_contract(chibi_audio_runtime "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities)
    chibi_sdk_contract(chibi_url_runtime "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities chibi_support)
    chibi_sdk_contract(chibi_particle_runtime "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_types chibi_native_capabilities)
    chibi_sdk_contract(chibi_save_runtime "${CHIBI_SDK_ROOT}/src/runtime" chibi_native_capabilities chibi_json)
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
        chibi_spatial_runtime chibi_execution chibi_support)
    chibi_sdk_contract(chibi_material_runtime "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_ecs chibi_tasks chibi_field_catalog chibi_native_capabilities chibi_native_types
        chibi_entity_runtime chibi_particle_runtime chibi_spatial_runtime chibi_task_pool_runtime
        chibi_execution chibi_support chibi_save_runtime chibi_mesh_runtime chibi_shader_runtime)
    chibi_sdk_contract(chibi_physics_runtime "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_types chibi_entity_runtime chibi_material_runtime chibi_task_pool_runtime
        chibi_native_capabilities chibi_spatial_runtime chibi_execution chibi_support chibi_box2d)
    chibi_sdk_contract(chibi_physics3d_runtime "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_native_types chibi_entity_runtime chibi_native_capabilities chibi_task_pool_runtime
        chibi_spatial_runtime chibi_mesh_runtime chibi_execution chibi_support chibi_box3d)
    chibi_sdk_contract(chibi_task_pool_runtime "${CHIBI_SDK_ROOT}/src/runtime"
        chibi_tasks chibi_support chibi_task_scheduler)
    chibi_sdk_contract(chibi_host_native_default "${CHIBI_SDK_ROOT}/src/host"
        chibi_host_native_catalog chibi_core_capabilities chibi_array_capabilities chibi_field_capabilities
        chibi_state_capabilities chibi_selection_capabilities chibi_set_capabilities chibi_comparison_capabilities
        chibi_conversion_capabilities chibi_text_capabilities chibi_native_bindings chibi_entity_runtime
        chibi_audio_runtime chibi_url_runtime chibi_vector_capabilities chibi_math_capabilities
        chibi_arithmetic_capabilities chibi_particle_runtime chibi_save_runtime chibi_ui_runtime
        chibi_physics_runtime chibi_physics3d_runtime chibi_material_runtime
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
    chibi_sdk_contract(chibi_source_catalog "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_authoring chibi_input_map chibi_asset_text chibi_ui_document chibi_prototype_document chibi_json chibi_tasks)
    chibi_sdk_contract(chibi_studio_capabilities "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_native_capabilities chibi_execution)
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
    chibi_sdk_contract(chibi_project_document "${CHIBI_SDK_ROOT}/src/assets"
        chibi_input_action chibi_asset_text chibi_language_syntax chibi_json chibi_support)
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
    chibi_sdk_contract(chibi_program "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_source_catalog chibi_native_capabilities chibi_behavior_runtime)
endif()

if(CHIBI_SDK_BUILD_DESKTOP)
    chibi_sdk_contract(chibi_platform "${CHIBI_SDK_ROOT}/src/support" chibi_json chibi_support
        "${CHIBI_DESKTOP_CUTE}")
    chibi_sdk_contract(chibi_ui_host "${CHIBI_SDK_ROOT}/src" chibi_platform chibi_json chibi_ui_document_values chibi_ui_documents chibi_builtin_fonts chibi_text_search chibi_support
        "${CHIBI_DESKTOP_RMLUI}" c++ stdc++)
    chibi_sdk_contract(chibi_renderer "${CHIBI_SDK_ROOT}/src" chibi_platform chibi_ui_host chibi_engine chibi_player
        chibi_shader_runtime chibi_model_runtime chibi_mesh_runtime chibi_native_types chibi_native_capabilities
        chibi_particle_runtime chibi_material_runtime chibi_ui_runtime chibi_save_runtime chibi_ui_document_values chibi_ui_documents chibi_visual_catalog
        chibi_image_catalog chibi_spatial_runtime chibi_support chibi_json chibi_asset_values chibi_task_pool_runtime chibi_tilemap_runtime
        "-framework Metal" "-framework Foundation")
endif()
if(CHIBI_SDK_BUILD_NATIVE)
    chibi_sdk_contract(chibi_visual_catalog "${CHIBI_SDK_ROOT}/src/render"
        chibi_native_types chibi_shader_runtime chibi_model_runtime chibi_mesh_runtime
        chibi_native_capabilities chibi_image_catalog chibi_types chibi_support)
endif()
if(CHIBI_SDK_BUILD_COMPILER AND CHIBI_SDK_BUILD_NATIVE)
    chibi_sdk_contract(chibi_compiler_syntax "${CHIBI_SDK_ROOT}/src/toolchain"
        chibi_language_syntax chibi_native_capabilities)
    chibi_sdk_contract(chibi_compiler_json "${CHIBI_SDK_ROOT}/src/toolchain"
        chibi_json chibi_native_capabilities)
    chibi_sdk_contract(chibi_scene_authoring "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_native_types chibi_scene_document)
    chibi_sdk_contract(chibi_asset_authoring "${CHIBI_SDK_ROOT}/src"
        chibi_prototype_document chibi_reference_index chibi_field_catalog chibi_authoring
        chibi_asset_text chibi_scene_document chibi_ui_document
        chibi_native_types chibi_text_source chibi_declared_components)
    chibi_sdk_contract(chibi_scenario_document "${CHIBI_SDK_ROOT}/src/assets"
        chibi_asset_values chibi_input_runtime chibi_native_types chibi_asset_text)
    chibi_sdk_contract(chibi_shader_preparation "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_visual_catalog chibi_player chibi_asset_text chibi_json chibi_image_catalog)
    chibi_sdk_contract(chibi_project_compilation "${CHIBI_SDK_ROOT}/src/authoring"
        chibi_source_catalog chibi_prototype_document chibi_scene_document chibi_player chibi_native_prepared
        chibi_shader_preparation chibi_host_native_default
        chibi_asset_authoring chibi_asset_text chibi_project_document chibi_text_source chibi_ui_authoring chibi_builtin_fonts
        chibi_image_preparation chibi_json chibi_compiler_syntax chibi_compiler_json chibi_text_capabilities chibi_array_capabilities chibi_task_pool_runtime
        chibi_asset_compiler_host)
    if(CHIBI_SDK_BUILD_DESKTOP)
        chibi_sdk_contract(chibi_project_preparation "${CHIBI_SDK_ROOT}/src/authoring"
            chibi_project_compilation chibi_engine chibi_ui_host chibi_renderer
 chibi_host_native_default
 chibi_asset_authoring chibi_asset_text chibi_project_document chibi_ui_authoring chibi_image_preparation
            chibi_declared_components)
    endif()
endif()

# Consumers select exported targets as package components, so ChibiConfig
# must list each one for check_required_components to accept it.
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/ChibiConfig.cmake.in" chibi_config)
string(REGEX MATCH "foreach\\(component ([^)]*)\\)" chibi_component_list "${chibi_config}")
string(REGEX REPLACE "[ \n]+" ";" chibi_components "${CMAKE_MATCH_1}")
foreach(target IN LISTS CHIBI_SDK_TARGETS)
    get_target_property(component ${target} EXPORT_NAME)
    if(NOT component IN_LIST chibi_components)
        message(FATAL_ERROR "ChibiConfig.cmake.in does not list ${target}'s component ${component}")
    endif()
endforeach()
