cmake_minimum_required(VERSION 3.25)
set(CHIBI_SDK_VERSION 0.1.0)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(ChibiSDK VERSION ${CHIBI_SDK_VERSION} LANGUAGES C)
    if(WIN32)
        enable_language(RC)
        set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)
    endif()
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    # Emscripten's SDL3 port label is independent of project source warnings.
    add_link_options(-Wno-experimental)
endif()

# This package compiles against its exported headers even inside the product
# build. Inherited whole-repository includes must not hide a dependency leak.
set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES "")
get_filename_component(CHIBI_SDK_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CHIBI_SDK_ROOT}/src/assets/project.h")
file(STRINGS "${CHIBI_SDK_ROOT}/src/assets/project.h" project_version_line REGEX "^#define CHIBI_PROJECT_VERSION [0-9]+$")
string(REGEX REPLACE ".* ([0-9]+)$" "\\1" CHIBI_PROJECT_FORMAT "${project_version_line}")
if(NOT CHIBI_PROJECT_FORMAT MATCHES "^[0-9]+$")
    message(FATAL_ERROR "Missing owning project format version")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/chibi_engine_metadata.json" "{\"project_version\": ${CHIBI_PROJECT_FORMAT}}\n")
set(CHIBI_SDK_HEADER_ROOT "${CMAKE_CURRENT_BINARY_DIR}/public")
option(CHIBI_SDK_BUILD_COMPILER "Build language, compiler and source authoring packages" ON)
option(CHIBI_SDK_BUILD_NATIVE "Build built-in component and entity packages" OFF)
option(CHIBI_SDK_BUILD_DESKTOP "Build desktop platform, rendering and UI packages" OFF)
option(CHIBI_WEB_THREADS "Build the web SDK with pthread workers" OFF)
option(CHIBI_WEB_PLAYER "Build the browser player that chibi export --web ships" OFF)
set(CHIBI_WEB_WORKERS 4 CACHE STRING "Total worker budget for the threaded web SDK")
if(CHIBI_WEB_THREADS)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        message(FATAL_ERROR "CHIBI_WEB_THREADS requires Emscripten")
    endif()
    if(NOT CHIBI_WEB_WORKERS MATCHES "^[0-9]+$" OR
       CHIBI_WEB_WORKERS LESS 2 OR CHIBI_WEB_WORKERS GREATER 8)
        message(FATAL_ERROR "CHIBI_WEB_WORKERS must be between 2 and 8")
    endif()
    math(EXPR CHIBI_WEB_POOL_SIZE "${CHIBI_WEB_WORKERS} - 1")
    add_compile_options(-pthread -msimd128)
    add_compile_definitions(CHIBI_WEB_WORKERS=${CHIBI_WEB_WORKERS})
endif()
if(CHIBI_SDK_BUILD_DESKTOP AND NOT CHIBI_SDK_BUILD_NATIVE)
    message(FATAL_ERROR "Desktop runtime requires CHIBI_SDK_BUILD_NATIVE")
endif()
set(CHIBI_SDK_NEEDS_THREADS OFF)

# Windows executables read and write file names as UTF-8, as elsewhere.
# Installed consumers add share/chibi/windows/chibi.rc themselves.
function(chibi_windows_manifest target)
    if(WIN32)
        set(windows "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/host/windows")
        target_sources(${target} PRIVATE "${windows}/chibi.rc")
        # The resource compiler reports no dependencies, so name the manifest.
        set_source_files_properties("${windows}/chibi.rc" TARGET_DIRECTORY ${target}
            PROPERTIES OBJECT_DEPENDS "${windows}/chibi.manifest")
    endif()
endfunction()
if(WIN32)
    install(FILES "${CHIBI_SDK_ROOT}/src/host/windows/chibi.rc"
        "${CHIBI_SDK_ROOT}/src/host/windows/chibi.manifest" DESTINATION share/chibi/windows)
endif()

# Both source and installed consumers see only headers owned by their declared
# dependency closure. Keep include spellings stable while isolating search roots.
function(chibi_sdk_headers target visibility)
    get_target_property(owner ${target} EXPORT_NAME)
    set(root "${CHIBI_SDK_HEADER_ROOT}/${owner}")
    # Removed or moved headers must disappear from reused build trees too.
    file(GLOB_RECURSE previous LIST_DIRECTORIES FALSE "${root}/*")
    foreach(path IN LISTS previous)
        file(RELATIVE_PATH header "${root}" "${path}")
        if(NOT header IN_LIST ARGN)
            file(REMOVE "${path}")
        endif()
    endforeach()
    foreach(header IN LISTS ARGN)
        configure_file("${CHIBI_SDK_ROOT}/src/${header}" "${root}/${header}" COPYONLY)
        get_filename_component(directory "${header}" DIRECTORY)
        install(FILES "${root}/${header}" DESTINATION "include/chibi/${owner}/${directory}")
    endforeach()
    target_include_directories(${target} ${visibility}
        $<BUILD_INTERFACE:${root}> $<INSTALL_INTERFACE:include/chibi/${owner}>)
endfunction()

add_library(chibi_support STATIC
    "${CHIBI_SDK_ROOT}/src/support/arena.c"
    "${CHIBI_SDK_ROOT}/src/support/file_io.c"
    "${CHIBI_SDK_ROOT}/src/support/xalloc.c"
    "${CHIBI_SDK_ROOT}/src/support/rcss_map.c")
