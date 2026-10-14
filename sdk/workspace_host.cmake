# Reusable graphical authoring services and a generic prepared-application host.
add_library(chibi_visual_previews STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/visual_authoring_preview.c"
    "${CHIBI_SDK_ROOT}/src/authoring/visual_authoring_preview_native.c")
add_library(Chibi::visual_previews ALIAS chibi_visual_previews)
set_target_properties(chibi_visual_previews PROPERTIES EXPORT_NAME visual_previews)
chibi_sdk_headers(chibi_visual_previews PUBLIC authoring/visual_authoring_preview.h authoring/visual_model_preview.h authoring/visual_catalog.h)
target_link_libraries(chibi_visual_previews PUBLIC chibi_visual_authoring chibi_project_preparation
    PRIVATE chibi_workspace_resources chibi_shader_preparation chibi_image_preparation chibi_renderer chibi_platform chibi_graph_model chibi_image_catalog chibi_support)
target_compile_definitions(chibi_visual_previews PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)

add_library(chibi_authoring_host STATIC "${CHIBI_SDK_ROOT}/src/host/workspace_host.c"
    "${CHIBI_SDK_ROOT}/src/host/package_identity.c")
add_library(Chibi::authoring_host ALIAS chibi_authoring_host)
set_target_properties(chibi_authoring_host PROPERTIES EXPORT_NAME authoring_host)
chibi_sdk_headers(chibi_authoring_host PUBLIC host/workspace_host.h host/package_identity.h)
target_link_libraries(chibi_authoring_host PUBLIC chibi_native_capabilities chibi_behavior_runtime chibi_observations chibi_player
    PRIVATE chibi_workspace_documents chibi_workspace_resources chibi_workspace_assets chibi_document_semantics
    chibi_visual_authoring chibi_ui_source_sessions chibi_visual_previews chibi_workspace_catalog chibi_graph_catalog
    chibi_project_preparation chibi_project_document chibi_renderer chibi_image_catalog chibi_native_types chibi_support chibi_platform chibi_json)
target_compile_definitions(chibi_authoring_host PRIVATE _POSIX_C_SOURCE=200809L _DEFAULT_SOURCE _DARWIN_C_SOURCE)
list(APPEND CHIBI_SDK_TARGETS chibi_visual_previews chibi_authoring_host)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR CHIBI_WEB_AUTHOR)
add_executable(chibi-author "${CHIBI_SDK_ROOT}/src/host/player_main.c"
    "${CHIBI_SDK_ROOT}/src/host/player_url.c")
target_compile_definitions(chibi-author PRIVATE CHIBI_AUTHORING_HOST)
foreach(header player_url player_clock)
    configure_file("${CHIBI_SDK_ROOT}/src/host/${header}.h"
        "${CMAKE_CURRENT_BINARY_DIR}/private/player/host/${header}.h" COPYONLY)
