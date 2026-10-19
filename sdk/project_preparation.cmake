# Offline asset preparation has no display host dependencies.
add_library(chibi_shader_preparation STATIC
    "${CHIBI_SDK_ROOT}/src/authoring/shader_prepare.c"
    "${CHIBI_SDK_ROOT}/src/authoring/parts_prepare.c"
    "${CHIBI_SDK_ROOT}/src/authoring/mesh_prepare.c"
    "${CHIBI_SDK_ROOT}/src/authoring/visual_prepare.c")
add_library(Chibi::shader_preparation ALIAS chibi_shader_preparation)
set_target_properties(chibi_shader_preparation PROPERTIES EXPORT_NAME shader_preparation)
chibi_sdk_headers(chibi_shader_preparation PUBLIC authoring/shader_prepare.h
    authoring/parts_prepare.h authoring/mesh_prepare.h authoring/visual_prepare.h)
target_include_directories(chibi_shader_preparation PRIVATE
    "${CHIBI_SDK_ROOT}/third_party/yyjson")
target_link_libraries(chibi_shader_preparation PUBLIC chibi_visual_catalog chibi_graph_model chibi_player
    PRIVATE chibi_graph_compilation chibi_graph_authoring chibi_asset_text chibi_json chibi_image_catalog)

# Players' pinned Vorbis decoder, compiled with private symbols, so preparation
# refuses an Ogg sound that players cannot decode.
file(READ "${CHIBI_CUTE_MATH_INCLUDE_DIR}/../libraries/stb/stb_vorbis.c" code)
string(REPLACE "stb_vorbis_" "chibi_sound_vorbis_" code "${code}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/private/sound_preparation/vendor/sound_vorbis.h" "${code}")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CHIBI_CUTE_MATH_INCLUDE_DIR}/../libraries/stb/stb_vorbis.c")
add_library(chibi_project_compilation STATIC)
foreach(unit component_declarations project_open project_prepare project_prepare_assets project_compile renderer_catalog prototype_prepare
    graph_sources native_graph_prepare source_graph_library material_prepare scene_prepare sound_prepare ui_prepare
    ui_prepare_assets)
    target_sources(chibi_project_compilation PRIVATE "${CHIBI_SDK_ROOT}/src/authoring/${unit}.c")
endforeach()
add_library(Chibi::project_compilation ALIAS chibi_project_compilation)
set_target_properties(chibi_project_compilation PROPERTIES EXPORT_NAME project_compilation)
chibi_sdk_headers(chibi_project_compilation PUBLIC authoring/component_declarations.h authoring/project_open.h authoring/project_compile.h authoring/renderer_catalog.h
    authoring/project_prepare.h authoring/prototype_prepare.h authoring/material_prepare.h authoring/scene_prepare.h
    authoring/sound_prepare.h authoring/source_graph_library.h)
foreach(header project_prepare_internal graph_sources native_graph_prepare ui_prepare)
    configure_file("${CHIBI_SDK_ROOT}/src/authoring/${header}.h"
        "${CMAKE_CURRENT_BINARY_DIR}/private/project_compilation/authoring/${header}.h" COPYONLY)
endforeach()
target_include_directories(chibi_project_compilation PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/project_compilation")
target_include_directories(chibi_project_compilation SYSTEM PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/private/sound_preparation")
target_link_libraries(chibi_project_compilation PUBLIC chibi_source_catalog chibi_prototype_document chibi_project_document
    chibi_scene_document chibi_player chibi_native_prepared chibi_shader_preparation
    PRIVATE chibi_graph_catalog chibi_host_native_default chibi_graph_compilation chibi_graph_authoring chibi_asset_authoring
    chibi_asset_text chibi_source_presentation chibi_text_source chibi_ui_authoring chibi_builtin_fonts chibi_image_preparation chibi_json)
list(APPEND CHIBI_SDK_TARGETS chibi_shader_preparation chibi_project_compilation)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Emscripten" OR CHIBI_WEB_PLAYER)
    add_executable(chibi-compile "${CHIBI_SDK_ROOT}/src/host/compile_main.c")
    target_link_libraries(chibi-compile PRIVATE Chibi::project_compilation)
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