add_library(Chibi::support ALIAS chibi_support)
set_target_properties(chibi_support PROPERTIES EXPORT_NAME support)
target_compile_features(chibi_support PUBLIC c_std_23)
if(CHIBI_WEB_THREADS)
    target_compile_options(chibi_support INTERFACE -pthread -msimd128)
    target_compile_definitions(chibi_support INTERFACE CHIBI_WEB_WORKERS=${CHIBI_WEB_WORKERS})
    target_link_options(chibi_support INTERFACE -pthread -msimd128
        "-sPTHREAD_POOL_SIZE=${CHIBI_WEB_POOL_SIZE}" -sPTHREAD_POOL_SIZE_STRICT=2
        -sALLOW_MEMORY_GROWTH=1)
endif()
set(support_headers support/arena.h support/array.h support/xalloc.h support/rcss_map.h
    support/enum.h support/cos_sin.h support/file_io.h support/math.h)
if(WIN32)
    # C sources that use the SDK get the POSIX calls the Microsoft runtime lacks.
    target_sources(chibi_support PRIVATE "${CHIBI_SDK_ROOT}/src/support/win32/posix.c")
    list(APPEND support_headers support/win32/posix.h support/win32/unistd.h
        support/win32/dirent.h support/win32/strings.h support/win32/sys/file.h)
    set(posix "support/support/win32")
    target_include_directories(chibi_support PUBLIC
        "$<$<COMPILE_LANGUAGE:C>:$<BUILD_INTERFACE:${CHIBI_SDK_HEADER_ROOT}/${posix}>>"
        "$<$<COMPILE_LANGUAGE:C>:$<INSTALL_INTERFACE:include/chibi/${posix}>>")
    target_compile_options(chibi_support PUBLIC
        "$<$<COMPILE_LANGUAGE:C>:-include$<BUILD_INTERFACE:${CHIBI_SDK_HEADER_ROOT}/${posix}/posix.h>$<INSTALL_INTERFACE:$<INSTALL_PREFIX>/include/chibi/${posix}/posix.h>>")
endif()
chibi_sdk_headers(chibi_support PUBLIC ${support_headers})
if(UNIX)
    target_link_libraries(chibi_support PUBLIC m)
endif()

add_library(chibi_types STATIC
    "${CHIBI_SDK_ROOT}/src/types/type.c"
    "${CHIBI_SDK_ROOT}/src/types/catalog.c"
    "${CHIBI_SDK_ROOT}/src/types/array.c"
    "${CHIBI_SDK_ROOT}/src/types/trace.c"
    "${CHIBI_SDK_ROOT}/src/types/index.c"
    "${CHIBI_SDK_ROOT}/src/types/value_format.c"
    "${CHIBI_SDK_ROOT}/src/types/value_wire.c")
add_library(Chibi::types ALIAS chibi_types)
set_target_properties(chibi_types PROPERTIES EXPORT_NAME types)
chibi_sdk_headers(chibi_types PUBLIC types/value.h types/type.h types/catalog.h types/array.h types/trace.h types/annotate.h types/state_machine.h types/value_format.h types/scalar.h types/value_wire.h)
target_link_libraries(chibi_types PUBLIC chibi_support)
if(UNIX)
    target_link_libraries(chibi_types PRIVATE m)
endif()

