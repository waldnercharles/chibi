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
    "${CHIBI_SDK_ROOT}/src/assets/field_parameters.c")
add_library(Chibi::asset_authoring ALIAS chibi_asset_authoring)
set_target_properties(chibi_asset_authoring PROPERTIES EXPORT_NAME asset_authoring)
chibi_sdk_headers(chibi_asset_authoring PUBLIC
    assets/call_hierarchy.h
    authoring/native_prototype.h assets/prototype_instance.h assets/field_parameters.h)
target_link_libraries(chibi_asset_authoring PUBLIC
    chibi_prototype_document chibi_reference_index chibi_field_catalog
    PRIVATE chibi_authoring chibi_asset_text chibi_scene_document chibi_ui_document
 chibi_native_types chibi_text_source
    chibi_declared_components)

add_library(chibi_scenario_document STATIC "${CHIBI_SDK_ROOT}/src/assets/scenario_document.c")
add_library(Chibi::scenario_document ALIAS chibi_scenario_document)
set_target_properties(chibi_scenario_document PROPERTIES EXPORT_NAME scenario_document)
chibi_sdk_headers(chibi_scenario_document PUBLIC assets/scenario_document.h)
target_link_libraries(chibi_scenario_document PUBLIC chibi_asset_values chibi_input_runtime
    PRIVATE chibi_native_types chibi_asset_text)

list(APPEND CHIBI_SDK_TARGETS chibi_scene_authoring chibi_asset_authoring chibi_scenario_document)

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
