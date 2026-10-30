# Private syntax facts for authored tools; no parser capability in game catalogs.
add_library(chibi_compiler_syntax STATIC "${CHIBI_SDK_ROOT}/src/toolchain/compiler_syntax.c")
add_library(Chibi::compiler_syntax ALIAS chibi_compiler_syntax)
set_target_properties(chibi_compiler_syntax PROPERTIES EXPORT_NAME compiler_syntax)
configure_file("${CHIBI_SDK_ROOT}/src/toolchain/compiler_syntax.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/compiler-syntax/toolchain/compiler_syntax.h" COPYONLY)
target_include_directories(chibi_compiler_syntax PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/compiler-syntax")
target_link_libraries(chibi_compiler_syntax PRIVATE chibi_language_syntax chibi_native_capabilities)
# The compiler's JSON string reader, for package tests of the compiler's packages.
add_library(chibi_compiler_json STATIC "${CHIBI_SDK_ROOT}/src/toolchain/compiler_json.c")
add_library(Chibi::compiler_json ALIAS chibi_compiler_json)
set_target_properties(chibi_compiler_json PROPERTIES EXPORT_NAME compiler_json)
configure_file("${CHIBI_SDK_ROOT}/src/toolchain/compiler_json.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/compiler-json/toolchain/compiler_json.h" COPYONLY)
target_include_directories(chibi_compiler_json PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/compiler-json")
target_link_libraries(chibi_compiler_json PRIVATE chibi_json chibi_native_capabilities)

# Offline asset preparation has no display host dependencies.
add_library(chibi_shader_preparation STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/parts_prepare.c"
    "${CHIBI_SDK_ROOT}/src/authoring/mesh_prepare.c")
add_library(Chibi::shader_preparation ALIAS chibi_shader_preparation)
set_target_properties(chibi_shader_preparation PROPERTIES EXPORT_NAME shader_preparation)
chibi_sdk_headers(chibi_shader_preparation PUBLIC
    authoring/parts_prepare.h authoring/mesh_prepare.h)
target_include_directories(chibi_shader_preparation PRIVATE
    "${CHIBI_SDK_ROOT}/third_party/yyjson")
target_link_libraries(chibi_shader_preparation PUBLIC chibi_visual_catalog chibi_player
    PRIVATE chibi_asset_text chibi_json chibi_image_catalog)

# Players' pinned Vorbis decoder, compiled with private symbols, so preparation
# refuses an Ogg sound that players cannot decode.
file(READ "${CHIBI_THIRD_PARTY_DIR}/source/cute_framework/libraries/stb/stb_vorbis.c" code)
string(REPLACE "stb_vorbis_" "chibi_sound_vorbis_" code "${code}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/private/sound_preparation/vendor/sound_vorbis.h" "${code}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CHIBI_THIRD_PARTY_DIR}/source/cute_framework/libraries/stb/stb_vorbis.c")
add_library(chibi_project_compilation STATIC)
foreach(unit renderer_catalog)
    target_sources(chibi_project_compilation PRIVATE "${CHIBI_SDK_ROOT}/src/authoring/${unit}.c")
endforeach()
add_library(Chibi::project_compilation ALIAS chibi_project_compilation)
set_target_properties(chibi_project_compilation PROPERTIES EXPORT_NAME project_compilation)
chibi_sdk_headers(chibi_project_compilation PUBLIC authoring/project_compile.h authoring/renderer_catalog.h)
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    configure_file("${CHIBI_SDK_ROOT}/src/authoring/asset_compiler_host_internal.h"
        "${CMAKE_CURRENT_BINARY_DIR}/private/project_compilation/authoring/asset_compiler_host_internal.h" COPYONLY)
    target_link_libraries(chibi_project_compilation PRIVATE chibi_asset_compiler_host)
endif()
configure_file("${CHIBI_SDK_ROOT}/src/toolchain/compiler_syntax.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/project_compilation/toolchain/compiler_syntax.h" COPYONLY)
configure_file("${CHIBI_SDK_ROOT}/src/toolchain/compiler_json.h"
    "${CMAKE_CURRENT_BINARY_DIR}/private/project_compilation/toolchain/compiler_json.h" COPYONLY)
target_include_directories(chibi_project_compilation PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/project_compilation")
target_include_directories(chibi_project_compilation SYSTEM PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/sound_preparation")
target_link_libraries(chibi_project_compilation PUBLIC chibi_source_catalog
    chibi_player chibi_native_prepared chibi_shader_preparation
    PRIVATE chibi_host_native_default
    chibi_asset_text chibi_text_source chibi_ui_authoring chibi_builtin_fonts chibi_image_preparation chibi_json
    chibi_compiler_syntax chibi_compiler_json chibi_text_capabilities chibi_array_capabilities chibi_task_pool_runtime)
list(APPEND CHIBI_SDK_TARGETS chibi_compiler_syntax chibi_compiler_json chibi_shader_preparation chibi_project_compilation)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR CHIBI_WEB_PLAYER)
    add_executable(chibi-compile "${CHIBI_SDK_ROOT}/src/host/compile_main.c" "${CHIBI_SDK_ROOT}/src/host/compile_tests.c"
        "${CHIBI_SDK_ROOT}/src/toolchain/compiler_target.c" "${CHIBI_SDK_ROOT}/src/toolchain/compiler_artifact.c")
    foreach(header compiler_target compiler_artifact)
        configure_file("${CHIBI_SDK_ROOT}/src/toolchain/${header}.h"
            "${CMAKE_CURRENT_BINARY_DIR}/private/compiler-target/toolchain/${header}.h" COPYONLY)
    endforeach()
    target_include_directories(chibi-compile PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/compiler-target")
    target_link_libraries(chibi-compile PRIVATE Chibi::project_compilation
        chibi_behavior_runtime chibi_asset_json chibi_host_native_default chibi_compiler_syntax chibi_compiler_json
        chibi_task_pool_runtime)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
        target_link_libraries(chibi-compile PRIVATE chibi_asset_compiler_host)
    endif()
endif()
if(CHIBI_WEB_PLAYER)
    # Prepared artifacts carry their target's ABI, so `chibi export --web` runs
    # this browser-target compiler under Node to prepare the game.
    set_target_properties(chibi-compile PROPERTIES SUFFIX ".js")
    target_link_options(chibi-compile PRIVATE -sENVIRONMENT=node -sNODERAWFS=1 -sEXIT_RUNTIME=1
        -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=1048576)
    install(FILES "$<TARGET_FILE:chibi-compile>" "$<TARGET_FILE_DIR:chibi-compile>/chibi-compile.wasm"
        DESTINATION compiler COMPONENT web_player EXCLUDE_FROM_ALL)
elseif(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten")
    chibi_windows_manifest(chibi-compile)
    chibi_install_product(chibi-compile)
endif()