configure_file("${CHIBI_SDK_ROOT}/src/support/float_text.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/asset_values/support/float_text.h" COPYONLY)
add_library(chibi_asset_values STATIC "${CHIBI_SDK_ROOT}/src/support/asset_value.c")
add_library(Chibi::asset_values ALIAS chibi_asset_values)
set_target_properties(chibi_asset_values PROPERTIES EXPORT_NAME asset_values)
chibi_sdk_headers(chibi_asset_values PUBLIC support/asset_value.h support/asset_format.h)
target_include_directories(chibi_asset_values PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/asset_values")
target_link_libraries(chibi_asset_values PUBLIC chibi_support)

add_library(chibi_value_document STATIC "${CHIBI_SDK_ROOT}/src/types/value_document.c"
    "${CHIBI_SDK_ROOT}/src/types/value_document_read.c")
add_library(Chibi::value_document ALIAS chibi_value_document)
set_target_properties(chibi_value_document PROPERTIES EXPORT_NAME value_document)
chibi_sdk_headers(chibi_value_document PUBLIC types/value_document.h)
target_link_libraries(chibi_value_document PUBLIC chibi_types chibi_asset_values)

add_library(chibi_capabilities STATIC "${CHIBI_SDK_ROOT}/src/capabilities/catalog.c"
    "${CHIBI_SDK_ROOT}/src/capabilities/definition.c")
add_library(Chibi::capabilities ALIAS chibi_capabilities)
set_target_properties(chibi_capabilities PROPERTIES EXPORT_NAME capabilities)
chibi_sdk_headers(chibi_capabilities PUBLIC capabilities/catalog.h capabilities/definition.h)
target_link_libraries(chibi_capabilities PUBLIC chibi_types)

add_library(chibi_execution_contract INTERFACE)
add_library(Chibi::execution_contract ALIAS chibi_execution_contract)
set_target_properties(chibi_execution_contract PROPERTIES EXPORT_NAME execution_contract)
chibi_sdk_headers(chibi_execution_contract INTERFACE execution/host.h execution/status.h
    execution/service.h execution/log.h execution/instruction.h execution/state.h execution/function.h
    execution/diagnostic.h execution/backend.h execution/component.h execution/scalar.h
    execution/request.h execution/source_debug.h execution/value_kind.h execution/arguments.h execution/random.h)
target_link_libraries(chibi_execution_contract INTERFACE chibi_types)

add_library(chibi_native_capabilities STATIC "${CHIBI_SDK_ROOT}/src/runtime/native_capabilities.c"
    "${CHIBI_SDK_ROOT}/src/runtime/native_definitions.c"
    "${CHIBI_SDK_ROOT}/src/runtime/type_definitions.c")
add_library(Chibi::native_capabilities ALIAS chibi_native_capabilities)
set_target_properties(chibi_native_capabilities PROPERTIES EXPORT_NAME native_capabilities)
chibi_sdk_headers(chibi_native_capabilities PUBLIC runtime/native_capabilities.h runtime/native_definitions.h
    runtime/type_definitions.h)
target_link_libraries(chibi_native_capabilities PUBLIC chibi_capabilities chibi_execution_contract)

add_library(chibi_ecs STATIC "${CHIBI_SDK_ROOT}/src/ecs/ecs.c"
    "${CHIBI_SDK_ROOT}/src/ecs/relationship.c" "${CHIBI_SDK_ROOT}/src/ecs/hierarchy.c"
    "${CHIBI_SDK_ROOT}/src/ecs/query.c"
    "${CHIBI_SDK_ROOT}/src/ecs/component_catalog.c")
add_library(Chibi::ecs ALIAS chibi_ecs)
set_target_properties(chibi_ecs PROPERTIES EXPORT_NAME ecs)
chibi_sdk_headers(chibi_ecs PUBLIC ecs/ecs.h ecs/execution.h ecs/component_catalog.h)
configure_file("${CHIBI_SDK_ROOT}/src/ecs/ecs_internal.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/ecs/ecs/ecs_internal.h" COPYONLY)
target_include_directories(chibi_ecs PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/ecs")
target_link_libraries(chibi_ecs PUBLIC chibi_types)

add_library(chibi_events STATIC "${CHIBI_SDK_ROOT}/src/runtime/events.c")
add_library(Chibi::events ALIAS chibi_events)
set_target_properties(chibi_events PROPERTIES EXPORT_NAME events)
chibi_sdk_headers(chibi_events PUBLIC runtime/events.h)
target_link_libraries(chibi_events PUBLIC chibi_types)

add_library(chibi_tasks INTERFACE)
add_library(Chibi::tasks ALIAS chibi_tasks)
set_target_properties(chibi_tasks PROPERTIES EXPORT_NAME tasks)
chibi_sdk_headers(chibi_tasks INTERFACE runtime/tasks.h)

add_library(chibi_input_action INTERFACE)
add_library(Chibi::input_action ALIAS chibi_input_action)
set_target_properties(chibi_input_action PROPERTIES EXPORT_NAME input_action)
chibi_sdk_headers(chibi_input_action INTERFACE runtime/input_action.h)

# The language requires only Tree-sitter's C runtime, independently of the
# HTML/CSS grammars and UI dependencies in the Studio build.
include(FetchContent)
include(execution.cmake)
add_library(chibi_core_capabilities STATIC "${CHIBI_SDK_ROOT}/src/runtime/core_definition.c")
add_library(Chibi::core_capabilities ALIAS chibi_core_capabilities)
set_target_properties(chibi_core_capabilities PROPERTIES EXPORT_NAME core_capabilities)
chibi_sdk_headers(chibi_core_capabilities PUBLIC runtime/core_definition.h)
target_link_libraries(chibi_core_capabilities PUBLIC chibi_native_capabilities PRIVATE chibi_execution)
add_library(chibi_array_capabilities STATIC "${CHIBI_SDK_ROOT}/src/runtime/arrays.c"
    "${CHIBI_SDK_ROOT}/src/runtime/arrays_definition.c")
add_library(Chibi::array_capabilities ALIAS chibi_array_capabilities)
set_target_properties(chibi_array_capabilities PROPERTIES EXPORT_NAME array_capabilities)
chibi_sdk_headers(chibi_array_capabilities PUBLIC runtime/arrays.h runtime/arrays_definition.h)
target_link_libraries(chibi_array_capabilities PUBLIC chibi_native_capabilities)
add_library(chibi_field_capabilities STATIC "${CHIBI_SDK_ROOT}/src/runtime/fields/native.c"
    "${CHIBI_SDK_ROOT}/src/runtime/fields/definition.c")
add_library(Chibi::field_capabilities ALIAS chibi_field_capabilities)
set_target_properties(chibi_field_capabilities PROPERTIES EXPORT_NAME field_capabilities)
chibi_sdk_headers(chibi_field_capabilities PUBLIC runtime/fields/native.h runtime/fields/definition.h)
target_link_libraries(chibi_field_capabilities PUBLIC chibi_native_capabilities)
add_library(chibi_state_capabilities STATIC "${CHIBI_SDK_ROOT}/src/runtime/state_definition.c")
add_library(Chibi::state_capabilities ALIAS chibi_state_capabilities)
set_target_properties(chibi_state_capabilities PROPERTIES EXPORT_NAME state_capabilities)
chibi_sdk_headers(chibi_state_capabilities PUBLIC runtime/state_definition.h)
target_link_libraries(chibi_state_capabilities PUBLIC chibi_native_capabilities)
add_library(chibi_selection_capabilities STATIC "${CHIBI_SDK_ROOT}/src/runtime/selection.c"
    "${CHIBI_SDK_ROOT}/src/runtime/selection_definition.c")
add_library(Chibi::selection_capabilities ALIAS chibi_selection_capabilities)
set_target_properties(chibi_selection_capabilities PROPERTIES EXPORT_NAME selection_capabilities)
chibi_sdk_headers(chibi_selection_capabilities PUBLIC runtime/selection.h runtime/selection_definition.h)
target_link_libraries(chibi_selection_capabilities PUBLIC chibi_native_capabilities)
add_library(chibi_set_capabilities STATIC "${CHIBI_SDK_ROOT}/src/runtime/sets.c"
    "${CHIBI_SDK_ROOT}/src/runtime/sets_definition.c")
add_library(Chibi::set_capabilities ALIAS chibi_set_capabilities)
set_target_properties(chibi_set_capabilities PROPERTIES EXPORT_NAME set_capabilities)
chibi_sdk_headers(chibi_set_capabilities PUBLIC runtime/sets.h runtime/sets_definition.h)
target_link_libraries(chibi_set_capabilities PUBLIC chibi_native_capabilities)
add_library(chibi_comparison_capabilities STATIC "${CHIBI_SDK_ROOT}/src/runtime/comparisons.c"
    "${CHIBI_SDK_ROOT}/src/runtime/comparison_definition.c")
add_library(Chibi::comparison_capabilities ALIAS chibi_comparison_capabilities)
set_target_properties(chibi_comparison_capabilities PROPERTIES EXPORT_NAME comparison_capabilities)
chibi_sdk_headers(chibi_comparison_capabilities PUBLIC runtime/comparisons.h runtime/comparison_definition.h)
target_link_libraries(chibi_comparison_capabilities PUBLIC chibi_native_capabilities)
add_library(chibi_conversion_capabilities STATIC "${CHIBI_SDK_ROOT}/src/runtime/conversions.c"
    "${CHIBI_SDK_ROOT}/src/runtime/conversion_definition.c")
add_library(Chibi::conversion_capabilities ALIAS chibi_conversion_capabilities)
set_target_properties(chibi_conversion_capabilities PROPERTIES EXPORT_NAME conversion_capabilities)
chibi_sdk_headers(chibi_conversion_capabilities PUBLIC runtime/conversions.h runtime/conversion_definition.h)
target_link_libraries(chibi_conversion_capabilities PUBLIC chibi_native_capabilities)
add_library(chibi_text_capabilities STATIC "${CHIBI_SDK_ROOT}/src/runtime/text_format.c"
    "${CHIBI_SDK_ROOT}/src/runtime/text_definition.c")
add_library(Chibi::text_capabilities ALIAS chibi_text_capabilities)
set_target_properties(chibi_text_capabilities PROPERTIES EXPORT_NAME text_capabilities)
chibi_sdk_headers(chibi_text_capabilities PUBLIC runtime/text_format.h runtime/text_definition.h)
target_link_libraries(chibi_text_capabilities PUBLIC chibi_native_capabilities)
add_library(chibi_fmt STATIC "${CHIBI_SDK_ROOT}/third_party/fmt/fmt.c")
set_target_properties(chibi_fmt PROPERTIES EXPORT_NAME fmt)
target_include_directories(chibi_fmt PRIVATE "${CHIBI_SDK_ROOT}/third_party/fmt")
target_compile_options(chibi_fmt PRIVATE -w)
target_include_directories(chibi_text_capabilities PRIVATE "${CHIBI_SDK_ROOT}/third_party/fmt")
target_link_libraries(chibi_text_capabilities PRIVATE chibi_fmt)
add_library(chibi_host_native_catalog STATIC "${CHIBI_SDK_ROOT}/src/host/native_catalog.c")
add_library(Chibi::host_native_catalog ALIAS chibi_host_native_catalog)
set_target_properties(chibi_host_native_catalog PROPERTIES EXPORT_NAME host_native_catalog)
chibi_sdk_headers(chibi_host_native_catalog PUBLIC host/native_catalog.h)
target_link_libraries(chibi_host_native_catalog PUBLIC chibi_native_capabilities PRIVATE chibi_events)
add_library(chibi_behavior_runtime STATIC
    "${CHIBI_SDK_ROOT}/src/runtime/event_catalog.c"
    "${CHIBI_SDK_ROOT}/src/runtime/behaviors/source.c"
    "${CHIBI_SDK_ROOT}/src/runtime/behaviors/source_profile.c"
    "${CHIBI_SDK_ROOT}/src/runtime/behaviors/source_publication.c"
    "${CHIBI_SDK_ROOT}/src/runtime/behaviors/package.c"
    "${CHIBI_SDK_ROOT}/src/runtime/behaviors/module_set.c")
add_library(Chibi::behavior_runtime ALIAS chibi_behavior_runtime)
set_target_properties(chibi_behavior_runtime PROPERTIES EXPORT_NAME behavior_runtime)
chibi_sdk_headers(chibi_behavior_runtime PUBLIC runtime/behaviors/source.h runtime/behaviors/package.h
    runtime/behaviors/module_set.h runtime/event_catalog.h)
target_link_libraries(chibi_behavior_runtime PUBLIC chibi_execution chibi_events)
add_library(chibi_native_prepared STATIC "${CHIBI_SDK_ROOT}/src/runtime/native_prepared.c")
add_library(Chibi::native_prepared ALIAS chibi_native_prepared)
set_target_properties(chibi_native_prepared PROPERTIES EXPORT_NAME native_prepared)
chibi_sdk_headers(chibi_native_prepared PUBLIC runtime/native_prepared.h)
target_link_libraries(chibi_native_prepared PUBLIC chibi_native_capabilities chibi_behavior_runtime PRIVATE chibi_input_action)
add_library(chibi_scene_runtime STATIC "${CHIBI_SDK_ROOT}/src/runtime/scenes/scene.c"
    "${CHIBI_SDK_ROOT}/src/runtime/scenes/prototypes.c"
    "${CHIBI_SDK_ROOT}/src/runtime/scenes/prepared.c")
add_library(Chibi::scene_runtime ALIAS chibi_scene_runtime)
set_target_properties(chibi_scene_runtime PROPERTIES EXPORT_NAME scene_runtime)
chibi_sdk_headers(chibi_scene_runtime PUBLIC runtime/scenes/scene.h runtime/scenes/physics_settings.h
    runtime/scenes/prototypes.h runtime/scenes/prepared.h)
target_link_libraries(chibi_scene_runtime PUBLIC chibi_behavior_runtime chibi_ecs chibi_types chibi_tasks)
add_library(chibi_scheduling STATIC "${CHIBI_SDK_ROOT}/src/runtime/systems/scheduler.c")
add_library(Chibi::scheduling ALIAS chibi_scheduling)
set_target_properties(chibi_scheduling PROPERTIES EXPORT_NAME scheduling)
chibi_sdk_headers(chibi_scheduling PUBLIC runtime/systems/scheduler.h)
target_link_libraries(chibi_scheduling PUBLIC chibi_ecs chibi_execution_contract chibi_tasks)
add_library(chibi_fields STATIC "${CHIBI_SDK_ROOT}/src/runtime/fields/program.c"
    "${CHIBI_SDK_ROOT}/src/runtime/fields/prepared.c")
add_library(Chibi::fields ALIAS chibi_fields)
set_target_properties(chibi_fields PROPERTIES EXPORT_NAME fields)
chibi_sdk_headers(chibi_fields PUBLIC runtime/fields/ir.h runtime/fields/program.h runtime/fields/prepared.h)
target_link_libraries(chibi_fields PUBLIC chibi_types chibi_tasks)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(chibi_fields PRIVATE -ffp-contract=off)
endif()
add_library(chibi_field_catalog STATIC "${CHIBI_SDK_ROOT}/src/runtime/material_field.c"
    "${CHIBI_SDK_ROOT}/src/runtime/material_field_prepared.c")
add_library(Chibi::field_catalog ALIAS chibi_field_catalog)
set_target_properties(chibi_field_catalog PROPERTIES EXPORT_NAME field_catalog)
chibi_sdk_headers(chibi_field_catalog PUBLIC runtime/material_field.h runtime/material_field_prepared.h)
target_link_libraries(chibi_field_catalog PUBLIC chibi_fields)
set(CHIBI_SDK_TARGETS chibi_support chibi_types chibi_execution_contract chibi_ecs
    chibi_execution chibi_core_capabilities chibi_array_capabilities chibi_field_capabilities chibi_state_capabilities chibi_selection_capabilities chibi_behavior_runtime chibi_events chibi_asset_values chibi_value_document chibi_capabilities chibi_native_capabilities chibi_scene_runtime chibi_scheduling chibi_fields chibi_field_catalog chibi_tasks chibi_input_action)
list(APPEND CHIBI_SDK_TARGETS chibi_set_capabilities chibi_host_native_catalog chibi_native_prepared
    chibi_comparison_capabilities chibi_conversion_capabilities chibi_text_capabilities chibi_fmt)
add_library(chibi_ui_documents STATIC "${CHIBI_SDK_ROOT}/src/runtime/ui/documents.c"
    "${CHIBI_SDK_ROOT}/src/runtime/ui/font.c")
add_library(Chibi::ui_documents ALIAS chibi_ui_documents)
set_target_properties(chibi_ui_documents PROPERTIES EXPORT_NAME ui_documents)
chibi_sdk_headers(chibi_ui_documents PUBLIC runtime/ui/documents.h runtime/ui/font.h)
target_link_libraries(chibi_ui_documents PRIVATE chibi_types chibi_support)
list(APPEND CHIBI_SDK_TARGETS chibi_ui_documents)
add_library(chibi_ui_document_values STATIC "${CHIBI_SDK_ROOT}/src/ui/document.c")
add_library(Chibi::ui_document_values ALIAS chibi_ui_document_values)
set_target_properties(chibi_ui_document_values PROPERTIES EXPORT_NAME ui_document_values)
chibi_sdk_headers(chibi_ui_document_values PUBLIC ui/document.h)
target_link_libraries(chibi_ui_document_values PUBLIC chibi_support)
add_library(chibi_text_search STATIC "${CHIBI_SDK_ROOT}/src/support/text_search.c"
    "${CHIBI_SDK_ROOT}/src/support/text_rank.c")
add_library(Chibi::text_search ALIAS chibi_text_search)
set_target_properties(chibi_text_search PROPERTIES EXPORT_NAME text_search)
chibi_sdk_headers(chibi_text_search PUBLIC support/text_search.h support/text_rank.h)
target_link_libraries(chibi_text_search PUBLIC chibi_support)
list(APPEND CHIBI_SDK_TARGETS chibi_ui_document_values chibi_text_search)

if(CHIBI_SDK_BUILD_COMPILER OR CHIBI_SDK_BUILD_DESKTOP)
    include(fonts.cmake)
endif()
if(CHIBI_SDK_BUILD_COMPILER OR CHIBI_SDK_BUILD_NATIVE)
    add_library(chibi_json STATIC "${CHIBI_SDK_ROOT}/third_party/yyjson/yyjson.c")
    add_library(Chibi::json ALIAS chibi_json)
    set_target_properties(chibi_json PROPERTIES EXPORT_NAME json)
    target_include_directories(chibi_json PRIVATE "${CHIBI_SDK_ROOT}/third_party/yyjson")
    target_compile_options(chibi_json PRIVATE -w)
    configure_file("${CHIBI_SDK_ROOT}/third_party/yyjson/yyjson.h"
        "${CHIBI_SDK_HEADER_ROOT}/json/yyjson.h" COPYONLY)
    target_include_directories(chibi_json PUBLIC $<BUILD_INTERFACE:${CHIBI_SDK_HEADER_ROOT}/json>
        $<INSTALL_INTERFACE:include/chibi/json>)
    install(FILES "${CHIBI_SDK_HEADER_ROOT}/json/yyjson.h" DESTINATION include/chibi/json)
    install(FILES "${CHIBI_SDK_ROOT}/third_party/yyjson/LICENSE"
        DESTINATION share/chibi/licenses/yyjson)
    list(APPEND CHIBI_SDK_TARGETS chibi_json)
endif()
if(CHIBI_SDK_BUILD_COMPILER OR CHIBI_SDK_BUILD_NATIVE)
    include(values.cmake)
endif()
if(CHIBI_SDK_BUILD_NATIVE)
    include(native.cmake)
endif()
include(models.cmake)
include(images.cmake)
if(CHIBI_SDK_BUILD_NATIVE)
    include(visual_catalog.cmake)
endif()
if(CHIBI_SDK_BUILD_DESKTOP)
    include(desktop.cmake)
endif()
if(CHIBI_SDK_BUILD_COMPILER)
    install(PROGRAMS "${CHIBI_SDK_ROOT}/tools/package_sdk.py" "${CHIBI_SDK_ROOT}/tools/package_player.py"
        "${CHIBI_SDK_ROOT}/tools/package_studio.py"
        "${CHIBI_SDK_ROOT}/tools/install_game_runtime.py" DESTINATION share/chibi/tools)
    install(FILES "${CHIBI_SDK_ROOT}/tools/kdl_asset.py" DESTINATION share/chibi/tools)
    add_library(chibi_asset_json STATIC "${CHIBI_SDK_ROOT}/src/support/asset_json.c")
    add_library(Chibi::asset_json ALIAS chibi_asset_json)
    set_target_properties(chibi_asset_json PROPERTIES EXPORT_NAME asset_json)
    chibi_sdk_headers(chibi_asset_json PUBLIC support/asset_json.h)
    target_link_libraries(chibi_asset_json PUBLIC chibi_asset_values chibi_json)
    list(APPEND CHIBI_SDK_TARGETS chibi_asset_json)
    include(asset_text.cmake)
    include(graph.cmake)
    add_library(chibi_studio_capabilities STATIC "${CHIBI_SDK_ROOT}/src/authoring/studio_native.c"
        "${CHIBI_SDK_ROOT}/src/authoring/studio_definition.c")
    add_library(Chibi::studio_capabilities ALIAS chibi_studio_capabilities)
    set_target_properties(chibi_studio_capabilities PROPERTIES EXPORT_NAME studio_capabilities)
    chibi_sdk_headers(chibi_studio_capabilities PUBLIC authoring/studio_schema.h authoring/studio_definition.h)
    target_link_libraries(chibi_studio_capabilities PUBLIC chibi_native_capabilities PRIVATE chibi_execution)
    list(APPEND CHIBI_SDK_TARGETS chibi_studio_capabilities)
    add_library(chibi_studio_graph STATIC "${CHIBI_SDK_ROOT}/src/graph/nodes/studio_nodes.c")
    add_library(Chibi::studio_graph ALIAS chibi_studio_graph)
    set_target_properties(chibi_studio_graph PROPERTIES EXPORT_NAME studio_graph)
    chibi_sdk_headers(chibi_studio_graph PUBLIC graph/nodes/studio_nodes.h)
    configure_file("${CHIBI_SDK_ROOT}/src/graph/model/native_definition.h"
        "${CMAKE_CURRENT_BINARY_DIR}/private/studio_graph/graph/model/native_definition.h" COPYONLY)
    target_include_directories(chibi_studio_graph PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/studio_graph")
    target_link_libraries(chibi_studio_graph PUBLIC chibi_graph_compilation chibi_studio_capabilities)
    list(APPEND CHIBI_SDK_TARGETS chibi_studio_graph)
    if(CHIBI_SDK_BUILD_NATIVE)
        add_library(chibi_studio_catalog STATIC "${CHIBI_SDK_ROOT}/src/authoring/studio_catalog.c")
        add_library(Chibi::studio_catalog ALIAS chibi_studio_catalog)
        set_target_properties(chibi_studio_catalog PROPERTIES EXPORT_NAME studio_catalog)
        chibi_sdk_headers(chibi_studio_catalog PUBLIC authoring/studio_catalog.h)
        target_link_libraries(chibi_studio_catalog PUBLIC chibi_native_capabilities
            PRIVATE chibi_studio_capabilities chibi_host_native_default)
        list(APPEND CHIBI_SDK_TARGETS chibi_studio_catalog)
    endif()
    set(engine_components "${CMAKE_CURRENT_BINARY_DIR}/generated/authoring/engine_components.c")
    add_custom_command(OUTPUT "${engine_components}"
        COMMAND ${CMAKE_COMMAND} "-DINPUT=${CHIBI_SDK_ROOT}/src/authoring/engine_components.kdl"
            "-DOUTPUT=${engine_components}" "-DSYMBOL=component_presentation_engine_kdl"
            -P "${CHIBI_SDK_ROOT}/cmake/embed_binary.cmake"
        DEPENDS "${CHIBI_SDK_ROOT}/src/authoring/engine_components.kdl"
            "${CHIBI_SDK_ROOT}/cmake/embed_binary.cmake"
            "${CHIBI_SDK_ROOT}/cmake/generated_file.cmake" VERBATIM)
    add_library(chibi_component_presentation STATIC
        "${CHIBI_SDK_ROOT}/src/authoring/component_presentation.c" "${engine_components}")
    add_library(Chibi::component_presentation ALIAS chibi_component_presentation)
    set_target_properties(chibi_component_presentation PROPERTIES EXPORT_NAME component_presentation)
    chibi_sdk_headers(chibi_component_presentation PUBLIC authoring/component_presentation.h)
    target_link_libraries(chibi_component_presentation PUBLIC chibi_ecs PRIVATE chibi_asset_text)
    list(APPEND CHIBI_SDK_TARGETS chibi_component_presentation)
    add_library(chibi_input_map STATIC "${CHIBI_SDK_ROOT}/src/assets/input_map.c")
    add_library(Chibi::input_map ALIAS chibi_input_map)
    set_target_properties(chibi_input_map PROPERTIES EXPORT_NAME input_map)
    chibi_sdk_headers(chibi_input_map PUBLIC assets/input_map.h)
    target_link_libraries(chibi_input_map PUBLIC chibi_input_action
        PRIVATE chibi_types chibi_asset_values chibi_asset_text chibi_support)
    list(APPEND CHIBI_SDK_TARGETS chibi_input_map)
    add_library(chibi_project_document STATIC "${CHIBI_SDK_ROOT}/src/assets/project.c"
        "${CHIBI_SDK_ROOT}/src/assets/project_migrate.c")
    add_library(Chibi::project_document ALIAS chibi_project_document)
    set_target_properties(chibi_project_document PROPERTIES EXPORT_NAME project_document)
    chibi_sdk_headers(chibi_project_document PUBLIC assets/project.h assets/project_migrate.h)
    target_link_libraries(chibi_project_document PRIVATE chibi_input_action chibi_asset_text
        chibi_language_syntax chibi_json chibi_support)
    list(APPEND CHIBI_SDK_TARGETS chibi_project_document)
    add_library(chibi_reference_index STATIC "${CHIBI_SDK_ROOT}/src/authoring/reference_index.c")
    add_library(Chibi::reference_index ALIAS chibi_reference_index)
    set_target_properties(chibi_reference_index PROPERTIES EXPORT_NAME reference_index)
    chibi_sdk_headers(chibi_reference_index PUBLIC authoring/reference_index.h)
    target_link_libraries(chibi_reference_index PRIVATE chibi_support chibi_events)
    list(APPEND CHIBI_SDK_TARGETS chibi_reference_index)
    add_library(chibi_prototype_document STATIC "${CHIBI_SDK_ROOT}/src/assets/prototype.c")
    add_library(Chibi::prototype_document ALIAS chibi_prototype_document)
    set_target_properties(chibi_prototype_document PROPERTIES EXPORT_NAME prototype_document)
    chibi_sdk_headers(chibi_prototype_document PUBLIC assets/prototype.h)
    target_link_libraries(chibi_prototype_document PUBLIC chibi_ecs chibi_asset_values chibi_scene_document
        PRIVATE chibi_asset_text)
    list(APPEND CHIBI_SDK_TARGETS chibi_prototype_document)
    add_library(chibi_scene_document STATIC "${CHIBI_SDK_ROOT}/src/assets/scene_document.c"
        "${CHIBI_SDK_ROOT}/src/assets/physics_settings.c")
    add_library(Chibi::scene_document ALIAS chibi_scene_document)
    set_target_properties(chibi_scene_document PROPERTIES EXPORT_NAME scene_document)
    chibi_sdk_headers(chibi_scene_document PUBLIC assets/scene_document.h assets/physics_settings.h)
    target_link_libraries(chibi_scene_document PUBLIC chibi_ecs chibi_asset_values chibi_scene_runtime
        PRIVATE chibi_asset_text chibi_support)
    list(APPEND CHIBI_SDK_TARGETS chibi_scene_document)
    FetchContent_Declare(chibi_language_tree_sitter
        GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
        GIT_TAG v0.25.10 GIT_SHALLOW TRUE SOURCE_SUBDIR chibi-sources-only)
    FetchContent_MakeAvailable(chibi_language_tree_sitter)
    configure_file("${chibi_language_tree_sitter_SOURCE_DIR}/lib/include/tree_sitter/api.h"
        "${CHIBI_SDK_HEADER_ROOT}/syntax_runtime/tree_sitter/api.h" COPYONLY)
    install(FILES "${CHIBI_SDK_HEADER_ROOT}/syntax_runtime/tree_sitter/api.h"
        DESTINATION include/chibi/syntax_runtime/tree_sitter)
    install(FILES "${chibi_language_tree_sitter_SOURCE_DIR}/LICENSE"
        DESTINATION share/chibi/licenses/tree-sitter)
    add_library(chibi_syntax_runtime STATIC "${chibi_language_tree_sitter_SOURCE_DIR}/lib/src/lib.c")
    set_target_properties(chibi_syntax_runtime PROPERTIES EXPORT_NAME syntax_runtime)
    target_include_directories(chibi_syntax_runtime PUBLIC
        $<BUILD_INTERFACE:${CHIBI_SDK_HEADER_ROOT}/syntax_runtime>
        $<INSTALL_INTERFACE:include/chibi/syntax_runtime>)
    target_include_directories(chibi_syntax_runtime PRIVATE "${chibi_language_tree_sitter_SOURCE_DIR}/lib/src"
        "${chibi_language_tree_sitter_SOURCE_DIR}/lib/include")
    target_compile_definitions(chibi_syntax_runtime PRIVATE _DEFAULT_SOURCE _DARWIN_C_SOURCE)
    target_compile_options(chibi_syntax_runtime PRIVATE -w)
    include(ui.cmake)
    add_subdirectory("${CHIBI_SDK_ROOT}/src/language" language)
    add_library(chibi_compiler STATIC "${CHIBI_SDK_ROOT}/src/language/lower.c")
    add_library(Chibi::compiler ALIAS chibi_compiler)
    set_target_properties(chibi_compiler PROPERTIES EXPORT_NAME compiler)
    chibi_sdk_headers(chibi_compiler PUBLIC language/lower.h)
    target_link_libraries(chibi_compiler PUBLIC chibi_language chibi_execution)
    configure_file("${CHIBI_SDK_ROOT}/src/authoring/source_prepare_internal.h"
        "${CMAKE_CURRENT_BINARY_DIR}/private/authoring/authoring/source_prepare_internal.h" COPYONLY)
    add_library(chibi_authoring STATIC
        "${CHIBI_SDK_ROOT}/src/authoring/native_bindings.c"
        "${CHIBI_SDK_ROOT}/src/authoring/source_literal.c"
        "${CHIBI_SDK_ROOT}/src/authoring/source_references.c"
        "${CHIBI_SDK_ROOT}/src/authoring/source_prepare.c"
        "${CHIBI_SDK_ROOT}/src/authoring/source_link.c"
        "${CHIBI_SDK_ROOT}/src/authoring/source_format.c"
        "${CHIBI_SDK_ROOT}/src/authoring/source_metadata.c"
        "${CHIBI_SDK_ROOT}/src/authoring/source_navigation.c"
        "${CHIBI_SDK_ROOT}/src/authoring/source_prototype_flow.c"
        "${CHIBI_SDK_ROOT}/src/authoring/source_rename.c"
        "${CHIBI_SDK_ROOT}/src/authoring/source_intelligence.c")
    add_library(Chibi::authoring ALIAS chibi_authoring)
    set_target_properties(chibi_authoring PROPERTIES EXPORT_NAME authoring)
    chibi_sdk_headers(chibi_authoring PUBLIC authoring/source_prepare.h authoring/source_link.h
        authoring/source_format.h authoring/source_metadata.h authoring/source_navigation.h
        authoring/source_prototype_flow.h
        authoring/source_rename.h authoring/source_intelligence.h authoring/source_declaration.h
        authoring/native_bindings.h authoring/source_literal.h authoring/source_references.h)
    target_include_directories(chibi_authoring PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/authoring"
        "${CHIBI_SDK_ROOT}/third_party/yyjson")
    target_link_libraries(chibi_authoring PUBLIC chibi_compiler chibi_native_capabilities chibi_reference_index
        PRIVATE chibi_json chibi_asset_text chibi_input_action)
    list(APPEND CHIBI_SDK_TARGETS chibi_syntax_runtime chibi_language_syntax
        chibi_language chibi_compiler chibi_authoring)
    include(source_authoring.cmake)
    if(CHIBI_SDK_BUILD_NATIVE)
        include(project_preparation.cmake)
        include(asset_authoring.cmake)
        if(CHIBI_SDK_BUILD_DESKTOP)
            include(preview.cmake)
        endif()
    endif()
endif()
# `chibi PROJECT` prepares a changed project and plays it with its siblings.
if(TARGET chibi-compile AND TARGET chibi-player)
    add_executable(chibi "${CHIBI_SDK_ROOT}/src/host/chibi_main.c"
        "${CHIBI_SDK_ROOT}/src/host/export.c" "${CHIBI_SDK_ROOT}/src/host/process.c")
    foreach(header export process)
        configure_file("${CHIBI_SDK_ROOT}/src/host/${header}.h"
            "${CMAKE_CURRENT_BINARY_DIR}/private/launcher/host/${header}.h" COPYONLY)
    endforeach()
    target_include_directories(chibi PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/launcher"
        "${CHIBI_SDK_ROOT}/third_party/yyjson")
    target_link_libraries(chibi PRIVATE chibi_project_document chibi_support chibi_json)
    target_compile_options(chibi PRIVATE -Wall -Wextra -Werror)
    add_dependencies(chibi chibi-compile chibi-player)
    chibi_windows_manifest(chibi)
    install(TARGETS chibi RUNTIME DESTINATION bin)
endif()
foreach(target IN LISTS CHIBI_SDK_TARGETS)
    if(NOT target MATCHES "^(chibi_execution_contract|chibi_tasks|chibi_input_action|chibi_math|chibi_syntax_runtime|chibi_ui_syntax|chibi_json|chibi_fmt|chibi_box2d|chibi_box3d|chibi_task_scheduler|chibi_asset_kdl)$")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
        if(CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
            target_compile_options(${target} PRIVATE -Wno-experimental)
        endif()
    endif()
endforeach()
include(artifact_formats.cmake)
include(contracts.cmake)

install(TARGETS ${CHIBI_SDK_TARGETS}
    EXPORT ChibiTargets ARCHIVE DESTINATION lib)
install(EXPORT ChibiTargets NAMESPACE Chibi:: DESTINATION lib/cmake/Chibi)
include(CMakePackageConfigHelpers)
# Installed consumers must link the C runtime these libraries were built with.
set(CHIBI_MSVC_RUNTIME MultiThreaded)
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/ChibiConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/ChibiConfig.cmake"
    INSTALL_DESTINATION lib/cmake/Chibi)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/ChibiConfigVersion.cmake"
    VERSION ${CHIBI_SDK_VERSION} COMPATIBILITY ExactVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ChibiConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ChibiConfigVersion.cmake" DESTINATION lib/cmake/Chibi)