endforeach()
target_include_directories(chibi-author PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/player")
target_link_libraries(chibi-author PRIVATE Chibi::rooted_files Chibi::project_document Chibi::render Chibi::player Chibi::native_prepared
    Chibi::workspace_catalog Chibi::visual_previews Chibi::authoring_host Chibi::graph_model)
set_target_properties(chibi-author PROPERTIES LINKER_LANGUAGE CXX)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    chibi_windows_manifest(chibi-author)
    install(TARGETS chibi-author RUNTIME DESTINATION bin)
else()
    set_target_properties(chibi-author PROPERTIES SUFFIX ".js")
    target_link_options(chibi-author PRIVATE
        "--pre-js=${CHIBI_SDK_ROOT}/src/host/web/author_data.js"
        "--pre-js=${CHIBI_SDK_ROOT}/src/host/web/browser_messages.js"
        "--pre-js=${CHIBI_SDK_ROOT}/src/host/web/browser_compiler.js"
        "--pre-js=${CHIBI_SDK_ROOT}/src/host/web/project_migration.js"
        "-sEXPORTED_RUNTIME_METHODS=['stringToNewUTF8']"
        "-sEXPORTED_FUNCTIONS=['_main','_malloc','_free']"
        -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576 -sFORCE_FILESYSTEM=1)
    set_property(TARGET chibi-author APPEND PROPERTY LINK_DEPENDS
        "${CHIBI_SDK_ROOT}/src/host/web/author_data.js"
        "${CHIBI_SDK_ROOT}/src/host/web/browser_messages.js"
        "${CHIBI_SDK_ROOT}/src/host/web/browser_compiler.js"
        "${CHIBI_SDK_ROOT}/src/host/web/project_migration.js")
    install(FILES "$<TARGET_FILE:chibi-author>" "$<TARGET_FILE_DIR:chibi-author>/chibi-author.wasm"
        DESTINATION . COMPONENT web_author EXCLUDE_FROM_ALL)
    install(FILES "${CHIBI_SDK_ROOT}/src/host/web/author.html"
        DESTINATION . RENAME index.html COMPONENT web_author EXCLUDE_FROM_ALL)
endif()
endif()

add_library(chibi_ui_dom STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/ui_dom.c"
    "${CHIBI_SDK_ROOT}/src/authoring/ui_dom_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/ui_dom_binding.c"
)
add_library(Chibi::ui_dom ALIAS chibi_ui_dom)
set_target_properties(chibi_ui_dom PROPERTIES EXPORT_NAME ui_dom)
chibi_sdk_headers(chibi_ui_dom PUBLIC authoring/ui_dom.h authoring/ui_dom_definition.h)
target_link_libraries(chibi_ui_dom PUBLIC chibi_ui_dom_declarations PRIVATE chibi_ui_host chibi_support chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_ui_dom)

add_library(chibi_authoring_platform STATIC
    "${CHIBI_SDK_ROOT}/src/host/authoring_platform.c"
    "${CHIBI_SDK_ROOT}/src/host/authoring_platform_jobs.c"
    "${CHIBI_SDK_ROOT}/src/host/authoring_platform_native.c"
    "${CHIBI_SDK_ROOT}/src/host/authoring_platform_binding.c"
)
add_library(Chibi::authoring_platform ALIAS chibi_authoring_platform)
set_target_properties(chibi_authoring_platform PROPERTIES EXPORT_NAME authoring_platform)
chibi_sdk_headers(chibi_authoring_platform PUBLIC host/authoring_platform.h)
target_link_libraries(chibi_authoring_platform PUBLIC chibi_authoring_platform_declarations PRIVATE chibi_workspace_scaffold chibi_browser_messages chibi_workspace_documents chibi_project_document chibi_platform chibi_asset_text chibi_asset_json chibi_support chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_authoring_platform)

foreach(header authoring_platform_internal.h)
    configure_file("${CHIBI_SDK_ROOT}/src/host/${header}" "${CMAKE_CURRENT_BINARY_DIR}/private/authoring_platform/host/${header}" COPYONLY)
endforeach()
target_include_directories(chibi_authoring_platform PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/authoring_platform")

add_library(chibi_workspace_runtime STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_runtime.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_runtime_model.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_runtime_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_snapshot.c"
)
add_library(Chibi::workspace_runtime ALIAS chibi_workspace_runtime)
set_target_properties(chibi_workspace_runtime PROPERTIES EXPORT_NAME workspace_runtime)
chibi_sdk_headers(chibi_workspace_runtime PUBLIC authoring/workspace_runtime.h authoring/workspace_runtime_model.h authoring/workspace_runtime_definition.h authoring/workspace_snapshot.h authoring/workspace_runtime_evidence.h)
target_link_libraries(chibi_workspace_runtime PUBLIC chibi_runtime_properties chibi_workspace_documents chibi_workspace_runtime_declarations chibi_renderer chibi_player PRIVATE chibi_visual_catalog chibi_component_presentation chibi_workspace_presentation chibi_scene_document chibi_value_json chibi_authoring_platform chibi_browser_compiler chibi_project_preparation chibi_project_document chibi_renderer chibi_platform chibi_native_types chibi_support chibi_execution)
configure_file("${CHIBI_SDK_ROOT}/src/authoring/workspace_runtime_model_internal.h" "${CMAKE_CURRENT_BINARY_DIR}/private/workspace_runtime/authoring/workspace_runtime_model_internal.h" COPYONLY)
target_include_directories(chibi_workspace_runtime PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/workspace_runtime")
list(APPEND CHIBI_SDK_TARGETS chibi_workspace_runtime)

add_library(chibi_workspace_simulation STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_simulation.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_simulation_native.c")
add_library(Chibi::workspace_simulation ALIAS chibi_workspace_simulation)
set_target_properties(chibi_workspace_simulation PROPERTIES EXPORT_NAME workspace_simulation)
chibi_sdk_headers(chibi_workspace_simulation PUBLIC authoring/workspace_simulation.h)
target_link_libraries(chibi_workspace_simulation PUBLIC chibi_workspace_simulation_declarations chibi_workspace_runtime chibi_input_runtime
    PRIVATE chibi_ui_host chibi_renderer chibi_material_runtime chibi_asset_json chibi_rooted_files chibi_utf8
    chibi_declared_components chibi_native_types chibi_ecs chibi_types chibi_support chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_workspace_simulation)
target_link_libraries(chibi_authoring_host PRIVATE chibi_workspace_simulation)

target_sources(chibi_visual_previews PRIVATE "${CHIBI_SDK_ROOT}/src/authoring/visual_model_preview.c" "${CHIBI_SDK_ROOT}/src/authoring/visual_catalog.c")
target_link_libraries(chibi_visual_previews PUBLIC chibi_visual_model PRIVATE chibi_rooted_files)
target_link_libraries(chibi_authoring_host PRIVATE chibi_workspace_runtime chibi_authoring_platform chibi_ui_dom chibi_observations chibi_document_graph chibi_visual_model chibi_workspace_debug)

add_library(chibi_resource_previews STATIC "${CHIBI_SDK_ROOT}/src/authoring/resource_previews.c" "${CHIBI_SDK_ROOT}/src/authoring/resource_previews_native.c")
add_library(Chibi::resource_previews ALIAS chibi_resource_previews)
set_target_properties(chibi_resource_previews PROPERTIES EXPORT_NAME resource_previews)
chibi_sdk_headers(chibi_resource_previews PUBLIC authoring/resource_previews.h)
target_link_libraries(chibi_resource_previews PUBLIC chibi_resource_previews_declarations chibi_workspace_documents chibi_workspace_resources chibi_image_catalog chibi_graph_model PRIVATE chibi_visual_previews chibi_image_preparation chibi_renderer chibi_platform chibi_graph_authoring chibi_support chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_resource_previews)
target_link_libraries(chibi_authoring_host PRIVATE chibi_resource_previews chibi_browser_messages chibi_image_preparation chibi_project_semantics)
if(TARGET chibi-author)
target_link_libraries(chibi-author PRIVATE chibi_workspace_simulation chibi_host_native_default chibi_graph_catalog chibi_ui_dom_declarations chibi_workspace_runtime_declarations chibi_authoring_platform_declarations chibi_browser_messages chibi_resource_previews chibi_ui_dom chibi_authoring_platform chibi_workspace_runtime)
endif()

target_link_libraries(chibi_authoring_host PRIVATE chibi_workspace_info chibi_workspace_imports)

if(TARGET chibi_workspace_imports)
    target_compile_definitions(chibi_authoring_host PRIVATE CHIBI_HAS_WORKSPACE_IMPORTS)
endif()
target_sources(chibi_workspace_runtime PRIVATE "${CHIBI_SDK_ROOT}/src/authoring/workspace_runtime_evidence.c")
target_link_libraries(chibi_workspace_runtime PRIVATE chibi_visual_model chibi_image_preparation chibi_material_authoring chibi_asset_json chibi_rooted_files)

if(TARGET chibi-author)
    target_sources(chibi-author PRIVATE "${CHIBI_SDK_ROOT}/src/host/ui_replay_runtime.c")
    configure_file("${CHIBI_SDK_ROOT}/src/host/ui_replay_runtime.h" "${CMAKE_CURRENT_BINARY_DIR}/private/player/host/ui_replay_runtime.h" COPYONLY)
    target_link_libraries(chibi-author PRIVATE chibi_ui_replay chibi_asset_json)
endif()

target_link_libraries(chibi_resource_previews PRIVATE chibi_builtin_fonts chibi_asset_text chibi_rooted_files chibi_resource_preview_data chibi_ui_source_provider chibi_document_semantics chibi_workspace_runtime chibi_workspace_info)

target_link_libraries(chibi_authoring_platform PRIVATE chibi_rooted_files chibi_renderer)

add_library(chibi_ui_source_preview STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/ui_source_preview.c"
    "${CHIBI_SDK_ROOT}/src/authoring/ui_source_preview_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/ui_source_preview_binding.c")
add_library(Chibi::ui_source_preview ALIAS chibi_ui_source_preview)
set_target_properties(chibi_ui_source_preview PROPERTIES EXPORT_NAME ui_source_preview)
target_link_libraries(chibi_ui_source_preview PUBLIC chibi_ui_source_preview_declarations
    PRIVATE chibi_ui_source_provider chibi_workspace_runtime chibi_ui_dom chibi_renderer chibi_platform chibi_declared_components chibi_ecs chibi_support chibi_execution)
list(APPEND CHIBI_SDK_TARGETS chibi_ui_source_preview)
target_link_libraries(chibi_authoring_host PRIVATE chibi_ui_source_preview)
if(TARGET chibi-author)
    target_link_libraries(chibi-author PRIVATE chibi_ui_source_preview_declarations)
endif()

if(WIN32)
    target_sources(chibi_authoring_platform PRIVATE "${CHIBI_SDK_ROOT}/src/host/authoring_platform_jobs_windows.c")
endif()

target_link_libraries(chibi_authoring_host PRIVATE chibi_workspace_file_history chibi_project_compilation chibi_native_prepared)

add_library(chibi_workspace_tools STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_saved.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_css.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_tools_native.c"
    "${CHIBI_SDK_ROOT}/src/authoring/workspace_tools_binding.c")
add_library(Chibi::workspace_tools ALIAS chibi_workspace_tools)
set_target_properties(chibi_workspace_tools PROPERTIES EXPORT_NAME workspace_tools)
target_link_libraries(chibi_workspace_tools PUBLIC chibi_workspace_tools_declarations
    PRIVATE chibi_workspace_runtime chibi_save_runtime chibi_workspace_documents chibi_ui_source_provider
    chibi_ui_document chibi_ui_style chibi_asset_text chibi_support chibi_execution chibi_types)
list(APPEND CHIBI_SDK_TARGETS chibi_workspace_tools)
target_link_libraries(chibi_authoring_host PRIVATE chibi_workspace_tools chibi_ui_source_provider)
if(TARGET chibi-author)
    target_link_libraries(chibi-author PRIVATE chibi_workspace_tools)
endif()
