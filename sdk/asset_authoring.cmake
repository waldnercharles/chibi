# Asset mutations and declarative language services are authoring utilities.
# The model export and scenario document packages remain separately selectable.
add_library(chibi_scene_authoring STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/scene_hierarchy.c"
    "${CHIBI_SDK_ROOT}/src/authoring/scene_snapshot.c")
add_library(Chibi::scene_authoring ALIAS chibi_scene_authoring)
set_target_properties(chibi_scene_authoring PROPERTIES EXPORT_NAME scene_authoring)
chibi_sdk_headers(chibi_scene_authoring PUBLIC authoring/scene_hierarchy.h authoring/scene_snapshot.h)
target_link_libraries(chibi_scene_authoring PUBLIC chibi_native_types PRIVATE chibi_scene_document)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    # Match the runtime transform propagation's multiply/add rounding.
    target_compile_options(chibi_scene_authoring PRIVATE -ffp-contract=off)
endif()

add_library(chibi_asset_authoring STATIC
    "${CHIBI_SDK_ROOT}/src/assets/call_hierarchy.c"
    "${CHIBI_SDK_ROOT}/src/authoring/native_prototype.c"
    "${CHIBI_SDK_ROOT}/src/assets/prototype_instance.c"
    "${CHIBI_SDK_ROOT}/src/assets/field_parameters.c"
    "${CHIBI_SDK_ROOT}/src/assets/new_asset.c"
    "${CHIBI_SDK_ROOT}/src/assets/asset_source.c"
    "${CHIBI_SDK_ROOT}/src/assets/reference_adapters.c"
    "${CHIBI_SDK_ROOT}/src/assets/refactor.c")
add_library(Chibi::asset_authoring ALIAS chibi_asset_authoring)
set_target_properties(chibi_asset_authoring PROPERTIES EXPORT_NAME asset_authoring)
chibi_sdk_headers(chibi_asset_authoring PUBLIC
    assets/call_hierarchy.h
    authoring/native_prototype.h assets/prototype_instance.h assets/field_parameters.h
    assets/new_asset.h assets/asset_source.h assets/reference_adapters.h assets/refactor.h
    support/code_completion.h)
target_link_libraries(chibi_asset_authoring PUBLIC
    chibi_prototype_document chibi_reference_index chibi_field_catalog
    PRIVATE chibi_authoring chibi_asset_text chibi_scene_document chibi_ui_document
 chibi_native_types chibi_text_source
    chibi_declared_components)

add_library(chibi_model_authoring STATIC
    "${CHIBI_SDK_ROOT}/src/assets/model_source.c"
    "${CHIBI_SDK_ROOT}/src/assets/model_validation.c"
    "${CHIBI_SDK_ROOT}/src/assets/model_export.c")
add_library(Chibi::model_authoring ALIAS chibi_model_authoring)
set_target_properties(chibi_model_authoring PROPERTIES EXPORT_NAME model_authoring)
chibi_sdk_headers(chibi_model_authoring PUBLIC
    assets/model_source.h assets/model_validation.h assets/model_export.h)
target_include_directories(chibi_model_authoring PRIVATE "${CHIBI_SDK_ROOT}/third_party/yyjson")
target_link_libraries(chibi_model_authoring PUBLIC chibi_model_bake
    PRIVATE chibi_image_preparation chibi_shader_preparation
    chibi_visual_catalog chibi_asset_text chibi_json chibi_support)

# glTF import reads models through Cute's loader, which only the desktop
# archive carries.
if(CHIBI_SDK_BUILD_DESKTOP)
    add_library(chibi_model_import STATIC
        "${CHIBI_SDK_ROOT}/src/assets/model_import.c"
        "${CHIBI_SDK_ROOT}/src/assets/standard_material.c")
    add_library(Chibi::model_import ALIAS chibi_model_import)
    set_target_properties(chibi_model_import PROPERTIES EXPORT_NAME model_import)
    chibi_sdk_headers(chibi_model_import PUBLIC assets/model_import.h assets/standard_material.h)
    target_include_directories(chibi_model_import PRIVATE "${CHIBI_SDK_ROOT}/third_party/yyjson")
    target_link_libraries(chibi_model_import PUBLIC chibi_shader_runtime
        PRIVATE chibi_platform chibi_asset_authoring chibi_prototype_document chibi_image_preparation
        chibi_mesh_runtime chibi_native_types
        chibi_asset_text chibi_json chibi_support chibi_source_catalog)
    list(APPEND CHIBI_SDK_TARGETS chibi_model_import)
endif()

add_library(chibi_scenario_document STATIC "${CHIBI_SDK_ROOT}/src/assets/scenario_document.c")
add_library(Chibi::scenario_document ALIAS chibi_scenario_document)
set_target_properties(chibi_scenario_document PROPERTIES EXPORT_NAME scenario_document)
chibi_sdk_headers(chibi_scenario_document PUBLIC assets/scenario_document.h)
target_link_libraries(chibi_scenario_document PUBLIC chibi_asset_values chibi_input_runtime
    PRIVATE chibi_native_types chibi_asset_text)

list(APPEND CHIBI_SDK_TARGETS chibi_scene_authoring chibi_asset_authoring chibi_model_authoring chibi_scenario_document)

# Canonical asset formatting, shared by chibi fmt and the automation controller.
add_library(chibi_project_format STATIC "${CHIBI_SDK_ROOT}/src/authoring/project_format.c")
add_library(Chibi::project_format ALIAS chibi_project_format)
set_target_properties(chibi_project_format PROPERTIES EXPORT_NAME project_format)
chibi_sdk_headers(chibi_project_format PUBLIC authoring/project_format.h)
target_link_libraries(chibi_project_format PUBLIC chibi_native_capabilities
    PRIVATE chibi_project_compilation chibi_source_catalog chibi_source_presentation chibi_authoring
    chibi_component_presentation chibi_scenario_document chibi_scene_document chibi_prototype_document
 chibi_input_map chibi_declared_components chibi_project_document
    chibi_asset_text chibi_support)
list(APPEND CHIBI_SDK_TARGETS chibi_project_format)
if(TARGET chibi-compile)
    target_link_libraries(chibi-compile PRIVATE chibi_project_format)
endif()
